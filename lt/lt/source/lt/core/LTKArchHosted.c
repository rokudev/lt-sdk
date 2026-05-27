/******************************************************************************
 * lt/source/lt/core/ltk/LTKArchHosted.c                 LTK Hosted BSP Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"
#include "LTCoreImpl.h"
#include "LTHandle.h"
#include "LTThreadImpl.h"
#include "lt/core/bsp/LTHostAPI.h"
#include "lt/core/LTMonitor.h"

declare_LTTRACE_STREAM(mutex);

typedef struct AllocationHeader {
    u32 seal;
    u32 allocationSize;
} AllocationHeader; /* must be multiple of 8 bytes */
LT_STATIC_ASSERT_SIZE_32_64(AllocationHeader, 8, 8)

#define LTKARCHHOSTED_MEM_SEAL          ((u32)0xDEAD5EA1)
#define LTKARCHHOSTED_MEMSIZE_HEADER    (sizeof(AllocationHeader))

static const LTCoreBSP *s_pBSP;
static LTAtomic         s_allocationCountCurrent;
static LTAtomic         s_allocationCountMax;

/********************
 * Idle Hook Procs */
typedef void (IdleHookProc)(void);
static  void DefaultIdleHookProc (void) { }
static LTKIdleHookProc * s_pEnterIdleHookProc = &DefaultIdleHookProc;
static LTKIdleHookProc * s_pIdleTickHookProc = &DefaultIdleHookProc;
static LTKIdleHookProc * s_pExitIdleHookProc = &DefaultIdleHookProc;
void LTKSetIdleHooks(LTKIdleHookProc * pEnterIdleHook, LTKIdleHookProc * pIdleTickHook, LTKIdleHookProc * pExitIdleHook) {
    s_pEnterIdleHookProc = pEnterIdleHook ? pEnterIdleHook : &DefaultIdleHookProc;
    s_pIdleTickHookProc  = pIdleTickHook  ? pIdleTickHook  : &DefaultIdleHookProc;
    s_pExitIdleHookProc  = pExitIdleHook  ? pExitIdleHook  : &DefaultIdleHookProc;
}

/************
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk_arch_hosted");

/* Initialization */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback) {
    LT_UNUSED(pFaultCallback);
    LTK_LTLOG_INITIALIZE(pLTCoreLogFunction);
    s_pBSP = pBSP;
    LTKHeapInitialize();
}

/* Time */
s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    return s_pBSP->GetHighFrequencyCounterNanoseconds();
}

s64 LTKGetHighFrequencyCounterNanosecondResolution(void) LT_ISR_SAFE {
    return s_pBSP->GetHighFrequencyCounterNanosecondResolution();
}

void LTKAdvanceTickCount(u32 nTicks) {
    LT_UNUSED(nTicks);
}

/* Interrupts / Debug */
bool LTKInsideInterruptContext(void) LT_ISR_SAFE {
    return LTCoreBSP_InsideInterruptContext();
}

bool LTKInterruptsAreDisabled(void) LT_ISR_SAFE {
    return LTCoreBSP_InterruptsAreDisabled();
}

LT_SIZE LTKDisableInterrupts(void) LT_ISR_SAFE {
    return LTCoreBSP_DisableInterrupts();
}

void LTKEnableInterrupts(LT_SIZE nMask) LT_ISR_SAFE {
    LTCoreBSP_EnableInterrupts(nMask);
}

void LTKSetInterruptVector(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler, LTCore_InterruptPriority priority) {
    LT_UNUSED(nInterrupt);
    LT_UNUSED(pInterruptHandler);
    LT_UNUSED(priority);
}

void LTKSetInterruptPriority(u32 nInterrupt, LTCore_InterruptPriority priority) {
    LT_UNUSED(nInterrupt);
    LT_UNUSED(priority);
}

void LTKDebugBreak(void) {
    LTCoreBSP_DebugBreak();
}

/* Mutex */
void LTKMutexInitialize(void * pMutex) {
    s_pBSP->hostAPI->MutexInitialize(pMutex);
}

void LTKMutexFinalize(void * pMutex) {
    s_pBSP->hostAPI->MutexFinalize(pMutex);
}

void LTKMutexLock(void * pMutex) {
    s_pBSP->hostAPI->MutexLock(pMutex);
}

void LTKMutexUnlock(void * pMutex) {
    s_pBSP->hostAPI->MutexUnlock(pMutex);
}

bool LTKMutexTryLock(void * pMutex) {
    return s_pBSP->hostAPI->MutexTryLock(pMutex);
}

/*______________________________________
  LTMutex object private data members */
typedef_LTObjectImpl(LTMutex, LTMutexImpl) {
    void *mutex;
} LTOBJECT_API;

/*________________________________
  LTMutex object constructors */
static bool LTMutexImpl_ConstructObject(LTMutexImpl *mutex) {
    mutex->mutex = lt_malloc(s_pBSP->hostAPI->MutexInstanceSize());
    if (mutex->mutex) {
        LTKMutexInitialize(mutex->mutex);
        return true;
    }
    return false;
}

static void LTMutexImpl_DestructObject(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (mutex && mutex->mutex) {
        LTKMutexFinalize(mutex->mutex);
        lt_free(mutex->mutex);
    }
}

/*_______________________________
  LTMutex object api functions */
static void
LTMutexImpl_Lock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTTRACE_NUMERIC(mutex, 0);
    if (!mutex) return;
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_PrivateDataToThreadImpl(LTHandle_ReservePrivateData(hThread));
    if (! pImpl) { /* if not LT Thread can't twiddle thread state; still lock mutex */
        LTKMutexLock(mutex->mutex);
        return;
    }
    /* change the thread state when it is stable with respect to our reading it (another thread may have
       called Terminate in between our reading the thread state and trying to set it).
       if ever the thread state is TerminatePending or Terminated (thanks BSP), lock the mutex and return */
    u32 threadState;
    do {
        threadState = LTAtomic_Load(&pImpl->nThreadState);
        if ((kLTThread_ThreadState_TerminatePending == threadState) || (kLTThread_ThreadState_Terminated == threadState)) {
            LTKMutexLock(mutex->mutex);
            LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl));
            return;
        }
    }
    while (! LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, kLTThread_ThreadState_MutexBlocked));

    // ok we changed threadState to kLTThread_ThreadState_MutexBlocked; lock the mutex (phew!)
    LTKMutexLock(mutex->mutex);

    // restore the thread state back but only if it didn't get changed by another thread
    // (e.g. to TerminatePending) while we were blocking
    LTAtomic_CompareAndExchange(&pImpl->nThreadState, kLTThread_ThreadState_MutexBlocked, threadState);
    LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl));
}

static void
LTMutexImpl_Unlock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTTRACE_NUMERIC(mutex, 3);
    if (mutex && mutex->mutex) {
        LTKMutexUnlock(mutex->mutex);
    }
}

static bool
LTMutexImpl_TryLock(LTMutexImpl *mutex) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    bool bRetVal = false;
    if (mutex && mutex->mutex) {
        bRetVal = LTKMutexTryLock(mutex->mutex);
        LTTRACE_NUMERIC(mutex, bRetVal ? 1 : 2);
    }
    return bRetVal;
}

/*_________________________________
  LTMutex LTObjectApi definition */
define_LTObjectImplPublic(LTMutex, LTMutexImpl,
    Lock, TryLock, Unlock,
);

/* Monitors */
LT_SIZE LTKMonitorInstanceSize(void) {
    return s_pBSP->hostAPI->MonitorInstanceSize();
}

void LTKMonitorInitialize(void * pMonitor) {
    s_pBSP->hostAPI->MonitorInitialize(pMonitor);
}

void LTKMonitorFinalize(void * pMonitor) {
    s_pBSP->hostAPI->MonitorFinalize(pMonitor);
}

void LTKMonitorEnter(void * pMonitor) {
    s_pBSP->hostAPI->MonitorEnter(pMonitor);
}

void LTKMonitorExit(void * pMonitor) {
    s_pBSP->hostAPI->MonitorExit(pMonitor);
}

void LTKMonitorNotify(void * pMonitor) LT_ISR_SAFE {
    s_pBSP->hostAPI->MonitorNotify(pMonitor);
}

bool LTKMonitorWait(void * pMonitor, s64 nTimeoutNanoseconds) {
    return s_pBSP->hostAPI->MonitorWait(pMonitor, nTimeoutNanoseconds);
}

/*________________________________________
  LTMonitor object private data members */
typedef_LTObjectImpl(LTMonitor, LTMonitorImpl) {
    LTKMonitor * pMonitor;
} LTOBJECT_API;

/*________________________________
  LTMonitor object constructors */
static bool LTMonitorImpl_ConstructObject(LTMonitorImpl *monitor) {
    return (NULL != (monitor->pMonitor = core_malloc(LTKMonitorInstanceSize()))) ? LTKMonitorInitialize(monitor->pMonitor), true : false;
}

static void LTMonitorImpl_DestructObject(LTMonitorImpl *monitor) {
    LTKMonitorFinalize(monitor->pMonitor);
    core_free(monitor->pMonitor);
}

/*_________________________________
  LTMonitor object api functions */
static void LTMonitorImpl_Enter(LTMonitorImpl *monitor) {                LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    u32 priorThreadState = LTThreadImpl_IngressBlockingThreadState(kLTThread_ThreadState_MutexBlocked);
    LTKMonitorEnter(monitor->pMonitor);
    LTThreadImpl_EgressBlockingThreadState(kLTThread_ThreadState_MutexBlocked, priorThreadState);
}

static void LTMonitorImpl_Exit(LTMonitorImpl *monitor) {                 LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTKMonitorExit(monitor->pMonitor);
}

static bool LTMonitorImpl_Wait(LTMonitorImpl *monitor, LTTime timeout) { LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    u32 priorThreadState = LTThreadImpl_IngressBlockingThreadState(kLTThread_ThreadState_WaitBlocked);
    bool bRetVal = LTKMonitorWait(monitor->pMonitor, timeout.nNanoseconds);
    LTThreadImpl_EgressBlockingThreadState(kLTThread_ThreadState_WaitBlocked, priorThreadState);
    return bRetVal;
}

static void LTMonitorImpl_Notify(LTMonitorImpl *monitor) LT_ISR_SAFE {
    LTKMonitorNotify(monitor->pMonitor);
}

/*___________________________________
  LTMonitor LTObjectApi definition */
define_LTObjectImplPublic(LTMonitor, LTMonitorImpl,
    Enter, Exit, Wait, Notify
);

/* Threads */
LT_SIZE LTKThreadInstanceSize(void) {
    return s_pBSP->hostAPI->ThreadInstanceSize();
}

/* Threads */
u32 LTKThreadGetDefaultStackSize(void) {
    return s_pBSP->hostAPI->ThreadGetDefaultStackSize ? s_pBSP->hostAPI->ThreadGetDefaultStackSize() : 512;
}

void LTKThreadInitializeAndStartScheduler(void * pThread, u8 nPriority,
                                              u32 nStackSize, const char * pName,
                                              void (* pInitialThreadProc)(void * pClientData),
                                              void * pClientData) {
    s_pBSP->hostAPI->ThreadInitializeAndStartScheduler(pThread, nPriority, nStackSize, pName,
                                                              pInitialThreadProc, pClientData);
}

bool
LTKThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize,
                              const char * pName, void (* pThreadProc)(void * pClientData),
                              void * pClientData) {
    return s_pBSP->hostAPI->ThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pThreadProc, pClientData);
}

void LTKThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    s_pBSP->hostAPI->ThreadGetStackUsage(pThread, pStackSizeToSet, pCurrentStackUsageToSet, pMaxStackUsageToSet);
}

LTKThread * LTKGetRunningThreadPtr(void) {
    return NULL;
}

void LTKThreadSetPriority(void * pThread, u8 nPriority) {
    s_pBSP->hostAPI->ThreadSetPriority(pThread, nPriority);
}

bool LTKThreadWaitUntilFinished(void * pThread, s64 nTimeoutNanoseconds) {
    return s_pBSP->hostAPI->ThreadWaitUntilFinished(pThread, nTimeoutNanoseconds);
}

void LTKThreadFinalize(void * pThread) {
    s_pBSP->hostAPI->ThreadFinalize(pThread);
}

LT_TEXT_RAM_CRITICAL(1)
void * LTKThreadGetCurrentThreadInstanceData(void) {
    return s_pBSP->hostAPI->ThreadGetCurrentThreadInstanceData();
}

void LTKThreadSleep(s64 nNanoseconds) {
    s_pBSP->hostAPI->ThreadSleep(nNanoseconds);
}

s64 LTKThreadGetCpuTime(void * pThread) {
    return s_pBSP->hostAPI->GetThreadCpuUtilization(pThread);
}

/* Memory Allocator */
void LTKHeapInitialize(void) {
    /* Heap is initialized by the host either before or during Hosted BSP initialization. */

    /* initialize our track count variables */
    LTAtomic_Store(&s_allocationCountCurrent, 0);
    LTAtomic_Store(&s_allocationCountMax, 0);
}

void LTKHeapAddRegion(u8 * pRegionBuffer, u32 nSizeInBytes) {
    LT_UNUSED(pRegionBuffer);
    LT_UNUSED(nSizeInBytes);
}

u32 LTKHeapAddRegionEx(u8 * pRegionBuffer, u32 nSizeInBytes, bool bExclusive) {
    LT_UNUSED(pRegionBuffer);
    LT_UNUSED(nSizeInBytes);
    LT_UNUSED(bExclusive);
    return 0;
}

bool LTKHeap_IsExclusiveByPtr(const void * p) {
    /* Hosted OS has no concept of exclusive regions; the host malloc backs all
     * allocations. */
    LT_UNUSED(p);
    return false;
}

void * LTKAlloc(LT_SIZE nBytes) {
    if (0 == nBytes) {
        return NULL;
    }
    nBytes += LTKARCHHOSTED_MEMSIZE_HEADER;
    AllocationHeader *pMem = s_pBSP->hostAPI->malloc(nBytes);
    if (! pMem) {
        return NULL;
    }
    pMem->allocationSize = nBytes;
    pMem->seal = LTKARCHHOSTED_MEM_SEAL;

    nBytes = LTAtomic_FetchAdd(&s_allocationCountCurrent, (u32)nBytes) + nBytes;
    u32 nBytesMax = LTAtomic_Load(&s_allocationCountMax);
    while (((u32)nBytes > nBytesMax) && (! (LTAtomic_CompareAndExchange(&s_allocationCountMax, nBytesMax, (u32)nBytes)))) nBytesMax = LTAtomic_Load(&s_allocationCountMax);

    return pMem + 1;
}


/* Hosted LTK has a single host-malloc backed heap; regions are not modeled
 * (LTKHeapAddRegionEx is a no-op on hosted).  All region values — sentinel,
 * the implicit "default", and any platform-specific named region — fall through
 * to LTKAlloc.  Returning NULL for region != 0/1 would diverge hosted unit tests
 * from on-target behavior, masking real bugs. */
void * LTKAllocFromRegion(LTMemoryRegion region, LT_SIZE nBytes) {
    LT_UNUSED(region);
    return LTKAlloc(nBytes);
}

void * LTKReAlloc(void * pMem, LT_SIZE nBytes) {
    if (nBytes == 0) {
        LTKFree(pMem);
        return NULL;
    }
    if (NULL == pMem) return LTKAlloc(nBytes);
    if ((LT_SIZE)pMem > LTKARCHHOSTED_MEMSIZE_HEADER && (0 == (((LT_SIZE)pMem) & 0x7))) {
        AllocationHeader * pHeader = (AllocationHeader *)pMem - 1;
        if (pHeader->seal == LTKARCHHOSTED_MEM_SEAL) {
            u32 nOldBytes = pHeader->allocationSize;
            pHeader->seal = 0;
            AllocationHeader *pNewHeader = s_pBSP->hostAPI->realloc(pHeader, nBytes + LTKARCHHOSTED_MEMSIZE_HEADER);
            if (pNewHeader) {
                pNewHeader->allocationSize = nBytes + LTKARCHHOSTED_MEMSIZE_HEADER;
                pNewHeader->seal = LTKARCHHOSTED_MEM_SEAL;
                if (pNewHeader->allocationSize > nOldBytes) {
                    nOldBytes = pNewHeader->allocationSize - nOldBytes; /* increasal amount */
                    nOldBytes = LTAtomic_FetchAdd(&s_allocationCountCurrent, (u32)nOldBytes) + nOldBytes;
                    nBytes = LTAtomic_Load(&s_allocationCountMax);
                    while ((nOldBytes > nBytes) && (! (LTAtomic_CompareAndExchange(&s_allocationCountMax, nBytes, nOldBytes)))) nBytes = LTAtomic_Load(&s_allocationCountMax);
                }
                else if (pNewHeader->allocationSize < nOldBytes) {
                    nOldBytes -= pNewHeader->allocationSize; /* decreasal amount */
                    LTAtomic_FetchSubtract(&s_allocationCountCurrent, nOldBytes);
                }
                return pNewHeader + 1;
            }
            else {
                pHeader->seal = LTKARCHHOSTED_MEM_SEAL; /* original memory left unchanged, restore seal */
                return NULL; /* indicates realloc failed, original mem unchanged */
           }
        }
        else {
            return NULL;
        }
    }
    else {
        return NULL;
    }
}

void LTKFree(void * pMem) {
    if ((LT_SIZE)pMem > LTKARCHHOSTED_MEMSIZE_HEADER && (0 == (((LT_SIZE)pMem) & 0x7))) {
        AllocationHeader * pHeader = (AllocationHeader *)pMem - 1;
        if (pHeader->seal == LTKARCHHOSTED_MEM_SEAL) {
            LTAtomic_FetchSubtract(&s_allocationCountCurrent, pHeader->allocationSize);
            pHeader->seal = 0;
            pHeader->allocationSize = 0;
            s_pBSP->hostAPI->free(pHeader);
       }
    }
}

LT_SIZE LTKGetActualAllocationSize(void * pMem) {
    if ((LT_SIZE)pMem > LTKARCHHOSTED_MEMSIZE_HEADER && (0 == (((LT_SIZE)pMem) & 0x7))) {
        AllocationHeader * pHeader = (AllocationHeader *)pMem - 1;
        return (pHeader->seal == LTKARCHHOSTED_MEM_SEAL) ? pHeader->allocationSize - sizeof(AllocationHeader) : 0;
    }
    else LT_GetCore()->ConsoleStomp("GetSize fail\n");
    return 0;
}

LT_SIZE LTKGetTotalSystemRAM(void) {
    return s_pBSP->hostAPI->GetTotalSystemRAM();
}

LT_SIZE LTKGetAvailableSystemRAM(void) {
    return s_pBSP->hostAPI->GetAvailableSystemRAM();
}

LT_SIZE LTKGetSystemRAMLowWatermark(void) {
    return s_pBSP->hostAPI->GetSystemRAMLowWatermark();
}

LT_SIZE LTKGetLTKCurrentAllocationCount(void) {
    return LTAtomic_Load(&s_allocationCountCurrent);
}

LT_SIZE LTKGetLTKAllocationHighWatermark(void) {
    return LTAtomic_Load(&s_allocationCountMax);
}

LT_SIZE LTKGetLargestAvailableBlockInRAM(void) {
    return s_pBSP->hostAPI->GetAvailableSystemRAM();
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-May-21   tiberius    created
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  18-Dec-22   augustus    added LTKThreadGetDefaultStackSize()
 *  08-Mar-24   augustus    added current and max allocation tracking; use s_pHeapChangeCallback to enable LTCore tracking
 */
