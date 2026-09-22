/******************************************************************************
 * esp32c3/Esp32_LTCoreBSP.c                                       ESP32-C3 BSP
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
 * LTDeviceConfig.json.  The numbers come from the esp32, whose four regions are
 * heap0..heap3; this part has two - the main DRAM run, and the second stage
 * bootloader's own footprint reclaimed once it has handed over - so heap0 and
 * heap1 are all there is.  There is no PSRAM on this part and no SRAM2.
 *
 * Both regions are fixed at link time by memory.ld; nothing here is detected at
 * runtime, unlike the esp32s3 arm of this file.
 */
extern int _heap0_start;
extern int _heap0_end;
extern int _heap1_start;
extern int _heap1_end;

/* heap0 (DRAM), heap1 (reclaimed bootloader DRAM) */
#define ESP32_NUM_HEAP_REGIONS  2

#define HEAP_REGION_SIZE(n) (u32)(((u8*)&_heap##n##_end) - ((u8*)&_heap##n##_start))

static LTCoreBSP_HeapRegion s_heapRegions[ESP32_NUM_HEAP_REGIONS];
static LTCoreBSP_LTHeapConfig LTHeapConfig = { s_heapRegions, ESP32_NUM_HEAP_REGIONS };

/*___________________
  BSP configuration */

/* Top of the startup stack, from mastering/ld/esp32c3/sections.ld */
extern int _system_stack_top;

/*
 * nClockSpeedHz is the rate of whatever pGetCycleCount() returns, not nominally
 * "the CPU clock" - the bl70x sets it to its 2MHz mtimer rate.  Here the counter is
 * systimer UNIT0, which runs at a fixed 16MHz whatever the CPU is doing, so this is
 * a constant and is independent of the frequency the bootloader left the core at.
 *
 * vectorMode is Vectored, not CLICNotPresent: this part has no CLIC - it uses
 * Espressif's interrupt matrix and INTERRUPT_CORE0 - but its mtvec is hardwired
 * to MODE = 1 and reads back as (BASE & ~0xff) | 1 whatever is written.  Asking
 * for direct mode therefore yields a vectored core with a base rounded down to
 * the previous 256 byte boundary, and every trap lands at BASE + 4 * cause in
 * the middle of unrelated code.  LTKInitialize() writes mtvec itself, pointing
 * it at LTK's vector table in this mode, so the BSP must not set a base of its own.
 */
static LTCoreBSP_RISCV_SystemConfig LTSystemConfig = {
    .nClockSpeedHz      = kEsp32_RegisterSYSTIMER_TICK_HZ,
    .pStackTop          = &_system_stack_top,
    .pDispatcher        = &Esp32_InterruptDispatcher,
    .pGetCycleCount     = &Esp32_GetCycleCount,
    .vectorMode         = kLTCoreBSP_RISCV_VectorMode_Vectored
};

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    /* NOTE: CPU interrupts stay disabled for the whole of this call - call_start_cpu0()
     * masked them and LTK unmasks them once the scheduler is ready.  Everything below
     * arms hardware that will fire the moment it does, so ordering matters: the
     * multiplexer is cleared of whatever the ROM left in it before anything routes a
     * line of its own, and the tick is started last. */

    Esp32InitializeIRQs();

    Esp32_InitializeCycleCount();

    /* the USB serial/JTAG device - see Esp32_Console.c */
    Esp32_ConsoleInitialize(pCallbacks);

    s_heapRegions[0] = (LTCoreBSP_HeapRegion) { (u8*)&_heap0_start, HEAP_REGION_SIZE(0), kLTMemoryRegionFlags_SRAM | kLTMemoryRegionFlags_Malloc };
    s_heapRegions[1] = (LTCoreBSP_HeapRegion) { (u8*)&_heap1_start, HEAP_REGION_SIZE(1), kLTMemoryRegionFlags_SRAM | kLTMemoryRegionFlags_Malloc };

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
 *  17-Sep-26   claudius    created, from esp32s3/Esp32_LTCoreBSP.c
 */
