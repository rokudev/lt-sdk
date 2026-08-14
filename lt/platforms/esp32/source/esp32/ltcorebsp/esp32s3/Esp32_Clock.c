/******************************************************************************
 * Esp32_Clock.c                                                   ESP32-S3 BSP
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
 * Unlike the esp32 arm of this file, nothing here drives the BBPLL.
 *
 * The esp32 reaches 240 MHz by writing the PLL over the analog I2C bus and then
 * raising DIG_DBIAS_WAK by a fused correction, all of which the BSP can do with
 * plain register writes.  The esp32s3 cannot be treated that way: its LDO has six
 * switchable slaves that have to be opened in step with the frequency, and the
 * bias itself comes from per-part PVT calibration fuses rather than a single
 * correction.  That sequence is already written, in the rtc_clk.c the second
 * stage bootloader vendors, so the switch is made there - see
 * source/esp32/ltbootloader/bootloader_clock_init.c - and this file only reports
 * what the bootloader left running.
 */

enum {
    /* Every esp32s3 module ships a 40 MHz crystal; the part also supports 32 MHz */
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
     * On the PLL path CPUPERIOD_SEL divides the fixed 480 MHz BBPLL; on either of
     * the other two paths PRE_DIV_CNT divides the source directly.
     */
    if (nSource == ESP32_REG_VAL(SYSTEM_SOC_CLK, PLL)) {
        u32 nPeriod = (ESP32_REG(SYSTEM_CPU_PER_CONF) & ESP32_REG_MASK(SYSTEM_CPU_PER_CONF, CPUPERIOD_SEL))
                      >> ESP32_REG_SHIFT(SYSTEM_CPU_PER_CONF, CPUPERIOD_SEL);
        switch (nPeriod) {
            case ESP32_REG_VAL(SYSTEM_CPUPERIOD, 80M):
                return 80;
            case ESP32_REG_VAL(SYSTEM_CPUPERIOD, 160M):
                return 160;
            case ESP32_REG_VAL(SYSTEM_CPUPERIOD, 240M):
            default:
                return 240;
        }
    }

    u32 nDivider  = ((nSysClkConf & ESP32_REG_MASK(SYSTEM_SYSCLK_CONF, PRE_DIV_CNT))
                     >> ESP32_REG_SHIFT(SYSTEM_SYSCLK_CONF, PRE_DIV_CNT)) + 1;
    u32 nSourceMHz = (nSource == ESP32_REG_VAL(SYSTEM_SOC_CLK, FOSC)) ? kEsp32_ClockRcFastMHz
                                                                     : kEsp32_ClockXtalMHz;
    return nSourceMHz / nDivider;
}

/* Initialize clocks */
u32 ESP32_MEM_REGION(IRAM) Esp32_ClockInitialize(void) {
    return Esp32_ClockGetMHz();
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
