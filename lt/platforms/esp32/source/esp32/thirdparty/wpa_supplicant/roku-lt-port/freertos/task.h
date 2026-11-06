/******************************************************************************
 * task.h                                          ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/* See the note in freertos/FreeRTOS.h. */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_TASK_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_TASK_H

#include "freertos/FreeRTOS.h"

typedef void * TaskHandle_t;
typedef void (*TaskFunction_t)(void *pParameters);

BaseType_t   xTaskCreate(TaskFunction_t pTaskCode, const char *pName, uint32_t nStackDepth,
                         void *pParameters, UBaseType_t uxPriority, TaskHandle_t *pCreatedTask);
void         vTaskDelete(TaskHandle_t xTaskToDelete);
TaskHandle_t xTaskGetCurrentTaskHandle(void);

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_TASK_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
