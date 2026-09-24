/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
//
// The clock getters the shared bootloader sources need on the esp32p4, and
// nothing else.  This is not IDF's rtc_clk.c.
//
// IDF's esp32p4 version is written against the v5.4 HAL - clk_tree_ll.h,
// pmu_hal.h, pmu_struct.h, esp_pmu.h - which this tree does not carry, and
// which pulls in about thirty thousand lines of headers from a second IDF
// generation if imported.  It is also not reachable as written: every frequency
// change in it goes through REGI2C, and the esp32p4 ROM exports no rom_i2c_*
// entries at all, unlike every earlier part here.
//
// So the part is left on the clock the ROM chose, and only the two getters the
// shared sources actually call are implemented.  Grepping the sources this
// library compiles, that is the whole clock surface:
//
//   rtc_clk_apb_freq_get()      bootloader_clock_loader.c, bootloader_console.c
//   rtc_clk_slow_freq_get_hz()  bootloader_init.c, for the RWDT stage timeout
//
// The console's use is vestigial on this chip - ESP_ROM_UART_CLK_IS_XTAL is 1,
// so bootloader_console.c overwrites the value with UART_CLK_FREQ_ROM before
// computing a divider, and the UART is left at whatever the ROM set anyway.
//
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_attr.h"
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "esp32p4/rom/ets_sys.h"

/* LP_CLKRST is LPAON + 0x1000.  SLOW_CLK_SEL is LP_CLK_CONF[1:0], and selects
 * RC_SLOW, XTAL32K or RC32K in that order, matching soc_rtc_slow_clk_src_t. */
#define LT_LP_CLKRST_LP_CLK_CONF_REG  (DR_REG_LP_CLKRST_BASE + 0x0)
#define LT_LP_CLKRST_SLOW_CLK_SEL_M   0x03
#define LT_LP_CLKRST_SLOW_CLK_SEL_S   0

/* Approximate rates from soc/clk_tree_defs.h.  The RC oscillators are only
 * trimmed, never calibrated here, so these are nominal by nature - which is
 * fine for the one consumer, a five second watchdog stage. */
#define LT_RC_SLOW_FREQ_APPROX        136000
#define LT_RC32K_FREQ_APPROX          32768
#define LT_XTAL32K_FREQ_APPROX        32768

uint32_t rtc_clk_slow_freq_get_hz(void)
{
    uint32_t nSelect = (REG_READ(LT_LP_CLKRST_LP_CLK_CONF_REG) >> LT_LP_CLKRST_SLOW_CLK_SEL_S)
                       & LT_LP_CLKRST_SLOW_CLK_SEL_M;
    switch (nSelect) {
    case 0:  return LT_RC_SLOW_FREQ_APPROX;
    case 1:  return LT_XTAL32K_FREQ_APPROX;
    case 2:  return LT_RC32K_FREQ_APPROX;
    default: return 0;
    }
}

uint32_t rtc_clk_apb_freq_get(void)
{
    /* APB sits three integer dividers below the CPU on this part: CPU -> MEM ->
     * SYS -> APB.  Each register field holds divider-1. */
    uint32_t nMemDiv = ((REG_READ(HP_SYS_CLKRST_ROOT_CLK_CTRL1_REG) >> HP_SYS_CLKRST_REG_MEM_CLK_DIV_NUM_S)
                        & HP_SYS_CLKRST_REG_MEM_CLK_DIV_NUM_V) + 1;
    uint32_t nSysDiv = ((REG_READ(HP_SYS_CLKRST_ROOT_CLK_CTRL1_REG) >> HP_SYS_CLKRST_REG_SYS_CLK_DIV_NUM_S)
                        & HP_SYS_CLKRST_REG_SYS_CLK_DIV_NUM_V) + 1;
    uint32_t nApbDiv = ((REG_READ(HP_SYS_CLKRST_ROOT_CLK_CTRL2_REG) >> HP_SYS_CLKRST_REG_APB_CLK_DIV_NUM_S)
                        & HP_SYS_CLKRST_REG_APB_CLK_DIV_NUM_V) + 1;

    /* esp_rom_caps.h sets ESP_ROM_GET_CLK_FREQ for this part, which is IDF's own
     * statement that the ROM entry is the way to read the CPU rate.  It reports MHz. */
    return (ets_get_cpu_frequency() * MHZ) / (nMemDiv * nSysDiv * nApbDiv);
}
