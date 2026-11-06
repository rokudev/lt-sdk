/******************************************************************************
 * esp_log.h                                       ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Stands in for IDF's esp_log.h, which LT does not carry.  include/utils/wpa_debug.h
 * is the component's only consumer, and it needs just the five level constants -
 * already vendored as enum esp_log_level_e - plus ESP_LOG_LEVEL_LOCAL, which it
 * uses to define wpa_printf().  That definition is behind DEBUG_PRINT, so nothing
 * calls the sink below unless the .mk adds -DDEBUG_PRINT.
 */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_LOG_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_LOG_H

#include "espidf_types.h"

/*
 * Out of line, in roku-lt-port/esp_log.c, so LTCore.h reaches exactly one
 * translation unit in this component: it redefines va_list and brings its own
 * u8/u32 typedefs, which collide with src/utils/common.h.  Deliberately carries
 * no printf format attribute - LT's formatter is not plain printf.
 */
void Esp32s3WPASupplicant_Log(int nLevel, const char *pTag, const char *pFormat, ...);

#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...) \
    Esp32s3WPASupplicant_Log((int)(level), (tag), (format), ##__VA_ARGS__)

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP_LOG_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
