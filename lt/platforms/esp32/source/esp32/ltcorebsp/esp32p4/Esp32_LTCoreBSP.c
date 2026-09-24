/******************************************************************************
 * esp32p4/Esp32_LTCoreBSP.c                                       ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/bsp/LTCoreBSP.h>

#include "Esp32_Irq.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"
#include "Esp32_Console.h"
#include "Esp32_Interrupt.h"

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;

/*___________________
  static variables */

static LTAtomic                             s_LTCoreBSPInitialized = { 0 };

/*___________________
   Heap definition  */
/*
 * The heap region numbering is load bearing.  LTMemoryRegion is 1-based
 * positional, and names are bound to positions by /memory/regions in
 * LTDeviceConfig.json.  This part has three regions - the low run of L2MEM, the
 * second stage bootloader's own footprint reclaimed once it has handed over, and
 * the run of L2MEM above the area the ROM reserves for itself.  The in-package
 * PSRAM is not brought up by this variant and contributes no region.
 *
 * heap2 and heap3 are absent rather than renumbered: those are the esp32's ROM
 * data island hole and the esp32s3's SRAM2, and reusing the numbers would give
 * the same LTMemoryRegion value two meanings across variants.
 *
 * All three regions are fixed at link time by memory.ld; nothing here is
 * detected at runtime.
 */
extern int _heap0_start;
extern int _heap0_end;
extern int _heap1_start;
extern int _heap1_end;
extern int _heap4_start;
extern int _heap4_end;

/* heap0 (low L2MEM), heap1 (reclaimed bootloader SRAM), heap4 (high L2MEM) */
#define ESP32_NUM_HEAP_REGIONS  3

#define HEAP_REGION_SIZE(n) (u32)(((u8*)&_heap##n##_end) - ((u8*)&_heap##n##_start))

static LTCoreBSP_HeapRegion s_heapRegions[ESP32_NUM_HEAP_REGIONS];
static LTCoreBSP_LTHeapConfig LTHeapConfig = { s_heapRegions, ESP32_NUM_HEAP_REGIONS };

/*___________________
  BSP configuration */

/* Top of the startup stack, from mastering/ld/esp32p4/sections.ld */
extern int _system_stack_top;

/*
 * nClockSpeedHz is the rate of whatever pGetCycleCount() returns, not nominally
 * "the CPU clock" - the bl70x sets it to its 2MHz mtimer rate.  Here the counter
 * is systimer UNIT0, which runs at a fixed 16MHz whatever the CPU is doing, so
 * this is a constant and is independent of the frequency the ROM left the core
 * at.
 *
 * vectorMode is CLICVectored because this part's interrupt controller is a
 * CLIC, and mtvec has to be in CLIC mode for it to deliver anything at all.  The
 * name describes the mtvec mode rather than per line hardware vectoring, which
 * is left off - see Esp32_Irq.h.
 */
static LTCoreBSP_RISCV_SystemConfig LTSystemConfig = {
    .nClockSpeedHz      = kEsp32_RegisterSYSTIMER_TICK_HZ,
    .pStackTop          = &_system_stack_top,
    .pDispatcher        = &Esp32_InterruptDispatcher,
    .pGetCycleCount     = &Esp32_GetCycleCount,
    .vectorMode         = kLTCoreBSP_RISCV_VectorMode_CLICVectored
};

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    /* NOTE: CPU interrupts stay disabled for the whole of this call - call_start_cpu0()
     * masked them and LTK unmasks them once the scheduler is ready.  Everything below
     * arms hardware that will fire the moment it does, so ordering matters: the matrix
     * and the CLIC are cleared of whatever the ROM left in them before anything routes
     * a line of its own, and the tick is started last. */

    Esp32InitializeIRQs();

    Esp32_InitializeCycleCount();

    /* UART0 on GPIO37/38 - see Esp32_Console.c */
    Esp32_ConsoleInitialize(pCallbacks);

    s_heapRegions[0] = (LTCoreBSP_HeapRegion) { (u8*)&_heap0_start, HEAP_REGION_SIZE(0), kLTMemoryRegionFlags_SRAM | kLTMemoryRegionFlags_Malloc };
    s_heapRegions[1] = (LTCoreBSP_HeapRegion) { (u8*)&_heap1_start, HEAP_REGION_SIZE(1), kLTMemoryRegionFlags_SRAM | kLTMemoryRegionFlags_Malloc };
    s_heapRegions[2] = (LTCoreBSP_HeapRegion) { (u8*)&_heap4_start, HEAP_REGION_SIZE(4), kLTMemoryRegionFlags_SRAM | kLTMemoryRegionFlags_Malloc };

    Esp32_InitializeTick();

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*____________
  debugging */
static bool LT_ISR_SAFE
LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) {
    LT_UNUSED(pFile);
    LT_UNUSED(nLine);
    LT_UNUSED(pTest);
    #if 1
        return true;   /* DRW 07-Feb-23 : always do asserts, even in release mode now */
    #else
        #ifdef LT_DEBUG
            /* return true to trap to debugger on assert - may be used to implement abort/continue prompt */
            return true;
        #else
            return false;
        #endif
    #endif
}

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    /* LT Configuration */
    .pLTSystemConfig = &LTSystemConfig,
    .pLTHeapConfig   = &LTHeapConfig,

    /* BSP Functions */
    .PutCharsToConsole    = Esp32_ConsolePutChars,
    .DebugAssertFailed    = LTCoreBSP_DebugAssertFailed,

};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Sep-26   claudius    created, from esp32c3/Esp32_LTCoreBSP.c
 */
