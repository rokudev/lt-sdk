/*
 * ESP32-S3 Linker Script Memory Layout
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

   Two things make this a different shape from the esp32 script beside it rather
   than a port of it.

   First, on the esp32s3 IRAM and DRAM are the *same bytes* seen on two buses.
   SRAM1 answers to 0x3FC88000 on the data bus and to 0x40378000 on the
   instruction bus, an offset of 0x6F0000, and a byte spent on .iram0.text is a
   byte no longer available to .data.  sections.ld handles that with a
   .dram0.dummy NOLOAD section at the head of dram_app0_seg that grows to match
   the IRAM actually used.  The esp32 needs no such trick: its IRAM at
   0x40080000 and its DRAM at 0x3FFAE000 are physically distinct.

   Second, the esp32 map is full of holes because ROM data islands and the
   libbtdm_app.a carve-outs sit in the middle of DRAM, which is why it needs nine
   segments to describe 328KB.  Neither applies here.  The esp32s3 ROM keeps its
   static data in one run at the top of SRAM1, and the esp32s3 BLE controller
   allocates from the heap instead of claiming a fixed window, so the same job
   takes five segments.

   SRAM regions:
      segment/section    start    end      size
      -------------------------------------------------------
      dram_app0_seg      3FC88000 3FCD9700 051700 (333568 bytes)
         .dram0.dummy
         .dram0.data
         .dram0.bss
         .heap0
      dram_app1_seg      3FCD9700 3FCE7700 00E000 ( 57344 bytes)
         .heap1
      dram_stack_seg     3FCE7700 3FCEB710 004010 ( 16400 bytes)
         <reserved, see below>
      dram_cpu1_stack_seg3FCEB710 3FCED710 002000 (  8192 bytes)
         <reserved, see below>
      dram_rom0_seg      3FCED710 3FCF0000 0028F0 ( 10480 bytes)
         <ROM .bss and .data, never reclaimable>
      -- SRAM2, a separate block --
      dram_app3_seg      3FCF0000 3FCF8000 008000 ( 32768 bytes)
         .heap3
      <DATA CACHE>       3FCF8000 3FD00000 008000 ( 32768 bytes)

   On dram_app1_seg: this is the second stage bootloader's own footprint, being
   its iram_loader_seg viewed as data (0x3FCD9700..0x3FCE3700, the alias of
   0x403C9700..0x403D3700) plus its dram_seg (0x3FCE3700..0x3FCE7700).  Both are
   dead the moment the bootloader hands control over, so the app takes the whole
   0xE000 as heap.  The end of that range is what SRAM_IRAM_END below has to
   agree with; see the ASSERT in ltbootloader/ld/esp32s3/bootloader.ld.

   On dram_stack_seg: this is the 0x2000 stack overflow margin the bootloader
   leaves below the ROM stacks, plus the 0x2000 ROM PRO CPU stack itself.  LT
   runs on that ROM stack from call_start_cpu0 all the way until the scheduler
   starts - Esp32_LTChipStart.c never switches stacks - so it is live for the
   whole of startup, including the LTCoreBSP_Initialize call that registers the
   heap regions.  It is dead and reclaimable after that, but LT has no way for a
   BSP to contribute a region late, so the segment is declared here and left
   unused rather than handed to a heap that would be corrupted by a startup
   stack spill.  See the note in lt/include/lt/core/bsp/LTCoreBSP.h.

   On dram_cpu1_stack_seg: the ROM APP CPU stack, deliberately not reclaimed.
   Bringing up the second CPU later needs it, and it is only worth 8KB.

   On dram_app3_seg: SRAM2 is DRAM only - it has no instruction bus alias - and
   the data cache is carved out of the top of it.  The 0x8000 length here is only
   correct for a 32KB data cache, which Esp32_PSRAM.c pins explicitly at startup
   precisely so this number can be trusted.  A 64KB data cache would consume all
   of SRAM2 and this segment would have to go.
*/

/* CPU instruction prefetch padding size for flash mmap scenario */
_esp_flash_mmap_prefetch_pad_size = 32;
/* CPU instruction prefetch padding size for memory protection scenario */
_esp_memprot_prefetch_pad_size = 0;
/* Memory alignment size for PMS */
_esp_memprot_align_size = 0;

/* The offset between the instruction and data views of SRAM1.  Used by
   sections.ld to size .dram0.dummy. */
iram_dram_offset  = 0x6F0000;

/* End of the SRAM1 window available to the app, expressed on the instruction
   bus.  This is where the second stage bootloader's iram_loader_seg begins, so
   it must match the ASSERT in ltbootloader/ld/esp32s3/bootloader.ld.  Note that
   it is 0x3000 below the vendor's 0x403CC700: LT merges the vendor's
   iram_loader_seg and iram_seg into one segment, so the bootloader starts lower
   even though it occupies the same window overall. */
SRAM_IRAM_END     = 0x403C9700;

MEMORY
{
  /* All these values assume the flash cache is on, and have the blocks this uses
  subtracted from the length of the various regions.  The 'data access port'
  dram/drom regions map to the same iram/irom regions but are connected to the
  data port of the CPU and eg allow bytewise access. */

  /* IRAM for PRO cpu.  This is the instruction bus alias of dram_app0_seg, less
  the 32KB instruction cache at the bottom of SRAM0 - hence 0x40378000 rather
  than 0x40370000.  Same bytes as dram_app0_seg; see the header comment. */
  iram0_0_seg (RX) : org = 0x40378000, len = 0x51700

  /* Even though the segment name is iram, it is actually mapped to flash */
  iram0_2_seg (RX) : org = 0x42000020, len = 0x800000-0x20
  /*
    (0x20 offset above is a convenience for the app binary image generation.
    Flash cache has 64KB pages. The .bin file which is flashed to the chip
    has a 0x18 byte file header, and each segment has a 0x08 byte segment
    header. Setting this offset makes it simple to meet the flash cache MMU's
    constraint that (paddr % 64KB == vaddr % 64KB).)
  */

  dram_app0_seg       (RW) : org = 0x3FC88000, len = 0x51700
  dram_app1_seg       (RW) : org = 0x3FCD9700, len = 0x0E000
  dram_stack_seg      (RW) : org = 0x3FCE7700, len = 0x04010
  dram_cpu1_stack_seg (RW) : org = 0x3FCEB710, len = 0x02000
  dram_rom0_seg       (RW) : org = 0x3FCED710, len = 0x028F0
  dram_app3_seg       (RW) : org = 0x3FCF0000, len = 0x08000

  /* Flash mapped constant data.  Note that on the esp32s3 this window is shared
  with PSRAM - the MMU decides per 64KB page which device a page targets - which
  is why there is no extern_ram_seg here the way there is on the esp32.  PSRAM is
  mapped at runtime above whatever drom actually uses; see Esp32_PSRAM.c.

  It also shares MMU entries with iram0_2_seg above: one 512 entry table serves
  both buses and the entry is picked from the low bits of the virtual address, so
  drom starting here would index the same entries as irom starting at 0x42000020.
  sections.ld skips the overlap with a NOLOAD gap; the origin below is the start of
  that gap, not the start of the rodata. */
  drom0_0_seg (R) : org = 0x3C000020, len = 0x2000000-0x20
  /* (See iram0_2_seg for meaning of 0x20 offset in the above.) */

  /* RTC fast memory (executable). Persists over deep sleep. */
  rtc_iram_seg(RWX) : org = 0x600FE000, len = 0x2000
  /* RTC fast memory (same block as above), viewed from data bus.  Unlike the
  esp32, the esp32s3 sees RTC fast memory at the same address on both buses. */
  rtc_data_seg(RW) : org = 0x600FE000, len = 0x2000 - 0
  /* RTC slow memory (data accessible). Persists over deep sleep.

     Start of RTC slow memory is reserved for ULP co-processor code + data, if enabled.
  */
  rtc_slow_seg(RW) : org = 0x50000000 + 0,
                                     len = 0x2000 - 0
}

_static_data_end = _bss_end;
_data_seg_org = ORIGIN(rtc_data_seg);

/* The lines below define location alias for .rtc.data section based on Kconfig option.
   When the option is not defined then use slow memory segment
   else the data will be placed in fast memory segment */
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

  /* Sanity-check the SRAM1 segment sizes.  SRAM1 runs 0x3FC88000..0x3FCF0000 on
     the data bus, so the four segments that describe it must sum to 0x68000.
     dram_app3_seg is deliberately not in this sum - it lives in SRAM2. */
  ASSERT(0x68000 == (LENGTH(dram_app0_seg)       +
                     LENGTH(dram_app1_seg)       +
                     LENGTH(dram_stack_seg)      +
                     LENGTH(dram_cpu1_stack_seg) +
                     LENGTH(dram_rom0_seg)),
         "SRAM1 segments don't sum to 416KB")

  /* iram0_0_seg and dram_app0_seg are two views of one block of memory, so a
     disagreement between them would silently hand the same bytes out twice. */
  ASSERT(ORIGIN(iram0_0_seg) == ORIGIN(dram_app0_seg) + iram_dram_offset,
         "iram0_0_seg is not the instruction bus alias of dram_app0_seg")
  ASSERT(LENGTH(iram0_0_seg) == LENGTH(dram_app0_seg),
         "iram0_0_seg and dram_app0_seg describe different amounts of memory")
  ASSERT(ORIGIN(iram0_0_seg) + LENGTH(iram0_0_seg) == SRAM_IRAM_END,
         "iram0_0_seg does not end where the second stage bootloader begins - see the matching ASSERT in ltbootloader/ld/esp32s3/bootloader.ld")

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Jul-26   claudius    created
 *  03-Aug-26   claudius    rodata segment now starts with .flash_rodata_dummy
 */
