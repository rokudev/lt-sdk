/******************************************************************************
 * esp_log.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/


#ifndef __ESP_LOG_H_
#define __ESP_LOG_H_

#include "esp_rom_sys.h"

#define ESP_PrintfWithCR(fmt, ...) (esp_rom_printf(fmt "\n" __VA_OPT__(,) __VA_ARGS__))
#define ESP_LOG_MESS(X, ...)       ((void)X);ESP_PrintfWithCR(__VA_ARGS__)

#define ESP_EARLY_LOGE  ESP_LOG_MESS
#define ESP_EARLY_LOGW  ESP_LOG_MESS
//#define ESP_EARLY_LOGI  ESP_LOG_MESS
#define ESP_EARLY_LOGI(...)
#define ESP_EARLY_LOGD(...)
#define ESP_EARLY_LOGV(...)

#define ESP_LOGE  ESP_LOG_MESS
#define ESP_LOGW  ESP_LOG_MESS
//#define ESP_LOGI  ESP_LOG_MESS
#define ESP_LOGI(...)
#define ESP_LOGD(...)
#define ESP_LOGV(...)

#endif

