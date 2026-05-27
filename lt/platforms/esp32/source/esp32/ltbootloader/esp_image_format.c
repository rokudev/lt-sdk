/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <sys/param.h>
#include <soc/cpu.h>
#include <bootloader_utility.h>
#include <esp_secure_boot.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_spi_flash.h>
#include <bootloader_flash_priv.h>
#include <bootloader_random.h>
#include <bootloader_sha.h>
#include "bootloader_util.h"
#include "bootloader_common.h"
#include "esp_rom_sys.h"
#include "soc/soc_memory_types.h"
#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp32h2/rom/secure_boot.h"
#endif

#include "LTBootPlatform.h"
#include "LTBootInternal.h"

static const char *TAG = "esp_image";

#define HASH_LEN ESP_IMAGE_HASH_LEN

#define SIXTEEN_MB 0x1000000
#define ESP_ROM_CHECKSUM_INITIAL 0xEF

/* Headroom to ensure between stack SP (at time of checking) and data loaded from flash */
#define STACK_LOAD_HEADROOM 32768

/* 64 bits of random data to obfuscate loaded RAM with, until verification is complete
   (Means loaded code isn't executable until after the secure boot check.)
*/
static uint32_t ram_obfs_value[2];

/* Return true if load_addr is an address the bootloader should load into */
static bool should_load(uint32_t load_addr);
/* Return true if load_addr is an address the bootloader should map via flash cache */
static bool should_map(uint32_t load_addr);

static esp_err_t process_segments(esp_image_metadata_t *data, bool silent, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum);
/* Load or verify a segment */
static esp_err_t process_segment(int index, uint32_t flash_addr, esp_image_segment_header_t *header, bool silent, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum);

/* split segment and verify if data_len is too long */
static esp_err_t process_segment_data(intptr_t load_addr, uint32_t data_addr, uint32_t data_len, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum);

/* Verify the main image header */
static esp_err_t verify_image_header(uint32_t src_addr, const esp_image_header_t *image, bool silent);

/* Verify a segment header */
static esp_err_t verify_segment_header(int index, const esp_image_segment_header_t *segment, uint32_t segment_data_offs, bool silent);

/* Log-and-fail macro for use in esp_image_load */
#define FAIL_LOAD(...) do {                         \
        if (!silent) {                              \
            ESP_LOGE(TAG, __VA_ARGS__);             \
        }                                           \
        goto err;                                   \
    }                                               \
    while(0)

#define CHECK_ERR(func) do {                        \
        if ((err = func) != ESP_OK) {               \
            goto err;                               \
        }                                           \
    }                                               \
    while(0)

static esp_err_t process_image_header(esp_image_metadata_t *data, uint32_t part_offset, bootloader_sha256_handle_t *sha_handle, bool do_verify, bool silent);
static esp_err_t process_appended_hash(esp_image_metadata_t *data, uint32_t part_len, bool do_verify, bool silent);
static esp_err_t process_checksum(bootloader_sha256_handle_t sha_handle, uint32_t checksum_word, esp_image_metadata_t *data, bool silent, bool skip_check_checksum);

static esp_err_t __attribute__((unused)) verify_secure_boot_signature(bootloader_sha256_handle_t sha_handle, esp_image_metadata_t *data, uint8_t *image_digest, uint8_t *verified_digest);

esp_err_t CheckAndOrLoadImage(esp_image_load_mode_t mode, const esp_partition_pos_t *part, esp_image_metadata_t *data) {
    bool do_load   = (mode == ESP_IMAGE_LOAD) || (mode == ESP_IMAGE_LOAD_NO_VALIDATE);
    bool do_verify = (mode == ESP_IMAGE_LOAD) || (mode == ESP_IMAGE_VERIFY) || (mode == ESP_IMAGE_VERIFY_SILENT);
    bool silent    = (mode == ESP_IMAGE_VERIFY_SILENT);

    esp_err_t err = ESP_OK;
    // checksum the image a word at a time. This shaves 30-40ms per MB of image size
    uint32_t checksum_word = ESP_ROM_CHECKSUM_INITIAL;
    uint32_t *checksum = (do_verify) ? &checksum_word : NULL;
    bootloader_sha256_handle_t sha_handle = NULL;
     /* used for anti-FI checks */
    uint8_t image_digest[HASH_LEN] = { [ 0 ... 31] = 0xEE };
    uint8_t verified_digest[HASH_LEN] = { [ 0 ... 31 ] = 0x01 };
    // For Secure Boot V2, we do verify signature on bootloader which includes the SHA calculation.
    bool verify_sha = do_verify;

    if (data == NULL || part == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (part->size > SIXTEEN_MB) {
        err = ESP_ERR_INVALID_ARG;
        FAIL_LOAD("partition size 0x%x invalid, larger than 16MB", part->size);
    }

    bootloader_sha256_handle_t *p_sha_handle = &sha_handle;
    CHECK_ERR(process_image_header(data, part->offset, (verify_sha) ? p_sha_handle : NULL, do_verify, silent));
    CHECK_ERR(process_segments(data, silent, do_load, sha_handle, checksum));
    bool skip_check_checksum = !do_verify;
    CHECK_ERR(process_checksum(sha_handle, checksum_word, data, silent, skip_check_checksum));
    CHECK_ERR(process_appended_hash(data, part->size, do_verify, silent));
    if (verify_sha) {
        /* NB: This does not return if check fails */
        err = verify_secure_boot_signature(sha_handle, data, image_digest, verified_digest);
        sha_handle = NULL; // verify_secure_boot_signature finishes sha_handle
    } // verify_sha

    // bootloader may still have a sha256 digest handle open
    if (sha_handle != NULL) {
        bootloader_sha256_finish(sha_handle, NULL);
        sha_handle = NULL;
    }

    /* If signature was checked in bootloader build, verified_digest should equal image_digest
       This is to detect any fault injection that caused signature verification to not complete normally.
       Any attack which bypasses this check should be of limited use as the RAM contents are still obfuscated,
       therefore we do the check immediately before we deobfuscate. */
    LTBootPlatform_SecurityAssert(IsSecureBootDisabled() || (memcmp(image_digest, verified_digest, HASH_LEN) == 0) || LTBoot_CheckLTATClaims(kLTSecurityClaimGroup1_BypassAppSignature) == kLTBootSecurityCheck_Pass);

    // Deobfuscate RAM
    if (do_load && ram_obfs_value[0] != 0 && ram_obfs_value[1] != 0) {
        for (int i = 0; i < data->image.segment_count; i++) {
            uint32_t load_addr = data->segments[i].load_addr;
            if (should_load(load_addr)) {
                uint32_t *loaded = (uint32_t *)load_addr;
                for (size_t j = 0; j < data->segments[i].data_len / sizeof(uint32_t); j++) {
                    loaded[j] ^= (j & 1) ? ram_obfs_value[0] : ram_obfs_value[1];
                }
            }
        }
    }

    // Success!
    return ESP_OK;

err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }
    if (sha_handle != NULL) {
        // Need to finish the hash process to free the handle
        bootloader_sha256_finish(sha_handle, NULL);
    }
    // Prevent invalid/incomplete data leaking out
    bzero(data, sizeof(esp_image_metadata_t));
    return err;
}

static esp_err_t verify_image_header(uint32_t src_addr, const esp_image_header_t *image, bool silent)
{
    esp_err_t err = ESP_OK;

    ESP_LOGD(TAG, "image header: 0x%02x 0x%02x 0x%02x 0x%02x %08x",
                image->magic,
                image->segment_count,
                image->spi_mode,
                image->spi_size,
                image->entry_addr);

    if (image->magic != ESP_IMAGE_HEADER_MAGIC) {
        FAIL_LOAD("image at 0x%x has invalid magic byte (nothing flashed here?)", src_addr);
    }

    if (image->segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        FAIL_LOAD("image at 0x%x segment count %d exceeds max %d", src_addr, image->segment_count, ESP_IMAGE_MAX_SEGMENTS);
    }
    return err;
err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }
    return err;
}

/* Check the region load_addr - load_end doesn't overlap any memory used by the bootloader, registers, or other invalid memory
 */
static bool verify_load_addresses(int segment_index, intptr_t load_addr, intptr_t load_end, bool print_error, bool no_recurse)
{
    /* Addresses of static data and the "loader" section of bootloader IRAM, all defined in ld script */
    const char *reason = NULL;
    extern int _dram_start, _dram_end, _loader_text_start, _loader_text_end;
    void *load_addr_p = (void *)load_addr;
    void *load_inclusive_end_p = (void *)((uint8_t *)load_end - 0x1);
    void *load_exclusive_end_p = (void *)load_end;

    if (load_end == load_addr) {
        return true; // zero-length segments are fine
    }
    assert(load_end > load_addr); // data_len<16MB is checked in verify_segment_header() which is called before this, so this should always be true

    if (esp_ptr_in_dram(load_addr_p) && esp_ptr_in_dram(load_inclusive_end_p)) { /* Writing to DRAM */
        /* Check if we're clobbering the stack */
        intptr_t sp = (intptr_t)esp_cpu_get_sp();
        if (bootloader_util_regions_overlap(sp - STACK_LOAD_HEADROOM, SOC_ROM_STACK_START,
                                           load_addr, load_end)) {
            reason = "overlaps bootloader stack";
            goto invalid;
        }

        /* Check if we're clobbering static data

           (_dram_start.._dram_end includes bss, data, rodata sections in DRAM)
         */
        if (bootloader_util_regions_overlap((intptr_t)&_dram_start, (intptr_t)&_dram_end, load_addr, load_end)) {
            reason = "overlaps bootloader data";
            goto invalid;
        }

        /* LAST DRAM CHECK (recursive): for D/IRAM, check the equivalent IRAM addresses if needed

           Allow for the possibility that even though both pointers are IRAM, only part of the region is in a D/IRAM
           section. In which case we recurse to check the part which falls in D/IRAM.

           Note: We start with SOC_DIRAM_DRAM_LOW/HIGH and convert that address to IRAM to account for any reversing of word order
           (chip-specific).
         */
        if (!no_recurse && bootloader_util_regions_overlap(SOC_DIRAM_DRAM_LOW, SOC_DIRAM_DRAM_HIGH, load_addr, load_end)) {
            intptr_t iram_load_addr, iram_load_end;

            if (esp_ptr_in_diram_dram(load_addr_p)) {
                iram_load_addr = (intptr_t)esp_ptr_diram_dram_to_iram(load_addr_p);
            } else {
                iram_load_addr = (intptr_t)esp_ptr_diram_dram_to_iram((void *)SOC_DIRAM_DRAM_LOW);
            }

            if (esp_ptr_in_diram_dram(load_inclusive_end_p)) {
                iram_load_end = (intptr_t)esp_ptr_diram_dram_to_iram(load_exclusive_end_p);
            } else {
                iram_load_end = (intptr_t)esp_ptr_diram_dram_to_iram((void *)SOC_DIRAM_DRAM_HIGH);
            }

            if (iram_load_end < iram_load_addr) {
                return verify_load_addresses(segment_index, iram_load_end, iram_load_addr, print_error, true);
            } else {
                return verify_load_addresses(segment_index, iram_load_addr, iram_load_end, print_error, true);
            }
        }
    }
    else if (esp_ptr_in_iram(load_addr_p) && esp_ptr_in_iram(load_inclusive_end_p)) { /* Writing to IRAM */
        /* Check for overlap of 'loader' section of IRAM */
        if (bootloader_util_regions_overlap((intptr_t)&_loader_text_start, (intptr_t)&_loader_text_end,
                                            load_addr, load_end)) {
            reason = "overlaps loader IRAM";
            goto invalid;
        }

        /* LAST IRAM CHECK (recursive): for D/IRAM, check the equivalent DRAM address if needed

           Allow for the possibility that even though both pointers are IRAM, only part of the region is in a D/IRAM
           section. In which case we recurse to check the part which falls in D/IRAM.
           Note: We start with SOC_DIRAM_IRAM_LOW/HIGH and convert that address to DRAM to account for any reversing of word order
           (chip-specific).
         */
        if (!no_recurse && bootloader_util_regions_overlap(SOC_DIRAM_IRAM_LOW, SOC_DIRAM_IRAM_HIGH, load_addr, load_end)) {
            intptr_t dram_load_addr, dram_load_end;

            if (esp_ptr_in_diram_iram(load_addr_p)) {
                dram_load_addr = (intptr_t)esp_ptr_diram_iram_to_dram(load_addr_p);
            } else {
                dram_load_addr = (intptr_t)esp_ptr_diram_iram_to_dram((void *)SOC_DIRAM_IRAM_LOW);
            }

            if (esp_ptr_in_diram_iram(load_inclusive_end_p)) {
                dram_load_end = (intptr_t)esp_ptr_diram_iram_to_dram(load_exclusive_end_p);
            } else {
                dram_load_end = (intptr_t)esp_ptr_diram_iram_to_dram((void *)SOC_DIRAM_IRAM_HIGH);
            }

            if (dram_load_end < dram_load_addr) {
                return verify_load_addresses(segment_index, dram_load_end, dram_load_addr, print_error, true);
            } else {
                return verify_load_addresses(segment_index, dram_load_addr, dram_load_end, print_error, true);
            }
        }
    /* Sections entirely in RTC memory won't overlap with a vanilla bootloader but are valid load addresses, thus skipping them from the check */
    } else if (esp_ptr_in_rtc_iram_fast(load_addr_p) && esp_ptr_in_rtc_iram_fast(load_inclusive_end_p)){
        return true;
    } else if (esp_ptr_in_rtc_dram_fast(load_addr_p) && esp_ptr_in_rtc_dram_fast(load_inclusive_end_p)){
        return true;
    } else if (esp_ptr_in_rtc_slow(load_addr_p) && esp_ptr_in_rtc_slow(load_inclusive_end_p)) {
        return true;
    } else { /* Not a DRAM or an IRAM or RTC Fast IRAM, RTC Fast DRAM or RTC Slow address */
        reason = "bad load address range";
        goto invalid;
    }
    return true;

 invalid:
    if (print_error) {
        ESP_LOGE(TAG, "Segment %d 0x%08x-0x%08x invalid: %s", segment_index, load_addr, load_end, reason);
    }
    return false;
}

static esp_err_t process_image_header(esp_image_metadata_t *data, uint32_t part_offset, bootloader_sha256_handle_t *sha_handle, bool do_verify, bool silent)
{
    esp_err_t err;
    bzero(data, sizeof(esp_image_metadata_t));
    data->start_addr = part_offset;

    ESP_LOGD(TAG, "reading image header @ 0x%x", data->start_addr);
    CHECK_ERR(bootloader_flash_read(data->start_addr, &data->image, sizeof(esp_image_header_t), true));

    if (do_verify) {
        // Calculate SHA-256 of image if secure boot is on, or if image has a hash appended
        if (sha_handle != NULL) {
            *sha_handle = bootloader_sha256_start();
            if (*sha_handle == NULL) {
                return ESP_ERR_NO_MEM;
            }
            bootloader_sha256_data(*sha_handle, &data->image, sizeof(esp_image_header_t));
        }
        CHECK_ERR(verify_image_header(data->start_addr, &data->image, silent));
    }
    data->image_len = sizeof(esp_image_header_t);
    return ESP_OK;
err:
    return err;
}

static esp_err_t process_segments(esp_image_metadata_t *data, bool silent, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum)
{
    esp_err_t err = ESP_OK;
    uint32_t start_segments = data->start_addr + data->image_len;
    uint32_t next_addr = start_segments;
    for (int i = 0; i < data->image.segment_count; i++) {
        esp_image_segment_header_t *header = &data->segments[i];
        ESP_LOGV(TAG, "loading segment header %d at offset 0x%x", i, next_addr);
        CHECK_ERR(process_segment(i, next_addr, header, silent, do_load, sha_handle, checksum));
        next_addr += sizeof(esp_image_segment_header_t);
        data->segment_data[i] = next_addr;
        next_addr += header->data_len;
    }
    // Segments all loaded, verify length
    uint32_t end_addr = next_addr;
    if (end_addr < data->start_addr) {
        FAIL_LOAD("image offset has wrapped");
    }

    data->image_len += end_addr - start_segments;
    ESP_LOGV(TAG, "image start 0x%08x end of last section 0x%08x", data->start_addr, end_addr);
    return err;
err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }
    return err;
}

static esp_err_t process_segment(int index, uint32_t flash_addr, esp_image_segment_header_t *header, bool silent, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum)
{
    esp_err_t err;

    /* read segment header */
    err = bootloader_flash_read(flash_addr, header, sizeof(esp_image_segment_header_t), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bootloader_flash_read failed at 0x%08x", flash_addr);
        return err;
    }
    if (sha_handle != NULL) {
        bootloader_sha256_data(sha_handle, header, sizeof(esp_image_segment_header_t));
    }

    intptr_t load_addr = header->load_addr;
    uint32_t data_len = header->data_len;
    uint32_t data_addr = flash_addr + sizeof(esp_image_segment_header_t);

    ESP_LOGV(TAG, "segment data length 0x%x data starts 0x%x", data_len, data_addr);

    CHECK_ERR(verify_segment_header(index, header, data_addr, silent));

    if (data_len % 4 != 0) {
        FAIL_LOAD("unaligned segment length 0x%x", data_len);
    }

    bool is_mapping = should_map(load_addr);
    (void)is_mapping;
    do_load = do_load && should_load(load_addr);

    if (!silent) {
        ESP_LOGI(TAG, "segment %d: paddr=%08x vaddr=%08x size=%05xh (%6d) %s",
                 index, data_addr, load_addr,
                 data_len, data_len,
                 (do_load) ? "load" : (is_mapping) ? "map" : "");
    }


    /* Before loading segment, check it doesn't clobber bootloader RAM. */
    if (do_load && data_len > 0) {
        if (!verify_load_addresses(index, load_addr, load_addr + data_len, true, false)) {
            return ESP_ERR_IMAGE_INVALID;
        }
    }

    uint32_t free_page_count = bootloader_mmap_get_free_pages();
    ESP_LOGD(TAG, "free data page_count 0x%08x", free_page_count);

    uint32_t data_len_remain = data_len;
    while (data_len_remain > 0) {
        /* Double check the address verification done above */
        LTBootPlatform_SecurityAssert(!do_load || verify_load_addresses(0, load_addr, load_addr + data_len_remain, false, false));
        uint32_t offset_page = ((data_addr & MMAP_ALIGNED_MASK) != 0) ? 1 : 0;
        /* Data we could map in case we are not aligned to PAGE boundary is one page size lesser. */
        data_len = MIN(data_len_remain, ((free_page_count - offset_page) * SPI_FLASH_MMU_PAGE_SIZE));
        CHECK_ERR(process_segment_data(load_addr, data_addr, data_len, do_load, sha_handle, checksum));
        data_addr += data_len;
        data_len_remain -= data_len;
    }

    return ESP_OK;

err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }

    return err;
}

static esp_err_t process_segment_data(intptr_t load_addr, uint32_t data_addr, uint32_t data_len, bool do_load, bootloader_sha256_handle_t sha_handle, uint32_t *checksum)
{
    // If we are not loading, and the checksum is empty, skip processing this
    // segment for data
    if (!do_load && checksum == NULL) {
        ESP_LOGD(TAG, "skipping checksum for segment");
        return ESP_OK;
    }

    const uint32_t *data = (const uint32_t *)bootloader_mmap(data_addr, data_len);
    if (!data) {
        ESP_LOGE(TAG, "bootloader_mmap(0x%x, 0x%x) failed",
                 data_addr, data_len);
        return ESP_FAIL;
    }

    if (checksum == NULL && sha_handle == NULL) {
        memcpy((void *)load_addr, data, data_len);
        bootloader_munmap(data);
        return ESP_OK;
    }

    // Set up the obfuscation value to use for loading
    while (ram_obfs_value[0] == 0 || ram_obfs_value[1] == 0) {
        bootloader_fill_random(ram_obfs_value, sizeof(ram_obfs_value));
    }
    uint32_t *dest = (uint32_t *)load_addr;

    const uint32_t *src = data;

    for (size_t i = 0; i < data_len; i += 4) {
        int w_i = i / 4; // Word index
        uint32_t w = src[w_i];
        if (checksum != NULL) {
            *checksum ^= w;
        }
        if (do_load) {
            dest[w_i] = w ^ ((w_i & 1) ? ram_obfs_value[0] : ram_obfs_value[1]);
        }
        // SHA_CHUNK determined experimentally as the optimum size
        // to call bootloader_sha256_data() with. This is a bit
        // counter-intuitive, but it's ~3ms better than using the
        // SHA256 block size.
        const size_t SHA_CHUNK = 1024;
        if (sha_handle != NULL && i % SHA_CHUNK == 0) {
            bootloader_sha256_data(sha_handle, &src[w_i],
                                   MIN(SHA_CHUNK, data_len - i));
        }
    }

    bootloader_munmap(data);

    return ESP_OK;
}

static esp_err_t verify_segment_header(int index, const esp_image_segment_header_t *segment, uint32_t segment_data_offs, bool silent)
{
    if ((segment->data_len & 3) != 0
            || segment->data_len >= SIXTEEN_MB) {
        if (!silent) {
            ESP_LOGE(TAG, "invalid segment length 0x%x", segment->data_len);
        }
        return ESP_ERR_IMAGE_INVALID;
    }

    uint32_t load_addr = segment->load_addr;
    bool map_segment = should_map(load_addr);

    /* Check that flash cache mapped segment aligns correctly from flash to its mapped address,
       relative to the 64KB page mapping size.
    */
    ESP_LOGV(TAG, "segment %d map_segment %d segment_data_offs 0x%x load_addr 0x%x",
             index, map_segment, segment_data_offs, load_addr);
    if (map_segment
            && ((segment_data_offs % SPI_FLASH_MMU_PAGE_SIZE) != (load_addr % SPI_FLASH_MMU_PAGE_SIZE))) {
        if (!silent) {
            ESP_LOGE(TAG, "Segment %d load address 0x%08x, doesn't match data 0x%08x",
                     index, load_addr, segment_data_offs);
        }
        return ESP_ERR_IMAGE_INVALID;
    }

    return ESP_OK;
}

static bool should_map(uint32_t load_addr)
{
    return (load_addr >= SOC_IROM_LOW && load_addr < SOC_IROM_HIGH)
           || (load_addr >= SOC_DROM_LOW && load_addr < SOC_DROM_HIGH);
}

static bool should_load(uint32_t load_addr)
{
    /* Reload the RTC memory segments whenever a non-deepsleep reset
       is occurring */
    bool load_rtc_memory = esp_rom_get_reset_reason(0) != RESET_REASON_CORE_DEEP_SLEEP;

    if (should_map(load_addr)) {
        return false;
    }

    if (load_addr < 0x10000000) {
        // Reserved for non-loaded addresses.
        // Current reserved values are
        // 0x0 (padding block)
        // 0x4 (unused, but reserved for an MD5 block)
        return false;
    }

    if (!load_rtc_memory) {
        if (load_addr >= SOC_RTC_IRAM_LOW && load_addr < SOC_RTC_IRAM_HIGH) {
            ESP_LOGD(TAG, "Skipping RTC fast memory segment at 0x%08x", load_addr);
            return false;
        }
        if (load_addr >= SOC_RTC_DRAM_LOW && load_addr < SOC_RTC_DRAM_HIGH) {
            ESP_LOGD(TAG, "Skipping RTC fast memory segment at 0x%08x", load_addr);
            return false;
        }
        if (load_addr >= SOC_RTC_DATA_LOW && load_addr < SOC_RTC_DATA_HIGH) {
            ESP_LOGD(TAG, "Skipping RTC slow memory segment at 0x%08x", load_addr);
            return false;
        }
    }

    return true;
}

esp_err_t esp_image_verify_bootloader_data(esp_image_metadata_t *data)
{
    const esp_partition_pos_t bootloader_part = {
        .offset = ESP_BOOTLOADER_OFFSET,
        .size   = ESP_PARTITION_TABLE_OFFSET - ESP_BOOTLOADER_OFFSET,
    };
    return CheckAndOrLoadImage(ESP_IMAGE_VERIFY, &bootloader_part, data);
}

static esp_err_t process_appended_hash(esp_image_metadata_t *data, uint32_t part_len, bool do_verify, bool silent)
{
    esp_err_t err = ESP_OK;
    if (data->image.hash_appended) {
        // Account for the hash in the total image length
        if (do_verify) {
            CHECK_ERR(bootloader_flash_read(data->start_addr + data->image_len, &data->image_digest, HASH_LEN, true));
        }
        data->image_len += HASH_LEN;
    }

    if (data->image_len > part_len) {
        FAIL_LOAD("Image length %d doesn't fit in partition length %d", data->image_len, part_len);
    }
    return err;
err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }

    return err;
}

static esp_err_t process_checksum(bootloader_sha256_handle_t sha_handle, uint32_t checksum_word, esp_image_metadata_t *data, bool silent, bool skip_check_checksum)
{
    esp_err_t err = ESP_OK;
    uint32_t unpadded_length = data->image_len;
    uint32_t length = unpadded_length + 1; // Add a byte for the checksum
    length = (length + 15) & ~15; // Pad to next full 16 byte block
    length = length - unpadded_length;

    // Verify checksum
    WORD_ALIGNED_ATTR uint8_t buf[16];
    if (!skip_check_checksum || sha_handle != NULL) {
        CHECK_ERR(bootloader_flash_read(data->start_addr + unpadded_length, buf, length, true));
    }
    uint8_t read_checksum = buf[length - 1];
    uint8_t calc_checksum = (checksum_word >> 24) ^ (checksum_word >> 16) ^ (checksum_word >> 8) ^ (checksum_word >> 0);
    if (!skip_check_checksum && calc_checksum != read_checksum) {
        FAIL_LOAD("Checksum failed. Calculated 0x%x read 0x%x", calc_checksum, read_checksum);
    }
    if (sha_handle != NULL) {
        bootloader_sha256_data(sha_handle, buf, length);
    }
    data->image_len += length;

    return err;
err:
    if (err == ESP_OK) {
        err = ESP_ERR_IMAGE_INVALID;
    }

    return err;
}

static esp_err_t verify_secure_boot_signature(bootloader_sha256_handle_t sha_handle, esp_image_metadata_t *data, uint8_t *image_digest, uint8_t *verified_digest)
{
    uint32_t end = data->start_addr + data->image_len;

    ESP_LOGI(TAG, "Verifying image signature...");

    // For secure boot, we calculate the signature hash over the whole file, which includes any "simple" hash
    // appended to the image for corruption detection
    if (data->image.hash_appended) {
        const void *simple_hash = bootloader_mmap(end - HASH_LEN, HASH_LEN);
        bootloader_sha256_data(sha_handle, simple_hash, HASH_LEN);
        bootloader_munmap(simple_hash);
    }

#if CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME
    // End of the image needs to be padded all the way to a 4KB boundary, after the simple hash
    // (for apps they are usually already padded due to --secure-pad-v2, only a problem if this option was not used.)
    uint32_t padded_end = (end + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE-1);
    if (padded_end > end) {
        const void *padding = bootloader_mmap(end, padded_end - end);
        bootloader_sha256_data(sha_handle, padding, padded_end - end);
        bootloader_munmap(padding);
        end = padded_end;
    }
#endif

    bootloader_sha256_finish(sha_handle, image_digest);

    // Use hash to verify signature block
    esp_err_t err = ESP_ERR_IMAGE_INVALID;
#if defined(CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME) || defined(CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME)
    const void *sig_block;
    LTBootPlatform_SecurityAssert(memcmp(image_digest, verified_digest, HASH_LEN) != 0); /* sanity check that these values start differently */
#ifdef CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME
    sig_block = bootloader_mmap(data->start_addr + data->image_len, sizeof(esp_secure_boot_sig_block_t));
    err = esp_secure_boot_verify_ecdsa_signature_block(sig_block, image_digest, verified_digest);
#else
    sig_block = bootloader_mmap(end, sizeof(ets_secure_boot_signature_t));
    err = esp_secure_boot_verify_rsa_signature_block(sig_block, image_digest, verified_digest);
#endif
    bootloader_munmap(sig_block);
#endif // CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME or CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Secure boot signature verification failed");
        if (IsSecureBootDisabled()) {
            LTBootPlatform_SecurityAssert(IsSecureBootDisabled());
        } else if (LTBoot_CheckLTATClaims(kLTSecurityClaimGroup1_BypassAppSignature) != kLTBootSecurityCheck_Pass) {
            LTBootPlatform_SecurityAbortWithFailover();
        }
    }

#if CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME
    // Adjust image length result to include the appended signature
    data->image_len = end - data->start_addr + sizeof(ets_secure_boot_signature_t);
#endif

    return ESP_OK;
}

