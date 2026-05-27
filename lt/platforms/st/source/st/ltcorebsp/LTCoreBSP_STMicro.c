/******************************************************************************
 * platforms/st/source/st/ltcorebsp/LTCoreBSP_STMicro.c
 *                                          - LTCoreBSP for STMicro
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "bsp_hal.h"
#include <lt/core/bsp/LTCoreBSP.h>

/*___________
  #defines */
#define LTCORE_BSP_IGNORE_RELEASE_MODE_ASSERTS (0)   /* 0 or 1 */
     /* Usually in release mode asserts are ignored, but this may be overridden
        by setting LTCORE_BSP_IGNORE_RELEASE_MODE_ASSERTS as follows:
          With 0 set: the OS is instructed to handle the release mode LT_ASSERT as if in LT_DEBUG mode,
                      i.e. asserts in release mode will reboot the system (or invoke the debugger if attached)
          With 1 set: the OS is instructed to ignore release mode LT_ASSERTs and continue execution
      */
#if defined (ST_H755_NUCLEO_144)
    #define HEAP_BUFFER_SIZE     (384 * 1024)
#else
    #define HEAP_BUFFER_SIZE     ( 64 * 1024)
#endif

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };

/*___________________
   Heap definition  */
u8 __attribute__((aligned(8))) heapBuffer[HEAP_BUFFER_SIZE];

/*___________________
  BSP configuration */
static const LTCoreBSP_ArmCortexM_SystemConfig LTSystemConfig = {
#if defined(ST_L4S5_DISCOVERY_IOT_NODE)
    .nClockSpeedHz = 120000000,
#elif defined(ST_F767_NUCLEO_144)
    .nClockSpeedHz = 216000000,
#elif defined(ST_F4_DISCOVERY)
    .nClockSpeedHz = 168000000,
#elif defined(ST_H755_NUCLEO_144)
    .nClockSpeedHz = 480000000,
#else
    #error "Unknown BSP"
#endif
    .pSecurityContainer = NULL,
    .bUseExternalClockRef = false
};
static LTCoreBSP_HeapRegion s_heapRegion = { heapBuffer, sizeof(heapBuffer), false };
static const LTCoreBSP_LTHeapConfig LTHeapConfig = { 1, &s_heapRegion };

/*____________________________
  UART char rx ISR callback */
static void LT_ISR_SAFE
LTCoreBSP_RxUARTCallback(char ch) {
    s_pCoreCallbacks->ProcessISRConsoleInputChars(&ch, 1);
    s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
}

static void LT_ISR_SAFE
LTCoreBSP_PutCharsToConsole(const char * pChars, u32 nChars) {
    while (nChars--) HalSendToTxUART(*pChars++);
}

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    /* remember pCallbacks */
    s_pCoreCallbacks = pCallbacks;

    /* register UART rx ISR callback - do this after setting s_pCoreCallbacks */
    HalRegisterRxUARTCallback(LTCoreBSP_RxUARTCallback);
    HalEnableRxUARTIRQ(true);

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

    HalEnableRxUARTIRQ(false);
    HalRegisterRxUARTCallback(NULL);

    s_pCoreCallbacks = NULL;
    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*____________
  debugging */
static bool LT_ISR_SAFE
LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) {
    LT_UNUSED(pFile); LT_UNUSED(nLine); LT_UNUSED(pTest);

    /* This function is a filter function on LT_ASSERT handling.
       return true and the system will break to a debugger if present, reboot if not.
       return false and the assert will be ignored and life will continue.
     */

    #if defined(LT_DEBUG) || LTCORE_BSP_IGNORE_RELEASE_MODE_ASSERTS == 0
        return true;
    #else
        return false;
    #endif
}

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    /* LT Configuration */
    .pLTSystemConfig = &LTSystemConfig,
    .pLTHeapConfig   = &LTHeapConfig,

    /* BSP Functions */
    .PutCharsToConsole  = LTCoreBSP_PutCharsToConsole,
    .DebugAssertFailed  = LTCoreBSP_DebugAssertFailed,
};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Feb-21   augustus    refactored for simplified BSP
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 */
