/*******************************************************************************
 * lt/device/ble/LTBleConnectionInfo.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * BLE Connection Info structure shared between controller and host layers.
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_BLE_LTBLECONNECTIONINFO_H
#define LT_INCLUDE_LT_DEVICE_BLE_LTBLECONNECTIONINFO_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/**
 * @brief Current version of LTBleConnectionInfo structure.
 *        Increment when adding new fields to maintain backward compatibility.
 */
#define LT_BLE_CONNECTION_INFO_VERSION 1

/**
 * @brief BLE connection statistics and information.
 *
 * This structure contains BLE connection statistics that can be retrieved
 * from the platform-specific BLE controller. The version field allows for
 * future extensibility without breaking backward compatibility.
 */
typedef struct LTBleConnectionInfo {
    u8  version;        /**< Structure version (LT_BLE_CONNECTION_INFO_VERSION) */
    u32 txCount;        /**< Number of ACL TX packets transmitted */
    u32 txMaxUs;        /**< Maximum TX time in microseconds */
    u64 txTotalUs;      /**< Total TX time in microseconds */
    u64 txAverageUs;    /**< Average TX time in microseconds */
} LTBleConnectionInfo;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_BLE_LTBLECONNECTIONINFO_H */
