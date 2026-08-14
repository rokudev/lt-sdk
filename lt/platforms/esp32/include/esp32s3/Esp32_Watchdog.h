/******************************************************************************
 * Esp32_Watchdog.h                                                ESP32-S3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_WATCHDOG_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_WATCHDOG_H

#include "Esp32_Registers.h"

/*
 * The esp32s3 RTC watchdog keeps the esp32's register layout, only moved up in
 * the RTC_CNTL block, so these are the esp32 sequences unchanged.  The stage 0
 * hold count is counted in RTC_SLOW_CLK cycles - see the timeout conversion in
 * the watchdog driver, which differs from the esp32's.
 */

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
    /*
     * Disable the RTC watchdog, keeping the rest of the setup so a later
     * Esp32EnableRTCWatchdog() only has to put WDT_EN back.
     *
     * SETUP_DIS has FLASHBOOT_MOD_EN clear, so this clears both of the register's
     * two arming paths rather than clearing WDT_EN and re-asserting the other one
     * in the same store.  See the field breakdown in Esp32_Registers.h.
     *
     * The feed is so that the count starts from zero if anything enables the
     * watchdog again without setting a fresh timeout.
     */
    ESP32_REG(RTC_CNTL_WDTWPROTECT) = ESP32_REG_VAL(RTC_CNTL, WDT_UNPROTECT);
    ESP32_REG(RTC_CNTL_WDT_CONFIG0) = ESP32_REG_VAL(RTC_CNTL, WDT_SETUP_DIS);
    ESP32_REG(RTC_CNTL_WDTFEED)     = ESP32_REG_VAL(RTC_CNTL, WDT_FEED);
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

/*
 * Silence every watchdog this chip has, for the start of day.
 *
 * There are four, and the RTC watchdog above is only one of them.  Nothing in
 * the LT boot path feeds any of them, and the app inherits whatever the ROM and
 * the bootloader left armed, so each has to be dealt with explicitly:
 *
 *   RWDT   - disabled outright, above.
 *   SWD    - the super watchdog, which has no enable bit.  All that can be done
 *            is to leave hardware feeding it, which is what the bootloader does
 *            in bootloader_super_wdt_auto_feed() and what is re-asserted here,
 *            since a bootloader that jumped through a different path may not
 *            have.  It has its own write protect key.
 *   MWDT0  - armed by the ROM with flash boot protection, and the timer group
 *            clock is running at reset, so it counts whether or not anything has
 *            configured a timer.
 *   MWDT1  - not touched by the ROM, cleared here for completeness.
 *
 * MWDT's flashboot bit is BIT(14), not RWDT's BIT(12): the two peripherals have
 * unrelated register layouts and only the key is shared.
 *
 * Callable at any point, including before BSS is cleared - all of it is register
 * writes behind write protect keys, with no state of its own.
 */
LT_INLINE void Esp32DisableAllWatchdogs(void) {
    Esp32DisableRTCWatchdog();

    ESP32_REG(RTC_CNTL_SWDWPROTECT) = ESP32_REG_VAL(RTC_CNTL, SWD_UNPROTECT);
    ESP32_REG(RTC_CNTL_SWD_CONF)   |= ESP32_REG_MASK(RTC_CNTL_SWD_CONF, AUTO_FEED_EN);
    ESP32_REG(RTC_CNTL_SWDWPROTECT) = ESP32_REG_VAL(RTC_CNTL, SWD_PROTECT);

    ESP32_REG(TIMG0_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_UNPROTECT);
    ESP32_REG(TIMG0_WDT_CONFIG0) &= ~(ESP32_REG_MASK(TIMG, WDT_EN)
                                    | ESP32_REG_MASK(TIMG, WDT_FLASHBOOT_MOD_EN));
    ESP32_REG(TIMG0_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_PROTECT);

    ESP32_REG(TIMG1_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_UNPROTECT);
    ESP32_REG(TIMG1_WDT_CONFIG0) &= ~(ESP32_REG_MASK(TIMG, WDT_EN)
                                    | ESP32_REG_MASK(TIMG, WDT_FLASHBOOT_MOD_EN));
    ESP32_REG(TIMG1_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_PROTECT);
}

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_WATCHDOG_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 *  03-Aug-26   claudius    stopped Disable re-arming flashboot; added DisableAll
 *  10-Aug-26   claudius    added EnableRTCWatchdogSystemReset for the reboot path
 *  10-Aug-26   claudius    removed it again; the backstop wants a full chip reset
 */
