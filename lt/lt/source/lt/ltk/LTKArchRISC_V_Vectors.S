/******************************************************************************
 * lt/source/lt/ltk/LTKArchRISC_V_Vectors.S                     LTK MicroKernel
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2023, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKArchRISC_V_Vectors.h"

/* Implementation Notes:
 *   The gp register is initialized in BSP prior to run.
 *   The tp register is used to store the scheduler run mode.
 *   This implementation (by default) keeps interrupts locked out during interrupt
 *     service. If interrupt nesting is desired interrupts may be re-enabled in the
 *     BSP interrupt handler, but the following rules must be followed:
 *       (1) Interrupts must be re-disabled prior to handler return.
 *       (2) The BSP interrupt handler and/or CLIC hardware must manage the interrupt
 *             priority scheme to prevent arbitrary interrupt nesting. */

/************************* Zero-init Data Section *****************************/

    .bss
    .global   _LTK_pInterruptStackTop
    .global   _LTK_nTrapNestingCounter
    /* After saving context on thread stack, system switches to interrupt stack.
     * NOTE: nTrapNestingCounter must immediately follow pInterruptStackTop. */
    .balign   8, 0
_LTK_pInterruptStackTop:
    .space    4, 0
_LTK_nTrapNestingCounter:
    .space    4, 0

/****************************** Text Section *********************************/

    .text
    .global   _LTKDispatcher
    .global   _LTKFaultHandler

    /* Both exceptions and interrupts are considered traps and are handled here.
     *   64 byte alignment is needed for CLIC direct vector MODE = 2. */
    .global   _LTKMachineTrapHandler
    .type     _LTKMachineTrapHandler, @function
    .balign   64, 0
_LTKMachineTrapHandler:
    /* Save Context */
    addi      sp, sp, -LTK_ARCH_RISC_V_STACK_PTR_ADJ
    sw        a0, LTK_FRAME(A0)(sp)
    sw        a1,    4(sp)
    sw        a2,    8(sp)
    sw        a3,   12(sp)
    sw        a4,   16(sp)
    sw        a5,   20(sp)
    sw        a6,   24(sp)
    sw        a7,   28(sp)
    sw        fp,   32(sp)  /* also known as s0 */
    sw        s1,   36(sp)
    sw        s2,   40(sp)
    sw        s3,   44(sp)
    sw        s4,   48(sp)
    sw        s5,   52(sp)
    sw        s6,   56(sp)
    sw        s7,   60(sp)
    sw        s8,   64(sp)
    sw        s9,   68(sp)
    sw        s10,  72(sp)
    sw        s11,  76(sp)
    sw        t0,   80(sp)
    sw        t1,   84(sp)
    sw        t2,   88(sp)
    sw        t3,   92(sp)
    sw        t4,   96(sp)
    sw        t5,  100(sp)
    sw        t6,  104(sp)
    csrr      a0, mstatus
    sw        a0, LTK_FRAME(MSTATUS)(sp)
    sw        ra, LTK_FRAME(RA)(sp)
    mv        a0, sp

    /* Increment nesting counter and potentially switch to interrupt stack */
    /* &_LTK_nTrapNestingCounter = &_LTK_pInterruptStackTop + 4 */
    la        s0, _LTK_pInterruptStackTop
    lw        a2, 4(s0)
    /* Skip stack switch if nested trap or scheduler is not running yet */
    bgtz      a2, 1f
    beqz      tp, 1f
    /* Switch to system stack for traps */
    lw        sp, 0(s0)
1:
    addi      a2, a2, 1
    sw        a2, 4(s0)
    csrr      t0, mepc

    /* Is this an interrupt or an exception? */
    csrr      a1, mcause
    li        t1, 1 << 31
    bgeu      a1, t1, Dispatch

Exception:
    /* Mask off MSBs in case CLIC (or other hardware) puts something there */
    andi      a1, a1, 0x3ff
    /* Redirect an "environment call" to the dispatcher */
    li        t1, LTK_ARCH_RISC_V_MCAUSE_ECALL
    beq       t1, a1, SystemCall
    sw        t0, LTK_FRAME(MEPC)(a0)
    csrr      a3, mtval
    j         _LTKFaultHandler
    /* Unreachable */

SystemCall:
    /* Dispatch on machine environment call, adjusting PC for return */
    addi      t0, t0, 4

Dispatch:
    /* Interrupts remain disabled upon entering dispatcher, and shall be disabled after exit. */
    sw        t0, LTK_FRAME(MEPC)(a0)
    call      _LTKDispatcher

    /* Only change stack pointer if not NULL */
    beqz      a0, Restore
    mv        sp, a0

Restore:
    /* Decrement nesting counter (s0 = &pInterruptStackTop) */
    lw        t0, 4(s0)
    addi      t0, t0, -1
    sw        t0, 4(s0)

    /* Restore Context */
    lw        a0, LTK_FRAME(MSTATUS)(sp)
    csrw      mstatus, a0
    lw        ra, LTK_FRAME(RA)(sp)
    lw        a0, LTK_FRAME(MEPC)(sp)
    csrw      mepc, a0
    lw        a0, LTK_FRAME(A0)(sp)
    lw        a1,    4(sp)
    lw        a2,    8(sp)
    lw        a3,   12(sp)
    lw        a4,   16(sp)
    lw        a5,   20(sp)
    lw        a6,   24(sp)
    lw        a7,   28(sp)
    lw        fp,   32(sp)
    lw        s1,   36(sp)
    lw        s2,   40(sp)
    lw        s3,   44(sp)
    lw        s4,   48(sp)
    lw        s5,   52(sp)
    lw        s6,   56(sp)
    lw        s7,   60(sp)
    lw        s8,   64(sp)
    lw        s9,   68(sp)
    lw        s10,  72(sp)
    lw        s11,  76(sp)
    lw        t0,   80(sp)
    lw        t1,   84(sp)
    lw        t2,   88(sp)
    lw        t3,   92(sp)
    lw        t4,   96(sp)
    lw        t5,  100(sp)
    lw        t6,  104(sp)
    addi      sp, sp, LTK_ARCH_RISC_V_STACK_PTR_ADJ
    mret

