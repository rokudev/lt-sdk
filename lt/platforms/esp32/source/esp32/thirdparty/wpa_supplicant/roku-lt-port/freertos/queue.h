/******************************************************************************
 * queue.h                                         ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/* See the note in freertos/FreeRTOS.h. */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_QUEUE_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_QUEUE_H

#include "freertos/FreeRTOS.h"

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
void          vQueueDelete(QueueHandle_t xQueue);
BaseType_t    xQueueSend(QueueHandle_t xQueue, const void *pItemToQueue, TickType_t xTicksToWait);
BaseType_t    xQueueReceive(QueueHandle_t xQueue, void *pBuffer, TickType_t xTicksToWait);

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_QUEUE_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
