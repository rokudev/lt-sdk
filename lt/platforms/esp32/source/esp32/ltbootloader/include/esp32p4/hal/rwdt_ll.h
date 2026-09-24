/*******************************************************************************
 * rwdt_ll.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
//
// The RWDT low level layer for the esp32p4.
//
// There is no RTC_CNTL block on this part.  What the other esp32 family members
// call the RTC watchdog lives in the LP_WDT block here, with its own register
// layout, so this is a reimplementation against lp_wdt_dev_t rather than a copy
// of another target's rwdt_ll.h.  IDF splits the same thing into an lpwdt_ll.h
// plus a one-line rwdt_ll.h shim; keeping it in one file avoids importing a
// second HAL generation for the sake of twenty #defines.
//
// rtc_cntl_dev_t is typedef'd to lp_wdt_dev_t below so that the shared
// hal/wdt_hal.h, wdt_hal_iram.c, bootloader_init.c and flash_encrypt.c - which
// all name rtc_cntl_dev_t and RTCCNTL directly - need no esp32p4 arm.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hal/wdt_types.h"
#include "soc/lp_wdt_struct.h"
#include "esp_attr.h"
#include "esp_assert.h"
#include "lt/LTTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RWDT of the other targets is the LP_WDT here.  LP_WDT is PROVIDEd at
   0x50116000 by esp32p4.peripherals.ld. */
typedef lp_wdt_dev_t rtc_cntl_dev_t;
#define RTCCNTL LP_WDT

/* Write-enable key for LP_WDT_WPROTECT_REG. */
#define LP_WDT_WKEY_VALUE 0x50D83AA1

/* LP_WDT_WDT_STGx field encodings.  Unlike the MWDT the LP_WDT stage fields are
   3 bits wide and carry a fifth action, reset-including-the-RTC. */
#define LP_WDT_STG_SEL_OFF          0
#define LP_WDT_STG_SEL_INT          1
#define LP_WDT_STG_SEL_RESET_CPU    2
#define LP_WDT_STG_SEL_RESET_SYSTEM 3
#define LP_WDT_STG_SEL_RESET_RTC    4

ESP_STATIC_ASSERT(WDT_STAGE_ACTION_OFF == LP_WDT_STG_SEL_OFF, "wdt_stage_action_t no longer matches the LP_WDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_INT == LP_WDT_STG_SEL_INT, "wdt_stage_action_t no longer matches the LP_WDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_RESET_CPU == LP_WDT_STG_SEL_RESET_CPU, "wdt_stage_action_t no longer matches the LP_WDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_RESET_SYSTEM == LP_WDT_STG_SEL_RESET_SYSTEM, "wdt_stage_action_t no longer matches the LP_WDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_RESET_RTC == LP_WDT_STG_SEL_RESET_RTC, "wdt_stage_action_t no longer matches the LP_WDT stage encoding");

FORCE_INLINE_ATTR void rwdt_ll_enable(rtc_cntl_dev_t *hw)
{
    hw->config0.wdt_en = 1;
}

FORCE_INLINE_ATTR void rwdt_ll_disable(rtc_cntl_dev_t *hw)
{
    hw->config0.wdt_en = 0;
}

FORCE_INLINE_ATTR bool rwdt_ll_check_if_enabled(rtc_cntl_dev_t *hw)
{
    return (hw->config0.wdt_en) ? true : false;
}

FORCE_INLINE_ATTR void rwdt_ll_config_stage(rtc_cntl_dev_t *hw, wdt_stage_t stage, uint32_t timeout_ticks, wdt_stage_action_t behavior)
{
    switch (stage) {
    case WDT_STAGE0:
        hw->config0.wdt_stg0 = behavior;
        hw->config1.wdt_stg0_hold = timeout_ticks;
        break;
    case WDT_STAGE1:
        hw->config0.wdt_stg1 = behavior;
        hw->config2.wdt_stg1_hold = timeout_ticks;
        break;
    case WDT_STAGE2:
        hw->config0.wdt_stg2 = behavior;
        hw->config3.wdt_stg2_hold = timeout_ticks;
        break;
    case WDT_STAGE3:
        hw->config0.wdt_stg3 = behavior;
        hw->config4.wdt_stg3_hold = timeout_ticks;
        break;
    default:
        break;
    }
}

FORCE_INLINE_ATTR void rwdt_ll_disable_stage(rtc_cntl_dev_t *hw, wdt_stage_t stage)
{
    switch (stage) {
    case WDT_STAGE0:
        hw->config0.wdt_stg0 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE1:
        hw->config0.wdt_stg1 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE2:
        hw->config0.wdt_stg2 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE3:
        hw->config0.wdt_stg3 = WDT_STAGE_ACTION_OFF;
        break;
    default:
        break;
    }
}

FORCE_INLINE_ATTR void rwdt_ll_set_cpu_reset_length(rtc_cntl_dev_t *hw, wdt_reset_sig_length_t length)
{
    hw->config0.wdt_cpu_reset_length = length;
}

FORCE_INLINE_ATTR void rwdt_ll_set_sys_reset_length(rtc_cntl_dev_t *hw, wdt_reset_sig_length_t length)
{
    hw->config0.wdt_sys_reset_length = length;
}

FORCE_INLINE_ATTR void rwdt_ll_set_flashboot_en(rtc_cntl_dev_t *hw, bool enable)
{
    hw->config0.wdt_flashboot_mod_en = (enable) ? 1 : 0;
}

FORCE_INLINE_ATTR void rwdt_ll_set_procpu_reset_en(rtc_cntl_dev_t *hw, bool enable)
{
    hw->config0.wdt_procpu_reset_en = (enable) ? 1 : 0;
}

FORCE_INLINE_ATTR void rwdt_ll_set_appcpu_reset_en(rtc_cntl_dev_t *hw, bool enable)
{
    hw->config0.wdt_appcpu_reset_en = (enable) ? 1 : 0;
}

FORCE_INLINE_ATTR void rwdt_ll_set_pause_in_sleep_en(rtc_cntl_dev_t *hw, bool enable)
{
    hw->config0.wdt_pause_in_slp = (enable) ? 1 : 0;
}

/* The esp32p4 has no analog-reset-on-timeout register and no reset width
   register.  These two exist only so that the shared wdt_hal_iram.c compiles
   unchanged. */
FORCE_INLINE_ATTR void rwdt_ll_set_chip_reset_en(rtc_cntl_dev_t *hw, bool enable)
{
    LT_UNUSED(hw);
    LT_UNUSED(enable);
}

FORCE_INLINE_ATTR void rwdt_ll_set_chip_reset_width(rtc_cntl_dev_t *hw, uint32_t width)
{
    LT_UNUSED(hw);
    LT_UNUSED(width);
}

FORCE_INLINE_ATTR void rwdt_ll_feed(rtc_cntl_dev_t *hw)
{
    hw->feed.feed = 1;
}

FORCE_INLINE_ATTR void rwdt_ll_write_protect_enable(rtc_cntl_dev_t *hw)
{
    hw->wprotect.wdt_wkey = 0;
}

FORCE_INLINE_ATTR void rwdt_ll_write_protect_disable(rtc_cntl_dev_t *hw)
{
    hw->wprotect.wdt_wkey = LP_WDT_WKEY_VALUE;
}

FORCE_INLINE_ATTR void rwdt_ll_set_intr_enable(rtc_cntl_dev_t *hw, bool enable)
{
    hw->int_ena.lp_wdt_int_ena = (enable) ? 1 : 0;
}

FORCE_INLINE_ATTR bool rwdt_ll_check_intr_status(rtc_cntl_dev_t *hw)
{
    return (hw->int_st.lp_wdt_int_st) ? true : false;
}

FORCE_INLINE_ATTR void rwdt_ll_clear_intr_status(rtc_cntl_dev_t *hw)
{
    hw->int_clr.lp_wdt_int_clr = 1;
}

#ifdef __cplusplus
}
#endif
