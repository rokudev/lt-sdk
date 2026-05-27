/******************************************************************************
 * Esp32_Irq.h                                                        ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_IRQ_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_IRQ_H

#include "Esp32_Registers.h"
#include "xtensa/config/core-isa.h"

/*
 * Mapping of peripherals to external interrupt lines for ESP32
 */
typedef u8 Esp32_ExternalIrq;
enum Esp32_ExternalIrq {
    kEsp32_ExternalIrq_WIFIMAC    = 0,
    kEsp32_ExternalIrq_GPIO       = 22,
    kEsp32_ExternalIrq_UART0      = 34,
    kEsp32_ExternalIrq_EMAC       = 38,
    kEsp32_ExternalIrq_LEDC       = 43,
};

/*
 *  Xtensa CPU Interrupt Assignments for ESP32
 *
 *  Xtensa interrupt priorities and types are normally fixed by the hardware
 *  implementation. However, the ESP32 implements a multiplexer that allows one
 *  to assign external interrupts to any Xtensa Irq. In addition, multiple
 *  interrupt sources may be assigned to the same Irq if necessary.
 *
 *  For ESP32 there are 32 CPU interrupt levels with priorities ranging from 1-5
 *  plus NMI. It is recommended to utilize priorities 1 to 3 when possible, since
 *  interrupt priorities 4, 5 and NMI must be handled outside of the kernel and
 *  require some coding in assembly. Any signalling that is required between these
 *  high priority interrupts and threads can be accomplished by raising lower
 *  priority software interrupts (with priorities 1 or 3) from those high priority
 *  handlers.
 *
 *  Nested interrupts are supported, which means that handlers for interrupts of
 *  higher priorities will preempt the execution of those with lower priority
 *  handlers. If multiple interrupts are pending on the same priority they will
 *  not preempt each other, but the handler with the lower numbered interrupt
 *  will be executed first.
 *
 *  Level interrupts are typically used when hardware external to the processor
 *  implements an interrupt latching mechanism, whereas edge interrupts are
 *  typically used when CPU hardware is to be used for the latching mechanism.
 *  Level interrupts generally require that external hardware be reset to clear
 *  the interrupt (typically via a register write), whereas edge interrupts are
 *  typically cleared only by writing a CPU register (e.g: INTCLEAR).
 *
 *  Irq  Prio  Type      Assignment
 *   0    1    level     WiFiMAC
 *   1    1    level
 *   2    1    level
 *   3    1    level     UART0
 *   4    1    level     GPIO
 *   5    1    level     BT_BB_NMI
 *   6    1    timer0    Reserved for Kernel Tick Only
 *   7    1   software   RWBLE_IRQ
 *   8    1    level     RWBT_NMI
 *   9    1    level     LEDC
 *   10   1     edge
 *   11   3  "profiling" TBD: What is this really?
 *   12   1    level     Ethernet MAC
 *   13   1    level
 *   14  NMI*   NMI
 *   15   3    timer1
 *   16   5*   timer2
 *   17   1    level
 *   18   1    level
 *   19   2    level
 *   20   2    level
 *   21   2    level
 *   22   3     edge
 *   23   3    level
 *   24   4*   level
 *   25   4*   level
 *   26   5*   level
 *   27   3    level
 *   28   4*    edge
 *   29   3   software
 *   30   4*    edge
 *   31   5*   level
 *   *Avoid these highest priorities if possible.
 */

typedef u8 Esp32_IrqNumber;
enum Esp32_IrqNumber {
    kEsp32_IrqNumber_WiFiMAC        = 0,
    kEsp32_IrqNumber_UART0          = 3,
    kEsp32_IrqNumber_GPIO           = 4,
    kEsp32_IrqNumber_BT_BB_NMI      = 5,
    kEsp32_IrqNumber_SystemTick     = 6,
    kEsp32_IrqNumber_RWBLE_IRQ      = 7,
    kEsp32_IrqNumber_RWBT_NMI       = 8,
    kEsp32_IrqNumber_LEDC           = 9,
    kEsp32_IrqNumber_EMAC           = 12,
};

typedef u8 Esp32_IrqPriority;
enum Esp32_IrqPriority {
    kEsp32_IrqPriority_WiFiMAC      = 1,
    kEsp32_IrqPriority_UART0        = 1,
    kEsp32_IrqPriority_SystemTick   = 1,
    kEsp32_IrqPriority_GPIO         = 1,
    kEsp32_IrqPriority_LEDC         = 1,
    kEsp32_IrqPriority_BLE          = 1,
    kEsp32_IrqPriority_EMAC         = 1,
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

    /* Irq Type for Peripherals */
    kEsp32_IrqType_SystemTick       = kEsp32_IrqType_Timer,
    kEsp32_IrqType_GPIO             = kEsp32_IrqType_Level,
    kEsp32_IrqType_WiFiMAC          = kEsp32_IrqType_Level,
    kEsp32_IrqType_EMAC             = kEsp32_IrqType_Level,
};

/* Map External Irq to CPU Irq */
LT_INLINE void
Esp32MapExternalToCPUIrq(u8 nCpu, Esp32_ExternalIrq nExternalIrq, Esp32_IrqNumber nCpuIrq) {
    if (nCpu == 0) ESP32_REG_ARRAY_VALUE(DPORT_CPU0_IRQ_MAP, nExternalIrq) = nCpuIrq;
    else ESP32_REG_ARRAY_VALUE(DPORT_CPU1_IRQ_MAP, nExternalIrq) = nCpuIrq;
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

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_IRQ_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-May-22   tiberius    created
 */
