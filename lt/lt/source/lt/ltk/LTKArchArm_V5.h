/******************************************************************************
 * lt/source/lt/ltk/LTKArchArm_V5.h                             LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_H

#include "LTKArchArm_V5_Vectors.h"

#if !defined(__SOFTFP__)
   #error "CPU hardware floating point acceleration is not supported"
#endif

/* Initial CPSR value (ARM / System Mode) */
#define LTK_ARCH_ARM_V5_START_CPSR                   0x1f

// ARM v5 yields using an unmaskable service call
#define LTKERNEL_STYLE                               LTKERNEL_STYLE_1

typedef struct {
    u32 nRegister[LTK_ARCH_ARM_V5_SF_SIZE / 4];
} LTKStackFrame;

enum {
    kLTK_IdleThreadStackSize    = sizeof(LTKStackFrame) + 512,
    kLTK_DefaultThreadStackSize = 1024
};

LT_INLINE void DataSynchronizationBarrier(void) {
    asm volatile ( "mcr p15, 0, r0, c7, c10, 4" : : : "memory" );
}

LT_INLINE bool InterruptsAreDisabled(void) LT_ISR_SAFE {
    register u32 nTemp;
    asm volatile (
        "mrs %0, cpsr"
            : "=r" (nTemp) : :
    );
    return (nTemp & 0xc0) > 0;
}

LT_INLINE bool InsideInterruptContext(void) LT_ISR_SAFE {
    extern volatile u32 _LTK_interruptNestingCounter;
    return _LTK_interruptNestingCounter > 0;
}

LT_INLINE u32 DisableInterruptsNested(void) LT_ISR_SAFE {
    u32 nMask;
    register u32 nTemp;
    asm volatile (
       "mrs %0, cpsr             \n\
        orr %1, %0, #0xc0        \n\
        msr cpsr_c, %1"
          : "=r" (nMask), "=r" (nTemp) : : "memory"
    );
    return nMask;
}

LT_INLINE void EnableInterruptsNested(u32 nMask) LT_ISR_SAFE {
    asm volatile (
       "msr cpsr_c, %0"
          : : "r" (nMask) : "memory"
    );
}

LT_INLINE bool InterruptsGotDisabled(u32 nMask) LT_ISR_SAFE {
    /* Return true if interrupts were originally enabled but then were disabled by last call to DisableInterruptsNested(). */
    return !(nMask & 0xc0);
}

LT_INLINE u32 LockScheduler(void) LT_ISR_SAFE {
    return DisableInterruptsNested();
}

LT_INLINE void UnlockScheduler(u32 nMask) LT_ISR_SAFE {
    EnableInterruptsNested(nMask);
}

LT_INLINE void YieldThread(void) LT_ISR_SAFE {
    /* SWI is actually an unmaskable service call */
    asm volatile ( "swi 0" );
}

LT_INLINE void SetThreadState(LTKThread * pThread, LTKThreadState nState) {
    DataSynchronizationBarrier();
    pThread->nState = nState;
}

LT_INLINE void BreakIntoOCD(void) {
    asm volatile ( "bkpt 0" );
}

LT_INLINE void *GetStackPointer(void) {
    void *pStack;
    asm volatile (
       "mov %0, sp"
           : "=r"(pStack) : :
    );
    return pStack;
}

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Sep-23   tiberius    created
 */
