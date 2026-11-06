/******************************************************************************
 * esp_wifi_crypto_types.h                              ESP32 wireless drivers
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Roku: a chip selector, not a header - see esp_wifi_types.h beside it for why
 * the wireless headers are split per chip.  This one carries wpa_crypto_funcs_t,
 * the table the supplicant hands the blobs, and the esp32s3 revision has one
 * more entry on the end than the esp32 one.
 */

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/esp_wifi_crypto_types.h"
#else
#include "esp32/esp_wifi_crypto_types.h"
#endif

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created; former contents moved to esp32/
 */
