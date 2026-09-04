/******************************************************************************
 * lt/source/lt/ltk/LTKArchRISC_V_Vectors.h                     LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_VECTORS_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_VECTORS_H

// Integer context size (offsets 0 .. LTK_ARCH_RISC_V_FRAME_MEPC)
#define LTK_ARCH_RISC_V_INT_SF_SIZE        (30 * 4)

// Environment call (syscall) mcause code
#define LTK_ARCH_RISC_V_MCAUSE_ECALL       11

// Register offsets in stack frame
#define LTK_ARCH_RISC_V_FRAME_A0           0
#define LTK_ARCH_RISC_V_FRAME_MSTATUS      108
#define LTK_ARCH_RISC_V_FRAME_RA           112
#define LTK_ARCH_RISC_V_FRAME_MEPC         116
#define LTK_FRAME(REG)                     LTK_ARCH_RISC_V_FRAME_ ## REG
#define LTK_FRAME_WORD(REG)                ((LTK_ARCH_RISC_V_FRAME_ ## REG) / 4)

#if defined(__riscv_flen)
#if __riscv_flen == 64
#define LTK_ARCH_RISC_V_FP_LOAD            fld
#define LTK_ARCH_RISC_V_FP_STORE           fsd
#define LTK_ARCH_RISC_V_FP_REG_SIZE        8
#else
#define LTK_ARCH_RISC_V_FP_LOAD            flw
#define LTK_ARCH_RISC_V_FP_STORE           fsw
#define LTK_ARCH_RISC_V_FP_REG_SIZE        4
#endif // #if __riscv_flen == 64
// FP context is stacked above the integer context in the same frame, so the
// saved stack pointer is the thread's true low-water mark. Slots hold f0-f31,
// FCSR, and one pad keeping the frame size 8-byte aligned.
#define LTK_ARCH_RISC_V_FP_FRAME_SIZE      (34 * LTK_ARCH_RISC_V_FP_REG_SIZE)
#define LTK_FP_FRAME(N)                    (LTK_ARCH_RISC_V_INT_SF_SIZE + (N) * LTK_ARCH_RISC_V_FP_REG_SIZE)
#define LTK_ARCH_RISC_V_FRAME_FCSR         LTK_FP_FRAME(32)
#define LTK_ARCH_RISC_V_SF_SIZE            (LTK_ARCH_RISC_V_INT_SF_SIZE + LTK_ARCH_RISC_V_FP_FRAME_SIZE)
#else
#define LTK_ARCH_RISC_V_SF_SIZE            LTK_ARCH_RISC_V_INT_SF_SIZE
#endif // #if defined(__riscv_flen)

// Stack frame adjustment for context switch (whole integer + FP frame)
#define LTK_ARCH_RISC_V_STACK_PTR_ADJ      LTK_ARCH_RISC_V_SF_SIZE

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_VECTORS_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jul-23   tiberius    created
 */
