/******************************************************************************
 * lt/source/lt/ltk/LTKArchXtensa_Vectors.h                     LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_VECTORS_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_VECTORS_H

// Exception types used for faults
#define LTK_ARCH_XTENSA_USER_FAULT         0
#define LTK_ARCH_XTENSA_KERNEL_FAULT       1
#define LTK_ARCH_XTENSA_DOUBLE_FAULT       2

// Base Save Area (potentially contains caller a0-a3 on window overflow)
#define LTK_ARCH_XTENSA_BSA_SIZE           16

// Nested Function Area (Xtensa C ABI quirk to support GCC nested functions)
#define LTK_ARCH_XTENSA_NFA_SIZE           16

//  Stack Frame Size (Registers)
//   a0, a2 -> a15 / SCOMPARE1 / LBEG / LEND / LCOUNT / SAR / PS / EPC_#
#define LTK_ARCH_XTENSA_SF_SIZE            (22 * 4)

// Pointer offset with  BSA/NFA (high memory) and Stack Frame (lower memory)
#define LTK_ARCH_XTENSA_STACK_PTR_ADJ      \
    (LTK_ARCH_XTENSA_BSA_SIZE + LTK_ARCH_XTENSA_NFA_SIZE + LTK_ARCH_XTENSA_SF_SIZE)

// PS = Window Mode + User Mode + Exception Mode + Call Increment 4 (call4)
#define LTK_ARCH_XTENSA_START_PS           \
    (PS_WOE_MASK | PS_UM_MASK | PS_EXCM_MASK | PS_CALLINC(1))

// Register offsets in stack frame
#define LTK_ARCH_XTENSA_FRAME_A0            0
// NB: A1 (the stack pointer) does NOT need to be included in the stack frame and is therefore skipped.
#define LTK_ARCH_XTENSA_FRAME_A2            4
#define LTK_ARCH_XTENSA_FRAME_A4           12
#define LTK_ARCH_XTENSA_FRAME_A6           20
#define LTK_ARCH_XTENSA_FRAME_SCOMPARE1    60
#define LTK_ARCH_XTENSA_FRAME_LBEG         64
#define LTK_ARCH_XTENSA_FRAME_LEND         68
#define LTK_ARCH_XTENSA_FRAME_LCOUNT       72
#define LTK_ARCH_XTENSA_FRAME_SAR          76
#define LTK_ARCH_XTENSA_FRAME_PS           80
#define LTK_ARCH_XTENSA_FRAME_EPC          84
#define LTK_FRAME(REG)                     LTK_ARCH_XTENSA_FRAME_ ## REG
#define LTK_FRAME_WORD(REG)                ((LTK_ARCH_XTENSA_FRAME_ ## REG) / 4)

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHXTENSA_VECTORS_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Apr-22   tiberius    created
 */
