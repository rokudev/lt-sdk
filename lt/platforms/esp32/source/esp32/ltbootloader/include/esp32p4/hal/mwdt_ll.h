/*******************************************************************************
 * mwdt_ll.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
//
// The MWDT low level layer for the esp32p4.
//
// Field-for-field the same as the other RISC-V targets' mwdt_ll.h, reproduced
// here rather than imported because IDF's esp32p4 version reaches for the v5.4
// clock tree (soc/hp_sys_clkrst_struct.h, 2987 lines) to implement a clock
// source selector nothing in this tree calls.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hal/misc.h"
#include "hal/wdt_types.h"
#include "soc/timer_group_struct.h"
#include "esp_attr.h"
#include "esp_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write-enable key for TIMG_WDTWPROTECT_REG. */
#define TIMG_WDT_WKEY_VALUE 0x50D83AA1

/* TIMG_WDT_STGx field encodings.  The MWDT stage fields are 2 bits wide, so
   unlike the LP_WDT there is no reset-including-the-RTC action. */
#define TIMG_WDT_STG_SEL_OFF          0
#define TIMG_WDT_STG_SEL_INT          1
#define TIMG_WDT_STG_SEL_RESET_CPU    2
#define TIMG_WDT_STG_SEL_RESET_SYSTEM 3

ESP_STATIC_ASSERT(WDT_STAGE_ACTION_OFF == TIMG_WDT_STG_SEL_OFF, "wdt_stage_action_t no longer matches the MWDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_INT == TIMG_WDT_STG_SEL_INT, "wdt_stage_action_t no longer matches the MWDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_RESET_CPU == TIMG_WDT_STG_SEL_RESET_CPU, "wdt_stage_action_t no longer matches the MWDT stage encoding");
ESP_STATIC_ASSERT(WDT_STAGE_ACTION_RESET_SYSTEM == TIMG_WDT_STG_SEL_RESET_SYSTEM, "wdt_stage_action_t no longer matches the MWDT stage encoding");

FORCE_INLINE_ATTR void mwdt_ll_enable(timg_dev_t *hw)
{
    hw->wdtconfig0.wdt_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_disable(timg_dev_t *hw)
{
    hw->wdtconfig0.wdt_en = 0;
}

FORCE_INLINE_ATTR bool mwdt_ll_check_if_enabled(timg_dev_t *hw)
{
    return (hw->wdtconfig0.wdt_en) ? true : false;
}

FORCE_INLINE_ATTR void mwdt_ll_config_stage(timg_dev_t *hw, wdt_stage_t stage, uint32_t timeout, wdt_stage_action_t behavior)
{
    switch (stage) {
    case WDT_STAGE0:
        hw->wdtconfig0.wdt_stg0 = behavior;
        hw->wdtconfig2.wdt_stg0_hold = timeout;
        break;
    case WDT_STAGE1:
        hw->wdtconfig0.wdt_stg1 = behavior;
        hw->wdtconfig3.wdt_stg1_hold = timeout;
        break;
    case WDT_STAGE2:
        hw->wdtconfig0.wdt_stg2 = behavior;
        hw->wdtconfig4.wdt_stg2_hold = timeout;
        break;
    case WDT_STAGE3:
        hw->wdtconfig0.wdt_stg3 = behavior;
        hw->wdtconfig5.wdt_stg3_hold = timeout;
        break;
    default:
        break;
    }
    /* The MWDT config registers are latched, not live. */
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_disable_stage(timg_dev_t *hw, uint32_t stage)
{
    switch (stage) {
    case WDT_STAGE0:
        hw->wdtconfig0.wdt_stg0 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE1:
        hw->wdtconfig0.wdt_stg1 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE2:
        hw->wdtconfig0.wdt_stg2 = WDT_STAGE_ACTION_OFF;
        break;
    case WDT_STAGE3:
        hw->wdtconfig0.wdt_stg3 = WDT_STAGE_ACTION_OFF;
        break;
    default:
        break;
    }
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_set_cpu_reset_length(timg_dev_t *hw, wdt_reset_sig_length_t length)
{
    hw->wdtconfig0.wdt_cpu_reset_length = length;
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_set_sys_reset_length(timg_dev_t *hw, wdt_reset_sig_length_t length)
{
    hw->wdtconfig0.wdt_sys_reset_length = length;
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_set_flashboot_en(timg_dev_t *hw, bool enable)
{
    hw->wdtconfig0.wdt_flashboot_mod_en = (enable) ? 1 : 0;
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_set_prescaler(timg_dev_t *hw, uint32_t prescaler)
{
    /* Read-modify-write by hand so the compiler cannot narrow the store to an
       8 or 16 bit instruction, which a peripheral register will not accept. */
    HAL_FORCE_MODIFY_U32_REG_FIELD(hw->wdtconfig1, wdt_clk_prescale, prescaler);
    hw->wdtconfig0.wdt_conf_update_en = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_feed(timg_dev_t *hw)
{
    hw->wdtfeed.wdt_feed = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_write_protect_enable(timg_dev_t *hw)
{
    hw->wdtwprotect.wdt_wkey = 0;
}

FORCE_INLINE_ATTR void mwdt_ll_write_protect_disable(timg_dev_t *hw)
{
    hw->wdtwprotect.wdt_wkey = TIMG_WDT_WKEY_VALUE;
}

FORCE_INLINE_ATTR void mwdt_ll_clear_intr_status(timg_dev_t *hw)
{
    hw->int_clr_timers.wdt_int_clr = 1;
}

FORCE_INLINE_ATTR void mwdt_ll_set_intr_enable(timg_dev_t *hw, bool enable)
{
    hw->int_ena_timers.wdt_int_ena = (enable) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif
