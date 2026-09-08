/******************************************************************************
 * semphr.h                                        ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/* See the note in freertos/FreeRTOS.h. */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_SEMPHR_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_SEMPHR_H

#include "freertos/queue.h"

typedef QueueHandle_t SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount);
BaseType_t        xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait);
BaseType_t        xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void              vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_SEMPHR_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
