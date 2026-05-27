/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_atomic.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>

#define enter_critical_section LT_GetCore()->Disable
#define leave_critical_section LT_GetCore()->Enable

static u32 flags = 0;
u32 ble_npl_hw_enter_critical(void)
{
    flags = enter_critical_section();
    return flags;
}

void ble_npl_hw_exit_critical(u32 ctx)
{
    LT_UNUSED(ctx);
    leave_critical_section(flags);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
