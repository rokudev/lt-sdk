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

// Stack Frame Size
#define LTK_ARCH_RISC_V_SF_SIZE            (30 * 4)

// Stack frame adjustment for context switch
#define LTK_ARCH_RISC_V_STACK_PTR_ADJ      LTK_ARCH_RISC_V_SF_SIZE

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
#define LTK_ARCH_RISC_V_FP_STACK_PTR_ADJ   (34 * LTK_ARCH_RISC_V_FP_REG_SIZE)
// Saved FCSR is immediately below the integer frame base. Its slot is one FP
// register wide, so this offset tracks both FLEN=32 and FLEN=64 frame layouts.
#define LTK_ARCH_RISC_V_FCSR_FRAME_OFFSET (-LTK_ARCH_RISC_V_FP_REG_SIZE)
#endif // #if defined(__riscv_flen)

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_VECTORS_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jul-23   tiberius    created
 */
