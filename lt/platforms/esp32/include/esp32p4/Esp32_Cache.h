/******************************************************************************
 * Esp32_Cache.h                                                   ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_CACHE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_CACHE_H

#include <lt/LTTypes.h>

/*
 * Bring up the caches that make flash readable.
 *
 * Called first thing in the chip start path.  Until it returns, nothing outside
 * on-chip SRAM can be executed or read, so it and everything it calls must be
 * in IRAM.
 *
 * The esp32p4 has three caches - a per core L1 instruction cache, a per core L1
 * data cache, and a shared L2 - but only the L2 backs external memory, so it is
 * the only one enabled here; the L1s are left as the ROM configured them.
 * Unlike the esp32c3 there is no enable bit to test, so there is no way to ask
 * the hardware whether the bootloader already left a cache running; the ROM
 * enable call is made unconditionally and is safe to repeat.
 *
 * There is also no instruction/data split to configure.  Both buses reach flash
 * through the same addresses on this part, so the MMU maps one window and there
 * is no counterpart to the esp32c3's IROM/DROM table split.
 */
void Esp32_CacheInitialize(void);

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_CACHE_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
