/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_mutex.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <stdint.h>

#include "nimble/nimble_npl.h"
#include "osif.h"

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    if (mu == NULL)                                     return BLE_NPL_ERROR;
    else if (osif_mutex_create(&(mu->pHandle)) == true) return BLE_NPL_OK;
    else                                                return BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_mutex_deinit(struct ble_npl_mutex *mu)
{
    if (mu == NULL)                                  return BLE_NPL_ERROR;
    else if (osif_mutex_delete(mu->pHandle) == true) return BLE_NPL_OK;
    else                                             return BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    if (mu == NULL)                                  return BLE_NPL_ERROR;
    else if (osif_mutex_give(mu->pHandle) == true)   return BLE_NPL_OK;
    else                                             return BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout)
{
    if (mu == NULL)                                         return BLE_NPL_ERROR;
    else if (osif_mutex_take(mu->pHandle, timeout) == true) return BLE_NPL_OK;
    else                                                    return BLE_NPL_ERROR;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 *  20-Sep-22   vespasian   Added NULL pointer checks
 */
