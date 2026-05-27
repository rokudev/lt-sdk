/******************************************************************************
 * lt/source/lt/ltk/LTKArchArmCortexM_Base.c                    LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include "LTKernel.h"

/* Kernel ABI version */
#define LTK_ABI_VERSION     "arm.m.base.1"

/***********************
 * Timer and Tick Data */
static volatile LT_ALIGNED(8) Tick s_tick = { .nCount = 1 };
static      u64 LT_ALIGNED(8)      s_nLastCycleCount = 0;

static LTKList s_timerQueue;
static u32     s_nCyclesPerTick;
static u32     s_nClockSpeedKHz;

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
static u32 s_nInitialExcReturn = LTK_ARCH_ARM_CORTEX_M_EXC_RETURN;

/***********************
 * Hooks and Callbacks */
static LTKFaultCallback * s_pFaultCallback = NULL;

/**************
 * Prototypes */
static void IdleThread(void *);
static void SysTick_Handler_Common(void);

/************
 * Logging */
DEFINE_LTK_LTLOG_SECTION("ltk");

/**************************************
 * Atomic Built-in Function Stand-ins */

unsigned
__atomic_fetch_add_4(volatile void * pValue, unsigned nAddValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue + nAddValue;
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_sub_4(volatile void * pValue, unsigned nSubValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue - nSubValue;
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_and_4(volatile void * pValue, unsigned nAndValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue & nAndValue;
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_fetch_or_4(volatile void * pValue, unsigned nOrValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nValue | nOrValue;
    EnableInterruptsNested(nMask);
    return nValue;
}

unsigned
__atomic_exchange_4(volatile void * pValue, unsigned nExchValue, int nOrder) {
    LT_UNUSED(nOrder);
    u32 nMask = DisableInterruptsNested();
    u32 nValue = *((u32 *)pValue);
    *((u32 *)pValue) = nExchValue;
    EnableInterruptsNested(nMask);
    return nValue;
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
        EnableInterruptsNested(nMask);
        return true;
    } else {
        *((u32 *)pExpected) = nValue;
        EnableInterruptsNested(nMask);
        return false;
    }
}

/**********************************
 * LTK Initialization and Startup */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback) {
    LTK_LTLOG_INITIALIZE(pLTCoreLogFunction);
    s_pFaultCallback = pFaultCallback;
    // Set lowest preemption priority for SysTick and PendSV (highest number).
    LTK_REG(SHPR3) = LTK_REG_VAL(EXC_PRIORITY);
    // Clocks / SysTick
    LTCoreBSP_ArmCortexM_SystemConfig * pConfig = (LTCoreBSP_ArmCortexM_SystemConfig *)pBSP->pLTSystemConfig;
    s_nClockSpeedKHz = pConfig->nClockSpeedHz / 1000;
    s_nCyclesPerTick = pConfig->nClockSpeedHz / kLTKTicksPerSecond;
    bool bInitTick = true;
    if (LTK_REG(TICK_CTRL) & LTK_REG_VAL(TICK_RUNNING)) {
        // If tick is enabled properly, then don't reinit tick
        if (LTK_REG(TICK_LOAD) == s_nCyclesPerTick - 1) bInitTick = false;
        else {
            s_tick.nCount = 0;
            LTK_LTLOG_STOMP("init", "Error: Check tick configuration.\n");
        }
    }
    if (bInitTick) {
        LTK_REG(TICK_LOAD) = s_nCyclesPerTick - 1;
        LTK_REG(TICK_VAL)  = 0;
    }
    // Ensure tick and its interrupt are enabled
    if (pConfig->bUseExternalClockRef) {
        LTK_REG(TICK_CTRL) = LTK_REG_VAL(TICK_ENABLE);
    } else {
        LTK_REG(TICK_CTRL) = LTK_REG_VAL(TICK_ENABLE) | LTK_REG_VAL(TICK_USE_CPU);
    }
    // Determine initial exception return value for threads
    LTK_ARCH_ARM_CORTEX_M_SET_EXC_RETURN_SECTION
    LTKList_Init(&s_timerQueue);
    _LTKInitialize(pBSP, pLTCoreLogFunction, &IdleThread);
}

static void __attribute__((optimize(2))) LTKRunScheduler(void) {
    // Start PSP in a safe place for first PendSV and then enable interrupts
    // 112 (28 words) is enough to store a dummy FP stack frame
    asm volatile (
       "ldr r0, _pspStart                \n\
        msr psp, r0                      \n\
        b SkipRS                         \n\
      .balign 4                          \n\
      _pspStart:                         \
        .word _LTK_idleThreadStack + 112 \n\
      SkipRS:"
            : : : "r0"
    );
    // Invoke PendSV handler to start scheduler (first context switch)
    YieldThread();
    EnableInterrupts();
    asm volatile ( "isb" );
    // Not reachable
    LT_ASSERT(0);
}

/********
 * Time */
LT_TEXT_RAM_CRITICAL(1)
static s64 GetCycleCount(void) LT_ISR_SAFE {
    u32 nMask = DisableInterruptsNested();
    u64 nTickCount = s_tick.nCount;
    u32 nVal = LTK_REG(TICK_VAL);
    /* Despite interrupts being disabled, the systick timer can still expire at
       any point. If it expires between reading the tick counter and timer value,
       above then we will need to re-read both. Interrupts are disabled, and we
       have a whole millisecond before it will expire again, so the second read
       should be safe. */
    if (LTK_REG(TICK_CTRL) & LTK_REG_VAL(TICK_FLAG)) {
        nTickCount = ++s_tick.nCount;
        nVal = LTK_REG(TICK_VAL);
    }
    /* Fix for race condition between tick count increment and hardware reset of
       tick value to TICK_LOAD. This is only needed for very low frequency timers. */
    if (nVal == 0 && s_nCyclesPerTick < 1000) {
        nVal = s_nCyclesPerTick;
    }
    EnableInterruptsNested(nMask);
    return (s64)(nTickCount * s_nCyclesPerTick) - nVal;
}

s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE {
    s64 nCycleCount = GetCycleCount();
    return (s64)((nCycleCount * (u64)1000000) / s_nClockSpeedKHz);
}

s64 LTKGetHighFrequencyCounterNanosecondResolution(void) LT_ISR_SAFE {
    return (s64)(((u64)1000000) / s_nClockSpeedKHz);
}

void LTKAdvanceTickCount(u32 nTicks) {
    if (nTicks) {
        u32 nMask = DisableInterruptsNested();
        s_tick.nCount += nTicks;
        SysTick_Handler_Common();
        EnableInterruptsNested(nMask);
    }
}

/**************
 * Interrupts */
void LTKSetInterruptVector(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler,
                               LTCore_InterruptPriority priority) {
    if (nInterrupt > LTK_ARCH_ARM_CORTEX_M_MAX_IRQ) return;
    if (priority > kLTCore_InterruptPriorityHighest) return;
    u32 nPriorityVal = (kLTCore_InterruptPriorityHighest - priority) << (8 - kLTCore_NumberOfInterruptPriorityBits);
    u32 nIntShift = (nInterrupt & 0x3) << 3;
    u32 nIntMask = ~(0xff << nIntShift);
    // Disable interrupts and disable IRQ line (if enabled)
    u32 nMask = DisableInterruptsNested();
    bool bEnabled = IsIRQEnabled(nInterrupt);
    if (bEnabled) DisableIRQ(nInterrupt);
    u32 nIPR = LTK_REG(IPR_W)(nInterrupt >> 2);
    LTK_REG(IPR_W)(nInterrupt >> 2) = (nIPR & nIntMask) | (nPriorityVal << nIntShift);
    // Exception Number = 16 + External Interrupt Number
    u32 * pIRQVectors = (u32 *)LTK_REG(VTOR);
    pIRQVectors[16 + nInterrupt] = (u32)pInterruptHandler;
    asm volatile ( "dsb" );
    // Enable IRQ line and enable interrupts (if just disabled)
    if (bEnabled) EnableIRQ(nInterrupt);
    EnableInterruptsNested(nMask);
}

void LTKSetInterruptPriority(u32 nInterrupt, LTCore_InterruptPriority priority) {
    if (nInterrupt > LTK_ARCH_ARM_CORTEX_M_MAX_IRQ) return;
    if (priority > kLTCore_InterruptPriorityHighest) return;
    u32 nPriorityVal = (kLTCore_InterruptPriorityHighest - priority) << (8 - kLTCore_NumberOfInterruptPriorityBits);
    u32 nIntShift = (nInterrupt & 0x3) << 3;
    u32 nIntMask = ~(0xff << nIntShift);
    // Disable interrupts and disable IRQ line (if enabled)
    u32 nMask = DisableInterruptsNested();
    bool bEnabled = IsIRQEnabled(nInterrupt);
    if (bEnabled) DisableIRQ(nInterrupt);
    u32 nIPR = LTK_REG(IPR_W)(nInterrupt >> 2);
    LTK_REG(IPR_W)(nInterrupt >> 2) = (nIPR & nIntMask) | (nPriorityVal << nIntShift);
    // Enable IRQ line and enable interrupts (if just disabled)
    if (bEnabled) EnableIRQ(nInterrupt);
    EnableInterruptsNested(nMask);
}

void LTKDebugBreak(void) {
    if (LTK_REG(DHCSR) & LTK_REG_VAL(DEBUG_ENABLED)) {
        LTK_LTLOG_STOMP("break", "\ndebugger detected, breaking now, Captain!\n\n");
        asm volatile ( "bkpt 1" );
    } else {
        LTK_LTLOG_STOMP("cat: all.your.base", "are belong to us");
        asm volatile (
           "mov r0, #3      \n\
            ldr r1, [r0]" ::: "r0", "r1"
        );
        /* unreachable */
        while (1);
    }
}

/***********
 * Threads */
static void IdleThread(void * pArg) {
    LT_UNUSED(pArg);
    while (1) {
        asm volatile (
            "dsb      \n\
             wfi" ::: "memory"
        );
    }
}

void
_LTKInitThread(LTKThread * pThread, u8 nPriority, void * entry, s32 arg,
                  u8 * pStackBottom, u32 nStackSize) {
    u8 * pStack = pStackBottom + nStackSize;
    // Insert canary
    pStack -= sizeof(u32);
    *((u32 *)pStack) = kLTKStackFillValue;
    // Ensure 8-byte alignment for ARM / varargs compatibility.
    pStack = (u8 *)((u32)pStack & 0xfffffff8);
    // Fill out initial frame
    LTKStackFrame * pStackFrame = (LTKStackFrame *)pStack - 1;
    pStackFrame->nPSR         = 0x01000000;
    pStackFrame->nPC          = (u32)entry;
    pStackFrame->nLR          = (u32)_LTKThreadExit;
    pStackFrame->nR12         = 0;
    pStackFrame->hwSave[0]    = arg;
    pStackFrame->nExcReturnLR = s_nInitialExcReturn;
    // Fill lower stack
    u32 * pFill = (u32 *)pStackFrame - 1;
    for (; pFill >= (u32 *)pStackBottom; pFill--) {
        *pFill = kLTKStackFillValue;
    }
    // Initialize context and state
    pThread->pStack            = (u8 *)pStackFrame;
    pThread->nCycleCount       = 0;
    pThread->nPriority         = nPriority;
    pThread->nNominalPriority  = nPriority;
    pThread->pStackBottom      = pStackBottom;
    pThread->nStackSize        = nStackSize;
}

void
LTKThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize,
                                        const char * pName, void (*pEntry)(void *),
                                        void * pClientData) {
    if (LTKThreadInitializeAndRun(pThread, nPriority, nStackSize, pName, pEntry, pClientData)) {
        LTKRunScheduler();
    }
}

bool _LTKSetTimeout(u64 nNanosecondsToWait) {
    if (nNanosecondsToWait > (u64)kLTKNanosecondsPerTick * 0x7ffffffe) return false;
    u32 nTicks = (nNanosecondsToWait + kLTKNanosecondsPerTick - 1) / kLTKNanosecondsPerTick;
    _LTK_pRunningThread->nWakeTick = s_tick.nLower + nTicks;
    return true;
}

s64 LTKThreadGetCpuTime(void * pInstance) {
    LTKThread * pThread = (LTKThread *)pInstance;
    LockScheduler();
    s64 nCycleCount = pThread->nCycleCount;
    if (pThread == _LTK_pRunningThread) {
        nCycleCount += (GetCycleCount() - s_nLastCycleCount);
    }
    UnlockScheduler(0);
    return (nCycleCount * 1000000) / s_nClockSpeedKHz;
}

/*************
 * Scheduler */
LT_TEXT_RAM_CRITICAL(1)
static u8 * LT_USED Scheduler(u8 * pStack) {
    bool bInIdleThread = false;
    // Save SP context
    if (_LTK_pRunningThread != kLTKNoSuchThread) {
        _LTK_pRunningThread->pStack = pStack;
        _LTKStackCheck(_LTK_pRunningThread);
        // if idle thread running mark it and call the idle tick hook
        if (_LTK_pRunningThread == &_LTK_idleThread) {
            bInIdleThread = true; // mark idle thread was running
            (*s_pIdleTickHookProc)();
        }
    } else {
        _LTK_pRunningThread = &_LTK_idleThread;
    }
    u64 nCycleCount = GetCycleCount();
    _LTK_pRunningThread->nCycleCount += (nCycleCount - s_nLastCycleCount);
    s_nLastCycleCount = nCycleCount;
    DisableInterrupts();
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
    EnableInterrupts();
    // Stack pointer limit check
    LTK_ARCH_ARM_CORTEX_M_SPLIM_SECTION
    // Set next thread ID and return its stack pointer
    _LTK_pRunningThread = pRunThread;
    // Call idle transition hooks if required
    if (bInIdleThread) {
        // Idle thread was running, see if it isn't anymore
        if (_LTK_pRunningThread != &_LTK_idleThread) (*s_pExitIdleHookProc)();
    } else {
        // Idle thread wasn't running, see if it now will be
        if (_LTK_pRunningThread == &_LTK_idleThread) (*s_pEnterIdleHookProc)();
    }
    return _LTK_pRunningThread->pStack;
}

/**********************
 * Exception handlers */
LT_TEXT_RAM_CRITICAL(1)
void LT_VERBATIM PendSV_Handler(void) {
    asm volatile (
        LTK_ARCH_ARM_CORTEX_M_CONTEXT_SWITCH_SECTION
    );
}

LT_TEXT_RAM_CRITICAL(1)
static void SysTick_Handler_Common(void) {
    if (_LTK_pRunningThread == kLTKNoSuchThread) return;
    DisableInterrupts();
    // Process timer queue
    LTKList_Node * pElmSave;
    for (LTKList_Node * pElm = s_timerQueue.pNext; pElm != &s_timerQueue; pElm = pElmSave) {
        pElmSave = pElm->pNext;
        LTKThread * pThread = LT_CONTAINER_OF(pElm, LTKThread, timerLink);
        s32 nRemTicks = (s32)pThread->nWakeTick - s_tick.nLower;
        if (nRemTicks <= 0) {
            LTKList_Remove(pElm);
            if (pThread->nState == kLTKThreadState_WaitForMonitorOrTick) {
                pThread->pMonitor->pWaiter = kLTKNoSuchThread;
            }
            LTKList_Remove(&pThread->runLink);
            LTKList_AddTail(&_LTK_runQueues[pThread->nPriority], &pThread->runLink);
            SetThreadState(pThread, kLTKThreadState_Runnable);
            pThread->nTimedOut = 1;
        } else break;
    }
    EnableInterrupts();
    YieldThread();
}

LT_TEXT_RAM_CRITICAL(1)
void SysTick_Handler(void) {
    DisableInterrupts();
    if (LTK_REG(TICK_CTRL) & LTK_REG_VAL(TICK_FLAG)) s_tick.nCount += 1;
    EnableInterrupts();
    SysTick_Handler_Common();
}

static void LT_USED FaultHandler(u32 * msp, u32 * psp, u32 lr) {
    bool in_isr = ((lr & 0x8) == 0x0);
    LTK_LT_CONSOLESTOMP("\n*** Hard Fault %s", in_isr ? "IN ISR " : "");
    if (_LTK_pRunningThread == kLTKNoSuchThread) {
        LTK_LT_CONSOLESTOMP("(Pre-Scheduler) ***\n");
    } else if (_LTK_pRunningThread->pName && _LTK_pRunningThread->pName[0] != '\0') {
        LTK_LT_CONSOLESTOMP("(Thread %s) ***\n", _LTK_pRunningThread->pName);
    } else {
        LTK_LT_CONSOLESTOMP("(Thread @%08X) ***\n", _LTK_pRunningThread);
    }
    LTK_LT_CONSOLESTOMP("%08x %08p\n", lr, _LTK_pRunningThread);
    //bool use_psp = ((lr & 0x4) == 0x4);
    bool use_psp = true;
    u32 * sp = use_psp ? psp : msp;
    s32 num_words = 16;
    if (use_psp && _LTK_pRunningThread != kLTKNoSuchThread) {
        _LTK_pRunningThread->pStack = (u8 *)psp;
        u8 * sp2 = _LTK_pRunningThread->pStackBottom;
        if (*((u32 *)sp2) != kLTKStackFillValue)
            LTK_LT_CONSOLESTOMP("!!! Thread Stack corruption (bottom) !!!\n");
        sp2 = (u8 *)(sp2 + _LTK_pRunningThread->nStackSize - sizeof(u32));
        if (*((u32 *)sp2) != kLTKStackFillValue)
            LTK_LT_CONSOLESTOMP("!!! Thread Stack corruption (top) !!!\n");
        s32 rem_words = ((u32 *)sp2) - sp;
        if (rem_words < 64) num_words = rem_words;
        else num_words = 64;
    }
    LTK_LT_CONSOLESTOMP("%s Stack @%08X:\n", use_psp ? "Process" : "Main", (u32)sp);
    LTK_LT_CONSOLESTOMP(" %08X %08X %08X %08X  (R0 R1 R2 R3)\n",  sp[0], sp[1], sp[2], sp[3]);
    LTK_LT_CONSOLESTOMP(" %08X %08X %08X %08X (R12 LR PC PSR)\n", sp[4], sp[5], sp[6], sp[7]);
    sp += 8;
    for (s32 ix = 0; ix < (num_words - 8); ix++) {
        LTK_LT_CONSOLESTOMP(" %08X", sp[ix]);
        if ((ix & 0x3) == 0x3) LTK_LT_CONSOLESTOMP("\n");
    }
    LTK_LT_CONSOLESTOMP("\n");
    if (s_pFaultCallback) {
        (*s_pFaultCallback)(LTK_ABI_VERSION, in_isr ? msp : NULL, sizeof(LTKStackFrame));
    }
    // Hang here to let the watchdog reset
    while (1)
        ;
}

void LT_VERBATIM LTKFaultHandler(void) {
    asm volatile (
        "mrs r0, msp\n"
        "mrs r1, psp\n"
        "mov r2, lr\n"
        "b FaultHandler"
            : : : "r0", "r1", "r2", "r3"
    );
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Oct-21   tiberius    created
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  18-Dec-22   augustus    added LTKThreadGetDefaultStackSize()
 */
