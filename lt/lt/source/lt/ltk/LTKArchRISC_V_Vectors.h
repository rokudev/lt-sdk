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

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHRISC_V_VECTORS_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jul-23   tiberius    created
 */
