/******************************************************************************
 * lt/source/lt/ltk/LTKArchRISC_V.c                             LTK MicroKernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"

/*
 * TODO: __riscv_float_abi_soft / __riscv_float_abi_single - and Hard FP lazy context switch
 */

/* Kernel ABI version */
#define LTK_ABI_VERSION     "RISC_V.1"

/*****************
 * Configuration */
static LTCoreBSP_RISCV_SystemConfig * s_pConfig;
static u16                            s_nClockSpeedMHz;

/***********************
 * Timer and Tick Data */
static LTKList      s_timerQueue;
static volatile u32 s_nTick = 0;
static s64          s_nLastCycleCount = 0;

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
extern u8  * _LTK_pInterruptStackTop;
extern u32   _LTK_nTrapNestingCounter;

/**************
 * Prototypes */
static void IdleThread(void *);
void _LTKMachineTrapHandler(void);
static void LTKTimerInterruptHandler(u32 nTickAdvance);

/***********************
 * Hooks and Callbacks */
static LTKFaultCallback * s_pFaultCallback = NULL;

/***********
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk");

/*************************************************
 * (Too many) Atomic Built-in Function Stand-ins */

#ifndef __riscv_atomic

unsigned LT_VERBATIM
__atomic_load_4(const volatile void * pValue, int nOrder) {
    LT_USED_ARG(pValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "lw       a0, 0(a0)             \n\
        fence    r, r                  \n\
        ret"
         : : : "a0"
    );
}

void LT_VERBATIM
__atomic_store_4(volatile void * pValue, unsigned nStoreValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nStoreValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "sw       a1, 0(a0)             \n\
        fence    w, rw                 \n\
        ret"
         : : : "memory"
    );
}

unsigned LT_VERBATIM
__atomic_fetch_add_4(volatile void * pValue, unsigned nAddValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nAddValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "mv       t0, a0                \n\
        csrrci   t1, mstatus, 1 << 3   \n\
        lw       a0, 0(t0)             \n\
        add      t2, a0, a1            \n\
        sw       t2, 0(t0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t1           \n\
        ret"
         : : : "a0", "t0", "t1", "t2", "memory"
    );
}

unsigned LT_VERBATIM
__atomic_fetch_sub_4(volatile void * pValue, unsigned nSubValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nSubValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "mv       t0, a0                \n\
        csrrci   t1, mstatus, 1 << 3   \n\
        lw       a0, 0(t0)             \n\
        sub      t2, a0, a1            \n\
        sw       t2, 0(t0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t1           \n\
        ret"
         : : : "a0", "t0", "t1", "t2", "memory"
    );
}

unsigned LT_VERBATIM
__atomic_fetch_and_4(volatile void * pValue, unsigned nAndValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nAndValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "mv       t0, a0                \n\
        csrrci   t1, mstatus, 1 << 3   \n\
        lw       a0, 0(t0)             \n\
        and      t2, a0, a1            \n\
        sw       t2, 0(t0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t1           \n\
        ret"
         : : : "a0", "t0", "t1", "t2", "memory"
    );
}

unsigned LT_VERBATIM
__atomic_fetch_or_4(volatile void * pValue, unsigned nOrValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nOrValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "mv       t0, a0                \n\
        csrrci   t1, mstatus, 1 << 3   \n\
        lw       a0, 0(t0)             \n\
        or       t2, a0, a1            \n\
        sw       t2, 0(t0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t1           \n\
        ret"
         : : : "a0", "t0", "t1", "t2", "memory"
    );
}

unsigned LT_VERBATIM
__atomic_exchange_4(volatile void * pValue, unsigned nExchValue, int nOrder) {
    LT_USED_ARG(pValue); LT_USED_ARG(nExchValue);
    LT_UNUSED(nOrder);
    asm volatile (
       "mv       t0, a0                \n\
        csrrci   t1, mstatus, 1 << 3   \n\
        lw       a0, 0(t0)             \n\
        sw       a1, 0(t0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t1           \n\
        ret"
         : : : "a0", "t0", "t1", "memory"
    );
}

bool LT_VERBATIM
__atomic_compare_exchange_4(volatile void * pValue, void * pExpected, unsigned nDesired,
                               bool bWeak, int nOrderSuccess, int nOrderFail) {
    LT_USED_ARG(pValue); LT_USED_ARG(pExpected);   LT_USED_ARG(nDesired);
    LT_UNUSED(bWeak);    LT_UNUSED(nOrderSuccess); LT_UNUSED(nOrderFail);
    asm volatile (
       "csrrci   t0, mstatus, 1 << 3   \n\
        lw       t1, 0(a0)             \n\
        lw       t2, 0(a1)             \n\
        bne      t1, t2, NotEqual      \n\
        sw       a2, 0(a0)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t0           \n\
        li       a0, 1                 \n\
        ret                            \n\
      NotEqual:                        \n\
        sw       t1, 0(a1)             \n\
        fence    rw, rw                \n\
        csrw     mstatus, t0           \n\
        li       a0, 0                 \n\
        ret"
         : : : "a0", "a1", "t0", "t1", "t2", "memory"
    );
}

#endif // __riscv_atomic

/**********************************
 * LTK Initialization and Startup */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback) {
    LTK_LTLOG_INITIALIZE(pLTCoreLogFunction);
    s_pFaultCallback = pFaultCallback;
    s_pConfig = (LTCoreBSP_RISCV_SystemConfig *)pBSP->pLTSystemConfig;
    // Ensure stack is aligned to a 16 byte boundary.
    _LTK_pInterruptStackTop = (u8 *)((u32)s_pConfig->pStackTop & 0xfffffff0);
    // Scheduler mode is currently off (tp = 0)
    // Set machine trap handler with vector mode given by configuration
    asm volatile (
        "li      tp, 0                 \n\
         csrw    mtvec, %0"
            : : "r"((u32)_LTKMachineTrapHandler + s_pConfig->vectorMode) :
    );
    s_nClockSpeedMHz = s_pConfig->nClockSpeedHz / 1000000;
    LTKList_Init(&s_timerQueue);
    _LTKInitialize(pBSP, pLTCoreLogFunction, &IdleThread);
}

/********
 * Time */
s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    u32 nMask = DisableInterruptsNested();
    s64 nCycleCount = (s64)s_pConfig->pGetCycleCount();
    EnableInterruptsNested(nMask);
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

// Interrupt dispatcher and scheduler
void * _LTKDispatcher(void * pStack, u32 mcause, u32 _LTK_nTrapNestingCounter) LT_ISR_SAFE {
    if (mcause != LTK_ARCH_RISC_V_MCAUSE_ECALL) {
        // Call Dispatch Handler in BSP
        u32 nTickAdvance = s_pConfig->pDispatcher(mcause);
        if (nTickAdvance) LTKTimerInterruptHandler(nTickAdvance);
    }
    // Scheduler
    u32 bSchedulerStarted;
    asm volatile ( "mv %0, tp" : "=r" (bSchedulerStarted) : : );
    if (bSchedulerStarted && _LTK_nTrapNestingCounter == 1) {
        // borrow and repurpose _LTK_nTrapNestingCounter to mean idle thread was (1) or wasn't (0) running
        _LTK_nTrapNestingCounter = 0; // initialize with idle thread wasn't running (0)
        if (_LTK_pRunningThread) {
            _LTK_pRunningThread->pStack = pStack;
            s64 nCycleCount = (s64)s_pConfig->pGetCycleCount();
            _LTK_pRunningThread->nCycleCount += (nCycleCount - s_nLastCycleCount);
            s_nLastCycleCount = nCycleCount;
            // if idle thread running mark it and call the idle tick hook
            if (_LTK_pRunningThread == &_LTK_idleThread) {
                _LTK_nTrapNestingCounter = 1; // mark idle thread was running (1)
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
        if (_LTK_nTrapNestingCounter) {
            // Idle thread was running, see if it isn't anymore
            if (_LTK_pRunningThread != &_LTK_idleThread) (*s_pExitIdleHookProc)();
        } else {
            // Idle thread wasn't running, see if it now will be
            if (_LTK_pRunningThread == &_LTK_idleThread) (*s_pEnterIdleHookProc)();
        }
        return _LTK_pRunningThread->pStack;
    }
    return NULL;
}

void LTKDebugBreak(void) {
    LTK_LTLOG_STOMP("cat: all.your.base", "are belong to us");
    asm volatile ( "unimp" );
    while (1);
    /* Unreachable */
}

/***********
 * Threads */
static void IdleThread(void * pArg) {
    LT_UNUSED(pArg);
    while (1) {
        asm volatile ( "wfi" );
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
    pStackFrame->nRegister[LTK_FRAME_WORD(MEPC)]    = (u32)pEntry;
    pStackFrame->nRegister[LTK_FRAME_WORD(A0)]      = nArg;
    pStackFrame->nRegister[LTK_FRAME_WORD(RA)]      = (u32)_LTKThreadExit;
#if defined(__riscv_float_abi_single)
    pStackFrame->nRegister[LTK_FRAME_WORD(MSTATUS)] = LTK_ARCH_RISC_V_START_MSTATUS_FP;
#else
    pStackFrame->nRegister[LTK_FRAME_WORD(MSTATUS)] = LTK_ARCH_RISC_V_START_MSTATUS;
#endif
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
    // Set scheduler mode, yield and enable traps
    asm volatile (
        "li      tp, 1                \n\
         ecall                        \n\
         csrs    mie, %0              \n\
         csrsi   mstatus, 1 << 3"
            : : "r"(0x880) :
    );
    // Unreachable
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
static void DumpStackFrame(u32 * pStackFrame) LT_ISR_SAFE {
    u8 data[] = { 'a', 0, 1, 'a', 4, 1, 's', 0, 1, 's', 4, 1, 's', 8, 1, 't', 0, 1, 't', 4, 0 };
    u32 nIx = 0;
    for (u8 * pData = data; pData < data + sizeof(data); nIx += 3, pData += 3) {
        if (pData[2]) {
            LTK_LTLOG_STOMP("stack", " %08X %08X %08X %08X (%c%u - %c%u)", pStackFrame[nIx], pStackFrame[nIx + 1],
                               pStackFrame[nIx + 2], pStackFrame[nIx + 3], pData[0], pData[1], pData[0], pData[1] + 3);
            nIx++;
        } else {
            LTK_LTLOG_STOMP("stack", " %08X %08X %08X          (%c%u - %c%u)", pStackFrame[nIx], pStackFrame[nIx + 1],
                                                     pStackFrame[nIx + 2], pData[0], pData[1], pData[0], pData[1] + 2);
        }
    }
    LTK_LTLOG_STOMP("stack", " %08X %08X %08X          (mstatus, ra, pc)\n",
                       pStackFrame[nIx], pStackFrame[nIx + 1], pStackFrame[nIx + 2]);
}

void _LTKFaultHandler(u32 * pStackFrame, u32 mcause, u32 _LTK_nTrapNestingCounter, u32 nFaultValue) LT_ISR_SAFE {
    u32 * pInterruptStackFrame = NULL;
    LTK_LTLOG_STOMP("fault", "\n*** Fault (Cause %u: %08X) ***\n", mcause, nFaultValue);
    if (_LTK_nTrapNestingCounter > 1) {
        pInterruptStackFrame = (u32 *)pStackFrame;
        // Subtract 1 from the counter because exception caused additional nesting
        LTK_LTLOG_STOMP("fault", "\nSystem stack @%08X (Nest level %u):", pInterruptStackFrame, _LTK_nTrapNestingCounter - 1);
        DumpStackFrame(pInterruptStackFrame);
        if (_LTK_pRunningThread)
            pStackFrame = (u32 *)_LTK_pRunningThread->pStack;
    }
    if (_LTK_pRunningThread) {
        _LTK_pRunningThread->pStack = (u8 *)pStackFrame;
        if (_LTK_pRunningThread->pName) {
            LTK_LTLOG_STOMP("fault", "\nThread stack @%08X (%s):", pStackFrame, _LTK_pRunningThread->pName);
        }
    } else {
        LTK_LTLOG_STOMP("fault", "\nSystem stack @%08X:", pStackFrame);
    }
    DumpStackFrame(pStackFrame);
    if (s_pFaultCallback) {
        (*s_pFaultCallback)(LTK_ABI_VERSION, (_LTK_nTrapNestingCounter > 1) ? pInterruptStackFrame : NULL,
                               _LTK_pInterruptStackTop - (u8 *)pInterruptStackFrame);
    }
    while (1);
    // unreachable
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Jul-23   tiberius    created
 */
