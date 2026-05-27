/******************************************************************************
 * lt/source/lt/ltk/LTKernel.c                                  LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"

/***************
 * Thread Data */
LTKList              _LTK_runQueues[kLTKNumThreadPriorities];
LTKThread          * _LTK_pRunningThread = NULL;
LTKThread            _LTK_idleThread = { .pName = "Idle" };
u8 LT_ALIGNED(16)    _LTK_idleThreadStack[kLTK_IdleThreadStackSize];

#if LTKERNEL_STYLE == LTKERNEL_STYLE_1
static volatile bool s_deferredYield = false;
#endif

/***********
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk");

/******************
 * Circular Lists */
void LTKList_Init(LTKList * pList) LT_ISR_SAFE {
    pList->pPrev = pList;
    pList->pNext = pList;
}

void LTKList_InsertHead(LTKList * pList, LTKList_Node * pNodeToAdd) LT_ISR_SAFE {
    pNodeToAdd->pPrev = pList;
    pNodeToAdd->pNext = pList->pNext;
    pList->pNext->pPrev = pNodeToAdd;
    pList->pNext = pNodeToAdd;
}

void LTKList_AddTail(LTKList * pList, LTKList_Node * pNodeToAdd) LT_ISR_SAFE {
    pNodeToAdd->pPrev = pList->pPrev;
    pNodeToAdd->pNext = pList;
    pList->pPrev->pNext = pNodeToAdd;
    pList->pPrev = pNodeToAdd;
}

void LTKList_Remove(LTKList * pNodeToRemove) LT_ISR_SAFE {
    pNodeToRemove->pNext->pPrev = pNodeToRemove->pPrev;
    pNodeToRemove->pPrev->pNext = pNodeToRemove->pNext;
    // Mark removed element as removed
    pNodeToRemove->pPrev = pNodeToRemove;
    pNodeToRemove->pNext = pNodeToRemove;
}

void LTKList_MoveToEnd(LTKList * pList, LTKList_Node * pNodeToMove) LT_ISR_SAFE {
    // Remove element
    pNodeToMove->pNext->pPrev = pNodeToMove->pPrev;
    pNodeToMove->pPrev->pNext = pNodeToMove->pNext;
    // Add to end of list
    pNodeToMove->pPrev = pList->pPrev;
    pNodeToMove->pNext = pList;
    pList->pPrev->pNext = pNodeToMove;
    pList->pPrev = pNodeToMove;
}

/**************
 * Interrupts */
LT_SIZE LTKDisableInterrupts(void) LT_ISR_SAFE {
    return DisableInterruptsNested();
}

void LTKEnableInterrupts(LT_SIZE nMask) LT_ISR_SAFE {
#if LTKERNEL_STYLE == LTKERNEL_STYLE_1
    if (InterruptsGotDisabled(nMask) && s_deferredYield) {
        s_deferredYield = false;
        // Deferred Yield from LTKMonitorNotify()
        YieldThread();
    }
#endif
    EnableInterruptsNested(nMask);
}

bool LTKInsideInterruptContext(void) LT_ISR_SAFE {
    return InsideInterruptContext();
}

bool LTKInterruptsAreDisabled(void) LT_ISR_SAFE {
    return InterruptsAreDisabled();
}

/***********
 * Threads */
LT_SIZE LTKThreadInstanceSize(void) {
    return sizeof(LTKThread);
}

u32 LTKThreadGetDefaultStackSize(void) {
    return kLTK_DefaultThreadStackSize;
}

static bool InitAndRunThread(LTKThread * pThread, s8 nPriority, void (*pEntry)(void *),
                                 void * arg, u8 * pStackBottom, u32 nStackSize) {
    LTKList_Init(&pThread->stopList);
    LTKList_Init(&pThread->timerLink);
    _LTKInitThread(pThread, nPriority, pEntry, (s32)arg, pStackBottom, nStackSize);
    u32 nMask = LockScheduler();
    SetThreadState(pThread, kLTKThreadState_Runnable);
    if (nPriority >= 0)
        LTKList_AddTail(&_LTK_runQueues[nPriority], &pThread->runLink);
    if (_LTK_pRunningThread != kLTKNoSuchThread && nPriority > _LTK_pRunningThread->nPriority)
        YieldThread();
    UnlockScheduler(nMask);
    return true;
}

void _LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction *ltCoreLogFunction, void (*pIdleEntry)(void *)) {
    BSP_LTLOG_INITIALIZE(ltCoreLogFunction);
    // Initialize empty run queues
    for (s8 nPriority = 0; nPriority < kLTKNumThreadPriorities; nPriority++)
        LTKList_Init(&_LTK_runQueues[nPriority]);
    // Initialize idle thread (lowest priority)
    InitAndRunThread(&_LTK_idleThread, -1, pIdleEntry, 0, _LTK_idleThreadStack, sizeof(_LTK_idleThreadStack));
    // Initialize the system heap
    LTKHeapInitialize();
    for (u32 nRegion = 0; nRegion < pBSP->pLTHeapConfig->nRegions; nRegion++) {
        const LTCoreBSP_HeapRegion * pR = &pBSP->pLTHeapConfig->pRegions[nRegion];
        u32 idx = LTKHeapAddRegionEx(pR->pRegionBuffer, pR->nSizeInBytes, pR->bExclusive);
        if (pR->bExclusive && idx >= LTK_MAX_HEAP_REGIONS) {
            /* Exclusive region was dropped because the slot table is full.  The block is
             * not added to the free list either, so this memory is permanently unreachable.
             * Fail fast at boot rather than letting a hardware DMA failure surface later. */
            LTK_LTLOG_STOMP_REDALERT("heap.region", "exclusive heap region dropped: slot table full (max=%u)",
                                     (unsigned)LTK_MAX_HEAP_REGIONS);
            LT_ASSERT(0);
        }
    }
}

bool LTKThreadInitializeAndRun(void * pInstance, u8 nPriority, u32 nStackSize,
                                  const char * pName, void (* pThreadProc)(void * pClientData),
                                  void * pClientData) {
    bool bSuccess = false;
    u8 * pStackBottom = LTKAlloc(nStackSize);
    if (pStackBottom) {
        LTKThread * pThread = (LTKThread *)pInstance;
        pThread->pInstanceData = pThread;
        pThread->pName = pName;
        if (InitAndRunThread(pThread, nPriority, pThreadProc, pClientData, pStackBottom, (LT_SIZE)nStackSize)) {
            bSuccess = true;
        } else {
            LTKFree(pStackBottom);
        }
    }
    return bSuccess;
}

void
LTKThreadFinalize(void * pInstance) {
    LTKThread * pThread = (LTKThread *)pInstance;
    // If the thread was never started, we don't have a stack.
    if (pThread->pStackBottom) {
        _LTKStackCheck(pThread);
        LTKFree(pThread->pStackBottom);
    }
}

void _LTKThreadExit(void) {
    u32 nMask = LockScheduler();
    SetThreadState(_LTK_pRunningThread, kLTKThreadState_Stopped);
    if (LTKList_IsNodeLinked(&_LTK_pRunningThread->timerLink))
        LTKList_Remove(&_LTK_pRunningThread->timerLink);
    LTKList * pElmSave;
    for (LTKList * pElm = _LTK_pRunningThread->stopList.pNext;
            pElm != &_LTK_pRunningThread->stopList; pElm = pElmSave) {
        pElmSave = pElm->pNext;
        LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, runLink);
        LTKList_Remove(pElm);
        LTKList_AddTail(&_LTK_runQueues[pThread->nPriority], &pThread->runLink);
        if (LTKList_IsNodeLinked(&pThread->timerLink))
            LTKList_Remove(&pThread->timerLink);
        SetThreadState(pThread, kLTKThreadState_Runnable);
    }
    LTKList_Remove(&_LTK_pRunningThread->runLink);
    YieldThread();
#if LTKERNEL_STYLE == LTKERNEL_STYLE_2
    UnlockScheduler(nMask);
    LT_ASSERT(0);
#else
    (void)nMask;
#endif
    // Unreachable
}

LTKThread * LTKGetRunningThreadPtr(void) {
    return _LTK_pRunningThread;
}

LT_TEXT_RAM_CRITICAL(1)
void * LTKThreadGetCurrentThreadInstanceData(void) {
    return _LTK_pRunningThread ? _LTK_pRunningThread->pInstanceData : NULL;
}

bool LTKThreadWaitUntilFinished(void * pInstance, s64 nNanosecondsToWait) {
    LTKThread * pThread = (LTKThread *)pInstance;
    bool bWithTimeout = _LTKSetTimeout(nNanosecondsToWait);
    _LTK_pRunningThread->nTimedOut = 0;
    u32 nMask = LockScheduler();
    if (pThread->nState > kLTKThreadState_Stopped) {
        LTKList_Remove(&_LTK_pRunningThread->runLink);
        LTKList_AddTail(&pThread->stopList, &_LTK_pRunningThread->runLink);
        LTKThreadState nState = kLTKThreadState_WaitForStop;
        if (bWithTimeout) nState = kLTKThreadState_WaitForStopOrTick;
        SetThreadState(_LTK_pRunningThread, nState);
        YieldThread();
    }
    UnlockScheduler(nMask);
    return (_LTK_pRunningThread->nTimedOut == 0);
}

void LTKThreadSetPriority(void * pInstance, u8 nPriority) {
    LTKThread * pThread = (LTKThread *)pInstance;
    u32 nMask = LockScheduler();
    // Snapshot the running thread priority (in case it gets changed)
    u8 nCurrentPriority = 0;
    if (_LTK_pRunningThread != kLTKNoSuchThread)
        nCurrentPriority = _LTK_pRunningThread->nPriority;
    // Change current priority if priority inheritance isn't active
    //  -OR- if new priority is higher than priority inheritance priority
    if (pThread->nPriority == pThread->nNominalPriority || nPriority > pThread->nPriority) {
        pThread->nPriority = nPriority;
        if (pThread->nState == kLTKThreadState_Runnable) {
            LTKList_Remove(&pThread->runLink);
            LTKList_AddTail(&_LTK_runQueues[nPriority], &pThread->runLink);
        }
    }
    // Always change nominal priority
    pThread->nNominalPriority = nPriority;
    // Yield if priority is lowered on currently running thread
    //  -OR- if other thread has a greater priority than running thread
    if (pThread == _LTK_pRunningThread) {
        if (pThread->nPriority < nCurrentPriority) YieldThread();
    } else if (pThread->nPriority > nCurrentPriority) YieldThread();
    UnlockScheduler(nMask);
}

void LTKThreadSleep(s64 nNanosecondsToWait) {
    if (nNanosecondsToWait > 0) {
        _LTKSetTimeout(nNanosecondsToWait);
        u32 nMask = LockScheduler();
        _LTK_pRunningThread->nState = kLTKThreadState_WaitForTick;
        YieldThread();
        UnlockScheduler(nMask);
    } else YieldThread();
}

void LTKThreadGetStackUsage(void * pInstance, u32 * pStackSizeToSet,
                               u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    LTKThread * pThread = (LTKThread *)pInstance;
    u32 nMask = LockScheduler();
    if (pThread->nState == kLTKThreadState_Uninitialized) {
        UnlockScheduler(nMask);
        return;
    }
    if (pThread == _LTK_pRunningThread) {
        pThread->pStack = GetStackPointer();
    }
    _LTKStackCheck(pThread);
    *pStackSizeToSet = pThread->nStackSize;
    u8 * pStackTop = pThread->pStackBottom + pThread->nStackSize;
    *pCurrentStackUsageToSet = pStackTop - pThread->pStack;
    u32 * pCheck = (u32 *)pThread->pStackBottom;
    while (*pCheck++ == kLTKStackFillValue);
    *pMaxStackUsageToSet = pStackTop - (u8 *)pCheck + 4;
    UnlockScheduler(nMask);
    if (*pMaxStackUsageToSet < *pCurrentStackUsageToSet) *pMaxStackUsageToSet = *pCurrentStackUsageToSet;
}

void _LTKStackCheck(LTKThread * pThread) LT_ISR_SAFE {
    if (pThread->pStack < pThread->pStackBottom) {
        LTK_LTLOG_STOMP_REDALERT("stack.overflow", "Thread: %s, SP %08X below bottom %08X, stack size %u",
            pThread->pName, pThread->pStack, pThread->pStackBottom, pThread->nStackSize);
        LTKDebugBreak();
    }
    if (*((u32 *)pThread->pStackBottom) != kLTKStackFillValue) {
        LTK_LTLOG_STOMP_REDALERT("stack.bot.corrupt", "Thread: %s, stack bottom at %08X corrupted",
            pThread->pName, pThread->pStackBottom);
        LTKDebugBreak();
    }
    u8 * pStackTop = pThread->pStackBottom + pThread->nStackSize - sizeof(u32);
    if (*((u32 *)pStackTop) != kLTKStackFillValue) {
        LTK_LTLOG_STOMP_REDALERT("stack.top.corrupt", "Thread: %s, stack top at %08X corrupted", pThread->pName, pStackTop);
        LTKDebugBreak();
    }
}

/*********
 * Mutex */
LT_SIZE LTKMutexInstanceSize(void) {
    return sizeof(LTKMutex);
}

void LTKMutexInitialize(void * pInstance) {
    LTKMutex * pMutex = (LTKMutex *)pInstance;
    pMutex->pOwner = kLTKNoSuchThread;
    pMutex->nDepth = 0;
    LTKList_Init(&pMutex->pendList);
}

void LTKMutexFinalize(void * pInstance) {
    LT_UNUSED(pInstance);
}

LTK_WEAK void LTKMutexLock(void * pInstance) {
    LTKMutex * pMutex = (LTKMutex *)pInstance;
    u32 nMask = LockScheduler();
    if (pMutex->pOwner == (LTKThread *)_LTK_pRunningThread) {
        pMutex->nDepth++;
        UnlockScheduler(nMask);
        return;
    }
    while (pMutex->pOwner != kLTKNoSuchThread) {
        // Add thread to pend queue
        LTKList_Node * pElm = pMutex->pendList.pNext;
        for (; pElm != &pMutex->pendList; pElm = pElm->pNext) {
            LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, runLink);
            if (pThread->nPriority < _LTK_pRunningThread->nPriority) break;
        }
        LTKList_Remove(&_LTK_pRunningThread->runLink);
        LTKList_InsertBefore(pElm, &_LTK_pRunningThread->runLink);
        // Basic priority inheritance
        LTKThread * pThread = (LTKThread *)pMutex->pOwner;
        if (_LTK_pRunningThread->nPriority > pThread->nPriority) {
            pThread->nPriority = _LTK_pRunningThread->nPriority;
            if (pThread->nState == kLTKThreadState_Runnable) {
                LTKList_Remove(&pThread->runLink);
                LTKList_InsertHead(&_LTK_runQueues[pThread->nPriority], &pThread->runLink);
            }
        }
        SetThreadState(_LTK_pRunningThread, kLTKThreadState_WaitForMutex);
        YieldThread();
#if LTKERNEL_STYLE == LTKERNEL_STYLE_1
        /* No need to unlock/relock scheduler */
#elif LTKERNEL_STYLE == LTKERNEL_STYLE_2
        UnlockScheduler(nMask);
        /* Scheduler is invoked here */
        nMask = LockScheduler();
#endif
    }
    // Take it
    pMutex->pOwner = (LTKThread *)_LTK_pRunningThread;
    pMutex->nDepth = 1;
    UnlockScheduler(nMask);
}

LTK_WEAK void LTKMutexUnlock(void * pInstance) {
    LTKMutex * pMutex = (LTKMutex *)pInstance;
    u32 nMask = LockScheduler();
    if (--pMutex->nDepth == 0) {
        pMutex->pOwner = kLTKNoSuchThread;
        if (!LTKList_IsEmpty(&pMutex->pendList)) {
            LTKList_Node * pElm = pMutex->pendList.pNext;
            LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, runLink);
            LTKList_Remove(pElm);
            LTKList_InsertHead(&_LTK_runQueues[pThread->nPriority], pElm);
            if (LTKList_IsNodeLinked(&pThread->timerLink))
                LTKList_Remove(&pThread->timerLink);
            SetThreadState(pThread, kLTKThreadState_Runnable);
            // Reset priority inheritance
            if (_LTK_pRunningThread->nPriority != _LTK_pRunningThread->nNominalPriority) {
                _LTK_pRunningThread->nPriority = _LTK_pRunningThread->nNominalPriority;
                LTKList_Remove(&_LTK_pRunningThread->runLink);
                LTKList_InsertHead(&_LTK_runQueues[_LTK_pRunningThread->nPriority],
                                      &_LTK_pRunningThread->runLink);
            }
            if (pThread->nPriority > _LTK_pRunningThread->nPriority) YieldThread();
        }
    }
    UnlockScheduler(nMask);
}

LTK_WEAK bool LTKMutexTryLock(void * pInstance) {
    LTKMutex * pMutex = (LTKMutex *)pInstance;
    u32 nMask = LockScheduler();
    if (pMutex->pOwner == kLTKNoSuchThread) pMutex->pOwner = (LTKThread *)_LTK_pRunningThread;
    if (pMutex->pOwner == (LTKThread *)_LTK_pRunningThread) {
        pMutex->nDepth++;
        UnlockScheduler(nMask);
        return true;
    }
    UnlockScheduler(nMask);
    return false;
}

/************
 * Monitors */
LT_SIZE LTKMonitorInstanceSize(void) {
    return sizeof(LTKMonitor);
}

void LTKMonitorInitialize(void * pInstance) {
    LTKMonitor * pMonitor = (LTKMonitor *)pInstance;
    pMonitor->nValue = 0;
    pMonitor->pWaiter = kLTKNoSuchThread;
    LTKMutexInitialize(&pMonitor->mutex);
}

void LTKMonitorFinalize(void * pInstance) {
    LT_UNUSED(pInstance);
}

void LTKMonitorEnter(void * pInstance) {
    LTKMonitor * pMonitor = (LTKMonitor *)pInstance;
    LTKMutexLock(&pMonitor->mutex);
}

void LTKMonitorExit(void * pInstance) {
    LTKMonitor * pMonitor = (LTKMonitor *)pInstance;
    LTKMutexUnlock(&pMonitor->mutex);
}

LTK_WEAK void LTKMonitorNotify(void * pInstance) LT_ISR_SAFE {
    LTKMonitor * pMonitor = (LTKMonitor *)pInstance;
    bool inInterrupt = InsideInterruptContext();
    u32 nMask = DisableInterruptsNested();
    /* Set flag in interrupt contexts or if interrupts were disabled at entry */
    if (inInterrupt || !InterruptsGotDisabled(nMask)) pMonitor->nValue = 1;
    // Move waiting thread (if any) to run queue
    LTKThread * pThread = pMonitor->pWaiter;
    if (pThread) {
        pMonitor->pWaiter = kLTKNoSuchThread;
        LTKList_InsertHead(&_LTK_runQueues[pThread->nPriority], &pThread->runLink);
        if (LTKList_IsNodeLinked(&pThread->timerLink))
            LTKList_Remove(&pThread->timerLink);
        SetThreadState(pThread, kLTKThreadState_Runnable);
#if LTKERNEL_STYLE == LTKERNEL_STYLE_1
        // If not in interrupt context, yield if released thread has higher priority
        //   than running thread (but defer if interrupts were disabled at entry).
        if (!inInterrupt && pThread->nPriority > _LTK_pRunningThread->nPriority) {
            if (!InterruptsGotDisabled(nMask)) s_deferredYield = true;
            else YieldThread();
        }
#elif LTKERNEL_STYLE == LTKERNEL_STYLE_2
        if (pThread->nPriority > _LTK_pRunningThread->nPriority) {
            YieldThread();
        }
#endif
    }
    EnableInterruptsNested(nMask);
}

LTK_WEAK bool LTKMonitorWait(void * pInstance, s64 nTimeoutNanoseconds) {
    LTKMonitor * pMonitor = (LTKMonitor *)pInstance;
    bool bWithTimeout = _LTKSetTimeout(nTimeoutNanoseconds);
    // Atomic mutex unlock and wait, ensures thread is waiting before
    // unlocked thread(s) run.
    u32 nMask = DisableInterruptsNested();
    pMonitor->mutex.nDepth = 0;
    pMonitor->mutex.pOwner = kLTKNoSuchThread;
    bool yieldThread = false;
    if (!LTKList_IsEmpty(&pMonitor->mutex.pendList)) {
        LTKMutex * pMutex = &pMonitor->mutex;
        LTKList_Node * pElm = pMutex->pendList.pNext;
        LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, runLink);
        LTKList_Remove(pElm);
        LTKList_InsertHead(&_LTK_runQueues[pThread->nPriority], pElm);
        if (LTKList_IsNodeLinked(&pThread->timerLink))
            LTKList_Remove(&pThread->timerLink);
        SetThreadState(pThread, kLTKThreadState_Runnable);
        // Reset priority inheritance
        if (_LTK_pRunningThread->nPriority != _LTK_pRunningThread->nNominalPriority) {
            _LTK_pRunningThread->nPriority = _LTK_pRunningThread->nNominalPriority;
            LTKList_Remove(&_LTK_pRunningThread->runLink);
            LTKList_InsertHead(&_LTK_runQueues[_LTK_pRunningThread->nPriority],
                                  &_LTK_pRunningThread->runLink);
        }
        yieldThread = (pThread->nPriority > _LTK_pRunningThread->nPriority);
    }
    LT_ASSERT(pMonitor->pWaiter == kLTKNoSuchThread);
    if (pMonitor->nValue == 0) {
        _LTK_pRunningThread->nTimedOut = 0;
        pMonitor->pWaiter = _LTK_pRunningThread;
        LTKList_Remove(&_LTK_pRunningThread->runLink);
        LTKThreadState nState = kLTKThreadState_WaitForMonitor;
        if (bWithTimeout) nState = kLTKThreadState_WaitForMonitorOrTick;
        _LTK_pRunningThread->pMonitor = pMonitor;
        SetThreadState(_LTK_pRunningThread, nState);
        YieldThread();
        EnableInterruptsNested(nMask);
        if (_LTK_pRunningThread->nTimedOut) {
            LTKMutexLock(&pMonitor->mutex);
            return false;
        }
    } else {
        pMonitor->nValue = 0;
        if (yieldThread) YieldThread();
        EnableInterruptsNested(nMask);
    }
    LTKMutexLock(&pMonitor->mutex);
    return true;
}

