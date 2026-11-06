/******************************************************************************
 * Esp32_Clock.h                                                   ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Peripheral clock gating for the esp32p4.
 *
 * The other variants gate everything from one register with one bit per
 * peripheral, so a clock is a bitmask and several can be enabled in a single
 * write.  That does not work here: the gates are scattered across some twenty
 * HP_SYS_CLKRST registers with no regular layout, and the reset releases are in
 * a different register again.
 *
 * So a clock here is a descriptor rather than a mask - a register and a bit for
 * the gate, and a register and a bit for the reset - packed into one word so
 * the call signatures stay what they are on every other variant.  Masks cannot
 * be OR'd together; one call enables one peripheral.
 *
 * Worth knowing before reaching for any of this: everything the BSP itself
 * needs is already clocked out of reset, including UART0, SYSTIMER, both timer
 * groups and IO_MUX, and GPIO has no gate at all.  Only I2C and LEDC start
 * gated off, which is why they are the ones with callers.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_CLOCK_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_CLOCK_H

#include "Esp32_Registers.h"
#include "Esp32_Irq.h"

/*
 * A packed gate descriptor: clock bit, clock register, reset bit, reset
 * register.  The two register fields hold an HP_SYS_CLKRST offset divided by
 * four, which fits a byte because the block is under 0x100 bytes long.
 *
 * A reset register field of zero means the peripheral has no reset bit to
 * release - offset 0 is CLK_EN0, which is never a reset register.
 */
#define ESP32_CLOCK_GATE(clkOff, clkBit, rstOff, rstBit)                  \
    ((u32)(clkBit) | ((u32)((clkOff) >> 2) << 8)                          \
                   | ((u32)(rstBit) << 16) | ((u32)((rstOff) >> 2) << 24))

#define ESP32_CLOCK_GATE_CLK_BIT(g)        ((g) & 0xff)
#define ESP32_CLOCK_GATE_CLK_OFF(g)        ((((g) >> 8) & 0xff) << 2)
#define ESP32_CLOCK_GATE_RST_BIT(g)        (((g) >> 16) & 0xff)
#define ESP32_CLOCK_GATE_RST_OFF(g)        ((((g) >> 24) & 0xff) << 2)

#define ESP32_CLKRST_REG(off)              (*(volatile u32 *)(ESP32_REG_BASE(HP_CLKRST) + (off)))

/*
 * Peripheral clocks.
 *
 * Timer group 0 has two gates that matter and no way to name one of them
 * "TIMERGROUP", so the counter and the watchdog are separate entries; both
 * release the same reset.
 */
typedef u32 Esp32_ClockPeripheralClock;
enum Esp32_ClockPeripheralClocks {
    kEsp32_Clock_UART0                  = ESP32_CLOCK_GATE(0x68, 26, 0xc4,  8),
    kEsp32_Clock_I2C0                   = ESP32_CLOCK_GATE(0x40,  1, 0xc4, 22),
    kEsp32_Clock_I2C1                   = ESP32_CLOCK_GATE(0x40, 27, 0xc4, 21),
    kEsp32_Clock_TIMERGROUP0_T0         = ESP32_CLOCK_GATE(0x94, 24, 0xc4,  6),
    kEsp32_Clock_TIMERGROUP0_WDT        = ESP32_CLOCK_GATE(0x94, 30, 0xc4,  6),
    kEsp32_Clock_TIMERGROUP1_WDT        = ESP32_CLOCK_GATE(0x98, 28, 0xc4,  7),
    kEsp32_Clock_SYSTIMER               = ESP32_CLOCK_GATE(0x98, 30, 0xc4,  5),
    kEsp32_Clock_LEDC                   = ESP32_CLOCK_GATE(0x9c,  2, 0xc4, 29),
    kEsp32_Clock_IOMUX                  = ESP32_CLOCK_GATE(0xac,  9, 0xc4,  3),
    /* The DSI gate is in SOC_CLK_CTRL1 rather than a PERI_CLK_CTRL register,
     * and the reset it releases is the bridge's - the host has none of its own */
    kEsp32_Clock_MIPI_DSI               = ESP32_CLOCK_GATE(0x18, 12, 0xc0, 26),
    /* The GDMA takes two gates, and a descriptor holds one: this is the system
     * clock and the reset.  The CPU clock, SOC_CLK_CTRL0 bit 13, has to be set
     * separately by whoever enables this. */
    kEsp32_Clock_GDMA                   = ESP32_CLOCK_GATE(0x18,  5, 0xc0, 21),
};

/*
 * Bring the CPU and the peripheral clock tree to the state the rest of the BSP
 * expects, and return the resulting CPU frequency in MHz.
 */
u32 Esp32_ClockInitialize(void);

/*
 * The current CPU frequency in MHz.  Read from hardware rather than inferred
 * from a divider setting - this part reports its own source frequency.
 */
u32 Esp32_ClockGetMHz(void);

/*
 * Ungate a peripheral and release it from reset.
 *
 * Read-modify-write on two shared registers, so interrupts are held off across
 * the pair - an ISR that gated a different peripheral in between would lose one
 * of the two changes.
 */
LT_INLINE void
Esp32_ClockEnablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 nMask = Esp32DisableInterrupts();

    ESP32_CLKRST_REG(ESP32_CLOCK_GATE_CLK_OFF(clock)) |= (1u << ESP32_CLOCK_GATE_CLK_BIT(clock));

    if (ESP32_CLOCK_GATE_RST_OFF(clock) != 0) {
        ESP32_CLKRST_REG(ESP32_CLOCK_GATE_RST_OFF(clock)) &= ~(1u << ESP32_CLOCK_GATE_RST_BIT(clock));
    }

    Esp32EnableInterrupts(nMask);
}

/*
 * Gate a peripheral off and hold it in reset.
 *
 * Note that two peripherals sharing a reset - the timer group entries - will
 * take each other down, so disabling one of a pair is not something to do
 * casually.
 */
LT_INLINE void
Esp32_ClockDisablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 nMask = Esp32DisableInterrupts();

    if (ESP32_CLOCK_GATE_RST_OFF(clock) != 0) {
        ESP32_CLKRST_REG(ESP32_CLOCK_GATE_RST_OFF(clock)) |= (1u << ESP32_CLOCK_GATE_RST_BIT(clock));
    }

    ESP32_CLKRST_REG(ESP32_CLOCK_GATE_CLK_OFF(clock)) &= ~(1u << ESP32_CLOCK_GATE_CLK_BIT(clock));

    Esp32EnableInterrupts(nMask);
}

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_CLOCK_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 *  30-Sep-26   dwoodward   added the MIPI-DSI gate
 *  30-Sep-26   dwoodward   added the GDMA gate
 */
