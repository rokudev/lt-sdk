/******************************************************************************
 * lt/source/lt/ltk/LTKArchArm_V5_Vectors.h                     LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_VECTORS_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_VECTORS_H

// Stack Frame Size
#define LTK_ARCH_ARM_V5_SF_SIZE            (16 * 4)

// Stack frame adjustment for context switch
#define LTK_ARCH_ARM_V5_STACK_PTR_ADJ      LTK_ARCH_ARM_V5_SF_SIZE

/*
 *  Context as stored on stack:
 *   60: lr     (high-memory)
 *   56: r12
 *     ...
 *    8: r0
 *    4: pc
 *    0: cpsr   (low-memory)
 */

// Register offsets in stack frame
#define LTK_ARCH_ARM_V5_FRAME_CPSR         0
#define LTK_ARCH_ARM_V5_FRAME_PC           4  /* R15 */
#define LTK_ARCH_ARM_V5_FRAME_R0           8
#define LTK_ARCH_ARM_V5_FRAME_LR           60 /* R14 */
#define LTK_FRAME(REG)                     LTK_ARCH_ARM_V5_FRAME_ ## REG
#define LTK_FRAME_WORD(REG)                ((LTK_ARCH_ARM_V5_FRAME_ ## REG) / 4)

// Dispatch mode
#define LTK_DISPATCH_MODE_SWI              0  /* reschedule thread (init scheduler on first SWI) */
#define LTK_DISPATCH_MODE_IRQ              1  /* reschedule thread, dispatch irq */
#define LTK_DISPATCH_MODE_FIQ              2  /* reschedule thread, dispatch fiq */
#define LTK_DISPATCH_MODE_FIQ_NOSCHEDULER  3  /* dispatch fiq only */

// Fault Types
#define LTK_FAULT_ILLEGAL_INSTRUCTION      0xdb
#define LTK_FAULT_PREFETCH_ABORT           0xd7
#define LTK_FAULT_DATA_ABORT               (LTK_FAULT_PREFETCH_ABORT + 0x100)

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARM_V5_VECTORS_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Sep-23   tiberius    created
*/
