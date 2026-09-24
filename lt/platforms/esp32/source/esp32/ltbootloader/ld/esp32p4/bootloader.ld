/*
Linker file used to link the bootloader.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*/

/* Simplified memory map for the bootloader

   The main purpose is to make sure the bootloader can load into main memory
   without overwriting itself.

   ESP32-P4 ROM static data usage is as follows:
   - 0x4ff296b8 - 0x4ff3afc0: Shared buffers, used in UART/USB/SPI download mode only
   - 0x4ff3afc0 - 0x4ff3fba4: CPU1 stack, can be reclaimed as heap after RTOS startup
   - 0x4ff3fba4 - 0x4ff40000: ROM .bss and .data (not easily reclaimable)

   The 2nd stage bootloader can take space up to the end of the ROM shared
   buffers area.

   Unlike the esp32c3 and the Xtensa parts, instruction and data addresses here
   are the same: HP L2MEM is one window at 0x4ff00000, reached by both buses, so
   there is no iram_dram_offset to apply.
*/

/* 0x4ff3abd0 is the last usable address for 2nd stage bootloader stack overhead
   and dram_seg; iram_loader_seg is worked out from there, backwards.
*/
bootloader_usable_dram_end     = 0x4ff3abd0;
bootloader_stack_overhead      = 0x2000;  /* Safety margin between bootloader data section and startup stacks */
bootloader_dram_seg_len        = 0x5000;

/* Unlike the vendor bootloader we do not split the text between a loader
   segment and a run segment - everything lives in iram_loader_seg.  The length
   here is therefore the sum of the vendor's iram_loader_seg (0x7000) and
   iram_seg (0x2000), so the bootloader occupies exactly the same IRAM window
   the vendor bootloader does: [0x4ff2abd0, 0x4ff33bd0).
*/
bootloader_iram_loader_seg_len = 0x9000;

/* Start of the lower region is determined by region size and the end of the higher region */
bootloader_dram_seg_end          = bootloader_usable_dram_end - bootloader_stack_overhead;
bootloader_dram_seg_start        = bootloader_dram_seg_end - bootloader_dram_seg_len;
bootloader_iram_loader_seg_start = bootloader_dram_seg_start - bootloader_iram_loader_seg_len;

MEMORY
{
  iram_loader_seg (RWX) :           org = bootloader_iram_loader_seg_start, len = bootloader_iram_loader_seg_len
  dram_seg (RW) :                   org = bootloader_dram_seg_start, len = bootloader_dram_seg_len
}

/* The app may use RAM for static allocations up to the start of iram_loader_seg.
 * If you have changed something above and this assert fails:
 * 1. Check what the new value of bootloader_iram_loader_seg_start is.
 * 2. Update the value in this assert.
 * 3. Update SRAM_LOW_END in mastering/ld/esp32p4/memory.ld to the same value.
 *
 * The vendor bootloader asserts 0x4ff2cbd0 here - that is its iram_loader_seg
 * start, above its separate iram_seg.  Merging the two moves the bottom of the
 * bootloader's window down by that segment's 0x2000.
 */
ASSERT(bootloader_iram_loader_seg_start == 0x4FF2ABD0, "bootloader_iram_loader_seg_start inconsistent with SRAM_LOW_END");

/*  Default entry point:  */
ENTRY(call_start_cpu0);


SECTIONS
{
  /* Shared RAM */
  .dram0.bss (NOLOAD) :
  {
    . = ALIGN (8);
    _dram_start = ABSOLUTE(.);
    _bss_start = ABSOLUTE(.);
    *(.dynsbss)
    *(.sbss)
    *(.sbss.*)
    *(.gnu.linkonce.sb.*)
    *(.scommon)
    *(.sbss2)
    *(.sbss2.*)
    *(.gnu.linkonce.sb2.*)
    *(.dynbss)
    *(.bss)
    *(.bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN (8);
    _bss_end = ABSOLUTE(.);
  } >dram_seg

  .dram0.data :
  {
    _data_start = ABSOLUTE(.);
    *(.data)
    *(.data.*)
    *(.gnu.linkonce.d.*)
    *(.data1)
    *(.sdata)
    *(.sdata.*)
    *(.gnu.linkonce.s.*)
    *(.gnu.linkonce.s2.*)
    *(.jcr)
    _data_end = ABSOLUTE(.);
  } >dram_seg

  .dram0.rodata :
  {
    _rodata_start = ABSOLUTE(.);
    *(.rodata)
    *(.rodata.*)
    *(.gnu.linkonce.r.*)
    *(.rodata1)
    /* .srodata is the small-data read only section the RISC-V ABI addresses
       through gp; it has no Xtensa counterpart and must be placed. */
    *(.sdata2 .sdata2.* .srodata .srodata.*)
    __XT_EXCEPTION_TABLE_ = ABSOLUTE(.);
    *(.xt_except_table)
    *(.gcc_except_table)
    *(.gnu.linkonce.e.*)
    *(.gnu.version_r)
    *(.eh_frame)
    . = (. + 3) & ~ 3;
    /*  C++ constructor and destructor tables, properly ordered:  */
    __init_array_start = ABSOLUTE(.);
    KEEP (*crtbegin.*(.ctors))
    KEEP (*(EXCLUDE_FILE (*crtend.*) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    __init_array_end = ABSOLUTE(.);
    KEEP (*crtbegin.*(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.*) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    /*  C++ exception handlers table:  */
    __XT_EXCEPTION_DESCS_ = ABSOLUTE(.);
    *(.xt_except_desc)
    *(.gnu.linkonce.h.*)
    __XT_EXCEPTION_DESCS_END__ = ABSOLUTE(.);
    *(.xt_except_desc_end)
    *(.dynamic)
    *(.gnu.version_d)
    _rodata_end = ABSOLUTE(.);
	/* Literals are also RO data. */
    _lit4_start = ABSOLUTE(.);
    *(*.lit4)
    *(.lit4.*)
    *(.gnu.linkonce.lit4.*)
    _lit4_end = ABSOLUTE(.);
    . = ALIGN(4);
    _dram_end = ABSOLUTE(.);
  } >dram_seg

  .iram.text :
  {
    _stext = .;
    _loader_text_start = ABSOLUTE(.);
    *(.entry.text)
    *(.init.literal)
    *(.init)
    *(.literal .text .literal.* .text.* .stub .gnu.warning .gnu.linkonce.literal.* .gnu.linkonce.t.*.literal .gnu.linkonce.t.*)
    *(.iram .iram.*)
    *(.iram1 .iram1.*)
    *(.fini.literal)
    *(.fini)
    *(.gnu.version)

    /** CPU will try to prefetch up to 16 bytes of
      * of instructions. This means that any configuration (e.g. MMU, PMS) must allow
      * safe access to up to 16 bytes after the last real instruction, add
      * dummy bytes to ensure this
      */
    . += 16;
    _loader_text_end = ABSOLUTE(.);
    _etext = .;
  } > iram_loader_seg

}
