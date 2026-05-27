/******************************************************************************
 * Esp32_Clock.c                                                      ESP32 BSP
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
#include "Esp32_I2C.h"
#include "Esp32_Clock.h"

#define   ESP32_REF_CLK_FREQ            1000000

/*
 * Rough delay (in microsec) when running CPU with 40 MHz clock,
 *   It can be used at different speeds but delay will scale accordingly!
 */
static void ESP32_MEM_REGION(IRAM)
Esp32_RoughSpinDelayAt40M(u32 nMicrosecondsToWait) {
    for (volatile u32 nIdx = 0; nIdx < (nMicrosecondsToWait * 20); nIdx++);
}

/* Initialize clocks */
u32 ESP32_MEM_REGION(IRAM) Esp32_ClockInitialize(void) {

    u32 nVal;
    u32 nTemp;

    /*
     * Part 1:  Set CPU Clock
     */

    /* UART tx wait idle */
    while (ESP32_UART_REG(0, STATUS) & (ESP32_REG_MASK(UART_STATUS, TXFIFO_CNT) | ESP32_REG_MASK(UART_STATUS, UTX_OUT)));

    /* 1.1 Switch to 40M XTAL clock source while setting BBPLL */

    /* Set pre-divider to 1 (clear field) and adjust reference tick for 40M */
    nVal = ESP32_REG(APB_CTRL_SYSCLK_CONF) & ~ESP32_REG_MASK(APB_CTRL, PRE_DIV_CNT);
    ESP32_REG(APB_CTRL_SYSCLK_CONF) = nVal;
    ESP32_REG(APB_CTRL_XTAL_TICK_CONF) = (40000000 / ESP32_REF_CLK_FREQ) - 1;

    /* Set SOC clock source to XTAL */
    nVal = ESP32_REG(RTC_CNTL_CLK_CONF);
    nVal = (nVal & ~ESP32_REG_MASK(RTC_CNTL_SOC, CLK_SEL)) | ESP32_REG_VAL(RTC_CNTL_SOC, CLK_SEL_XTAL);
    ESP32_REG(RTC_CNTL_CLK_CONF) = nVal;

    /* APB frequency = 40M/40M */
    ESP32_REG(RTC_CNTL_STORE5) = ESP32_REG_VAL(RTC_CNTL_APB_FREQ, 40M_40M);

    /* Select 1.1V bias voltage (4) */
    nVal = (ESP32_REG(RTC_CNTL_VREG) & ~ESP32_REG_MASK(RTC_CNTL, DIG_DBIAS_WAK));
    nVal |= (4 << ESP32_REG_SHIFT(RTC_CNTL, DIG_DBIAS_WAK));
    ESP32_REG(RTC_CNTL_VREG) = nVal;

    /* 1.2 Enable and Configure BBPLL */

    /* Clear I2C pull-down force registers */
    nVal = ESP32_REG(RTC_CNTL_OPTIONS0) & ~ESP32_REG_MASK(RTC_CNTL, PLL_FORCE_PD);
    ESP32_REG(RTC_CNTL_OPTIONS0) = nVal;

    /* Set BBPLL options */
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, IR_CAL_DELAY),   ESP32_I2C_VAL(BBPLL, IR_CAL_DELAY));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, IR_CAL_EXT_CAP), ESP32_I2C_VAL(BBPLL, IR_CAL_EXT_CAP));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, OC_ENB_FCAL),    ESP32_I2C_VAL(BBPLL, OC_ENB_FCAL));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, OC_ENB_VCON),    ESP32_I2C_VAL(BBPLL, OC_ENB_VCON));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, BBADC_CAL_7_0),  ESP32_I2C_VAL(BBPLL, BBADC_CAL_7_0));

    /* Increase voltage for 240 MHz using fused calibration */
    nTemp = ESP32_REG(EFUSE_RD_BLK0_RDATA5) & ESP32_REG_MASK(EFUSE_RD_VOL_LEVEL, HP_INV);
    nTemp = 7 - (nTemp >> ESP32_REG_SHIFT(EFUSE_RD_VOL_LEVEL, HP_INV));
    nVal = (ESP32_REG(RTC_CNTL_VREG) & ~ESP32_REG_MASK(RTC_CNTL, DIG_DBIAS_WAK));
    nVal |= (nTemp << ESP32_REG_SHIFT(RTC_CNTL, DIG_DBIAS_WAK));
    ESP32_REG(RTC_CNTL_VREG) = nVal;

    /* Give some time for the bias voltage to settle */
    Esp32_RoughSpinDelayAt40M(3);

    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, ENDIV5),     ESP32_I2C_VAL(BBPLL, ENDIV5_480M));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, BBADC_DSMP), ESP32_I2C_VAL(BBPLL, BBADC_DSMP_480M));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, OC_LREF),    ESP32_I2C_VAL(BBPLL, OC_LREF_40M_480M));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, OC_DIV_7_0), ESP32_I2C_VAL(BBPLL, OC_DIV_7_0_40M_480M));
    ESP32_I2C_WRITE(BBPLL, ESP32_I2C_REG(BBPLL, OC_DCUR),    ESP32_I2C_VAL(BBPLL, OC_DCUR_40M_480M));

    /* 1.3 Set CPU frequency to 240MHz and APB to 80MHz */

    ESP32_REG(DPORT_CPU_PER_CONF) = ESP32_REG_VAL(DPORT_CPU_PER_CONF, 240M);

    /* NB: Bias voltage needs to remain at the high-level it was set to prior to this step. */

    /* Set SOC clock source to PLL */
    nVal = ESP32_REG(RTC_CNTL_CLK_CONF);
    nVal = (nVal & ~ESP32_REG_MASK(RTC_CNTL_SOC, CLK_SEL)) | ESP32_REG_VAL(RTC_CNTL_SOC, CLK_SEL_PLL);
    ESP32_REG(RTC_CNTL_CLK_CONF) = nVal;

    /* APB frequency = 80M/80M */
    ESP32_REG(RTC_CNTL_STORE5) = ESP32_REG_VAL(RTC_CNTL_APB_FREQ, 80M_80M);

    /*
     * Part 2:  RTC calibration
     */

    /* 2.1 RTC_CLK calibration */

    /* Clear Start, Select RTC_CLK, Configure for 1024 cycles */
    nVal = ESP32_REG(TIMG0_RTCCALICFG) & ~ESP32_REG_VAL(TIMG0, RTCCAL_START);
    nVal &= ~(ESP32_REG_MASK(TIMG0, RTCCAL_RTC_CLK_SEL) | ESP32_REG_MASK(TIMG0, RTCCAL_MAX));
    nVal |=   (ESP32_REG_VAL(TIMG0, RTCCAL_RTC_CLK_SEL) | (1024 << ESP32_REG_SHIFT(TIMG0, RTCCAL_MAX)));
    ESP32_REG(TIMG0_RTCCALICFG) = nVal;

    /* Start calibration */
    nVal |= ESP32_REG_VAL(TIMG0, RTCCAL_START);
    ESP32_REG(TIMG0_RTCCALICFG) = nVal;

    /* Wait for ready */
    do {
        /* Spin for less than 1 usec, as CPU is now running at 240 MHz */
        Esp32_RoughSpinDelayAt40M(1);
        nVal = ESP32_REG(TIMG0_RTCCALICFG) & ESP32_REG_MASK(TIMG0_RTCCAL, READY);
    } while (nVal == 0);

    return 240;
}

/* return configured clock speed in MHz */
u32 Esp32_ClockGetMHz(void) {
    u32 regVal = ESP32_REG(DPORT_CPU_PER_CONF);
    u32 speed  = 0;
    switch (regVal) {
        case ESP32_REG_VAL(DPORT_CPU_PER_CONF, 80M):
            speed = 80;
            break;
        case ESP32_REG_VAL(DPORT_CPU_PER_CONF, 160M):
            speed = 160;
            break;
        case ESP32_REG_VAL(DPORT_CPU_PER_CONF, 240M):
        default:
            speed = 240;
            break;
    }
    return speed;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Jun-22   tiberius    created
 */
