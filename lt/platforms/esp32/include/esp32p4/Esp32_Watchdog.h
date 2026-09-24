/******************************************************************************
 * Esp32_Watchdog.h                                                ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32p4 has four watchdogs and all four are awake out of reset:
 *
 *   RWDT   the always-on watchdog, in the LP_WDT block.  The one LT actually
 *          uses, because it is the only one that survives sleep and can reset
 *          the whole chip rather than just a core.
 *   SWD    the super watchdog, sharing the LP_WDT block.  It cannot be turned
 *          off, only fed, and AUTO_FEED_EN hands that to hardware.
 *   MWDT0  in timer group 0, and
 *   MWDT1  in timer group 1.  Both armed by FLASHBOOT_MOD_EN out of reset and
 *          both unused here, so both are disabled outright.
 *
 * Every one of them is behind a write lock: the key goes into the block's
 * WPROTECT register, the change is made, and zero goes back.  Leaving a lock
 * open is how a stray write reboots the part.
 *
 * Two things differ from the esp32c3 and both will bite a reader who assumes
 * otherwise.  The RWDT is in LP_WDT rather than RTC_CNTL, with the CONFIG0
 * fields rearranged and two of them gone, so the esp32c3's packed setup
 * constants are not the same numbers.  And SWD's auto-feed bit is bit 18 here
 * rather than bit 31 - writing bit 31 would trigger a single feed and leave
 * auto-feed off, which looks like it worked until the watchdog bites.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_WATCHDOG_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_WATCHDOG_H

#include "Esp32_Registers.h"

/*
 * Set the RWDT stage 0 timeout, in RTC slow clock ticks.
 *
 * CONFIG1 is the word immediately after CONFIG0 and its whole 32 bits are the
 * hold count, so it is reached as an array element - the same idiom the other
 * esp32 variants use.
 *
 * The count is multiplied by hardware according to EFUSE_WDT_DELAY_SEL, which
 * is at least two, so a caller that wants a specific wall clock timeout has to
 * read the efuse and divide.  Esp32p4DriverWatchdog does.
 */
LT_INLINE void
Esp32SetTimeoutRTCWatchdog(u32 timeoutTicks) {
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_UNPROTECT);
    ESP32_REG_ARRAY_VALUE(LP_WDT_CONFIG0, 1) = timeoutTicks;
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_PROTECT);
}

LT_INLINE void
Esp32EnableRTCWatchdog(void) {
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_UNPROTECT);
    ESP32_REG(LP_WDT_CONFIG0)     = ESP32_REG_VAL(LP_WDT, WDT_SETUP_EN);
    ESP32_REG(LP_WDT_WDTFEED)     = ESP32_REG_VAL(LP_WDT, WDT_FEED);
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_PROTECT);
}

/*
 * Disable the RWDT.  The setup word is written with WDT_EN clear rather than
 * just clearing the bit, because FLASHBOOT_MOD_EN also arms the watchdog and
 * resets set - clearing WDT_EN alone leaves it running.
 */
LT_INLINE void
Esp32DisableRTCWatchdog(void) {
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_UNPROTECT);
    ESP32_REG(LP_WDT_CONFIG0)     = ESP32_REG_VAL(LP_WDT, WDT_SETUP_DIS);
    ESP32_REG(LP_WDT_WDTFEED)     = ESP32_REG_VAL(LP_WDT, WDT_FEED);
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_PROTECT);
}

LT_INLINE void
Esp32PetRTCWatchdog(void) {
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_UNPROTECT);
    ESP32_REG(LP_WDT_WDTFEED)     = ESP32_REG_VAL(LP_WDT, WDT_FEED);
    ESP32_REG(LP_WDT_WDTWPROTECT) = ESP32_REG_VAL(LP_WDT, WDT_PROTECT);
}

LT_INLINE bool
Esp32IsEnabledRTCWatchdog(void) {
    return (ESP32_REG(LP_WDT_CONFIG0) & ESP32_REG_MASK(LP_WDT, WDT_ENABLED)) ? true : false;
}

/*
 * Silence every watchdog on the part.  Called early in chip start, before
 * anything is in a position to pet one.
 *
 * SWD is fed by hardware rather than disabled, because it has no disable.
 */
LT_INLINE void
Esp32DisableAllWatchdogs(void) {
    Esp32DisableRTCWatchdog();

    ESP32_REG(LP_WDT_SWDWPROTECT) = ESP32_REG_VAL(LP_WDT, SWD_UNPROTECT);
    ESP32_REG(LP_WDT_SWD_CONF)   |= ESP32_REG_MASK(LP_WDT_SWD_CONF, AUTO_FEED_EN);
    ESP32_REG(LP_WDT_SWDWPROTECT) = ESP32_REG_VAL(LP_WDT, SWD_PROTECT);

    ESP32_REG(TIMG0_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_UNPROTECT);
    ESP32_REG(TIMG0_WDT_CONFIG0) &= ~(ESP32_REG_MASK(TIMG, WDT_EN) | ESP32_REG_MASK(TIMG, WDT_FLASHBOOT_MOD_EN));
    ESP32_REG(TIMG0_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_PROTECT);

    ESP32_REG(TIMG1_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_UNPROTECT);
    ESP32_REG(TIMG1_WDT_CONFIG0) &= ~(ESP32_REG_MASK(TIMG, WDT_EN) | ESP32_REG_MASK(TIMG, WDT_FLASHBOOT_MOD_EN));
    ESP32_REG(TIMG1_WDTWPROTECT)  = ESP32_REG_VAL(TIMG, WDT_PROTECT);
}

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_WATCHDOG_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
