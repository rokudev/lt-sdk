/******************************************************************************
 * FreeRTOS.h                                      ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The subset of FreeRTOS that esp_supplicant/src/esp_wpa2.c uses, which is the
 * one file in this component that talks to FreeRTOS at all.  LT is not
 * FreeRTOS; ../freertos.c implements these on LTMutex, LTCountingSemaphore and
 * LTThread.  Nothing in LT calls esp_wpa2_* directly, but libnet80211 and
 * esp_wpa_main.c both do, so the member is extracted and the entry points are
 * needed.
 */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_FREERTOS_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_FREERTOS_H

#include "sdkconfig.h"
#include "espidf_types.h"

#define pdFALSE             ((BaseType_t)0)
#define pdTRUE              ((BaseType_t)1)
#define pdFAIL              pdFALSE
#define pdPASS              pdTRUE

#define portMAX_DELAY       ((TickType_t)0xFFFFFFFFU)
#define portTICK_PERIOD_MS  ((TickType_t)(1000 / CONFIG_FREERTOS_HZ))

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_FREERTOS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
