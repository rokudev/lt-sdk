/******************************************************************************
 * esp_rom_sys.h                                   ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * IDF's esp_rom_sys.h, transcribed from source/esp32/ltbootloader/include, less
 * esp_rom_get_reset_reason() - the one declaration that needs soc/, a tree that
 * shadows the wireless headers this component is built against.  src/rsn_supp/
 * wpa.c is the only file that includes this and it calls nothing from it.
 */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_ROM_SYS_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_ROM_SYS_H

#include <stdint.h>

int  esp_rom_printf(const char *fmt, ...);
void esp_rom_delay_us(uint32_t us);
void esp_rom_install_channel_putc(int channel, void (*putc)(char c));
void esp_rom_install_uart_printf(void);

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_ROM_SYS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
