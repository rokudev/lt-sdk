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

static __attribute__((unused)) const char *TAG = "dummy";

esp_err_t esp_flash_encryption_enable_secure_features(void) {
    /* CRYPT_CONFIG determines which bits of the AES block key are XORed
       with bits from the flash address, to provide the key tweak.

       CRYPT_CONFIG == 0 is effectively AES ECB mode (NOT SUPPORTED)

       For now this is hardcoded to XOR all 256 bits of the key.

       If you need to override it, you can pre-burn this efuse to the
       desired value and then write-protect it, in which case this
       operation does nothing. Please note this is not recommended!
    */
    ESP_LOGI(TAG, "Setting CRYPT_CONFIG efuse to 0xF");
    uint32_t crypt_config = 0;
    esp_efuse_read_field_blob(ESP_EFUSE_ENCRYPT_CONFIG, &crypt_config, 4);
    if (crypt_config == 0) {
        crypt_config = EFUSE_FLASH_CRYPT_CONFIG;
        esp_efuse_write_field_blob(ESP_EFUSE_ENCRYPT_CONFIG, &crypt_config, 4);
    } else if (crypt_config != EFUSE_FLASH_CRYPT_CONFIG) {
        ESP_LOGE(TAG, "EFUSE_ENCRYPT_CONFIG should be set 0xF but it is 0x%x", crypt_config);
        return ESP_ERR_INVALID_STATE;
    }

#ifndef LT_BOOTLOADER_SECURITY_DEV_BYPASS
    esp_err_t err = esp_efuse_write_field_bit(ESP_EFUSE_DISABLE_DL_ENCRYPT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable ROM Encrypt");
        return err;
    }
    err = esp_efuse_write_field_bit(ESP_EFUSE_DISABLE_DL_DECRYPT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable ROM Decrypt");
        return err;
    }
    esp_efuse_write_field_bit(ESP_EFUSE_DISABLE_DL_CACHE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't disable MMU Cache");
        return err;
    }
#endif

    /* NOTE: Final lockdown of SoC occurs when secure boot is enabled. */

    return ESP_OK;
}
