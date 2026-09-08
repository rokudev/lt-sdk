/******************************************************************************
 * esp_log.c                                       ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>

#include "esp_log.h"

DEFINE_LTLOG_SECTION("esp32.wpa");

/* The sink behind ESP_LOG_LEVEL_LOCAL, and so behind wpa_printf(). */
void
Esp32s3WPASupplicant_Log(int nLevel, const char *pTag, const char *pFormat, ...) {
    lt_va_list args;

    lt_va_start(args, pFormat);

    switch (nLevel) {
        case ESP_LOG_ERROR:   LTLOGV_REDALERT(pTag, pFormat, args);    break;
        case ESP_LOG_WARN:    LTLOGV_YELLOWALERT(pTag, pFormat, args); break;
        case ESP_LOG_INFO:    LTLOGV(pTag, pFormat, args);             break;
        case ESP_LOG_DEBUG:   LTLOGV_DEBUG(pTag, pFormat, args);       break;
        case ESP_LOG_VERBOSE: LTLOGV_VERBOSE(pTag, pFormat, args);     break;
        default:                                                       break;
    }

    lt_va_end(args);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
