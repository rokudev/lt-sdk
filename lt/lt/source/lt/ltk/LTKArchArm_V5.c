/******************************************************************************
 * lt/source/lt/ltk/LTKArchArm_V5.c                             LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"

/* Kernel ABI version */
#define LTK_ABI_VERSION     "arm.v5.1"

/*****************
 * Configuration */
static LTCoreBSP_ArmV5_SystemConfig * s_pConfig;

/***********************
 * Timer and Tick Data */
static s64     s_nLastCycleCount = 0;
static u32     s_nTick = 0;
static LTKList s_timerQueue;
static u16     s_nClockSpeedMHz;

/*******************
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

/******************
 * Interrupt Data */
static bool    s_bSchedulerStarted;
volatile u32   _LTK_interruptNestingCounter = 0;

/**************
 * Prototypes */
static void IdleThread(void *);

/***********************
 * Hooks and Callbacks */
static LTKFaultCallback * s_pFaultCallback = NULL;

/************************
 * Forward declarations */
static void LTKTimerInterruptHandler(u32 nTickAdvance) LT_ISR_SAFE;

/***********
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk");

/*************************************************
 * (Too many) Atomic Built-in Function Stand-ins */

void
__sync_synchronize(void) {
    DataSynchronizationBarrier();
}

unsigned
__atomic_fetch_add_4(volatile void * pValue, unsigned nAddValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue + nAddValue;
    DataSynchronizationBarrier();
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_sub_4(volatile void * pValue, unsigned nSubValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue - nSubValue;
    DataSynchronizationBarrier();
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_and_4(volatile void * pValue, unsigned nAndValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue & nAndValue;
    DataSynchronizationBarrier();
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_or_4(volatile void * pValue, unsigned nOrValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue | nOrValue;
    DataSynchronizationBarrier();
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned LT_VERBATIM
__atomic_exchange_4(volatile void * pValue, unsigned nExchValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nExchValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "swp r2, r1, [r0]            \n\
        mcr p15, 0, r0, c7, c10, 4  \n\
        mov r0, r2                  \n\
        bx lr"
          : : : "r0", "r1", "r2", "memory"
    );
}

bool
__atomic_compare_exchange_4(volatile void * pValue, void * pExpected, unsigned nDesired,
                               bool bWeak, int nOrderSuccess, int nOrderFail) {
    LT_UNUSED(bWeak); LT_UNUSED(nOrderSuccess); LT_UNUSED(nOrderFail);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    u32 nExpected = *((u32 *)pExpected);
    if (nValue == nExpected) {
        *((u32 *)pValue) = nDesired;
        DataSynchronizationBarrier();
        EnableInterruptsNested(nMask);
        return true;
    } else {
        *((u32 *)pExpected) = nValue;
        DataSynchronizationBarrier();
        EnableInterruptsNested(nMask);
        return false;
    }
}

/**********************************
 * LTK Initialization and Startup */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback) {
    LTK_LTLOG_INITIALIZE(pLTCoreLogFunction);
    s_pFaultCallback = pFaultCallback;
    s_pConfig = (LTCoreBSP_ArmV5_SystemConfig *)pBSP->pLTSystemConfig;
    s_nClockSpeedMHz = s_pConfig->nClockSpeedHz / 1000000;
    LTKList_Init(&s_timerQueue);
    _LTKInitialize(pBSP, pLTCoreLogFunction, &IdleThread);
}

/********
 * Time */
s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    s64 nCycleCount = (s64)s_pConfig->pGetCycleCount();
    return (s64)((nCycleCount * (u64)1000) / s_nClockSpeedMHz);
}

s64 LTKGetHighFrequencyCounterNanosecondResolution(void) LT_ISR_SAFE {
    return (s64)(((u64)1000) / s_nClockSpeedMHz);
}

bool _LTKSetTimeout(u64 nNanosecondsToWait) {
    if (nNanosecondsToWait > (u64)kLTKNanosecondsPerTick * 0x7ffffffe) return false;
    u32 nTicks = (nNanosecondsToWait + kLTKNanosecondsPerTick - 1) / kLTKNanosecondsPerTick;
    _LTK_pRunningThread->nWakeTick = s_nTick + nTicks;
    return true;
}

void LTKAdvanceTickCount(u32 nTicks) {
    if (nTicks) {
        u32 nMask = DisableInterruptsNested();
        LTKTimerInterruptHandler(nTicks);
        YieldThread();
        EnableInterruptsNested(nMask);
    }
}

/**************
 * Interrupts */
void LTKSetInterruptVector(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler, LTCore_InterruptPriority priority) {
    LT_UNUSED(nInterrupt);
    LT_UNUSED(pInterruptHandler);
    LT_UNUSED(priority);
}

void LTKSetInterruptPriority(u32 nInterrupt, LTCore_InterruptPriority priority) {
    LT_UNUSED(nInterrupt);
    LT_UNUSED(priority);
}

static void LTKTimerInterruptHandler(u32 nTickAdvance) LT_ISR_SAFE {
    s_nTick += nTickAdvance;
    // Process timer queue
    //  Timer queues can contain threads or message timers
    LTKList_Node * pElmSave;
    for (LTKList_Node * pElm = s_timerQueue.pNext; pElm != &s_timerQueue; pElm = pElmSave) {
        pElmSave = pElm->pNext;
        LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, timerLink);
        s32 nRemTicks = (s32)pThread->nWakeTick - s_nTick;
        if (nRemTicks <= 0) {
            if (pThread->nState == kLTKThreadState_WaitForMonitorOrTick) {
                pThread->pMonitor->pWaiter = kLTKNoSuchThread;
            }
            LTKList_Remove(pElm);
            LTKList_Remove(&pThread->runLink);
            LTKList_AddTail(&_LTK_runQueues[pThread->nPriority], &pThread->runLink);
            SetThreadState(pThread, kLTKThreadState_Runnable);
            pThread->nTimedOut = 1;
        } else break;
    }
}

void * _LTKDispatcher(void * pStack, u32 dispatchMode) LT_ISR_SAFE {
    _LTK_interruptNestingCounter++;
    if (dispatchMode == LTK_DISPATCH_MODE_SWI) {
        s_bSchedulerStarted = true;
    } else {
        u32 nTickAdvance = s_pConfig->pDispatcher();
        if (nTickAdvance) LTKTimerInterruptHandler(nTickAdvance);
    }
    // Scheduler
    if (s_bSchedulerStarted && dispatchMode < LTK_DISPATCH_MODE_FIQ_NOSCHEDULER) {
        // borrow and repurpose dispatchMode to mean idle thread was (1) or wasn't (0) running
        dispatchMode = 0; // initialize with idle thread wasn't running (0)
        if (_LTK_pRunningThread) {
            _LTK_pRunningThread->pStack = pStack;
            _LTKStackCheck(_LTK_pRunningThread);
            s64 nCycleCount = (s64)s_pConfig->pGetCycleCount();
            _LTK_pRunningThread->nCycleCount += (nCycleCount - s_nLastCycleCount);
            s_nLastCycleCount = nCycleCount;
            // if idle thread running mark it and call the idle tick hook
            if (_LTK_pRunningThread == &_LTK_idleThread) {
                dispatchMode = 1; // mark idle thread was running (1)
                (*s_pIdleTickHookProc)();
            }
        } else {
            _LTK_pRunningThread = &_LTK_idleThread;
        }
        if (_LTK_pRunningThread->nState >= kLTKThreadState_WaitForTick) {
            // Update running thread timer state (insertion sort in timer queue)
            s32 nRemTicks = (s32)_LTK_pRunningThread->nWakeTick - s_nTick;
            LTKList_Node * pElm;
            for (pElm = s_timerQueue.pNext; pElm != &s_timerQueue; pElm = pElm->pNext) {
                LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, timerLink);
                s32 nTmrRemTicks = (s32)pThread->nWakeTick - s_nTick;
                if (nRemTicks <= nTmrRemTicks) break;
            }
            LTKList_InsertBefore(pElm, &_LTK_pRunningThread->timerLink);
            // If thread is only waiting for a tick
            if (_LTK_pRunningThread->nState == kLTKThreadState_WaitForTick)
                LTKList_Remove(&_LTK_pRunningThread->runLink);
        }
        // Process Priority Queues
        // Start scan at first thread of highest priority, looking for first
        //  thread of list, and if no threads are runnable schedule idle thread.
        LTKThread * pRunThread = NULL;
        for (s8 nPriority = kLTKNumThreadPriorities - 1; nPriority >= 0; nPriority--) {
            if (!LTKList_IsEmpty(&_LTK_runQueues[nPriority])) {
                pRunThread = LT_CONTAINER_OF(_LTK_runQueues[nPriority].pNext, LTKThread, runLink);
                break;
            }
        }
        if (pRunThread) {
            // Round-robin
            if (!LTKList_IsTail(&_LTK_runQueues[pRunThread->nPriority], &pRunThread->runLink))
                LTKList_MoveToEnd(&_LTK_runQueues[pRunThread->nPriority], &pRunThread->runLink);
            _LTK_pRunningThread = pRunThread;
        } else _LTK_pRunningThread = &_LTK_idleThread;
        // Call idle transition hooks if required
        if (dispatchMode) {
            // Idle thread was running, see if it isn't anymore
            if (_LTK_pRunningThread != &_LTK_idleThread) (*s_pExitIdleHookProc)();
        } else {
            // Idle thread wasn't running, see if it now will be
            if (_LTK_pRunningThread == &_LTK_idleThread) (*s_pEnterIdleHookProc)();
        }
        _LTK_interruptNestingCounter--;
        return _LTK_pRunningThread->pStack;
    }
    _LTK_interruptNestingCounter--;
    return pStack;
}

void LTKDebugBreak(void) {
    LTK_LTLOG_STOMP("cat: all.your.base", "are belong to us");
    asm volatile ( "udf 0" );
    while (1);
    /* Unreachable */
}

/***********
 * Threads */
static void IdleThread(void * pArg) {
    LT_UNUSED(pArg);
    while (1) {
        asm volatile( "mcr p15, 0, r0, c7, c0, 4" : : : "memory" );
    }
}

void
_LTKInitThread(LTKThread * pThread, u8 nPriority, void * pEntry, s32 nArg,
                  u8 * pStackBottom, u32 nStackSize) {
    u8 * pStack = pStackBottom + nStackSize;
    // Insert canary
    pStack -= sizeof(u32);
    *((u32 *)pStack) = kLTKStackFillValue;
    // Align to 8 bytes
    pStack = (u8 *)((u32)pStack & 0xfffffff8);
    // Fill out initial frame
    LTKStackFrame * pStackFrame = (LTKStackFrame *)pStack - 1;
    pStackFrame->nRegister[LTK_FRAME_WORD(CPSR)] = LTK_ARCH_ARM_V5_START_CPSR;
    pStackFrame->nRegister[LTK_FRAME_WORD(R0)]   = (u32)nArg;
    pStackFrame->nRegister[LTK_FRAME_WORD(PC)]   = (u32)pEntry;
    pStackFrame->nRegister[LTK_FRAME_WORD(LR)]   = (u32)&_LTKThreadExit;
    // Fill lower stack
    u32 * pFill = (u32 *)pStackFrame - 1;
    for (; pFill >= (u32 *)pStackBottom; pFill--) {
        *pFill = kLTKStackFillValue;
    }
    // Initialize context and state
    pThread->pStack           = (u8 *)pStackFrame;
    pThread->nCycleCount      = 0;
    pThread->nPriority        = nPriority;
    pThread->nNominalPriority = nPriority;
    pThread->pStackBottom     = pStackBottom;
    pThread->nStackSize       = nStackSize;
}

void LTKThreadInitializeAndStartScheduler(void * pThread, u8 nPriority,
                                             u32 nStackSize, const char * pName,
                                             void (* pInitialThreadProc)(void * pClientData),
                                             void * pClientData) {
    LTKThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pInitialThreadProc, pClientData);
    /* At this point interrupts should be disabled and CPU should be in system mode. */
    YieldThread();
    while (1);
}

s64 LTKThreadGetCpuTime(void * pInstance) {
    LTKThread * pThread = (LTKThread *)pInstance;
    u32 nMask = LockScheduler();
    s64 nCycleCount = pThread->nCycleCount;
    if (pThread == _LTK_pRunningThread) {
        nCycleCount += ((s64)s_pConfig->pGetCycleCount() - s_nLastCycleCount);
    }
    UnlockScheduler(nMask);
    return (nCycleCount * 1000) / s_nClockSpeedMHz;
}

/*****************
 * Fault Handler */
static void DumpStackFrame(u32 * pStackFrame, u32 * pStack) LT_ISR_SAFE {
    for (u32 nIx = 2; nIx < 13; nIx += 4) {
        LTK_LTLOG_STOMP("stack", " %08X %08X %08X %08X (r%u - r%u)", pStackFrame[nIx], pStackFrame[nIx + 1],
                            pStackFrame[nIx + 2], pStackFrame[nIx + 3], nIx - 2, nIx + 1);
    }
    LTK_LTLOG_STOMP("stack", " %08X %08X %08X          (r12, cpsr, sp)", pStackFrame[14], pStackFrame[LTK_FRAME_WORD(CPSR)], pStack);
    LTK_LTLOG_STOMP("stack", " %08X %08X                   (pc, lr)",    pStackFrame[LTK_FRAME_WORD(PC)],  pStackFrame[LTK_FRAME_WORD(LR)]);
}

void _LTKFaultHandler(u32 * pStackFrame, u32 * pStack, u32 nContext) LT_ISR_SAFE {
    char * pFaultName = "Illegal Instruction";
    if (nContext == LTK_FAULT_PREFETCH_ABORT) {
        pFaultName = "Prefetch Abort";
    } else if (nContext == LTK_FAULT_DATA_ABORT) {
        pFaultName = "Data Abort";
    }
    u32 nFSR, nFAR;
    asm volatile (
       "mrc p15, 0, %0, c5, c0, 0     \n\
        mrc p15, 0, %1, c6, c0, 0"
          : "=r" (nFSR), "=r" (nFAR) : :
    );
    LTK_LTLOG_STOMP("fault", "\n*** Fault: %s FSR: %03X FAR: %08X ***\n", pFaultName, nFSR, nFAR);
    u32 * pInterruptStackFrame = NULL;
    if (_LTK_interruptNestingCounter) {
        LTK_LTLOG_STOMP("fault", "\nInterrupt Stack Frame @%08X: (nest level: %u)", pStackFrame, _LTK_interruptNestingCounter);
        DumpStackFrame(pStackFrame, pStack);
        pInterruptStackFrame = pStackFrame;
        if (_LTK_pRunningThread) {
            pStackFrame = (u32 *)_LTK_pRunningThread->pStack;
            pStack      = pStackFrame;
        }
    } else if (_LTK_pRunningThread) {
        /* the case where an exception happened while in the thread context, so the thread stack
         * frame is saved on an exception mode stack, pointed to by pStackFrane. We have to copy a
         * frame on top of the thread stack, so that we dump the right content to the coredump. */
        /* We have to adjust pStack for the running thread because there was no interrupt, and the
         * current value for _LTK_pRunningThread->pStack is an invalid one. */
        _LTK_pRunningThread->pStack = (u8 *)pStack;
        _LTK_pRunningThread->pStack -= sizeof(LTKStackFrame);
        for (u8 i = 0; i < sizeof(LTKStackFrame) / 4; i++) {
            *((u32 *)_LTK_pRunningThread->pStack + i) = *(pStackFrame + i);
        }
        pStackFrame = (u32 *)_LTK_pRunningThread->pStack;
        pStack      = pStackFrame;
    }
    if (_LTK_pRunningThread) {
        LTK_LTLOG_STOMP("fault", "\nThread Stack Frame @%08X: (%s)", pStackFrame,
                        _LTK_pRunningThread->pName);
        DumpStackFrame(pStackFrame, pStack);
        /* Fix up stack pointer to correct new frame */
        _LTK_pRunningThread->pStack = (u8 *)pStackFrame;
    } else if (!_LTK_interruptNestingCounter) {
        /* This is an early exception before the scheduler was started */
        LTK_LTLOG_STOMP("fault", "\nStack Frame @%08X:");
        DumpStackFrame(pStackFrame, pStack);
    }
    if (s_pFaultCallback) {
         /* LT Core handler will check _LTK_interruptNestingCounter by calling
          * LTKInsideInterruptContext before writing crashdump, and proceed only if
          * _LTK_interruptNestingCounter is >0. However, Anyka LTK does not increment this counter
          * when an exception occurs in a thread context. We have to increment the counter here for
          * this use case. We are not coming back so no reason to decrement it afterwards.
          * It's important that this "artificial" increment happens after we check the counter and
          * set pInterruptStackFrame to pStackFrame. If an exception did not occur in an interrupt
          * context, we must pass NULL for pInterruptStackFrame. Also there is another check just
          * above for the use case when an exception happens before the scheduler was started. */
        _LTK_interruptNestingCounter++;
        (*s_pFaultCallback)(LTK_ABI_VERSION, pInterruptStackFrame, sizeof(LTKStackFrame));
    }
    while (1);
    // unreachable
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Sep-23   tiberius    created
 */
