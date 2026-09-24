/******************************************************************************
 * Esp32_Cache.c                                                   ESP32-P4 BSP
 *
 * Cache bring-up.
 *
 * External flash is reached through the L2 cache on this part - the L1 caches
 * sit in front of it and serve internal SRAM as well - so the L2 is the one
 * thing that has to be running before a single byte of flash text or rodata can
 * be touched.  That makes this the first thing call_start_cpu0() does, and once
 * the stale lines the bootloader left behind are gone the cache is not touched
 * again for the life of the image.
 *
 * Two things the esp32c3 file beside this one has to do have no counterpart
 * here.  There are no SHUT bits gating the CPU's buses, and there is no IROM /
 * DROM split to declare: both buses reach flash through the same window, so the
 * MMU maps it once and Cache_Set_IDROM_MMU_Size() - which the ROM does export -
 * has nothing to divide.
 *
 * Everything here runs from IRAM, and so must everything it calls:
 * reconfiguring the cache pulls the ground out from under any code fetched from
 * flash.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Cache.h"

/******************************************************************************
 * ROM entry points
 *
 * Declared here rather than in a header, following the pattern Esp32_SoC.h uses
 * for esp_rom_printf().  PROVIDEd by mastering/ld/esp32p4/rom/esp32p4.rom.ld.
 *****************************************************************************/

/* Enables the L2 cache.  nAutoload is the preload state to come up with; 0 asks
   for none, which is what IDF's cache_hal_init() passes when the hardware it
   read the state from had preload off. */
void Cache_Enable_L2_Cache(u32 nAutoload);

/* Drops any cache lines covering [nAddr, nAddr + nSize).  nMap selects which
   caches; the ROM header's own bit for the L1 data cache is BIT(4). */
int Cache_Invalidate_Addr(u32 nMap, u32 nAddr, u32 nSize);

#define CACHE_MAP_L1_DCACHE 0x10

/* End of the flash window this image occupies; see mastering/ld/esp32p4. */
extern int _text_end;

/******************************************************************************
 * bring-up
 *****************************************************************************/

void ESP32_MEM_REGION(IRAM)
Esp32_CacheInitialize(void) {
    /*
     * Enabled unconditionally, because this part has no register reporting
     * whether a cache is on - IDF tracks that in software instead - so there is
     * nothing to test and no way to be idempotent.  Enabling a cache that is
     * already enabled only invalidates its tags, which costs nothing here: the
     * flash mappings live in the MMU, not in the cache, and this code is running
     * from SRAM.
     *
     * The L1 caches are left exactly as the ROM configured them.  Only the L2
     * backs external memory, and IDF's own cache_hal_init() likewise enables
     * just the level that does.
     */
    Cache_Enable_L2_Cache(0);

    /*
     * The bootloader read the partition table and this image's header through a
     * flash mapping at the same virtual addresses the image's own rodata now
     * occupies, and its handoff invalidates only the L1 instruction cache.  So
     * the L1 data cache can still hold lines from that old mapping, and a read
     * of flash rodata returns whatever the bootloader happened to have there.
     * Drop them by address: the bootloader cannot do this itself without also
     * discarding the dirty write-back lines holding its own stack.
     */
    Cache_Invalidate_Addr(CACHE_MAP_L1_DCACHE, kEsp32_RegisterMMU_FLASH_LOW,
                          (u32)&_text_end - kEsp32_RegisterMMU_FLASH_LOW);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 */
