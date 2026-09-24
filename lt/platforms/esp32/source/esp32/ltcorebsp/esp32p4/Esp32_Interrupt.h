/******************************************************************************
 * Esp32_Interrupt.h                                               ESP32-P4 BSP
 *
 * The parts of Esp32_Interrupt.c only Esp32_LTCoreBSP.c needs.
 *
 * Esp32_AttachInterrupt() and Esp32_DetachInterrupt() are the halves peripheral
 * drivers use, and they are declared in the public include/esp32p4/Esp32_Irq.h
 * instead.  What is left here is bring-up and the two function pointers
 * LTCoreBSP_RISCV_SystemConfig carries, none of which anything outside the BSP
 * has any business calling.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_SOURCE_ESP32P4_INTERRUPT_H
#define PLATFORMS_ESP32_SOURCE_ESP32P4_INTERRUPT_H

#include <lt/LTTypes.h>

/*
 * Arm the tick comparator.  Last in LTCoreBSP_Initialize(), so that nothing else
 * is still half configured when the first tick lands.  The CPU interrupt enable is
 * still off at that point - LTCore turns it on - but the peripheral is free
 * running from here.  Requires Esp32_InitializeCycleCount() to have started the
 * counter the comparator watches.
 */
void Esp32_InitializeTick(void);

/* Start the systimer counter, which is both LTK's clock and the tick's source.
 * Before anything reads it. */
void Esp32_InitializeCycleCount(void);

/* LTCoreBSP_RISCV_SystemConfig.pDispatcher */
u32 LT_ISR_SAFE Esp32_InterruptDispatcher(u32 mcause);

/* LTCoreBSP_RISCV_SystemConfig.pGetCycleCount - systimer counts, not CPU cycles.
 * Caller must have interrupts disabled; see the implementation. */
u64 LT_ISR_SAFE Esp32_GetCycleCount(void);

#endif // #ifndef PLATFORMS_ESP32_SOURCE_ESP32P4_INTERRUPT_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created
 */
