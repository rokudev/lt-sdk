/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
//
// The clock bring-up and the clock getters the shared bootloader sources need
// on the esp32p4.  This is not IDF's rtc_clk.c.
//
// IDF's esp32p4 version is written against the v5.4 HAL - clk_tree_ll.h,
// pmu_hal.h, pmu_struct.h, esp_pmu.h - which this tree does not carry, and
// which pulls in about thirty thousand lines of headers from a second IDF
// generation if imported.  So the CPLL sequence it performs is open coded here
// against the register headers this tree does have, driving the analog bus
// through the LT copy of the REGI2C master in esp_rom_regi2c_esp32p4.c - the
// same path bootloader_esp32p4.c already uses for the BIAS and PLL trims.
//
// rtc_clk_init() below is deliberately narrower than IDF's.  It sets the CPU
// frequency and nothing else:
//
//   - The RC_FAST / RC_SLOW / RC32K trims and the slow and fast clock source
//     selects are left as the ROM set them.  Nothing in this bootloader reads
//     them beyond rtc_clk_slow_freq_get_hz(), whose one consumer is a five
//     second watchdog stage.
//   - The efuse calibrated regulator bias and the DCDC switch are left alone.
//     That path needs efuse_hal_blk_version() and efuse_ll_get_active_hp_dbias(),
//     neither of which this tree's efuse HAL carries, and at 90MHz the reset
//     voltage is ample - the calibration matters at 360/400MHz, which is the
//     BSP's business, not the bootloader's.
//
// The two getters the shared sources call are:
//
//   rtc_clk_apb_freq_get()      bootloader_clock_loader.c, bootloader_console.c
//   rtc_clk_slow_freq_get_hz()  bootloader_init.c, for the RWDT stage timeout
//
// The console's use is vestigial on this chip - ESP_ROM_UART_CLK_IS_XTAL is 1,
// so bootloader_console.c overwrites the value with UART_CLK_FREQ_ROM before
// computing a divider, and the UART is left at whatever the ROM set anyway.
// That is also why raising the CPU clock here cannot disturb the console.
//
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/chip_revision.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "soc/pmu_reg.h"
#include "soc/regi2c_cpll.h"
#include "esp_private/regi2c_ctrl.h"
#include "esp32p4/rom/ets_sys.h"
#include "hal/efuse_hal.h"

/* LP_CLKRST is LPAON + 0x1000.  SLOW_CLK_SEL is LP_CLK_CONF[1:0], and selects
 * RC_SLOW, XTAL32K or RC32K in that order, matching soc_rtc_slow_clk_src_t. */
#define LT_LP_CLKRST_LP_CLK_CONF_REG  (DR_REG_LP_CLKRST_BASE + 0x0)
#define LT_LP_CLKRST_SLOW_CLK_SEL_M   0x03
#define LT_LP_CLKRST_SLOW_CLK_SEL_S   0

/* The HP root clock mux, LPAON + 0x1040.  HP_ROOT_CLK_SRC_SEL[1:0] picks XTAL,
 * CPLL or the 20MHz RC in that order.  This tree carries no lp_clkrst_reg.h. */
#define LT_LP_CLKRST_HP_CLK_CTRL_REG            (DR_REG_LP_CLKRST_BASE + 0x40)
#define LT_LP_CLKRST_HP_ROOT_CLK_SRC_SEL_V      0x03
#define LT_LP_CLKRST_HP_ROOT_CLK_SRC_SEL_S      0
#define LT_HP_ROOT_CLK_SRC_XTAL                 0
#define LT_HP_ROOT_CLK_SRC_CPLL                 1

/* Approximate rates from soc/clk_tree_defs.h.  The RC oscillators are only
 * trimmed, never calibrated here, so these are nominal by nature - which is
 * fine for the one consumer, a five second watchdog stage. */
#define LT_RC_SLOW_FREQ_APPROX        136000
#define LT_RC32K_FREQ_APPROX          32768
#define LT_XTAL32K_FREQ_APPROX        32768

/* MEM_CLK is capped at 200MHz and APB_CLK at 100MHz, which leaves exactly three
 * usable CPU rates off a 360MHz CPLL.  Anything else is refused rather than
 * approximated: the hardware silently corrects an illegal divider without
 * reflecting it in the register, so the real bus rates become unknowable. */
typedef struct {
    uint32_t nCpuMHz;
    uint32_t nCpuDiv;
    uint32_t nMemDiv;
    uint32_t nApbDiv;
} LTCpllOperatingPoint;

static const LTCpllOperatingPoint s_cpllOperatingPoints[] = {
    { 360, 1, 2, 2 },
    { 180, 2, 1, 2 },
    {  90, 4, 1, 1 },
};
#define kCpllOperatingPointCount (sizeof(s_cpllOperatingPoints) / sizeof(s_cpllOperatingPoints[0]))

/* The divider registers latch together on this write-trigger bit, which the
 * hardware clears when the new set has taken effect. */
static void BusUpdate(void)
{
    REG_SET_BIT(HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_SOC_CLK_DIV_UPDATE);
    while (REG_GET_BIT(HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_SOC_CLK_DIV_UPDATE)) {
    }
}

/* Every divider field holds n-1.  The CPU divider also has a fractional part,
 * which is left at zero throughout. */
static void SetCpuDivider(uint32_t nDivider)
{
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_CLK_DIV_NUM, nDivider - 1);
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_CLK_DIV_NUMERATOR, 0);
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_CLK_DIV_DENOMINATOR, 0);
}

static void SetMemDivider(uint32_t nDivider)
{
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL1_REG, HP_SYS_CLKRST_REG_MEM_CLK_DIV_NUM, nDivider - 1);
}

static void SetSysDivider(uint32_t nDivider)
{
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL1_REG, HP_SYS_CLKRST_REG_SYS_CLK_DIV_NUM, nDivider - 1);
}

static void SetApbDivider(uint32_t nDivider)
{
    REG_SET_FIELD(HP_SYS_CLKRST_ROOT_CLK_CTRL2_REG, HP_SYS_CLKRST_REG_APB_CLK_DIV_NUM, nDivider - 1);
}

static void SetRootClockSource(uint32_t nSource)
{
    REG_SET_FIELD(LT_LP_CLKRST_HP_CLK_CTRL_REG, LT_LP_CLKRST_HP_ROOT_CLK_SRC_SEL, nSource);
}

/* Park the whole tree on the crystal at 1:1:1:1.  The update bit does not reach
 * the source mux, so the mux is moved first and the dividers after it: coming
 * the other way would leave the CPLL driving an illegal divider set. */
static void SwitchToXtal(void)
{
    SetRootClockSource(LT_HP_ROOT_CLK_SRC_XTAL);
    SetCpuDivider(1);
    SetMemDivider(1);
    SetSysDivider(1);
    SetApbDivider(1);
    BusUpdate();
    ets_update_cpu_frequency(SOC_XTAL_FREQ_40M);
}

static void CpllEnable(void)
{
    SET_PERI_REG_MASK(PMU_IMM_HP_CK_POWER_REG, PMU_TIE_HIGH_XPD_CPLL | PMU_TIE_HIGH_XPD_CPLL_I2C);
    SET_PERI_REG_MASK(PMU_IMM_HP_CK_POWER_REG, PMU_TIE_HIGH_GLOBAL_CPLL_ICG);
}

/* Calibrate the CPLL to 360MHz against the 40MHz crystal, which is the only
 * crystal this part supports.  Calibration runs while CAL_STOP is clear. */
static void CpllConfigure(void)
{
    const uint8_t nDchgp = 5;
    const uint8_t nDcur = 3;
    const uint8_t nEnbFcal = 0;
    const uint8_t nDivRef = 0;

    /* Bits 2 and 3 of the divider code are swapped from ECO1 onwards, so the
     * same 360MHz is a different number on either side of the revision. */
    uint8_t nDiv7_0 = ESP_CHIP_REV_ABOVE(efuse_hal_chip_revision(), 1) ? 9 : 5;

    CLEAR_PERI_REG_MASK(HP_SYS_CLKRST_ANA_PLL_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_PLL_CAL_STOP);

    REGI2C_WRITE(I2C_CPLL, I2C_CPLL_OC_REF_DIV,
                 (uint8_t) ((nEnbFcal << I2C_CPLL_OC_ENB_FCAL_LSB) | (nDchgp << I2C_CPLL_OC_DCHGP_LSB) | nDivRef));
    REGI2C_WRITE(I2C_CPLL, I2C_CPLL_OC_DIV_7_0, nDiv7_0);
    REGI2C_WRITE(I2C_CPLL, I2C_CPLL_OC_DCUR,
                 (uint8_t) ((1 << I2C_CPLL_OC_DLREF_SEL_LSB) | (3 << I2C_CPLL_OC_DHREF_SEL_LSB) | nDcur));

    while (!REG_GET_BIT(HP_SYS_CLKRST_ANA_PLL_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_PLL_CAL_END)) {
    }
    esp_rom_delay_us(10);  /* let it come to a true stop before freezing it */
    SET_PERI_REG_MASK(HP_SYS_CLKRST_ANA_PLL_CTRL0_REG, HP_SYS_CLKRST_REG_CPU_PLL_CAL_STOP);
}

/* Move the tree onto the CPLL.  The dividers are widened from APB upwards so no
 * intermediate state ever overclocks a bus, and the source mux goes last for the
 * same reason - it is not covered by the update bit. */
static void SwitchToCpll(const LTCpllOperatingPoint *pPoint)
{
    SetApbDivider(pPoint->nApbDiv);
    BusUpdate();
    SetSysDivider(1);
    BusUpdate();
    SetMemDivider(pPoint->nMemDiv);
    BusUpdate();
    SetCpuDivider(pPoint->nCpuDiv);
    BusUpdate();

    SetRootClockSource(LT_HP_ROOT_CLK_SRC_CPLL);
    ets_update_cpu_frequency(pPoint->nCpuMHz);
}

void rtc_clk_init(rtc_clk_config_t cfg)
{
    const LTCpllOperatingPoint *pPoint = NULL;
    uint32_t i;

    for (i = 0; i < kCpllOperatingPointCount; i++) {
        if (s_cpllOperatingPoints[i].nCpuMHz == cfg.cpu_freq_mhz) {
            pPoint = &s_cpllOperatingPoints[i];
            break;
        }
    }
    if (pPoint == NULL) {
        /* Leave the part where the ROM left it rather than guess at dividers. */
        return;
    }

    /* The CPLL cannot be calibrated while it is clocking the CPU. */
    CpllEnable();
    SwitchToXtal();
    CpllConfigure();
    SwitchToCpll(pPoint);
}

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
