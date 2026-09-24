/******************************************************************************
 * Esp32_Irq.h                                                     ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Interrupt routing for the esp32p4.
 *
 * Two pieces of hardware, not one.  The interrupt matrix routes a peripheral
 * source to a CPU line, exactly as on the esp32c3 - same dense array indexed by
 * source number, same six bit field.  Everything else about the esp32c3's
 * INTERRUPT_CORE0 block, though - the enable mask, the priority file, the
 * trigger type register, the threshold - is gone, replaced by a CLIC.
 *
 * The CLIC gives every line a 32 bit control word at CLIC_INT_CTRL_REG(i)
 * holding its priority, trigger type, enable and pending bits, and a single
 * global threshold at CLIC_INT_THRESH_REG.  Its line numbering is not the
 * matrix's: CLIC lines 0..15 are the core's own standard interrupts and the 32
 * external lines start at 16, so what the matrix calls line n the CLIC calls
 * n + 16.  That bias is applied in exactly two places below, and nothing
 * outside this file should need to know about it.  Writing 0 to a matrix entry
 * names CLIC line 0, which is not an external line, and so detaches the source.
 *
 * Priorities are CLIC levels.  The hardware implements three level bits packed
 * into the top of the control byte, and the threshold is inclusive - a line
 * fires only if its level is strictly above the threshold - so the threshold
 * sits at level 0 and every line that wants to fire runs at 1 or above.
 *
 * Hardware vectoring is deliberately not used.  A line with SHV set vectors
 * through a table pointed at by the MTVT CSR, which nothing in LTK writes, so
 * SHV is left clear on every line and all of them trap to the common handler at
 * mtvec.  mtvec still has to be in CLIC mode for the CLIC to deliver anything,
 * which is what kLTCoreBSP_RISCV_VectorMode_CLICVectored asks LTK for - the name
 * describes the mtvec mode, not whether individual lines vector.
 *
 * As on the esp32c3, LTKSetInterruptVector() and LTKSetInterruptPriority() are
 * no-ops in the RISC-V kernel port, so the vector table lives in the BSP.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_IRQ_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_IRQ_H

#include "Esp32_Registers.h"
#include "Esp32_SoC.h"

/*
 * Peripheral interrupt sources, ETS_*_INTR_SOURCE.  Only the ones this BSP and
 * its drivers route; the part defines 128.
 *
 * The GPIO entry is worth a note: the esp32p4 has four GPIO interrupt outputs,
 * GPIO_INTR0..3, at sources 74 to 77.  IDF wires only the first and so does
 * this BSP, matching the single INT_ENA bit the GPIO driver sets.
 */
typedef u32 Esp32_ExternalIrq;
enum Esp32_ExternalIrq {
    kEsp32_ExternalIrq_LPWatchdog        = 1,
    kEsp32_ExternalIrq_USBSerialJTAG     = 22,
    kEsp32_ExternalIrq_UART0             = 31,
    kEsp32_ExternalIrq_UART1             = 32,
    kEsp32_ExternalIrq_TG0_T0            = 46,
    kEsp32_ExternalIrq_TG0_WDT           = 48,
    kEsp32_ExternalIrq_SystemTimer0      = 53,
    kEsp32_ExternalIrq_SystemTimer1      = 54,
    kEsp32_ExternalIrq_SystemTimer2      = 55,
    kEsp32_ExternalIrq_GPIO              = 74,
};

/*
 * CPU interrupt lines.  There are 32 of them and the assignment is this BSP's
 * to make; line 0 is reserved as the detached value and is never allocated.
 *
 *   0   detached
 *   1   kernel tick, SYSTIMER comparator 0
 *   4   GPIO
 *   5   console UART0
 */
typedef u32 Esp32_IrqNumber;
enum Esp32_IrqNumbers {
    kEsp32_IrqNumber_Detached            = 0,
    kEsp32_IrqNumber_SystemTick          = 1,
    kEsp32_IrqNumber_GPIO                = 4,
    kEsp32_IrqNumber_UART0               = 5,
    kEsp32_IrqNumber_Count               = 32,
};

/*
 * CLIC levels.  Level 0 is the threshold and never fires, so the lowest usable
 * priority is 1, and three level bits put the ceiling at 7.  Nothing here needs
 * to pre-empt anything else, so everything runs at 1.
 */
typedef u32 Esp32_IrqPriority;
enum Esp32_IrqPriorities {
    kEsp32_IrqPriority_Lowest            = 1,
    kEsp32_IrqPriority_SystemTick        = 1,
    kEsp32_IrqPriority_GPIO              = 1,
    kEsp32_IrqPriority_UART0             = 1,
    kEsp32_IrqPriority_Highest           = 7,
};

typedef u32 Esp32_IrqType;
enum Esp32_IrqType {
    kEsp32_IrqType_Level                 = 0,
    kEsp32_IrqType_Edge                  = 1,
};

/*
 * CLIC layout.  The vendor's clic_reg.h is not usable as written - its
 * CLIC_INT_CONFIG_NLBITS_M expands to a misspelled symbol - so the fields it
 * documents are spelled out here instead.
 */
enum Esp32_CLIC {
    /* CLIC line number of matrix line 0 */
    kEsp32_CLIC_ExternalBase             = 16,

    kEsp32_CLIC_THRESH                   = ESP32_REG_BASE(CLIC) + 0x08,
    kEsp32_CLIC_CTRL0                    = ESP32_REG_BASE(CLIC_CTRL),

    /* Fields of a CLIC_INT_CTRL word */
    kEsp32_CLIC_CTRL_LEVEL_S             = 24,
    kEsp32_CLIC_CTRL_LEVEL_M             = 0xffu << 24,
    /* Machine mode.  Resets set, and kept set - zeroing it would hand the line
     * to a privilege level this BSP does not run in. */
    kEsp32_CLIC_CTRL_MODE_MACHINE_V      = 0x03u << 22,
    kEsp32_CLIC_CTRL_TRIG_S              = 17,
    kEsp32_CLIC_CTRL_TRIG_M              = 0x03u << 17,
    /* Selective hardware vectoring.  Left clear - see the note above. */
    kEsp32_CLIC_CTRL_SHV_M               = 0x01u << 16,
    kEsp32_CLIC_CTRL_IE_M                = 0x01u << 8,
    kEsp32_CLIC_CTRL_IP_M                = 0x01u << 0,

    /* Three level bits, left justified in the control byte with the unused low
     * bits set - so level L is (L << 5) | 0x1f. */
    kEsp32_CLIC_LEVEL_SHIFT              = 5,
    kEsp32_CLIC_LEVEL_FILL               = 0x1f,
};

#define ESP32_CLIC_THRESH                  (*(volatile u32 *)kEsp32_CLIC_THRESH)
#define ESP32_CLIC_CTRL(n)                 (*((volatile u32 *)kEsp32_CLIC_CTRL0 + (n)))

/* Pack a CLIC level into a control byte */
#define ESP32_CLIC_LEVEL_BYTE(l)           ((((l) << kEsp32_CLIC_LEVEL_SHIFT) | kEsp32_CLIC_LEVEL_FILL) & 0xff)

/*
 * Route a peripheral source to a CPU line, or to kEsp32_IrqNumber_Detached to
 * stop routing it.  Both cores share one matrix on this part, so nCpu selects
 * nothing; it is kept for the shape of the call.
 */
LT_INLINE void
Esp32MapExternalToCPUIrq(Esp32_CPU nCpu, Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq) {
    LT_UNUSED(nCpu);
    u32 nCLICLine = (kEsp32_IrqNumber_Detached == nCpuIrq) ? 0 : (nCpuIrq + kEsp32_CLIC_ExternalBase);
    ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_IRQ_MAP, nExternalIrq) = nCLICLine;
}

LT_INLINE void
Esp32SetCPUIrqType(Esp32_IrqNumber nCpuIrq, Esp32_IrqType nType) {
    u32 nLine  = nCpuIrq + kEsp32_CLIC_ExternalBase;
    u32 nValue = ESP32_CLIC_CTRL(nLine) & ~(u32)kEsp32_CLIC_CTRL_TRIG_M;
    ESP32_CLIC_CTRL(nLine) = nValue | ((nType << kEsp32_CLIC_CTRL_TRIG_S) & kEsp32_CLIC_CTRL_TRIG_M);
}

LT_INLINE void
Esp32SetCPUIrqPriority(Esp32_IrqNumber nCpuIrq, Esp32_IrqPriority nPriority) {
    u32 nLine  = nCpuIrq + kEsp32_CLIC_ExternalBase;
    u32 nValue = ESP32_CLIC_CTRL(nLine) & ~(u32)kEsp32_CLIC_CTRL_LEVEL_M;
    ESP32_CLIC_CTRL(nLine) = nValue | (ESP32_CLIC_LEVEL_BYTE(nPriority) << kEsp32_CLIC_CTRL_LEVEL_S);
}

LT_INLINE void
Esp32EnableCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_CLIC_CTRL(nCpuIrq + kEsp32_CLIC_ExternalBase) |= kEsp32_CLIC_CTRL_IE_M;
}

LT_INLINE void
Esp32DisableCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_CLIC_CTRL(nCpuIrq + kEsp32_CLIC_ExternalBase) &= ~(u32)kEsp32_CLIC_CTRL_IE_M;
}

/*
 * Clear a pending line.  Only edge triggered lines latch - a level triggered
 * line's pending bit follows the source and ignores the write - so this is
 * harmless either way and callers need not know which they have.
 */
LT_INLINE void
Esp32ClearCPUIrq(Esp32_IrqNumber nCpuIrq) {
    ESP32_CLIC_CTRL(nCpuIrq + kEsp32_CLIC_ExternalBase) |= kEsp32_CLIC_CTRL_IP_M;
}

/*
 * Bring both blocks to a known state: nothing routed, nothing enabled, nothing
 * pending, and the threshold low enough to let any configured line through.
 *
 * The threshold write is read back before returning.  The CLIC does not make a
 * threshold write visible to the delivery logic immediately, and enabling
 * interrupts against a stale threshold either drops the first interrupt or
 * takes one that should have been masked.
 */
LT_INLINE void
Esp32InitializeIRQs(void) {
    for (u32 nSource = 0; nSource < kEsp32_RegisterINTERRUPT_SOURCE_COUNT; nSource++) {
        ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_IRQ_MAP, nSource) = kEsp32_IrqNumber_Detached;
    }

    for (u32 nLine = 0; nLine < kEsp32_IrqNumber_Count; nLine++) {
        ESP32_CLIC_CTRL(nLine + kEsp32_CLIC_ExternalBase) = kEsp32_CLIC_CTRL_MODE_MACHINE_V;
    }

    ESP32_CLIC_THRESH = ESP32_CLIC_LEVEL_BYTE(0) << kEsp32_CLIC_CTRL_LEVEL_S;
    u32 nReadback = ESP32_CLIC_THRESH;
    LT_UNUSED(nReadback);
}

/*
 * Global interrupt enable, mstatus.MIE.  Unchanged from the esp32c3 - the CLIC
 * does not alter how the core masks interrupts as a whole, only how it chooses
 * between them.  Esp32DisableInterrupts() returns the previous mstatus for
 * Esp32EnableInterrupts() to restore, so these nest.
 */
LT_INLINE u32
Esp32DisableInterrupts(void) {
    u32 nMask;
    asm volatile ("csrrci %0, mstatus, 1 << 3  \n\
        fence" : "=r"(nMask) : : "memory");
    return nMask;
}

LT_INLINE void
Esp32EnableInterrupts(u32 nMask) {
    asm volatile ("fence                       \n\
        csrw mstatus, %0" : : "r"(nMask) : "memory");
}

typedef u32 (Esp32_InterruptVector)(void) LT_ISR_SAFE;

void Esp32_AttachInterrupt(Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq,
                           Esp32_IrqType nType, Esp32_IrqPriority nPriority,
                           Esp32_InterruptVector * pVector);

void Esp32_DetachInterrupt(Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq);

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_IRQ_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
