/******************************************************************************
 * Esp32_Cache.c                                                   ESP32-S3 BSP
 *
 * Instruction and data cache bring-up.
 *
 * Neither cache comes out of reset running: EXTMEM_{I,D}CACHE_CTRL's ENABLE
 * resets to 0 and CTRL1's two SHUT bits reset to 1.  The second stage bootloader
 * needs neither, since it is loaded whole into RAM, so it leaves the instruction
 * side alone apart from clearing the SHUT bits in bootloader_reset_mmu().  The
 * data side it brings up as a side effect: set_cache_and_start_app() brackets its
 * MMU writes in Cache_Suspend_DCache()/Cache_Resume_DCache(), and the resume
 * enables DCache.  Nothing sets either cache's geometry, and nothing tells the
 * ROM where this image split the shared MMU table.  That is this file's job, and
 * it is all of it - Esp32_CacheInitialize() is the first thing call_start_cpu0()
 * does, and after it the caches are not touched again for the life of the image.
 *
 * Every function here runs from IRAM, and so must everything they call.
 * Reconfiguring the instruction cache pulls the ground out from under any code
 * fetched from flash, and reconfiguring the data cache does the same to flash
 * rodata.
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
 * Declared here rather than in a header, following the pattern Esp32_SoC.h
 * uses for esp_rom_printf().  All of these are PROVIDEd by
 * mastering/ld/esp32s3/rom/esp32s3.rom.ld.
 *****************************************************************************/

/* Set the cache geometry.  These take care of the bank occupancy that decides
   how much of SRAM0 and SRAM2 the caches consume, which is why the raw
   Cache_Set_{I,D}Cache_Mode entry points are not used directly.  Each also
   suspends and resumes the cache it is reconfiguring. */
void rom_config_instruction_cache_mode(u32 nCacheSize, u8 nWays, u8 nLineSize);
void rom_config_data_cache_mode(u32 nCacheSize, u8 nWays, u8 nLineSize);

/* Enables the instruction cache, discarding whatever autoload and lock state it
   was holding.  nAutoload is the state to come up with. */
void Cache_Enable_ICache(u32 nAutoload);

/* Divides the shared MMU table between the instruction and the data bus.  Both
   sizes are in bytes of table, not in entries or pages, and irom's comes first. */
u32 Cache_Set_IDROM_MMU_Size(u32 nIRomBytes, u32 nDRomBytes);

/* Stops the data cache and returns its autoload state; the argument to the
   matching resume.

   The name is deliberate.  On this silicon the ROM's Cache_Suspend_DCache()
   can return before the cache has actually gone idle, so IDF renames the ROM
   symbol to rom_Cache_Suspend_DCache and ships a patched wrapper that spins on
   the cache state register (esp_rom/patches/esp_rom_cache.c, guarded by
   ESP_ROM_HAS_CACHE_SUSPEND_WAITI_BUG).  LT vendors that patch on the bootloader
   side (ltbootloader/esp32s3/esp_rom_cache.c) but not app side, so
   Esp32_CacheSuspendDCache() below repeats the spin locally.  Vendoring the
   patch here too would want the same symbol-rename link flag the bootloader
   uses; until something else needs it, one spin in one place is cheaper. */
u32  rom_Cache_Suspend_DCache(void);
void Cache_Resume_DCache(u32 nAutoload);

/******************************************************************************
 * constants
 *****************************************************************************/

/* Cache geometry.  These are IDF's defaults for the part, and the data cache
 * size is load bearing rather than a preference - see Esp32_CacheInitialize(). */
enum {
    kICache_SizeBytes                   = 32 * 1024,
    kICache_Ways                        = 8,
    kICache_LineSizeBytes               = 32,

    kDCache_SizeBytes                   = 32 * 1024,
    kDCache_Ways                        = 8,
    kDCache_LineSizeBytes               = 32,
};

enum {
    /* Cache state register reports 1 when the cache has gone idle. */
    kCache_State_Idle                   = 1,
};

/* Start of the flash rodata, from mastering/ld/esp32s3/sections.ld.  This is
 * where irom's MMU entries stop and drom's begin. */
extern int _rodata_reserved_start;

/******************************************************************************
 * data cache suspend and resume
 *****************************************************************************/

u32 ESP32_MEM_REGION(IRAM)
Esp32_CacheSuspendDCache(void) {
    u32 nAutoload = rom_Cache_Suspend_DCache();
    while (((ESP32_REG(EXTMEM_CACHE_STATE) & ESP32_REG_MASK(EXTMEM_CACHE_STATE, DCACHE))
            >> ESP32_REG_SHIFT(EXTMEM_CACHE_STATE, DCACHE)) != kCache_State_Idle);
    return nAutoload;
}

void ESP32_MEM_REGION(IRAM)
Esp32_CacheResumeDCache(u32 nAutoload) {
    Cache_Resume_DCache(nAutoload);
}

/******************************************************************************
 * bring-up
 *****************************************************************************/

/*
 * Tell the cache where irom's MMU entries end and drom's begin.
 *
 * The esp32s3 has one 512 entry MMU table shared by the instruction and the data
 * bus, indexed by virtual address alone, so irom at 0x42000000 and drom at
 * 0x3C000000 index it from the same end.  sections.ld keeps them apart by giving
 * drom a NOLOAD gap the size of the flash text segment, which leaves irom holding
 * the low entries and drom immediately above it.  Nothing in that arrangement is
 * visible to the ROM's cache code, which still believes the split is wherever it
 * left it, so it has to be told: the boundary derived from _rodata_reserved_start
 * below has to be the same boundary the linker produced, or the next
 * Cache_Dbus_MMU_Set() - PSRAM's, in Esp32_PSRAM.c - is validated against the
 * wrong range.  The ROM takes both sizes in bytes of table rather than in pages,
 * and takes irom's first, which is the other reason irom has to be the low one.
 *
 * DROM_MAX_END is the top of the flash half of the table; the rest belongs to
 * PSRAM.  It is held here in entries, hence the scaling to bytes.
 */
static void ESP32_IRAM_FUNC
_Esp32_CacheSplitFlashMMUTable(void) {
    u32 nRodataStart = (u32)&_rodata_reserved_start & ~(u32)(kEsp32_RegisterMMU_PAGE_SIZE - 1);
    u32 nIRomBytes   = ((nRodataStart - kEsp32_RegisterMMU_DBUS_LOW) / kEsp32_RegisterMMU_PAGE_SIZE)
                     * sizeof(u32);
    u32 nTableBytes  = kEsp32_RegisterMMU_DROM_MAX_END * sizeof(u32);

    Cache_Set_IDROM_MMU_Size(nIRomBytes, nTableBytes - nIRomBytes);
}

/*
 * Fix the geometry of both caches.
 *
 * The data cache size is the load bearing one.  SRAM2 is 64KB at 0x3FCF0000 and
 * the data cache is carved out of the top of it, so a 32KB cache leaves exactly
 * 0x8000 free at 0x3FCF0000 - which is dram_app3_seg, and therefore heap3.  A
 * 64KB cache would swallow the whole of SRAM2 and heap3 would be handing out
 * cache tag memory.  Nothing in the ROM or the second stage bootloader guarantees
 * a size, so mastering/ld/esp32s3/memory.ld's 0x8000 is only true because of this
 * call.  IDF makes the same choice by default and registers the same
 * 0x3FCF0000/0x8000 block as heap (components/heap/port/esp32s3/memory_layout.c).
 *
 * The instruction cache size matters less but is pinned for the same reason: at
 * 32KB it consumes all of SRAM0, which is why SRAM0 appears in no segment in the
 * memory map.
 *
 * There is nothing dirty in the data cache to lose here - it is backing read-only
 * flash rodata at this point, and PSRAM is not mapped yet - so a plain
 * suspend/resume around the change is enough.
 */
static void ESP32_IRAM_FUNC
_Esp32_CachePinGeometry(void) {
    rom_config_instruction_cache_mode(kICache_SizeBytes, kICache_Ways, kICache_LineSizeBytes);

    /* IDF resumes with autoload 0 here rather than restoring what suspend
     * returned, and the reconfigured cache should not start autoloading against
     * the old geometry, so the saved value is deliberately dropped. */
    (void)Esp32_CacheSuspendDCache();
    rom_config_data_cache_mode(kDCache_SizeBytes, kDCache_Ways, kDCache_LineSizeBytes);
    Cache_Resume_DCache(0);
}

void ESP32_MEM_REGION(IRAM)
Esp32_CacheInitialize(void) {
    /*
     * Un-shut the instruction cache first.  The bootloader clears these already,
     * but the ROM does not, and nothing below has any effect while they are set.
     */
    ESP32_REG(EXTMEM_ICACHE_CTRL1) &= ~(ESP32_REG_MASK(EXTMEM_ICACHE_CTRL1, SHUT_CORE0)
                                      | ESP32_REG_MASK(EXTMEM_ICACHE_CTRL1, SHUT_CORE1));

    /*
     * Geometry before anything else that depends on it, which is everything.
     * This is also the order IDF uses (esp_system/port/cpu_start.c): both
     * rom_config_*_cache_mode() calls, then Cache_Set_IDROM_MMU_Size().
     */
    _Esp32_CachePinGeometry();

    /*
     * Enable the instruction cache, without which no flash text can be fetched -
     * which is exactly as far as the boot used to get, into
     * Esp32_ClockInitialize()'s call to the one function in that file that is not
     * IRAM resident.
     *
     * rom_config_instruction_cache_mode() above ends in a resume that appears to
     * enable the cache as a side effect, which is how unicore IDF gets ICache on
     * at all - its do_multicore_settings(), which disables and re-enables both
     * caches to force core 1's settings to match core 0's, is compiled out of
     * unicore builds and so cannot be the whole story there.  That side effect is
     * undocumented, so do it deliberately rather than inherit it, and do it
     * idempotently: enabling an already-enabled cache would drop its current
     * autoload and lock state on the floor.
     */
    if (!(ESP32_REG(EXTMEM_ICACHE_CTRL) & ESP32_REG_MASK(EXTMEM_ICACHE_CTRL, ENABLE))) {
        Cache_Enable_ICache(0);
    }

    /* Before anything maps flash or PSRAM through the cache. */
    _Esp32_CacheSplitFlashMMUTable();

    /*
     * Read the data cache size back.  If the ROM did not do what was asked, heap3
     * is going to be handed out on top of live cache tag memory, which corrupts
     * silently and at random.  There is no way to retract the region from here, so
     * say so as loudly as this stage of startup allows.
     */
    if (ESP32_REG(EXTMEM_DCACHE_CTRL) & ESP32_REG_MASK(EXTMEM_DCACHE_CTRL, SIZE_MODE)) {
        esp_rom_printf("cache: data cache is 64KB, not the 32KB heap3 assumes - SRAM2 heap is UNSAFE\n");
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  03-Aug-26   claudius    created, from Esp32_LTChipStart.c and Esp32_PSRAM.c
 *  05-Aug-26   claudius    ESP32_IRAM_FUNC
 */
