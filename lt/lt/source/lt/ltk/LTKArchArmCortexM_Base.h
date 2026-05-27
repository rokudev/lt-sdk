/******************************************************************************
 * lt/source/lt/ltk/LTKArchArmCortexM_Base.h                    LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_BASE_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_BASE_H

// ARM Cortex-M Register Access
#define LTK_REG(NAME)           LTK_ARCH_ARM_CORTEX_M_REG_ ## NAME
#define LTK_REG_VAL(NAME)       LTK_ARCH_ARM_CORTEX_M_VAL_ ## NAME
#define LTK_REG_GET(NAME)       LTK_ARCH_ARM_CORTEX_M_GET_ ## NAME

// System Tick
#define LTK_ARCH_ARM_CORTEX_M_REG_TICK_CTRL      (*(volatile u32 *)0xe000e010)
#define LTK_ARCH_ARM_CORTEX_M_REG_TICK_LOAD      (*(volatile u32 *)0xe000e014)
#define LTK_ARCH_ARM_CORTEX_M_REG_TICK_VAL       (*(volatile u32 *)0xe000e018)
#define LTK_ARCH_ARM_CORTEX_M_VAL_TICK_USE_CPU   0x4
#define LTK_ARCH_ARM_CORTEX_M_VAL_TICK_ENABLE    0x3
#define LTK_ARCH_ARM_CORTEX_M_VAL_TICK_RUNNING   0x1
#define LTK_ARCH_ARM_CORTEX_M_VAL_TICK_FLAG      (0x1 << 16)

// SCB:ICSR
#define LTK_ARCH_ARM_CORTEX_M_REG_ICSR           (*(volatile u32 *)0xe000ed04)
#define LTK_ARCH_ARM_CORTEX_M_VAL_ICSR_PENDST    (0x1 << 26)
#define LTK_ARCH_ARM_CORTEX_M_VAL_ICSR_PENDSV    (0x1 << 28)

// SCB:AIRCR
#define LTK_ARCH_ARM_CORTEX_M_REG_AIRCR          (*(volatile u32 *)0xe000ed0c)
#define LTK_ARCH_ARM_CORTEX_M_VAL_AIRCR_MASK     0x00006030
#define LTK_ARCH_ARM_CORTEX_M_VAL_VECTKEY        0x05fa0000
#define LTK_ARCH_ARM_CORTEX_M_VAL_AIRCR_SEC_MASK 0x00000730
#define LTK_ARCH_ARM_CORTEX_M_VAL_AIRCR_SEC      (LTK_ARCH_ARM_CORTEX_M_VAL_VECTKEY | 0x00004000)

// Debug registers
#define LTK_ARCH_ARM_CORTEX_M_REG_DHCSR          (*(volatile u32 *)0xe000edf0)
#define LTK_ARCH_ARM_CORTEX_M_VAL_DEBUG_ENABLED  (0x1)

// Interrupts
#define LTK_ARCH_ARM_CORTEX_M_REG_ISER           (*(volatile u32 *)0xe000e100)
#define LTK_ARCH_ARM_CORTEX_M_REG_ICER           (*(volatile u32 *)0xe000e180)
#define LTK_ARCH_ARM_CORTEX_M_REG_VTOR           (*(volatile u32 *)0xe000ed08)
#define LTK_ARCH_ARM_CORTEX_M_REG_SHPR3          (*(volatile u32 *)0xe000ed20)

// Only 2-bits are implemented, but it doesn't hurt to write 8-bits
//  for compatibility with mainline variants
#define LTK_ARCH_ARM_CORTEX_M_VAL_EXC_PRIORITY   0xffff0000

// NOTE: baseline only supports word accesses
#define LTK_ARCH_ARM_CORTEX_M_REG_IPR_W(x)       (*((volatile u32 *)0xe000e400 + (x)))

#ifndef LTK_ARCH_ARM_CORTEX_M_MAX_IRQ
  // v6m supports 32 external interrupts (max)
  #define LTK_ARCH_ARM_CORTEX_M_MAX_IRQ          31
#endif

// NOTE: Security Fault is reserved on v6M/v7M but it doesn't hurt to install the vector
#define LTK_ARCH_ARM_CORTEX_M_HARD_FAULT_IRQ     3
#define LTK_ARCH_ARM_CORTEX_M_SEC_FAULT_IRQ      7
#define LTK_ARCH_ARM_CORTEX_M_PENDSV_IRQ         14
#define LTK_ARCH_ARM_CORTEX_M_SYSTICK_IRQ        15

#if !defined(LTK_ARCH_ARM_CORTEX_M_SPLIM_SECTION)
  // Cortex v6m / v7m does not support SPLIM
  #define LTK_ARCH_ARM_CORTEX_M_SPLIM_SECTION
#endif
#define LTK_ARCH_ARM_CORTEX_M_EXC_RETURN         0xfffffffd
#if !defined(LTK_ARCH_ARM_CORTEX_M_SET_EXC_RETURN_SECTION)
  #define LTK_ARCH_ARM_CORTEX_M_SET_EXC_RETURN_SECTION
#endif
#if !defined(LTK_ARCH_ARM_CORTEX_M_CONTEXT_SWITCH_SECTION)
  #define LTK_ARCH_ARM_CORTEX_M_CONTEXT_SWITCH_SECTION \
     "mrs r0, psp                 \n\
      sub r0, r0, #36             \n\
      stmia r0!, {r4-r7}          \n\
      mov r3, r8                  \n\
      mov r4, r9                  \n\
      mov r5, r10                 \n\
      mov r6, r11                 \n\
      mov r7, lr                  \n\
      stmia r0!, {r3-r7}          \n\
      sub r0, r0, #36             \n\
      bl Scheduler                \n\
      ldmia r0!, {r4-r7}          \n\
      ldmia r0!, {r1-r3}          \n\
      mov r8, r1                  \n\
      mov r9, r2                  \n\
      mov r10, r3                 \n\
      ldmia r0!, {r1-r2}          \n\
      mov r11, r1                 \n\
      mov lr, r2                  \n\
      msr psp, r0                 \n\
      bx lr"
#endif

// Cortex-M uses PendSV software interrupt for yielding when required
#define LTKERNEL_STYLE                           LTKERNEL_STYLE_2

typedef struct {
    u32 swSave[8];    // R4-R11
    u32 nExcReturnLR; // Exception Return LR
    u32 hwSave[4];    // R0-R3
    u32 nR12;         // R12
    u32 nLR;          // R14
    u32 nPC;          // R15
    u32 nPSR;         // xPSR
} LTKStackFrame;

enum {
    /* 28 words is enough for FP stack frame, another frame for stack initialization. */
    /* This is a requirement for Cortex M Main, Base doesn't need this much (no FP). */
    kLTK_IdleThreadStackSize    = 112 + sizeof(LTKStackFrame),
    kLTK_DefaultThreadStackSize = 768
};

LT_INLINE bool IsIRQEnabled(s8 nIRQ) {
    volatile u32 * pReg = &LTK_REG(ISER) + (nIRQ >> 5);
    return ((*pReg & (1 << (nIRQ & 0x1f))) > 0);
}

LT_INLINE void EnableIRQ(s8 nIRQ) {
    volatile u32 * pReg = &LTK_REG(ISER) + (nIRQ >> 5);
    *pReg = (1 << (nIRQ & 0x1f));
}

LT_INLINE void DisableIRQ(s8 nIRQ) {
    volatile u32 * pReg = &LTK_REG(ICER) + (nIRQ >> 5);
    *pReg = (1 << (nIRQ & 0x1f));
    asm volatile (
       "dsb              \n\
        isb"
    );
}

LT_INLINE bool InterruptsAreDisabled(void) LT_ISR_SAFE {
    u32 primask;
    asm volatile (
        "mrs %0, primask"
            : "=r" (primask)
    );
    return (primask & 0x1);
}

LT_INLINE bool InsideInterruptContext(void) LT_ISR_SAFE {
    u32 nIRQ;
    asm volatile (
        "mrs %0, ipsr"
            : "=r" (nIRQ)
    );
    return (nIRQ != 0);
}

LT_INLINE void DisableInterrupts(void) {
    asm volatile ( "cpsid if" : : : "memory" );
}

LT_INLINE void EnableInterrupts(void) {
    asm volatile ( "cpsie if" : : : "memory" );
}

/* Nested interrupt calls are required for LT_ISR_SAFE API */
LT_INLINE u32 DisableInterruptsNested(void) LT_ISR_SAFE {
    u32 nMask;
    asm volatile (
       "mrs %0, primask          \n\
        cpsid i"
            : "=r" (nMask) : : "memory"
    );
    return nMask;
}

LT_INLINE void EnableInterruptsNested(u32 nMask) LT_ISR_SAFE {
    // Barrier ensures that pending interrupts execute immediately
    asm volatile (
       "msr primask, %0          \n\
        isb"
            : : "r" (nMask) : "memory"
    );
}

LT_INLINE bool InterruptsGotDisabled(u32 nMask) LT_ISR_SAFE {
    /* Return true if interrupts were originally enabled but then were disabled by last call to DisableInterruptsNested(). */
    return (nMask & 0x1) == 0;
}

LT_INLINE u32 LockScheduler(void) LT_ISR_SAFE {
    return DisableInterruptsNested();
}

LT_INLINE void UnlockScheduler(u32 nMask) {
    EnableInterruptsNested(nMask);
}

LT_INLINE void YieldThread(void) LT_ISR_SAFE {
    LTK_REG(ICSR) = LTK_REG_VAL(ICSR_PENDSV);
    asm volatile ( "dsb" );
}

LT_INLINE void SetThreadState(LTKThread * pThread, LTKThreadState nState) LT_ISR_SAFE {
    asm volatile ( "dmb" );
    pThread->nState = nState;
}

LT_INLINE void *GetStackPointer(void) {
    void *pStack;
    asm volatile (
       "mrs %0, psp"
           : "=r" (pStack) : :
    );
    return pStack;
}

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_BASE_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Oct-21   tiberius    created
 */
