/*
 * ESP32-P4 Linker Script Memory Layout
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 */

/* This file describes the memory layout (memory blocks) as virtual memory
   addresses; sections.ld contains the output sections that link compiler output
   into these blocks.

   THIS IS THE ESP32-P4 SILICON v3.x LAYOUT.  It is the shared esp32p4 memory.ld
   (source/esp32/mastering/ld/esp32p4/memory.ld, which is for silicon before v3)
   with the L2MEM map changed, and nothing else changed.  sections.ld and
   esp32p4.peripherals.ld beside it are copies of the shared ones; the ROM scripts
   in rom/ are Espressif's for ROM eco5 and later.

   What differs.  IDF v6.1 keeps two memory maps for this part, selected by
   CONFIG_ESP32P4_SELECTS_REV_LESS_V3 (components/esp_system/ld/esp32p4/memory.ld.in):

     before v3   the ROM reserves 0x4FF2ABD0..0x4FF40000 for download buffers, the
                 CPU1 stack and its own bss, and the L2 cache is carved off the
                 very top.  That is the shared map.
     v3 and on   one contiguous run, from 0x4FF00000 plus the L2 cache size up to
                 0x4FFADFC0 - the cache is at the BOTTOM, and the ROM's bss, data and
                 stack have moved to the top of L2MEM (rom_spiflash_legacy_data is
                 at 0x4FFBFFE8 and the ROM stack was at 0x4FFBCDA0 when this was
                 written).

   Observed on a v3.2 part (ROM esp32p4-eco7): code linked at 0x4FF00000 and at
   0x4FF10000 takes an illegal instruction on its first fetch, while code at
   0x4FF2ABD0 and at 0x4FF50000 runs - the first 128KB is cache.  The shared map put
   the application's IRAM at 0x4FF00000, which is why an application built with it
   cannot start on this silicon.

   The cache size.  This part has no register reporting the L2 cache size, nothing
   in LT sets it, and so it is whatever the ROM left.  128KB is IDF's default, is
   consistent with the observation above (the second stage bootloader runs at
   0x4FF2ABD0, so the cache cannot be larger than 0x2ABD0), and is what is assumed
   here: SRAM_LOW_START below.  If the ROM ever left it larger, the first thing
   to go wrong is the application's IRAM at SRAM_LOW_START.

   Where things go.  The second stage bootloader is unchanged and still runs at
   0x4FF2ABD0, so the application's loadable sections (IRAM, static data, the trap
   stack) must stay below it, in sram_low_seg: the bootloader would be overwriting
   itself otherwise.  That leaves 0xABD0 bytes, against about 0x41F0 used.  The
   bootloader's own footprint, dead once it hands over, is heap1 as before.  Above
   it the whole of the old ROM-reserved area and the old high run is ordinary
   memory on this silicon, and is heap4, up to IDF's 0x4FFADFC0.

   The shared map's dram_stack_seg and dram_rom0_seg are gone: nothing in
   sections.ld refers to them, and on this silicon the memory they described is
   not ROM's.  Startup still runs on the ROM stack, which is now at the top of
   L2MEM (above SRAM_HIGH_END) and is reached by whatever SP the bootloader
   hands over with.

   Other points that do not change:

     - flash irom and drom share one *virtual* window.  Both start at 0x40000000
       (SOC_IRAM0_CACHE_ADDRESS_LOW == SOC_DRAM0_CACHE_ADDRESS_LOW), 1024 MMU
       entries of 64KB spanning 64MB, declared here as the single idrom_seg.  So
       the rodata and the text share one contiguous run and one set of MMU
       entries, and there is no Cache_Set_IDROM_MMU_Size() on this part to tell
       the hardware where the split falls, because there is no split;
     - there is no D/IRAM alias: HP L2MEM is one window reached by both buses at the
       same addresses, so IRAM and DRAM sections simply follow one another;
     - there is one LP(RTC) block rather than distinguished fast and slow, so
       rtc_iram_seg, rtc_data_seg and rtc_slow_seg all name the same memory.

   SRAM regions:
      segment/section    start    end      size
      -------------------------------------------------------
      <L2 cache>         4FF00000 4FF20000 020000 (assumed 128KB)
      sram_low_seg       4FF20000 4FF2ABD0 00ABD0 ( 43984 bytes)
         .iram0.*
         .dram0.data
         .dram0.bss
         .stack
         .heap0
      dram_app1_seg      4FF2ABD0 4FF38BD0 00E000 ( 57344 bytes)
         .heap1
      sram_high_seg      4FF38BD0 4FFADFC0 0751F0 (479728 bytes)
         .heap4
*/

/* CPU instruction prefetch padding size for flash mmap scenario */
_esp_flash_mmap_prefetch_pad_size = 16;
/* CPU instruction prefetch padding size for memory protection scenario */
_esp_memprot_prefetch_pad_size = 0;
/* Memory alignment size for PMS */
_esp_memprot_align_size = 0;

/* End of the low SRAM run available to the app.  This is where the second stage
   bootloader's iram_loader_seg begins, so it must match the ASSERT in
   ltbootloader/ld/esp32p4/bootloader.ld.  Note that it is 0x2000 below the
   vendor's 0x4FF2CBD0: LT merges the vendor's iram_loader_seg and iram_seg into
   one segment, so the bootloader starts lower even though it occupies the same
   window overall. */
SRAM_LOW_END      = 0x4FF2ABD0;

/* Start of the low run: the end of the L2 cache, which is at the bottom of L2MEM on
   this silicon.  See the header comment for the 128KB this assumes. */
SRAM_LOW_START    = 0x4FF00000 + 0x20000;

/* End of the application's memory: IDF's SRAM_END for this silicon, below the ROM's
   own bss, data and stack at the top of L2MEM. */
SRAM_HIGH_END     = 0x4FFADFC0;

MEMORY
{
  /* All these values assume the flash cache is on, and have the blocks this uses
  subtracted from the length of the various regions.  Unlike the other esp32
  parts there is no separate 'data access port' view: both buses see L2MEM at the
  same addresses. */

  /* The low run of L2MEM: above the L2 cache, below the second stage bootloader.
  Executable as well as writable - .iram0.* and .dram0.* both land here. */
  sram_low_seg  (RWX) : org = SRAM_LOW_START, len = SRAM_LOW_END - SRAM_LOW_START

  /* Flash mapped code and constant data.  One region, not the two the other
  parts declare, because on this part there genuinely is only one: a single 1024
  entry MMU table serves both buses and both see it at 0x40000000, so irom and
  drom are the same 64MB of virtual address space.  Declaring it twice would give
  ld two independent allocation counters over the same addresses and each would
  hand out space the other had already used.  sections.ld lays the rodata out
  first and the text straight after it. */
  idrom_seg (RX) : org = 0x40000020, len = 0x4000000-0x20
  /*
    (0x20 offset above is a convenience for the app binary image generation.
    Flash cache has 64KB pages. The .bin file which is flashed to the chip
    has a 0x18 byte file header, and each segment has a 0x08 byte segment
    header. Setting this offset makes it simple to meet the flash cache MMU's
    constraint that (paddr % 64KB == vaddr % 64KB).)
  */

  dram_app1_seg  (RW) : org = 0x4FF2ABD0, len = 0x0E000

  /* Everything above the second stage bootloader's footprint, up to IDF's SRAM_END for
  this silicon.  Data only - nothing is executed from here. */
  sram_high_seg  (RW) : org = 0x4FF38BD0, len = SRAM_HIGH_END - 0x4FF38BD0

  /* LP(RTC) memory, persists over deep sleep.  This part has a single unified LP
  RAM block rather than distinguished fast and slow, so soc.h points
  SOC_RTC_IRAM_*, SOC_RTC_DRAM_* and SOC_RTC_DATA_* all at this same range.  The
  length stops at LP_ROM_DRAM_START (0x5010FA80) rather than running the full
  0x8000, because the LP ROM's own stack and data sit above that. */
  rtc_slow_seg(RW) : org = 0x50108000, len = 0x7A80
}

_static_data_end = _bss_end;
_data_seg_org = ORIGIN(rtc_slow_seg);

  /* rtc_data_location exists so sections.ld can stay the shape of the esp32c3's,
     where the alias selects between RTC fast and RTC slow.  Here there is only
     one LP RAM block to select. */
  REGION_ALIAS("rtc_data_location", rtc_slow_seg );
  /* Both aliases name the one flash window; see idrom_seg above. */
  REGION_ALIAS("default_code_seg", idrom_seg);
  REGION_ALIAS("default_rodata_seg", idrom_seg);

/**
 *  The flash rodata must start at the very beginning of the flash window.  The
 *  application descriptor is the first thing in it, and both the bootloader and
 *  elf2image locate that descriptor by a fixed offset into the image; see the
 *  header comment on .flash.appdesc in sections.ld.
 */
  ASSERT(_rodata_reserved_start == ORIGIN(default_rodata_seg),
         "the flash rodata must be placed at the beginning of the flash window.")

  /* The three runs of L2MEM must be contiguous. */
  ASSERT(ORIGIN(dram_app1_seg) + LENGTH(dram_app1_seg) == ORIGIN(sram_high_seg),
         "dram_app1_seg does not reach the start of sram_high_seg")

  ASSERT(ORIGIN(sram_high_seg) + LENGTH(sram_high_seg) == SRAM_HIGH_END,
         "sram_high_seg does not end at SRAM_HIGH_END")

  ASSERT(ORIGIN(sram_low_seg) + LENGTH(sram_low_seg) == SRAM_LOW_END,
         "sram_low_seg does not end where the second stage bootloader begins - see the matching ASSERT in ltbootloader/ld/esp32p4/bootloader.ld")

  ASSERT(ORIGIN(dram_app1_seg) == SRAM_LOW_END,
         "dram_app1_seg does not start at the second stage bootloader's iram_loader_seg")

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created (shared esp32p4 memory.ld, silicon before v3)
 *  03-Nov-26   claudius    p4v3: the silicon v3.x L2MEM map
 */
