/******************************************************************************
 * lt/source/lt/ltk/LTKArchRISC_V.h                             LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_H

#include "LTKArchRISC_V_Vectors.h"

#if __riscv_xlen != 32 || defined(__riscv_abi_rve)
    #error "RISC-V core must be rv32i"
#endif

#if defined(__riscv_float_abi_double) || (defined(__riscv_flen) && __riscv_flen == 64)
    #error "RISC-V double precision float is NOT supported"
#endif

#if 0
  /* TODO: Do I need to do anything about this? */
  #if __riscv_cmodel_medlow
      /* TODO: This is what the BL702L has . */
      #warning "RISC-V medlow cmodel"
  #elif __riscv_cmodel_medany
      #warning "RISC-V medany cmodel"
  #endif
#endif

/* Initial mstatus value (set MPIE to enable interrupts, MPP to machine mode) */
#define LTK_ARCH_RISC_V_START_MSTATUS       0x1880

/* Initial mstatus value (set FS to enable hardware FP support) */
#define LTK_ARCH_RISC_V_START_MSTATUS_FP    0x3880

// RISC-V yields using an unmaskable service call
#define LTKERNEL_STYLE                      LTKERNEL_STYLE_1

typedef struct {
    u32 nRegister[LTK_ARCH_RISC_V_SF_SIZE / 4];
} LTKStackFrame;

enum {
    kLTK_IdleThreadStackSize    = sizeof(LTKStackFrame) + 128,
    /* RISC-V has more expensive stacking requirements than ARM. */
    kLTK_DefaultThreadStackSize = 512 + 128
};

LT_INLINE bool InterruptsAreDisabled(void) LT_ISR_SAFE {
    u32 nStatus;
    asm volatile (
        "csrr %0, mstatus"
            : "=r"(nStatus) : : "memory"
    );
    return !(nStatus & 0x8);
}

LT_INLINE bool InsideInterruptContext(void) LT_ISR_SAFE {
    extern u32 _LTK_nTrapNestingCounter;
    return (_LTK_nTrapNestingCounter > 0);
}

LT_INLINE u32 DisableInterruptsNested(void) LT_ISR_SAFE {
    u32 nMask;
    asm volatile (
        "csrrci %0, mstatus, 1 << 3        \n\
         fence"
            : "=r"(nMask) : : "memory"
    );
    return nMask;
}

LT_INLINE void EnableInterruptsNested(u32 nMask) LT_ISR_SAFE {
    asm volatile (
        "fence                             \n\
         csrw mstatus, %0"
            : : "r"(nMask) : "memory"
    );
}

LT_INLINE bool InterruptsGotDisabled(u32 nMask) LT_ISR_SAFE {
    /* Return true if interrupts were originally enabled but then were disabled by last call to DisableInterruptsNested(). */
    return (nMask & 0x8) > 0;
}

LT_INLINE u32 LockScheduler(void) LT_ISR_SAFE {
    u32 nMask;
    asm volatile (
        "csrrci %0, mstatus, 1 << 3        \n\
         fence"
            : "=r"(nMask) : : "memory"
    );
    return nMask;
}

LT_INLINE void UnlockScheduler(u32 nMask) LT_ISR_SAFE {
    asm volatile (
        "fence                             \n\
         csrw mstatus, %0"
            : : "r"(nMask) : "memory"
    );
}

LT_INLINE void YieldThread(void) LT_ISR_SAFE {
    asm volatile ( "ecall" );
}

LT_INLINE void SetThreadState(LTKThread * pThread, LTKThreadState nState) {
    asm volatile ( "fence w, rw" ::: "memory" );
    pThread->nState = nState;
}

LT_INLINE void BreakIntoOCD(void) {
    asm volatile ( "ebreak" );
}

LT_INLINE void *GetStackPointer(void) {
    void *pStack;
    asm volatile (
       "mv %0, sp"
           : "=r"(pStack) : :
    );
    return pStack;
}

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Jul-23   tiberius    created
 */
