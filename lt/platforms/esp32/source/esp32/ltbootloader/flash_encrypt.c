/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <strings.h>
#include "bootloader_flash_priv.h"
#include "bootloader_random.h"
#include "esp_image_format.h"
#include "esp_flash_encrypt.h"
#include "esp_flash_partitions.h"
#include "esp_secure_boot.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "hal/wdt_hal.h"

#include "LTBoot.h"
#include "LTBootPlatform.h"
#include "LTBootDriver.h"

#if CONFIG_IDF_TARGET_ESP32
#define CRYPT_CNT ESP_EFUSE_FLASH_CRYPT_CNT
#define WR_DIS_CRYPT_CNT ESP_EFUSE_WR_DIS_FLASH_CRYPT_CNT
#else
#define CRYPT_CNT ESP_EFUSE_SPI_BOOT_CRYPT_CNT
#define WR_DIS_CRYPT_CNT ESP_EFUSE_WR_DIS_SPI_BOOT_CRYPT_CNT
#endif

/* This file implements FLASH ENCRYPTION related APIs to perform
 * various operations such as programming necessary flash encryption
 * eFuses, detect whether flash encryption is enabled (by reading eFuse)
 * and if required encrypt the partitions in flash memory
 */

static const char *TAG = "flash_encrypt";

/* Static functions for stages of flash encryption */
static esp_err_t initialise_flash_encryption(void);
static esp_err_t encrypt_flash_contents(size_t flash_crypt_cnt, bool flash_crypt_wr_dis) __attribute__((unused));

esp_err_t esp_flash_encrypt_check_and_update(void) {
    size_t flash_crypt_cnt = 0;
    esp_efuse_read_field_cnt(CRYPT_CNT, &flash_crypt_cnt);
    bool flash_crypt_wr_dis = esp_efuse_read_field_bit(WR_DIS_CRYPT_CNT);

    ESP_LOGV(TAG, "CRYPT_CNT %d, write protection %d", flash_crypt_cnt, flash_crypt_wr_dis);

    if (flash_crypt_cnt % 2 == 1) {
        /* Flash is already encrypted */
        return ESP_OK;
    } else {
#ifndef CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED
        /* Flash is not encrypted, so encrypt it! */
        return encrypt_flash_contents(flash_crypt_cnt, flash_crypt_wr_dis);
#else
        ESP_LOGE(TAG, "flash encryption is not enabled, and SECURE_FLASH_REQUIRE_ALREADY_ENABLED "
                      "is set, refusing to boot.");
        return ESP_ERR_INVALID_STATE;
#endif // CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED
    }
}

static esp_err_t check_and_generate_encryption_keys(void) {
    size_t key_size = 32;
    (void)key_size;
#ifdef CONFIG_IDF_TARGET_ESP32
    enum { BLOCKS_NEEDED = 1 };
    esp_efuse_purpose_t purposes[BLOCKS_NEEDED] = {
        ESP_EFUSE_KEY_PURPOSE_FLASH_ENCRYPTION,
    };
    esp_efuse_coding_scheme_t coding_scheme = esp_efuse_get_coding_scheme(EFUSE_BLK_ENCRYPT_FLASH);
    if (coding_scheme != EFUSE_CODING_SCHEME_NONE && coding_scheme != EFUSE_CODING_SCHEME_3_4) {
        ESP_LOGE(TAG, "Unknown/unsupported CODING_SCHEME value 0x%x", coding_scheme);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (coding_scheme == EFUSE_CODING_SCHEME_3_4) {
        key_size = 24;
    }
#else
#ifdef CONFIG_SECURE_FLASH_ENCRYPTION_AES256
    enum { BLOCKS_NEEDED = 2 };
    esp_efuse_purpose_t purposes[BLOCKS_NEEDED] = {
        ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1,
        ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_2,
    };
    if (esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_XTS_AES_128_KEY, NULL)) {
        ESP_LOGE(TAG, "XTS_AES_128_KEY is already in use, XTS_AES_256_KEY_1/2 can not be used");
        return ESP_ERR_INVALID_STATE;
    }
#else
    enum { BLOCKS_NEEDED = 1 };
    esp_efuse_purpose_t purposes[BLOCKS_NEEDED] = {
        ESP_EFUSE_KEY_PURPOSE_XTS_AES_128_KEY,
    };
#endif // CONFIG_SECURE_FLASH_ENCRYPTION_AES256
#endif // CONFIG_IDF_TARGET_ESP32

    /* Initialize all efuse block entries to invalid (max) value */
    esp_efuse_block_t blocks[BLOCKS_NEEDED] = {[0 ... BLOCKS_NEEDED-1] = EFUSE_BLK_KEY_MAX};
    bool has_key = true;
    for (unsigned i = 0; i < BLOCKS_NEEDED; i++) {
        bool tmp_has_key = esp_efuse_find_purpose(purposes[i], &blocks[i]);
        if (tmp_has_key) { // For ESP32: esp_efuse_find_purpose() always returns True, need to check whether the key block is used or not.
            tmp_has_key &= !esp_efuse_key_block_unused(blocks[i]);
        }
        if (i == 1 && tmp_has_key != has_key) {
            ESP_LOGE(TAG, "Invalid efuse key blocks: Both AES-256 key blocks must be set.");
            return ESP_ERR_INVALID_STATE;
        }
        has_key &= tmp_has_key;
    }

    if (!has_key) {
        ESP_LOGE(TAG, "Flash encryption key is not provided yet, flash won't be encrypted!");
        return ESP_ERR_NOT_FOUND;
    } else {
        for (unsigned i = 0; i < BLOCKS_NEEDED; i++) {
            if (!esp_efuse_get_key_dis_write(blocks[i])
                || !esp_efuse_get_key_dis_read(blocks[i])
                || !esp_efuse_get_keypurpose_dis_write(blocks[i])) { // For ESP32: no keypurpose, it returns always True.
                ESP_LOGE(TAG, "Invalid key state, check read&write protection for key and keypurpose(if exists)");
                return ESP_ERR_INVALID_STATE;
            }
        }
        ESP_LOGI(TAG, "Using pre-loaded flash encryption key in efuse");
    }
    return ESP_OK;
}

static esp_err_t initialise_flash_encryption(void) {
    esp_efuse_batch_write_begin(); /* Batch all efuse writes at the end of this function */

    /* Enable encryption if encryption OTP key is valid (e.g.: not zero) */
    esp_err_t err = check_and_generate_encryption_keys();
    if (err != ESP_OK) {
        esp_efuse_batch_write_cancel();
        /* Returns here for non-secure units */
        return err;
    }

    err = esp_flash_encryption_enable_secure_features();
    if (err != ESP_OK) {
        esp_efuse_batch_write_cancel();
        return err;
    }

    err = esp_efuse_batch_write_commit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error programming security eFuses (err=0x%x).", err);
        return err;
    }

    return ESP_OK;
}

static bool EncryptPartition(u32 nByteOffset, u32 nNumSectors) {
    u32 nStartSector = nByteOffset / FLASH_SECTOR_SIZE;
    wdt_hal_context_t rtc_wdt_ctx = { .inst = WDT_RWDT, .rwdt_dev = &RTCCNTL };
    u32 buf[FLASH_SECTOR_SIZE / sizeof(u32)];
    for (u32 nSector = 0; nSector < nNumSectors; nSector++, nByteOffset += FLASH_SECTOR_SIZE) {
        wdt_hal_write_protect_disable(&rtc_wdt_ctx);
        wdt_hal_feed(&rtc_wdt_ctx);
        wdt_hal_write_protect_enable(&rtc_wdt_ctx);
        esp_err_t err = bootloader_flash_read(nByteOffset, buf, FLASH_SECTOR_SIZE, false);
        if (err != ESP_OK) return false;
        err = bootloader_flash_erase_sector(nStartSector + nSector);
        if (err != ESP_OK) return false;
        err = bootloader_flash_write(nByteOffset, buf, FLASH_SECTOR_SIZE, true);
        if (err != ESP_OK) return false;
    }
    return true;
}

static bool EncryptPartitionCallback(const LTDeviceFlash_PartitionEntry * pPartition, void * pClientData) {
    LT_UNUSED(pClientData);
    /* Skip partition tables and LTAT */
    if (LTBootPlatform_strncmp(pPartition->type, "part", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        return true;
    } else if (LTBootPlatform_strncmp(pPartition->type, "ltat", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        return true;
    }
    LTBootPlatform_printf("Encrypting %s\n", pPartition->name);
    return EncryptPartition(pPartition->nByteOffset, pPartition->nNumSectors);
}

/* Encrypt all flash data that should be encrypted */
static esp_err_t encrypt_flash_contents(size_t flash_crypt_cnt, bool flash_crypt_wr_dis) {
    esp_err_t err;
    /* If all flash_crypt_cnt bits are burned or write-disabled, the device can't re-encrypt itself. */
    if (flash_crypt_wr_dis || flash_crypt_cnt == CRYPT_CNT[0]->bit_count) {
        ESP_LOGE(TAG, "Cannot re-encrypt data CRYPT_CNT %d write disabled %d", flash_crypt_cnt, flash_crypt_wr_dis);
        return ESP_FAIL;
    }
    if (flash_crypt_cnt == 0) {
        /* Very first flash of encrypted data: generate keys, etc. */
        err = initialise_flash_encryption();
        if (err != ESP_OK) {
            return err;
        }
    }
    /* Encrypt all partitions EXCEPT for partition tables and LTAT */
    if (LTBoot_EnumeratePartitions(EncryptPartitionCallback, NULL) == false) {
        LTBootPlatform_printf("Flash encryption failed\n");
        return ESP_FAIL;
    }
    /* Encrypt partition tables as last step */
    u32 nPartTableOffset;
    if (LTBootDriverFlash_GetPartitionTableOffset(&nPartTableOffset, true)) {
        if (EncryptPartition(nPartTableOffset, 1) == false) {
            LTBootPlatform_printf("Flash encryption failed\n");
            return ESP_FAIL;
        }
    }
    if (LTBootDriverFlash_GetPartitionTableOffset(&nPartTableOffset, false)) {
        if (EncryptPartition(nPartTableOffset, 1) == false) {
            LTBootPlatform_printf("Flash encryption failed\n");
            return ESP_FAIL;
        }
    }
    /* Go straight to max, permanently enabled */
    size_t new_flash_crypt_cnt = CRYPT_CNT[0]->bit_count - flash_crypt_cnt;
    err = esp_efuse_write_field_cnt(CRYPT_CNT, new_flash_crypt_cnt);
    if (err == ESP_OK) {
        LTBootPlatform_printf("Flash encryption complete\n");
    }
    return err;
}

