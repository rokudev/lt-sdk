/******************************************************************************
 * lt/source/lt/ltk/LTKArchXtensa.c                             LTK MicroKernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKernel.h"

/* Kernel ABI version */
#define LTK_ABI_VERSION     "xtensa.1"

/***********************
 * Timer and Tick Data */
static volatile LT_ALIGNED(8) Tick s_tick = { .nCount = 1 };
static      s64 LT_ALIGNED(8)      s_nLastCycleCount = 0;

static LTKList s_timerQueue;
static u32     s_nCyclesPerTick;
static u32     s_nClockSpeedMHz;

/******************
 * Interrupt Data */
static LTCore_InterruptHandler * s_pInterruptHandlers[LTK_ARCH_XTENSA_MAX_INTERRUPTS];

static bool    s_bSchedulerStarted;
static u32     s_nInterruptMask[XCHAL_EXCM_LEVEL];
volatile u32   _LTK_interruptNestingCounter = 0;
extern u8    * _LTK_pThreadStackBSA;
extern u8      _LTK_interruptStackTop;

/***********************
 * Hooks and Callbacks */
static LTKFaultCallback * s_pFaultCallback = NULL;

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

/**************
 * Prototypes */
static void LTKTimerInterruptHandler(void);
static void IdleThread(void *);
/* ASM trampoline to ensure call4 ABI that thread main function uses */
void _LTKThreadExitTrampoline(void);

/***********
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk");

/**********************************
 * LTK Initialization and Startup */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback) {
    LTK_LTLOG_INITIALIZE(pLTCoreLogFunction);
    s_pFaultCallback = pFaultCallback;
    // Configure and start timer tick (Timer 0 is dedicated to kernel)
    LTCoreBSP_Xtensa_SystemConfig * pConfig = (LTCoreBSP_Xtensa_SystemConfig *)pBSP->pLTSystemConfig;
    s_nClockSpeedMHz = pConfig->nClockSpeedHz / 1000000;
    s_nCyclesPerTick = pConfig->nClockSpeedHz / kLTKTicksPerSecond;
    u32 nMask = DisableInterruptsNested();
    // Set first tick
    u32 nTemp = LTK_READ_SR(CCOUNT);
    nTemp += s_nCyclesPerTick;
    LTK_WRITE_SR(CCOMPARE_0, nTemp);
    // Set vector and enable interrupt
    LTKSetInterruptVector(XCHAL_TIMER0_INTERRUPT, LTKTimerInterruptHandler, 1);
    nTemp = LTK_READ_SR(INTENABLE);
    nTemp |= 1 << XCHAL_TIMER0_INTERRUPT;
    LTK_WRITE_SR(INTENABLE, nTemp);
    EnableInterruptsNested(nMask);
    LTKList_Init(&s_timerQueue);
    _LTKInitialize(pBSP, pLTCoreLogFunction, &IdleThread);
}

/********
 * Time */
static s64 GetCycleCount(void) LT_ISR_SAFE {
    u32 nMask = DisableInterruptsNested();
    u64 nTickCount = s_tick.nCount;
    s32 nCycleAdjust = LTK_READ_SR(CCOUNT) - LTK_READ_SR(CCOMPARE_0);
    EnableInterruptsNested(nMask);
    return (s64)(nTickCount * s_nCyclesPerTick) + nCycleAdjust;
}

s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    s64 nCycleCount = GetCycleCount();
    return (s64)((nCycleCount * (u64)1000) / s_nClockSpeedMHz);
}

s64 LTKGetHighFrequencyCounterNanosecondResolution(void) LT_ISR_SAFE {
    return (s64)(((u64)1000) / s_nClockSpeedMHz);
}

bool _LTKSetTimeout(u64 nNanosecondsToWait) {
    if (nNanosecondsToWait > (u64)kLTKNanosecondsPerTick * 0x7ffffffe) return false;
    u32 nTicks = (nNanosecondsToWait + kLTKNanosecondsPerTick - 1) / kLTKNanosecondsPerTick;
    _LTK_pRunningThread->nWakeTick = s_tick.nLower + nTicks;
    return true;
}

void LTKAdvanceTickCount(u32 nTicks) {
    /* The ISR is self adjusting */
    if (nTicks) {
        u32 nMask = DisableInterruptsNested();
        LTKTimerInterruptHandler();
        YieldThread();
        EnableInterruptsNested(nMask);
    }
}

/**************
 * Interrupts */
void LTKSetInterruptVector(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler,
                               LTCore_InterruptPriority priority) {
    if (nInterrupt > LTK_ARCH_XTENSA_MAX_INTERRUPTS) return;
    if (priority > kLTCore_InterruptPriorityHighest) return;
    /*                   Interrupt mapping
     *
     * LTPriority        LTK Priority      Xtensa INTLEVEL
     *   [0, 1]    <->   0            <->  1 (LOW)
     *   [2-XEL]   <->   [1-(XEL-1)]  <->  2 -> XCHAL_EXCM_LEVEL (MEDIUM)
     *   >XEL      <->   (XEL-1)      <->  XCHAL_EXCM_LEVEL (MEDIUM)
     *   N/A       <->   N/A          <->  >XCHAL_EXCM_LEVEL (HIGH*)
     *
     *  *High priority interrupts are not supported by LTK, high priority
     *       interrupts (if any) are recommended to be implemented in assembly.
     */
    u8 nPriorityVal = priority - 1;
    if (priority == 0) nPriorityVal = 0;
    if (priority > XCHAL_EXCM_LEVEL) nPriorityVal = XCHAL_EXCM_LEVEL - 1;
    u32 nMask = DisableInterruptsNested();
    s_pInterruptHandlers[nInterrupt] = pInterruptHandler;
    s_nInterruptMask[nPriorityVal] |= (1 << nInterrupt);
    EnableInterruptsNested(nMask);
}

void LTKSetInterruptPriority(u32 nInterrupt, LTCore_InterruptPriority priority) {
    // Xtensa interrupt hardware priority cannot be changed dynamically
    LT_UNUSED(nInterrupt);
    LT_UNUSED(priority);
}

static void LTKTimerInterruptHandler(void) LT_ISR_SAFE {
    // Update tick, accounting for slips
    u32 nAdd = 1;
    u32 nMask = DisableInterruptsNested();
    u32 nCCOMPARE = LTK_READ_SR(CCOMPARE_0);
    LTK_WRITE_SR(CCOMPARE_0, nCCOMPARE + s_nCyclesPerTick);
    u32 nCCOUNT = LTK_READ_SR(CCOUNT);
    while (__builtin_expect(nCCOUNT - nCCOMPARE > s_nCyclesPerTick, 0)) {
        nAdd++;
        nCCOMPARE += s_nCyclesPerTick;
        LTK_WRITE_SR(CCOMPARE_0, nCCOMPARE + s_nCyclesPerTick);
        nCCOUNT = LTK_READ_SR(CCOUNT);
    }
    s_tick.nCount += nAdd;
    EnableInterruptsNested(nMask);
    if (_LTK_pRunningThread == kLTKNoSuchThread) return;

    // Process timer queue
    //  Timer queues can contain threads or message timers
    //TODO: Could just lock for one removed from queue at a time
    LTKList_Node * pElmSave;
    for (LTKList_Node * pElm = s_timerQueue.pNext; pElm != &s_timerQueue; pElm = pElmSave) {
        pElmSave = pElm->pNext;
        LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, timerLink);
        s32 nRemTicks = (s32)pThread->nWakeTick - s_tick.nLower;
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
    EnableInterruptsNested(nMask);
}

// Interrupt dispatcher and scheduler
void * _LTKDispatcher(u32 nExcLevel) LT_ISR_SAFE {
    // Dispatch Interrupts
    u32 nPendingInterrupts = LTK_READ_SR(INTERRUPT) & s_nInterruptMask[nExcLevel];
    while (nPendingInterrupts) {
        u32 nInterrupt = __builtin_ctz(nPendingInterrupts);
        (*s_pInterruptHandlers[nInterrupt])();
        nPendingInterrupts = LTK_READ_SR(INTERRUPT) & s_nInterruptMask[nExcLevel];
    }
    // Scheduler
    LTKThread * pPreviousThread = _LTK_pRunningThread;
    if (s_bSchedulerStarted && _LTK_interruptNestingCounter == 1) {
        // Save SP context
        if (_LTK_pRunningThread) {
            _LTK_pRunningThread->pStack = _LTK_pThreadStackBSA;
            _LTKStackCheck(_LTK_pRunningThread);
            _LTK_pRunningThread->pStack += LTK_ARCH_XTENSA_BSA_SIZE;
            // Adjust cycle count
            s64 nCycleCount = GetCycleCount();
            _LTK_pRunningThread->nCycleCount += (nCycleCount - s_nLastCycleCount);
            s_nLastCycleCount = nCycleCount;
            // if idle thread running call the idle tick hook
            if (_LTK_pRunningThread == &_LTK_idleThread) (*s_pIdleTickHookProc)();
        } else {
            _LTK_pRunningThread = &_LTK_idleThread;
        }
        //  Disable without matching enable is desired here as exception handler will
        //    safely re-enable interrupts when this function returns.
        DisableInterruptsNested();
        if (_LTK_pRunningThread->nState >= kLTKThreadState_WaitForTick) {
            // Update running thread timer state (insertion sort in timer queue)
            s32 nRemTicks = (s32)_LTK_pRunningThread->nWakeTick - s_tick.nLower;
            LTKList_Node * pElm;
            for (pElm = s_timerQueue.pNext; pElm != &s_timerQueue; pElm = pElm->pNext) {
                LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, timerLink);
                s32 nTmrRemTicks = (s32)pThread->nWakeTick - s_tick.nLower;
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
        } else pRunThread = &_LTK_idleThread;
        _LTK_pRunningThread = pRunThread;
        // Call idle transition hooks if required
        if (pPreviousThread == &_LTK_idleThread) {
            // Idle thread was running, see if it isn't anymore
            if (_LTK_pRunningThread != &_LTK_idleThread) (*s_pExitIdleHookProc)();
        } else {
            // Idle thread wasn't running, see if it now will be
            if (_LTK_pRunningThread == &_LTK_idleThread) (*s_pEnterIdleHookProc)();
        }
    }
    return (_LTK_pRunningThread == pPreviousThread) ? NULL : _LTK_pRunningThread->pStack;
}

void LTKDebugBreak(void) {
    if (OCDIsAttached()) {
        LTK_LTLOG_STOMP("break", "\ndebugger detected, breaking now, Captain!\n\n");
        BreakIntoOCD();
    }
    else {
        asm volatile ( "ill.n" );
    }
}

/***********
 * Threads */
static void IdleThread(void * pArg) {
    LT_UNUSED(pArg);
    while (1) {
        asm volatile ( "waiti 0" );
    }
}

void
_LTKInitThread(LTKThread * pThread, u8 nPriority, void * pEntry, s32 nArg, u8 * pStackBottom,
                  u32 nStackSize) {
    u8 * pStack = pStackBottom + nStackSize;
    // Insert canary
    pStack -= sizeof(u32);
    *((u32 *)pStack) = kLTKStackFillValue;
    // Make room for a BSA (supporting return to _LTKThreadExit)
    //   and then align to 16 bytes per Xtensa ISA
    pStack -= LTK_ARCH_XTENSA_BSA_SIZE;
    pStack = (u8 *)((u32)pStack & 0xfffffff0);
    // Fill out initial frame
    LTKStackFrame * pStackFrame = (LTKStackFrame *)pStack - 1;
    // _ThreadExitTrampoline in assembly, uses call4 to C-function _LTKThreadExit(),
    //   so PS and windows are consistent
    pStackFrame->nRegister[LTK_FRAME_WORD(A4)]     = (u32)_LTKThreadExitTrampoline;
    pStackFrame->nRegister[LTK_FRAME_WORD(A6)]     = nArg;
    pStackFrame->nRegister[LTK_FRAME_WORD(LBEG)]   = 0;
    pStackFrame->nRegister[LTK_FRAME_WORD(LEND)]   = 0;
    pStackFrame->nRegister[LTK_FRAME_WORD(LCOUNT)] = 0;
    pStackFrame->nRegister[LTK_FRAME_WORD(SAR)]    = 0;
    pStackFrame->nRegister[LTK_FRAME_WORD(PS)]     = LTK_ARCH_XTENSA_START_PS;
    pStackFrame->nRegister[LTK_FRAME_WORD(EPC)]    = (u32)pEntry;
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
    s_bSchedulerStarted = true;
    YieldThread();
    // Unreachable
    while (1);
}

s64 LTKThreadGetCpuTime(void * pInstance) {
    LTKThread * pThread = (LTKThread *)pInstance;
    u32 nMask = LockScheduler();
    s64 nCycleCount = pThread->nCycleCount;
    if (pThread == _LTK_pRunningThread) {
        nCycleCount += (GetCycleCount() - s_nLastCycleCount);
    }
    UnlockScheduler(nMask);
    return (nCycleCount * 1000) / s_nClockSpeedMHz;
}

/*****************
 * Fault Handler */
__attribute__((section(".iram1"))) static void
DumpStackFrame(u32 * pStackFrame) LT_ISR_SAFE {
    LTK_LTLOG_STOMP("stack", " %08X %08X %08X          (a0 a2 a3)\n",
                              pStackFrame[0], pStackFrame[1], pStackFrame[2]);
    for (u32 nIx = 4; nIx <= 12; nIx += 4) {
        LTK_LTLOG_STOMP("stack", " %08X %08X %08X %08X (a%u a%u a%u a%u)", pStackFrame[nIx - 1], pStackFrame[nIx],
                                     pStackFrame[nIx + 1], pStackFrame[nIx + 2], nIx, nIx + 1, nIx + 2, nIx + 3);
    }
    LTK_LTLOG_STOMP("stack", " %08X %08X %08X %08X (SCOMP1 LBEG LEND LCOUNT)\n",
                              pStackFrame[15], pStackFrame[16], pStackFrame[17], pStackFrame[18]);
    LTK_LTLOG_STOMP("stack", " %08X %08X %08X          (SAR PS PC)\n",
                              pStackFrame[19], pStackFrame[20], pStackFrame[21]);
    LTK_LTLOG_STOMP("stack", "\n");
}

__attribute__((section(".iram1"))) void
_LTKFaultHandler(u32 nType, u32 * pThreadStackFrame, u32 * pInterruptStackFrame) LT_ISR_SAFE {
    char * exceptionType[] = {
        "User", "Kernel", "Double"
    };
    LTK_LTLOG_STOMP("fault", "\n*** %s Fault ***\n", exceptionType[nType]);
    if (OCDIsAttached()) {
        LTK_LTLOG_STOMP("fault.ocd", "OCD %08X\n", pThreadStackFrame);
        BreakIntoOCD();
    }
    LTK_LTLOG_STOMP("fault", " Cause: %02u  Virt Addr: %08X\n\n",
                              LTK_READ_SR(EXCCAUSE), LTK_READ_SR(EXCVADDR));
    if (_LTK_interruptNestingCounter > 1) {
        // Subtract 1 from the counter because exception caused additional nesting
        LTK_LTLOG_STOMP("fault", "Interrupt stack @%08X (Nest level %u):\n",
                                   pInterruptStackFrame, _LTK_interruptNestingCounter - 1);
        DumpStackFrame(pInterruptStackFrame);
    }
    if (_LTK_pRunningThread) {
        _LTK_pRunningThread->pStack = (u8 *)pThreadStackFrame;
        if (_LTK_pRunningThread->pName) {
            LTK_LTLOG_STOMP("fault", "Thread stack @%08X (%s):\n", pThreadStackFrame, _LTK_pRunningThread->pName);
        } else {
            LTK_LTLOG_STOMP("fault", "Thread stack @%08X:\n", pThreadStackFrame);
        }
    }
    DumpStackFrame(pThreadStackFrame);
    if (s_pFaultCallback) {
        (*s_pFaultCallback)(LTK_ABI_VERSION, (_LTK_interruptNestingCounter > 1) ? pInterruptStackFrame : NULL,
                               &_LTK_interruptStackTop - (u8 *)pInterruptStackFrame);
    }
    while (1);
    // unreachable
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Mar-22   tiberius    created
 *  18-Dec-22   augustus    added LTKThreadGetDefaultStackSize()
 */
