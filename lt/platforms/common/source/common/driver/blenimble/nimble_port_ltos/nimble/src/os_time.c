/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_time.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>
#include "nimble/nimble_npl.h"

ble_npl_time_t ble_npl_time_get(void)
{
    return (ble_npl_time_t)LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime());
}


ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks)
{
    *out_ticks = ms;

    return BLE_NPL_OK;
}


ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms)
{
    *out_ms = ticks;

    return BLE_NPL_OK;
}

ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms)
{
    return ms;
}

uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks)
{
    return ticks;
}

void ble_npl_time_delay(ble_npl_time_t ticks)
{
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds((u32)ticks));
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
