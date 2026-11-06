/******************************************************************************
 * esp_wifi_types.h                                     ESP32 wireless drivers
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Roku: a chip selector, not a header.  The real thing now lives per chip,
 * because the esp32 and esp32s3 Wi-Fi blobs in source/esp32/mastering/lib were
 * cut from different ESP-IDF revisions and the two headers disagree on values
 * that cross the blob boundary: wifi_cipher_type_t gained four members ahead of
 * WIFI_CIPHER_TYPE_UNKNOWN, WIFI_REASON_INVALID_PMKID moved from 53 to 49,
 * wifi_auth_mode_t grew, and wifi_scan_config_t gained a trailing byte.  Sharing
 * one copy silently mismatched the esp32s3.
 *
 * This file exists because esp_wifi.h and its neighbours reach the types with a
 * quoted include, which the compiler resolves next to esp_wifi.h before it looks
 * at -I - so a per-chip directory earlier on the include path cannot win on its
 * own.  Adding a chip is a matter of dropping its copy in include/<chip> and
 * adding a branch here; chips without one keep the esp32 copy, which is what
 * they were already getting.
 */

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/esp_wifi_types.h"
#else
#include "esp32/esp_wifi_types.h"
#endif

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created; former contents moved to esp32/
 */
