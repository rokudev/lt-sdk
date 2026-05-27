/******************************************************************************
 * lt/source/core/LTThreadImpl.c   -  implementation of LTCore thread interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************
 ******************************************************************************
 * CAUTION: Any functions here called by LTLoggerImpl.c MUST NOT log or assert.
 ******************************************************************************
 ******************************************************************************/

#define  LTTHREADIMPL_INTERNAL

#include "LTCoreImpl.h"
#include "LTHandle.h"
#include "LTThreadImpl.h"
#include "LTStdlibImpl.h"
#include "LTCoreDebugHelpers.h"
#include "LTKernel.h"

DEFINE_LTLOG_SECTION("ltthread");

/*******************************************
 * file LTThreadImpl forward declarations */
static void LTThreadImpl_YellowAlert(const char *pTag, LTThread hThread);
static void LTThreadImpl_YellowAlertTargetThreadState(const char *pTag, LTThreadImpl *pTargetThreadImpl);

/*******************************
 * file LTThreadImpl #defines */
#define LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY                   0

#ifdef LT_DEBUG
    #define LTTHREAD_STACK_ARTIFICIAL_INCREASAL_BYTES           (LTTHREAD_STACK_FUDGE_BYTES + LTTHREAD_STACK_LT_DEBUG_INCREASAL_BYTES)
    #define LTTHREAD_LOGSTRUMENTATION                           LTLOG_DEBUG        /* for nice blue color */
#else
    #define LTTHREAD_STACK_ARTIFICIAL_INCREASAL_BYTES           (LTTHREAD_STACK_FUDGE_BYTES)
    #define LTTHREAD_LOGSTRUMENTATION                           LTLOG
#endif /* #ifdef LT_DEBUG
_____*/
#define     LTTHREAD_INSTRUMENT_ISR_POOL                        0   /* turn on and off instrumentation of the ISRTaskProcRecord pool */
#define     LTTHREAD_LOGSTRUMENTATE                             0   /* turn on and off copious thread initiation logging */
#if         LTTHREAD_LOGSTRUMENTATE
    #define LTTHREAD_LOGSTRUMENT                                LTTHREAD_LOGSTRUMENTATION
#else
    #define LTTHREAD_LOGSTRUMENT                                LTLOG_LOGNULL
#endif/* #if LTTHREAD_LOGSTRUMENTATE
____*/



/* this block of #defines assumes hThread is already a local parameter and creates pImpl as a local variable */
#define     LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(function, retVal)  LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread); if (NULL == pImpl) { LTThreadImpl_YellowAlert(#function ".hthreadinvalid", hThread); return retVal; }
#define     LTHREAD_RESERVE_PIMPL_OR_RETURN(function, retVal)                   LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread); if (NULL == pImpl) return retVal;
#define     LTTHREAD_RELEASE_PIMPL()                                            LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    /* NOTE NOTE NOTE NOTE NOTE: LTTHREAD_RESERVE_PIMPL_XXX may be called at any time, e.g.  when inside a monitor, but
       LTTHREAD_RELEASE_PIMPL *must not* be called inside *any* monitor because it can trigger a Destroy to occur, and Destroy must not be called
       with any monitors entered. */

/************************
 * LTThreadImpl.c types */
//     ISRTaskProcRecord
struct ISRTaskProcRecord {
    struct ISRTaskProcRecord        *pNext;
    LTThread_TaskProc               *pTaskProc;
    LTThread_ClientDataReleaseProc  *pClientDataReleaseProc;
    void                            *pClientData;
    bool                             bIfRequired;
};

//     TaskProcRecord
struct TaskProcRecord {
    struct TaskProcRecord           *pNext;
    LTThread_TaskProc               *pTaskProc;
    LTThread_ClientDataReleaseProc  *pClientDataReleaseProc;
    void                            *pClientData;
};

//     ThreadSpecificClientDataRecord
struct ThreadSpecificClientDataRecord {
    struct ThreadSpecificClientDataRecord *pNext;
    void                                  *pClientData;
    LTThread_ClientDataReleaseProc        *pClientDataReleaseProc;
    char                                   key[kLTThread_MaxThreadSpecificIDBuff];
};

enum {
    kLTThreadImpl_TimerFlagsIsAbsolute    = (1 << 0),
    kLTThreadImpl_TimerFlagsIsWakeupTimer = (1 << 1),
};

//     TimerRecord
struct TimerRecord {
    LTTime                           fireTime;
    LTTime                           period;
    struct TimerRecord              *pNext;
    LTThread_TimerProc              *pTimerProc;
    void                            *pClientData;
    LTThread_ClientDataReleaseProc  *pClientDataReleaseProc;
    u32                              nFlags;
};

enum {
//  kLTThreadImpl_MaxInterruptTaskProcRecords               = 24,
//  kLTThreadImpl_ExcessiveQueueTaskProcWarningThreshold    = 10
    kLTThreadImpl_MaxInterruptTaskProcRecords               = 24,
    kLTThreadImpl_ExcessiveQueueTaskProcWarningThreshold    = 48
};

/************************************
 * file LTThreadImpl static variables */
static u32                          s_nNextThreadNumber                     = 1;
static const LTCoreBSP *            s_pBSP                                  = NULL;
static void *                       s_pMonitor                              = 0;
static LTThreadImpl *               s_pThreadsRunningListHead               = NULL;
static struct ISRTaskProcRecord *   s_pISRTaskProcRecordFreePool            = NULL;
static struct ISRTaskProcRecord *   s_pISRTaskProcRecordFreePoolAllocation  = NULL;
static LT_SIZE                      s_threadBSPSize                         = 0;

/***************************************
 * LTThreadImpl.c forward declarations */
static const char *     LTThreadImpl_ThreadStateToString(LTThread_ThreadState threadState);
void                    LTThreadImpl_ThreadMain(void * pClientData);
LTThread_ThreadState    LTThreadImpl_GetThreadState(LTThread hThread);
static const ILTThread  s_ILTThread;

/*******************************************
 * LTThreadImpl.c static utility functions */
LT_INLINE LTThreadImpl *                      LTThreadImpl_PrivateDataToThreadImpl(void *privateData)               { return privateData ? (LTThreadImpl *)(((u8 *)privateData) + s_threadBSPSize) : NULL; }
LT_INLINE void *                              LTThreadImpl_ThreadImplToPrivateData(LTThreadImpl *pImpl)             { return pImpl ? (void *)(((u8 *)pImpl) - s_threadBSPSize) : NULL; }
LT_TEXT_RAM_CRITICAL(1) static LTThreadImpl * LTThreadImpl_ReserveThreadImpl(LTThread hThread)                      { return LTThreadImpl_PrivateDataToThreadImpl(LTHandle_ReserveInterfaceCheckedPrivateData(hThread, (LTInterface *)&s_ILTThread)); }
LT_TEXT_RAM_CRITICAL(1)         static void   LTThreadImpl_ReleaseThreadImpl(LTThread hThread, LTThreadImpl *pImpl) { LTHandle_ReleasePrivateData(hThread, LTThreadImpl_ThreadImplToPrivateData(pImpl)); }
    /* NOTE NOTE NOTE NOTE NOTE: LTThreadImpl_ReserveThreadImpl() may be called when inside the global monitor and/or inside the thread's monitor,
       but LTThreadImpl_ReleaseThreadImpl  *MUST NOT* be called inside *any* monitor because it can trigger a Destroy to occur, and Destroy must not be called  with *any* monitors entered. */

static void LTThreadImpl_YellowAlert(const char *pTag, LTThread hTargetThread) {
    LTThread hCT = LTThreadImpl_GetCurrentThread(); LTThreadImpl *pCTI = LTThreadImpl_ReserveThreadImpl(hCT); const char *pName = pCTI ? pCTI->name : "???";
    LTLOG_YELLOWALERT(pTag, "calling thread = %s, invalid hTargetThread(0x%x)", pName, (int)hTargetThread);
    if (pCTI) LTThreadImpl_ReleaseThreadImpl(hCT, pCTI);
}

static void LTThreadImpl_YellowAlertTargetThreadState(const char *pTag, LTThreadImpl *pTargetThreadImpl) {
    LTThread hCT = LTThreadImpl_GetCurrentThread(); LTThreadImpl *pCTI = LTThreadImpl_ReserveThreadImpl(hCT); const char *pName = pCTI ? pCTI->name : "???";
    LTLOG_YELLOWALERT(pTag, "hCurrentThread = %s, hTargetThread = 0x%x(%s,%s)", pName, (int)pTargetThreadImpl->hThread, pTargetThreadImpl->name,
        LTThreadImpl_ThreadStateToString((pTargetThreadImpl == pCTI) ? kLTThread_ThreadState_Running : (LTThread_ThreadState)(LTAtomic_Load(&pTargetThreadImpl->nThreadState))));
    if (pCTI) LTThreadImpl_ReleaseThreadImpl(hCT, pCTI);
}

static bool LTThreadImpl_InActiveRunState(LTThreadImpl * pImpl) {
    u32 nThreadState = LTAtomic_Load(&pImpl->nThreadState);
    switch (((LTThread_ThreadState)nThreadState)) {
        case kLTThread_ThreadState_InvalidHandle: case kLTThread_ThreadState_NotStarted: case kLTThread_ThreadState_TerminatePending: case kLTThread_ThreadState_Terminated: return false;
        default: return true;
    }
}

bool LTThreadImpl_ThreadHandleInActiveRunState(LTThread hThread) {
    bool bRetVal = false;
    LTThreadImpl *pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        bRetVal = LTThreadImpl_InActiveRunState(pImpl);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    return bRetVal;
}

static bool LTThreadImpl_ClientThreadsAreRunningInternal(void) {
    /* This function must be called with s_pMonitor entered */
    LTThreadImpl * pImpl = s_pThreadsRunningListHead;
    while (pImpl) { if (0 == (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsIsLTCoreSystemThread)) return true; pImpl = pImpl->pNext; }
    return false;
}

static LTThread_ReleaseReason
LTThreadImpl_QueueTaskProcRecordInternal(LTThreadImpl * pImpl, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData, bool bIfRequired) {
    /* This function, having Internal on the end of the name, means it must be called with pImpl->pMonitor entered */
    struct TaskProcRecord * pTaskProcRecord;
    if (bIfRequired) {
        // queuing only if required, check to see if there is a match already in the queue and bail if so
        pTaskProcRecord = pImpl->pTaskProcQueueHead;
        while (pTaskProcRecord) {
            if ((pTaskProcRecord->pTaskProc == pTaskProc) && (pTaskProcRecord->pClientDataReleaseProc == pClientDataReleaseProc) && (pTaskProcRecord->pClientData == pClientData)) {
                return kLTThread_ReleaseReason_NullReason; /* returning NullReason so we don't call the client data release proc */
            }
            pTaskProcRecord = pTaskProcRecord->pNext;
        }
    }
    // we are going to queue - malloc a task proc record
    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc_TaskProcRecord, sizeof(struct TaskProcRecord));
    pTaskProcRecord = (struct TaskProcRecord *)core_malloc(sizeof (*pTaskProcRecord));
    if (pTaskProcRecord) {
        pTaskProcRecord->pTaskProc = pTaskProc;
        pTaskProcRecord->pClientDataReleaseProc = pClientDataReleaseProc;
        pTaskProcRecord->pClientData = pClientData;
        pTaskProcRecord->pNext = NULL;
        if (pImpl->pTaskProcQueueTail) {
            pImpl->pTaskProcQueueTail->pNext = pTaskProcRecord;
            pImpl->pTaskProcQueueTail = pTaskProcRecord;
        }
        else {
            pImpl->pTaskProcQueueHead = pImpl->pTaskProcQueueTail = pTaskProcRecord;
        }
        pImpl->nTaskProcRecordsQueued++;
        if (pImpl->nTaskProcRecordsQueued >= kLTThreadImpl_ExcessiveQueueTaskProcWarningThreshold) {
            LTLOG("excessive.queuetaskprocs", "Thread \'%s\' Proc %p: %d total allocated TaskProcRecords",
                pImpl->name, pTaskProc, (int)pImpl->nTaskProcRecordsQueued);
        }
        return kLTThread_ReleaseReason_TaskProcComplete;
    }
    else {
        LTLOG_YELLOWALERT("taskprocrecord.alloc.failure", "Thread \'%s\' Proc %p: failure with %d total allocated TaskProcRecords",
            pImpl->name, pTaskProc, (int)pImpl->nTaskProcRecordsQueued);
        return kLTThread_ReleaseReason_TaskProcQueueFull;
    }
}

#if 0
static u32 LTThreadImpl_TemporaryIncrementQTPDisableWarningCount(LTThreadImpl *pThreadImpl) {
    u32 nFlagsOld = 0, nFlagsNew = 0;
    if (pThreadImpl) {
        while (1) {
            nFlagsOld = LTAtomic_Load(&pThreadImpl->nThreadFlags);
            nFlagsNew = ((nFlagsOld >> 6) + 1) & 3;
            if (! nFlagsNew) break;
            if (LTAtomic_CompareAndExchange(&pThreadImpl->nThreadFlags, nFlagsOld, ((nFlagsOld & ~(3<<6))) | (nFlagsNew << 6))) break;
        }
        if (nFlagsNew) lt_consoleprint("nFlagsOld = 0x%lx, nFlagsNew = 0x%lx, nFlags = 0x%lx\n", LT_Pu32(nFlagsOld), LT_Pu32(nFlagsNew), LT_Pu32(LTAtomic_Load(&pThreadImpl->nThreadFlags)));
    }
    return nFlagsNew;
}
#endif


static bool LT_ISR_SAFE
LTThreadImpl_QueueTaskProcPrivate(LTThread hThread, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData, bool bIfRequired) {
    if (NULL == pTaskProc && NULL == pClientDataReleaseProc) return false;

    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    bool bInISR = LTKInsideInterruptContext();
    LTThread_ReleaseReason releaseReason = kLTThread_ReleaseReason_TaskProcComplete;
    if (pImpl) {
        if (bInISR || LTKInterruptsAreDisabled()) {
            if (! bInISR) {
#if 0
                LTThread hCurrThread = LTThreadImpl_GetCurrentThread(); LTThreadImpl *pCurrThread = LTThreadImpl_ReserveThreadImpl(hCurrThread);
                if (LTThreadImpl_TemporaryIncrementQTPDisableWarningCount(pCurrThread)) {
                    LTLOG_YELLOWALERT("q.when.disabled", "thread %s called QueueTaskProc() to thread %s with interrupts disabled.  No bueno.", pCurrThread && *pCurrThread->name ? pCurrThread->name : "???", *pImpl->name ? pImpl->name : "???");
                }
#endif
                /* set bInISR to true to pretend to be in ISR so the queue with interrupts disabled will work like a queue from an ISR */
                bInISR = true;
            }
            LT_SIZE nMask = LTKDisableInterrupts();
            if (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsCanPostFromISR) {
                struct ISRTaskProcRecord * pISRTaskProcRecord;
                if (bIfRequired) {
                    // we are queueing 'if required' so if there is a matching record already in the isr list then return without queuing
                    pISRTaskProcRecord = pImpl->pFirstISRTaskProcRecord;
                    while (pISRTaskProcRecord) {
                        if (pISRTaskProcRecord->pTaskProc == pTaskProc && pISRTaskProcRecord->pClientDataReleaseProc == pClientDataReleaseProc && pISRTaskProcRecord->pClientData == pClientData) {
                            // already queued in the ISR Task proc record list; we don't queue and we don't call the release proc
                            LTKEnableInterrupts(nMask);
                            LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
                            return true;
                        }
                        pISRTaskProcRecord = pISRTaskProcRecord->pNext;
                    }
                }
                /* going to queue get a TaskProcRecord from the free list of interrupt TaskProcRecords */
                pISRTaskProcRecord = s_pISRTaskProcRecordFreePool;
                #if LTTHREAD_INSTRUMENT_ISR_POOL
                    u32 nPoolCount = 0;
                    while (pISRTaskProcRecord) { pISRTaskProcRecord = pISRTaskProcRecord->pNext; nPoolCount++; }
                    pISRTaskProcRecord = s_pISRTaskProcRecordFreePool;
                #endif
                if (pISRTaskProcRecord) {
                    /* got a TaskProcRecord, fixup the free list, and put it on pImpl's list */
                    s_pISRTaskProcRecordFreePool = pISRTaskProcRecord->pNext;
                    pISRTaskProcRecord->pNext = pImpl->pFirstISRTaskProcRecord;
                    pImpl->pFirstISRTaskProcRecord = pISRTaskProcRecord;
                    pISRTaskProcRecord->pTaskProc = pTaskProc;
                    pISRTaskProcRecord->pClientDataReleaseProc = pClientDataReleaseProc;
                    pISRTaskProcRecord->pClientData = pClientData;
                    pISRTaskProcRecord->bIfRequired = bIfRequired;
                    LTKMonitorNotify(pImpl->pMonitor);
                    LTKEnableInterrupts(nMask);
                    #if LTTHREAD_INSTRUMENT_ISR_POOL
                       LTLOG("isr.queue.poolcount", "isr-q to thread %s, isr-poolcount %d\n", pImpl->name, (int)nPoolCount);
                       if (nPoolCount <= kLTThreadImpl_MaxInterruptTaskProcRecords / 3) {
                            //LTLOG_YELLOWALERT("queuetaskproc.isr-poolcount.low", "isr-poolcount %d/%d LOW", (int)nPoolCount, (int)kLTThreadImpl_MaxInterruptTaskProcRecords);
                            LTLOG_YELLOWALERT("isr.queue.poolcount.low", "isr-poolcount %d/%d LOW\n", (int)nPoolCount, (int)kLTThreadImpl_MaxInterruptTaskProcRecords);
                        }
                    #endif
                    LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
                    return true;
                }
                else {
                    LTKEnableInterrupts(nMask);
                    LTLOG_YELLOWALERT("isr.queue.poolempty", "isr-q to thread %s failed", pImpl->name);
                    releaseReason = kLTThread_ReleaseReason_TaskProcQueueFull;
                }
            }
            else {
                LTKEnableInterrupts(nMask);
                LTLOG_YELLOWALERT("isr.queue.refuse", "isr-q to thread %s failed, kLTThread_FlagsCanPostFromISR == 0", pImpl->name);
                releaseReason = LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted ? kLTThread_ReleaseReason_ThreadNotStarted : kLTThread_ReleaseReason_ThreadTerminal;
            }
        }
        else {
            /* thread context */
            LTKMonitorEnter(pImpl->pMonitor);
            if (LTThreadImpl_InActiveRunState(pImpl)) {
                releaseReason = LTThreadImpl_QueueTaskProcRecordInternal(pImpl, pTaskProc, pClientDataReleaseProc, pClientData, bIfRequired);
                if (releaseReason == kLTThread_ReleaseReason_TaskProcComplete) LTKMonitorNotify(pImpl->pMonitor);
                if (releaseReason == kLTThread_ReleaseReason_TaskProcComplete || releaseReason == kLTThread_ReleaseReason_NullReason) {
                    /* TaskProcComplete means it was queued, NullReason means it was bIfRequired and already in queue */
                    LTKMonitorExit(pImpl->pMonitor);
                    LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
                    return true;
                }
                /* LTThreadImpl_QueueTaskProcRecordInternal has already yellow alerted and returned pertinent releaseReason for failure */
            }
            else {
                LTThreadImpl_YellowAlertTargetThreadState("queuetaskproc.refused", pImpl);
                releaseReason = LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted ? kLTThread_ReleaseReason_ThreadNotStarted : kLTThread_ReleaseReason_ThreadTerminal;
            }
            LTKMonitorExit(pImpl->pMonitor);
        }
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    else {
        LTThreadImpl_YellowAlert("queuetaskproc.targetThreadinvalid", hThread);
        releaseReason = kLTThread_ReleaseReason_ThreadInvalid;
    }

    /* if we have gotten here then we failed to queue so we need to call the release proc if there is one
       but we'll queue it to the Core thread because we don't want to run it from ISRs and it usually runs
       on the target thread, not the queuing thread; since there is no target thread- Core thread. */
    if (pClientDataReleaseProc) {
        LTHandle hThreadCore   = LTCoreImpl_GetLTCoreImpl()->hThreadCore;
        LTThreadImpl *CoreImpl = LTThreadImpl_ReserveThreadImpl(hThreadCore);
        if (CoreImpl) {
            if (hThread != hThreadCore && (bInISR || (LTThreadImpl_GetCurrentThread() != hThreadCore))) {
                /* if we aren't queuing to the Core thread and we aren't running in the context of the Core thread */
                /* then we'll queue the release to the Core thread */
                /* then we will try to queue the client data release proc to the Core thread */
                if (bInISR) {
                    // in ISR, call ourselves recursively to get on in the ISR queue of the Core thread
                    // we won't infinite recurse because the next time through hThread == hThreadCore and we hit the isr error case below
                    LTThreadImpl_QueueTaskProcPrivate(hThreadCore, NULL, pClientDataReleaseProc, pClientData, false);
                    LTThreadImpl_ReleaseThreadImpl(hThreadCore, CoreImpl);
                    return false;
                }
                LTKMonitorEnter(CoreImpl->pMonitor);
                if (LTThreadImpl_InActiveRunState(CoreImpl)) {
                    if (kLTThread_ReleaseReason_TaskProcComplete == LTThreadImpl_QueueTaskProcRecordInternal(CoreImpl, (void *)((LT_SIZE)releaseReason), pClientDataReleaseProc, pClientData, false)) {
                        LTKMonitorNotify(CoreImpl->pMonitor);
                        LTKMonitorExit(CoreImpl->pMonitor);
                        LTThreadImpl_ReleaseThreadImpl(hThreadCore, CoreImpl);
                        return false;
                    }
                }
                LTKMonitorExit(CoreImpl->pMonitor);
            }
            LTThreadImpl_ReleaseThreadImpl(hThreadCore, CoreImpl);
        }

        /* if we're here we didn't get the release proc posted to the Core thread, call it directly */
        if (bInISR) {
            /* can't call release proc from ISR, the only case where we leak it */
            LTLOG_YELLOWALERT("leak.releaseproc", "isr failed to q release proc after task proc q failed");
        }
        else {
            /* in a thread context, call release proc directly */
            pClientDataReleaseProc(releaseReason, pClientData);
        }
    }
    return false;
}

static void
LTThreadImpl_FillSnapshot(LTThreadImpl * pImpl, LTThread_Snapshot * pSnapshotToFill) {
    /* It is not vitally important that data is coherent within a snapshot, so overall locking is not required */
    pSnapshotToFill->hThread = pImpl->hThread;
    pSnapshotToFill->reserved1 = LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsIsLTCoreSystemThread;
    pSnapshotToFill->runTime.nNanoseconds = LTKThreadGetCpuTime(LTThreadImpl_ThreadImplToPrivateData(pImpl));
    pSnapshotToFill->nThreadNumber = (LTAtomic_Load(&pImpl->nThreadFlags) >> kLTThread_ThreadNumberShift);
    LTKThreadGetStackUsage(LTThreadImpl_ThreadImplToPrivateData(pImpl), &pSnapshotToFill->nStackSize, &pSnapshotToFill->nStackCurrent, &pSnapshotToFill->nStackHiWatermark);
    pSnapshotToFill->nHeapCurrent = LTAtomic_Load(&pImpl->nCurrHeapUsage);
    pSnapshotToFill->nHeapHiWatermark = LTAtomic_Load(&pImpl->nMaxHeapUsage);
    if (0 == pSnapshotToFill->nStackSize) pSnapshotToFill->nStackSize = LTAtomic_Load(&pImpl->nStackSize);
    pSnapshotToFill->nStructureSize = sizeof(*pSnapshotToFill);
    pSnapshotToFill->nThreadState = (LTThreadImpl_GetCurrentThread() == pSnapshotToFill->hThread) ? kLTThread_ThreadState_Running : (u8)(LTAtomic_Load(&pImpl->nThreadState) & 0xFF);
    pSnapshotToFill->nPriority = (u8)(LTAtomic_Load(&pImpl->nPriority) & 0xFF);
    LTStdlibImpl_strncpyTerm(pSnapshotToFill->name, pImpl->name, kLTThread_MaxNameBuff);
}

static void
LTThreadImpl_CalculateFireTimeInternal(struct TimerRecord * pTimerRecord) {
    pTimerRecord->fireTime = LTTime_Add(LTCoreImpl_GetKernelTime(), pTimerRecord->period);
    if (LTTime_IsLessThanOrEqual(pTimerRecord->fireTime, LTTime_Zero())) pTimerRecord->fireTime = LTTime_Infinite(); // wraparound, clamp to infinite
}

static void
LTThreadImpl_ScheduleTimerRecordInternal(LTThreadImpl * pImpl, struct TimerRecord * pTimerRecord) {
    if (NULL == pImpl->pFirstTimerRecord) pImpl->pFirstTimerRecord = pTimerRecord;
    else {
        struct TimerRecord * pPrev = NULL;
        struct TimerRecord * pCurr = pImpl->pFirstTimerRecord;
        while (pCurr && LTTime_IsLessThanOrEqual(pCurr->fireTime, pTimerRecord->fireTime)) {
            pPrev = pCurr;
            pCurr = pCurr->pNext;
        }
        if (NULL == pPrev) {
            pTimerRecord->pNext = pImpl->pFirstTimerRecord;
            pImpl->pFirstTimerRecord = pTimerRecord;
        }
        else {
            pTimerRecord->pNext = pCurr;
            pPrev->pNext = pTimerRecord;
        }
    }
}

static struct TimerRecord *
LTThreadImpl_FindTimerRecordInternal(LTThreadImpl * pImpl, LTThread_TimerProc * pTimerProc, void * pClientData) {
    struct TimerRecord * pCurr = pImpl->pFirstTimerRecord;
    while (pCurr) {
        if (pCurr->pTimerProc == pTimerProc && pCurr->pClientData == pClientData) break;
        pCurr = pCurr->pNext;
    }
    return pCurr;
}

static struct TimerRecord *
LTThreadImpl_UnlinkTimerRecordInternal(LTThreadImpl * pImpl, LTThread_TimerProc * pTimerProc, void * pClientData) {
    struct TimerRecord * pPrev = NULL;
    struct TimerRecord * pCurr = pImpl->pFirstTimerRecord;
    while (pCurr) {
        if (pCurr->pTimerProc == pTimerProc && pCurr->pClientData == pClientData) {
            if (NULL == pPrev) pImpl->pFirstTimerRecord = pCurr->pNext;
            else pPrev->pNext = pCurr->pNext;
            pCurr->pNext = NULL;
            return pCurr;
        }
        pPrev = pCurr;
        pCurr = pCurr->pNext;
    }
    return NULL;
}

static void
LTThreadImpl_ReleaseThreadSpecificClientData(LTThreadImpl * pImpl) {
    struct ThreadSpecificClientDataRecord * pCurr;
    while (pImpl->pThreadSpecificClientDataHead) {
        pCurr = pImpl->pThreadSpecificClientDataHead;
        pImpl->pThreadSpecificClientDataHead = pCurr->pNext;
        if (pCurr->pClientDataReleaseProc) pCurr->pClientDataReleaseProc(kLTThread_ReleaseReason_ThreadSpecificPurge, pCurr->pClientData);
        core_free(pCurr);
    }
}

static void
LTThreadImpl_OnDestroyHandle(LTThread hThread) {
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(ondestroyhandle,)
    if (hThread == LTThreadImpl_GetCurrentThread()) {
        LTLOG_YELLOWALERT("destroy.cantdestroyself", "should never happen - should be caught by LTCore->DestroyHandle()");
        LTTHREAD_RELEASE_PIMPL();
        return;
    }
    LTKMonitorEnter(s_pMonitor); /* s_pMonitor guards run state transitions */
    switch (LTAtomic_Load(&pImpl->nThreadState)) {
        case kLTThread_ThreadState_NotStarted:
            LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_Terminated);
            LTKMonitorExit(s_pMonitor);
            break;
        case kLTThread_ThreadState_Terminated:
            LTKMonitorExit(s_pMonitor);
            break;
        default:
            LTKMonitorEnter(pImpl->pMonitor); /* always enter pImpl->pMonitor second when entering both */
            if ((u32)kLTThread_ThreadState_TerminatePending != LTAtomic_Load(&pImpl->nThreadState)) {
                LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~kLTThread_FlagsCanPostFromISR);
                LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_TerminatePending);
                LTKMonitorNotify(pImpl->pMonitor);
            }
            LTKMonitorExit(pImpl->pMonitor);
            LTKMonitorExit(s_pMonitor);
            LTThreadImpl_WaitUntilFinished(hThread, LTTime_Infinite());
            break;
    }
    LTThreadImpl_ReleaseThreadSpecificClientData(pImpl);
    LTKThreadFinalize(LTThreadImpl_ThreadImplToPrivateData(pImpl));
    LT_ASSERT(LTAtomic_Load(&pImpl->nThreadState) == (u32)kLTThread_ThreadState_Terminated);
    LT_ASSERT(pImpl->nTaskProcRecordsQueued == 0);
    LT_ASSERT(pImpl->pTaskProcQueueHead == NULL);
    LT_ASSERT(pImpl->pTaskProcQueueTail == NULL);
    if (pImpl->pMonitor) {
        LTKMonitorFinalize(pImpl->pMonitor);
        core_free(pImpl->pMonitor);
        pImpl->pMonitor = NULL;
    }
    LTTHREAD_RELEASE_PIMPL();
}

/********************************************************************
 * LTCore Library Private LTThreadImpl non-static utility functions */
void LTThreadImpl_AcceptBSP(const LTCoreBSP * pBSP) {
    s_pBSP = pBSP;
}

void LTThreadImpl_RelinquishBSP(void) {
    s_pBSP = NULL;
}

void
LTThreadImpl_Init(void) {
    s_pMonitor = core_malloc(LTKMonitorInstanceSize());
    LTKMonitorInitialize(s_pMonitor);
    // allocate the interrupt thread TaskProcRecord pool, point the free list head to the pool and link the TaskProcRecords together
    s_pISRTaskProcRecordFreePoolAllocation = (struct ISRTaskProcRecord *)core_malloc(kLTThreadImpl_MaxInterruptTaskProcRecords * sizeof(struct ISRTaskProcRecord));
    s_pISRTaskProcRecordFreePool = s_pISRTaskProcRecordFreePoolAllocation;
    for (u32 i = 0; i < ((u32)kLTThreadImpl_MaxInterruptTaskProcRecords - 1); i++) s_pISRTaskProcRecordFreePool[i].pNext = &s_pISRTaskProcRecordFreePool[i+1];
    s_pISRTaskProcRecordFreePool[kLTThreadImpl_MaxInterruptTaskProcRecords - 1].pNext = NULL;
}

void
LTThreadImpl_Fini(void) {
    /* restore static variables to initial state */
    LTKMonitorFinalize(s_pMonitor); core_free(s_pMonitor); s_pMonitor = NULL;
    core_free(s_pISRTaskProcRecordFreePoolAllocation);
    s_pISRTaskProcRecordFreePool = s_pISRTaskProcRecordFreePoolAllocation = NULL;
    s_nNextThreadNumber = 1;
    s_pThreadsRunningListHead = NULL;
}

void
LTThreadImpl_IncreaseHeapAllocatedByteCount(LTThread hThread, u32 nBytes) {
    LTThreadImpl * pImpl = hThread ? LTThreadImpl_ReserveThreadImpl(hThread) : 0;
    if (pImpl) {
        nBytes = LTAtomic_FetchAdd(&pImpl->nCurrHeapUsage, nBytes) + nBytes;
        u32 nMaxUsage = LTAtomic_Load(&pImpl->nMaxHeapUsage);
        while ((nBytes > nMaxUsage) && (! (LTAtomic_CompareAndExchange(&pImpl->nMaxHeapUsage, nMaxUsage, nBytes)))) nMaxUsage = LTAtomic_Load(&pImpl->nMaxHeapUsage);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

void
LTThreadImpl_ReduceHeapAllocatedByteCount(LTThread hThread, u32 nBytes) {
    LTThreadImpl * pImpl = hThread ? LTThreadImpl_ReserveThreadImpl(hThread) : 0;
    if (pImpl) {
        // don't underflow pImpl->nCurrHeapUsage
        while (1) {
            u32 bytesCurrent = LTAtomic_Load(&pImpl->nCurrHeapUsage);
            u32 bytesNew = (bytesCurrent > nBytes) ? bytesCurrent - nBytes : 0;
            if (bytesCurrent == bytesNew) break;
            if (LTAtomic_CompareAndExchange(&pImpl->nCurrHeapUsage, bytesCurrent, bytesNew)) break;
        }
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

LT_SIZE
LTThreadImpl_GetThreadBSPSize(void) {
    return s_threadBSPSize;
}

void
LTThreadImpl_WaitForAllThreadsToFinish(void) {
    LTKMonitorEnter(s_pMonitor);
    while (LTThreadImpl_ClientThreadsAreRunningInternal()) {
        LTKMonitorWait(s_pMonitor, LTTIME_INFINITE);
        continue;
    }
    LTKMonitorExit(s_pMonitor);
}

static void
LTThreadImpl_TerminateImplInternal(LTThreadImpl * pImpl) {
    /* must be called with s_pMonitor entered */
    LTThread_ThreadState threadState = LTAtomic_Load(&pImpl->nThreadState);
    if (threadState == kLTThread_ThreadState_TerminatePending || threadState == kLTThread_ThreadState_Terminated) return;

    LTKMonitorEnter(pImpl->pMonitor);
    threadState = LTAtomic_Load(&pImpl->nThreadState);
    while (threadState != kLTThread_ThreadState_TerminatePending && threadState != kLTThread_ThreadState_Terminated) {
        LTThread_ThreadState newThreadState = LTThreadImpl_InActiveRunState(pImpl) ? kLTThread_ThreadState_TerminatePending
                : ((threadState == kLTThread_ThreadState_NotStarted) ? kLTThread_ThreadState_Terminated : kLTThread_ThreadState_InvalidHandle);
        if (newThreadState != kLTThread_ThreadState_InvalidHandle) {
            if (LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, newThreadState)) {
                LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~kLTThread_FlagsCanPostFromISR);
                LTKMonitorNotify(pImpl->pMonitor);
                break;
            }
        }
        threadState = LTAtomic_Load(&pImpl->nThreadState);
    }
    LTKMonitorExit(pImpl->pMonitor);
}

void
LTThreadImpl_TerminateAllNonSystemThreads(void) {
    LTKMonitorEnter(s_pMonitor);
    LTThreadImpl * pImpl = s_pThreadsRunningListHead;
    while (pImpl) {
        if (0 == (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsIsLTCoreSystemThread)) LTThreadImpl_TerminateImplInternal(pImpl);
        pImpl = pImpl->pNext;
    }
    LTKMonitorExit(s_pMonitor);
}

void
LTThreadImpl_SetThreadFlags(LTThread hThread, u32 nFlags) {
    /* no thread safety required, only works on non started threads; take care to do no logging; that means no asserting or alerting!! */
    /* don't let the init flags be set */
    nFlags &= ~kLTThread_FlagsThreadInitComplete;
    nFlags &= ~kLTThread_FlagsThreadInitResult;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted) {
            LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~((u32)kLTThread_ThreadFlagsMask));
            LTAtomic_FetchOr(&pImpl->nThreadFlags, (nFlags & ((u32)kLTThread_ThreadFlagsMask)));
        }
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

u32 LTThreadImpl_IngressBlockingThreadState(u32 blockingThreadState) {
    LTThread     hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        u32 threadState = LTAtomic_Load(&pImpl->nThreadState);
        while (threadState != kLTThread_ThreadState_TerminatePending && threadState != kLTThread_ThreadState_Terminated && threadState != kLTThread_ThreadState_NotStarted) {
            if (LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, blockingThreadState)) break;
            threadState = LTAtomic_Load(&pImpl->nThreadState);
        }
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
        return threadState;
    }
    else return kLTThread_ThreadState_InvalidHandle;
}

void LTThreadImpl_EgressBlockingThreadState(u32 blockingThreadState, u32 priorThreadState) {

    switch (priorThreadState) {
        case kLTThread_ThreadState_InvalidHandle:
        case kLTThread_ThreadState_NotStarted:
        case kLTThread_ThreadState_TerminatePending:
        case kLTThread_ThreadState_Terminated:          return;
        default:                                        break;
    }

    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        LTAtomic_CompareAndExchange(&pImpl->nThreadState, blockingThreadState, priorThreadState);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

LTThread_ThreadState
LTThreadImpl_GetCurrentThreadState(void) {
    /* this is the only getter that doesn't remap kLTThread_ThreadState_ReadyToRun; for internal LTCore use only */
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    LTThread_ThreadState retVal = pImpl ? LTAtomic_Load(&pImpl->nThreadState) : kLTThread_ThreadState_InvalidHandle;
    LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    return retVal;
}

void
LTThreadImpl_SetCurrentThreadState(LTThread_ThreadState threadState) {
    /* atomic, no safety required; never set running, only ready to run, getters knows what to do */
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
#if LTTHREAD_LOGSTRUMENTATE
    if (pImpl) {
        LTThread_ThreadState threadStateOld = LTAtomic_Load(&pImpl->nThreadState);
        LTAtomic_Store(&pImpl->nThreadState, (kLTThread_ThreadState_Running == threadState) ? (u32)kLTThread_ThreadState_ReadyToRun : (u32)threadState);
        if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_ReadyToRun && threadStateOld == kLTThread_ThreadState_NotStarted) {
            LTTHREAD_LOGSTRUMENT("SetCurrentThreadState", "Thread %s changed its thread state from %s to %s, on request for %s\n",
              pImpl->name, LTThreadImpl_ThreadStateToString(threadStateOld), LTThreadImpl_ThreadStateToString(LTAtomic_Load(&pImpl->nThreadState)), LTThreadImpl_ThreadStateToString(threadState));
        }
#else
    if (pImpl) {
        LTAtomic_Store(&pImpl->nThreadState, (kLTThread_ThreadState_Running == threadState) ? (u32)kLTThread_ThreadState_ReadyToRun : (u32)threadState);

#endif
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

LT_INLINE bool LTThreadImpl_IsFlagSet(LTThread hThread, u32 nFlag) {
    bool bRetVal = false;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        bRetVal = LTAtomic_Load(&pImpl->nThreadFlags) & nFlag;
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    return bRetVal;
}

LT_INLINE void LTThreadImpl_SetFlagBit(LTThread hThread, u32 nFlag, bool bSet) {
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        if (bSet) LTAtomic_FetchOr(&pImpl->nThreadFlags, nFlag);
        else LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~nFlag);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

LT_INLINE bool LTThreadImpl_FetchSetFlagBit(LTThread hThread, u32 nFlag, bool bSet) {
    u32 nFlags = 0;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        nFlags = bSet ? LTAtomic_FetchOr(&pImpl->nThreadFlags, nFlag) : LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~nFlag);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    return nFlags & nFlag;
}

bool
LTThreadImpl_IsUnregisterCurrentEventFlagSet(void) {
    return LTThreadImpl_IsFlagSet(LTThreadImpl_GetCurrentThread(), kLTThread_FlagsUnregisterCurrentEvent);
}

void
LTThreadImpl_SetUnregisterCurrentEventFlag(bool bSet) {
    LTThreadImpl_SetFlagBit(LTThreadImpl_GetCurrentThread(), kLTThread_FlagsUnregisterCurrentEvent, bSet);
}

bool
LTThreadImpl_IsProhibitLoggingFlagSet(void) {
    return LTThreadImpl_IsFlagSet(LTThreadImpl_GetCurrentThread(), kLTThread_FlagsProhibitLogging);
}

void
LTThreadImpl_SetProhibitLoggingFlag(bool bSet) {
    LTThreadImpl_SetFlagBit(LTThreadImpl_GetCurrentThread(), kLTThread_FlagsProhibitLogging, bSet);
}

bool
LTThreadImpl_FetchSetProhibitLoggingFlag(bool bSet) {
    return LTThreadImpl_FetchSetFlagBit(LTThreadImpl_GetCurrentThread(), kLTThread_FlagsProhibitLogging, bSet);
}

static const LTOThreadApi s_LTOThreadImplApi;

static void
LTThreadImpl_SetupThreadImpl(LTThreadImpl *pImpl, LTThread hThread, const char *pName, u32 nStackSize, u32 nPriority) {
    LTStdlibImpl_memset(pImpl, 0, sizeof(*pImpl));
    pImpl->hThread = hThread;
    LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_NotStarted);
    LTAtomic_Store(&pImpl->nStackSize, nStackSize);
    LTAtomic_Store(&pImpl->nPriority, nPriority);
    if (NULL == pName || 0 == *pName) pName = "<unnamed>";
    LTStdlibImpl_strncpyTerm(pImpl->name, pName, sizeof(pImpl->name));
    LTStdlibImpl_memset(LTThreadImpl_ThreadImplToPrivateData(pImpl), 0, s_threadBSPSize);
    pImpl->threadObject.API = &s_LTOThreadImplApi;
    *((u32 *)(&pImpl->threadObject.reserved)) = kLTObjectFlags_PseudoObject;
    pImpl->threadObject.hThread = hThread;
    pImpl->pThreadObject = (LTOThread *)&pImpl->threadObject;
    pImpl->pMonitor = core_malloc(LTKMonitorInstanceSize());
    if (pImpl->pMonitor) LTKMonitorInitialize(pImpl->pMonitor);
}

LTThread
LTThreadImpl_CreateCoreThread(const char *pName, u32 nStackSize, u32 nPriority) {
    s_threadBSPSize = LTKThreadInstanceSize();
    s_threadBSPSize = (s_threadBSPSize + 0x7) & ~0x7;
    LT_ASSERT(s_threadBSPSize != 0);
    LTThread hThreadCore = LTHandle_CreateHandle((LTInterface *)&s_ILTThread, s_threadBSPSize + sizeof(LTThreadImpl));
    LTThreadImpl *pImpl = LTThreadImpl_ReserveThreadImpl(hThreadCore);
    if (pImpl) {
        LTThreadImpl_SetupThreadImpl(pImpl, hThreadCore, pName, nStackSize + LTTHREAD_STACK_ARTIFICIAL_INCREASAL_BYTES, nPriority);
        LT_ASSERT(pImpl->pMonitor);
        LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_ReadyToRun);
        LTAtomic_Store(&pImpl->nThreadFlags, ((LTAtomic_Load(&pImpl->nThreadFlags) & ~kLTThread_FlagsThreadInitComplete) &
                  kLTThread_ThreadFlagsMask) | kLTThread_FlagsIsLTCoreSystemThread | kLTThread_FlagsSystemThreadIsTimeCritical);
        s_nNextThreadNumber++;
        s_pThreadsRunningListHead = pImpl;
        LTThreadImpl_ReleaseThreadImpl(hThreadCore, pImpl);
    }
    return hThreadCore;
}

#if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY

    #if 0
        // copy this into LTShellCommandsImpl.c
        void LTThread_Dumper(bool bOn);

        static int
        ShellCommand_Dumper(LTShell hShell, int argc, const char **argv) {
            ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
            if (argc != 2) goto usage;
            bool bOn = false;
            if (0 == lt_strcmp(argv[1], "on")) bOn = true;
            else if (0 == lt_strcmp(argv[1], "off")) bOn = false;
            else goto usage;

            LTThread_Dumper(bOn);
            iShell->Print(hShell, "dumper: dumping is %s\n", argv[1]);
            return 0;

        usage:
            iShell->Print(hShell, "usage: dumper <on|off>\n");
            return 0;
        }
    #endif

static void DumpPimple(LTThreadImpl *pImpl) {
    lt_consoleprint("0x%p(%s), pPrev=0x%p(%s), pNext = 0x%p(%s), WakeupTimer(%s)\n",
        pImpl, pImpl->name,
        pImpl->pPrev, pImpl->pPrev ? pImpl->pPrev->name : "NULL",
        pImpl->pNext, pImpl->pNext ? pImpl->pNext->name : "NULL",
        (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer) ? "YES" : "NO");
}
static LTAtomic s_dumper = { 0 };

void LTThread_Dumper(bool bOn) {
    LTAtomic_Store(&s_dumper, bOn ? 1 : 0);
}

static void DumpThreadListInternal(const char * why, LTThreadImpl *pImpl) {
    if (0 == LTAtomic_Load(&s_dumper)) return;
    lt_consoleprint("\n_____________\nDumpThreadList: %s\n", why);
    lt_consoleprint("   pImpl = ");
    DumpPimple(pImpl);
    lt_consoleprint("ThreadList\n");
    LTThreadImpl * pCurr = s_pThreadsRunningListHead;
    int i = 0;
    while (pCurr) {
        lt_consoleprint("Thread %d = ", i);
        DumpPimple(pCurr);
        pCurr = pCurr->pNext;
        i++;
    }
    lt_consoleprint("\nDumpThreadList: %s complete\n", why);
}

static void
LTThreadImpl_RelinkWakeupTimerThreadInternal(const char *why, LTThreadImpl *pImpl) {
    char reason[24];
    lt_snprintf(reason, sizeof(reason), "%s 1", why);
    DumpThreadListInternal(reason, pImpl);
#else
static void
LTThreadImpl_RelinkWakeupTimerThreadInternal(LTThreadImpl *pImpl) {
#endif
    /* this version just links a wakeup timer thread at the head and a non wakeup timer thread after the last wakeup timer thread */
    /* first unlink the thread */
    if (pImpl == s_pThreadsRunningListHead) {
        s_pThreadsRunningListHead = pImpl->pNext;
        if (s_pThreadsRunningListHead) s_pThreadsRunningListHead->pPrev = NULL;
        pImpl->pNext = NULL;
    }
    else if (pImpl->pPrev) {
        pImpl->pPrev->pNext = pImpl->pNext;
        if (pImpl->pNext) {
            pImpl->pNext->pPrev = pImpl->pPrev;
            pImpl->pNext = NULL;
        }
        pImpl->pPrev = NULL;
    }
    else {
        LT_ASSERT(pImpl->pPrev == NULL);
        LT_ASSERT(pImpl->pNext == NULL);
    }

#if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
    lt_snprintf(reason, sizeof(reason), "%s 2", why);
    DumpThreadListInternal(reason, pImpl);
#endif

    /* figure out where it should go now */
    LTThreadImpl *pLinkBefore = s_pThreadsRunningListHead;
    LTThreadImpl *pLinkAfter = NULL;
    if (0 == (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer)) {
        /* thread [no longer] has wakeup timer; link it before the first thread without a wakeup timer */
        while (pLinkBefore) {
            if (0 == (LTAtomic_Load(&pLinkBefore->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer)) break;
            pLinkAfter = pLinkBefore;
            pLinkBefore = pLinkBefore->pNext;
        }
    }

    /* link it into the new place */
    if (pLinkBefore) {
        if (pLinkBefore->pPrev == NULL) {
            LT_ASSERT(pLinkBefore == s_pThreadsRunningListHead);
            s_pThreadsRunningListHead->pPrev = pImpl;
            pImpl->pNext = s_pThreadsRunningListHead;
            s_pThreadsRunningListHead = pImpl;
        }
        else {
            pImpl->pNext = pLinkBefore;
            pImpl->pPrev = pLinkBefore->pPrev;
            pLinkBefore->pPrev->pNext = pImpl;
            pLinkBefore->pPrev = pImpl;
        }
    }
    else if (pLinkAfter) {
        pImpl->pPrev = pLinkAfter;
        pImpl->pNext = pLinkAfter->pNext;
        pLinkAfter->pNext = pImpl;
    }
    else {
        LT_ASSERT(s_pThreadsRunningListHead == NULL);
        s_pThreadsRunningListHead = pImpl;
    }

#if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
    lt_snprintf(reason, sizeof(reason), "%s 3", why);
    DumpThreadListInternal(reason, pImpl);
#endif
}

#if 0
static LTTime
LTThreadImpl_GetThreadWakeupFireTimeInternal(LTThreadImpl *pImpl) {
    struct TimerRecord * pTimerRecord = pImpl->pFirstTimerRecord;
    while (pTimerRecord) {
        if (pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsWakeupTimer) return pTimerRecord->fireTime;
        pTimerRecord = pTimerRecord->pNext;
    }
    return LTTime_Zero();
}


/* This implements insertion sort of thread records ordered by thread soonest firing timer time.  We're not doing this, but I keep it here for
   possible ressurrection at a later date.  See massive comment below in function LTThreadImpl_GetSoonestFiringThreadWakeupTimerFireTime() */

static void
LTThreadImpl_RelinkWakeupTimerThreadInternal(LTThreadImpl *pImpl) {
    /* first unlink the thread */
    if (pImpl == s_pThreadsRunningListHead) {
        s_pThreadsRunningListHead = pImpl->pNext;
        if (pImpl->pNext) pImpl->pNext->pPrev = NULL;
        pImpl->pNext = NULL;
    }
    else {
        pImpl->pPrev->pNext = pImpl->pNext;
        if (pImpl->pNext) pImpl->pNext->pPrev = pImpl->pPrev;
        pImpl->pPrev = NULL;
        pImpl->pNext = NULL;
    }

    /* now link it in */
    LTThreadImpl *pLinkBefore = s_pThreadsRunningListHead;
    LTThreadImpl *pLinkAfter = NULL;
    if (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer) {
        /* thread has wakeup timer; link it before the first thread without a wakeup timer
           or before the first thread with a wakeup timer whose fire time is > this one's */
        LTTime fireTime = LTThreadImpl_GetThreadWakeupFireTimeInternal(pImpl);
        while (pLinkBefore) {
            if (0 == (LTAtomic_Load(&pLinkBefore->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer)) break;
            LTKMonitorEnter(pLinkBefore->pMonitor);
            if (LTTime_IsLessThanOrEqual(fireTime, LTThreadImpl_GetThreadWakeupFireTimeInternal(pLinkBefore))) {
                LTKMonitorExit(pLinkBefore->pMonitor);
                break;
            }
            LTKMonitorExit(pLinkBefore->pMonitor);
            pLinkAfter = pLinkBefore;
            pLinkBefore = pLinkBefore->pNext;
        }
    }
    else {
        /* thread [no longer] has wakeup timer; link it before the first thread without a wakeup timer */
        while (pLinkBefore) {
            if (0 == (LTAtomic_Load(&pLinkBefore->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer)) break;
            pLinkAfter = pLinkBefore;
            pLinkBefore = pLinkBefore->pNext;
        }
    }

    if (pLinkBefore) {
        if (pLinkBefore->pPrev == NULL) {
            LT_ASSERT(pLinkBefore == s_pThreadsRunningListHead);
            s_pThreadsRunningListHead->pPrev = pImpl;
            pImpl->pNext = s_pThreadsRunningListHead;
            s_pThreadsRunningListHead = pImpl;
        }
        else {
            pImpl->pNext = pLinkBefore;
            pImpl->pPrev = pLinkBefore->pPrev;
            pLinkBefore->pPrev = pImpl;
        }
    }
    else if (pLinkAfter) {
        pImpl->pPrev = pLinkAfter;
        pImpl->pNext = pLinkAfter->pNext;
        pLinkAfter->pNext = pImpl;
    }
    else {
        LT_ASSERT(s_pThreadsRunningListHead == NULL);
        s_pThreadsRunningListHead = pImpl;
    }
}
#endif

LTTime
LTThreadImpl_GetSoonestFiringThreadWakeupTimerFireTime(void) {
    /* To find the soonest firing wakeup timer across all threads in the sysetem we iterate through all threads that have a flag set indicating the thread
       has a software wakeup timer. We have previously grouped all such threads together at the beginning of the list so that we can efficiently stop iterating
       through the thread list once we encounter the first thread record without the flag set.  While this optimizes for much fewer iterations over thread record
       list nodes, and limits the examination of those records, this is still is an O(n) algorithm with n being the number of threads that have software wakeup
       timers set.  This number is unbounded in principle; every thread could set a software wakeup timer.  In principle, however, the number of threads with
       software wakeup timers is expected to practically amount to between 0 and 3 for any given product instantiation.   This is because the calculation and
       specification of software required wakeup from sleep for any particular software subsystem via an alarm style timer is thought to be of little utility
       relative to the more dynamic and agile mechanism of a software subsystem registering an LTCore SleepAction event handler and returning any required sleep
       wakeup time from their event handler in response to receiving notification of the GoingToSleep sleep action.

       Despite the low number of anticipated sleep wakeup timers and lower anticipated number of threads on which those timers are set, the fact remains this
       O(n) algorithm could one day create problems on systems with thousands of threads that all have set wakeup timers.

       This entire missive is written to explain why an extremely simple O(1) solution which was identified an implemented was replaced with the O(n) solution
       implemented in this function.  The O(1) solution is a simple addition on top of grouping the thread records, namely to also *order* the threads in the group by
       thread soonest firing wakeup timer time.  It's trivial to implement sorting of the thread list using an insertion sort whenever the list is modified.
       Effectively an insertion sort after remove in SetTimer, KillTimer, and RestartTimer.

       The problem with the list manipulation arises with the increased locking of the global thread monitor (s_pMonitor in this file) to protect the thread list
       while it is being reordered.  This monitor not only guards the thread list but also all thread state transitions (start->started->terminate_requested->terminated).
       Holding the global monitor for long periods of time, or increasing contention over it could affect the performance of thread state transitions.
       Previously the thread list was never ordered and the only time it was modified was when a thread started and when a thread terminated.  As a doubly linked list,
       the insertion and removal of thread records into the list is O(1).  Adding ordering to the list adds [up to] O(n) operations to all list insertion, removal,
       and now timer scheduling operaitons.   SetTimer, KillTimer, and RestartTimer are not called with great frequency so it is unlikely any noticicable change
       will be observed due to the infrequent added contention over the thread global monitor.

       The contention problem comes in when we realize that with an ordered list of thread records, the rescheduling of any individual wakeup timer can cause the ordered
       list to lose its ordering.  Consider Thread A having a wakeup timer firing every 5000ms and Thread B having a wakeup timer firing every 7500ms.  Let's assume
       Thread A and Thread B magically were both able to set these timers exactly when kerneltime was 1000.  This means Thread A's timer will file at kernel time 6000 and
       Thread B's timer will fire at kernel time 8500.  When Thread A's timer fires at 6000, it will be removed from Thread A's timer record list, have it's fire time
       recalculated to 11,000, rescheduled into Thread A's timer list but now in the ordered thread list Thread A, whose wakeup timer just got rescheduled to be farther
       out thatn Thread B's wakeup timer, must be moved in the thread record list to after Thread B.   So before the timer fired, the thread List looked like this:
       Thread A->Thread B, and after the timer fired the list must be reordered to be Thread B->Thread A.  So it would seem now the activity of every thread operating
       its timer now has the potential to wreak havoc on the thread list ordering while creating contention over the global mutex in a non-deterministic manner that
       was not part of  its original design.   But it gets worse.   Each thread has a monitor that protects its thread instance data, such as its timer list.  To avoid
       deadlocking, whenever both monitors are entered, the global monitor must be entered first.  In the thread loop, only a thread's monitor is entered (and then exited
       before calling task procs and re-entered after, same for timers).  Now we can't know a-priori what the wakeup timer effect is going to be without first entering
       the global monitor around the entire timer dispatch Enter Enter Exit Exit,  for each timer that fires in each loop iteration.  And, when there is a list ordering check
       the thread does Enter Enter (for each other thread not me, enter his, exit his) Exit Exit.  Now, imagine all threads doing this when executing their own timers,
       not only contending for and interfering with thread startup and shutdown state sequencing and list maintainence but also contending against each other for
       execution of their own timers.

       So, NO ORDERING OF THE THREAD LIST BY TIMER FIRE TIME.
       Instead, KEEP THE GLOBAL MONITOR LOCKED LONGER IN THIS ONE SEARCH FUNCTION, while we do our O(n) search.

       **  THIS IS NOT ANTICIPATED TO BE A PROBLEM.  WE'RE GOING TO SLEEP WHEN THIS PROCEDURE IS CALLED.  NO ONE ELSE IS AROUND TO TRY AND TAKE THE GLOBAL MONITOR ***

       But let's keep an eye out if sleep starts getting sluggish because of this algorithm which paws through the timer lists of every thread that has a wakeup timer
       set every time we go to sleep.  (Again this isn't really a problem, unless someone decides to create a hundred threads with a hundred timers, each thread having
       at least 1 wakeup timer.   (Then this algorithm would paw through 100 threads looking for that 1 wakeup timer in each of them).

       IN SUMMARY:
       a. If you're ever in this code, always enter s_pMonitor before pThreadImpl->pMonitor, and not the other way around or deadlocks will result
       b. Don't mess with s_pMonitor
       c. O(n) won't get ugly for the forseeable future.  On Ranger our O(n) algorithm that executes before every sleep amounts to about 6 total node traversals.  The O(1) would have 3 traversals.
               O(n) 6 node traversals assumed here is that Ranger ends up using 2 sleep wakeup timers from 2 different threads.  Further it is assumed these threads will each have 3 timers overall (2 regular, 1 wakeup)).
               O(1) 3 node traversals is checking the head of the thread list, and then checking the head thread's timer list.
       d. LT doesn't limit the amount of threads or the amount of wakeup timers so in a future world on a product with mega memory O(n) and O(1) could look like this:
               O(n) 10000 threads each have 1000 timers, one of which is a wakeup timer.                  There are 10,000 * 1,000  node iterations or 10,000,000 node iterations.
               O(1) 10000 threads each have 1000 timers, one of which is a wakeup timer.                  There are      1 * 1,000* node iterations or      1,000 node iterations.
               O(1) 100000000000000000000 threads each have 1000 timers, one of which is a wakeup timer.  There are      1 * 1,000* node iterations or      1,000 node iterations.

               * Note the 1000 multiplier is the number of timer nodes.
                  The O(1) would actually vary based on where the wakeup timer is in the list of 1000 timers.  If it's first in the list (firing soonest) will be 1*1    or 1 node iteration.
                                                                                                               If it's the 500th node in the list there will be   1*500  or 500 node iteratons.
                  O notation is always worst case so O*(1) is always 1*1000.
       e. No action here - everything just fine.  Just don't create 100000000000000000000 threads, each with 1000 timers, one a wakeup timer.
    */

    /* ok, let's find our soonest firing timer time */

    LTTime fireTime = LTTime_Infinite();
    LTKMonitorEnter(s_pMonitor);
    LTThreadImpl * pImpl = s_pThreadsRunningListHead;
    while (pImpl && (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsHasSofwareWakeupTimer)) {
        /* we have a thread that has one or more wakeup timers, find the first (soonest firing) wakeup timer in the thread's timer list  */
        LTKMonitorEnter(pImpl->pMonitor);
        struct TimerRecord * pTimerRecord = pImpl->pFirstTimerRecord;
        while (pTimerRecord) {
            if (pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsWakeupTimer) {
                /* found the first (soonest firing) wakeup timer for this thread.  Cache it if it is soonest so far. */
                if (LTTime_IsLessThan(pTimerRecord->fireTime, fireTime)) fireTime = pTimerRecord->fireTime;
                break;
            }
            pTimerRecord = pTimerRecord->pNext;
        }
        LTKMonitorExit(pImpl->pMonitor);
        pImpl = pImpl->pNext;
    }
    LTKMonitorExit(s_pMonitor);
    return LTTime_IsInfinite(fireTime) ? LTTime_Zero() : fireTime;
}

/**********************************************************
 * LTThreadImpl functions for the LTCore Public Interface */
LTThread
LTThreadImpl_CreateThread(const char * pName) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTThread hThread = (LTThread)LTHandle_CreateHandle((LTInterface *)&s_ILTThread, s_threadBSPSize + sizeof(LTThreadImpl));
    LTThreadImpl *pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        LTThreadImpl_SetupThreadImpl(pImpl, hThread, pName, LTKThreadGetDefaultStackSize(), kLTThread_PriorityDefault);
        if (!pImpl->pMonitor) {
            LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
            LTHandle_DestroyHandle(hThread);
            hThread = 0;
        } else {
            LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
        }
    }
    return hThread;
}

/*  ___________________________________
 *  LTTHREAD PUBLIC INTERFACE FUNCTIONS
 *
 *  ________________________________________________________
 *  ________________________________________________________
 *  Functions that operate on the specified hThread argument
 *  ________________
 *  Thread Execution - Start() and QueueTaskProc() */
void
LTThreadImpl_Start(LTThread hThread, LTThread_InitProc * pInitProc, LTThread_ExitProc * pExitProc) {
    /* this function is intentionally not a static function */
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(start,);
    LTTHREAD_LOGSTRUMENT("start", "Start(%s): Entering s_pMonitor for ThreadMain flow control", pImpl->name);
    LTKMonitorEnter(s_pMonitor); /* the static monitor s_pMonitor protects s_pThreadsRunningListHead and
      also provides thread start flow control as starting threads block on s_pMonitor at the beginning of
      LTThreadImpl_ThreadProc before proceeding. */
    LTTHREAD_LOGSTRUMENT("start", "Start(%s): Entered s_pMonitor", pImpl->name);
    if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted) {
        LTAtomic_FetchAdd(&pImpl->nStackSize, LTTHREAD_STACK_ARTIFICIAL_INCREASAL_BYTES);
        LTTHREAD_LOGSTRUMENT("start", "Start(%s): LTKThreadInitializeAndRun() initiate", pImpl->name); // name can't change while thread running
        if (LTKThreadInitializeAndRun(LTThreadImpl_ThreadImplToPrivateData(pImpl), (u8)LTAtomic_Load(&pImpl->nPriority), LTAtomic_Load(&pImpl->nStackSize), pImpl->name, &LTThreadImpl_ThreadMain, &pImpl->hThread)) {
            LTTHREAD_LOGSTRUMENT("start", "Start(%s): LTKThreadInitializeAndRun() complete", pImpl->name); // name can't change while thread running
           /* set the init proc and the exit proc. */
            pImpl->pInitProc = pInitProc;
            pImpl->pExitProc = pExitProc;
            /* set the thread state and thread number */
            LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_ReadyToRun);
            LTAtomic_Store(&pImpl->nThreadFlags, (s_nNextThreadNumber << kLTThread_ThreadNumberShift) | ((LTAtomic_Load(&pImpl->nThreadFlags) & ~kLTThread_FlagsThreadInitComplete) & kLTThread_ThreadFlagsMask));
            s_nNextThreadNumber++;
            // link the thread into the running list
            #if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
                LTThreadImpl_RelinkWakeupTimerThreadInternal("Start", pImpl);
            #else
                LTThreadImpl_RelinkWakeupTimerThreadInternal(pImpl);
            #endif
        #if 0
            if (s_pThreadsRunningListHead) {
                s_pThreadsRunningListHead->pPrev = pImpl;
                pImpl->pNext = s_pThreadsRunningListHead;
            }
            s_pThreadsRunningListHead = pImpl;
        #endif
        }
        else {
            LTLOG_YELLOWALERT("start.failed", "Failed to start thread");
            // make sure StartSynchronous won't hang waiting for something that will never happen
            LTAtomic_FetchOr(&pImpl->nThreadFlags, kLTThread_FlagsThreadInitComplete);
       }
    }
    else {
        LTLOG_YELLOWALERT("start.already", "Thread %s has been already started", pImpl->name);
    }
    LTTHREAD_LOGSTRUMENT("start", "Start(%s): Exiting s_pMonitor to proceedify ThreadMain", pImpl->name); // name can't change while thread running
    LTKMonitorExit(s_pMonitor);
    LTTHREAD_LOGSTRUMENT("start", "Start(%s): Exited s_pMonitor", pImpl->name); // name can't change while thread running
    LTTHREAD_RELEASE_PIMPL();
}

bool
LTThreadImpl_StartSynchronous(LTThread hThread, LTThread_InitProc * pInitProc, LTThread_ExitProc * pExitProc) {
    /* this function is intentionally not a static function */
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(startsynchronous, false);
    LTThreadImpl_Start(hThread, pInitProc, pExitProc);

    /* _________________________________________________
     * Wait on the thread's monitor for init to complete
     * Note: a thread's monitor is only waited on in ThreadMain(), KillTimer(), and here, and it is impossible for those Waits to
     *      occur simultaneously.  This is important.   Don't add additional waits on an LTThread monitor
     *      without talking to Don first. */
    u32 nFlags = 0;
    LTKMonitorEnter(pImpl->pMonitor);
    while (0 == ((nFlags = LTAtomic_Load(&pImpl->nThreadFlags)) & kLTThread_FlagsThreadInitComplete)) LTKMonitorWait(pImpl->pMonitor, LTTIME_INFINITE);
    LTKMonitorExit(pImpl->pMonitor);
    LTTHREAD_RELEASE_PIMPL();
    return (nFlags & kLTThread_FlagsThreadInitResult);
}

bool LT_ISR_SAFE
LTThreadImpl_QueueTaskProc(LTThread hThread, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    return LTThreadImpl_QueueTaskProcPrivate(hThread, pTaskProc, pClientDataReleaseProc, pClientData, false);
}

bool LT_ISR_SAFE
LTThreadImpl_QueueTaskProcIfRequired(LTThread hThread, LTThread_TaskProc * pTaskProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    return LTThreadImpl_QueueTaskProcPrivate(hThread, pTaskProc, pClientDataReleaseProc, pClientData, true);
}

/*  _________________________
 *  Getting thread attributes */
void
LTThreadImpl_GetName(LTThread hThread, char nameBuffToFill[kLTThread_MaxNameBuff]) {
    // do not assert or log from this function
    nameBuffToFill[0] = 0;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        LTStdlibImpl_strncpyTerm(nameBuffToFill, pImpl->name, kLTThread_MaxNameBuff);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    else LTStdlibImpl_strncpyTerm(nameBuffToFill, "non-lt", kLTThread_MaxNameBuff);
}

void *
LTThreadImpl_GetThreadSpecificClientData(LTThread hThread, const char * pKey) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    struct ThreadSpecificClientDataRecord * pRecord;
    void * pClientData = NULL;
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(getthreadspecificclientdata, NULL);
    if (pKey && *pKey) {
        LTKMonitorEnter(pImpl->pMonitor);
        pRecord = pImpl->pThreadSpecificClientDataHead;
        while (pRecord) if (0 == lt_strcmp(pKey, pRecord->key)) { pClientData = pRecord->pClientData; break; } else pRecord = pRecord->pNext;
        LTKMonitorExit(pImpl->pMonitor);
    }
    LTTHREAD_RELEASE_PIMPL();
    return pClientData;
}

LTThread_ThreadState
LTThreadImpl_GetThreadState(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(getthreadstate, kLTThread_ThreadState_InvalidHandle);
    LTThread_ThreadState threadState = (LTThread_ThreadState)LTAtomic_Load(&pImpl->nThreadState); // LTAtomic, no protection required
    if (kLTThread_ThreadState_ReadyToRun == threadState && hThread == LTThreadImpl_GetCurrentThread()) threadState = kLTThread_ThreadState_Running;
    LTTHREAD_RELEASE_PIMPL();
    return threadState;
}

static u32
LTThreadImpl_GetStackSize(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(getstacksize, 0);
    u32 nStackSize = 0, nCurrStack = 0, nMaxStack = 0;
    if (pImpl) LTKThreadGetStackUsage(LTThreadImpl_ThreadImplToPrivateData(pImpl), &nStackSize, &nCurrStack, &nMaxStack);
    if (0 == nStackSize) nStackSize = LTAtomic_Load(&pImpl->nStackSize);
    LTTHREAD_RELEASE_PIMPL();
    return nStackSize;
}

static void
LTThreadImpl_GetStackUsage(LTHandle hThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet) {
    u32 nStackSize = 0, nStackUsage = 0, nMaxStackUsage = 0;
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        LTKThreadGetStackUsage(LTThreadImpl_ThreadImplToPrivateData(pImpl), &nStackSize, &nStackUsage, &nMaxStackUsage);
        if (0 == nStackSize) nStackSize = LTAtomic_Load(&pImpl->nStackSize);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    else {
        LTThreadImpl_YellowAlert("getstackusage.hthreadinvalid", hThread);
    }

    if (pStackSizeToSet)          *pStackSizeToSet         = nStackSize;
    if (pCurrentStackUsageToSet)  *pCurrentStackUsageToSet = nStackUsage;
    if (pMaxStackUsageToSet)      *pMaxStackUsageToSet     = nMaxStackUsage;
}

static void
LTThreadImpl_GetHeapUsage(LTHandle hThread, u32 * pCurrentHeapUsageToSet, u32 * pMaxHeapUsageToSet) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    u32 nCurrentHeapUsage = 0, nMaxHeapUsage = 0;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        nCurrentHeapUsage = LTAtomic_Load(&pImpl->nCurrHeapUsage);
        nMaxHeapUsage     = LTAtomic_Load(&pImpl->nMaxHeapUsage);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    else {
        LTThreadImpl_YellowAlert("getheapusage.hthreadinvalid", hThread);
    }
    if (pCurrentHeapUsageToSet) *pCurrentHeapUsageToSet = nCurrentHeapUsage;
    if (pMaxHeapUsageToSet)     *pMaxHeapUsageToSet     = nMaxHeapUsage;
}

static bool
LTThreadImpl_IsSystemThread(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(issystemthread, false);
    bool bRetVal = ((LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsIsLTCoreSystemThread) ? true : false); // LTAtomic, no protection required
    LTTHREAD_RELEASE_PIMPL();
    return bRetVal;
}

static bool
LTThreadImpl_IsSystemThreadTimeCritical(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(issystemthreadtimecritical, false);
    u32 nFlags = LTAtomic_Load(&pImpl->nThreadFlags); // LTAtomic, no protection required
    bool bRetVal = ((nFlags & kLTThread_FlagsIsLTCoreSystemThread) && (nFlags & kLTThread_FlagsSystemThreadIsTimeCritical)) ? true : false;
    LTTHREAD_RELEASE_PIMPL();
    return bRetVal;
}

static bool
LTThreadImpl_IsCurrentThread(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(iscurrentthread, false);
    bool bRetVal = ((hThread == LTThreadImpl_GetCurrentThread()) ? true : false);
    LTTHREAD_RELEASE_PIMPL();
    return bRetVal;
}

static void
LTThreadImpl_GetSnapshot(LTThread hThread, LTThread_Snapshot * pSnapshotToFill) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    /* check to make sure we have a valid snapshot to fill */
    if ((NULL == pSnapshotToFill) || (pSnapshotToFill->nStructureSize != sizeof(LTThread_Snapshot))) return;

    /* zero pSnapshot to fill */
    LTStdlibImpl_memset(pSnapshotToFill, 0, sizeof(*pSnapshotToFill));
    pSnapshotToFill->nStructureSize = sizeof(*pSnapshotToFill);

    /* reserve the thread impl, do the fill, and release the thread impl */
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        LTThreadImpl_FillSnapshot(pImpl, pSnapshotToFill);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
}

/*  _________________________
 *  Setting thread attributes */
/* non-static */ void
LTThreadImpl_SetThreadSpecificClientData(LTThread hThread, const char * pKey, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();

    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(setthreadspecificclientdata,);
    if (lt_strlen(pKey) >= kLTThread_MaxThreadSpecificIDBuff) {
        LTLOG_YELLOWALERT("setcd.key.toolarge", "Max keylen is %d", kLTThread_MaxThreadSpecificIDBuff-1);
        pKey = NULL;
    }
    // check for invalid parameters; if params invalid then release the client's client data right away
    if ((NULL == pImpl) || (NULL == pKey) || (0 == *pKey) || (kLTThread_ThreadState_Terminated == LTAtomic_Load(&pImpl->nThreadState))) {
        if (pClientDataReleaseProc && pClientData) pClientDataReleaseProc(kLTThread_ReleaseReason_Because, pClientData);
        if (pImpl) LTTHREAD_RELEASE_PIMPL()
        return;
    }
    // don't allow set but allow clear if the thread has terminate pending
    if ((kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState)) && (NULL != pClientData)) {
        if (pClientDataReleaseProc) pClientDataReleaseProc(kLTThread_ReleaseReason_ThreadSpecificPurge, pClientData);
        LTTHREAD_RELEASE_PIMPL()
        return;
    }

    // see if a record exists already
    struct ThreadSpecificClientDataRecord   *pRecord;
    struct ThreadSpecificClientDataRecord   *pPrev = NULL;
    LTThread_ClientDataReleaseProc          *pReleaseReleaseProc = NULL;
    void                                    *pReleaseClientData = NULL;

    LTKMonitorEnter(pImpl->pMonitor);
    pRecord = pImpl->pThreadSpecificClientDataHead;
    while (pRecord) {
        if (0 == lt_strcmp(pKey, pRecord->key)) {
            // record exists, check if clientdata is same or different
            if (pClientData == pRecord->pClientData) {
                // same, just update the pClientDataReleaseProc
                pRecord->pClientDataReleaseProc = pClientDataReleaseProc;
            }
            else {
                // updating the client data, first save existing clientdata and proc for release
                pReleaseClientData = pRecord->pClientData;
                pReleaseReleaseProc = pRecord->pClientDataReleaseProc;
                // set in the new client data if it is non-null
                if (pClientData) {
                    pRecord->pClientData = pClientData;
                    pRecord->pClientDataReleaseProc = pClientDataReleaseProc;
                }
                else {
                    // clearing this record, remove it
                    if (pRecord == pImpl->pThreadSpecificClientDataHead) pImpl->pThreadSpecificClientDataHead = pRecord->pNext;
                    else pPrev->pNext = pRecord->pNext;
                    core_free(pRecord);
                }
            }
            break;
        }
        pPrev = pRecord;
        pRecord = pRecord->pNext;
    }

    if (NULL == pRecord && pClientData) {
        // a record for pKey was not found, add one
        if (NULL != (pRecord = core_malloc(sizeof(*pRecord)))) {
            pRecord->pNext = pImpl->pThreadSpecificClientDataHead;
            pImpl->pThreadSpecificClientDataHead = pRecord;
            pRecord->pClientData = pClientData;
            pRecord->pClientDataReleaseProc = pClientDataReleaseProc;
            LTStdlibImpl_strncpyTerm(pRecord->key, pKey, kLTThread_MaxThreadSpecificIDBuff);
        }
        else {
            LTLOG_YELLOWALERT("setcd.badmalloc", "Failed to allocate record for thread specific client data");
            pReleaseReleaseProc = pClientDataReleaseProc;
            pReleaseClientData = pClientData;
        }
    }
    LTKMonitorExit(pImpl->pMonitor);

    if (pReleaseReleaseProc && pReleaseClientData) pReleaseReleaseProc(kLTThread_ReleaseReason_ThreadSpecificReset, pReleaseClientData);
    LTTHREAD_RELEASE_PIMPL()
}

/* non-static */ void
LTThreadImpl_SetStackSize(LTThread hThread, u32 nStackSize) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(setstacksize,);
    LTKMonitorEnter(s_pMonitor); /* entering s_pMonitor locks out thread startup */
    if ((u32)kLTThread_ThreadState_NotStarted == LTAtomic_Load(&pImpl->nThreadState)) {
        LTAtomic_Store(&pImpl->nStackSize, nStackSize);
    }
    LTKMonitorExit(s_pMonitor);
    LTTHREAD_RELEASE_PIMPL()
}

/* non-static */ u8
LTThreadImpl_GetPriority(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(getpriority, 0);
    u8 retVal = (u8) LTAtomic_Load(&pImpl->nPriority);
    LTTHREAD_RELEASE_PIMPL()
    return retVal;
}

/* non-static */ u8
LTThreadImpl_SetHighestLTCoreThreadPriorityFromLTKIdleTickISRContext(LTThread hThread) {
    u8 previousPriority = 0;
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (pImpl) {
        previousPriority = (u8) LTAtomic_Load(&pImpl->nPriority);
        LTKThreadSetPriority(LTThreadImpl_ThreadImplToPrivateData(pImpl), kLTThread_PrivatePriorityHighest);
        LTAtomic_Store(&pImpl->nPriority, kLTThread_PrivatePriorityHighest);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    return previousPriority;
}

/* non-static */ void
LTThreadImpl_SetPriority(LTThread hThread, u8 nPriority) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(setpriority,);
    if (! LTKInsideInterruptContext()) {
        u8 nPriorityMax = (LTAtomic_Load(&pImpl->nThreadFlags) & kLTThread_FlagsIsLTCoreSystemThread) ? kLTThread_PrivatePriorityHighest : kLTThread_ClientPriorityHighest;
        if (nPriority > nPriorityMax) nPriority = nPriorityMax;
        if (nPriority != (u8)LTAtomic_Load(&pImpl->nPriority)) {
            LTKMonitorEnter(s_pMonitor); /* lock out thread state transitions */
            if (LTThreadImpl_InActiveRunState(pImpl)) LTKThreadSetPriority(LTThreadImpl_ThreadImplToPrivateData(pImpl), nPriority);
            LTAtomic_Store(&pImpl->nPriority, (u32)nPriority);
            LTKMonitorExit(s_pMonitor);
        }
    }
    LTTHREAD_RELEASE_PIMPL()
}

/*  ___________________________
 *  Controlling thread shutdown */
void
LTThreadImpl_Terminate(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(terminate,);

    LTKMonitorEnter(s_pMonitor); /* s_pMonitor guards run state transitions  */
    LTThreadImpl_TerminateImplInternal(pImpl);
    LTKMonitorExit(s_pMonitor);

    LTTHREAD_RELEASE_PIMPL();
}

static void
LTThreadImpl_TerminatePublic(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (! LTThreadImpl_IsSystemThread(hThread)) LTThreadImpl_Terminate(hThread);
}

bool
LTThreadImpl_WaitUntilFinished(LTThread hThread, LTTime timeout) {
    bool bRetVal = false;
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (hThread == LTThreadImpl_GetCurrentThread()) {
        LTThreadImpl_YellowAlert("waituntilfinished.cantwaitonself", hThread);
        return false;
    }
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(waituntilfinished, false);
    if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted) {
        LTThreadImpl_YellowAlert("waituntilfinished.threadnotstarted", hThread);
    }
    else {
        // call underlying scheduler to verify termination state
        bRetVal = LTKThreadWaitUntilFinished(LTThreadImpl_ThreadImplToPrivateData(pImpl), timeout.nNanoseconds);
    }
    LTTHREAD_RELEASE_PIMPL();
    return bRetVal;
}

static bool
LTThreadImpl_IsTerminatePending(LTThread hThread) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(isterminatepending, false);
    bool bRetVal = (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_TerminatePending);
    LTTHREAD_RELEASE_PIMPL();
    return bRetVal;
}


static void
LTThreadImpl_SetTimer(LTThread hThread, LTTime timeout, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    bool bReleaseNow = false;
    if (pTimerProc) {
        LTThreadImpl *pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
        if (pImpl) {
            if (LTTime_IsLessThan(timeout, LTTime_Milliseconds(1))) timeout = LTTime_Milliseconds(1);
            LTKMonitorEnter(pImpl->pMonitor);
            if (LTThreadImpl_InActiveRunState(pImpl)) {
                struct TimerRecord * pTimerRecord = LTThreadImpl_UnlinkTimerRecordInternal(pImpl, pTimerProc, pClientData);
                if (pTimerRecord) {
                    if (pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsAbsolute) {
                        // can't reset an absolute timer, put him back
                        LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);
                        pTimerRecord = NULL; // so we don't wake other thread to wait with new timeout
                    }
                    else {
                        // reset timeout and release proc and reschedule
                        pTimerRecord->period = timeout;
                        pTimerRecord->pClientDataReleaseProc = pClientDataReleaseProc;
                        LTThreadImpl_CalculateFireTimeInternal(pTimerRecord);
                        LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);
                    }
                }
                else {
                    LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc_TimerRecord, sizeof(struct TimerRecord));
                    if (NULL != (pTimerRecord = (struct TimerRecord *)core_malloc(sizeof(struct TimerRecord)))) {
                        LTTime fireTime = LTTime_Add(LTCoreImpl_GetKernelTime(), timeout);
                        if (LTTime_IsLessThanOrEqual(fireTime, LTTime_Zero())) fireTime = LTTime_Infinite(); // wraparound, clamp to infinite
                        pTimerRecord->pNext = NULL;
                        pTimerRecord->nFlags = 0;
                        pTimerRecord->period = timeout;
                        pTimerRecord->pTimerProc = pTimerProc;
                        pTimerRecord->pClientData = pClientData;
                        pTimerRecord->pClientDataReleaseProc = pClientDataReleaseProc;
                        pTimerRecord->fireTime = fireTime;
                        LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);
                    }
                    else {
                        bReleaseNow = true;
                    }
                }
                if (pTimerRecord && pTimerRecord == pImpl->pFirstTimerRecord && hThread != LTThreadImpl_GetCurrentThread()) {
                    // we just set the next-to-fire timer on another thread; wake the other thread so it can recalc it's wait time
                    LTKMonitorNotify(pImpl->pMonitor);
                }
            }
            else {
                bReleaseNow = true;
            }
            LTKMonitorExit(pImpl->pMonitor);
            LTTHREAD_RELEASE_PIMPL();
        }
        else {
            bReleaseNow = true;
            LTThreadImpl_YellowAlert("settimer.invalidthread", hThread);
        }
    }
    else {
        bReleaseNow = true;
    }

    if (bReleaseNow && pClientDataReleaseProc) pClientDataReleaseProc(kLTThread_ReleaseReason_Because, pClientData);
}

static void
LTThreadImpl_SetTimerAbsolute(LTThread hThread, LTTime kernelStartTime, LTTime deadlinePeriod, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    bool bReleaseNow = false;
    if (pTimerProc) {
        LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(settimerabsolute,);
        if (LTTime_IsLessThan(deadlinePeriod, LTTime_Milliseconds(1))) deadlinePeriod = LTTime_Milliseconds(1);
        LTKMonitorEnter(pImpl->pMonitor);
        if (LTThreadImpl_InActiveRunState(pImpl)) {
            // we can only set an absolute timer if there exists no timer with same timer proc and client data already
            struct TimerRecord * pCurr = pImpl->pFirstTimerRecord;
            while (pCurr && ((pCurr->pTimerProc != pTimerProc) || (pCurr->pClientData != pClientData))) pCurr = pCurr->pNext;
            if (NULL == pCurr) {
                // does not yet exist, create one
                LTTRACE_MEM_OP(kLTTrace_MemOp_Alloc_TimerRecord, sizeof(struct TimerRecord));
                if (NULL != (pCurr = (struct TimerRecord *)core_malloc(sizeof(struct TimerRecord)))) {
                    LTTime kernelTime = LTCoreImpl_GetKernelTime();
                    pCurr->pNext = NULL;
                    pCurr->nFlags = kLTThreadImpl_TimerFlagsIsAbsolute;
                    pCurr->period = deadlinePeriod;
                    pCurr->pTimerProc = pTimerProc;
                    pCurr->pClientData = pClientData;
                    pCurr->pClientDataReleaseProc = pClientDataReleaseProc;
                    if (LTTime_IsZero(kernelStartTime)) {
                        // LTTime_Zero means start the timer now; first firing is now + deadlinePeriod
                        pCurr->fireTime = LTTime_Add(kernelTime, deadlinePeriod);
                    }
                    else if (LTTime_IsGreaterThan(kernelStartTime, kernelTime)) {
                        // requested kernelStartTime is in the future; use directly
                        pCurr->fireTime = kernelStartTime;
                    }
                    else {
                        // calculate when the first deadline past the current tkernel time will hit
                        // equation is: fireTime = kernelStartTime + deadlinePeriod * (1 + (kernelTime - kernelStartTime) / deadlinePeriod)
                        LTTime_SubtractFrom(kernelTime, kernelStartTime);
                        LTTime_DivideBy(kernelTime, deadlinePeriod.nNanoseconds);
                        kernelTime = LTTime_Multiply(deadlinePeriod, kernelTime.nNanoseconds + 1);
                        pCurr->fireTime = LTTime_Add(kernelStartTime, kernelTime);
                    }
                    LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pCurr);
                    if (pCurr == pImpl->pFirstTimerRecord && hThread != LTThreadImpl_GetCurrentThread()) {
                        // we just set the next-to-fire timer on another thread; wake the other thread so it can recalc it's wait time
                        LTKMonitorNotify(pImpl->pMonitor);
                    }
                }
                else {
                    bReleaseNow = true;
                }
            }
        }
        else {
            bReleaseNow = true;
        }
        LTKMonitorExit(pImpl->pMonitor);
        LTTHREAD_RELEASE_PIMPL();
    }
    else {
        bReleaseNow = true;
    }

    if (bReleaseNow && pClientDataReleaseProc) pClientDataReleaseProc(kLTThread_ReleaseReason_Because, pClientData);
}

static LTTime
LTThreadImpl_GetTimerExpirationKernelTime(LTThread hThread, LTThread_TimerProc * pTimerProc, void * pClientData) {    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTTime fireTime = LTTime_Zero();
    LTThreadImpl *pImpl = pTimerProc ? LTThreadImpl_ReserveThreadImpl(hThread) : NULL;
    if (pImpl) {
        LTKMonitorEnter(pImpl->pMonitor);
        struct TimerRecord * pTimerRecord = LTThreadImpl_InActiveRunState(pImpl) ? pImpl->pFirstTimerRecord : NULL;
        while (pTimerRecord) {
            if (pTimerRecord->pTimerProc == pTimerProc && pTimerRecord->pClientData == pClientData) {
                fireTime = pTimerRecord->fireTime;
                break;
            }
            pTimerRecord = pTimerRecord->pNext;
        }
        LTKMonitorExit(pImpl->pMonitor);
        LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
    }
    return fireTime;
}

static void
LTThreadImpl_KillTimer(LTThread hThread, LTThread_TimerProc * pTimerProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pTimerProc) {
        LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(killtimer,);
        LTKMonitorEnter(s_pMonitor);
        LTKMonitorEnter(pImpl->pMonitor);
        struct TimerRecord * pTimerRecord = LTThreadImpl_UnlinkTimerRecordInternal(pImpl, pTimerProc, pClientData);
        if (pTimerRecord && (pTimerRecord->nFlags == kLTThreadImpl_TimerFlagsIsWakeupTimer)) {
            /* wakeup timer just removed from list; see if it was the last wakeup timer */
            struct TimerRecord * pCurr = pImpl->pFirstTimerRecord;
            while (pCurr) {
                if (pCurr->nFlags & kLTThreadImpl_TimerFlagsIsWakeupTimer) break;
                pCurr = pCurr->pNext;
            }
            if (! pCurr) {
                /* no more wakeup timers in list, clear the thread flags and relink thread record */
                LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~kLTThread_FlagsHasSofwareWakeupTimer);
                #if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
                    LTThreadImpl_RelinkWakeupTimerThreadInternal("KillTimer", pImpl);
                #else
                    LTThreadImpl_RelinkWakeupTimerThreadInternal(pImpl);
                #endif
            }
        }
        LTKMonitorExit(s_pMonitor);
        if ((pTimerProc == pImpl->pTimerProcInProgress) && (pClientData == pImpl->pTimerClientDataInProgress) && (hThread != LTThreadImpl_GetCurrentThread())) {
            /* this timer is in process of being dispatched by another thread.  We have to wait here until the dispatch
               is complete because the contract of KillTimer() is that once it returns, pClientData will no longer be accessed
               and the client is free to delete it.

               Note: a thread's monitor is only waited on in ThreadMain(), StartSynchronous(), and here, and it is impossible for those Waits to
               occur simultaneously.  This is important.   Don't add additional waits on an LTThread monitor
               without talking to Don first. */

            /* first set pImpl->pTimerProcInProgress to NULL so ThreadMain knows we need to be notified. */
           pImpl->pTimerProcInProgress = NULL;
           LTKMonitorWait(pImpl->pMonitor, LTTIME_INFINITE);
        }
        LTKMonitorExit(pImpl->pMonitor);
        if (pTimerRecord) {
            if (pTimerRecord->pClientDataReleaseProc) (*pTimerRecord->pClientDataReleaseProc)(kLTThread_ReleaseReason_TimerKilled, pTimerRecord->pClientData);
            LTTRACE_MEM_OP(kLTTrace_MemOp_Free_TimerRecord, sizeof(*pTimerRecord));
            core_free(pTimerRecord);
        }
        LTTHREAD_RELEASE_PIMPL();
    }
}

static void
LTThreadImpl_RestartTimer(LTThread hThread, LTThread_TimerProc * pTimerProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pTimerProc) {
        LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(restarttimer,);
        LTKMonitorEnter(pImpl->pMonitor);
        if (LTThreadImpl_InActiveRunState(pImpl)) {
            struct TimerRecord * pTimerRecord = LTThreadImpl_UnlinkTimerRecordInternal(pImpl, pTimerProc, pClientData);
            if (pTimerRecord) {
                if (pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsAbsolute) {
                    // can't restart absolute timers; put it back
                    LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);
                }
                else {
                    LTThreadImpl_CalculateFireTimeInternal(pTimerRecord);
                    LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);
                    if (pTimerRecord == pImpl->pFirstTimerRecord && hThread != LTThreadImpl_GetCurrentThread()) {
                        // we just restarted the next-to-fire timer on another thread; wake the other thread so it can recalc it's wait time
                        LTKMonitorNotify(pImpl->pMonitor);
                    }
                }
            }
        }
        LTKMonitorExit(pImpl->pMonitor);
        LTTHREAD_RELEASE_PIMPL();
    }
}

static void
LTThreadImpl_SetAsWakeupTimer(LTThread hThread, LTThread_TimerProc *pTimerProc, void *pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if (pTimerProc) {
        LTHREAD_RESERVE_PIMPL_OR_YELLOW_ALERT_AND_RETURN(setaswakeuptimer,);
        /* assume we're going to find it and set it so take the thread global monitor */
        LTKMonitorEnter(s_pMonitor);
        LTKMonitorEnter(pImpl->pMonitor);
        if (LTThreadImpl_InActiveRunState(pImpl)) {
            struct TimerRecord * pTimerRecord = LTThreadImpl_FindTimerRecordInternal(pImpl, pTimerProc, pClientData);
            if (pTimerRecord && pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsWakeupTimer) pTimerRecord = NULL; /* already a wakeup timer, nothing to do */
            if (pTimerRecord) {
                pTimerRecord->nFlags |= kLTThreadImpl_TimerFlagsIsWakeupTimer;
                u32 nOldFlags = LTAtomic_FetchOr(&pImpl->nThreadFlags, kLTThread_FlagsHasSofwareWakeupTimer);
                #if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
                    if (0 == (nOldFlags & kLTThread_FlagsHasSofwareWakeupTimer)) LTThreadImpl_RelinkWakeupTimerThreadInternal("SetAsWakeupTimer", pImpl);
                #else
                    if (0 == (nOldFlags & kLTThread_FlagsHasSofwareWakeupTimer)) LTThreadImpl_RelinkWakeupTimerThreadInternal(pImpl);
                #endif
            }
        }
        LTKMonitorExit(pImpl->pMonitor);
        LTKMonitorExit(s_pMonitor);
        LTTHREAD_RELEASE_PIMPL();
    }
}

/*  _____________________________________________________________
 *  Functions that only operate on the currently executing thread */
void
LTThreadImpl_Sleep(LTTime sleepTime) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    /* non-static function on purpose */
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    u32 threadState = pImpl ? LTAtomic_Load(&pImpl->nThreadState) : kLTThread_ThreadState_TerminatePending;
        /* Note when pImpl == NULL (non-LTThread) we pretend TerminatePending.  This is to
           optimize the comparison on the next line to a single test */
    if ((kLTThread_ThreadState_TerminatePending == threadState) || (kLTThread_ThreadState_Terminated == threadState)) {
        // not an LTThread or terminate pending or sleep after 'terminated' (thanks bsp); still allow sleep but don't change threadState
        LTKThreadSleep(sleepTime.nNanoseconds);
    }
    else {
        // only change the thread state if it didn't change out from under us  (due to other thread calling Terminate after we just checked)
        #ifdef LT_DEBUG
            LT_ASSERT(   LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, kLTThread_ThreadState_Sleeping)
                      || (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_TerminatePending)
                      || (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_Terminated));
        #else
            LTAtomic_CompareAndExchange(&pImpl->nThreadState, threadState, kLTThread_ThreadState_Sleeping);
        #endif

        // sleep (phew!)
        LTKThreadSleep(sleepTime.nNanoseconds);

        // only change the thread state back if we didn't change out of sleeping (to terminate pending)
        #ifdef LT_DEBUG
            LT_ASSERT(   LTAtomic_CompareAndExchange(&pImpl->nThreadState, kLTThread_ThreadState_Sleeping, threadState)
                      || (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_TerminatePending)
                      || (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_Terminated));
        #else
            LTAtomic_CompareAndExchange(&pImpl->nThreadState, kLTThread_ThreadState_Sleeping, threadState);
        #endif
    }
    if (pImpl) LTTHREAD_RELEASE_PIMPL();
}


void
LTThreadImpl_Yield(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTKThreadSleep(0);
}

/*  __________________________________________________
 *  Functions that don't depend on any specific thread */
LT_TEXT_RAM_CRITICAL(1)
LTThread LTThreadImpl_GetCurrentThread(void) {
    // don't need to reserve because we get pImpl from LTK/BSP and it's currently executing
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTThreadImpl * pImpl = LTThreadImpl_PrivateDataToThreadImpl(LTKThreadGetCurrentThreadInstanceData());
    return (pImpl && LTHandle_IsHandleValid(pImpl->hThread)) ? pImpl->hThread : (LTHandle)0;
}

static void
LTThreadImpl_SnapshotRunningThreads(LTThread_SnapshotRunningThreadsCB * pSnapshotRunningThreadsCB, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    // don't need to reserve pImpls because we're getting them from the running list and they're valid while in the list
    if (pSnapshotRunningThreadsCB) {
        u32                   nCount            = 0;
        LTThreadImpl        * pImpl             = NULL;
        LTThread_Snapshot   * pSnapshots        = NULL;

        // with s_pMonitor locked, count the running threads, alloc space for the snapshots, take the snapshots and exit the monitor before notifying client
        LTKMonitorEnter(s_pMonitor);
            for (pImpl = s_pThreadsRunningListHead; pImpl != NULL; ++nCount, pImpl = pImpl->pNext);
            if (nCount) {
                pSnapshots = core_malloc(sizeof(LTThread_Snapshot) * nCount);
                if (pSnapshots == NULL) {
                    LTLOG_DEBUG("snapshotrunningthreads.allocfailure", NULL);
                } else {
                    LTThread_Snapshot * pSnapshot = pSnapshots;
                    for (pImpl = s_pThreadsRunningListHead; pImpl != NULL; ++pSnapshot, pImpl = pImpl->pNext) {
                        LTThreadImpl_FillSnapshot(pImpl, pSnapshot);
                    }
                }
            }
        LTKMonitorExit(s_pMonitor);

        (*pSnapshotRunningThreadsCB)(pSnapshots, nCount, pClientData);
        if (pSnapshots != NULL) {
            core_free(pSnapshots);
        }
    }
}

u32
LTThreadImpl_GetDefaultStackSize(void) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    return LTKThreadGetDefaultStackSize();
}

static const char *
LTThreadImpl_ThreadStateToString(LTThread_ThreadState threadState) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    switch (threadState) {
        case kLTThread_ThreadState_InvalidHandle     : return "InvalidHandle";
        case kLTThread_ThreadState_NotStarted        : return "NotStarted";
        case kLTThread_ThreadState_ReadyToRun        : return "ReadyToRun";
        case kLTThread_ThreadState_Running           : return "Running";
        case kLTThread_ThreadState_WaitBlocked       : return "WaitBlocked";
        case kLTThread_ThreadState_MutexBlocked      : return "MutexBlocked";
        case kLTThread_ThreadState_Sleeping          : return "Sleeping";
        case kLTThread_ThreadState_TerminatePending  : return "TerminatePending";
        case kLTThread_ThreadState_Terminated        : return "Terminated";
        default                                      : return "???";
    }
}

/*************************************************
 *************************************************
 * Interface definition *
 *
 **************
 * ILTThread  */
define_LTLIBRARY_INTERFACE(ILTThread, LTThreadImpl_OnDestroyHandle, 1)

    .Start                          = &LTThreadImpl_Start,
    .StartSynchronous               = &LTThreadImpl_StartSynchronous,
    .QueueTaskProc                  = &LTThreadImpl_QueueTaskProc,
    .QueueTaskProcIfRequired        = &LTThreadImpl_QueueTaskProcIfRequired,

    .GetName                        = &LTThreadImpl_GetName,
    .GetThreadSpecificClientData    = &LTThreadImpl_GetThreadSpecificClientData,
    .GetThreadState                 = &LTThreadImpl_GetThreadState,
    .GetStackSize                   = &LTThreadImpl_GetStackSize,
    .GetStackUsage                  = &LTThreadImpl_GetStackUsage,
    .GetHeapUsage                   = &LTThreadImpl_GetHeapUsage,
    .GetPriority                    = &LTThreadImpl_GetPriority,
    .IsSystemThread                 = &LTThreadImpl_IsSystemThread,
    .IsSystemThreadTimeCritical     = &LTThreadImpl_IsSystemThreadTimeCritical,
    .IsCurrentThread                = &LTThreadImpl_IsCurrentThread,
    .GetSnapshot                    = &LTThreadImpl_GetSnapshot,

    .SetThreadSpecificClientData    = &LTThreadImpl_SetThreadSpecificClientData,
    .SetStackSize                   = &LTThreadImpl_SetStackSize,
    .SetPriority                    = &LTThreadImpl_SetPriority,

    .Terminate                      = &LTThreadImpl_TerminatePublic,
    .WaitUntilFinished              = &LTThreadImpl_WaitUntilFinished,
    .IsTerminatePending             = &LTThreadImpl_IsTerminatePending,

    .SetTimer                       = &LTThreadImpl_SetTimer,
    .SetTimerAbsolute               = &LTThreadImpl_SetTimerAbsolute,
    .KillTimer                      = &LTThreadImpl_KillTimer,
    .SetAsWakeupTimer               = &LTThreadImpl_SetAsWakeupTimer,
    .GetTimerExpirationKernelTime   = &LTThreadImpl_GetTimerExpirationKernelTime,
    .RestartTimer                   = &LTThreadImpl_RestartTimer,

    .Sleep                          = &LTThreadImpl_Sleep,
    .Yield                          = &LTThreadImpl_Yield,

    .GetCurrentThread               = &LTThreadImpl_GetCurrentThread,
    .SnapshotRunningThreads         = &LTThreadImpl_SnapshotRunningThreads,
    .GetDefaultStackSize            = &LTThreadImpl_GetDefaultStackSize,
    .ThreadStateToString            = &LTThreadImpl_ThreadStateToString,

LTLIBRARY_DEFINITION;

/*  _________________________________
 *  _________________________________
 *  ThreadMain() - THE BIG MAGILLA !!
 */
#if 0
static void
NumberToString(u32 number, char *buff, u32 buffLen) {
    char *pChar = buff + buffLen - 1;
    if (0 == number) *pChar = '0';
    else while (number) {
            if (pChar < buff) {
                /* won't fit in allotted space, reset with 'E' and clear with spaces */
                pChar = buff + buffLen - 1; *pChar-- = 'E'; while (pChar >= buff) *pChar-- = ' ';
                break;
            }
            *pChar-- = (number % 10) + '0'; number /= 10;
         }
}

static void ReportStackAndHeap(const char * tag) {
        u32 stackSize = 0, stackCurr = 0, stackMax = 0, heapCurr = 0, heapMax = 0;
        LTCore * pCore = LT_GetCore();
        ILTThread * iThread = lt_getlibraryinterface(ILTThread, pCore);
        iThread->GetStackUsage(iThread->GetCurrentThread(), &stackSize, &stackCurr, &stackMax);
        iThread->GetHeapUsage(iThread->GetCurrentThread(), &heapCurr, &heapMax);
        enum { kBuffSize = 48 };
        char *buff = lt_malloc(kBuffSize);
        if (buff) {
            pCore->ConsolePutString("\nMAGILLA "); pCore->ConsolePutString(tag); pCore->ConsolePutString(":\n");
            pCore->ConsolePutString(" __________________________________________\n");
//                                   0  x     x1     x   2    x   3     x   4
//                                   01234567890123456789012345678901234567890123 4
            pCore->ConsolePutString(" | stack   curr    max | heap curr      max\n");
            lt_memset(buff, ' ', kBuffSize -  2); buff[kBuffSize-1] = 0; buff[1] = buff[23] = '|';
            NumberToString(stackSize, buff +  3, 5);
            NumberToString(stackCurr, buff +  9, 6);
            NumberToString(stackMax,  buff + 16, 6);
            NumberToString(heapCurr,  buff + 25, 9);
            NumberToString(heapMax,   buff + 35, 8);
            pCore->ConsolePutString(buff);
            lt_free(buff);
        }
}

LT_TEXT_RAM_CRITICAL(1) static void LTThreadImpl_ThreadMain2(void * pClientData);
LT_TEXT_RAM_CRITICAL(1) static void LTThreadImpl_ThreadMain(void * pClientData) {
    LTThread hThread = *((LTThread *)pClientData);
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    ReportStackAndHeap(pImpl->name);
    LTThreadImpl_ThreadMain2(pClientData);
    LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
}
#endif

LT_TEXT_RAM_CRITICAL(1) /* non-static */ void LTThreadImpl_ThreadMain(void * pClientData) {
    /* This is the main thread dispatch loop! */
    LT_SIZE nMask;
    LTThread hThread = *((LTThread *)pClientData);
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(hThread);
    if (NULL == pImpl) {
        LTLOG_REDALERT("threadmain.invalidhandle", "time to die");
        return;
    }

#if 0
    nMask = LTThreadImpl_IsProhibitLoggingFlagSet() ? LTThreadImpl_SetProhibitLoggingFlag(false), 1 : 0;
    LTLOG("threadmain", "%s running with stacksize %d",
        pImpl->name, (int)LTAtomic_Load(&pImpl->nStackSize));
    if (nMask) LTThreadImpl_SetProhibitLoggingFlag(true);
#endif

    // enter and exit the global monitor to ensure that we don't start executing before Start() completes
        // NOTE: must use LTLOG_LOGSTOMP prior have to log stomp which disables and uses interrupt buffer while Start() is still modifying thread state
        // in order to avoid logger mutex acquisition with threaded logs which also modifies thread state
        // NOTE: I fixed the condition described in the prior 2 lines so this can stay LTLOG_DEBUG for now.. Keep an eye on it.
    LTTHREAD_LOGSTRUMENT("threadmain", "s_pMonitor Enter/Exit: Enter s_pMonitor to wait until Start() proceedifies");
    LTKMonitorEnter(s_pMonitor);
    // from here forward we can use regular logging
    LTTHREAD_LOGSTRUMENT("threadmain", "s_pMonitor Enter/Exit: s_pMonitor Entered");
    LTKMonitorExit(s_pMonitor);
    LTTHREAD_LOGSTRUMENT("threadmain", "s_pMonitor Enter/Exit: s_pMonitor Exited");

    LT_ASSERT((u32)kLTThread_ThreadState_ReadyToRun == LTAtomic_Load(&pImpl->nThreadState) || (u32)kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState));

    /* DRW 22-Jan-2025 : LT-2799 - enable QueueTaskProc from ISRs *before* calling ThreadInit so ThreadInit can initialize devices that want to start queueing right away */
    LTAtomic_FetchOr(&pImpl->nThreadFlags, kLTThread_FlagsCanPostFromISR);

    // DRW 23-Jul-21 : LT-177 always call init proc, even if terminate requested squeezes in first
    if (pImpl->pInitProc && (false == (pImpl->pInitProc)())) {
        /* DRW 22-Jan-2025 : LT-2799 - ThreadInit returned false, disable receipt from ISRs (though the user shouldn't leave devices configured if they're returning false) */
        LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~kLTThread_FlagsCanPostFromISR);
        LTThreadImpl_Terminate(hThread);
    }
    else LTAtomic_FetchOr(&pImpl->nThreadFlags, kLTThread_FlagsThreadInitResult);

    LTAtomic_FetchOr(&pImpl->nThreadFlags, kLTThread_FlagsThreadInitComplete);

    // now run the thread loop
    LTTime kernelTime;
    LTThread_ReleaseReason reason;
    struct TimerRecord * pTimerRecord;
    struct ISRTaskProcRecord * pHeadISR;
    struct ISRTaskProcRecord * pCurrISR;
    struct TaskProcRecord * pNext = NULL;
    LTThread_TimerProc * pLastFiredTimerProc = NULL;
    void * pLastFiredTimerClientData = NULL;

    LTKMonitorEnter(pImpl->pMonitor);
    LTKMonitorNotify(pImpl->pMonitor); /* Notify in case this thread was started with StartSynchronous */
    while ((u32)kLTThread_ThreadState_TerminatePending != LTAtomic_Load(&pImpl->nThreadState)) {
        /* service any TaskProcRecords from interrupt handlers by appending them to the TaskProcRecord queue
            ISSUE: may want to just execute them all directly instead of appending to queue, but  */
        nMask = LTKDisableInterrupts();
        if (pImpl->pFirstISRTaskProcRecord) {
            // first reverse the list to get them in the proper order
            pHeadISR = NULL;
            while (pImpl->pFirstISRTaskProcRecord) {
                pCurrISR = pImpl->pFirstISRTaskProcRecord->pNext;
                pImpl->pFirstISRTaskProcRecord->pNext = pHeadISR;
                pHeadISR = pImpl->pFirstISRTaskProcRecord;
                pImpl->pFirstISRTaskProcRecord = pCurrISR;
            }
            // I've got them out of the list into a local list; enable interrupts while I post to my TaskProcRecord queue
            // marking the failed post records that have a release proc with a release reason
            LTKEnableInterrupts(nMask);
            pCurrISR = pHeadISR; pNext = NULL;
            while (pCurrISR) {
                if (NULL == pCurrISR->pTaskProc || ((LT_SIZE)pCurrISR->pTaskProc > (LT_SIZE)kLTThread_ReleaseReason_Last)) {
                    /* coming from ISR directly as only a release proc or as a legit task proc */
                    reason = LTThreadImpl_QueueTaskProcRecordInternal(pImpl, pCurrISR->pTaskProc, pCurrISR->pClientDataReleaseProc, pCurrISR->pClientData, pCurrISR->bIfRequired);
                    if (pCurrISR->pClientDataReleaseProc && ((reason != kLTThread_ReleaseReason_TaskProcComplete) && (reason != kLTThread_ReleaseReason_NullReason))) {
                        pCurrISR->pTaskProc = (void *)((LT_SIZE)reason);
                        pNext = (void *)1; /* flag to mark presence of a post failure */
                    }
                    else pCurrISR->pTaskProc = NULL;
                }
                else {
                    /* ISR queued execution of the release proc due to a previous queueing error, let it be executed now, do not requeue */
                    pNext = (void *)1; /* flag to mark presence of a post failure */
                }
                pCurrISR = pCurrISR->pNext;
            }
            /* call the client data release procs on any failures */
            if (pNext) {
                pCurrISR = pHeadISR;
                LTKMonitorExit(pImpl->pMonitor);
                while (pCurrISR) {
                    if (pCurrISR->pTaskProc && ((LT_SIZE)pCurrISR->pTaskProc <= (LT_SIZE)kLTThread_ReleaseReason_Last)) pCurrISR->pClientDataReleaseProc((LTThread_ReleaseReason)((LT_SIZE)pCurrISR->pTaskProc), pCurrISR->pClientData);
                    pCurrISR = pCurrISR->pNext;
                }
                LTKMonitorEnter(pImpl->pMonitor);
            }
            /* put records back on isr free queue */
            nMask = LTKDisableInterrupts();
            while (pHeadISR) {
                pCurrISR = pHeadISR->pNext;
                pHeadISR->pNext = s_pISRTaskProcRecordFreePool;
                s_pISRTaskProcRecordFreePool = pHeadISR;
                pHeadISR = pCurrISR;
            }
        }

        LTKEnableInterrupts(nMask);

        // service the timers
        if (pImpl->pFirstTimerRecord) {
            pLastFiredTimerProc = NULL; pLastFiredTimerClientData = NULL; // reset each time through outer loop
            kernelTime = LTCoreImpl_GetKernelTime(); // record this outside of the loop
            while (pImpl->pFirstTimerRecord && LTTime_IsLessThanOrEqual(pImpl->pFirstTimerRecord->fireTime, kernelTime)) {
                // if the same timer wants to go again, let him only if there are no TaskProcRecords in the queue
                if (pImpl->pFirstTimerRecord->pTimerProc == pLastFiredTimerProc && pImpl->pFirstTimerRecord->pClientData == pLastFiredTimerClientData && pImpl->pTaskProcQueueHead) break;

                // take this record out of the list and reschedule it
                pTimerRecord = pImpl->pFirstTimerRecord;
                pImpl->pFirstTimerRecord = pTimerRecord->pNext;
                pTimerRecord->pNext = NULL;
                pLastFiredTimerProc = pTimerRecord->pTimerProc;
                pLastFiredTimerClientData = pTimerRecord->pClientData;
                // calculate new fire time
                if (pTimerRecord->nFlags & kLTThreadImpl_TimerFlagsIsAbsolute) {
                       do  LTTime_AddTo(pTimerRecord->fireTime, pTimerRecord->period);
                    while (LTTime_IsLessThanOrEqual(pTimerRecord->fireTime, kernelTime));
                }
                else {
                    pTimerRecord->fireTime = LTTime_Add(kernelTime, pTimerRecord->period);
                }
                if (LTTime_IsLessThanOrEqual(pTimerRecord->fireTime, LTTime_Zero())) pTimerRecord->fireTime = LTTime_Infinite();
                // put it back in the list
                LTThreadImpl_ScheduleTimerRecordInternal(pImpl, pTimerRecord);

                // now fire the timer
                pImpl->pTimerProcInProgress = pLastFiredTimerProc;
                pImpl->pTimerClientDataInProgress = pLastFiredTimerClientData;
                LTKMonitorExit(pImpl->pMonitor);
                LTTRACE_NUMERIC(timer, 1, LTTRACE_PTR(pLastFiredTimerProc));
                pLastFiredTimerProc(pLastFiredTimerClientData);
                LTTRACE_NUMERIC(timer, 0, LTTRACE_PTR(pLastFiredTimerProc));
                LTKMonitorEnter(pImpl->pMonitor);
                if (NULL == pImpl->pTimerProcInProgress) {
                    // another thread called KillTimer on this timer while dispatch was in progress
                    // and is now waiting on our monitor for dispatch to complete; let him proceed
                    LTKMonitorNotify(pImpl->pMonitor);
                }
                pImpl->pTimerProcInProgress = NULL;
                pImpl->pTimerClientDataInProgress = NULL;
                if ((u32)kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState)) break;
            }
            if ((u32)kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState)) break;
        }

        if (NULL == pImpl->pTaskProcQueueHead) {
            // no TaskProcRecords in our queue; going to wait for a TaskProcRecord, reuse kernelTime for timeout to save stack space
            if (pImpl->pFirstTimerRecord) {
                // calculate how long we should wait
                kernelTime = LTCoreImpl_GetKernelTime();
                kernelTime = (LTTime_IsLessThan(kernelTime, pImpl->pFirstTimerRecord->fireTime)) ? LTTime_Subtract(pImpl->pFirstTimerRecord->fireTime, kernelTime) : LTTime_Zero();
            }
            else kernelTime = LTTime_Infinite();

#if 0           /* DRW: 18-Jul-2024 disabling max wait 200ms hack */
                if (kernelTime.nNanoseconds > 200000000) kernelTime.nNanoseconds = 200000000;
                /* DRW : LT-166: temporary hack to always wake up at least every 200ms
                         to unblock so we will re-loop to check again for any ISR messages posted.
                         If an ISR message is delivered after we check for and move them
                         to the task proc queue above, but before the Wait is enacted, we will miss it.  */
#endif

            LTThread_ThreadState threadState = LTThreadImpl_GetCurrentThreadState();
            LTThreadImpl_SetCurrentThreadState(kLTThread_ThreadState_WaitBlocked);
            LTKMonitorWait(pImpl->pMonitor, kernelTime.nNanoseconds);
            if ((u32)kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState)) break;
            LTThreadImpl_SetCurrentThreadState(threadState);
            if (NULL == pImpl->pTaskProcQueueHead) continue;
        }
        // dispatch and dispose of a single TaskProcRecord
        pNext = pImpl->pTaskProcQueueHead;
        if (NULL == (pImpl->pTaskProcQueueHead = pNext->pNext)) pImpl->pTaskProcQueueTail = NULL;
        pImpl->nTaskProcRecordsQueued--;
        LTKMonitorExit(pImpl->pMonitor);
        reason = kLTThread_ReleaseReason_TaskProcComplete;
        if ((LT_SIZE)pNext->pTaskProc > kLTThread_ReleaseReason_Last) {
            LTTRACE_NUMERIC(proc, 1, LTTRACE_PTR(pNext->pTaskProc));
            pNext->pTaskProc(pNext->pClientData);
            LTTRACE_NUMERIC(proc, 0, LTTRACE_PTR(pNext->pTaskProc));
        } else if (pNext->pTaskProc) {
            reason = (LTThread_ReleaseReason)((LT_SIZE)pNext->pTaskProc);
        }
        if (pNext->pClientDataReleaseProc) pNext->pClientDataReleaseProc(reason, pNext->pClientData);
        // free the TaskProcRecord
        LTTRACE_MEM_OP(kLTTrace_MemOp_Free_TaskProcRecord, sizeof(*pNext));
        core_free(pNext);
        LTKMonitorEnter(pImpl->pMonitor);
    }

    LT_ASSERT((u32)kLTThread_ThreadState_TerminatePending == LTAtomic_Load(&pImpl->nThreadState));

    // perform cleanup, exit the local monitor
    LTKMonitorExit(pImpl->pMonitor);

    // thread loop complete, disable ISR TaskProcRecord posting, call any client data complete process  and return any intermediately queued interrupt TaskProcRecords to the free pool
    nMask = LTKDisableInterrupts();
    LTAtomic_FetchAnd(&pImpl->nThreadFlags, ~kLTThread_FlagsCanPostFromISR);
    if (pImpl->pFirstISRTaskProcRecord) {
        LTKEnableInterrupts(nMask);
        pCurrISR = pImpl->pFirstISRTaskProcRecord;
        while (pCurrISR) {
            if (pCurrISR->pClientDataReleaseProc) pCurrISR->pClientDataReleaseProc(kLTThread_ReleaseReason_TaskProcPurged, pCurrISR->pClientData);
            pCurrISR = pCurrISR->pNext;
        }
        nMask = LTKDisableInterrupts();
        while (pImpl->pFirstISRTaskProcRecord) {
            pCurrISR = pImpl->pFirstISRTaskProcRecord->pNext;
            pImpl->pFirstISRTaskProcRecord->pNext = s_pISRTaskProcRecordFreePool;
            s_pISRTaskProcRecordFreePool = pImpl->pFirstISRTaskProcRecord;
            pImpl->pFirstISRTaskProcRecord = pCurrISR;
        }
    }
    LTKEnableInterrupts(nMask);

#if 0
//#ifdef LT_DEBUG
    nMask = LTThreadImpl_IsProhibitLoggingFlagSet() ? LTThreadImpl_SetProhibitLoggingFlag(false), 1 : 0;
    LTLOG("threadmain", "%s exiting with %d total allocated TaskProcRecords (%d queued, %d in free pool)",
        pImpl->name, (int)(pImpl->nTaskProcRecordsQueued + pImpl->nFreeTaskProcRecords), pImpl->nTaskProcRecordsQueued, pImpl->nFreeTaskProcRecords);
    if (nMask) LTThreadImpl_SetProhibitLoggingFlag(true);
//#endif
#endif

    // purge all of the remaining queued TaskProcRecords by calling their pClientDataReleaseProc
    // safe to access them outside monitor when state is terminate pending
    while (pImpl->pTaskProcQueueHead) {
        pNext = pImpl->pTaskProcQueueHead;
        pImpl->pTaskProcQueueHead = pNext->pNext;
        if (pNext->pClientDataReleaseProc) pNext->pClientDataReleaseProc(kLTThread_ReleaseReason_TaskProcPurged, pNext->pClientData);
        LTTRACE_MEM_OP(kLTTrace_MemOp_Free_TaskProcRecord, sizeof(*pNext));
        core_free(pNext);
        pImpl->nTaskProcRecordsQueued--;
    }
    pImpl->pTaskProcQueueTail = NULL;
    LT_ASSERT(0 == pImpl->nTaskProcRecordsQueued);

    // free all of the timer records; safe to access the list outside of the monitor
    while (pImpl->pFirstTimerRecord) {
        pTimerRecord = pImpl->pFirstTimerRecord;
        pImpl->pFirstTimerRecord = pTimerRecord->pNext;
        if (pTimerRecord->pClientDataReleaseProc) pTimerRecord->pClientDataReleaseProc(kLTThread_ReleaseReason_TimerPurge, pTimerRecord->pClientData);
        LTTRACE_MEM_OP(kLTTrace_MemOp_Free_TimerRecord, sizeof(*pTimerRecord));
        core_free(pTimerRecord);
    }

    // call the ThreadExit proc, if exists
    if (pImpl->pExitProc) pImpl->pExitProc();

    // free all of the thread specific client data records; safe to do this outside of the monitor
    // do this after calling the exit proc so that clients can retrieve their client data in their exit procs
    LTThreadImpl_ReleaseThreadSpecificClientData(pImpl);

#if 0
#ifdef LT_DEBUG
    nMask = LTThreadImpl_IsProhibitLoggingFlagSet() ? LTThreadImpl_SetProhibitLoggingFlag(false), 1 : 0;
    LTLOG_DEBUG("threadmain", "Heap Usage: current=%lu bytes, max=%lu bytes", LT_Pu32(LTAtomic_Load(&pImpl->nCurrHeapUsage)), LT_Pu32(LTAtomic_Load(&pImpl->nMaxHeapUsage)));
    if (nMask) LTThreadImpl_SetProhibitLoggingFlag(true);
#endif
#endif

    // take the thread out of the thread table
    // must enter global monitor before local monitor, then remove record and exit local monitor, then global
    // TODO: should probably move this to the envisioned bsp 'thread really gone' callback
    LTKMonitorEnter(s_pMonitor);
        LTKMonitorEnter(pImpl->pMonitor);
            #if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
                DumpThreadListInternal("Terminate 1", pImpl);
            #endif
            if (s_pThreadsRunningListHead == pImpl) {
                LT_ASSERT(NULL == pImpl->pPrev);
                if (NULL != (s_pThreadsRunningListHead = pImpl->pNext)) {
                    LT_ASSERT(pImpl->pNext->pPrev == pImpl);
                    pImpl->pNext->pPrev = NULL;
                    pImpl->pNext = NULL;
               }
            }
            else {
                LT_ASSERT(NULL != pImpl->pPrev);
                LT_ASSERT(pImpl->pPrev->pNext = pImpl);
                if (NULL != (pImpl->pPrev->pNext = pImpl->pNext)) {
                    LT_ASSERT(pImpl->pNext->pPrev == pImpl);
                    pImpl->pNext->pPrev = pImpl->pPrev;
                    pImpl->pNext = NULL;
                }
                pImpl->pPrev = NULL;
            }
            #if LTHREADIMPL_DUMP_THREAD_LIST_ACTIVITY
                DumpThreadListInternal("Terminate 2", pImpl);
            #endif
            LTAtomic_Store(&pImpl->nThreadState, kLTThread_ThreadState_Terminated);

        LTKMonitorExit(pImpl->pMonitor);
        if (! LTThreadImpl_ClientThreadsAreRunningInternal()) LTKMonitorNotify(s_pMonitor);
    LTKMonitorExit(s_pMonitor);
    LTThreadImpl_ReleaseThreadImpl(hThread, pImpl);
}

/*_______________________________________
  \_____________________________________/\
  /_____________________________________\/
  \_____________________________________/\
  /_____________________________________\/
  \_____                           _____/\
  /_____ LTOThread LTThread Object _____\/
  \_____________________________________/\
  /_____________________________________\/
  \___ You have no chance to survive ___/\
  /________ make your LTTime! __________\/
  \_____________________________________/\
  /_____________________________________\/
  \_____________________________________/\
  /_____________________________________\/
  \____________________________________*/

/*_____________________________
  LTOThread object constructors */
static bool LTOThreadImpl_ConstructObject(LTOThreadImpl *thread) {
    if (0 != (thread->hThread = LTThreadImpl_CreateThread(" "))) {
        LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(thread->hThread);
        if (pImpl) {
            pImpl->pThreadObject = (LTOThread *)thread;
            LTThreadImpl_ReleaseThreadImpl(thread->hThread, pImpl);
        }
        return true;
    }
    return false;
}

static void LTOThreadImpl_DestructObject(LTOThreadImpl *thread) {
    LTHandle_DestroyHandle(thread->hThread);
}

/*_________________________________
  LTOThread public api functions */
static void   LTOThreadImpl_Start(LTOThreadImpl *thread, const char * pName, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc) {
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(thread->hThread);
    if (pImpl) {
        LTKMonitorEnter(s_pMonitor);
        if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted) LTStdlibImpl_strncpyTerm(pImpl->name, (pName && *pName) ? pName : "<unnamed>", sizeof(pImpl->name));
        LTKMonitorExit(s_pMonitor);
        LTThreadImpl_ReleaseThreadImpl(thread->hThread, pImpl);
    }
    LTThreadImpl_Start(thread->hThread, pInitProc, pExitProc);
}
static bool   LTOThreadImpl_StartSynchronous(LTOThreadImpl *thread, const char * pName, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc) {
    LTThreadImpl * pImpl = LTThreadImpl_ReserveThreadImpl(thread->hThread);
    if (pImpl) {
        LTKMonitorEnter(s_pMonitor);
        if (LTAtomic_Load(&pImpl->nThreadState) == kLTThread_ThreadState_NotStarted) LTStdlibImpl_strncpyTerm(pImpl->name, (pName && *pName) ? pName : "<unnamed>", sizeof(pImpl->name));
        LTKMonitorExit(s_pMonitor);
        LTThreadImpl_ReleaseThreadImpl(thread->hThread, pImpl);
    }
    return LTThreadImpl_StartSynchronous(thread->hThread, pInitProc, pExitProc);
}
static bool   LTOThreadImpl_QueueTaskProc(LTOThreadImpl *thread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) { return LTThreadImpl_QueueTaskProc(thread->hThread, pTaskProc, pClientDataReleaseProc, pClientData); }
static bool   LTOThreadImpl_QueueTaskProcIfRequired(LTOThreadImpl *thread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) LT_ISR_SAFE { return LTThreadImpl_QueueTaskProcIfRequired(thread->hThread, pTaskProc, pClientDataReleaseProc, pClientData); }
static void   LTOThreadImpl_GetName(LTOThreadImpl *thread, char nameBuffToFill[kLTThread_MaxNameBuff]) { LTThreadImpl_GetName(thread->hThread, nameBuffToFill); }
static void * LTOThreadImpl_GetThreadSpecificClientData(LTOThreadImpl *thread, const char *pKey) { return LTThreadImpl_GetThreadSpecificClientData(thread->hThread, pKey); }
static u32    LTOThreadImpl_GetStackSize(LTOThreadImpl *thread) { return LTThreadImpl_GetStackSize(thread->hThread); }
static void   LTOThreadImpl_GetStackUsage(LTOThreadImpl *thread, u32 *pStackSizeToSet, u32 *pCurrentStackUsageToSet, u32 *pMaxStackUsageToSet) { LTThreadImpl_GetStackUsage(thread->hThread, pStackSizeToSet, pCurrentStackUsageToSet, pMaxStackUsageToSet); }
static void   LTOThreadImpl_GetHeapUsage(LTOThreadImpl *thread, u32 *pCurrentHeapUsageToSet, u32 *pMaxHeapUsageToSet) { LTThreadImpl_GetHeapUsage(thread->hThread, pCurrentHeapUsageToSet, pMaxHeapUsageToSet); }
static u8     LTOThreadImpl_GetPriority(LTOThreadImpl *thread) { return LTThreadImpl_GetPriority(thread->hThread); }
static void   LTOThreadImpl_GetSnapshot(LTOThreadImpl *thread, LTThread_Snapshot *pSnapshotToFill) { LTThreadImpl_GetSnapshot(thread->hThread, pSnapshotToFill); }
static bool   LTOThreadImpl_IsSystemThread(LTOThreadImpl *thread) { return LTThreadImpl_IsSystemThread(thread->hThread); }
static bool   LTOThreadImpl_IsSystemThreadTimeCritical(LTOThreadImpl *thread) { return LTThreadImpl_IsSystemThreadTimeCritical(thread->hThread); }
static bool   LTOThreadImpl_IsCurrentThread(LTOThreadImpl *thread) { return LTThreadImpl_IsCurrentThread(thread->hThread); }
static void   LTOThreadImpl_SetThreadSpecificClientData(LTOThreadImpl *thread, const char *pKey, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) { LTThreadImpl_SetThreadSpecificClientData(thread->hThread, pKey, pClientDataReleaseProc, pClientData); }
static void   LTOThreadImpl_SetStackSize(LTOThreadImpl *thread, u32 nStackSize) { LTThreadImpl_SetStackSize(thread->hThread, nStackSize); }
static void   LTOThreadImpl_SetPriority(LTOThreadImpl *thread, u8 nPriority) { LTThreadImpl_SetPriority(thread->hThread, nPriority); }
static void   LTOThreadImpl_Terminate(LTOThreadImpl *thread) { LTThreadImpl_Terminate(thread->hThread); }
static bool   LTOThreadImpl_WaitUntilFinished(LTOThreadImpl *thread, LTTime timeout) { return LTThreadImpl_WaitUntilFinished(thread->hThread, timeout); }
static bool   LTOThreadImpl_IsTerminatePending(LTOThreadImpl *thread) { return LTThreadImpl_IsTerminatePending(thread->hThread); }
static void   LTOThreadImpl_SetTimer(LTOThreadImpl *thread, LTTime timeout, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) { LTThreadImpl_SetTimer(thread->hThread, timeout, pTimerProc, pClientDataReleaseProc, pClientData); }
static void   LTOThreadImpl_SetTimerAbsolute(LTOThreadImpl *thread, LTTime kernelStartTime, LTTime deadlinePeriod, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) { LTThreadImpl_SetTimerAbsolute(thread->hThread, kernelStartTime, deadlinePeriod, pTimerProc, pClientDataReleaseProc, pClientData); }
static void   LTOThreadImpl_KillTimer(LTOThreadImpl *thread, LTThread_TimerProc *pTimerProc, void *pClientData) { LTThreadImpl_KillTimer(thread->hThread, pTimerProc, pClientData); }
static void   LTOThreadImpl_SetAsWakeupTimer(LTOThreadImpl *thread, LTThread_TimerProc *pTimerProc, void *pClientData) { LTThreadImpl_SetAsWakeupTimer(thread->hThread, pTimerProc, pClientData); }
static LTTime LTOThreadImpl_GetTimerExpirationKernelTime(LTOThreadImpl *thread, LTThread_TimerProc *pTimerProc, void *pClientData) { return LTThreadImpl_GetTimerExpirationKernelTime(thread->hThread, pTimerProc, pClientData); }
static void   LTOThreadImpl_RestartTimer(LTOThreadImpl *thread, LTThread_TimerProc *pTimerProc, void *pClientData) { LTThreadImpl_RestartTimer(thread->hThread, pTimerProc, pClientData);  }
static void   LTOThreadImpl_Sleep(LTTime sleepTime) { LTThreadImpl_Sleep(sleepTime); }
static void   LTOThreadImpl_Yield(void) { LTThreadImpl_Yield(); }
static void   LTOThreadImpl_SnapshotRunningThreads(LTThread_SnapshotRunningThreadsCB *pSnapshotRunningThreadsCB, void *pClientData) { LTThreadImpl_SnapshotRunningThreads(pSnapshotRunningThreadsCB, pClientData); }
static u32    LTOThreadImpl_GetDefaultStackSize(void) { return LTThreadImpl_GetDefaultStackSize(); }
static LTThread  LTOThreadImpl_GetThreadHandle(LTOThreadImpl *thread) { return thread->hThread; }
static LTThread_ThreadState LTOThreadImpl_GetThreadState(LTOThreadImpl *thread) { return LTThreadImpl_GetThreadState(thread->hThread); }
LTOThread *   LTOThreadImpl_GetCurrentThread(void) {
    LTOThread * pCurrentThreadObject = NULL;
    LTThread hCurrentThread = LTThreadImpl_GetCurrentThread();
    LTThreadImpl * pImpl = hCurrentThread ? LTThreadImpl_ReserveThreadImpl(hCurrentThread) : NULL;
    if (pImpl) {
        pCurrentThreadObject = pImpl->pThreadObject;
        LTThreadImpl_ReleaseThreadImpl(hCurrentThread, pImpl);
    }
    return pCurrentThreadObject;
}
static const char * LTOThreadImpl_ThreadStateToString(LTThread_ThreadState threadState) { return LTThreadImpl_ThreadStateToString(threadState);  }

/*___________________________________
  LTOThread LTObjectApi definition */
define_LTObjectImplPublic(LTOThread, LTOThreadImpl,
    Start,
    StartSynchronous,
    QueueTaskProc,
    QueueTaskProcIfRequired,
    GetName,
    GetThreadSpecificClientData,
    GetStackSize,
    GetStackUsage,
    GetHeapUsage,
    GetPriority,
    GetSnapshot,
    IsSystemThread,
    IsSystemThreadTimeCritical,
    IsCurrentThread,
    SetThreadSpecificClientData,
    SetStackSize,
    SetPriority,
    Terminate,
    WaitUntilFinished,
    IsTerminatePending,
    SetTimer,
    SetTimerAbsolute,
    GetTimerExpirationKernelTime,
    KillTimer,
    RestartTimer,
    SetAsWakeupTimer,
    Sleep,
    Yield,
    SnapshotRunningThreads,
    GetDefaultStackSize,
    GetThreadHandle,
    GetThreadState,
    GetCurrentThread,
    ThreadStateToString
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Aug-19   augustus    created
 *  19-Aug-19   augustus    added ResetTimer() functions
 *  06-Sep-19   augustus    axed PostTaskProcRecordFromInterruptHandler(); now PostTaskProcRecord() handles both cases
 *  21-Oct-19   augustus    added bCanPostFromISR
 *  24-Nov-19   augustus    init thread specific PCG random number generator state variables
 *  24-Feb-20   augustus    added RunPreGenesisWorkerThread()
 *  13-Mar-20   augustus    Enable/Disable uses nMask
 *  09-Jun-20   augustus    re-created in C from LTCoreImplBase.h
 *  28-Jul-20   augustus    memset instance data to 0 on alloc, don't call bsp to wait or uninit instance data unless thread was run
 *  05-Aug-20   augustus    Logger is now an LTHandle not an LTObject; added IsTerminatePending
 *  20-Aug-20   augustus    added LTThreadImpl_ExecuteAsyncProcedure and LTThreadImpl_InvokeAsyncAPI
 *  26-Aug-20   augustus    retooled to execute queued TaskProcs instead of being message queue based
 *                          added LTThread_ThreadState and LTThread_Snapshot and associated functions
 *  30-Sep-20   augustus    added SetProhibitLoggingFlag and IsProhibitLoggingFlagSet
 *  11-Oct-20   augustus    Start() now takes InitProc and ExitProc
 *                          SetRealtimePriority and SetNonRealtime now return previous priority
 *  23-Dec-20   augustus    added IsSystemThread and IsCurrentThread, prevent public termination
 *                          and priority setting of system threads
 *  30-Dec-20   augustus    added TerminateAllNonSystemThreads
 *  05-Feb-21   augustus    added QueueTaskProcIfRequired
 *  18-Jun-21   augustus    added back IsTerminatePending()
 *  22-Jun-21   tiberius    clean up thread priorities
 *  22-Jul-21   augustus    removed possibility of logging from GetName() and made it report non lt-threads
 *  10-Aug-21   augustus    don't use LTLOG for logging ISR queue task proc failures in ISR mode
 *  09-Sep-21   tiberius    simplify thread priority API
 *  20-Oct-21   augustus    redid Timers to be based on procedure and clientdata, not timer id, allow setting on arbitrary threads
 *  18-Dec-22   augustus    use LTKThreadGetDefaultStackSize() for the default stack sizeof
 *  30-Apr-24   augustus    added GetTimerExpirationKernelTime
 *  05-May-24   augustus    added GetThreadHandle to LTOThread
 *  26-Mar-25   augustus    added StartSynchronous
*/
