/******************************************************************************
 * Esp32_Clock.c                                                   ESP32-C3 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Clock.h"

/*
 * As on the esp32s3, nothing here drives the BBPLL.  The frequency switch needs
 * the LDO slaves opened in step with it and a bias taken from per-part
 * calibration fuses, and that sequence already exists in the rtc_clk.c the second
 * stage bootloader vendors - see source/esp32/ltbootloader/bootloader_clock_init.c.
 * This file only reports what the bootloader left running.
 */

enum {
    /* Every esp32c3 module ships a 40 MHz crystal; there is no 26 MHz option */
    kEsp32_ClockXtalMHz   = 40,
    /* Nominal RC_FAST rate.  Uncalibrated, and never a CPU source in this port */
    kEsp32_ClockRcFastMHz = 17,
};

/* return configured clock speed in MHz */
u32 Esp32_ClockGetMHz(void) {

    u32 nSysClkConf = ESP32_REG(SYSTEM_SYSCLK_CONF);
    u32 nSource     = (nSysClkConf & ESP32_REG_MASK(SYSTEM_SYSCLK_CONF, SOC_CLK_SEL))
                      >> ESP32_REG_SHIFT(SYSTEM_SYSCLK_CONF, SOC_CLK_SEL);

    /*
     * On the PLL path CPUPERIOD_SEL divides the 480 MHz BBPLL; on either of the
     * other two paths PRE_DIV_CNT divides the source directly.  This part tops
     * out at 160 MHz, so unlike the esp32s3 there is no 240 MHz case.
     */
    if (nSource == ESP32_REG_VAL(SYSTEM_SOC_CLK, PLL)) {
        u32 nPeriod = (ESP32_REG(SYSTEM_CPU_PER_CONF) & ESP32_REG_MASK(SYSTEM_CPU_PER_CONF, CPUPERIOD_SEL))
                      >> ESP32_REG_SHIFT(SYSTEM_CPU_PER_CONF, CPUPERIOD_SEL);
        switch (nPeriod) {
            case ESP32_REG_VAL(SYSTEM_CPUPERIOD, 80M):
                return 80;
            case ESP32_REG_VAL(SYSTEM_CPUPERIOD, 160M):
            default:
                return 160;
        }
    }

    u32 nDivider   = ((nSysClkConf & ESP32_REG_MASK(SYSTEM_SYSCLK_CONF, PRE_DIV_CNT))
                      >> ESP32_REG_SHIFT(SYSTEM_SYSCLK_CONF, PRE_DIV_CNT)) + 1;
    u32 nSourceMHz = (nSource == ESP32_REG_VAL(SYSTEM_SOC_CLK, FOSC)) ? kEsp32_ClockRcFastMHz
                                                                      : kEsp32_ClockXtalMHz;
    return nSourceMHz / nDivider;
}

/* Initialize clocks */
u32 ESP32_MEM_REGION(IRAM) Esp32_ClockInitialize(void) {
    return Esp32_ClockGetMHz();
}

/*
 * Enable the clock the Wi-Fi and BT radios share.  The caller reference counts.
 * The esp32 gates these bits in DPORT_WIFI_CLK_EN; here the same register sits in
 * APB_CTRL, which IDF calls SYSTEM_WIFI_CLK_EN_REG.
 */
void Esp32_ClockEnableRadioCommonClock(void) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(APB_CTRL_WIFI_CLK_EN) |= ESP32_REG_MASK(APB_CTRL_WIFI_CLK, WIFI_BT_COMMON);
    Esp32EnableInterrupts(mask);
}

/* Disable the clock the Wi-Fi and BT radios share. */
void Esp32_ClockDisableRadioCommonClock(void) {
    u32 mask = Esp32DisableInterrupts();
    ESP32_REG(APB_CTRL_WIFI_CLK_EN) &= ~ESP32_REG_MASK(APB_CTRL_WIFI_CLK, WIFI_BT_COMMON);
    Esp32EnableInterrupts(mask);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
