/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include "lt/LTTypes.h"
/* Upstream includes soc/lp_analog_peri_reg.h here; both functions below are
   empty on this chip (IDF-7514), so nothing in it is referenced. */

void bootloader_ana_super_wdt_reset_config(bool enable)
{
    //TODO: IDF-7514
    LT_UNUSED(enable);
}

void bootloader_ana_clock_glitch_reset_config(bool enable)
{
    //TODO: IDF-7514
    LT_UNUSED(enable);
}
