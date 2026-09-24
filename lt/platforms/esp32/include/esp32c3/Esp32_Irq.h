/******************************************************************************
 * Esp32_Irq.h                                                     ESP32-C3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_IRQ_H
#define PLATFORMS_ESP32_INCLUDE_ESP32C3_IRQ_H

#include "Esp32_Registers.h"
#include "Esp32_SoC.h"

/*
 *  CPU Interrupt Assignments for ESP32-C3
 *
 *  Like the Xtensa parts, the esp32c3 multiplexes peripheral sources onto a
 *  small set of CPU interrupt lines.  What differs is that nothing about a line
 *  is fixed in the core: an Xtensa line has its level and its type burned in, so
 *  the choice of line decides both, whereas here CPU_INT_TYPE, CPU_INT_PRI_n and
 *  CPU_INT_THRESH make all three a software decision and every line is equal.
 *  There is therefore no level 1 restriction to work around and no assignable
 *  line mask - lines 1..31 are all equally usable, and line 0 is the multiplexer's
 *  "detached" encoding rather than a line at all.
 *
 *  Line   Type    Assignment
 *    0      -     detached (writing 0 to a map register unroutes the source)
 *    1    level   LTK system tick, SYSTIMER TARGET0
 *    4    level   GPIO
 *    5    level   UART0
 *    6    level   USB Serial/JTAG
 *  others         free
 *
 *  Everything is level triggered, the tick included.  periph_defs.h calls the
 *  SYSTIMER TARGET0 source EDGE, but that name is inherited from the Xtensa
 *  parts: the block here latches into SYSTIMER_INT_ST and holds the line until
 *  SYSTIMER_INT_CLR is written, which is a level source by any other name.
 *  Taking it as level means the acknowledgement happens at the peripheral, where
 *  it has to happen anyway, instead of in two places.
 *
 *  GPIO and UART0 keep the numbers the ROM used for them (ETS_GPIO_INUM,
 *  ETS_UART0_INUM in soc/soc.h) purely so a trace is easier to read against ROM
 *  code; nothing requires it, because LT owns mtvec from LTKInitialize onward and
 *  no ROM handler runs after that.  For the same reason the three lines soc.h
 *  marks reserved for IDF's riscv/vector.S - 24, 25 and 26 - are free here.
 *
 *  Priorities are all 1 against a threshold of 0: a line fires when its priority
 *  is strictly above the threshold, so 1 over 0 accepts everything, and LTK's
 *  dispatcher is flat anyway - it takes whatever the core hands it.
 */

/*
 * Peripheral interrupt sources.
 *
 * These are IDF's ETS_*_INTR_SOURCE values from soc/esp32c3/periph_defs.h, and
 * they double as the word index into the INTERRUPT_CORE0 map register array.
 * Only the sources this platform routes are listed.  The numbering is dense on
 * this part, unlike the esp32s3, but it is still its own - nothing carries over.
 */
typedef u8 Esp32_ExternalIrq;
enum Esp32_ExternalIrq {
    kEsp32_ExternalIrq_GPIO             = 16,
    kEsp32_ExternalIrq_UART0            = 21,
    kEsp32_ExternalIrq_UART1            = 22,
    kEsp32_ExternalIrq_USBSerialJTAG    = 26,
    kEsp32_ExternalIrq_RTCCore          = 27,
    kEsp32_ExternalIrq_TG0_T0           = 32,
    kEsp32_ExternalIrq_SystemTimer0     = 37,
};

/* CPU interrupt lines, as tabulated above */
typedef u8 Esp32_IrqNumber;
enum Esp32_IrqNumbers {
    kEsp32_IrqNumber_Detached           = 0,
    kEsp32_IrqNumber_SystemTick         = 1,
    kEsp32_IrqNumber_GPIO               = 4,
    kEsp32_IrqNumber_UART0              = 5,
    kEsp32_IrqNumber_USBSerialJTAG      = 6,
    kEsp32_IrqNumber_Count              = 32,
};

typedef u8 Esp32_IrqPriority;
enum Esp32_IrqPriorities {
    kEsp32_IrqPriority_SystemTick       = 1,
    kEsp32_IrqPriority_GPIO             = 1,
    kEsp32_IrqPriority_UART0            = 1,
    kEsp32_IrqPriority_USBSerialJTAG    = 1,
};

/*
 * Line trigger type.
 *
 * Only two values, unlike the seven the Xtensa parts distinguish, because the
 * rest of that list describes things the core does for itself - NMI, software
 * and timer lines - and this part has none of them.  A software interrupt here
 * is a peripheral source (ETS_FROM_CPU_INTR0..3) routed like any other.
 */
typedef u8 Esp32_IrqType;
enum Esp32_IrqType {
    kEsp32_IrqType_Level            = 0,  /* pending while the source asserts; the source clears it */
    kEsp32_IrqType_Edge             = 1,  /* latched on the rising edge; CPU_INT_CLEAR clears it */
};

/*
 * Route a peripheral interrupt source to a CPU interrupt line.
 *
 * Writing kEsp32_IrqNumber_Detached (0) unroutes the source, which is the reset
 * state of every map register.  The nCpu argument exists only so that code
 * shared with the dual core parts still compiles - this part has one core.
 */
LT_INLINE void
Esp32MapExternalToCPUIrq(Esp32_CPU nCpu, Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq) {
    LT_UNUSED(nCpu);
    ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_IRQ_MAP, nExternalIrq) = nCpuIrq;
}

/* Choose level or edge triggering for a CPU interrupt line */
LT_INLINE void
Esp32SetCPUIrqType(Esp32_IrqNumber nCpuIrq, Esp32_IrqType nType) {
    if (nType == kEsp32_IrqType_Edge) ESP32_REG(INTERRUPT_CORE0_CPU_INT_TYPE) |=  (1u << nCpuIrq);
    else                              ESP32_REG(INTERRUPT_CORE0_CPU_INT_TYPE) &= ~(1u << nCpuIrq);
}

/* Give a CPU interrupt line a priority, 1..15.  Zero would mask it outright */
LT_INLINE void
Esp32SetCPUIrqPriority(Esp32_IrqNumber nCpuIrq, Esp32_IrqPriority nPriority) {
    ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_CPU_INT_PRI_0, nCpuIrq) = nPriority;
}

/*
 * CPU_INT_ENABLE is the only per-line gate on this part.  The core does not
 * implement the standard mie CSR - writing it traps as an illegal instruction -
 * so the interrupt matrix is the whole story, exactly as IDF's
 * esprv_intc_int_enable() has it.  Global masking is mstatus.MIE, below.
 */
LT_INLINE void
Esp32EnableCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_ENABLE) |= (1u << nCpuIrq);
}

LT_INLINE void
Esp32DisableCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_ENABLE) &= ~(1u << nCpuIrq);
}

/*
 * Clear a pending edge triggered line.
 *
 * CPU_INT_CLEAR is a plain read/write register, not write-1-to-clear: a bit left
 * set holds that line's latch clear and it will never go pending again.  Hence
 * the pulse.  Level lines are unaffected by any of this - they go away when the
 * peripheral stops asserting, so an ISR that forgets to acknowledge at the
 * peripheral is re-entered forever.
 */
LT_INLINE void
Esp32ClearCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_CLEAR) |=  (1u << nCpuIrq);
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_CLEAR) &= ~(1u << nCpuIrq);
}

/*
 * Detach every source and mask every line, from the BSP before it routes
 * anything of its own.  The ROM leaves some of its own routing behind.
 */
LT_INLINE void
Esp32InitializeIRQs(void) {
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_ENABLE) = 0;
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_THRESH) = 0;
    for (u32 nSource = 0; nSource < 64; nSource++) {
        ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_IRQ_MAP, nSource) = kEsp32_IrqNumber_Detached;
    }
    for (u32 nLine = 0; nLine < kEsp32_IrqNumber_Count; nLine++) {
        ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_CPU_INT_PRI_0, nLine) = 0;
    }
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_CLEAR) = 0xffffffffu;
    ESP32_REG(INTERRUPT_CORE0_CPU_INT_CLEAR) = 0;
}

/* Disable interrupts, returning the previous mstatus for Esp32EnableInterrupts */
LT_INLINE u32
Esp32DisableInterrupts(void) {
    u32 nMask;
    asm volatile (
       "csrrci %0, mstatus, 1 << 3  \n\
        fence"
            : "=r"(nMask) : : "memory"
    );
    return nMask;
}

/* Restore the mstatus a matching Esp32DisableInterrupts() returned */
LT_INLINE void
Esp32EnableInterrupts(u32 nMask) {
    asm volatile (
       "fence                       \n\
        csrw mstatus, %0"
            : : "r"(nMask) : "memory"
    );
}

/*
 * CPU interrupt line vectors.
 *
 * LTKSetInterruptVector() and LTKSetInterruptPriority() are no-ops in the RISC-V
 * kernel port - it hands every trap straight to the BSP's dispatcher and leaves
 * the decoding to it - so the vector table lives in the BSP rather than in LTK,
 * and a peripheral driver attaches to a line through the two calls below rather
 * than through the LTCore callbacks the Xtensa parts use.
 *
 * A handler returns the number of system ticks that have elapsed.  Only the tick
 * line has anything to say there; everything else returns 0, and the dispatcher
 * ignores the value from any other line in any case.
 */
typedef u32 (Esp32_InterruptVector)(void) LT_ISR_SAFE;

/*
 * Route nExternalIrq to nCpuIrq, install pVector on it, and unmask it.  The line
 * is left pending-free and enabled on return.  Interrupts are disabled across the
 * update, so this is safe to call once the system is running.
 */
void Esp32_AttachInterrupt(Esp32_ExternalIrq   nExternalIrq,
                           Esp32_IrqNumber     nCpuIrq,
                           Esp32_IrqType       nType,
                           Esp32_IrqPriority   nPriority,
                           Esp32_InterruptVector * pVector);

/* Mask nCpuIrq, detach nExternalIrq from it, and forget the vector. */
void Esp32_DetachInterrupt(Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_IRQ_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
