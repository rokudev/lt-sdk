/******************************************************************************
 * Esp32_Clock.h                                                      ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_CLOCK_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_CLOCK_H

#include "Esp32_Irq.h"

typedef u32 Esp32_ClockPeripheralClock;
enum Esp32_ClockPeripheralClocks {
    kEsp32_Clock_UART_MEM                   = (1 << 24),
    kEsp32_Clock_UART2                      = (1 << 23),
    kEsp32_Clock_SPI_DMA                    = (1 << 22),
    kEsp32_Clock_I2S1                       = (1 << 21),
    kEsp32_Clock_PWM1                       = (1 << 20),
    kEsp32_Clock_TWAI                       = (1 << 19),
    kEsp32_Clock_I2C_EXT1                   = (1 << 18),
    kEsp32_Clock_PWM0                       = (1 << 17),
    kEsp32_Clock_SPI3                       = (1 << 16),
    kEsp32_Clock_TIMERGROUP1                = (1 << 15),
    kEsp32_Clock_EFUSE                      = (1 << 14),
    kEsp32_Clock_TIMERGROUP                 = (1 << 13),
    kEsp32_Clock_UHCI1                      = (1 << 12),
    kEsp32_Clock_LEDC                       = (1 << 11),
    kEsp32_Clock_PCNT                       = (1 << 10),
    kEsp32_Clock_RMT                        = (1 << 9),
    kEsp32_Clock_UHCI0                      = (1 << 8),
    kEsp32_Clock_I2C_EXT0                   = (1 << 7),
    kEsp32_Clock_SPI2                       = (1 << 6),
    kEsp32_Clock_UART1                      = (1 << 5),
    kEsp32_Clock_I2S0                       = (1 << 4),
    kEsp32_Clock_UART                       = (1 << 2),
    kEsp32_Clock_SPI01                      = (1 << 1),
};

typedef u32 Esp32_ClockCryptoClock;
enum Esp32_ClockCryptoClock {
    /* NB: Digital signature reset will hold AES & RSA in reset */
    kEsp32_ClockCrypto_DIGITAL_SIGNATURE    = (1 << 4),
    /* NB: Secure boot reset will hold SHA & AES in reset */
    kEsp32_ClockCrypto_SECUREBOOT           = (1 << 3),
    kEsp32_ClockCrypto_RSA                  = (1 << 2),
    kEsp32_ClockCrypto_SHA                  = (1 << 1),
    kEsp32_ClockCrypto_AES                  = (1 << 0),
};

/* Initialize clocks, returns CPU clock in MHz */
u32 Esp32_ClockInitialize(void);

/* returns CPU clock in MHz */
u32 Esp32_ClockGetMHz(void);

/* Enable peripheral clock */
LT_INLINE void
Esp32_ClockEnablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(DPORT_PERIP_CLK_EN) |= clock;
    ESP32_REG(DPORT_PERIP_RST_EN) &= ~clock;
    Esp32EnableInterrupts(mask);
}

/* Disable peripheral clock */
LT_INLINE void
Esp32_ClockDisablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(DPORT_PERIP_CLK_EN) &= ~clock;
    ESP32_REG(DPORT_PERIP_RST_EN) |= clock;
    Esp32EnableInterrupts(mask);
}

/*
 * Enable crypto clock.
 * The reset status of a crypto engine is controlled by its clock bit and some other bits at the same time.
 * Refer to Esp32DriverCryptoEngine.c for details of each crypto engine.
 * The bits in the reset parameter are cleared in the RST_EN register without being enabled in the CLK_EN register
 */
LT_INLINE void
Esp32_ClockEnableCryptoClock(Esp32_ClockCryptoClock clock, Esp32_ClockCryptoClock reset) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(DPORT_PERI_CLK_EN) |= clock;
    ESP32_REG(DPORT_PERI_RST_EN) &= ~reset;
    Esp32EnableInterrupts(mask);
}

/* Disable crypto clock */
LT_INLINE void
Esp32_ClockDisableCryptoClock(Esp32_ClockCryptoClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(DPORT_PERI_CLK_EN) &= ~clock;
    ESP32_REG(DPORT_PERI_RST_EN) |= clock;
    Esp32EnableInterrupts(mask);
}

#endif // #define PLATFORMS_ESP32_INCLUDE_ESP32_CLOCK_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Jun-22   tiberius    created
 */
