/******************************************************************************
 * Esp32_Watchdog.h                                                   ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_WATCHDOG_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_WATCHDOG_H

#include "Esp32_Registers.h"

LT_INLINE void Esp32SetTimeoutRTCWatchdog(u32 timeoutTicks) {
    // Set timeout for Watchdog stage 0
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_UNPROTECT);
    ESP32_REG_ARRAY_VALUE(RTC_CNTL_WDT_CONFIG0, 1) = timeoutTicks;
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_PROTECT);
}

LT_INLINE void Esp32EnableRTCWatchdog(void) {
    // Setup and enable RTC watchdog
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_UNPROTECT);
    ESP32_REG(RTC_CNTL_WDT_CONFIG0) = ESP32_REG_VAL(RTC_CNTL, WDT_SETUP_EN);
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_PROTECT);
}

LT_INLINE void Esp32DisableRTCWatchdog(void) {
    // Disable RTC watchdog, preserving setup
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_UNPROTECT);
    ESP32_REG(RTC_CNTL_WDT_CONFIG0) = ESP32_REG_VAL(RTC_CNTL, WDT_SETUP_DIS);
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_PROTECT);
}

LT_INLINE void Esp32PetRTCWatchdog(void) {
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_UNPROTECT);
    ESP32_REG(RTC_CNTL_WDTFEED)     = ESP32_REG_VAL(RTC_CNTL, WDT_FEED);
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_PROTECT);
}

LT_INLINE bool Esp32IsEnabledRTCWatchdog(void) {
    return (ESP32_REG(RTC_CNTL_WDT_CONFIG0) & ESP32_REG_MASK(RTC_CNTL, WDT_ENABLED));
}

#endif // #define PLATFORMS_ESP32_INCLUDE_ESP32_WATCHDOG_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Jun-22   tiberius    created
 */
