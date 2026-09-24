/*
 * ESP32-P4 Linker Script Output Sections
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 */

/* This file places compiler output into the memory blocks that memory.ld
   describes.  It follows the esp32c3 script beside it - same core family, same
   toolchain, same startup path - and differs from it in two ways.

   No .dram0.dummy.  The esp32c3 and the Xtensa parts see one block of SRAM on
   two buses at two addresses, so IRAM consumed there has to be skipped again
   before any data section is placed.  Here the instruction and data views are
   the same addresses, so .iram0.* and .dram0.* simply follow one another in
   sram_low_seg and the location counter does the accounting by itself.  For the
   same reason there is no _diram_i_start.

   A third heap.  The ROM's reserved area splits L2MEM in two, and the run above
   it is real memory the app can use, so sram_high_seg carries a heap of its own.

   What carries over from the esp32c3 unchanged, and why:

   No vector table.  The Xtensa parts reserve the head of IRAM for a table of
   fixed-offset vectors and hand LTK the base address.  Here the core has a
   single trap entry in mtvec, LTKInitialize() writes it itself
   (LTKArchRISC_V.c), and the BSP decodes mcause - so there is no
   .iram0.vectors, no _vector_table, and nothing at a fixed offset.

   Constructors arrive in .init_array, not .ctors.  The riscv32-esp-elf
   toolchain is configured --enable-initfini-array, so collecting .ctors the way
   the Xtensa scripts do would leave __init_array_start == __init_array_end, and
   every LT global constructor silently unrun with a clean build.

   Small data has to be placed.  The RISC-V ABI splits out .sdata/.srodata/.sbss
   for gp-relative addressing; these have no Xtensa counterpart and ld will not
   home them for you.  .srodata goes to SRAM beside .sdata rather than to flash
   rodata, since it is small-data by construction.  Note that __global_pointer$
   is deliberately *not* defined: ld only relaxes to gp-relative addressing when
   it is, and nothing in LT's startup path loads gp.  The second stage bootloader
   links the same way.

   The trap stack is real memory.  pStackTop in the BSP's
   LTCoreBSP_RISCV_SystemConfig is not a startup-only stack - the trap handler
   switches sp to it on every non-nested trap for the life of the system - so
   .stack below reserves it out of sram_low_seg.  Startup itself still runs on
   the ROM stack in dram_stack_seg; see the note in memory.ld.

   Archive placement rules name only archives LT actually links.  This variant
   sets LT_PLATFORM_HAS_WIRELESS_BLOBS := no, so the libnet80211/libpp/libphy/
   libbtdm_app rules the esp32s3 script carries would match nothing and are not
   reproduced.  The .iram1 catch-all stays: LT's own code uses it through
   ESP32_MEM_REGION(IRAM).
*/

/* Trap stack, see the header comment.  LTKInitialize() aligns the top down to
   16 bytes, so the section is aligned to match and nothing is lost. */
__SYSTEM_STACK_SIZE = 0x1000;

/*  Default entry point:  */
ENTRY(call_start_cpu0);

SECTIONS
{
  /* RTC data section holds RTC wake stub data/rodata, including from any source
     file named rtc_wake_stub*.c and the data marked with RTC_DATA_ATTR,
     RTC_RODATA_ATTR attributes.

     This part has a single unified LP RAM block rather than distinguished fast
     and slow, so the esp32s3's .rtc.text / .rtc.dummy / .rtc.force_fast trio has
     nowhere separate to go and is not reproduced.  Nothing in LT uses any of
     this today; the sections exist so that _rtc_bss_start.._rtc_bss_end have
     somewhere to come from, which Esp32_LTChipStart.c clears at boot.
  */
  .rtc.data :
  {
    _rtc_data_start = ABSOLUTE(.);
    *(.rtc.data .rtc.data.*)
    *(.rtc.rodata .rtc.rodata.*)

    *rtc_wake_stub*.*(.data .rodata .data.* .rodata.* .bss .bss.*)
    _rtc_data_end = ABSOLUTE(.);
  } > rtc_data_location

  /* RTC bss, from any source file named rtc_wake_stub*.c */
  .rtc.bss (NOLOAD) :
  {
    _rtc_bss_start = ABSOLUTE(.);
    *rtc_wake_stub*.*(.bss .bss.*)
    *rtc_wake_stub*.*(COMMON)

    *(.rtc.bss)

    _rtc_bss_end = ABSOLUTE(.);
  } > rtc_data_location

  /* This section holds data that should not be initialized at power up
     and will be retained during deep sleep.
     User data marked with RTC_NOINIT_ATTR will be placed
     into this section. See the file "esp_attr.h" for more information.
  */
  .rtc_noinit (NOLOAD):
  {
    . = ALIGN(4);
    _rtc_noinit_start = ABSOLUTE(.);
    *(.rtc_noinit .rtc_noinit.*)
    . = ALIGN(4) ;
    _rtc_noinit_end = ABSOLUTE(.);
  } > rtc_data_location

  /* Data marked with RTC_SLOW_ATTR.  The same block as everything above it on
     this part, but kept as its own section so the attribute still means
     something if a later part separates them again. */
  .rtc.force_slow :
  {
    . = ALIGN(4);
    _rtc_force_slow_start = ABSOLUTE(.);
    *(.rtc.force_slow .rtc.force_slow.*)
    . = ALIGN(4) ;
    _rtc_force_slow_end = ABSOLUTE(.);
  } > rtc_slow_seg

  ASSERT(((_rtc_force_slow_end - _rtc_data_start) <= LENGTH(rtc_slow_seg)),
          "RTC_SLOW segment data does not fit.")

  /* Code marked as running out of IRAM.  _iram_start is where the esp32s3 script
     has .iram0.vectors; there is no fixed-offset vector table here, so IRAM
     simply starts with the text. */
  .iram0.text :
  {
    _iram_start = ABSOLUTE(.);
    _iram_text_start = ABSOLUTE(.);

    /* LTK's trap path.  Unlike the esp32c3 this is pulled out of flash by object
       name rather than left where the compiler put it, for two reasons.  A trap
       taken while the flash driver has the cache suspended cannot fetch a
       handler that lives in flash.  And the vector table is aligned to 256,
       which is stricter than the 0x20 the flash text segment starts on.

       Both objects, not just the table: the table reaches _LTKFaultHandler in
       LTKArchRISC_V.o with a j, which is a JAL and so limited to +-1MB. */
    *LTKArchRISC_V_Vectors.o(.text .text.*)
    *LTKArchRISC_V.o(.text .text.*)

    /* ESP32_MEM_REGION(IRAM) resolves to .iram1.text; the catch-all also takes
       any .iram1 an Espressif ROM-facing source brings with it. */
    *(.iram1 .iram1.*)

  } > sram_low_seg

  /* Marks the end of IRAM code segment */
  .iram0.text_end (NOLOAD) :
  {
    . += _esp_memprot_prefetch_pad_size;
    . = ALIGN (4);
    _iram_text_end = ABSOLUTE(.);
  } > sram_low_seg

  .iram0.data :
  {
    . = ALIGN(4);
    _iram_data_start = ABSOLUTE(.);

    *(.iram.data .iram.data.*)

    _iram_data_end = ABSOLUTE(.);
  } > sram_low_seg

  .iram0.bss (NOLOAD) :
  {
    . = ALIGN(4);
    _iram_bss_start = ABSOLUTE(.);

    *(.iram.bss .iram.bss.*)

    _iram_bss_end = ABSOLUTE(.);
    . = ALIGN(4);
    _iram_end = ABSOLUTE(.);
   } > sram_low_seg

  /* Data follows straight on from IRAM in the same region.  On the esp32c3 and
     the Xtensa parts a .dram0.dummy section has to skip the IRAM already placed,
     because there it is the *same* memory at a different address; here it is the
     same memory at the same address and the location counter has already
     accounted for it. */
  .dram0.data :
  {
    _data_start = ABSOLUTE(.);
    *(.gnu.linkonce.d.*)
    *(.data1)

    /* RISC-V small data.  .srodata is read-only but belongs here rather than in
       flash rodata: it is small-data by construction, and the whole point of the
       section is that it sits with .sdata. */
    *(.sdata .sdata.*)
    *(.gnu.linkonce.s.*)
    *(.sdata2 .sdata2.*)
    *(.gnu.linkonce.s2.*)
    *(.srodata .srodata.*)

    *(.data .data.*)
    *(.dram1 .dram1.*)

    /* Buffers the app places by name with LT_SECTION().  These need explicit
       homes: as orphans ld appends them to the DROM run, which is read-only
       mapped flash, and one of an odd size there leaves the next section at an
       unaligned vaddr - which makes elf2image emit a padding segment of
       unaligned length that the bootloader rejects. */
    . = ALIGN(4);
    *(.tcm_data)
    . = ALIGN(4);
    *(.log_buf)
    . = ALIGN(4);
    *(.blechar_buf)
    . = ALIGN(4);

    _data_end = ABSOLUTE(.);
    . = ALIGN(4);
  } > sram_low_seg

  /*This section holds data that should not be initialized at power up.
    The section located in Internal SRAM memory region. The macro _NOINIT
    can be used as attribute to place data into this section.
    See the esp_attr.h file for more information.
  */
  .noinit (NOLOAD):
  {
    . = ALIGN(4);
    _noinit_start = ABSOLUTE(.);
    *(.noinit .noinit.*)
    . = ALIGN(4) ;
    _noinit_end = ABSOLUTE(.);
  } > sram_low_seg

  /* Shared RAM */
  .dram0.bss (NOLOAD) :
  {
    . = ALIGN (8);
    _bss_start = ABSOLUTE(.);

    *(.bss .bss.*)
    *(.dynbss .dynsbss .gnu.linkonce.b .gnu.linkonce.b.* .gnu.linkonce.sb .gnu.linkonce.sb.* .gnu.linkonce.sb2 .gnu.linkonce.sb2.* .sbss .sbss.* .sbss2 .sbss2.* .scommon .share.mem)
    *(COMMON)

    . = ALIGN (8);
    _bss_end = ABSOLUTE(.);
  } > sram_low_seg

  /* The trap stack LTK switches to on every non-nested trap; see the header
     comment.  Reserved before heap0 so it is never handed out as heap. */
  .stack (NOLOAD) : ALIGN(16)
  {
    _system_stack_base = ABSOLUTE(.);
    . += __SYSTEM_STACK_SIZE;
    _system_stack_top = ABSOLUTE(.);
  } > sram_low_seg

  /* Everything left over in sram_low_seg once the IRAM, the static data and the
     trap stack are accounted for.  This is the main heap. */
  .heap0 (NOLOAD) :
  {
     _heap0_start = ABSOLUTE(.);
     . = ORIGIN(sram_low_seg) + LENGTH(sram_low_seg);
     _heap0_end = ABSOLUTE(.);
  } > sram_low_seg

  /* The second stage bootloader's own footprint, dead once it hands over. */
  .heap1 (NOLOAD) :
  {
     _heap1_start = ABSOLUTE(.);
     . += LENGTH(dram_app1_seg);
     _heap1_end = ABSOLUTE(.);
  } > dram_app1_seg

  /* The run of L2MEM above the area the ROM reserves, below the L2 cache.  Data
     only - the image executes nothing from here, and memory.ld declares the
     region RW to say so. */
  .heap4 (NOLOAD) :
  {
     _heap4_start = ABSOLUTE(.);
     . += LENGTH(sram_high_seg);
     _heap4_end = ABSOLUTE(.);
  } > sram_high_seg

  /* Note the absence of .heap2 and .heap3.  heap2 is the esp32's hole between
     two ROM data islands and heap3 is the esp32s3's SRAM2; this part has
     neither, and the numbers are retired rather than reused, because
     LTMemoryRegion values are positional and are bound to names in
     LTDeviceConfig.json.  Esp32_LTCoreBSP.c registers heap0, heap1 and heap4. */

  /* Note also the absence of .ext_ram.bss and .ext_ram_noinit.  This variant
     does not bring the in-package PSRAM up, so there is nothing to place there
     and no heap region for it. */

  /* Flash rodata is laid out before flash text, and must stay that way.  Unlike
     every other part in this tree, irom and drom here are not merely two windows
     sharing an MMU table - they are the *same* window.  Both start at
     0x40000000, and the 1024 entry table is indexed by the virtual address
     alone.

     So there is nothing to separate, and nothing separates them: the other parts
     call Cache_Set_IDROM_MMU_Size() to tell the cache where the split falls, and
     there is no such call on this part because from the hardware's point of view
     there is no split.  memory.ld declares the one window accordingly.

     Rodata goes first because the application descriptor has to be at the head
     of the image.  Both the bootloader (LTBootDriver_CheckOTAPartition) and the
     elf2image --elf-sha256-offset argument in Esp32_MasterFirmwareImage.mk read
     it at a fixed offset past the image and segment headers, which only holds if
     the segment carrying it is the first one written.  esptool orders flash
     segments by virtual address, so the descriptor's segment must be the lowest.
     The other parts get this for free - their drom window is below their irom
     one - but here the order is ours to choose. */
  .flash.appdesc : ALIGN(0x10)
  {
    _rodata_reserved_start = ABSOLUTE(.);
    _rodata_start = ABSOLUTE(.);

    *(.rodata_desc .rodata_desc.*)               /* Should be the first.  App version info.        DO NOT PUT ANYTHING BEFORE IT! */
    *(.rodata_custom_desc .rodata_custom_desc.*) /* Should be the second. Custom app version info. DO NOT PUT ANYTHING BEFORE IT! */

    /* Create an empty gap within this section. Thanks to this, the end of this
     * section will match .flash.rodata's begin address. Thus, both sections
     * will be merged when creating the final bin image. */
    . = ALIGN(ALIGNOF(.flash.rodata));
  } >default_rodata_seg

  .flash.rodata : ALIGN(0x10)
  {
    _flash_rodata_start = ABSOLUTE(.);

    *(.rodata .rodata.*)
    *(.irom1.text) /* catch stray ICACHE_RODATA_ATTR */
    *(.gnu.linkonce.r.*)
    *(.rodata1)
    *(.gcc_except_table .gcc_except_table.*)
    *(.gnu.linkonce.e.*)
    *(.gnu.version_r)
    . = ALIGN(4);
    __eh_frame = ABSOLUTE(.);
    KEEP(*(.eh_frame))
    . = ALIGN(8);

    /*  C++ constructor and destructor tables.
        The riscv32-esp-elf toolchain emits .init_array, not .ctors - see the
        header comment.  The esp32s3 script excludes crtbegin.o and crtend.o
        here; there is no EXCLUDE_FILE below because ld will not accept one
        inside a SORT_BY_INIT_PRIORITY wildcard, and LT does not link the
        toolchain crt in the first place, so neither object is in the link. */
    __init_array_start = ABSOLUTE(.);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP (*(.init_array))
    __init_array_end = ABSOLUTE(.);

    __fini_array_start = ABSOLUTE(.);
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP (*(.fini_array))
    __fini_array_end = ABSOLUTE(.);

    *(.dynamic)
    *(.gnu.version_d)
    _rodata_end = ABSOLUTE(.);
    . = ALIGN(4);
    _thread_local_start = ABSOLUTE(.);
    *(.tdata)
    *(.tdata.*)
    *(.tbss)
    *(.tbss.*)
    _thread_local_end = ABSOLUTE(.);
    . = ALIGN(4);
  } >default_rodata_seg

  _flash_rodata_align = ALIGNOF(.flash.rodata);

  .flash.rodata_noload (NOLOAD) :
  {
    /* Marks the end of the rodata that actually occupies flash MMU pages. */
    _rodata_reserved_end = ABSOLUTE(.);
    . = ALIGN (4);
  } > default_rodata_seg

  /* The text follows the rodata with no gap, and the two become one flash
     segment in the image.  That is the right answer on this part rather than an
     oversight: the bootloader's unpack_load_app() classifies a segment by its
     load address, SOC_DROM_LOW == SOC_IROM_LOW here, so the one segment is taken
     as both the drom and the irom and Cache_FLASH_MMU_Set() is called twice with
     identical arguments.  One contiguous run, one set of MMU entries. */
  .flash.text :
  {
    _stext = .;
    _text_start = ABSOLUTE(.);

    *(.text .text.*)
    *(.stub .gnu.warning .gnu.linkonce.t.*)
    *(.irom0.text) /* catch stray ICACHE_RODATA_ATTR */
    *(.fini)
    *(.gnu.version)

    /** CPU will try to prefetch up to 16 bytes of
      * of instructions. This means that any configuration (e.g. MMU, PMS) must allow
      * safe access to up to 16 bytes after the last real instruction, add
      * dummy bytes to ensure this
      */
    . += _esp_flash_mmap_prefetch_pad_size;

    _text_end = ABSOLUTE(.);
    _etext = .;

    /* Similar to _iram_start, this symbol goes here so it is
       resolved by addr2line in preference to the first symbol in
       the flash.text segment.
    */
    _flash_cache_start = ABSOLUTE(0);
  } >default_code_seg
}

ASSERT(((_iram_end - ORIGIN(sram_low_seg)) <= LENGTH(sram_low_seg)),
          "IRAM0 segment data does not fit.")

/* sram_low_seg carries the IRAM, the static data, the trap stack and heap0, so
   this is the check that all four together still fit.  If it fires, either the
   image grew or the segment boundaries in memory.ld moved. */
ASSERT(((_heap0_end - ORIGIN(sram_low_seg)) <= LENGTH(sram_low_seg)),
          "LT heap0 segment does not fit.")

/* Static data and the trap stack must not run past the start of heap0.  The
   ASSERT above would catch that too, but only after the fact and with a less
   helpful message. */
ASSERT((_system_stack_top <= _heap0_start),
          "Static data and the trap stack overrun the start of LT heap0.")

ASSERT(((_heap1_end - ORIGIN(dram_app1_seg)) <= LENGTH(dram_app1_seg)),
          "LT heap1 segment does not fit.")

ASSERT(((_heap4_end - ORIGIN(sram_high_seg)) <= LENGTH(sram_high_seg)),
          "LT heap4 segment does not fit.")

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 */
