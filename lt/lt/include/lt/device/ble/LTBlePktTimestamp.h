/*******************************************************************************
 * lt/device/ble/LTBlePktTimestamp.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * BLE Packet Timestamp structure and callback type shared between controller
 * and host layers.  Platforms that support packet-level timestamping (e.g.
 * Blueridge) populate an LTBlePktTimestamp at TX/RX time; platforms
 * that do not simply leave the Register/Unregister function pointers NULL.
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_BLE_LTBLEPKTTIMESTAMP_H
#define LT_INCLUDE_LT_DEVICE_BLE_LTBLEPKTTIMESTAMP_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#define LT_BLE_PKT_TIMESTAMP_VERSION 1

typedef struct LTBlePktTimestamp {
    u8  version;     /**< Structure version (LT_BLE_PKT_TIMESTAMP_VERSION) */
    u8  direction;   /**< 0 = TX, 1 = RX */
    u16 conn_handle; /**< BLE connection handle */
    u64 timestamp;   /**< Microseconds, controller clock */
    u8  tag[4];      /**< Matched bytes copied from packet at tag_offset */
    u8  tag_len;     /**< Number of valid bytes in tag[] */
} LTBlePktTimestamp;

typedef void (*LTBlePktTimestampCb)(const LTBlePktTimestamp *info, void *userData);

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_BLE_LTBLEPKTTIMESTAMP_H */
