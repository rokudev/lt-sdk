/*
 * SPDX-FileCopyrightText: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/param.h>

#include "esp_attr.h"
#include "esp_log.h"

#include "esp_rom_sys.h"
#include "esp_rom_uart.h"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32
#include "soc/dport_reg.h"
#include "esp32/rom/cache.h"
#include "esp32/rom/spi_flash.h"
#include "esp32/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/cache.h"
#include "esp32s2/rom/spi_flash.h"
#include "esp32s2/rom/secure_boot.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/cache.h"
#include "esp32s3/rom/spi_flash.h"
#include "esp32s3/rom/secure_boot.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/cache.h"
#include "esp32c3/rom/efuse.h"
#include "esp32c3/rom/ets_sys.h"
#include "esp32c3/rom/spi_flash.h"
#include "esp32c3/rom/crc.h"
#include "esp32c3/rom/uart.h"
#include "esp32c3/rom/gpio.h"
#include "esp32c3/rom/secure_boot.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp32h2/rom/cache.h"
#include "esp32h2/rom/efuse.h"
#include "esp32h2/rom/ets_sys.h"
#include "esp32h2/rom/spi_flash.h"
#include "esp32h2/rom/crc.h"
#include "esp32h2/rom/uart.h"
#include "esp32h2/rom/gpio.h"
#include "esp32h2/rom/secure_boot.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#else // CONFIG_IDF_TARGET_*
#error "Unsupported IDF_TARGET"
#endif

#include "soc/soc.h"
#include "soc/cpu.h"
#include "soc/rtc.h"
#include "soc/gpio_periph.h"
#include "soc/efuse_periph.h"
#include "soc/rtc_periph.h"
#include "soc/timer_periph.h"

#include "esp_image_format.h"
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "esp_flash_partitions.h"
#include "bootloader_flash_priv.h"
#include "bootloader_random.h"
#include "bootloader_config.h"
#include "bootloader_common.h"
#include "bootloader_utility.h"
#include "bootloader_sha.h"
#include "bootloader_console.h"
#include "bootloader_soc.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

#include "LTBootPlatform.h"

static const char *TAG = "boot";

/* Reduce literal size for some generic string literals */
#define MAP_ERR_MSG "Image contains multiple %s segments. Only the last one will be mapped."

static void unpack_load_app(const esp_image_metadata_t *data);
static void set_cache_and_start_app(uint32_t drom_addr,
                                    uint32_t drom_load_addr,
                                    uint32_t drom_size,
                                    uint32_t irom_addr,
                                    uint32_t irom_load_addr,
                                    uint32_t irom_size,
                                    uint32_t entry_addr);

void BootApplication(const esp_image_metadata_t *image_data)
{
    /**
     * Rough steps for a first boot, when encryption and secure boot are both disabled:
     *   1) Generate secure boot key and write to EFUSE.
     *   2) Write plaintext digest based on plaintext bootloader
     *   3) Generate flash encryption key and write to EFUSE.
     *   4) Encrypt flash in-place including bootloader, then digest,
     *      then app partitions and other encrypted partitions
     *   5) Burn EFUSE to enable flash encryption (FLASH_CRYPT_CNT)
     *   6) Burn EFUSE to enable secure boot (ABS_DONE_0)
     *
     * If power failure happens during Step 1, probably the next boot will continue from Step 2.
     * There is some small chance that EFUSEs will be part-way through being written so will be
     * somehow corrupted here. Thankfully this window of time is very small, but if that's the
     * case, one has to use the espefuse tool to manually set the remaining bits and enable R/W
     * protection. Once the relevant EFUSE bits are set and R/W protected, Step 1 will be skipped
     * successfully on further reboots.
     *
     * If power failure happens during Step 2, Step 1 will be skipped and Step 2 repeated:
     * the digest will get re-written on the next boot.
     *
     * If power failure happens during Step 3, it's possible that EFUSE was partially written
     * with the generated flash encryption key, though the time window for that would again
     * be very small. On reboot, Step 1 will be skipped and Step 2 repeated, though, Step 3
     * may fail due to the above mentioned reason, in which case, one has to use the espefuse
     * tool to manually set the remaining bits and enable R/W protection. Once the relevant EFUSE
     * bits are set and R/W protected, Step 3 will be skipped successfully on further reboots.
     *
     * If power failure happens after start of 4 and before end of 5, the next boot will fail
     * (bootloader header is encrypted and flash encryption isn't enabled yet, so it looks like
     * noise to the ROM bootloader). The check in the ROM is pretty basic so if the first byte of
     * ciphertext happens to be the magic byte E9 then it may try to boot, but it will definitely
     * crash (no chance that the remaining ciphertext will look like a valid bootloader image).
     * Only solution is to reflash with all plaintext and the whole process starts again: skips
     * Step 1, repeats Step 2, skips Step 3, etc.
     *
     * If power failure happens after 5 but before 6, the device will reboot with flash
     * encryption on and will regenerate an encrypted digest in Step 2. This should still
     * be valid as the input data for the digest is read via flash cache (so will be decrypted)
     * and the code in secure_boot_generate() tells bootloader_flash_write() to encrypt the data
     * on write if flash encryption is enabled. Steps 3 - 5 are skipped (encryption already on),
     * then Step 6 enables secure boot.
     */

    esp_err_t err;

    /* Generate flash encryption EFUSE key
     * Encrypt flash contents
     * Burn EFUSE to enable flash encryption */
    bool flash_encryption_enabled = esp_flash_encryption_enabled();
    err = esp_flash_encrypt_check_and_update();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Flash encryption check failed (%d).", err);
        return;
    }

    /* Permanently enable secure boot if both bootloader AND application are signed */
    err = esp_secure_boot_v2_permanently_enable(image_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Secure Boot v2 failed (%d)", err);
        return;
    }

    if (!flash_encryption_enabled && esp_flash_encryption_enabled()) {
        /* Flash encryption was just enabled for the first time,
           so issue a system reset to ensure flash encryption
           cache resets properly */
        ESP_LOGI(TAG, "Resetting with flash encryption enabled...");
        esp_rom_uart_tx_wait_idle(0);
        bootloader_reset();
    }

    /* Disable RNG early entropy source. */
    bootloader_random_disable();

    /* Disable glitch reset after all the security checks are completed.
     * Glitch detection can be falsely triggered by EMI interference (high RF TX power, etc)
     * and to avoid such false alarms, disable it.
     */
    bootloader_ana_clock_glitch_reset_config(false);

    // copy loaded segments to RAM, set up caches for mapped segments, and start application
    unpack_load_app(image_data);
}

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
void bootloader_utility_load_boot_image_from_deep_sleep(void)
{
    if (esp_rom_get_reset_reason(0) == RESET_REASON_CORE_DEEP_SLEEP) {
        esp_partition_pos_t *partition = bootloader_common_get_rtc_retain_mem_partition();
        if (partition != NULL) {
            esp_image_metadata_t image_data;
            if (CheckAndOrLoadImage(ESP_IMAGE_LOAD_NO_VALIDATE, partition, &image_data) == ESP_OK) {
                ESP_LOGI(TAG, "Fast booting app from partition at offset 0x%x", partition->offset);
                bootloader_common_update_rtc_retain_mem(NULL, true);
                BootApplication(&image_data);
            }
        }
        ESP_LOGE(TAG, "Fast booting is not successful");
        ESP_LOGI(TAG, "Try to load an app as usual with all validations");
    }
}
#endif

// Copy loaded segments to RAM, set up caches for mapped segments, and start application.

static void unpack_load_app(const esp_image_metadata_t *data)
{
    uint32_t drom_addr = 0;
    uint32_t drom_load_addr = 0;
    uint32_t drom_size = 0;
    uint32_t irom_addr = 0;
    uint32_t irom_load_addr = 0;
    uint32_t irom_size = 0;

    // Find DROM & IROM addresses, to configure cache mappings
    for (int i = 0; i < data->image.segment_count; i++) {
        const esp_image_segment_header_t *header = &data->segments[i];
        if (header->load_addr >= SOC_DROM_LOW && header->load_addr < SOC_DROM_HIGH) {
            if (drom_addr != 0) {
                ESP_LOGE(TAG, MAP_ERR_MSG, "DROM");
            } else {
                ESP_LOGD(TAG, "Mapping segment %d as %s", i, "DROM");
            }
            drom_addr = data->segment_data[i];
            drom_load_addr = header->load_addr;
            drom_size = header->data_len;
        }
        if (header->load_addr >= SOC_IROM_LOW && header->load_addr < SOC_IROM_HIGH) {
            if (irom_addr != 0) {
                ESP_LOGE(TAG, MAP_ERR_MSG, "IROM");
            } else {
                ESP_LOGD(TAG, "Mapping segment %d as %s", i, "IROM");
            }
            irom_addr = data->segment_data[i];
            irom_load_addr = header->load_addr;
            irom_size = header->data_len;
        }
    }

    ESP_LOGD(TAG, "calling set_cache_and_start_app");
    set_cache_and_start_app(drom_addr,
                            drom_load_addr,
                            drom_size,
                            irom_addr,
                            irom_load_addr,
                            irom_size,
                            data->image.entry_addr);
}

static void set_cache_and_start_app(
    uint32_t drom_addr,
    uint32_t drom_load_addr,
    uint32_t drom_size,
    uint32_t irom_addr,
    uint32_t irom_load_addr,
    uint32_t irom_size,
    uint32_t entry_addr)
{
    int rc __attribute__((unused));

    ESP_LOGD(TAG, "configure drom and irom and start");
#if CONFIG_IDF_TARGET_ESP32
    Cache_Read_Disable(0);
    Cache_Flush(0);
#elif CONFIG_IDF_TARGET_ESP32S2
    uint32_t autoload = Cache_Suspend_ICache();
    Cache_Invalidate_ICache_All();
#elif CONFIG_IDF_TARGET_ESP32S3
    uint32_t autoload = Cache_Suspend_DCache();
    Cache_Invalidate_DCache_All();
#elif CONFIG_IDF_TARGET_ESP32C3
    uint32_t autoload = Cache_Suspend_ICache();
    Cache_Invalidate_ICache_All();
#elif CONFIG_IDF_TARGET_ESP32H2
    uint32_t autoload = Cache_Suspend_ICache();
    Cache_Invalidate_ICache_All();
#endif

    /* Clear the MMU entries that are already set up,
     * so the new app only has the mappings it creates.
     */
#if CONFIG_IDF_TARGET_ESP32
    for (int i = 0; i < DPORT_FLASH_MMU_TABLE_SIZE; i++) {
        DPORT_PRO_FLASH_MMU_TABLE[i] = DPORT_FLASH_MMU_TABLE_INVALID_VAL;
    }
#else
    for (size_t i = 0; i < FLASH_MMU_TABLE_SIZE; i++) {
        FLASH_MMU_TABLE[i] = MMU_TABLE_INVALID_VAL;
    }
#endif
    uint32_t drom_load_addr_aligned = drom_load_addr & MMU_FLASH_MASK;
    uint32_t drom_addr_aligned = drom_addr & MMU_FLASH_MASK;
    uint32_t drom_page_count = bootloader_cache_pages_to_map(drom_size, drom_load_addr);
    ESP_LOGV(TAG, "d mmu set paddr=%08x vaddr=%08x size=%d n=%d",
             drom_addr_aligned, drom_load_addr_aligned, drom_size, drom_page_count);
#if CONFIG_IDF_TARGET_ESP32
    rc = cache_flash_mmu_set(0, 0, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count);
#elif CONFIG_IDF_TARGET_ESP32S2
    rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32S3
    rc = Cache_Dbus_MMU_Set(MMU_ACCESS_FLASH, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32C3
    rc = Cache_Dbus_MMU_Set(MMU_ACCESS_FLASH, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32H2
    rc = Cache_Dbus_MMU_Set(MMU_ACCESS_FLASH, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count, 0);
#endif
    ESP_LOGV(TAG, "rc=%d", rc);
#if CONFIG_IDF_TARGET_ESP32
    rc = cache_flash_mmu_set(1, 0, drom_load_addr_aligned, drom_addr_aligned, 64, drom_page_count);
    ESP_LOGV(TAG, "rc=%d", rc);
#endif
    uint32_t irom_load_addr_aligned = irom_load_addr & MMU_FLASH_MASK;
    uint32_t irom_addr_aligned = irom_addr & MMU_FLASH_MASK;
    uint32_t irom_page_count = bootloader_cache_pages_to_map(irom_size, irom_load_addr);
    ESP_LOGV(TAG, "i mmu set paddr=%08x vaddr=%08x size=%d n=%d",
             irom_addr_aligned, irom_load_addr_aligned, irom_size, irom_page_count);
#if CONFIG_IDF_TARGET_ESP32
    rc = cache_flash_mmu_set(0, 0, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count);
#elif CONFIG_IDF_TARGET_ESP32S2
    uint32_t iram1_used = 0;
    if (irom_load_addr + irom_size > IRAM1_ADDRESS_LOW) {
        iram1_used = 1;
    }
    if (iram1_used) {
        rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, IRAM0_ADDRESS_LOW, 0, 64, 64, 1);
        rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, IRAM1_ADDRESS_LOW, 0, 64, 64, 1);
        REG_CLR_BIT(EXTMEM_PRO_ICACHE_CTRL1_REG, EXTMEM_PRO_ICACHE_MASK_IRAM1);
    }
    rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32S3
    rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32C3
    rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count, 0);
#elif CONFIG_IDF_TARGET_ESP32H2
    rc = Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count, 0);
#endif
    ESP_LOGV(TAG, "rc=%d", rc);
#if CONFIG_IDF_TARGET_ESP32
    rc = cache_flash_mmu_set(1, 0, irom_load_addr_aligned, irom_addr_aligned, 64, irom_page_count);
    ESP_LOGV(TAG, "rc=%d", rc);
    DPORT_REG_CLR_BIT( DPORT_PRO_CACHE_CTRL1_REG,
                       (DPORT_PRO_CACHE_MASK_IRAM0) | (DPORT_PRO_CACHE_MASK_IRAM1 & 0) |
                       (DPORT_PRO_CACHE_MASK_IROM0 & 0) | DPORT_PRO_CACHE_MASK_DROM0 |
                       DPORT_PRO_CACHE_MASK_DRAM1 );
    DPORT_REG_CLR_BIT( DPORT_APP_CACHE_CTRL1_REG,
                       (DPORT_APP_CACHE_MASK_IRAM0) | (DPORT_APP_CACHE_MASK_IRAM1 & 0) |
                       (DPORT_APP_CACHE_MASK_IROM0 & 0) | DPORT_APP_CACHE_MASK_DROM0 |
                       DPORT_APP_CACHE_MASK_DRAM1 );
#elif CONFIG_IDF_TARGET_ESP32S2
    REG_CLR_BIT( EXTMEM_PRO_ICACHE_CTRL1_REG, (EXTMEM_PRO_ICACHE_MASK_IRAM0) | (EXTMEM_PRO_ICACHE_MASK_IRAM1 & 0) | EXTMEM_PRO_ICACHE_MASK_DROM0 );
#elif CONFIG_IDF_TARGET_ESP32S3
    REG_CLR_BIT(EXTMEM_DCACHE_CTRL1_REG, EXTMEM_DCACHE_SHUT_CORE0_BUS);
#if !CONFIG_FREERTOS_UNICORE
    REG_CLR_BIT(EXTMEM_DCACHE_CTRL1_REG, EXTMEM_DCACHE_SHUT_CORE1_BUS);
#endif
#elif CONFIG_IDF_TARGET_ESP32C3
    REG_CLR_BIT(EXTMEM_ICACHE_CTRL1_REG, EXTMEM_ICACHE_SHUT_IBUS);
    REG_CLR_BIT(EXTMEM_ICACHE_CTRL1_REG, EXTMEM_ICACHE_SHUT_DBUS);
#elif CONFIG_IDF_TARGET_ESP32H2
    REG_CLR_BIT(EXTMEM_ICACHE_CTRL1_REG, EXTMEM_ICACHE_SHUT_IBUS);
    REG_CLR_BIT(EXTMEM_ICACHE_CTRL1_REG, EXTMEM_ICACHE_SHUT_DBUS);
#endif
#if CONFIG_IDF_TARGET_ESP32
    Cache_Read_Enable(0);
#elif CONFIG_IDF_TARGET_ESP32S2
    Cache_Resume_ICache(autoload);
#elif CONFIG_IDF_TARGET_ESP32S3
    Cache_Resume_DCache(autoload);
#elif CONFIG_IDF_TARGET_ESP32C3
    Cache_Resume_ICache(autoload);
#elif CONFIG_IDF_TARGET_ESP32H2
    Cache_Resume_ICache(autoload);
#endif
    // Application will need to do Cache_Flush(1) and Cache_Read_Enable(1)

    ESP_LOGD(TAG, "start: 0x%08x", entry_addr);
    bootloader_atexit();
    typedef void (*entry_t)(void) __attribute__((noreturn));
    entry_t entry = ((entry_t) entry_addr);

    // TODO: we have used quite a bit of stack at this point.
    // use "movsp" instruction to reset stack back to where ROM stack starts.
    (*entry)();
}

void bootloader_reset(void)
{
#ifdef BOOTLOADER_BUILD
    bootloader_atexit();
    esp_rom_delay_us(1000); /* Allow last byte to leave FIFO */
    REG_WRITE(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
    while (1) { }       /* This line will never be reached, used to keep gcc happy */
#else
    abort();            /* This function should really not be called from application code */
#endif
}

void bootloader_atexit(void)
{
#ifdef BOOTLOADER_BUILD
    bootloader_console_deinit();
#else
    abort();
#endif
}

esp_err_t bootloader_sha256_hex_to_str(char *out_str, const uint8_t *in_array_hex, size_t len)
{
    if (out_str == NULL || in_array_hex == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < len; i++) {
        for (int shift = 0; shift < 2; shift++) {
            uint8_t nibble = (in_array_hex[i] >> (shift ? 0 : 4)) & 0x0F;
            if (nibble < 10) {
                out_str[i * 2 + shift] = '0' + nibble;
            } else {
                out_str[i * 2 + shift] = 'a' + nibble - 10;
            }
        }
    }
    return ESP_OK;
}

void bootloader_debug_buffer(const void *buffer, size_t length, const char *label)
{
#if CONFIG_BOOTLOADER_LOG_LEVEL >= 4
    const uint8_t *bytes = (const uint8_t *)buffer;
    const size_t output_len = MIN(length, 128);
    char hexbuf[128 * 2 + 1];

    bootloader_sha256_hex_to_str(hexbuf, bytes, output_len);

    hexbuf[output_len * 2] = '\0';
    ESP_LOGD(TAG, "%s: %s", label, hexbuf);
#else
    (void) buffer;
    (void) length;
    (void) label;
#endif
}

esp_err_t bootloader_sha256_flash_contents(uint32_t flash_offset, uint32_t len, uint8_t *digest)
{
    if (digest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Handling firmware images larger than MMU capacity */
    uint32_t mmu_free_pages_count = bootloader_mmap_get_free_pages();
    bootloader_sha256_handle_t sha_handle = NULL;

    sha_handle = bootloader_sha256_start();
    if (sha_handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    while (len > 0) {
        uint32_t mmu_page_offset = ((flash_offset & MMAP_ALIGNED_MASK) != 0) ? 1 : 0; /* Skip 1st MMU Page if it is already populated */
        uint32_t partial_image_len = MIN(len, ((mmu_free_pages_count - mmu_page_offset) * SPI_FLASH_MMU_PAGE_SIZE)); /* Read the image that fits in the free MMU pages */

        const void * image = bootloader_mmap(flash_offset, partial_image_len);
        if (image == NULL) {
            bootloader_sha256_finish(sha_handle, NULL);
            return ESP_FAIL;
        }
        bootloader_sha256_data(sha_handle, image, partial_image_len);
        bootloader_munmap(image);

        flash_offset += partial_image_len;
        len -= partial_image_len;
    }
    bootloader_sha256_finish(sha_handle, digest);
    return ESP_OK;
}

