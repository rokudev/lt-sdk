/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reduced to the two entries this tree reaches.  Upstream's esp32p4 file also
 * carries bootloader_flash_update_size, _clock_config, _gpio_config,
 * _dummy_config and the MSPI clock helper; nothing here calls any of them, and
 * the MSPI clock is set from bootloader_esp32p4.c instead.
 */
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "soc/soc.h"
#include "soc/spi_mem_c_reg.h"
#include "esp32p4/rom/spi_flash.h"
#include "bootloader_flash.h"
#include "bootloader_flash_config.h"

void bootloader_flash_update_id(void)
{
    esp_rom_spiflash_chip_t *chip = &rom_spiflash_legacy_data->chip;
    chip->device_id = bootloader_read_flash_id();
}

void IRAM_ATTR bootloader_flash_cs_timing_config(void)
{
    /* One MSPI controller here, so a single register pair - the earlier parts
     * index SPI_MEM_*_REG(0) and (1). */
    SET_PERI_REG_MASK(SPI_MEM_C_USER_REG, SPI_MEM_C_CS_HOLD_M | SPI_MEM_C_CS_SETUP_M);
    SET_PERI_REG_BITS(SPI_MEM_C_CTRL2_REG, SPI_MEM_C_CS_HOLD_TIME_V, 0, SPI_MEM_C_CS_HOLD_TIME_S);
    SET_PERI_REG_BITS(SPI_MEM_C_CTRL2_REG, SPI_MEM_C_CS_SETUP_TIME_V, 0, SPI_MEM_C_CS_SETUP_TIME_S);
}
