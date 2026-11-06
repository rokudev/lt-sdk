/*
 * ESP32-C3 Linker Script Memory Layout
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

   The shape is the esp32s3's, not the esp32's: IRAM and DRAM here are the *same
   bytes* seen on two buses.  SRAM1 answers to 0x3FC80000 on the data bus and to
   0x40380000 on the instruction bus, an offset of 0x700000, and a byte spent on
   .iram0.text is a byte no longer available to .data.  sections.ld handles that
   with a .dram0.dummy NOLOAD section at the head of dram_app0_seg that grows to
   match the IRAM actually used.

   Where this part differs from the esp32s3:

     - one core, so there is no ROM APP CPU stack and no dram_cpu1_stack_seg;
     - no SRAM2, so there is no dram_app3_seg and no separate data cache carve
       out.  SRAM0 is 16KB and is *entirely* the instruction cache, which is why
       the app's IRAM starts at 0x40380000 rather than at SOC_IRAM_LOW;
     - one 16KB ICache serves both buses, fixed in hardware, so nothing has to be
       pinned at startup the way Esp32_PSRAM.c pins the esp32s3's data cache;
     - the flash cache MMU has 128 entries of 64KB, so each bus window is 8MB
       rather than the esp32s3's 32MB.  Esp32_Cache.c splits that one table
       between irom and drom at _rodata_reserved_start, so the window lengths
       below and kEsp32_RegisterMMU_* in include/esp32c3/Esp32_Registers.h must
       describe the same thing;
     - there is no RTC fast memory at all - RTC slow at 0x50000000 is the only
       RTC block - so rtc_iram_seg and rtc_data_seg collapse into rtc_slow_seg.

   SRAM regions:
      segment/section    start    end      size
      -------------------------------------------------------
      dram_app0_seg      3FC80000 3FCCC710 04C710 (313104 bytes)
         .dram0.dummy
         .dram0.data
         .dram0.bss
         .heap0
      dram_app1_seg      3FCCC710 3FCDA710 00E000 ( 57344 bytes)
         .heap1
      dram_stack_seg     3FCDA710 3FCDE710 004000 ( 16384 bytes)
         <reserved, see below>
      dram_rom0_seg      3FCDE710 3FCE0000 0018F0 (  6384 bytes)
         <ROM .bss and .data, never reclaimable>

   On dram_app1_seg: this is the second stage bootloader's own footprint, being
   its iram_loader_seg viewed as data (0x3FCCC710..0x3FCD5710, the alias of
   0x403CC710..0x403D5710) plus its dram_seg (0x3FCD5710..0x3FCDA710).  Both are
   dead the moment the bootloader hands control over, so the app takes the whole
   0xE000 as heap.  The start of that range is what SRAM_IRAM_END below has to
   agree with; see the ASSERT in ltbootloader/ld/esp32c3/bootloader.ld.

   On dram_stack_seg: this is the 0x2000 stack overflow margin the bootloader
   leaves below the ROM stack, plus the 0x2000 ROM stack itself (SOC_ROM_STACK_START
   0x3FCDE710, SOC_ROM_STACK_SIZE 0x2000).  LT runs on that ROM stack from
   call_start_cpu0 all the way until the scheduler starts - Esp32_LTChipStart.c
   never switches stacks - so it is live for the whole of startup, including the
   LTCoreBSP_Initialize call that registers the heap regions.  It is dead and
   reclaimable after that, but LT has no way for a BSP to contribute a region
   late, so the segment is declared here and left unused rather than handed to a
   heap that would be corrupted by a startup stack spill.  See the note in
   lt/include/lt/core/bsp/LTCoreBSP.h.
*/

/* CPU instruction prefetch padding size for flash mmap scenario */
_esp_flash_mmap_prefetch_pad_size = 16;
/* CPU instruction prefetch padding size for memory protection scenario */
_esp_memprot_prefetch_pad_size = 0;
/* Memory alignment size for PMS */
_esp_memprot_align_size = 0;

/* The offset between the instruction and data views of SRAM1.  Used by
   sections.ld to size .dram0.dummy. */
iram_dram_offset  = 0x700000;

/* End of the SRAM1 window available to the app, expressed on the instruction
   bus.  This is where the second stage bootloader's iram_loader_seg begins, so
   it must match the ASSERT in ltbootloader/ld/esp32c3/bootloader.ld.  Note that
   it is 0x2000 below the vendor's 0x403CE710: LT merges the vendor's
   iram_loader_seg and iram_seg into one segment, so the bootloader starts lower
   even though it occupies the same window overall. */
SRAM_IRAM_END     = 0x403CC710;

MEMORY
{
  /* All these values assume the flash cache is on, and have the blocks this uses
  subtracted from the length of the various regions.  The 'data access port'
  dram/drom regions map to the same iram/irom regions but are connected to the
  data port of the CPU and eg allow bytewise access. */

  /* IRAM for the CPU.  This is the instruction bus alias of dram_app0_seg, less
  the 16KB instruction cache that is all of SRAM0 - hence 0x40380000 rather than
  SOC_IRAM_LOW's 0x4037C000.  Same bytes as dram_app0_seg; see the header
  comment. */
  iram0_0_seg (RX) : org = 0x40380000, len = 0x4C710

  /* Even though the segment name is iram, it is actually mapped to flash */
  iram0_2_seg (RX) : org = 0x42000020, len = 0x800000-0x20
  /*
    (0x20 offset above is a convenience for the app binary image generation.
    Flash cache has 64KB pages. The .bin file which is flashed to the chip
    has a 0x18 byte file header, and each segment has a 0x08 byte segment
    header. Setting this offset makes it simple to meet the flash cache MMU's
    constraint that (paddr % 64KB == vaddr % 64KB).)
  */

  dram_app0_seg  (RW) : org = 0x3FC80000, len = 0x4C710
  dram_app1_seg  (RW) : org = 0x3FCCC710, len = 0x0E000
  dram_stack_seg (RW) : org = 0x3FCDA710, len = 0x04000
  dram_rom0_seg  (RW) : org = 0x3FCDE710, len = 0x018F0

  /* Flash mapped constant data.  This window shares MMU entries with iram0_2_seg
  above: one 128 entry table serves both buses and the entry is picked from the
  low bits of the virtual address, so drom starting here would index the same
  entries as irom starting at 0x42000020.  sections.ld skips the overlap with a
  NOLOAD gap; the origin below is the start of that gap, not the start of the
  rodata. */
  drom0_0_seg (R) : org = 0x3C000020, len = 0x800000-0x20
  /* (See iram0_2_seg for meaning of 0x20 offset in the above.) */

  /* RTC slow memory (data accessible). Persists over deep sleep.  This part has
  no RTC fast memory, so this is the only RTC block - soc.h points SOC_RTC_IRAM_*,
  SOC_RTC_DRAM_* and SOC_RTC_DATA_* all at this same range. */
  rtc_slow_seg(RW) : org = 0x50000000, len = 0x2000
}

_static_data_end = _bss_end;
_data_seg_org = ORIGIN(rtc_slow_seg);

  /* rtc_data_location exists so sections.ld can stay the shape of the esp32s3's,
     where the alias selects between RTC fast and RTC slow.  Here there is only
     one RTC block to select. */
  REGION_ALIAS("rtc_data_location", rtc_slow_seg );
  REGION_ALIAS("default_code_seg", iram0_2_seg);
  REGION_ALIAS("default_rodata_seg", drom0_0_seg);

/**
 *  If rodata default segment is placed in `drom0_0_seg`, then flash's first rodata section must
 *  also be first in the segment.  That first section is .flash_rodata_dummy, the
 *  NOLOAD gap that keeps drom's MMU pages clear of irom's; the real rodata starts
 *  above it at _rodata_reserved_start.  See sections.ld.
 */
  ASSERT(_flash_rodata_dummy_start == ORIGIN(default_rodata_seg),
         ".flash_rodata_dummy section must be placed at the beginning of the rodata segment.")

  /* Sanity-check the SRAM1 segment sizes.  SRAM1 runs 0x3FC80000..0x3FCE0000 on
     the data bus - SOC_DRAM_LOW to SOC_DRAM_HIGH - so the four segments that
     describe it must sum to 0x60000. */
  ASSERT(0x60000 == (LENGTH(dram_app0_seg)  +
                     LENGTH(dram_app1_seg)  +
                     LENGTH(dram_stack_seg) +
                     LENGTH(dram_rom0_seg)),
         "SRAM1 segments don't sum to 384KB")

  /* iram0_0_seg and dram_app0_seg are two views of one block of memory, so a
     disagreement between them would silently hand the same bytes out twice. */
  ASSERT(ORIGIN(iram0_0_seg) == ORIGIN(dram_app0_seg) + iram_dram_offset,
         "iram0_0_seg is not the instruction bus alias of dram_app0_seg")
  ASSERT(LENGTH(iram0_0_seg) == LENGTH(dram_app0_seg),
         "iram0_0_seg and dram_app0_seg describe different amounts of memory")
  ASSERT(ORIGIN(iram0_0_seg) + LENGTH(iram0_0_seg) == SRAM_IRAM_END,
         "iram0_0_seg does not end where the second stage bootloader begins - see the matching ASSERT in ltbootloader/ld/esp32c3/bootloader.ld")

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
