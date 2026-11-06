/******************************************************************************
 * Esp32_Cache.h                                                   ESP32-C3 BSP
 *
 * Instruction cache bring-up.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_CACHE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32C3_CACHE_H

#include <lt/LTTypes.h>

/*
 * Bring the cache up, from the first thing call_start_cpu0() does.
 *
 * Nothing before this point may call into flash, and after it the MMU split is
 * fixed for the life of the image - the memory map depends on it, so nothing may
 * change it later.  See the implementation for the ordering and for what each
 * step is worth.
 *
 * Unlike the esp32s3 this part has one cache, serving both buses, and its
 * geometry is fixed in hardware at 16KB - there is nothing to configure, so the
 * counterpart header on that part carries a suspend/resume pair this one does
 * not need.  The flash driver still has to stop the cache around an erase or a
 * write; it reaches the ROM routines directly for that.
 */
void Esp32_CacheInitialize(void);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_CACHE_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
