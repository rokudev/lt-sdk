/******************************************************************************
 * lt/source/lt/core/ltk/LTKArchXtensa_Vectors.S                LTK Microkernel
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2022-2023, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <xtensa/config/core.h>
#include <xtensa/config/specreg.h>

#include "LTKArchXtensa_Vectors.h"

/****************************** Vector Table *********************************/

/* Debug Exception Vector */
#if XCHAL_HAVE_DEBUG
    .begin    literal_prefix .DebugExceptionVector
    .section  .DebugExceptionVector.text, "ax"
    .global   _DebugExceptionVector
    .align    4
_DebugExceptionVector:
    j         _DebugExceptionVector
    /* Does not return */
    .end      literal_prefix
#endif

/* Double Exception Vector */
#ifdef XCHAL_DOUBLEEXC_VECTOR_VADDR
    .begin    literal_prefix .DoubleExceptionVector
    .section  .DoubleExceptionVector.text, "ax"
    .global   _DoubleExceptionVector
    .align    4
_DoubleExceptionVector:
    wsr       a0, EXCSAVE1
    call0     _DoubleFaultHandler
    /* Does not return */
    .end      literal_prefix
#endif

/* Kernel Exception Vector */
    .begin    literal_prefix .KernelExceptionVector
    .section  .KernelExceptionVector.text, "ax"
    .global   _KernelExceptionVector
    .align    4
_KernelExceptionVector:
    wsr       a0, EXCSAVE1
    call0     _KernelFaultHandler
    /* Does not return */
    .end      literal_prefix

/*
 * User Exception Vector (EXCCAUSE)
 *   syscall      -> call C dispatcher (1)
 *   L1 interrupt -> call C dispatcher (4)
 *   alloca       -> call alloca asm handler (5)
 *   error        -> call panic handler (0, 2, 3, 6-)
 *   TODO: coprocessor
 */
    .begin    literal_prefix .UserExceptionVector
    .section  .UserExceptionVector.text, "ax"
    .global   _UserExceptionVector
    .type     _UserExceptionVector, @function
    .align    4
_UserExceptionVector:
    wsr       a0, EXCSAVE1
    call0     _UserException
    /* Does not return */
    .end      literal_prefix

/*
 * Medium Priority Exception Vectors
 */
#if XCHAL_EXCM_LEVEL >= 2
    .begin    literal_prefix .Level2InterruptVector
    .section  .Level2InterruptVector.text, "ax"
    .global   _Level2ExceptionVector
    .type     _Level2ExceptionVector, @function
    .align    4
_Level2ExceptionVector:
     wsr       a0, EXCSAVE_2
     call0     _Level2Exception
     /* Does not return */
    .end      literal_prefix
#endif

#if XCHAL_EXCM_LEVEL >= 3
    .begin    literal_prefix .Level3InterruptVector
    .section  .Level3InterruptVector.text, "ax"
    .global   _Level3ExceptionVector
    .type     _Level3ExceptionVector, @function
    .align    4
_Level3ExceptionVector:
     wsr       a0, EXCSAVE_3
     call0     _Level3Exception
     /* Does not return */
    .end      literal_prefix
#endif

#if XCHAL_EXCM_LEVEL >= 4
    .begin    literal_prefix .Level4InterruptVector
    .section  .Level4InterruptVector.text, "ax"
    .global   _Level4ExceptionVector
    .type     _Level4ExceptionVector, @function
    .align    4
_Level4ExceptionVector:
     wsr       a0, EXCSAVE_4
     call0     _Level4Exception
     /* Does not return */
    .end      literal_prefix
#endif

#if XCHAL_EXCM_LEVEL >= 5
    .begin    literal_prefix .Level5InterruptVector
    .section  .Level5InterruptVector.text, "ax"
    .global   _Level5ExceptionVector
    .type     _Level5ExceptionVector, @function
    .align    4
_Level5ExceptionVector:
     wsr       a0, EXCSAVE_5
     call0     _Level5Exception
     /* Does not return */
    .end      literal_prefix
#endif

#if XCHAL_EXCM_LEVEL >= 6
    .begin    literal_prefix .Level6InterruptVector
    .section  .Level6InterruptVector.text, "ax"
    .global   _Level6ExceptionVector
    .type     _Level6ExceptionVector, @function
    .align    4
_Level6ExceptionVector:
     wsr       a0, EXCSAVE_6
     call0     _Level6Exception
     /* Does not return */
    .end      literal_prefix
#endif

/****************************** Data Section *********************************/

    .data
/*
 * The thread stack pointer points to the bottom of the Base Save Area (BSA)
 *   and is saved aside during interrupt processing
 */
    .align    4
    .global   _LTK_pThreadStackBSA
_LTK_pThreadStackBSA:
    .word     0
/*
 * After saving context on thread stack, system switches to interrupt stack
 *   for interrupt handling (including nested interrupts).
 */
    .align    16
InterruptStack:
    .space    4096, 0
/* The first 16 bytes of stack (at the top) is for the Base Save Area (BSA). */
InterruptStackBSA:
    .space    16, 0
    .global   _LTK_interruptStackTop
_LTK_interruptStackTop:

/****************************** Text Section *********************************/

    .section .iram1, "ax"
    .global   _LTKDispatcher
    .global   _LTKFaultHandler
    .global   _LTK_interruptNestingCounter
    .global   _LTKThreadExit

#ifdef XCHAL_DOUBLEEXC_VECTOR_VADDR
_DoubleFaultHandler:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, DEPC
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE1
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, PS
    s32i      a0, a1, LTK_FRAME(PS)
    movi      a0, LTK_ARCH_XTENSA_DOUBLE_FAULT
    wsr       a0, EXCSAVE1
    j         _FaultHandler
#endif

_KernelFaultHandler:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC1
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE1
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, PS
    s32i      a0, a1, LTK_FRAME(PS)
    movi      a0, LTK_ARCH_XTENSA_KERNEL_FAULT
    wsr       a0, EXCSAVE1
    j         _FaultHandler

/*
 * Fault handler (invoke on all error exceptions)
 *   On call:
 *     EPC#     - must already be saved
 *     EPS#     - must already be saved
 *     a0       - must already be saved
 *     EXCSAVE1 - Load with exception type
 *   Does not return
 */
    .type     _FaultHandler, @function
    .align    4
_FaultHandler:
    call0     _SaveContext
    rsr       a6, EXCSAVE1
    movi      a7, _LTK_pThreadStackBSA
    l32i      a7, a7, 0
    addi      a7, a7, LTK_ARCH_XTENSA_BSA_SIZE
    mov       a8, a1
    movi      a0, PS_INTLEVEL(XCHAL_EXCM_LEVEL) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKFaultHandler
    /* Does not return */

/*
 * Save context and potentially switch from user to system stack
 *   On call:
 *     EPC#     - must already be saved
 *     EPS#     - must already be saved
 *     a0       - must already be saved
 *     a0       - call0 places return address here
 *     a1       - interrupted stack pointer
 *   On return:
 *     a0/a2/a3 - altered
 *     a1       - system stack pointer (possibly altered)
 *     a2-a15   - context saved
 *     Misc SRs - context saved
 *     LCOUNT   - cleared
 */
    .type     _SaveContext, @function
    .align    4
_SaveContext:
    s32i      a2,  a1,  4
    s32i      a3,  a1,  8
    s32i      a4,  a1, 12
    s32i      a5,  a1, 16
    s32i      a6,  a1, 20
    s32i      a7,  a1, 24
    s32i      a8,  a1, 28
    s32i      a9,  a1, 32
    s32i      a10, a1, 36
    s32i      a11, a1, 40
    s32i      a12, a1, 44
    s32i      a13, a1, 48
    s32i      a14, a1, 52
    s32i      a15, a1, 56
#if XCHAL_HAVE_S32C1I
    rsr       a2, SCOMPARE1
    s32i      a2, a1, LTK_FRAME(SCOMPARE1)
#endif
#if XCHAL_HAVE_LOOPS
    rsr       a2, LBEG
    s32i      a2, a1, LTK_FRAME(LBEG)
    rsr       a2, LEND
    s32i      a2, a1, LTK_FRAME(LEND)
    rsr       a2, LCOUNT
    s32i      a2, a1, LTK_FRAME(LCOUNT)
    movi      a2, 0
    wsr       a2, LCOUNT
#endif
    rsr       a2, SAR
    s32i      a2, a1, LTK_FRAME(SAR)
    isync

    /* Increment nesting counter */
    movi      a2, _LTK_interruptNestingCounter
    l32i      a3, a2, 0
    addi      a3, a3, 1
    s32i      a3, a2, 0

    /* Locate next BSA (for nested interrupt) and current BSA (a2) */
    addi      a1, a1, -LTK_ARCH_XTENSA_BSA_SIZE
    addi      a2, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    bnei      a3, 1, 1f

    /* Save thread stack pointer BSA and switch to interrupt stack BSA */
    movi      a3, _LTK_pThreadStackBSA
    s32i      a1, a3, 0
    movi      a1, InterruptStackBSA
1:
    /* Copy BSA */
    l32i      a3, a2, 0
    s32i      a3, a1, 0
    l32i      a3, a2, 4
    s32i      a3, a1, 4
    l32i      a3, a2, 8
    s32i      a3, a1, 8
    l32i      a3, a2, 12
    s32i      a3, a1, 12

    /* Reset SP */
    addi      a1, a1, LTK_ARCH_XTENSA_BSA_SIZE
    ret

/*
 * Restore context and potentially switch from system to user stack
 *   On call:
 *     a0       - call0 places return address here
 *     a1       - system stack pointer
 *     a6       - context switch stack pointer -OR- null pointer
 *   On return:
 *     a1       - interrupted stack pointer (possibly altered)
 *     a2-a15   - context restored/switched
 *     Misc SRs - context restored/switched
 */
    .type     _RestoreContext, @function
    .align    4
_RestoreContext:
    /* Lock out interrupts for counter */
    movi      a2, PS_EXCM
    wsr       a2, PS
    rsync

    /* Decrement nesting counter */
    movi      a2, _LTK_interruptNestingCounter
    l32i      a3, a2, 0
    addi      a3, a3, -1
    s32i      a3, a2, 0

    /* Locate next BSA (for nested interrupt) and current BSA (a2) */
    addi      a1, a1, -LTK_ARCH_XTENSA_BSA_SIZE
    mov       a2, a1

    bnei      a3, 0, 1f

    /* Restore thread stack pointer BSA */
    movi      a3, _LTK_pThreadStackBSA
    l32i      a1, a3, 0
1:
    /* Copy BSA */
    l32i      a3, a2, 0
    s32i      a3, a1, (LTK_ARCH_XTENSA_STACK_PTR_ADJ)
    l32i      a3, a2, 4
    s32i      a3, a1, (LTK_ARCH_XTENSA_STACK_PTR_ADJ + 4)
    l32i      a3, a2, 8
    s32i      a3, a1, (LTK_ARCH_XTENSA_STACK_PTR_ADJ + 8)
    l32i      a3, a2, 12
    s32i      a3, a1, (LTK_ARCH_XTENSA_STACK_PTR_ADJ + 12)

    /* Set SP above BSA */
    addi      a1, a1, LTK_ARCH_XTENSA_BSA_SIZE

    /* Save new thread sp so it doesn't get clobbered */
    mov       a2, a6
2:
    /* Restore a4-a15 first for potential window spill */
    l32i      a4,  a1, 12
    l32i      a5,  a1, 16
    l32i      a6,  a1, 20
    l32i      a7,  a1, 24
    l32i      a8,  a1, 28
    l32i      a9,  a1, 32
    l32i      a10, a1, 36
    l32i      a11, a1, 40
    l32i      a12, a1, 44
    l32i      a13, a1, 48
    l32i      a14, a1, 52
    l32i      a15, a1, 56

    /* Maintain current stack -OR- switch to new thread stack pointer BSA */
    beqi      a2, 0, 3f

    /* Spill the windows (save extended registers to stack for context switch) */

    /* EXCSAVE1/EPC1 are safe since EXCM_LEVEL is locked */
    wsr       a2, EXCSAVE1
    wsr       a0, EPC1
    /* a1 = sp _prior_ to the first nested interrupt */
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    call0     xthal_window_spill_nw
    rsr       a1, EXCSAVE1
    rsr       a0, EPC1
    movi      a2, 0
    j         2b
3:
#if XCHAL_HAVE_S32C1I
    l32i      a2, a1, LTK_FRAME(SCOMPARE1)
    wsr       a2, SCOMPARE1
#endif
#if XCHAL_HAVE_LOOPS
    l32i      a2, a1, LTK_FRAME(LBEG)
    wsr       a2, LBEG
    l32i      a2, a1, LTK_FRAME(LEND)
    wsr       a2, LEND
    l32i      a2, a1, LTK_FRAME(LCOUNT)
    wsr       a2, LCOUNT
#endif
    l32i      a2, a1, LTK_FRAME(SAR)
    wsr       a2, SAR
    l32i      a2, a1, 4
    l32i      a3, a1, 8
    ret

/*
 * User Exception Handler
 */
    .type     _UserException, @function
    .align    4
_UserException:
    rsr       a0, EXCCAUSE
    bnei      a0, EXCCAUSE_SYSCALL, 1f
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC1
    addi      a0, a0, 3
    s32i      a0, a1, LTK_FRAME(EPC)
    j         2f
1:
    bnei      a0, EXCCAUSE_LEVEL1_INTERRUPT, 3f
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC1
    s32i      a0, a1, LTK_FRAME(EPC)
2:
    rsr       a0, EXCSAVE1
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, PS
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext

    /* Setup for C call and dispatch interrupt */
    movi      a6, 1-1
    movi      a0, PS_INTLEVEL(1) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    /* Return value in a6 is either NULL or contains new stack pointer */

    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, PS
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC1
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfe
3:
    bnei      a0, EXCCAUSE_ALLOCA, 4f
    call0     _xt_alloca_exc
4:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC1
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE1
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, PS
    s32i      a0, a1, LTK_FRAME(PS)
    movi      a0, LTK_ARCH_XTENSA_USER_FAULT
    wsr       a0, EXCSAVE1
    j         _FaultHandler

/*
 * Medium Priority Exception Handlers
 */
#if XCHAL_EXCM_LEVEL >= 2
    .type     _Level2Exception, @function
    .align    4
_Level2Exception:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC2
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE2
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, EPS2
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext
    movi      a6, 2-1
    movi      a0, PS_INTLEVEL(2) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, EPS2
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC2
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfi       2
#endif

#if XCHAL_EXCM_LEVEL >= 3
    .type     _Level3Exception, @function
    .align    4
_Level3Exception:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC3
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE3
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, EPS3
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext
    movi      a6, 3-1
    movi      a0, PS_INTLEVEL(3) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, EPS3
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC3
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfi       3
#endif

#if XCHAL_EXCM_LEVEL >= 4
    .type     _Level4Exception, @function
    .align    4
_Level4Exception:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC4
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE4
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, EPS4
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext
    movi      a6, 4-1
    movi      a0, PS_INTLEVEL(4) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, EPS4
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC4
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfi       4
#endif

#if XCHAL_EXCM_LEVEL >= 5
    .type     _Level5Exception, @function
    .align    4
_Level5Exception:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC5
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE5
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, EPS5
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext
    movi      a6, 5-1
    movi      a0, PS_INTLEVEL(5) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, EPS5
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC5
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfi       5
#endif

#if XCHAL_EXCM_LEVEL >= 6
    .type     _Level6Exception, @function
    .align    4
_Level6Exception:
    addi      a1, a1, -LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsr       a0, EPC6
    s32i      a0, a1, LTK_FRAME(EPC)
    rsr       a0, EXCSAVE6
    s32i      a0, a1, LTK_FRAME(A0)
    rsr       a0, EPS6
    s32i      a0, a1, LTK_FRAME(PS)
    call0     _SaveContext
    movi      a6, 6-1
    movi      a0, PS_INTLEVEL(6) | PS_UM | PS_WOE
    wsr       a0, PS
    rsync
    call4     _LTKDispatcher
    call0     _RestoreContext
    l32i      a0, a1, LTK_FRAME(PS)
    wsr       a0, EPS6
    l32i      a0, a1, LTK_FRAME(EPC)
    wsr       a0, EPC6
    l32i      a0, a1, LTK_FRAME(A0)
    addi      a1, a1, LTK_ARCH_XTENSA_STACK_PTR_ADJ
    rsync
    rfi       6
#endif

# Thread Exit trampoline
    .text
    .align  4
    .global _LTKThreadExitTrampoline
    .type   _LTKThreadExitTrampoline, @function
_LTKThreadExitTrampoline:
    call4 _LTKThreadExit

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Apr-22   tiberius    created
 */
