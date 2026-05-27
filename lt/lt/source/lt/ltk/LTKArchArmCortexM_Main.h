/******************************************************************************
 * lt/source/lt/ltk/LTKArchArmCortexM_Main.h                    LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_MAIN_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_MAIN_H

#if defined (__VFP_FP__) && !defined(__SOFTFP__)
  #define LTK_ARCH_ARM_CORTEX_M_ENABLE_LAZY_FP     true
  // Context switch for hardware floating point (lazy stacking)
  #define LTK_ARCH_ARM_CORTEX_M_CONTEXT_SWITCH_SECTION \
      "mrs r0, psp                \n\
       tst lr, #16                \n\
       it eq                      \n\
       vstmdbeq r0!, { s16-s31 }  \n\
       stmdb r0!, { r4-r11, lr }  \n\
       bl Scheduler               \n\
       ldmfd r0!, { r4-r11, lr }  \n\
       tst lr, #16                \n\
       it eq                      \n\
       vldmiaeq r0!, { s16-s31 }  \n\
       msr psp, r0                \n\
       bx lr"
#else
  #define LTK_ARCH_ARM_CORTEX_M_ENABLE_LAZY_FP     false
  // Context switch without hardware floating point
  #define LTK_ARCH_ARM_CORTEX_M_CONTEXT_SWITCH_SECTION \
     "mrs r0, psp                 \n\
      stmdb r0!, { r4-r11, lr }   \n\
      bl Scheduler                \n\
      ldmfd r0!, { r4-r11, lr }   \n\
      msr psp, r0                 \n\
      bx lr"
#endif

// ARM Cortex-M Register Access

// SCB:CCR
#define LTK_ARCH_ARM_CORTEX_M_REG_CCR            (*(volatile u32 *)0xe000ed14)
// Divide by zero trap and unintentional unaligned accesses
#define LTK_ARCH_ARM_CORTEX_M_VAL_DIV0_TRAP      (0x1 << 4)
#define LTK_ARCH_ARM_CORTEX_M_VAL_UNALIGN_TRAP   (0x1 << 3)

// SCB:SHCSR
#define LTK_ARCH_ARM_CORTEX_M_REG_SHCSR          (*(volatile u32 *)0xe000ed24)
// Memory faults, bus faults, usage faults (and security faults for v8m)
#define LTK_ARCH_ARM_CORTEX_M_VAL_FAULT_ENABLE   (0xf << 16)

// Fault status registers
#define LTK_ARCH_ARM_CORTEX_M_REG_CFSR           (*(volatile u32 *)0xe000ed28)
#define LTK_ARCH_ARM_CORTEX_M_REG_HFSR           (*(volatile u32 *)0xe000ed2c)
#define LTK_ARCH_ARM_CORTEX_M_REG_MMFAR          (*(volatile u32 *)0xe000ed34)
#define LTK_ARCH_ARM_CORTEX_M_REG_BFAR           (*(volatile u32 *)0xe000ed38)
#define LTK_ARCH_ARM_CORTEX_M_REG_AFSR           (*(volatile u32 *)0xe000ed3c)
#define LTK_ARCH_ARM_CORTEX_M_REG_SFSR           (*(volatile u32 *)0xe000ede4)
#define LTK_ARCH_ARM_CORTEX_M_REG_SFAR           (*(volatile u32 *)0xe000ede8)

// SCB:CPACR
#define LTK_ARCH_ARM_CORTEX_M_REG_CPACR          (*(volatile u32 *)0xe000ed88)
#define LTK_ARCH_ARM_CORTEX_M_VAL_FPU_ENABLE     (0x3 << 20)

// Floating point registers
#define LTK_ARCH_ARM_CORTEX_M_REG_FPCCR          (*(volatile u32 *)0xe000ef34)
#define LTK_ARCH_ARM_CORTEX_M_VAL_LAZY_STACKING  (0x3 << 30)

// Interrupts
#define LTK_ARCH_ARM_CORTEX_M_REG_IPR(x)         (*((volatile u8 *)0xe000e400 + (x)))
#define LTK_ARCH_ARM_CORTEX_M_REG_SHPR(x)        (*((volatile u8 *)0xe000ed18 + (x)))

#ifndef LTK_ARCH_ARM_CORTEX_M_MAX_IRQ
  #define LTK_ARCH_ARM_CORTEX_M_MAX_IRQ          239
#endif

#if 0
LT_INLINE u32 LockScheduler(void) {
    extern u8 _LTK_nInterruptPriMaskLow;
    asm volatile (
        "msr basepri, %0\n"
        "isb"
            : : "r" (_LTK_nInterruptPriMaskLow) : "memory"
    );
    return 0;
}

LT_INLINE void UnlockScheduler(u32 nMask) {
    asm volatile (
        "msr basepri, %0\n"
        "isb"
            : : "r" (nMask) : "memory"
    );
}
#endif

#include "LTKArchArmCortexM_Base.h"

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_MAIN_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-May-21   tiberius    created
 */
