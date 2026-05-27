/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <strings.h>
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "sdkconfig.h"

/* Full Securing process:
 *   1. Manufacturing/developer writes encryption key into flash and reboots.
 *   2. Bootloader encrypts all flash partitions and enables encryption mode.
 *   3. If both bootloader AND application are properly signed, secure boot is enabled.
 *   4. After enabling secure boot OTP is fully(*) locked down.
 *     (*) If LT_BOOTLOADER_SECURITY_DEV_BYPASS set, some OTP lockdwon bits are
 *           not burned allowing for secure development with TEST keys.
 *
 * The following security combinations are supported by following applicable steps above:
 *   E  - Encryption enabled only (non-secured or DEV BYPASS).
 *   S  - Secure Boot enabled only (DEV BYPASS only).
 *   ES - Encryption AND Secure Boot enabled (fully-secured or DEV BYPASS). */

static const char * TAG = "dummy";

esp_err_t esp_secure_boot_enable_secure_features(void) {
    esp_err_t err;

#ifndef LT_BOOTLOADER_SECURITY_DEV_BYPASS
    if (!esp_flash_encryption_enabled()) {
        ESP_LOGE(TAG, "Error, encryption not enabled!\n");
        return ESP_ERR_INVALID_STATE;
    }
    err = esp_efuse_write_field_bit(ESP_EFUSE_UART_DOWNLOAD_DIS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable ROM Downloads");
        return err;
    }
    err = esp_efuse_write_field_bit(ESP_EFUSE_DISABLE_JTAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable JTAG");
        return err;
    }
    err = esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_EFUSE_RD_DISABLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't prevent disable of more fuses");
        return err;
    }
#endif

    err = esp_efuse_write_field_bit(ESP_EFUSE_CONSOLE_DEBUG_DISABLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable ROM BASIC");
        return err;
    }
    err = esp_efuse_write_field_bit(ESP_EFUSE_ABS_DONE_1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't enable secure boot");
        return err;
    }
    return ESP_OK;
}

