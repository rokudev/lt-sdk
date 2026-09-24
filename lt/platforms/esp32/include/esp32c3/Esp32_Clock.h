/******************************************************************************
 * Esp32_Clock.h                                                   ESP32-C3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32c3 counterpart of include/esp32s3/Esp32_Clock.h.
 *
 * Gating lives in SYSTEM, as it does on the esp32s3, and the two registers hold
 * nearly the same bits - but not quite, so the enums below are this part's own.
 * The one that matters most: the USB serial/JTAG device is gated from
 * PERIP_CLK_EN0 here, not from PERIP_CLK_EN1, and bit 23 of EN0 is the esp32s3's
 * USB OTG controller, which this part does not have.  Getting that wrong costs
 * you the console.
 *
 * There is also no second CPU to gate, and no PERI_BACKUP.
 *
 * As on the esp32s3 the CPU frequency is not set here - the second stage
 * bootloader makes the switch using the vendored rtc_clk.c, and
 * Esp32_ClockInitialize() only reads back what it left behind.  This part tops
 * out at 160 MHz.  See source/esp32/ltbootloader/bootloader_clock_init.c.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_CLOCK_H
#define PLATFORMS_ESP32_INCLUDE_ESP32C3_CLOCK_H

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
    kEsp32_Clock_USB_DEVICE                 = (1u << 23),
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
    kEsp32_ClockModule_TSENS                = (1u << 10),
    kEsp32_ClockModule_UART2                = (1u << 9),
    kEsp32_ClockModule_LCD_CAM              = (1u << 8),
    kEsp32_ClockModule_SDIO_HOST            = (1u << 7),
    kEsp32_ClockModule_DMA                  = (1u << 6),
    kEsp32_ClockModule_CRYPTO_HMAC          = (1u << 5),
    kEsp32_ClockModule_CRYPTO_DS            = (1u << 4),
    kEsp32_ClockModule_CRYPTO_RSA           = (1u << 3),
    kEsp32_ClockModule_CRYPTO_SHA           = (1u << 2),
    kEsp32_ClockModule_CRYPTO_AES           = (1u << 1),
};

/* Report the CPU clock in MHz the bootloader left the part running at */
u32 Esp32_ClockInitialize(void);

/* returns CPU clock in MHz */
u32 Esp32_ClockGetMHz(void);

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

/*
 * Gate the clock the Wi-Fi and BT radios share.  Out of line, unlike the rest of
 * this header, because the Wi-Fi/BLE OS adapter is its only caller and cannot
 * include this header: the adapter is built against the esp32 register map on
 * every part.  The caller reference counts - both radios use these bits.
 */
void Esp32_ClockEnableRadioCommonClock(void);
void Esp32_ClockDisableRadioCommonClock(void);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32C3_CLOCK_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
