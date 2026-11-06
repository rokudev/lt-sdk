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

   The shape is simpler than the esp32c3's and the esp32s3's, because this part
   has no D/IRAM alias.  HP L2MEM is one window at 0x4FF00000..0x4FFC0000 reached
   by both buses at the same addresses - SOC_IRAM_LOW == SOC_DRAM_LOW and
   SOC_IRAM_HIGH == SOC_DRAM_HIGH - so IRAM and DRAM sections simply follow one
   another in a single region.  There is no iram_dram_offset and no .dram0.dummy
   growth section.

   Where this part differs from the esp32c3 in the other direction:

     - flash irom and drom share one *virtual* window.  Both start at 0x40000000
       (SOC_IRAM0_CACHE_ADDRESS_LOW == SOC_DRAM0_CACHE_ADDRESS_LOW), 1024 MMU
       entries of 64KB spanning 64MB, declared here as the single idrom_seg.  So
       the rodata and the text share one contiguous run and one set of MMU
       entries, and there is no Cache_Set_IDROM_MMU_Size() on this part to tell
       the hardware where the split falls, because there is no split;
     - the 768KB of L2MEM is not contiguous from the app's point of view.  The
       ROM reserves 0x4FF2ABD0..0x4FF40000 for download buffers, the CPU1 stack
       and its own bss, so the app gets a low run below that and a high run above
       it, with the L2 cache carved off the very top;
     - there is one LP(RTC) block rather than distinguished fast and slow, so
       rtc_iram_seg, rtc_data_seg and rtc_slow_seg all name the same memory.

   SRAM regions:
      segment/section    start    end      size
      -------------------------------------------------------
      sram_low_seg       4FF00000 4FF2ABD0 02ABD0 (175056 bytes)
         .iram0.*
         .dram0.data
         .dram0.bss
         .stack
         .heap0
      dram_app1_seg      4FF2ABD0 4FF38BD0 00E000 ( 57344 bytes)
         .heap1
      dram_stack_seg     4FF38BD0 4FF3CFC0 0043F0 ( 17392 bytes)
         <reserved, see below>
      dram_rom0_seg      4FF3CFC0 4FF40000 003040 ( 12352 bytes)
         <ROM CPU1 stack, .bss and .data>
      sram_high_seg      4FF40000 4FF80000 040000 (262144 bytes)
         .heap4

   On dram_app1_seg: this is the second stage bootloader's own footprint, its
   iram_loader_seg (0x9000) plus its dram_seg (0x5000).  Both are dead the moment
   the bootloader hands control over, so the app takes the whole 0xE000 as heap.
   The start of that range is what SRAM_LOW_END below has to agree with; see the
   ASSERT in ltbootloader/ld/esp32p4/bootloader.ld.

   On dram_stack_seg: the 0x2000 stack overflow margin the bootloader leaves
   below the ROM stack, a 0x3F0 gap, and the 0x2000 ROM stack itself.  The stack
   grows down from SOC_ROM_STACK_START (0x4FF3CFC0) for SOC_ROM_STACK_SIZE.  LT
   runs on that ROM stack from call_start_cpu0 all the way until the scheduler
   starts - Esp32_LTChipStart.c never switches stacks - so it is live for the
   whole of startup, including the LTCoreBSP_Initialize call that registers the
   heap regions.  It is dead and reclaimable after that, but LT has no way for a
   BSP to contribute a region late, so the segment is declared here and left
   unused rather than handed to a heap that would be corrupted by a startup stack
   spill.  See the note in lt/include/lt/core/bsp/LTCoreBSP.h.

   On sram_high_seg: the L2 cache is carved out of the top of the 0x80000 that
   runs from SRAM_HIGH_START to SOC_DRAM_HIGH, so how much of that is memory
   depends on how large the cache is.  IDF takes the size from a Kconfig option
   that has no definition anywhere in v5.4, and nothing in IDF ever calls
   Cache_Set_L2_Cache_Mode(), so the cache is whatever size the ROM left it -
   which cannot be discovered from a register on this part.  The number below is
   therefore the worst case rather than the actual one: rom/cache.h caps the
   cache at MAX_L2_CACHE_SIZE (256KB) and its cache_size_t offers only
   CACHE_SIZE_128K and CACHE_SIZE_256K, so 256KB of the 512KB is the most the
   cache can ever take and the 256KB below it is always real memory.  A smaller
   cache only leaves memory above sram_high_seg unused; it can never overlap.
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

MEMORY
{
  /* All these values assume the flash cache is on, and have the blocks this uses
  subtracted from the length of the various regions.  Unlike the other esp32
  parts there is no separate 'data access port' view: both buses see L2MEM at the
  same addresses. */

  /* The low run of L2MEM, below the ROM's reserved area.  Executable as well as
  writable - .iram0.* and .dram0.* both land here. */
  sram_low_seg  (RWX) : org = 0x4FF00000, len = 0x2ABD0

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
  dram_stack_seg (RW) : org = 0x4FF38BD0, len = 0x043F0
  dram_rom0_seg  (RW) : org = 0x4FF3CFC0, len = 0x03040

  /* The high run of L2MEM, above the ROM's reserved area and below the L2 cache.
  Data only - nothing is executed from here. */
  sram_high_seg  (RW) : org = 0x4FF40000, len = 0x40000

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

  /* Sanity-check the low L2MEM segment sizes.  The low run plus the three
     segments the ROM reserves above it must reach SRAM_HIGH_START, which is
     0x40000 above SOC_DRAM_LOW. */
  ASSERT(0x40000 == (LENGTH(sram_low_seg)   +
                     LENGTH(dram_app1_seg)  +
                     LENGTH(dram_stack_seg) +
                     LENGTH(dram_rom0_seg)),
         "low L2MEM segments don't reach the start of sram_high_seg")

  /* sram_high_seg must stop at or below SOC_DRAM_HIGH less the largest L2 cache
     the hardware can be configured for; see the note in the header comment. */
  ASSERT(ORIGIN(sram_high_seg) + LENGTH(sram_high_seg) <= 0x4FFC0000 - 0x40000,
         "sram_high_seg runs into the memory the L2 cache can occupy")

  ASSERT(ORIGIN(sram_low_seg) + LENGTH(sram_low_seg) == SRAM_LOW_END,
         "sram_low_seg does not end where the second stage bootloader begins - see the matching ASSERT in ltbootloader/ld/esp32p4/bootloader.ld")

  ASSERT(ORIGIN(dram_app1_seg) == SRAM_LOW_END,
         "dram_app1_seg does not start at the second stage bootloader's iram_loader_seg")

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 */
