/******************************************************************************
 * LTCoreBSP for ESP32
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/bsp/LTCoreBSP.h>

#include "Esp32_Registers.h"
#include "Esp32_SoC.h"
#include "Esp32_PSRAM.h"
#include "Esp32_Console.h"

#include <setjmp.h>

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
 * LTDeviceConfig.json, so heap0..heap3 keep the indexes they have always had
 * and PSRAM is appended after them.
 *
 * Which of those regions actually exist is fixed at link time by memory.ld.
 * What varies from board to board - whether a PSRAM part is fitted and how big
 * it is - is detected at runtime, below.
 */
extern int _heap0_start;
extern int _heap0_end;
extern int _heap1_start;
extern int _heap1_end;
extern int _heap2_start;
extern int _heap2_end;
extern int _heap3_start;
extern int _heap3_end;
/* the external RAM window, sized by the linker and clamped by the probe */
extern int _heap4_start;
extern int _heap4_end;

/* heap0..heap3 as they have always been, plus PSRAM at index 4 */
#define ESP32_MAX_HEAP_REGIONS  5

static LTCoreBSP_HeapRegion s_heapRegions[ESP32_MAX_HEAP_REGIONS];
/* nRegions is filled in by LTCoreBSP_Initialize once the PSRAM probe has run,
 * so a board with no PSRAM fitted registers no zero-sized trailing region */
static LTCoreBSP_LTHeapConfig LTHeapConfig = { 0, s_heapRegions };
#define HEAP_REGION_SIZE(n) (u32)(((u8*)&_heap##n##_end) - ((u8*)&_heap##n##_start))

/*___________________
  BSP configuration */
static const LTCoreBSP_Xtensa_SystemConfig LTSystemConfig = {
    .nClockSpeedHz      = 240000000
};

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    Esp32_ConsoleInitialize(pCallbacks);

    Esp32_PSRAM_Info psram;
    bool bHavePSRAM = Esp32_PSRAM_Initialize(&psram);
    u8   nRegions;

    s_heapRegions[0] = (LTCoreBSP_HeapRegion) { (u8*)&_heap0_start, HEAP_REGION_SIZE(0), false };
    s_heapRegions[1] = (LTCoreBSP_HeapRegion) { (u8*)&_heap1_start, HEAP_REGION_SIZE(1), false };
    s_heapRegions[2] = (LTCoreBSP_HeapRegion) { (u8*)&_heap2_start, HEAP_REGION_SIZE(2), false };
    s_heapRegions[3] = (LTCoreBSP_HeapRegion) { (u8*)&_heap3_start, HEAP_REGION_SIZE(3), false };
    nRegions = 4;

    if (bHavePSRAM) {
        /* The external RAM window is a fixed linker segment here
         * (extern_ram_seg), and .ext_ram_noinit / .ext_ram.bss may have taken
         * a bite out of the front of it, so start where the linker says and
         * clamp to whatever the probe actually mapped. */
        u8 * pStart = (u8*)&_heap4_start;
        u8 * pEnd   = psram.pBase + psram.nSizeInBytes;
        if (pEnd > (u8*)&_heap4_end) pEnd = (u8*)&_heap4_end;

        if (pEnd > pStart) {
            /* Quad PSRAM cannot be reached by EDMA and is behind a
             * bank-switched 4MB window, so it is exclusive: general
             * allocations stay in internal RAM, and callers that want PSRAM
             * ask for it by region. */
            s_heapRegions[nRegions++] = (LTCoreBSP_HeapRegion) { pStart, (u32)(pEnd - pStart), true };
        }
    }

    LTHeapConfig.nRegions = nRegions;

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

/*____________
  wrappers  */
void __wrap_longjmp(jmp_buf buf, int nVal) {
    LT_UNUSED(buf);
    LT_UNUSED(nVal);
    /* longjmp shouldn't be needed, let's assert instead */
    esp_rom_printf("longjmp not supported.\n");
    while (1);
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
 *  25-Mar-22   tiberius    created
 */
