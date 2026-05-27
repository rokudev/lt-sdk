/******************************************************************************
 * lt/source/lt/ltk/LTKArchArm_V5_Vectors.S                     LTK MicroKernel
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2023, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTKArchArm_V5_Vectors.h"

/*
 * LTK requirements for Arm V5 platform:
 *   1. Set up stacks and place CPU in "System Mode" before starting LT.
 *   2. Set all stacks to 2048 bytes and arrange as follows:
 *        High   FIQ stack                       2048 bytes
 *         Mem   System Stack (startup)          2048 bytes
 *               IRQ stack                       2048 bytes
 *               Software Interrupt Stack (SVC)  2048 bytes
 *        Low    Abort Stack                     2048 bytes
 *         Mem   Undefined Instruction Stack     2048 bytes
 */

/****************************** Vector Table **********************************/

    .section .VectorTable

VectorTable:
    ldr pc, _ResetVector
    ldr pc, _UndefinedInstructionVector
    ldr pc, _SoftwareInterruptVector
    ldr pc, _PrefetchAbortVector
    ldr pc, _DataAbortVector
    ldr pc, _ReservedVector
    ldr pc, _IRQVector
    ldr pc, _FIQVector

_ResetVector:
    .word ResetVector
_UndefinedInstructionVector:
    .word UndefinedInstructionVector
_SoftwareInterruptVector:
    .word SoftwareInterruptVector
_PrefetchAbortVector:
    .word PrefetchAbortVector
_DataAbortVector:
    .word DataAbortVector
_ReservedVector:
    .word ReservedVector
_IRQVector:
    .word IRQVector
_FIQVector:
    .word FIQVector

/************************* Zero-init Data Section *****************************/

    .bss

/****************************** Text Section **********************************/

    .section ".text.vectors"
    .global  _LTKDispatcher
    .global  _LTKFaultHandler

/*
 * Use full descending stacks (FD syntax which is also compatible with push/pop):
 *    Using stmfd/ldmfd (aka stmdb/ldmia)
 */

SoftwareInterruptVector:
    /* Disable interrupts, temporarily enter system mode, push system context, return to SWI mode */
    msr cpsr_c, #0xdf
    stmfd sp!, { r0-r12, lr }
    mov r0, sp
    msr cpsr_c, #0xd3
    mrs r1, spsr
    stmfd r0!, { r1, lr }
    /* Enable FIQ TODO: should be able to do this above? */
    msr cpsr_c, #0x93

    /* Dispatch SWI */
    mov r1, #LTK_DISPATCH_MODE_SWI
    bl _LTKDispatcher

    /* Disable interrupts, temporarily enter system mode, pop system context, return to SWI mode */
    ldmfd r0!, { r1, lr }
    msr spsr, r1
    msr cpsr_c, #0xdf
    mov sp, r0
    ldmfd sp!, { r0-r12, lr }
    msr cpsr_c, #0xd3

    /* Return from software interrupt, restoring original PC and CPSR */
    movs pc, lr

IRQVector:
    /* Disable interrupts, temporarily enter system mode, push system context, return to IRQ mode */
    msr cpsr_c, #0xdf
    stmfd sp!, { r0-r12, lr }
    mov r0, sp
    msr cpsr_c, #0xd2
    mrs r1, spsr
    sub lr, lr, #4
    stmfd r0!, { r1, lr }
    msr cpsr_c, #0x92

    mov r1, #LTK_DISPATCH_MODE_IRQ
    bl _LTKDispatcher

    /* Disable interrupts, temporarily enter system mode, pop system context, return to IRQ mode */
    ldmfd r0!, { r1, lr }
    msr spsr, r1
    msr cpsr_c, #0xdf
    mov sp, r0
    ldmfd sp!, { r0-r12, lr }
    msr cpsr_c, #0xd2

    /* Return from IRQ, restoring original CPSR and PC */
    movs pc, lr

FIQVector:
    /* TODO: detect interrupted mode and save context on stack */
    /* TODO: if interrupted mode is not system mode, then FIQ_NOSCHEDULE */
    udf 0

FaultHandler:
    /* Save original CPSR and PC to correct offsets in stack frame */
    mrs r1, spsr
    sub lr, lr, #4
    stmfd sp!, { r1, lr }
    add r0, sp, #64
    /* Temporarily enter interrupted mode, saving r8-r12 and lr registers */
    and r1, r1, #0x1f
    orr  r1, r1, #0xc0
    msr cpsr_c, r1
    stmfd r0, { r8-r12, lr }
    mov r1, sp
    /* Return to the exception mode, adjust R0 (pointer to the interrupted context,
     * and call C fault handler */
    msr cpsr_c, r2
    sub r0, r0, #64
    b _LTKFaultHandler

/*
 * Fault Vectors:
 *   Disable IRQs and FIQs and retain mode to save context on mode stack.
 *   Save r0-r7 to correct offsets in stack frame and allow registers to be used.
 *   Save mode in r2 and call common fault handler.
 */
UndefinedInstructionVector:
    msr cpsr_c, #0xdb
    sub sp, sp, #24
    stmfd sp!, { r0-r7 }
    mov r2, #LTK_FAULT_ILLEGAL_INSTRUCTION
    b FaultHandler

PrefetchAbortVector:
    msr cpsr_c, #0xd7
    sub sp, sp, #24
    stmfd sp!, { r0-r7 }
    mov r2, #LTK_FAULT_PREFETCH_ABORT
    b FaultHandler

DataAbortVector:
    msr cpsr_c, #0xd7
    sub sp, sp, #24
    stmfd sp!, { r0-r7 }
    mov r2, #LTK_FAULT_PREFETCH_ABORT
    /* Add 0x100 to differentiate between data abort and prefetch abort */
    add r2, r2, #0x100
    b FaultHandler

ReservedVector:
    udf 0

