/*******************************************************************************
 * lt/device/blecontroller/LTDeviceBleController.h
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 * 
 * BLE Controller Device Library
 *
 * Provides HCI interface functions for link-layer BLE controller. All
 * platform-dependent features and functions are hidden below this layer and
 * shall not be called or otherwise referenced directly from higher levels.
 * 
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_BLECONTROLLER_LTDECVICEBLECONTROLLER_H
#define LT_INCLUDE_LT_DEVICE_BLECONTROLLER_LTDECVICEBLECONTROLLER_H

#include <lt/LTTypes.h>
#include <lt/device/ble/LTBleConnectionInfo.h>
#include <lt/device/ble/LTBlePktTimestamp.h>
LT_EXTERN_C_BEGIN

/**
 * @internal
 * HCI packet explanation : http://www.dziwior.org/Bluetooth/HCI.html
 * 
 * HCI command: H4_TYPE_COMMAND (1B) + Command (OGF+OCF,2B) + ParaLength (1B) + Paras (<255B)
 */

/* HCI H4 message type definitions */
enum {
    H4_TYPE_COMMAND = 1,  // command
    H4_TYPE_ACL     = 2,  // asynchronous connectionless data
    H4_TYPE_SCO     = 3,  // synchronous data
    H4_TYPE_EVENT   = 4,  // event
};

#define BLE_HCI_EVENT_HDR_LEN       2
#define BLE_HCI_CMD_HDR_LEN         3
#define BLE_HCI_DATA_HDR_LEN        4

/* HCI command opcode group field (OGF) */
#define HCI_GRP_HOST_CONT_BASEBAND_CMDS         (0x03 << 10)
#define HCI_GRP_BLE_CMDS                        (0x08 << 10)

/* HCI command opcode field (OCF) */
/* Reset command */
#define HCI_RESET                               (0x0003 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)
/* Set event command */
#define HCI_SET_EVT_MASK                        (0x0001 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)
/* Advertising Commands */
#define HCI_BLE_WRITE_ADV_PARAMS                (0x0006 | HCI_GRP_BLE_CMDS)
#define HCI_BLE_WRITE_ADV_DATA                  (0x0008 | HCI_GRP_BLE_CMDS)
#define HCI_BLE_WRITE_ADV_ENABLE                (0x000A | HCI_GRP_BLE_CMDS)

/* HCI Command length. */
#define HCI_H4_CMD_PREAMBLE_SIZE                (4)
#define HCIC_PARAM_SIZE_WRITE_ADV_ENABLE        (1)
#define HCIC_PARAM_SIZE_SET_EVENT_MASK          (8)
#define HCIC_PARAM_SIZE_BLE_WRITE_ADV_PARAMS    (15)
#define HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA      (31)

/* Device address length */
#define BD_ADDR_LEN                             (6)

#define UINT16_TO_STREAM(pBuf, val) {*(pBuf)++ = (u8)(val); *(pBuf)++ = (u8)((val) >> 8);}
#define UINT8_TO_STREAM(pBuf, val)  {*(pBuf)++ = (u8)(val);}
#define BDADDR_TO_STREAM(pBuf, a)   {int i; for (i = 0; i < BD_ADDR_LEN;  i++) *(pBuf)++ = (u8) a[BD_ADDR_LEN - 1 - i];}

/* The names of all of the symbols (#defines, enums) in this file are a train wreck */


/*******************************************************************************
** BLE Controller Device API
*******************************************************************************/
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceBleController, 1) LTLIBRARY_EMPTY_INTERFACE;

/* Callback used to notify that the host can send a HCI packet to controller. */
typedef void (*LTBleController_NotifyHostSendReady)(void);

/* Callback used to notify that the host can receive data from the controller. */
typedef int (*LTBleController_NotifyHostRecvReady)(const u8 *data, u16 len);

typedef struct LTBleController_VhciHostCallback {
    LTBleController_NotifyHostSendReady NotifySendReady;
    LTBleController_NotifyHostRecvReady NotifyRecvReady;
} LTBleController_VhciHostCallback;

typedef_LTLIBRARY_INTERFACE(ILTBleController, 1) {
    bool (*Enable)(bool enable);
        /**< Enable BLE controller
         *   @param enable if true enable the controller, if false disable it.
         */

    bool (*RegisterCallbacks)(const LTBleController_VhciHostCallback *callbacks);
        /**< Register controller send/receive message callbacks 
         * @param callbacks  Pointer to a callback struct.
         * @note  If callbacks is NULL, default driver callbacks are registered.
         */

    bool (*Send)(const u8 *data, u16 len);
        /**< Send a message to BLE controller
         *   @param data  Address of the data to send to controller
         *   @param len   Length of data in bytes
         */

    bool (*SendPrivVenCommand)(const char* cmd, u8* hci_buffer, u32* len, u16* opcode);
        /**
         * Send a vendor specific command to the BLE controller.
         * The size of the buffer will be max 32 bytes. If you have written
         * More than 32 bytes, then will result into an Assert.
         */
    void  (*LoggingEnable)(bool enable);
        /**< Enable or disable logging
         *   @param enable if true enable logging, if false disable it.
         */

    bool (*GetConnectionInfo)(u16 connHandle, LTBleConnectionInfo *info);
        /**< Get BLE connection statistics from the controller.
         *   @param connHandle  Connection handle (may be ignored by some platforms)
         *   @param info        Pointer to structure to fill with connection info
         *   @return true on success, false if not supported or error
         */


    bool (*RegisterPktTimestamp)(u16 att_handle, u16 tag_offset, u8 tag_len,
                                 LTBlePktTimestampCb cb, void *userData);
        /**< Register for packet-level TX/RX timestamp callbacks.
         *   @param att_handle   ATT attribute handle to monitor
         *   @param tag_offset   Byte offset in packet to copy tag bytes from
         *   @param tag_len      Number of tag bytes to copy (max 4)
         *   @param cb           Callback invoked on the callers thread
         *   @param userData     Opaque pointer passed through to callback
         *   @return true on success, false if not supported
         */

    bool (*UnregisterPktTimestamp)(u16 att_handle);
        /**< Unregister packet timestamp callback.
         *   @param att_handle   ATT attribute handle to stop monitoring
         *   @return true on success, false if not supported
         */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_BLECONTROLLER_LTDECVICEBLECONTROLLER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Aug-22   vespasian   created
 *  20-Dec-22   gallienus   restructured and cleaned
 *  31-Jan-23   augustus    #if 0'd out the driver references
 */
