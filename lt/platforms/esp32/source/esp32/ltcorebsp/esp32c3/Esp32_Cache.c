/******************************************************************************
 * Esp32_Cache.c                                                   ESP32-C3 BSP
 *
 * Instruction cache bring-up.
 *
 * This part has one cache, serving both buses, and its geometry is fixed in
 * hardware at 16KB - so where the esp32s3 file beside this one has to pin two
 * cache sizes before anything else can be trusted, there is nothing here to
 * configure and nothing carved out of SRAM to account for.  What remains is the
 * part both chips share: the cache comes out of reset shut and disabled
 * (ICACHE_CTRL1's two SHUT bits reset to 1, ICACHE_CTRL's ENABLE to 0), and the
 * ROM has no idea where this image split the MMU table between the instruction
 * and the data bus.
 *
 * Esp32_CacheInitialize() is the first thing call_start_cpu0() does, and after it
 * the cache is not touched again for the life of the image.  Everything here runs
 * from IRAM, and so must everything it calls - reconfiguring the cache pulls the
 * ground out from under any code fetched from flash.
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
 * for esp_rom_printf().  Both are PROVIDEd by
 * mastering/ld/esp32c3/rom/esp32c3.rom.ld.
 *****************************************************************************/

/* Enables the instruction cache, discarding whatever autoload and lock state it
   was holding.  nAutoload is the state to come up with. */
void Cache_Enable_ICache(u32 nAutoload);

/* Divides the shared MMU table between the instruction and the data bus.  Both
   sizes are in bytes of table, not in entries or pages, and irom's comes first. */
u32 Cache_Set_IDROM_MMU_Size(u32 nIRomBytes, u32 nDRomBytes);

/* Start of the flash rodata, from mastering/ld/esp32c3/sections.ld.  This is
 * where irom's MMU entries stop and drom's begin. */
extern int _rodata_reserved_start;

/******************************************************************************
 * bring-up
 *****************************************************************************/

/*
 * Tell the cache where irom's MMU entries end and drom's begin.
 *
 * One 128 entry MMU table serves both buses and is indexed by virtual address
 * alone, so irom at 0x42000000 and drom at 0x3C000000 index it from the same end.
 * sections.ld keeps them apart by giving drom a NOLOAD gap the size of the flash
 * text segment, which leaves irom holding the low entries and drom immediately
 * above it.  Nothing in that arrangement is visible to the ROM's cache code,
 * which still believes the split is wherever it left it, so it has to be told.
 * The ROM takes both sizes in bytes of table rather than in pages, and takes
 * irom's first, which is why irom has to be the low one.
 */
static void ESP32_IRAM_FUNC
_Esp32_CacheSplitFlashMMUTable(void) {
    u32 nRodataStart = (u32)&_rodata_reserved_start & ~(u32)(kEsp32_RegisterMMU_PAGE_SIZE - 1);
    u32 nIRomBytes   = ((nRodataStart - kEsp32_RegisterMMU_DBUS_LOW) / kEsp32_RegisterMMU_PAGE_SIZE)
                     * sizeof(u32);
    u32 nTableBytes  = kEsp32_RegisterMMU_DROM_MAX_END * sizeof(u32);

    Cache_Set_IDROM_MMU_Size(nIRomBytes, nTableBytes - nIRomBytes);
}

void ESP32_MEM_REGION(IRAM)
Esp32_CacheInitialize(void) {
    /*
     * Un-shut both buses first.  The second stage bootloader clears these in
     * bootloader_reset_mmu(), but the ROM does not, and nothing below has any
     * effect while they are set.
     */
    ESP32_REG(EXTMEM_ICACHE_CTRL1) &= ~(ESP32_REG_MASK(EXTMEM_ICACHE_CTRL1, SHUT_IBUS)
                                      | ESP32_REG_MASK(EXTMEM_ICACHE_CTRL1, SHUT_DBUS));

    /*
     * Enable the cache, without which no flash text can be fetched.  Done
     * idempotently: enabling an already-enabled cache would drop its current
     * autoload and lock state on the floor.
     */
    if (!(ESP32_REG(EXTMEM_ICACHE_CTRL) & ESP32_REG_MASK(EXTMEM_ICACHE_CTRL, ENABLE))) {
        Cache_Enable_ICache(0);
    }

    /* Before anything maps flash through the cache. */
    _Esp32_CacheSplitFlashMMUTable();
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
