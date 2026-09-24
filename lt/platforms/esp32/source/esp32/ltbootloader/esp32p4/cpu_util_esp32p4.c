/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
//
// esp_cpu_configure_region_protection() for the esp32p4, in the shape of
// cpu_util_esp32c3.c beside it: PMP TOR entries walking the address map bottom
// to top.  bootloader_mem.c is the only caller.
//
// This is not IDF's esp32p4 cpu_region_protect.c.  That file blocks the invalid
// ranges with the PMA unit and keeps the PMP for the valid ones, which needs the
// PMA_* CSR macro family; riscv/csr.h in this tree carries PMP macros only.  It
// also calls esp_cpu_dbgr_is_attached() and CACHE_LL_L2MEM_NON_CACHE_ADDR(),
// neither of which exists here.  A PMP-only map is what the esp32c3 already runs
// and is sufficient - the PMA would only add cacheability attributes, which the
// ROM has already set by the time this runs.
//
#include <stdint.h>
#include "soc/cpu.h"
#include "soc/soc.h"

void esp_cpu_configure_region_protection(void)
{
    /* Notes on implementation:
     *
     * 1) This part does support overlapping PMP regions, resolved by static
     * priority, but nothing below overlaps - the entries are a strict bottom to
     * top walk, so the simpler TOR-only form the esp32c3 uses carries over.
     *
     * 2) There are not enough entries to describe every region exactly.  Where
     * a gap has to be merged with a neighbour it is merged into the more
     * restrictive one, so that executing an unmapped address still faults.
     *
     * 3) All 16 entries are spent, so unlike the esp32c3 there is none left for
     * the top four bytes of the address space: a TOR top of UINT32_MAX encodes
     * as 0xFFFFFFFC, and closing the remainder would need the NA4 entry the
     * esp32c3 spends its sixteenth on.
     */
    const unsigned NONE = PMP_L | PMP_TOR;
    const unsigned RW      = PMP_L | PMP_TOR | PMP_R | PMP_W;
    const unsigned RX      = PMP_L | PMP_TOR | PMP_R | PMP_X;
    const unsigned RWX     = PMP_L | PMP_TOR | PMP_R | PMP_W | PMP_X;

    // 1. Gap at bottom of address space
    PMP_ENTRY_SET(0, SOC_CPU_SUBSYSTEM_LOW, NONE);

    // 2. CPU subsystem - holds the CLIC and the debug module
    PMP_ENTRY_SET(1, SOC_CPU_SUBSYSTEM_HIGH, RW);
    _Static_assert(SOC_CPU_SUBSYSTEM_LOW < SOC_CPU_SUBSYSTEM_HIGH, "Invalid CPU subsystem region");

    // 3. Gap between CPU subsystem & HP TCM
    // 4. HP TCM
    // (Note: to save PMP entries these two are merged into one data-only region;
    //  nothing in this port executes from TCM)
    PMP_ENTRY_SET(2, SOC_TCM_HIGH, RW);
    _Static_assert(SOC_CPU_SUBSYSTEM_HIGH < SOC_TCM_LOW, "Invalid PMP entry order");
    _Static_assert(SOC_TCM_LOW < SOC_TCM_HIGH, "Invalid TCM region");

    // 5. Gap between HP TCM & CPU peripherals
    PMP_ENTRY_SET(3, CPU_PERIPH_LOW, NONE);
    _Static_assert(SOC_TCM_HIGH < CPU_PERIPH_LOW, "Invalid PMP entry order");

    // 6. CPU peripherals - the cache controller lives here
    PMP_ENTRY_SET(4, CPU_PERIPH_HIGH, RW);
    _Static_assert(CPU_PERIPH_LOW < CPU_PERIPH_HIGH, "Invalid CPU peripheral region");

    // 7. Gap between CPU peripherals & flash cache
    PMP_ENTRY_SET(5, SOC_IROM_LOW, NONE);
    _Static_assert(CPU_PERIPH_HIGH < SOC_IROM_LOW, "Invalid PMP entry order");

    // 8. Flash cache.  IROM and DROM are one window on this part, so unlike the
    // esp32c3 there is no separate read-only DROM entry
    PMP_ENTRY_SET(6, SOC_IROM_HIGH, RX);
    _Static_assert(SOC_IROM_LOW < SOC_IROM_HIGH, "Invalid flash cache region");
    _Static_assert(SOC_DROM_LOW == SOC_IROM_LOW, "Unexpected split I/D flash cache");
    _Static_assert(SOC_DROM_HIGH == SOC_IROM_HIGH, "Unexpected split I/D flash cache");

    // 9. Gap between flash cache & external RAM
    PMP_ENTRY_SET(7, SOC_EXTRAM_LOW, NONE);
    _Static_assert(SOC_IROM_HIGH < SOC_EXTRAM_LOW, "Invalid PMP entry order");

    // 10. External RAM.  This variant does not bring the PSRAM up, but the
    // window is described anyway so that adding it later needs no fuse-locked
    // entry changed
    PMP_ENTRY_SET(8, SOC_EXTRAM_HIGH, RWX);
    _Static_assert(SOC_EXTRAM_LOW < SOC_EXTRAM_HIGH, "Invalid external RAM region");

    // 11. Gap between external RAM & mask ROM
    PMP_ENTRY_SET(9, SOC_IROM_MASK_LOW, NONE);
    _Static_assert(SOC_EXTRAM_HIGH < SOC_IROM_MASK_LOW, "Invalid PMP entry order");

    // 12. Mask ROM - every ROM entry this bootloader calls is in here
    PMP_ENTRY_SET(10, SOC_IROM_MASK_HIGH, RX);
    _Static_assert(SOC_IROM_MASK_LOW < SOC_IROM_MASK_HIGH, "Invalid mask ROM region");

    // 13. Gap between mask ROM & L2MEM
    PMP_ENTRY_SET(11, SOC_IRAM_LOW, NONE);
    _Static_assert(SOC_IROM_MASK_HIGH < SOC_IRAM_LOW, "Invalid PMP entry order");

    // 14. HP L2MEM.  IRAM and DRAM are the same window here, with no dual-bus
    // alias, so one entry covers both
    PMP_ENTRY_SET(12, SOC_IRAM_HIGH, RWX);
    _Static_assert(SOC_IRAM_LOW < SOC_IRAM_HIGH, "Invalid L2MEM region");
    _Static_assert(SOC_DRAM_LOW == SOC_IRAM_LOW, "Unexpected split I/D RAM");
    _Static_assert(SOC_DRAM_HIGH == SOC_IRAM_HIGH, "Unexpected split I/D RAM");

    // 15. Gap between L2MEM & HP peripherals
    PMP_ENTRY_SET(13, SOC_PERIPHERAL_LOW, NONE);
    _Static_assert(SOC_IRAM_HIGH < SOC_PERIPHERAL_LOW, "Invalid PMP entry order");

    // 16. HP peripherals, LP ROM, LP RAM and LP peripherals.  These four are
    // contiguous but for the gap above LP ROM, and are merged into one data-only
    // region to leave an entry for closing the address space
    PMP_ENTRY_SET(14, SOC_LP_PERIPH_HIGH, RW);
    _Static_assert(SOC_PERIPHERAL_LOW < SOC_PERIPHERAL_HIGH, "Invalid peripheral region");
    _Static_assert(SOC_PERIPHERAL_HIGH == SOC_LP_ROM_LOW, "Unexpected gap above HP peripherals");
    _Static_assert(SOC_LP_ROM_HIGH <= SOC_LP_RAM_LOW, "Invalid PMP entry order");
    _Static_assert(SOC_LP_RAM_HIGH == SOC_LP_PERIPH_LOW, "Unexpected gap above LP RAM");
    _Static_assert(SOC_LP_PERIPH_LOW < SOC_LP_PERIPH_HIGH, "Invalid LP peripheral region");

    // 17. End of address space
    PMP_ENTRY_SET(15, UINT32_MAX, NONE);
}
