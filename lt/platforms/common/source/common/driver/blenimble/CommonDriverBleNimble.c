/****************************************************************************
 * platforms/common/source/common/driver/blenimble/CommonDriverBleNimble.c
 *
 * Copyright 2019 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * Modified by Roku, Inc. Please see changelog below for more information.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ***************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/ble/LTDeviceBle.h>
#include <lt/device/blecontroller/LTDeviceBleController.h>
#include "CommonDriverBleNimble.h"
#include "osif.h"

DEFINE_LTLOG_SECTION("com.drv.ble");
#if DEBUG_LOG
#define P(...)  if (S.logging_en) LTLOG(__VA_ARGS__)
#else
#define P(...)
#endif
#define PLOG(...) LTLOG(__VA_ARGS__)

// Parameters initialized and cleaned on library open and close.
typedef struct NimblePortInitCtx {
    LTBleDeviceHandle   hDevice;
    int                 ble_host_priority;
    LTThread            bleHostThr;
} NimblePortInitCtx;
static struct Statics {
    LTCore                *core;
    LTDeviceBleController *devController;
    LTDeviceUnit           hDevController;
    ILTBleController      *controller;
    ILTThread             *thread;
    ILTEvent              *event;

    bool                  logging_en;
    NimblePortInitCtx     nim_ctx;
    LTUtilityMacAddress  *mac_lib;
} S;

// Parameters used by BLE host, but not matter if initialized.
static u16                    s_mtu;
static LTAtomic               s_state;
static u8                     s_ownAddrType;
static LTMacAddress           s_ownMacAddr;
static LTMacAddress           s_peerMacAddr;

static int GattChrCallback(u16 conn_handle, u16 attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// Single service. The end of s_services must be {0}.
static struct ble_gatt_svc_def* services_ptr = NULL;
static const u8 s_num_services = 2;

static ble_hci_trans_rx_cmd_fn ble_hci_rx_cmd_hs_cb = NULL;
static ble_hci_trans_rx_acl_fn ble_hci_rx_acl_hs_cb = NULL;

// copy MAC address and convert from host (LSB) to network order (MSB)
static void CopyMacAddressH2N(u8 *d, u8 *s) {
    s += 5;
    for (int i = 0; i < 6; ++i, ++d, --s)
        *d = *s;
}

/** In blehost or app thread
 * Transport adaptor functions
 * 
 * These functions are called by NimBLE (with ble_hs_lock held) to send data
 * to the controller. Its is expected that S.controller->Send is thread-safe function. 
 * DO NOT use mutex to protect controller access, to avoid AB-BA deadlock:
 * - External APIs: mutex → ble_hs_lock
 * - Transport funcs: ble_hs_lock → Send (safe!)
 */
int ble_transport_to_ll_cmd_impl(void *vcmd) {
    int rc = BLE_HS_ETIMEOUT_HCI;
    if (!vcmd) return 0;
    u8  *cmd    = (u8 *)vcmd;
    u16  cmdLen = BLE_HCI_CMD_HDR_LEN + cmd[2] + 1;
    u8  *llCmd  = (u8 *)lt_malloc(cmdLen);
    if (!llCmd) {
        rc = BLE_HS_ENOMEM;
        goto error;
    }

    // Add starting command byte that was missing
    lt_memcpy(&llCmd[1], cmd, cmdLen - 1);
    llCmd[0] = H4_TYPE_COMMAND;

    LTTime start_time = S.core->GetKernelTime();
    rc = S.controller->Send(llCmd, cmdLen) ? 0 : BLE_HS_ECONTROLLER;
    s64 elapsed_time = LTTime_GetMilliseconds(LTTime_Subtract(S.core->GetKernelTime(), start_time));
    if (elapsed_time >= 2000) {
        LTLOG_YELLOWALERT("cmd.tx", "timeout %llx", LT_Ps64(elapsed_time));
        rc = BLE_HS_ETIMEOUT_HCI;
    }
    lt_free(llCmd);
error:
    ble_transport_free(cmd);
    if (rc) LTLOG_YELLOWALERT("cmd.tx", "error %d", rc);
    return rc;
}

// In blehost or app thread
int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
    if (!om) return 0;
    /// If this packet is zero length, just free it
    if (OS_MBUF_PKTLEN(om) == 0) {
        os_mbuf_free_chain(om);
        return 0;
    }

    // u8 data[MYNEWT_VAL(BLE_ACL_BUF_SIZE) + 1];
    u8 *data = lt_malloc(MYNEWT_VAL(BLE_ACL_BUF_SIZE) + 1);
    if (!data) return BLE_HS_ENOMEM;

    data[0] = H4_TYPE_ACL;
    u16 len = 0;
    len++;

    os_mbuf_copydata(om, 0, OS_MBUF_PKTLEN(om), &data[1]);
    len += OS_MBUF_PKTLEN(om);

    int rc = BLE_HS_ETIMEOUT_HCI;
    LTTime start_time = S.core->GetKernelTime();
    rc = S.controller->Send(data, len) ? 0 : BLE_HS_ECONTROLLER;
    s64 elapsed_time = LTTime_GetMilliseconds(LTTime_Subtract(S.core->GetKernelTime(), start_time));
    if (elapsed_time >= 2000) {
        LTLOG_YELLOWALERT("acl.tx", "timeout %llx", LT_Ps64(elapsed_time));
        rc = BLE_HS_ETIMEOUT_HCI;
    }

    os_mbuf_free_chain(om);
    lt_free(data);
    if (rc) LTLOG_YELLOWALERT("acl.tx", "error %d", rc);
    return rc;
}

static void ble_hci_trans_cfg_hs(ble_hci_trans_rx_cmd_fn cmd_cb, ble_hci_trans_rx_acl_fn acl_cb) {
    ble_hci_rx_cmd_hs_cb  = cmd_cb;
    ble_hci_rx_acl_hs_cb  = acl_cb;
}

// In ble controller thread
static int ble_hci_trans_ll_evt_tx(u8 *pHciEvt) {
    int rc = -1;
    if (ble_hci_rx_cmd_hs_cb) {
        // ble_hci_rx_cmd_hs_cb doesn't need locks because:
        // It only queues events without modifying any shared state
        // Event processing is purely asynchronous with no immediate side effects
        rc = ble_hci_rx_cmd_hs_cb((void *)pHciEvt);
    }
    return rc;
}

// In ble controller thread
static void ble_hci_rx_acl(const u8 *data, u16 len) {
    LT_ASSERT(data);

    struct os_mbuf *mbuf;
    int rc;

    if (len < BLE_HCI_DATA_HDR_SZ || len > MYNEWT_VAL(BLE_ACL_BUF_SIZE)) {
        return;
    }

#if MYNEWT_VAL(BLE_HS_FLOW_CTRL)
    mbuf = ble_transport_alloc_acl_from_ll();
#else
    mbuf = ble_transport_alloc_acl_from_hs();
#endif

    if (!mbuf) {
        LTLOG("mbuf.alloc", "%s failed to allocate ACL buffers; increase ACL_BUF_COUNT", __func__);
        return;
    }

    if ((rc = os_mbuf_append(mbuf, data, len)) != 0) {
        LTLOG("mbuf.append", "%s failed to os_mbuf_append; rc = %d", __func__, rc);
        os_mbuf_free_chain(mbuf);
        return;
    }

    if (ble_hci_rx_acl_hs_cb) {
        // No lock needed here as per original design - NimBLE handles internal synchronization
        // for ACL data reception and this is called from controller context
        ble_hci_rx_acl_hs_cb(mbuf);
    }
}

static void controller_send_ready(void) {}

static bool CommonDriverBle_ResetController(s16 reason) {
    if (reason == 0) {
        reason = BLE_HS_ECONTROLLER; // Default reset reason
    }
    // Attempt to reset the BLE controller using NimBLE's scheduler reset.
    P(__FUNCTION__, "Resetting BLE stack via ble_hs_sched_reset");
    ble_hs_sched_reset(reason);
    return true;
}

static int16_t CommonDriverBle_GetControllerResetReason(void) {
    // Get the reset reason from NimBLE's scheduler (ble_hs.c).
    return ble_hs_controller_get_reset_reason();
}

static int host_recv_ready(const u8 *data, u16 len) {
    LT_ASSERT(data);

    if (data[0] == H4_TYPE_EVENT) {
        u8 *evbuf;
        int totlen;

        totlen = BLE_HCI_EVENT_HDR_LEN + data[2];
        LT_ASSERT(totlen <= UINT8_MAX + BLE_HCI_EVENT_HDR_LEN);

        if (totlen > MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE)) {
            PLOG("host.rcv", "Received HCI data length at host (%d) exceeds maximum configured HCI event buffer size (%d).",
                     totlen, MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE));
            ble_hs_sched_reset(BLE_HS_ECONTROLLER);
            return 0;
        }

        if (data[1] == BLE_HCI_EVCODE_HW_ERROR) {
            LT_ASSERT(0);
        }

        /// Allocate LE Advertising Report Event from lo pool only
        if ((data[1] == BLE_HCI_EVCODE_LE_META) && (data[3] == BLE_HCI_LE_SUBEV_ADV_RPT)) {
            evbuf = ble_transport_alloc_evt(BLE_HCI_TRANS_BUF_EVT_DISCARDABLE);
            // Skip advertising report if we're out of memory
            if (!evbuf) {
                return 0;
            }
        } else {
            evbuf = ble_transport_alloc_evt(BLE_HCI_TRANS_BUF_EVT_NON_DISCARDABLE);
            LT_ASSERT(evbuf != NULL);
        }

        lt_memcpy(evbuf, &data[1], totlen);

        ble_hci_trans_ll_evt_tx(evbuf);
    } else if (data[0] == H4_TYPE_ACL) {
        ble_hci_rx_acl(data + 1, len - 1);
    }
    return 0;
}

static const LTBleController_VhciHostCallback VhciHostCallback = {
    .NotifySendReady = &controller_send_ready,
    .NotifyRecvReady = &host_recv_ready,
};

/** Run in ble host thread ****************************************************/

// In blehost thread
static int GattChrCallback(u16 conn_handle, u16 attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    P(__FUNCTION__, "th 0x%lx ch 0x%04X ah 0x%04X", LT_PLT_HANDLE(S.thread->GetCurrentThread()), conn_handle, attr_handle);
    LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(VOIDPTR_TO_LTHANDLE(arg));
    if (!chrCtx || !chrCtx->hChr) return BLE_ATT_ERR_UNLIKELY;

    int ret = BLE_ATT_ERR_UNLIKELY;
    const struct ble_gatt_chr_def *chr = ctxt->chr;
    u16 len;
    P("cc.u", "0x%x vh %p op %d", BLE_UUID16(chr->uuid)->value, chr->val_handle, ctxt->op);
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            /* Peer device reads data from this device.
             * 1. Sanity check data.
             * 2. Execute read call back if present.
             * 3. Output data from readBuf to om.
             * 4. Notify Read_Done event.
             *    The Read_Done doesn't guarantee the buffer will be completely processed before another READ_CHR event occurs
             */
            chr = ctxt->chr;
            if ((chr->flags & BLE_GATT_CHR_F_READ) == 0) {
                ret = BLE_ATT_ERR_READ_NOT_PERMITTED;
                break;
            }
            // Read call back to prepare data in readBuf and readLen.
            if (chrCtx->readCb) {
                if (!chrCtx->readCb(chrCtx->hChr)) {
                    ret = BLE_ATT_ERR_UNLIKELY;
                    break;
                }
            }
            // Append data from read buffer to om.
            ret = os_mbuf_append(ctxt->om, chrCtx->readBuf, chrCtx->readLen);
            P("cc.r", "%d len %u", ret, chrCtx->readLen);
            S.event->NotifyEvent(chrCtx->hEvent, chrCtx->hChr, kLTDeviceBle_Event_Read_Done);
            break;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            /* Peer device writes data to this device.
             * 1. Sanity check data.
             * 2. Take data from om to writeBuf.
             * 3. Execute write call back if present.
             * 4. Notify Write_Done event.
             *    The Write_Done doesn't guarantee the buffer will be completely processed before another WRITE_CHR event occurs
             */
            chr = ctxt->chr;
            if ((chr->flags & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)) == 0) {
                PLOG("cc.w.fail", "write not permitted, allowed permission flag 0x%X", chr->flags);
                ret = BLE_ATT_ERR_WRITE_NOT_PERMITTED;
                break;
            }
            len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > chrCtx->writeBufSize) {
                PLOG("cc.w.fail", "incoming data len %d > buffer len %d", len, chrCtx->writeBufSize);
                ret = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                break;
            }
            // Get data from om to writeBuf and writeLen.
            if (0 != ble_hs_mbuf_to_flat(ctxt->om, chrCtx->writeBuf, chrCtx->writeBufSize, &len)) {
                ret = BLE_ATT_ERR_UNLIKELY;
                break;
            }
            chrCtx->writeLen = len;
            // Write call back to process data in writeBuf.
            if (chrCtx->writeCb) {
                if (!chrCtx->writeCb(chrCtx->hChr)) {
                    ret = BLE_ATT_ERR_UNLIKELY;
                    break;
                }
            }
            S.event->NotifyEvent(chrCtx->hEvent, chrCtx->hChr, kLTDeviceBle_Event_Write_Done);
            P("cc.w", "len %lu", LT_Pu32(len));
            ret = 0;
            break;

        case BLE_GATT_ACCESS_OP_READ_DSC:
        case BLE_GATT_ACCESS_OP_WRITE_DSC:
        default:
            P("cc.o", "%d", ctxt->op);
            ret = BLE_ATT_ERR_UNLIKELY;
    }

    S.core->ReleaseHandlePrivateData(VOIDPTR_TO_LTHANDLE(arg), chrCtx);
    return ret;
}

/**Copy Alert**/
// This function is copy from nimble stack.
// Open to a suggestion which can save this copy or
// any other better way to handle this.
static int
ble_uuid_to_any(const ble_uuid_t *uuid, ble_uuid_any_t *uuid_any)
{
    uuid_any->u.type = uuid->type;

    switch (uuid->type) {
    case BLE_UUID_TYPE_16:
        uuid_any->u16.value = BLE_UUID16(uuid)->value;
        break;

    case BLE_UUID_TYPE_32:
        uuid_any->u32.value = BLE_UUID32(uuid)->value;
        break;

    case BLE_UUID_TYPE_128:
        memcpy(uuid_any->u128.value, BLE_UUID128(uuid)->value, 16);
        break;
    default:
        return BLE_HS_EINVAL;
    }

    return 0;
}

static bool CompareUuid(const LTBleUuidAny* uuid1, const LTBleUuidAny* uuid2) {
    if (uuid1->type != uuid2->type) return false;
    if (uuid1->type == kLTBleUuidType_16) {
        return uuid1->value.uuid16.value == uuid2->value.uuid16.value;
    } else if (uuid1->type == kLTBleUuidType_128) {
        return lt_memcmp(uuid1->value.uuid128.value, uuid2->value.uuid128.value, LT_UUID_BYTE_LEN) == 0;
    }
    return false;
}

static LTBleUuidAny GetUuid(const ble_uuid_any_t *uuid) {
    LTBleUuidAny u;
    if (uuid->u.type == BLE_UUID_TYPE_16) {
        u.type = kLTBleUuidType_16;
        u.value.uuid16.value = uuid->u16.value;
    } else if (uuid->u.type == BLE_UUID_TYPE_128) {
        u.type = kLTBleUuidType_128;
        lt_memcpy(u.value.uuid128.value, uuid->u128.value, LT_UUID_BYTE_LEN);
    } else {
        u.type = kLTBleUuidType_Unknown;
        P("uuid.unknown.type", "unknown type %d", uuid->u.type);
    }
    return u;
}

bool checkAndUpdateAttrHandle(struct ble_gatt_register_ctxt *ctxt, LTBleDeviceCtx *dvcCtx)
{
    // Get the Parent Service from the ble_gatt_register_ctxt
    ble_uuid_any_t nim_uuid;
    if (ble_uuid_to_any(ctxt->chr.svc_def->uuid, &nim_uuid))
    {
        P("gr.2", "Failed to convert service uuid (type %d) to any", ctxt->chr.svc_def->uuid->type);
        return false;
    }
    LTBleUuidAny service_uuid = GetUuid(&nim_uuid);
    // Then see which service matches in the device handle
    bool found = false;
    for (int svcIdx = 0; (svcIdx < dvcCtx->numServices && !found); svcIdx++)
    {
        LTBleSvcCtx *svcCtx = S.core->ReserveHandlePrivateData(dvcCtx->svc_hdl[svcIdx]);
        P("gr.2.0", "service_uuid.type %d svcCtx->uuid %d", service_uuid.type, svcCtx->uuid->type);
        if (CompareUuid(&service_uuid, svcCtx->uuid))
        {
            // We got our Service Context, now look for character context
            P("gr.2.1", "Found our service context 0x%04lx", LT_Pu32(dvcCtx->svc_hdl[svcIdx]));
            for (int charIdx = 0; (charIdx < svcCtx->numCharacteristics && !found); charIdx++)
            {
                LTBleChrHandle hChr = svcCtx->chr_hdl[charIdx];
                LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
                if (ble_uuid_to_any(ctxt->chr.chr_def->uuid, &nim_uuid) == 0)
                {
                    LTBleUuidAny chr_uuid = GetUuid(&nim_uuid);
                    if (CompareUuid(&chr_uuid, chrCtx->uuid))
                    {
                        found = true;
                        chrCtx->attrHandle = ctxt->chr.val_handle;
                        P("gr.2.1", "Found our character attribute handle 0x%04lx", LT_Pu32(chrCtx->attrHandle));
                    }
                }
                else
                {
                    P("gr.2.2", "Failed to convert characteristic uuid");
                }
                S.core->ReleaseHandlePrivateData(hChr, chrCtx);
            }
        }
        S.core->ReleaseHandlePrivateData(dvcCtx->svc_hdl[svcIdx], svcCtx);
    }
    return true;
}

// In blehost thread
static void GattRegisterCallback(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    LTBleDeviceHandle hDev = VOIDPTR_TO_LTHANDLE(arg);
    LTBleDeviceCtx *dvcCtx = S.core->ReserveHandlePrivateData(hDev);
    if (!dvcCtx || dvcCtx->hDev != hDev) {
        P("gatt.reg.cb", "null device context or incorrect device handle");
        return;
    }

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            if (ctxt->svc.svc_def->uuid->type == BLE_UUID_TYPE_16) {
                P("gr.1", "serv.uuid 0x%04X svc.hdl 0x%04X\n", BLE_UUID16(ctxt->svc.svc_def->uuid)->value, ctxt->svc.handle);
            } else if (ctxt->svc.svc_def->uuid->type == BLE_UUID_TYPE_128) {
                // only show the first 4 and last 4 bytes of service uuid
                u8 *v = BLE_UUID128(ctxt->svc.svc_def->uuid)->value;
                LT_UNUSED(v);
                P("gr.1", "serv.uuid %02X%02X%02X%02X...%02X%02X%02X%02X svc.hdl 0x%04X", v[0], v[1], v[2], v[3], v[12], v[13], v[14], v[15], ctxt->svc.handle);
            }
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            if (ctxt->chr.chr_def->uuid->type == BLE_UUID_TYPE_16) {
                P("gr.2", "ch.uuid 0x%04X dh 0x%04X vh 0x%04X\n", BLE_UUID16(ctxt->chr.chr_def->uuid)->value, ctxt->chr.def_handle, ctxt->chr.val_handle);
            } else if (ctxt->chr.chr_def->uuid->type == BLE_UUID_TYPE_128) {
                // only show the first 4 bytes and last 4  of char uuid
                u8 *v = BLE_UUID128(ctxt->chr.chr_def->uuid)->value;
                LT_UNUSED(v);
                P("gr.2", "chr.uuid %02X%02X%02X%02X...%02X%02X%02X%02X dh 0x%04X vh 0x%04X",
                     v[0], v[1], v[2], v[3], v[12], v[13], v[14], v[15], ctxt->chr.def_handle, ctxt->chr.val_handle);
            }
            checkAndUpdateAttrHandle(ctxt, dvcCtx);
            break;
        case BLE_GATT_REGISTER_OP_DSC:
            if (ctxt->chr.chr_def->uuid->type == BLE_UUID_TYPE_16) {
                P("gr.3", "desc 0x%04X dh 0x%04X\n", BLE_UUID16(ctxt->dsc.dsc_def->uuid)->value, ctxt->dsc.handle);
            } else if (ctxt->chr.chr_def->uuid->type == BLE_UUID_TYPE_128) {
                // only show the first 4 bytes and last 4  of desc uuid
                u8 *v = BLE_UUID128(ctxt->dsc.dsc_def->uuid)->value;
                LT_UNUSED(v);
                P("gr.3", "desc %02X%02X%02X%02X...%02X%02X%02X%02X dh 0x%04X", v[0], v[1], v[2], v[3], v[12], v[13], v[14], v[15], ctxt->dsc.handle);
            }
            break;

        default:
            P("gr.4", "how?!");
            break;
        }
        S.core->ReleaseHandlePrivateData(dvcCtx->hDev, dvcCtx);
}

// GAPEventCallback can be called from any thread, so we need to queue it
// to the BLE host thread for processing. This is done by posting a task
// to the BLE host thread with the event data. The task will then call the
// appropriate handler for the event type, ensuring that all BLE host
// operations are performed in the correct thread context.

// Structure to hold GAP event data for thread-safe queuing
typedef struct {
    LTBleDeviceHandle device_handle;
    struct ble_gap_event gap_event;  // Copy of the entire event structure
    void (*async_handler)(struct ble_gap_event *event, LTBleDeviceHandle hDevice);
} GapEventData;

// Release function for GapEventData when queued task completes
static void ReleaseGapEventData(LTThread_ReleaseReason releaseReason, void *pClientData) {
    if (pClientData) {
        lt_free(pClientData);
    }
}

// All handlers in blehost thread
// Specific event handlers - each handles one event type completely
static void HandleConnectEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }

    struct ble_gap_conn_desc *desc = lt_malloc(sizeof(struct ble_gap_conn_desc));
    if (desc) {
        P("ge.0", "connect 0x%x ch 0x%02x", event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) {
            if (0 == ble_gap_conn_find(event->connect.conn_handle, desc)) {
                devCtx->connHandle = event->connect.conn_handle;
                LTAtomic_Store(&s_state, kLTDeviceBle_State_Connected);
                s_mtu = ble_att_mtu(event->connect.conn_handle);
                P("MTU", "%u", s_mtu);
                lt_memcpy(s_peerMacAddr.octet, desc->peer_ota_addr.val, sizeof(desc->peer_ota_addr.val));
                devCtx->remoteMac = s_peerMacAddr;
                P("rmt",
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  s_peerMacAddr.octet[5],
                  s_peerMacAddr.octet[4],
                  s_peerMacAddr.octet[3],
                  s_peerMacAddr.octet[2],
                  s_peerMacAddr.octet[1],
                  s_peerMacAddr.octet[0]);
                P("conn.params",
                  "itvl %.2fms, latency %d, timeout %dms",
                  desc->conn_itvl * 1.25,
                  desc->conn_latency,
                  desc->supervision_timeout * 10);
                devCtx->connInterval        = desc->conn_itvl; // need to convert to ms by * 1.25
                devCtx->latency             = desc->conn_latency;
                devCtx->supervision_timeout = desc->supervision_timeout;

                S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Connected, NULL);
            }
        } else {
            // Connection failed
            LTAtomic_Store(&s_state, kLTDeviceBle_State_Started);
            S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Disconnected, NULL);
            devCtx->connHandle = 0;
        }
        lt_free(desc);
    } else {
        LTLOG_YELLOWALERT("ge.0.fail", "failed to allocate memory for ble_gap_conn_desc");
    }
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleDisconnectEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }
    
    PLOG("ge.1", "disconn 0x%x ch 0x%02x", event->disconnect.reason, event->disconnect.conn.conn_handle);
    LTAtomic_Store(&s_state, kLTDeviceBle_State_Started);
    S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Disconnected, NULL);
    devCtx->connHandle = 0;
    
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleConnUpdateEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }

    struct ble_gap_conn_desc *desc = lt_malloc(sizeof(struct ble_gap_conn_desc));
    if (desc) {
        PLOG("ge.3", "conn upd status 0x%x ch 0x%02x", event->conn_update.status, event->conn_update.conn_handle);
        if (event->conn_update.status != 0) {
            LTLOG_YELLOWALERT("conn.update.failed", "reason code 0x%x", event->conn_update.status);
            S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Conn_Update_Failed, NULL);
        } else if (0 == ble_gap_conn_find(event->connect.conn_handle, desc)) {
            PLOG("conn.update.success",
                 "conn handle 0x%02x, itvl %.2fms, latency %d, timeout %dms",
                 event->conn_update.conn_handle,
                 desc->conn_itvl * 1.25,
                 desc->conn_latency,
                 desc->supervision_timeout * 10);
            devCtx->connInterval        = desc->conn_itvl; // need to convert to ms by * 1.25
            devCtx->latency             = desc->conn_latency;
            devCtx->supervision_timeout = desc->supervision_timeout;
            S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Conn_Update_Complete, NULL);
        }
        lt_free(desc);
    } else {
        LTLOG_YELLOWALERT("ge.3.fail", "failed to allocate memory for ble_gap_conn_desc");
    }
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleAdvCompleteEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }
    
    P("ge.9", "advcmpl 0x%x", event->adv_complete.reason);
    // no connection yet
    LTAtomic_CompareAndExchange(&s_state, kLTDeviceBle_State_ConnectedAdvertising, kLTDeviceBle_State_Connected);
    // Atomically change to Started if not Connected

    u32 currentState;
    do {
        currentState = LTAtomic_Load(&s_state);
        if (currentState == kLTDeviceBle_State_Connected) {
            break;
        }
    } while (!LTAtomic_CompareAndExchange(&s_state, currentState, kLTDeviceBle_State_Started));
    
    if (LTAtomic_Load(&s_state) != kLTDeviceBle_State_Connected) {
        devCtx->connHandle = 0;
        S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Advertise_Complete, NULL);
    }
    
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleEncChangeEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }

    PLOG("ge.10", "status 0x%x ch 0x%02x", event->enc_change.status, event->enc_change.conn_handle);
    LTBleEventData *bleEvtData = lt_malloc(sizeof(LTBleEventData));
    if (bleEvtData) {
        lt_memset(bleEvtData, 0, sizeof(LTBleEventData));
        bleEvtData->pair.pairing_status = (event->enc_change.status == 0) ? true : false;
        S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Paired, bleEvtData);
    } else {
        LTLOG_YELLOWALERT("ge.10.fail", "failed to allocate memory for bleEvtData");
    }

    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleSubscribeEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }
    
    P("ge.14", "subscribe reason 0x%x ch 0x%02x ah 0x%02x p_notify %d c_notify %d p_indicate %d c_indicate %d",
                event->subscribe.reason, event->subscribe.conn_handle, event->subscribe.attr_handle,
                event->subscribe.prev_notify, event->subscribe.cur_notify,
                event->subscribe.prev_indicate, event->subscribe.cur_indicate);

    bool found = false;
    // Check if peer is subscribing to one of the registered characteristics
    LTBleChrHandle hChr = 0;
    for (int svcIdx = 0; (svcIdx < devCtx->numServices && !found); svcIdx++) {
        LTBleSvcCtx *svcCtx = S.core->ReserveHandlePrivateData(devCtx->svc_hdl[svcIdx]);
        for (int charIdx = 0; (charIdx < svcCtx->numCharacteristics && !found); charIdx++) {
            hChr = svcCtx->chr_hdl[charIdx];
            LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
            if (chrCtx->attrHandle == event->subscribe.attr_handle) {
                found = true;
                P("gr.14.1", "Found matching attribute handle 0x%04X", chrCtx->attrHandle);
            }
            S.core->ReleaseHandlePrivateData(hChr, chrCtx);
        }
        S.core->ReleaseHandlePrivateData(devCtx->svc_hdl[svcIdx], svcCtx);
    }

    // Notify application
    if (found) {
        LTBleEventData *bleEvtData = lt_malloc(sizeof(LTBleEventData));
        P("debug", "bleEvtData = %p", bleEvtData);
        if (bleEvtData) {
            lt_memset(bleEvtData, 0, sizeof(LTBleEventData));
            bleEvtData->subscribe.cur_notify   = event->subscribe.cur_notify;
            bleEvtData->subscribe.cur_indicate = event->subscribe.cur_indicate;
            bleEvtData->subscribe.hChr         = hChr;
            S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Subscribe, bleEvtData);
        } else {
            LTLOG_YELLOWALERT("ge.14.fail", "failed to allocate memory for bleEvtData");
        }
    }

    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

static void HandleMtuEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    s_mtu = ble_att_mtu(event->mtu.conn_handle);
    P("ge.15", "mtu %d/%d ch 0x%02x", event->mtu.value, s_mtu, event->mtu.conn_handle);
}

#if 0
//keeping for debugging purposes
static void HandleNotifyTxEvent(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    P("ge.13", "notifytx %d ch 0x%02x ah 0x%02x", event->notify_tx.indication, event->notify_tx.conn_handle, event->notify_tx.attr_handle);
}
#endif

// Synchronous event handlers - these must return immediately with meaningful values
static int HandleRepeatPairing(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    
    struct ble_gap_conn_desc desc;
    int ret = BLE_GAP_REPEAT_PAIRING_IGNORE;
    
    P("ge.17", "repairing ch 0x%02x", event->repeat_pairing.conn_handle);
    if (0 == ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc)) {
        devCtx->connHandle = event->repeat_pairing.conn_handle;
        ble_store_util_delete_peer(&desc.peer_id_addr);
        ret = BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
    return ret;
}

#if 0
// keeping for future
static int HandleConnUpdateReq(struct ble_gap_event *event, LTBleDeviceHandle hDevice) {
    // Handle connection update request synchronously if needed
    // For now, just accept all requests
    return 0;
}
#endif

// Asynchronous event handler function type
typedef void (*AsyncEventHandler)(struct ble_gap_event *event, LTBleDeviceHandle hDevice);

// Modified GapEventCallback - now executes in blehost thread context
static void ExecuteAsyncEventHandler(void *pClientData) {
    GapEventData *gap_data = (GapEventData*)pClientData;
    if (!gap_data) return;
    
    LTBleDeviceHandle hDevice = gap_data->device_handle;
    
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return;
    }

    AsyncEventHandler handler = gap_data->async_handler;
    if (handler) {
        handler(&gap_data->gap_event, gap_data->device_handle);
    }

    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
}

// Helper function to queue specific asynchronous event handlers
static int QueueSpecificAsyncHandler(AsyncEventHandler handler, struct ble_gap_event *event, 
                                    LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return 0;
    }
    
    if (!devCtx->bleHostThr) {
        PLOG("gap.event.cb", "bleHostThread is NULL, can't queue task");
        S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
        return 0;
    }
    
    GapEventData *gap_data = lt_malloc(sizeof(GapEventData));
    if (gap_data) {
        gap_data->device_handle = hDevice;
        gap_data->async_handler = handler;
        lt_memcpy(&gap_data->gap_event, event, sizeof(struct ble_gap_event));

        S.thread->QueueTaskProc(devCtx->bleHostThr, ExecuteAsyncEventHandler, ReleaseGapEventData, gap_data);
    } else {
        LTLOG_YELLOWALERT("gap.event.cb", "failed to allocate memory for GapEventData");
    }
    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
    return 0;
}

// In blehost or app thread
// Main GAP event callback wrapper - single-dispatch pattern
static int GapEventCallbackWrapper(struct ble_gap_event *event, void *arg) {
    LTBleDeviceHandle hDevice = VOIDPTR_TO_LTHANDLE(arg);
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx || !devCtx->hDev) {
        return 0;
    }

    int ret = 0;

    // Single-switch dispatcher routes each event type to appropriate handler
    switch (event->type) {
        // Synchronous events - handled immediately with return values
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            ret = HandleRepeatPairing(event, hDevice);
            break;

#if 0
// keeping for future
        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            ret = HandleConnUpdateReq(event, hDevice);
            break;
#endif

        // Asynchronous events - queued to BLE host thread with specific handlers
        case BLE_GAP_EVENT_CONNECT:
            ret = QueueSpecificAsyncHandler(HandleConnectEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ret = QueueSpecificAsyncHandler(HandleDisconnectEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_CONN_UPDATE:
            ret = QueueSpecificAsyncHandler(HandleConnUpdateEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ret = QueueSpecificAsyncHandler(HandleAdvCompleteEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ret = QueueSpecificAsyncHandler(HandleEncChangeEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ret = QueueSpecificAsyncHandler(HandleSubscribeEvent, event, hDevice);
            break;

        case BLE_GAP_EVENT_MTU:
            ret = QueueSpecificAsyncHandler(HandleMtuEvent, event, hDevice);
            break;
#if 0
// keeping for debugging
        case BLE_GAP_EVENT_NOTIFY_TX:
            ret = QueueSpecificAsyncHandler(HandleNotifyTxEvent, event, hDevice);
            break;
#endif
        // all not-occured and ignored events
        // case BLE_GAP_EVENT_NOTIFY_RX:
        // case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        // case BLE_GAP_EVENT_TERM_FAILURE:
        // case BLE_GAP_EVENT_DISC:
        // case BLE_GAP_EVENT_DISC_COMPLETE:
        // case BLE_GAP_EVENT_PASSKEY_ACTION:
        // case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        // case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        // case BLE_GAP_EVENT_EXT_DISC:
        // case BLE_GAP_EVENT_PERIODIC_SYNC:
        // case BLE_GAP_EVENT_PERIODIC_REPORT:
        // case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        // case BLE_GAP_EVENT_SCAN_REQ_RCVD:
        // case BLE_GAP_EVENT_PERIODIC_TRANSFER:
        // case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
        // case BLE_GAP_EVENT_TRANSMIT_POWER:
        default:
            P("ge.unk", "unhandled event type %d", event->type);
            break;
    }

    S.core->ReleaseHandlePrivateData(devCtx->hDev, devCtx);
    return ret;
}

static void free_nimble_adv_fields(struct ble_hs_adv_fields *nimble_adv) {
    if (!nimble_adv) return;

    if (nimble_adv->uuids16) {
        lt_free((void *)nimble_adv->uuids16);
    }
    if (nimble_adv->uuids128) {
        lt_free((void *)nimble_adv->uuids128);
    }
    lt_free(nimble_adv);
}

static struct ble_hs_adv_fields* get_nimble_adv_fields(const LTBleAdvFields *advFields) {
    if (!advFields) {
        return NULL;
    }

    struct ble_hs_adv_fields *nimble_adv = lt_malloc(sizeof(struct ble_hs_adv_fields));
    if (!nimble_adv) {
        LTLOG_REDALERT("nimble_adv", "failed to allocate memory");
        goto error;
    }
    lt_memset(nimble_adv, 0x0, sizeof(struct ble_hs_adv_fields));
    nimble_adv->flags = 0x0;
    if (advFields->flags & LT_BLE_ADV_F_DISC_LTD) {
        nimble_adv->flags |= BLE_HS_ADV_F_DISC_LTD;
    }
    if (advFields->flags & LT_BLE_ADV_F_DISC_GEN) {
        nimble_adv->flags |= BLE_HS_ADV_F_DISC_GEN;
    }
    if (advFields->flags & LT_BLE_ADV_F_BREDR_UNSUP) {
        nimble_adv->flags |= BLE_HS_ADV_F_BREDR_UNSUP;
    }
    if (advFields->num_uuids16 && advFields->uuids16) {
        ble_uuid16_t *uuids16 = lt_malloc(advFields->num_uuids16 * sizeof(ble_uuid16_t));
        if (!uuids16) {
            LTLOG_REDALERT("nimble_adv.uuids16", "failed to allocate memory");
            goto error;
        }
        for (int i = 0; i < advFields->num_uuids16; i++) {
            uuids16[i].u.type = BLE_UUID_TYPE_16;
            uuids16[i].value = advFields->uuids16[i].value;
        }
        nimble_adv->num_uuids16 = advFields->num_uuids16;
        nimble_adv->uuids16 = uuids16;
        nimble_adv->uuids16_is_complete = 1;
    }

    if (advFields->num_uuids128 && advFields->uuids128) {
        ble_uuid128_t *uuids128 = lt_malloc(advFields->num_uuids128 * sizeof(ble_uuid128_t));
        if (!uuids128) {
            LTLOG_REDALERT("nimble_adv.uuids128", "failed to allocate memory");
            goto error;
        }
        for (int i = 0; i < advFields->num_uuids128; i++) {
            uuids128[i].u.type = BLE_UUID_TYPE_128;
            lt_memcpy(uuids128[i].value, advFields->uuids128[i].value, sizeof(advFields->uuids128[i].value));
        }
        nimble_adv->num_uuids128 = advFields->num_uuids128;
        nimble_adv->uuids128 = uuids128;
        nimble_adv->uuids128_is_complete = 1;
    }

    if (lt_strlen(advFields->name) > 0) {
        nimble_adv->name = (const u8 *)advFields->name;
        nimble_adv->name_len = lt_strlen(advFields->name);
        nimble_adv->name_is_complete = 1;
    }

    nimble_adv->tx_pwr_lvl = advFields->tx_pwr_lvl;
    nimble_adv->tx_pwr_lvl_is_present = advFields->tx_pwr_lvl_is_present;

    nimble_adv->mfg_data = advFields->mfg_data;
    nimble_adv->mfg_data_len = advFields->mfg_data_len;

    if (advFields->svc_data_uuid16_len > 0 && advFields->svc_data_uuid16) {
        nimble_adv->svc_data_uuid16 = advFields->svc_data_uuid16;
        nimble_adv->svc_data_uuid16_len = advFields->svc_data_uuid16_len;
    }

    if (advFields->svc_data_uuid128_len > 0 && advFields->svc_data_uuid128) {
        nimble_adv->svc_data_uuid128 = advFields->svc_data_uuid128;
        nimble_adv->svc_data_uuid128_len = advFields->svc_data_uuid128_len;
    }

    return nimble_adv;
error:
    LTLOG_REDALERT("nimble.adv.trans.err", "failed to translate adv fields to nimble adv fields");
    free_nimble_adv_fields(nimble_adv);
    return NULL;
}

static void get_nimble_adv_params(const LTBleAdvParams* advParams, struct ble_gap_adv_params* nimbleAdvParams) {
    if (advParams->conn_mode == LT_BLE_CONN_MODE_NON) {
        nimbleAdvParams->conn_mode = BLE_GAP_CONN_MODE_NON;
    } else if (advParams->conn_mode == LT_BLE_CONN_MODE_DIR) {
        nimbleAdvParams->conn_mode = BLE_GAP_CONN_MODE_DIR;
    } else if (advParams->conn_mode == LT_BLE_CONN_MODE_UND) {
        nimbleAdvParams->conn_mode = BLE_GAP_CONN_MODE_UND;
    }

    if (advParams->disc_mode == LT_BLE_DISC_MODE_NON) {
        nimbleAdvParams->disc_mode = BLE_GAP_DISC_MODE_NON;
    } else if (advParams->disc_mode == LT_BLE_DISC_MODE_LTD) {
        nimbleAdvParams->disc_mode = BLE_GAP_DISC_MODE_LTD;
    } else if (advParams->disc_mode == LT_BLE_DISC_MODE_GEN) {
        nimbleAdvParams->disc_mode = BLE_GAP_DISC_MODE_GEN;
    }

    nimbleAdvParams->itvl_min = advParams->itvl_min;
    nimbleAdvParams->itvl_max = advParams->itvl_max;
    nimbleAdvParams->high_duty_cycle = advParams->high_duty_cycle;
}

// In blehost thread
static void PeripheralOnSync(void) {
    P(__FUNCTION__, "bleHost Thread 0x%lx S.nim_ctx.bleHostThr 0x%lx",
        LT_PLT_HANDLE(S.thread->GetCurrentThread()),
        LT_PLT_HANDLE(S.nim_ctx.bleHostThr));
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_ownAddrType);
    ble_hs_id_copy_addr(s_ownAddrType, s_ownMacAddr.octet, NULL);
    P("mac", "%02X:%02X:%02X:%02X:%02X:%02X", s_ownMacAddr.octet[5], s_ownMacAddr.octet[4], s_ownMacAddr.octet[3],
                                             s_ownMacAddr.octet[2], s_ownMacAddr.octet[1], s_ownMacAddr.octet[0]);
    LTBleDeviceHandle hDevice = VOIDPTR_TO_LTHANDLE(S.thread->GetThreadSpecificClientData(S.thread->GetCurrentThread(), "lt_ble"));
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (devCtx) {
        devCtx->mac = s_ownMacAddr;
        devCtx->bleHostThr = S.thread->GetCurrentThread();
        S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Started, NULL);
    }
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
}

// In blehost thread
static void PeripheralOnReset(int reason) {
    P(__FUNCTION__, " ");
}

/** End of run in ble host thread *********************************************/

// In app thread
static bool CommonDriverBle_StartAdvertise(LTBleDeviceHandle hDevice, LTBleAdvFields* advFields, LTBleAdvFields* rspFields, LTBleAdvParams* advParams) {
    int ret = 0;
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    struct ble_gap_adv_params nimble_adv_params = {0};
    struct ble_hs_adv_fields *nimble_adv = NULL;
    struct ble_hs_adv_fields *nimble_res = NULL;
    if (!hDevice) {
        LTLOG_YELLOWALERT("no.dev.handle", "No device handle supplied");
        ret = BLE_HS_EINVAL;
        goto done;
    }

    if (advParams) {
        get_nimble_adv_params(advParams, &nimble_adv_params);
    } else {
        LTLOG_YELLOWALERT("no.adv.param", "No adv params supplied");
        ret = BLE_HS_EINVAL;
        goto done;
    }

    if (advFields) {
        nimble_adv = get_nimble_adv_fields(advFields);
    } else {
        LTLOG_YELLOWALERT("no.adv.field", "No adv advertise data supplied");
        ret = BLE_HS_EINVAL;
        goto done;
    }

    // rspFields (scan response) is optional
    if (rspFields) {
        nimble_res = get_nimble_adv_fields(rspFields);
    } else {
        LTLOG("no.adv.rsp", "No resp advertise data supplied");
    }

    if (!nimble_adv || (rspFields && !nimble_res)) {
        LTLOG_REDALERT("mem.alloc.failed", "failed to get adv or resp fields");
        ret = BLE_HS_EINVAL;
        goto done;
    }

    ret = ble_gap_adv_set_fields(nimble_adv);
    if (ret) {
        LTLOG_YELLOWALERT("adv.set.fail", "failed to set adv fields, error code: %d", ret);
        ret = BLE_HS_EINVAL;
        goto done;
    }

    // scan response is optional, so check if needs to be configured.
    if (nimble_res) {
        ret = ble_gap_adv_rsp_set_fields(nimble_res);
        if (ret) {
            LTLOG_YELLOWALERT("adv.resp.set.fail", "failed to set rsp fields, error code: %d", ret);
            ret = BLE_HS_EINVAL;
            goto done;
        }
    }

    ble_svc_gap_device_name_set((const char*)nimble_adv->name);
    // Set to Started if not Connected 
    {
        u32 currentState;
        do {
            currentState = LTAtomic_Load(&s_state);
            if (currentState == kLTDeviceBle_State_Connected) {
                break; // Don't change if Connected
            }
        } while (!LTAtomic_CompareAndExchange(&s_state, currentState, kLTDeviceBle_State_Started));
    }
    ret = ble_gap_adv_start(s_ownAddrType, NULL, advParams->duration_ms, &nimble_adv_params, GapEventCallbackWrapper, LTHANDLE_TO_VOIDPTR(hDevice));
    if (ret) {
        LTLOG_REDALERT("adv.start", "failed to start advertising");
    } else {
        if(!LTAtomic_CompareAndExchange(&s_state, kLTDeviceBle_State_Connected, kLTDeviceBle_State_ConnectedAdvertising)) {
            LTAtomic_Store(&s_state, kLTDeviceBle_State_Advertising);
        }
        
    }
done:
    free_nimble_adv_fields(nimble_adv);
    free_nimble_adv_fields(nimble_res);
    return (0 == ret);
}

static bool CommonDriverBle_StartDirectedAdvertise(LTBleDeviceHandle hDevice, LTMacAddress peer) {
    int ret = 0;
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    struct ble_gap_adv_params directed_adv_params = {0};
    if (!hDevice) {
        LTLOG_YELLOWALERT("no.dev.handle", "No device handle supplied");
        return false;
    }

    // Set high duty directed advertisement
    directed_adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;
    directed_adv_params.high_duty_cycle = 1;

    // Convert peer addr from host to network
    LTMacAddress h2n_peer_addr;
    CopyMacAddressH2N(h2n_peer_addr.octet, peer.octet);
    char peerStr[18];
    S.mac_lib->MacAddressToString(&peer, peerStr, ':');
    PLOG("directed.avd.peer.add", "%s", peerStr);

    // Set peer addr to which the advertisement are directed.
    ble_addr_t adv_peer_addr;
    adv_peer_addr.type = BLE_ADDR_PUBLIC;
    lt_memcpy(adv_peer_addr.val, h2n_peer_addr.octet, sizeof(LTMacAddress));
    if (LTAtomic_Load(&s_state) == kLTDeviceBle_State_Connected && S.mac_lib->IsEqual(&s_peerMacAddr, &peer)) {
        LTLOG_YELLOWALERT("directed.adv.connected", "Connected already");
        return false;
    }
    LTAtomic_Store(&s_state, kLTDeviceBle_State_Started);
    // Start directed advertisement
    ret = ble_gap_adv_start(s_ownAddrType, &adv_peer_addr, BLE_HS_FOREVER, &directed_adv_params, GapEventCallbackWrapper, LTHANDLE_TO_VOIDPTR(hDevice));
    // 0x20b (BLE_ERR_ACL_CONN_EXISTS + BLE_HS_ERR_HCI_BASE) means connection already exists error
    // The controller is unable to recover from this. Reset controller
    if (ret == (BLE_ERR_ACL_CONN_EXISTS + BLE_HS_ERR_HCI_BASE)) {
        ble_hs_sched_reset(ret);
    }
    if (ret == BLE_HS_ENOMEM) { // Out of memory error. Unable to recover. Reboot system
        LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
        if (devCtx) {
            S.event->NotifyEvent(devCtx->hEvent, devCtx->hDev, kLTDeviceBle_Event_Nimble_DirAdv_fail, NULL);
        }
    }
    if (ret) {
        LTLOG_REDALERT("directed.adv.start.fail", "failed to start directed advertising ret:%d", ret);
    } else {
        LTAtomic_Store(&s_state, kLTDeviceBle_State_Advertising);
    }
    return (0 == ret);
}

static void CommonDriverBle_StopAdvertise(void) {
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    ble_gap_adv_stop();
    // keep all other non-advertising state - atomically change Advertising to Started
    if (!LTAtomic_CompareAndExchange(&s_state, kLTDeviceBle_State_Advertising, kLTDeviceBle_State_Started)) {
        LTAtomic_CompareAndExchange(&s_state, kLTDeviceBle_State_ConnectedAdvertising, kLTDeviceBle_State_Connected);
    }
}

static bool CommonDriverBle_Indicate(LTBleDeviceHandle hDevice, LTBleChrHandle hChr) {
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    LTBleDeviceCtx *devCtx = NULL;
    LTBleChrCtx *ctx = NULL;
    int ret = 0;
    devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) {
        ret = BLE_HS_EINVAL;
        goto done;
    }
    ctx = S.core->ReserveHandlePrivateData(hChr);
    if (!ctx) {
        ret = BLE_HS_EINVAL;
        goto done;
    }
    P("indicate", "0x%x, 0x%x", devCtx->connHandle, ctx->attrHandle);
    struct os_mbuf *om = NULL;
    om = ble_hs_mbuf_from_flat(ctx->readBuf, ctx->readLen);
    if (om) {
        ret = ble_gatts_indicate_custom(devCtx->connHandle, ctx->attrHandle, om);
    } else {
        PLOG("ind.no.buf", "fails to allocate buffer");
        ret = BLE_HS_ENOMEM;
    }

done:
    if (ctx) S.core->ReleaseHandlePrivateData(hChr, ctx);
    if (devCtx) S.core->ReleaseHandlePrivateData(hDevice, devCtx);
    if (ret) {PLOG("ind.ret", "error %d", ret);}
    return (0 == ret);
}

static bool CommonDriverBle_Notify(LTBleDeviceHandle hDevice, LTBleChrHandle hChr) {
    P(__FUNCTION__, "th 0x%lx", LT_PLT_HANDLE(S.thread->GetCurrentThread()));
    LTBleDeviceCtx *devCtx = NULL;
    LTBleChrCtx *ctx = NULL;
    int ret = 0;
    devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) {
        ret = BLE_HS_EINVAL;
        goto done;
    }
    ctx = S.core->ReserveHandlePrivateData(hChr);
    if (!ctx) {
        ret = BLE_HS_EINVAL;
        goto done;
    }
    P("notify", "conn=0x%x, attr=0x%x readlen=0x%x", devCtx->connHandle, ctx->attrHandle, ctx->readLen);
    struct os_mbuf *om = NULL;
    om = ble_hs_mbuf_from_flat(ctx->readBuf, ctx->readLen);
    if (om) {
        ret = ble_gatts_notify_custom(devCtx->connHandle, ctx->attrHandle, om);
    } else {
        PLOG("nfy.no.buf", "fails to allocate buffer");
        ret = BLE_HS_ENOMEM;
    }

done:
    if (ctx) S.core->ReleaseHandlePrivateData(hChr, ctx);
    if (devCtx) S.core->ReleaseHandlePrivateData(hDevice, devCtx);
    if (ret) {P("nfy.ret", "error %d", ret);}
    return (0 == ret);
}

static void DisableController(void) {
    S.controller->Enable(false);
}

static void FreeService(void) {
    if (services_ptr) {
        for (int i = 0 ; i < s_num_services; i++) {
            lt_free((void*)services_ptr[i].uuid);
            const struct ble_gatt_chr_def *chr = services_ptr[i].characteristics;
            if (chr) {
                for (const struct ble_gatt_chr_def *ptr = chr;
                     ptr->uuid != NULL; ptr++) {
                    lt_free((void*)ptr->uuid);
                }
                lt_free((void *)chr);
            }
        }
    }
    lt_free(services_ptr);
    services_ptr = NULL;
}

/**
 * @brief This will set only one service
 *
 * @param svc
 * @return true
 * @return false
 */
static bool CommonDriverBle_SetService(LTBleSvcHandle hSvc) {
    P(__FUNCTION__, " ");
    if (!hSvc) return false;
    LTBleSvcCtx *svc = S.core->ReserveHandlePrivateData(hSvc);
    if (!svc) return false;
    // Service
    services_ptr = lt_malloc(sizeof(struct ble_gatt_svc_def)*s_num_services);
    if (!services_ptr) return false;
    lt_memset(services_ptr, 0x00, s_num_services*sizeof(struct ble_gatt_svc_def));
    struct ble_gatt_svc_def *service = &services_ptr[0];
    service->type = svc->serviceType;

    if (svc->uuid->type == kLTBleUuidType_128) {
        ble_uuid128_t *uuid = lt_malloc(sizeof(ble_uuid128_t));
        if (!uuid) goto error;
        uuid->u.type = BLE_UUID_TYPE_128;
        lt_memcpy(uuid->value, svc->uuid->value.uuid128.value, sizeof(uuid->value));
        service->uuid = (ble_uuid_t *)uuid;
    } else if (svc->uuid->type == kLTBleUuidType_16) {
        LTLOG_YELLOWALERT("set.svc", "short uuid in use %04X", svc->uuid->value.uuid16.value);
        ble_uuid16_t *uuid = lt_malloc(sizeof(ble_uuid16_t));
        if (!uuid) goto error;
        uuid->u.type = BLE_UUID_TYPE_16;
        uuid->value = svc->uuid->value.uuid16.value;
        service->uuid = (ble_uuid_t *)uuid;
    } else {
        goto error; // only support uuid128 and uuid16
    }

    // Characteristics portion
    // Allocate for ble_gatt_chr_def array
    struct ble_gatt_chr_def *chr = lt_malloc(sizeof(struct ble_gatt_chr_def) * (svc->numCharacteristics + 1));
    if (!chr) {
        LTLOG_YELLOWALERT("chars", "failed to allocate memory");
        goto error;
    }
    lt_memset(chr, 0, sizeof(struct ble_gatt_chr_def) * (svc->numCharacteristics + 1));
    service->characteristics = chr;
    for (int i = 0; i < svc->numCharacteristics; ++i) {
        LTBleChrHandle hChr = svc->chr_hdl[i];
        LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
        if (chrCtx->uuid->type == kLTBleUuidType_128) {
            ble_uuid128_t *uuid = lt_malloc(sizeof(ble_uuid128_t));
            if (!uuid) goto error;
            uuid->u.type = BLE_UUID_TYPE_128;
            lt_memcpy(uuid->value, chrCtx->uuid->value.uuid128.value, sizeof(uuid->value));
            chr[i].uuid = (ble_uuid_t *)uuid;
        } else if (chrCtx->uuid->type == kLTBleUuidType_16) {
            LTLOG_YELLOWALERT("set.svc.chr", "short uuid in use %04X", chrCtx->uuid->value.uuid16.value);
            ble_uuid16_t *uuid = lt_malloc(sizeof(ble_uuid16_t));
            if (!uuid) goto error;
            uuid->u.type = BLE_UUID_TYPE_16;
            uuid->value = chrCtx->uuid->value.uuid16.value;
            chr[i].uuid = (ble_uuid_t *)uuid;
        } else {
            goto error; // only support uuid128 and uuid16
        }
        chr[i].flags = chrCtx->chrFlags;
        chr[i].access_cb = &GattChrCallback;
        chr[i].arg = LTHANDLE_TO_VOIDPTR(hChr);
        S.core->ReleaseHandlePrivateData(hChr, chrCtx);
    }
    if (0 != ble_gatts_count_cfg(services_ptr)) {
        LTLOG_YELLOWALERT("err.service", "failed to count cfg");
        goto error;
    }
    if (0 != ble_gatts_add_svcs(services_ptr)) {
        LTLOG_YELLOWALERT("err.service", "failed to add svcs");
        goto error;
    }
    ble_hs_sched_start();
    S.core->ReleaseHandlePrivateData(hSvc, svc);
    return true;

error:
    if(svc) S.core->ReleaseHandlePrivateData(hSvc, svc);
    FreeService();
    return false;
}

static bool CommonDriverBle_Start(LTBleDeviceHandle hDevice) {
    P(__FUNCTION__, " ");
    bool ret = true;
    u16 preferred_mtu = LT_BLE_DEFAULT_MTU_LEN;
    if (!hDevice) return false;

    // add handle contxt of control characteristic to gatt service
    LTBleDeviceCtx *dvcCtx = S.core->ReserveHandlePrivateData(hDevice);

    // Set callbacks
    ble_hs_cfg.reset_cb           = &PeripheralOnReset;    // Callback when the host resets itself and the controller due to fatal error.
    ble_hs_cfg.sync_cb            = &PeripheralOnSync;     // Callback when the host and controller become synced. This happens at startup and after a reset.
    ble_hs_cfg.gatts_register_cb  = &GattRegisterCallback; // An optional callback upon registration of each GATT resource (service, characteristic, or descriptor).
    ble_hs_cfg.gatts_register_arg = LTHANDLE_TO_VOIDPTR(hDevice);
    ble_hs_cfg.store_status_cb    = &ble_store_util_status_rr; // Callback when a persistence operation cannot be performed or a persistence failure is imminent.

    // Security Manager Local Input Output Capabilities
    ble_hs_cfg.sm_io_cap          = MYNEWT_VAL_BLE_SM_IO_CAP;  // BLE_HS_IO_NO_INPUT_OUTPUT, 3
    ble_hs_cfg.sm_oob_data_flag   = 0;  // MYNEWT_VAL_BLE_SM_OOB_DATA_FLAG, 0
    ble_hs_cfg.sm_bonding         = 1;  // MYNEWT_VAL_BLE_SM_BONDING, 0
    ble_hs_cfg.sm_mitm            = 0;  // MYNEWT_VAL_BLE_SM_MITM, 0
    ble_hs_cfg.sm_sc              = 0;  // MYNEWT_VAL_BLE_SM_SC, 0, may fall back to legacy
    ble_hs_cfg.sm_keypress        = 0;  // not supported

    if (dvcCtx->securePairing) {
        ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK;
    } else {
        ble_hs_cfg.sm_our_key_dist   = 0;
        ble_hs_cfg.sm_their_key_dist = 0;
    }
        // Init controller
    if (!S.controller->Enable(true)) {
        ret = false;
        goto done;
    }
    if (!S.controller->RegisterCallbacks(&VhciHostCallback)){
        ret = false;
        goto done;
    }

    S.nim_ctx.hDevice = hDevice;
    S.nim_ctx.ble_host_priority = dvcCtx->ble_host_thread_prio;
    nimble_port_init(&S.nim_ctx);
    // Configure the HCI transport to communicate with a host.
    ble_hci_trans_cfg_hs(&ble_transport_to_hs_evt_impl, &ble_transport_to_hs_acl_impl);
    // Use preferred mtu set by app, otherwise fall back to default.
    if (dvcCtx->preferred_mtu) preferred_mtu = dvcCtx->preferred_mtu;
    ble_att_set_preferred_mtu(preferred_mtu);
    // Init service and char
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_store_config_init();
    // Set state to started
    LTAtomic_Store(&s_state, kLTDeviceBle_State_Started);

done:
    S.core->ReleaseHandlePrivateData(hDevice, dvcCtx);
    return ret;
}

// TODO: if stopped, only restart after reboot.
static void CommonDriverBle_Stop(void) {
    if (LTAtomic_Load(&s_state) == kLTDeviceBle_State_Off) return;
    LTAtomic_Store(&s_state, kLTDeviceBle_State_Off);
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();

    DisableController();
    lt_memset(&s_ownMacAddr.octet, 0, sizeof(s_ownMacAddr.octet));
    FreeService();
}

static LTDeviceBle_State CommonDriverBle_GetState(void) {
    return (LTDeviceBle_State)LTAtomic_Load(&s_state);
}

static void CommonDriverBle_GetOwnAddress(LTMacAddress *own) {
    if (!own) return;
    CopyMacAddressH2N(own->octet, s_ownMacAddr.octet);
}

static void CommonDriverBle_GetPeerAddress(LTMacAddress *peer) {
    if (!peer) return;
    CopyMacAddressH2N(peer->octet, s_peerMacAddr.octet);
}

static s8 CommonDriverBle_GetRSSI(u16 connHandle, s8 *rssi) {
    if (!rssi) return -1;
    return ble_gap_conn_rssi(connHandle, rssi);
}

static bool CommonDriverBle_SendPrivVenCommand(const char *cmd, u8* resp, u32 respLen) {
    P(__FUNCTION__, "Vendor Command = %s\n", cmd);
    u8 hci_buffer[32] = {}; //Max Length 32
    u32 len = 0;
    u16 opcode = 0;
    bool rc = true;
    if (S.controller->SendPrivVenCommand(cmd, hci_buffer, &len, &opcode) == false) {
        LTLOG_YELLOWALERT("vendor.cmd.tx", "error");
        rc = false;
        goto error;
    }
    P("hci", "opcode=0x%2x len=%lu\n", opcode, LT_Pu32(len));
    LT_ASSERT(len <= 32);
    if (len > 32) {
        LTLOG_REDALERT("vendor.cmd.hci.tx.bad.len", "Exceeded length");
        rc = false;
        goto error;
    }

    if (opcode && S.logging_en) {
        int hexStringIndex = 0;
        char hexString[31];
        hexString[0] = '\0';
        for (int i = 0; i < len; ++i) {
            hexStringIndex += lt_snprintf(hexString + hexStringIndex,
                sizeof(hexString) - hexStringIndex, "%02X ", hci_buffer[i]);
            if ((i % 10 == 9) || (i == len - 1)) {
                hexString[hexStringIndex - 1] = '\0';
                P("hci", "opcode=0x%2x len=%lx cmd=%s\n", opcode, LT_Pu32(len), hexString);
                hexStringIndex = 0;
            }
        }
    }

    if (opcode>0 && ble_hs_hci_send_cmd(opcode, hci_buffer, len, resp, respLen)) {
        LTLOG_YELLOWALERT("vendor.cmd.hci.tx", "error");
        rc = false;
        goto error;
    }
error:
    return rc;
}

static bool CommonDriverBle_SendHciCommand(const u16 opcode, const u8 *cmd, u32 len,  u8* resp, u32 respLen) {
    P(__FUNCTION__, "HCI Command Len = %lu\n", LT_Pu32(len));
    bool rc = true;
    if (ble_hs_hci_send_cmd(opcode, cmd, len, resp, respLen)) {
        LTLOG_YELLOWALERT("hc.cmd.tx", "error");
        rc = false;
    }
    return rc;
}

static void CommonDriverBle_EnableLogging(bool enable) {
    S.logging_en = enable;
    if(S.controller->LoggingEnable)
        S.controller->LoggingEnable(enable);
}

static const char* CommonDriverBle_GetDeviceName(void) {
    return ble_svc_gap_device_name();
}

static bool CommonDriverBle_SetMaxTransmitParams(LTBleDeviceCtx *devCtx, u16 maxTxOctets, u16 maxTxTime) {
    if (maxTxOctets >= BLE_HCI_SET_DATALEN_TX_OCTETS_MIN && maxTxOctets <= BLE_HCI_SET_DATALEN_TX_OCTETS_MAX &&
        maxTxTime >= BLE_HCI_SET_DATALEN_TX_TIME_MIN && maxTxTime <= BLE_HCI_SET_DATALEN_TX_TIME_MAX) {
        s16 ret  = ble_gap_set_data_len(devCtx->connHandle, maxTxOctets, maxTxTime);
        if (ret != 0) {
            LTLOG_YELLOWALERT("max.tx.param.set.fail", "ret error 0x%x", ret);
            return false;
        }
        P("max.tx.param.set", "Sucecssfully updated the transmit parameters");
        return true;
    }
    return false;
}

static bool CommonDriverBle_UpdateConnectionParams(LTBleDeviceHandle hDevice, LTBleUpdConnParams* updConnParams) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return false;

    int rc = 0;
    bool ret_val = true;

    struct ble_gap_upd_params upd_conn_params = {0};
    upd_conn_params.itvl_min = updConnParams->itvl_min;
    upd_conn_params.itvl_max = updConnParams->itvl_max;
    upd_conn_params.latency = updConnParams->latency;
    upd_conn_params.supervision_timeout = updConnParams->supervision_timeout;
    upd_conn_params.min_ce_len = updConnParams->min_ce_len;
    upd_conn_params.max_ce_len = updConnParams->max_ce_len;
    rc = ble_gap_update_params(devCtx->connHandle, &upd_conn_params);
    if (rc != 0) {
        LTLOG_YELLOWALERT("upd.conn.param.fail", "ret error %d", rc);
        ret_val = false;
    }
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
    return ret_val;
}

static bool CommonDriverBle_SecurityInitiate(LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return false;

    int ret = ble_gap_security_initiate(devCtx->connHandle);
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);

    if (ret != 0) {
        LTLOG_YELLOWALERT("security.initiate.fail", "ret error %d", ret);
        return false;
    }
    return true;
}

static bool CommonDriverBle_Disconnect(LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return false;

    int ret = ble_gap_terminate(devCtx->connHandle, BLE_ERR_REM_USER_CONN_TERM);
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);

    if (ret == 0 || ret == BLE_HS_ENOTCONN) {
        return true;
    } else {
        LTLOG_YELLOWALERT("terminate.fail", "ret error %d", ret);
        return false;
    }
}

static bool CommonDriverBle_GetConnectionInfo(u16 connHandle, LTBleConnectionInfo *info) {
    if (!info) return false;
    if (!S.controller || !S.controller->GetConnectionInfo) return false;
    return S.controller->GetConnectionInfo(connHandle, info);
}

static bool CommonDriverBle_RegisterPktTimestamp(u16 att_handle, u16 tag_offset,
        u8 tag_len, LTBlePktTimestampCb cb, void *userData) {
    if (!S.controller || !S.controller->RegisterPktTimestamp) return false;
    return S.controller->RegisterPktTimestamp(att_handle, tag_offset, tag_len, cb, userData);
}

static bool CommonDriverBle_UnregisterPktTimestamp(u16 att_handle) {
    if (!S.controller || !S.controller->UnregisterPktTimestamp) return false;
    return S.controller->UnregisterPktTimestamp(att_handle);
}

/*******************************************************************************
 * ILTHostBle Library Interface
 ******************************************************************************/

static ILTHostBle s_ILTHostBle;

static u32 CommonDriverBleImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit CommonDriverBleImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return S.core->CreateHandle((LTInterface *)&s_ILTHostBle, 1);
}

static void ShutDownDriverBle(void) {
    lt_destroyhandle(S.hDevController);
    lt_closelibrary(S.devController);
    lt_closelibrary(S.mac_lib);
    S = (struct Statics){};
}

static bool CommonDriverBleImpl_LibInit(void) {
    S = (struct Statics){};
    S.core = LT_GetCore();
    S.thread = lt_getlibraryinterface(ILTThread, S.core);
    S.event = lt_getlibraryinterface(ILTEvent, S.core);
    do {
        if (!osif_lt_init()) break;
        S.devController = lt_openlibrary(LTDeviceBleController);
        if (!S.devController) break;
        u32 numDev = S.devController->GetNumDeviceUnits();
        S.hDevController = S.devController->CreateDeviceUnitHandle(numDev - 1);
        if (!S.hDevController) break;
        S.controller = lt_gethandleinterface(ILTBleController, S.hDevController);
        if (!S.controller) break;
        S.mac_lib = lt_openlibrary(LTUtilityMacAddress);
        if (!S.mac_lib) break;
        return true;
    } while (0);

    LTLOG_REDALERT("error", "fail to load ble driver");
    ShutDownDriverBle();
    return false;
}

static void CommonDriverBleImpl_LibFini(void) {
    ShutDownDriverBle();
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceBle, CommonDriverBle);

define_LTLIBRARY_INTERFACE(ILTHostBle) {
    .SetService               = &CommonDriverBle_SetService,
    .Start                    = &CommonDriverBle_Start,
    .Stop                     = &CommonDriverBle_Stop,
    .StartAdvertise           = &CommonDriverBle_StartAdvertise,
    .StartDirectedAdvertise   = &CommonDriverBle_StartDirectedAdvertise,
    .StopAdvertise            = &CommonDriverBle_StopAdvertise,
    .Indicate                 = &CommonDriverBle_Indicate,
    .Notify                   = &CommonDriverBle_Notify,
    .SetMaxTransmitParams     = &CommonDriverBle_SetMaxTransmitParams,
    .UpdateConnectionParams   = &CommonDriverBle_UpdateConnectionParams,
    .GetState                 = &CommonDriverBle_GetState,
    .GetOwnAddress            = &CommonDriverBle_GetOwnAddress,
    .GetPeerAddress           = &CommonDriverBle_GetPeerAddress,
    .GetRSSI                  = &CommonDriverBle_GetRSSI,
    .SendPrivVenCommand       = &CommonDriverBle_SendPrivVenCommand,
    .SendHciCommand           = &CommonDriverBle_SendHciCommand,
    .EnableLogging            = &CommonDriverBle_EnableLogging,
    .GetDeviceName            = &CommonDriverBle_GetDeviceName,
    .SecurityInitiate         = &CommonDriverBle_SecurityInitiate,
    .Disconnect               = &CommonDriverBle_Disconnect,
    .Reset                    = &CommonDriverBle_ResetController,
    .GetControllerResetReason = &CommonDriverBle_GetControllerResetReason,
    .GetConnectionInfo        = &CommonDriverBle_GetConnectionInfo,
    .RegisterPktTimestamp     = &CommonDriverBle_RegisterPktTimestamp,
    .UnregisterPktTimestamp   = &CommonDriverBle_UnregisterPktTimestamp
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(CommonDriverBle, (ILTHostBle))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Sep-22   vespasian   created
 *  12-Sep-22   vespasian   Isolated host driver functions
 *  30-Sep-22   vespasian   Migrated to common
 *  21-Dec-22   gallienus   restructured and cleaned
 */
