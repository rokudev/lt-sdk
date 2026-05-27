/*
 * SPDX-FileCopyrightText: 2020-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "string.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/spi_flash.h"
#include "esp32s2/rom/ets_sys.h"
#endif
#include "esp_rom_crc.h"
#include "esp_rom_gpio.h"
#include "esp_flash_partitions.h"
#include "bootloader_flash.h"
#include "bootloader_common.h"
#include "soc/gpio_periph.h"
#include "soc/rtc.h"
#include "soc/efuse_reg.h"
#include "soc/soc_memory_types.h"
#include "hal/gpio_ll.h"
#include "esp_image_format.h"
#include "bootloader_sha.h"
#include "sys/param.h"
#include "bootloader_flash_priv.h"

#define ESP_PARTITION_HASH_LEN 32 /* SHA-256 digest length */

#if defined( CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP ) || defined( CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC )

#define RTC_RETAIN_MEM_ADDR (SOC_RTC_DRAM_HIGH - sizeof(rtc_retain_mem_t))

static rtc_retain_mem_t *const rtc_retain_mem = (rtc_retain_mem_t *)RTC_RETAIN_MEM_ADDR;

static bool check_rtc_retain_mem(void)
{
    return esp_rom_crc32_le(UINT32_MAX, (uint8_t*)rtc_retain_mem, sizeof(rtc_retain_mem_t) - sizeof(rtc_retain_mem->crc)) == rtc_retain_mem->crc && rtc_retain_mem->crc != UINT32_MAX;
}

static void update_rtc_retain_mem_crc(void)
{
    rtc_retain_mem->crc = esp_rom_crc32_le(UINT32_MAX, (uint8_t*)rtc_retain_mem, sizeof(rtc_retain_mem_t) - sizeof(rtc_retain_mem->crc));
}

void bootloader_common_reset_rtc_retain_mem(void)
{
    memset(rtc_retain_mem, 0, sizeof(rtc_retain_mem_t));
}

uint16_t bootloader_common_get_rtc_retain_mem_reboot_counter(void)
{
    if (check_rtc_retain_mem()) {
        return rtc_retain_mem->reboot_counter;
    }
    return 0;
}

esp_partition_pos_t* bootloader_common_get_rtc_retain_mem_partition(void)
{
    if (check_rtc_retain_mem()) {
        return &rtc_retain_mem->partition;
    }
    return NULL;
}

void bootloader_common_update_rtc_retain_mem(esp_partition_pos_t* partition, bool reboot_counter)
{
    if (reboot_counter) {
        if (!check_rtc_retain_mem()) {
            bootloader_common_reset_rtc_retain_mem();
        }
        if (++rtc_retain_mem->reboot_counter == 0) {
            // do not allow to overflow. Stop it.
            --rtc_retain_mem->reboot_counter;
        }

    }

    if (partition != NULL) {
        rtc_retain_mem->partition.offset = partition->offset;
        rtc_retain_mem->partition.size   = partition->size;
    }

    update_rtc_retain_mem_crc();
}

rtc_retain_mem_t* bootloader_common_get_rtc_retain_mem(void)
{
    return rtc_retain_mem;
}

#endif // defined( CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP ) || defined( CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC )
