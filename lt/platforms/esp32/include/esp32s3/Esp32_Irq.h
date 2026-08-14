/******************************************************************************
 * Esp32_Irq.h                                                     ESP32-S3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_IRQ_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_IRQ_H

#include "Esp32_Registers.h"
#include "Esp32_SoC.h"
#include "xtensa/config/core-isa.h"

/*
 *  Xtensa CPU Interrupt Assignments for ESP32-S3
 *
 *  Like the esp32, the esp32s3 multiplexes peripheral interrupt sources onto the
 *  32 Xtensa CPU interrupts, so the priority and type of a given peripheral is a
 *  software choice rather than a hardware one.  The multiplexer itself moved out
 *  of DPORT and into the INTERRUPT_CORE0/INTERRUPT_CORE1 register blocks, and
 *  the source numbering changed, so none of the esp32's numbers carry over.
 */

/*
 * Peripheral interrupt sources.
 *
 * These are IDF's ETS_*_INTR_SOURCE values from soc/esp32s3/periph_defs.h, and
 * they double as the word index into either core's map register array.  Only the
 * sources this platform routes are listed; the numbering is not contiguous in
 * IDF either, because several sources in the middle of the range are unused on
 * this part.
 */
typedef u8 Esp32_ExternalIrq;
enum Esp32_ExternalIrq {
    kEsp32_ExternalIrq_GPIO             = 16,
    kEsp32_ExternalIrq_UART0            = 27,
    kEsp32_ExternalIrq_UART1            = 28,
    kEsp32_ExternalIrq_UART2            = 29,
    kEsp32_ExternalIrq_LEDC             = 35,
    kEsp32_ExternalIrq_TG0_T0           = 50,
    kEsp32_ExternalIrq_SystemTimer0     = 57,
    kEsp32_ExternalIrq_USBSerialJTAG    = 96,

    /* Writing this to a map register detaches the source from both cores */
    kEsp32_ExternalIrq_Detached         = 0,
};

/*
 * CPU interrupt lines.
 *
 * Only lines whose XCHAL_INTn_LEVEL is 1 can carry a peripheral here, because
 * LTKArchXtensa's dispatcher runs from the level 1 vector; the higher level
 * vectors belong to the kernel and to lines the core reserves for itself.  Line
 * 6 is CCOMPARE0, which LTK owns as its system tick, and 7 is the software
 * interrupt, so neither appears below.
 *
 * The esp32's UART0 and GPIO assignments are kept so that the two BSPs read the
 * same; USB Serial/JTAG takes line 13, which the esp32 BSP leaves free.
 */
typedef u8 Esp32_IrqNumber;
enum Esp32_IrqNumbers {
    kEsp32_IrqNumber_UART0              = 3,
    kEsp32_IrqNumber_GPIO               = 4,
    kEsp32_IrqNumber_LEDC               = 9,
    kEsp32_IrqNumber_USBSerialJTAG      = 13,
};

typedef u8 Esp32_IrqPriority;
enum Esp32_IrqPriorities {
    kEsp32_IrqPriority_UART0            = 1,
    kEsp32_IrqPriority_GPIO             = 1,
    kEsp32_IrqPriority_LEDC             = 1,
    kEsp32_IrqPriority_USBSerialJTAG    = 1,
};

typedef u8 Esp32_IrqType;
enum Esp32_IrqType {
    /* Fundamental Types */
    kEsp32_IrqType_Level            = 0,  /* External-level from device sets, device clears */
    kEsp32_IrqType_Edge             = 1,  /* External-signal edge sets, INTCLEAR clears */
    kEsp32_IrqType_NMI              = 2,  /* External-signal edge sets, auto clears */
    kEsp32_IrqType_Software         = 3,  /* Internal-INTSET sets, INTCLEAR clears */
    kEsp32_IrqType_Timer            = 4,  /* Internal-CCOMPARE# sets and clears*/
    kEsp32_IrqType_Debug            = 5,  /* Debug   -debug hardware sets and clears */
    kEsp32_IrqType_WriteErr         = 6,  /* Internal-Bus error on write, INTCLEAR to clear */
};

/*
 * Route a peripheral interrupt source to a CPU interrupt line on one core.
 *
 * The esp32 had one map register array per core inside DPORT; here each core has
 * its own INTERRUPT_CORE block, 0x800 apart, and within a block the source
 * number is the word index.  A source routed on both cores would interrupt both,
 * so callers route on one and leave the other detached, which is the reset state.
 */
LT_INLINE void
Esp32MapExternalToCPUIrq(Esp32_CPU nCpu, Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq) {
    if (nCpu == kEsp32_CPU0) ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE0_IRQ_MAP, nExternalIrq) = nCpuIrq;
    else                     ESP32_REG_ARRAY_VALUE(INTERRUPT_CORE1_IRQ_MAP, nExternalIrq) = nCpuIrq;
}

/* Set vector table base address */
LT_INLINE void
Esp32SetVectorTableBaseAddress(void * pBaseAddr) {
    asm volatile (
        "wsr   %0, VECBASE" : : "r"(pBaseAddr) : "memory"
    );
}

/* Set (raise) a software interrupt */
LT_INLINE void
Esp32SetSoftwareInterrupt(Esp32_IrqNumber nCpuIrq) {
    asm volatile (
        "wsr   %0, INTSET      \n\
         isync                 \n\
         rsync"
                : : "r"(nCpuIrq) : "memory"
    );
}

/* Disable interrupts */
LT_INLINE u32
Esp32DisableInterrupts(void) {
    u32 nMask;
    asm volatile (
       "rsil %0, %1"
           : "=r"(nMask) : "i"(XCHAL_EXCM_LEVEL) : "memory"
    );
    return nMask;
}

/* Enable interrupts */
LT_INLINE void
Esp32EnableInterrupts(u32 nMask) {
    asm volatile (
       "wsr %0, PS         \n\
        rsync"
           : : "r"(nMask) : "memory"
    );
}

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_IRQ_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
