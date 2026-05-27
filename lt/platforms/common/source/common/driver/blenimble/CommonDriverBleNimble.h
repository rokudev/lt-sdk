/*******************************************************************************
 * platforms/common/source/common/driver/blenimble/CommonDriverBleNimble.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_BLENIMBLE_COMMONDRIVERBLENIMBLE_H
#define PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_BLENIMBLE_COMMONDRIVERBLENIMBLE_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include <lt/device/ble/LTDeviceBle.h>

#define _VA_LIST_DEFINED
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_hci.h"

#define CONFIG_BT_NIMBLE_ACL_BUF_SIZE 255
#define CONFIG_BT_NIMBLE_HCI_EVT_BUF_SIZE 70
#include "host/ble_hs.h"
#include "host/util/util.h"

#define BLE_HCI_TRANS_BUF_EVT_NON_DISCARDABLE    0
#define BLE_HCI_TRANS_BUF_EVT_DISCARDABLE         1

typedef int (*ble_hci_trans_rx_cmd_fn)(void *buf);
typedef int (*ble_hci_trans_rx_acl_fn)(struct os_mbuf *om);


void ble_store_config_init(void);
int  nimble_port_stop(void);
void nimble_port_deinit(void);

LT_EXTERN_C_END
#endif /* PLATFORMS_COMMON_SOURCE_COMMON_DRIVER_BLENIMBLE_COMMONDRIVERBLENIMBLE_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Aug-22   vespasian   created
 *  21-Dec-22   gallienus   restructured and cleaned
 */
