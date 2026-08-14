/******************************************************************************
 * Esp32_Cache.h                                                   ESP32-S3 BSP
 *
 * Instruction and data cache bring-up, and the suspend/resume pair that anything
 * reprogramming the cache MMU needs.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CACHE_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_CACHE_H

#include <lt/LTTypes.h>

/*
 * Bring both caches up, from the first thing call_start_cpu0() does.
 *
 * Nothing before this point may call into flash, and after it the geometry is
 * fixed for the life of the image - the memory map depends on it, so nothing may
 * change it later.  See the implementation for the ordering and for what each
 * step is worth.
 */
void Esp32_CacheInitialize(void);

/*
 * Stop and restart the data cache, around a change to what it is caching.
 *
 * Suspend returns the autoload state to hand back to resume, which is also the
 * only way to enable the cache without discarding that state.  Both are safe to
 * call before Esp32_CacheInitialize().
 *
 * Callers must be IRAM resident for the duration, and must not touch any flash
 * rodata in between: the data bus is what serves flash constants on this part.
 */
u32  Esp32_CacheSuspendDCache(void);
void Esp32_CacheResumeDCache(u32 nAutoload);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CACHE_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  03-Aug-26   claudius    created
 */
