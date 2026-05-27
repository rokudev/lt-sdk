/******************************************************************************
 * platforms/common/source/common/ltcorebsp/LTCoreBSP_CommonEmpty.c
 *                                          - LTCoreBSP empty common copy source
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <lt/core/bsp/LTCoreBSP.h>

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;
static bool LTCoreBSP_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData);

/*___________________
  static variables */
static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_nLTCoreBSPInitialized = 0;

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (s_nLTCoreBSPInitialized) return NULL; /* don't let anyone come in here except LTCore the first time */
    s_nLTCoreBSPInitialized = 1; /* don't need a test and set, LTCore calls this before any threads are running */

    // remember pCallbacks
    s_pCoreCallbacks = pCallbacks;

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((0 == s_nLTCoreBSPInitialized) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */

    s_pCoreCallbacks = NULL;
    s_nLTCoreBSPInitialized = 0;
}

/*________________________________________________________________
  LTCoreBSP global functions (statically linked only with LTCore) \________________________________________________
  Excluded from s_bsp struct to enable direct placement into LTCore and LTStdlib interfaces to avoid extra thunk */
bool LT_ISR_SAFE
LTCoreBSP_InsideInterruptContext(void) {
    return false;
}

LT_SIZE LT_ISR_SAFE
LTCoreBSP_DisableInterrupts(void) {
    return 0;
}

void LT_ISR_SAFE
LTCoreBSP_EnableInterrupts(LT_SIZE nMask) {
    LT_UNUSED(nMask);
}

bool LT_ISR_SAFE
LTCoreBSP_InterruptsAreDisabled(void) {
    return false;
}

void
LTCoreBSP_DebugBreak(void) {
    /* break into debugger if debugger available, otherwise raise exception */
}

/* _______________________________________________
 * static functions placed into struct LTCoreBSP */
/*_________________________
  high frequency counter */
static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanoseconds(void) {
    return 0;
}

static s64 LT_ISR_SAFE
LTCoreBSP_GetHighFrequencyCounterNanosecondResolution(void) {
    return 0;
}

static void LT_ISR_SAFE
LTCoreBSP_EnableConsoleReceiveInterrupt(bool bEnable) {
    if (bEnable) /*  enable UART receive-character-available interrupt */;
    else         /* disable UART receive-character-available interrupt */;
}

/*_____________________
  putchar to console */
static void LT_ISR_SAFE
LTCoreBSP_ConsoleCramChar(char c) { putchar((int)c); }

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

/*_______________________
  allocation functions */
static void *
LTCoreBSP_malloc(LT_SIZE nBytes) { /* this is here temporarily */
    return malloc(nBytes);
}

static void *
LTCoreBSP_realloc(void * pMem, LT_SIZE nBytes) { /* this is here temporarily */
    return realloc(pMem, nBytes);
}

static void
LTCoreBSP_free(void * pMem) { /* this is here temporarily */
    free(pMem);
}

/*________________
  ram functions */
static LT_SIZE
LTCoreBSP_GetTotalSystemRAM(void) {
    return 0;
}

static LT_SIZE
LTCoreBSP_GetAvailableSystemRAM(void) {
    return 0;
}

static LT_SIZE
LTCoreBSP_GetSystemRAMLowWatermark(void) {
    return 0;
}

/*_________________________________________________________
/*___________________________________________________________________________________________________________
  mutex - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
static LT_SIZE
LTCoreBSP_MutexInstanceSize(void) {
    return 0;
}

static void
LTCoreBSP_MutexInitialize(void * pMutex) {
    LT_UNUSED(pMutex);
}

static void
LTCoreBSP_MutexFinalize(void * pMutex) {
    LT_UNUSED(pMutex);
}

static void
LTCoreBSP_MutexLock(void * pMutex) {
    LT_UNUSED(pMutex);
}

static void
 LTCoreBSP_MutexUnlock(void * pMutex) {
    LT_UNUSED(pMutex);
}

static bool
 LTCoreBSP_MutexTryLock(void * pMutex) {
    LT_UNUSED(pMutex);
    return false;
}

/*___________________________________________________________________________________________________________
  monitor - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
static LT_SIZE
LTCoreBSP_MonitorInstanceSize(void) {
    return 0;
}

static void
LTCoreBSP_MonitorInitialize(void * pMonitor) {
    LT_UNUSED(pMonitor);
}

static void
LTCoreBSP_MonitorFinalize(void * pMonitor) {
    LT_UNUSED(pMonitor);
}

static void
LTCoreBSP_MonitorEnter(void * pMonitor) {
    LT_UNUSED(pMonitor);
}

static void
LTCoreBSP_MonitorExit(void * pMonitor) {
    LT_UNUSED(pMonitor);
}

static void LT_ISR_SAFE
LTCoreBSP_MonitorNotify(void * pMonitor) {
    LT_UNUSED(pMonitor);
}

static bool
LTCoreBSP_MonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    LT_UNUSED(pMonitor);
    LT_UNUSED(nTimeoutNanoseconds);
    return false;
}

/*___________________________________________________________________________________________________________
  thread - this will move into core once we have our own kernel - tbd how we run that on top of a hosted os */
static LT_SIZE
LTCoreBSP_ThreadInstanceSize(void) {
    return 0;
}

static void
LTCoreBSP_ThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pInitialThreadProc)(void * pClientData), void * pClientData) {
    LT_UNUSED(pThread);
    LT_UNUSED(nPriority);
    LT_UNUSED(nStackSize);
    LT_UNUSED(pName);
    LT_UNUSED(pInitialThreadProc);
    LT_UNUSED(pClientData);
    /* This function should do three things:
    1. perform any instance initialization of pThread.
    2. start the scheduler, running the initial thread as specified by the function parameters.
    3. block until the thread exits, then UNINITIALIZE THE INSTANCE DATA,  STOP THE SCHEDULER and return */
}

static void
LTCoreBSP_ThreadStopScheduler(void) {
}

static bool
LTCoreBSP_ThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize, const char * pName, void (* pThreadProc)(void * pClientData), void * pClientData) {
    LT_UNUSED(pThread);
    LT_UNUSED(nPriority);
    LT_UNUSED(nStackSize);
    LT_UNUSED(pName);
    LT_UNUSED(pThreadProc);
    LT_UNUSED(pClientData);
    /* called to spawn threads after the scheduler has been started.  This function should do three things:
        1. perform any instance initialization of pThread.
        2. run the initial thread as specified by the function parms.
        3. return from this function immediately after spawning the thread. */
    return false;
}

static void
LTCoreBSP_ThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    LT_UNUSED(pThread);
    *pStackSizeToSet = *pCurrentStackUsageToSet = *pMaxStackUsageToSet = 0;
}

static bool
LTCoreBSP_ThreadSetPriority(void * pThread, u8 nPriority) {
    LT_UNUSED(pThread);
    LT_UNUSED(nPriority);
    return false;
}

static bool
LTCoreBSP_ThreadWaitUntilFinished(void * pThread, s64 nTimeoutNanoseconds) {
    LT_UNUSED(pThread);
    LT_UNUSED(nTimeoutNanoseconds);
    /* This function should block for [at least] nTimeoutNanoseconds and return true at the point when the thread
       completely finishes all execution, or false if the thread has not finished execution within the timeout period.
       By returning true, this function indicates it is now legal to subsequently call LTCoreBSP_ThreadUninitializeInstance()
       on the thread object.  */
    return false;
}

static void
LTCoreBSP_ThreadFinalize(void * pThread) {
    LT_UNUSED(pThread);
    /* called to uninitialize the thread
       This function call will always be preceded by a call to LTCoreBSP_ThreadWaitUntilThreadFinished() that returned
       a value of true, therefore it is guaranteed the thread is no longer running when this function is called. */
}

static void *
LTCoreBSP_ThreadGetCurrentThreadInstanceData(void) {
    return NULL;
}

static void
LTCoreBSP_ThreadSleep(s64 nNanoseconds) {
    LT_UNUSED(nNanoseconds);
}

/*___________________________________________________________________________________________________________________
  Dynamic library enumeration and loading ala dlopen/dlsym/dlclose - only if platform has a runtime dynamic loader */
#ifndef LT_NO_DYNAMIC_LOADER
    static bool
    LTCoreBSP_LibraryLoad(const char * pLTLibraryName, void ** pLibraryHandleToSet) {
        LT_UNUSED(pLTLibraryName);
        LT_UNUSED(pLibraryHandleToSet);
        /* This function returns one of the following result codes:
               LTCOREBSP_LOADLIBRARY_SUCCESS,
               LTCOREBSP_LOADLIBRARY_ERROR_LIBNOTFOUND,
               LTCOREBSP_LOADLIBRARY_ERROR_UNRESOLVEDSYMBOL,
               LTCOREBSP_LOADLIBRARY_ERROR_LIBINVALID,
               LTCOREBSP_LOADLIBRARY_ERROR_GENERIC           */
        return false;
    }

    static void
    LTCoreBSP_LibraryUnload(void * pLibraryHandle) {
        LT_UNUSED(pLibraryHandle);
    }

    static void *
    LTCoreBSP_LibraryLookupSymbol(void * pLibraryHandle, const char * pSymbolName) {
        LT_UNUSED(pLibraryHandle);
        LT_UNUSED(pSymbolName);
        return NULL;
    }

    static bool
    LTCoreBSP_LibraryEnumerate(LTCoreBSP_LibraryEnumProc * pEnumProc, void * pClientData) {
        LT_UNUSED(pEnumProc);
        LT_UNUSED(pClientData);
        return false;
    }

#endif /* #ifndef LT_NO_DYNAMIC_LOADER */

/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    .GetHighFrequencyCounterNanoseconds = LTCoreBSP_GetHighFrequencyCounterNanoseconds,
    .GetHighFrequencyCounterNanosecondResolution = LTCoreBSP_GetHighFrequencyCounterNanosecondResolution,

    .EnableConsoleReceiveInterrupt = LTCoreBSP_EnableConsoleReceiveInterrupt,
    .ConsoleCramChar = LTCoreBSP_ConsoleCramChar,

    .DebugAssertFailed = LTCoreBSP_DebugAssertFailed,

    .malloc = LTCoreBSP_malloc,
    .realloc = LTCoreBSP_realloc,
    .free = LTCoreBSP_free,

    .GetTotalSystemRAM = LTCoreBSP_GetTotalSystemRAM,
    .GetAvailableSystemRAM = LTCoreBSP_GetAvailableSystemRAM,
    .GetSystemRAMLowWatermark = LTCoreBSP_GetSystemRAMLowWatermark,

    .MutexInstanceSize = LTCoreBSP_MutexInstanceSize,
    .MutexInitialize = LTCoreBSP_MutexInitialize,
    .MutexFinalize = LTCoreBSP_MutexFinalize,
    .MutexLock = LTCoreBSP_MutexLock,
    .MutexUnlock = LTCoreBSP_MutexUnlock,
    .MutexTryLock = LTCoreBSP_MutexTryLock,

    .MonitorInstanceSize = LTCoreBSP_MonitorInstanceSize,
    .MonitorInitialize = LTCoreBSP_MonitorInitialize,
    .MonitorFinalize = LTCoreBSP_MonitorFinalize,
    .MonitorEnter = LTCoreBSP_MonitorEnter,
    .MonitorExit = LTCoreBSP_MonitorExit,
    .MonitorNotify = LTCoreBSP_MonitorNotify,
    .MonitorWait = LTCoreBSP_MonitorWait,

    .ThreadInstanceSize = LTCoreBSP_ThreadInstanceSize,
    .ThreadInitializeAndStartScheduler = LTCoreBSP_ThreadInitializeAndStartScheduler,
    .ThreadStopScheduler = LTCoreBSP_ThreadStopScheduler,
    .ThreadInitializeAndRun = LTCoreBSP_ThreadInitializeAndRun,
    .ThreadGetStackUsage = LTCoreBSP_ThreadGetStackUsage,
    .ThreadSetPriority = LTCoreBSP_ThreadSetPriority,
    .ThreadWaitUntilFinished = LTCoreBSP_ThreadWaitUntilFinished,
    .ThreadFinalize = LTCoreBSP_ThreadFinalize,
    .ThreadGetCurrentThreadInstanceData = LTCoreBSP_ThreadGetCurrentThreadInstanceData,
    .ThreadSleep = LTCoreBSP_ThreadSleep,

#ifndef LT_NO_DYNAMIC_LOADER
    .LibraryLoad = LTCoreBSP_LibraryLoad,
    .LibraryUnload = LTCoreBSP_LibraryUnload,
    .LibraryLookupSymbol = LTCoreBSP_LibraryLookupSymbol,
    .LibraryEnumerate = LTCoreBSP_LibraryEnumerate
#endif
};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Feb-21   augustus    refactored for simplified BSP; created as copy source
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32
 *  15-Jun-21   tiberius    remove handle concept from BSPs, pass in void * instead
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 */
