/******************************************************************************
 * Esp32_Clock.h                                                   ESP32-S3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32s3 counterpart of include/esp32/Esp32_Clock.h.
 *
 * Two things differ from the esp32.  Gating lives in SYSTEM rather than DPORT,
 * and the register that used to hold only the crypto engines (DPORT_PERI_CLK_EN)
 * is gone: on this part the crypto engines are simply more bits in
 * PERIP_CLK_EN1 alongside UART2, the DMA controller and the USB serial/JTAG
 * device, so they are gated through the same pair of calls as everything else in
 * that register and there is no separate crypto clock enum.
 *
 * The second difference is that the CPU frequency is not set here.  Reaching
 * 240 MHz on the esp32s3 means moving the RTC and digital LDO bias in step with
 * the frequency using per-part PVT calibration, which the vendored rtc_clk.c
 * already does correctly, so the second stage bootloader makes the switch and
 * Esp32_ClockInitialize() only reads back what it left behind.  See
 * source/esp32/ltbootloader/bootloader_clock_init.c.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CLOCK_H
#define PLATFORMS_ESP32_INCLUDE_ESP32S3_CLOCK_H

#include "Esp32_Irq.h"

/* Bits in SYSTEM_PERIP_CLK_EN0 and, at the same positions, SYSTEM_PERIP_RST_EN0 */
typedef u32 Esp32_ClockPeripheralClock;
enum Esp32_ClockPeripheralClocks {
    kEsp32_Clock_SPI4                       = (1u << 31),
    kEsp32_Clock_ADC2_ARB                   = (1u << 30),
    kEsp32_Clock_SYSTIMER                   = (1u << 29),
    kEsp32_Clock_APB_SARADC                 = (1u << 28),
    kEsp32_Clock_SPI3_DMA                   = (1u << 27),
    kEsp32_Clock_PWM3                       = (1u << 26),
    kEsp32_Clock_PWM2                       = (1u << 25),
    kEsp32_Clock_UART_MEM                   = (1u << 24),
    kEsp32_Clock_USB                        = (1u << 23),
    kEsp32_Clock_SPI2_DMA                   = (1u << 22),
    kEsp32_Clock_I2S1                       = (1u << 21),
    kEsp32_Clock_PWM1                       = (1u << 20),
    kEsp32_Clock_TWAI                       = (1u << 19),
    kEsp32_Clock_I2C_EXT1                   = (1u << 18),
    kEsp32_Clock_PWM0                       = (1u << 17),
    kEsp32_Clock_SPI3                       = (1u << 16),
    kEsp32_Clock_TIMERGROUP1                = (1u << 15),
    kEsp32_Clock_EFUSE                      = (1u << 14),
    kEsp32_Clock_TIMERGROUP                 = (1u << 13),
    kEsp32_Clock_UHCI1                      = (1u << 12),
    kEsp32_Clock_LEDC                       = (1u << 11),
    kEsp32_Clock_PCNT                       = (1u << 10),
    kEsp32_Clock_RMT                        = (1u << 9),
    kEsp32_Clock_UHCI0                      = (1u << 8),
    kEsp32_Clock_I2C_EXT0                   = (1u << 7),
    kEsp32_Clock_SPI2                       = (1u << 6),
    kEsp32_Clock_UART1                      = (1u << 5),
    kEsp32_Clock_I2S0                       = (1u << 4),
    kEsp32_Clock_WDG                        = (1u << 3),
    kEsp32_Clock_UART                       = (1u << 2),
    kEsp32_Clock_SPI01                      = (1u << 1),
    kEsp32_Clock_TIMERS                     = (1u << 0),
};

/* Bits in SYSTEM_PERIP_CLK_EN1 and, at the same positions, SYSTEM_PERIP_RST_EN1 */
typedef u32 Esp32_ClockModuleClock;
enum Esp32_ClockModuleClocks {
    kEsp32_ClockModule_USB_DEVICE           = (1u << 10),
    kEsp32_ClockModule_UART2                = (1u << 9),
    kEsp32_ClockModule_LCD_CAM              = (1u << 8),
    kEsp32_ClockModule_SDIO_HOST            = (1u << 7),
    kEsp32_ClockModule_DMA                  = (1u << 6),
    kEsp32_ClockModule_CRYPTO_HMAC          = (1u << 5),
    kEsp32_ClockModule_CRYPTO_DS            = (1u << 4),
    kEsp32_ClockModule_CRYPTO_RSA           = (1u << 3),
    kEsp32_ClockModule_CRYPTO_SHA           = (1u << 2),
    kEsp32_ClockModule_CRYPTO_AES           = (1u << 1),
    kEsp32_ClockModule_PERI_BACKUP          = (1u << 0),
};

/* Report the CPU clock in MHz the bootloader left the part running at */
u32 Esp32_ClockInitialize(void);

/* returns CPU clock in MHz */
u32 Esp32_ClockGetMHz(void);

/*
 * Gate the clock to the second CPU, which LT does not yet bring up.  Called
 * once, from Esp32_LTChipStart, before anything else has run on this core.  The
 * esp32 does this through DPORT_CPU1_CTRL_B; here the same bit lives in SYSTEM,
 * which is why the two chips keep their own copies of this.
 */
LT_INLINE void
Esp32_ClockDisableAppCpu(void) {
    ESP32_REG(SYSTEM_CORE_1_CONTROL_0) &= ~ESP32_REG_MASK(SYSTEM_CORE_1, CLKGATE_EN);
}

/* Enable peripheral clock */
LT_INLINE void
Esp32_ClockEnablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(SYSTEM_PERIP_CLK_EN0) |= clock;
    ESP32_REG(SYSTEM_PERIP_RST_EN0) &= ~clock;
    Esp32EnableInterrupts(mask);
}

/* Disable peripheral clock */
LT_INLINE void
Esp32_ClockDisablePeripheralClock(Esp32_ClockPeripheralClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(SYSTEM_PERIP_CLK_EN0) &= ~clock;
    ESP32_REG(SYSTEM_PERIP_RST_EN0) |= clock;
    Esp32EnableInterrupts(mask);
}

/* Enable module clock */
LT_INLINE void
Esp32_ClockEnableModuleClock(Esp32_ClockModuleClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(SYSTEM_PERIP_CLK_EN1) |= clock;
    ESP32_REG(SYSTEM_PERIP_RST_EN1) &= ~clock;
    Esp32EnableInterrupts(mask);
}

/* Disable module clock */
LT_INLINE void
Esp32_ClockDisableModuleClock(Esp32_ClockModuleClock clock) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(SYSTEM_PERIP_CLK_EN1) &= ~clock;
    ESP32_REG(SYSTEM_PERIP_RST_EN1) |= clock;
    Esp32EnableInterrupts(mask);
}

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32S3_CLOCK_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
