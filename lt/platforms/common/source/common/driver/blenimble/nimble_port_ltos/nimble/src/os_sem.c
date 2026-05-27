/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_sem.c
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
#include "nimble/nimble_npl.h"
#include "osif.h"

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (sem == NULL)
    {
        return BLE_NPL_INVALID_PARAM;
    }

    if (osif_sem_create(&(sem->pHandle), (uint32_t)tokens, 1) == false)
    {
        return BLE_NPL_ERROR;
    }
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_deinit(struct ble_npl_sem *sem)
{
    if (sem == NULL)
    {
        return BLE_NPL_INVALID_PARAM;
    }

    if (osif_sem_delete(sem->pHandle))
    {
        return BLE_NPL_ERROR;
    }
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem)
{
    if (sem == NULL)
    {
        return BLE_NPL_INVALID_PARAM;
    }
    if (osif_sem_give(sem->pHandle) == false)
    {
        return BLE_NPL_ERROR;
    }
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, uint32_t timeout)
{
    if (sem == NULL)
    {
        return BLE_NPL_INVALID_PARAM;
    }

    if (osif_sem_take(sem->pHandle, timeout) == false)
    {
        return BLE_NPL_TIMEOUT;
    }
    return BLE_NPL_OK;
}

uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    return osif_sem_getvalue(sem->pHandle);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
