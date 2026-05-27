/*******************************************************************************
 * lt/device/ble/LTDeviceBle.h: BLE Device Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

/**
 * @defgroup ltdevice_ble LTDeviceBle
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for Bluetooth LE.
 */

#ifndef LT_INCLUDE_LT_DEVICE_BLE_LTDEVICEBLE_H
#define LT_INCLUDE_LT_DEVICE_BLE_LTDEVICEBLE_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/utility/macaddress/LTUtilityMacAddress.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/core/LTThread.h>
#include <lt/device/ble/LTBleConnectionInfo.h>
#include <lt/device/ble/LTBlePktTimestamp.h>

// This is the default MTU size that is used by BLE stack but the application
// can set a larger MTU using preferred_mtu of LTBleDeviceCtx.
// The max MTU can't be more than 527 (BLE_ATT_MTU_MAX)

// Max MTU used by iOS is 185
// Max MTU used by Roku Host is 517
#define LT_BLE_DEFAULT_MTU_LEN                             (185)    // MTU length, BUF_MAX_LEN + (BLE_ATT_MTU_MAX - BLE_ATT_ATTR_MAX_LEN)

//********************Re-define BLE macros and data types - do not modify - Start*********************************//

// Copied from blenimble/apache-mynewt-nimble-1.5.0/nimble/host/include/host/ble_gatt.h
#define LT_BLE_GATT_SVC_TYPE_PRIMARY                       1
#define LT_BLE_GATT_CHR_F_BROADCAST                        0x0001
#define LT_BLE_GATT_CHR_F_READ                             0x0002
#define LT_BLE_GATT_CHR_F_WRITE_NO_RSP                     0x0004
#define LT_BLE_GATT_CHR_F_WRITE                            0x0008
#define LT_BLE_GATT_CHR_F_NOTIFY                           0x0010
#define LT_BLE_GATT_CHR_F_INDICATE                         0x0020
#define LT_BLE_GATT_CHR_F_AUTH_SIGN_WRITE                  0x0040
#define LT_BLE_GATT_CHR_F_RELIABLE_WRITE                   0x0080
#define LT_BLE_GATT_CHR_F_AUX_WRITE                        0x0100
#define LT_BLE_GATT_CHR_F_READ_ENC                         0x0200
#define LT_BLE_GATT_CHR_F_READ_AUTHEN                      0x0400
#define LT_BLE_GATT_CHR_F_READ_AUTHOR                      0x0800
#define LT_BLE_GATT_CHR_F_WRITE_ENC                        0x1000
#define LT_BLE_GATT_CHR_F_WRITE_AUTHEN                     0x2000
#define LT_BLE_GATT_CHR_F_WRITE_AUTHOR                     0x4000

//********************Re-define BLE macros and data types - do not modify - End*********************************//

typedef LTHandle LTBleChrHandle;        // BLE characteristic handle
typedef LTHandle LTBleSvcHandle;        // BLE service handle
typedef LTHandle LTBleDeviceHandle;     // BLE device handle

/**
 * @brief Characteristic read callback
 *        Upon read op, LTDriverBle checks read permission and calls this callback.
 *        In LTDeviceBle, this callback produces and outputs the to-read bytes to the readBuf (up to LT_BLE_BUF_MAX_LEN), and saves the length of output bytes in readLen.
 *        Then, in LTDriverBle, the bytes are sent out, and the read op is done.
 *
 * @param[in]  hChr  Characteristic handle
 * @return true   Success
 * @return false  Fail
 */
struct LTBleChrCtx; // Needs forward declaration to avoid circular dependency
typedef bool (*LTBleChrReadCb)(LTBleChrHandle hChr);

/**
 * @brief Characteristic write callback
 *        Upon write op, LTDriverBle checks write permission, extract data to buf, and calls this callback.
 *        In LTDeviceBle, this callback reads and processes the data in the writeBuf up to writeLen.
 *        Then, in LTDriverBle, the write op is done.
 *
 * @param[in]    hChr  Characteristic handle
 * @return true   Success
 * @return false  Fail
 */
typedef bool (*LTBleChrWriteCb)(LTBleChrHandle hChr);

typedef enum LTBleUuidType {
    kLTBleUuidType_16 = 16,
    kLTBleUuidType_32 = 32,
    kLTBleUuidType_128 = 128,
    kLTBleUuidType_Unknown = 255,
} LTBleUuidType;

typedef struct {
    u16 value;
} LTBLeUuid16;

typedef struct {
    u8 value[LT_UUID_BYTE_LEN];
} LTBLeUuid128;

typedef struct {
    LTBleUuidType type;
    union {
        LTBLeUuid16 uuid16;
        LTBLeUuid128 uuid128;
    } value;
} LTBleUuidAny;

/** Context of a characteristic */
typedef struct LTBleChrCtx {
    // variables init on start
    LTEvent             hEvent;           // event of characteristic
    LTBleChrHandle      hChr;             // characteristic handle
    const LTBleUuidAny *uuid;
    u16                 chrFlags;
    // to send data out to remote device
    LTBleChrReadCb      readCb;           // callback on characteristic read for app to prepare data to send out
    u8                 *readBuf;          // app's read buffer
    u16                 readBufSize;      // app's read buffer size
    u16                 readLen;          // read data length
    // to receive data from remote device
    LTBleChrWriteCb     writeCb;          // callback on characteristic write for app to read data
    u8                 *writeBuf;         // app's write buffer
    u16                 writeBufSize;     // app's write buffer size
    u16                 writeLen;         // write data length
    u16                 attrHandle;       // attribute handle of the characteristic
    bool                isSubscribedForIndication; // Characteristic is subscribed for indication
    bool                isSubscribedForNotification; // Characteristic is subscribed for notification
} LTBleChrCtx;

typedef struct LTBleSvcCtx {
    const LTBleUuidAny *uuid;                             // Service UUID
    LTEvent             hEvent;                            // event of Service
    #ifndef SVC_MAX_CHARACTERISCS
    #define             SVC_MAX_CHARACTERISCS 2
    #endif
    LTBleChrHandle      chr_hdl[SVC_MAX_CHARACTERISCS];    // Array of characteristics handles
    u8                  numCharacteristics;                // update 255 chrs.
    u8                  serviceType;
    LTBleSvcHandle      hSvc;                              // service handle
} LTBleSvcCtx;

typedef struct LTBleDeviceCtx {
    LTMacAddress        mac;                                // MAC address
    LTMacAddress        remoteMac;                          // remote MAC address
    #ifndef DEV_MAX_SVCS
    #define DEV_MAX_SVCS 1
    #endif
    LTBleSvcHandle      svc_hdl[DEV_MAX_SVCS];              // Array of service handles
    u8                  numServices;                        // update 255 services
    u16                 connHandle;                         // connection handle of the device
    LTEvent             hEvent;                             // event of Device
    bool                securePairing;                      // secure pairing
    u16                 preferred_mtu;                      // Preferred MTU size set by application, can't be more than BLE_ATT_MTU_MAX
    LTBleDeviceHandle   hDev;                               // device handle
    u32                 ble_host_thread_prio;               // Ble host thread priority
    LTThread            bleHostThr;                         // Ble host thread
    u16                 connInterval;                       // negotiated Connection interval
    u16                 latency;                            // Connection latency
    u16                 supervision_timeout;                // Connection supervision timeout 
} LTBleDeviceCtx;

typedef struct LTBleAdvFields {
    u8 flags;
    #define LT_BLE_ADV_F_DISC_LTD                   0x01
    #define LT_BLE_ADV_F_DISC_GEN                   0x02
    #define LT_BLE_ADV_F_BREDR_UNSUP                0x04

    LTBLeUuid16 *uuids16;
    u8 num_uuids16;

    LTBLeUuid128 *uuids128;
    u8 num_uuids128;

    const char *name;

    s8 tx_pwr_lvl;
    #define LT_BLE_ADV_TX_PWR_LVL_AUTO (-128)
    u8 tx_pwr_lvl_is_present:1;

    u8 *mfg_data;
    u8 mfg_data_len;

    u8 *svc_data_uuid16;
    u8 svc_data_uuid16_len;

    u8 *svc_data_uuid128;
    u8 svc_data_uuid128_len;
} LTBleAdvFields;

// Advertising parameters
typedef struct LTBleAdvParams {
    // Advertising mode. Can be one of following constants
    #define LT_BLE_CONN_MODE_NON               0 // non-connectable
    #define LT_BLE_CONN_MODE_DIR               1 // directed-connectable
    #define LT_BLE_CONN_MODE_UND               2 // undirected-connectable
    u8 conn_mode;

    // Discoverable mode. Can be one of following constants
    #define LT_BLE_DISC_MODE_NON               0 // non-discoverable
    #define LT_BLE_DISC_MODE_LTD               1 // limited-discoverable
    #define LT_BLE_DISC_MODE_GEN               2 // general-discoverabl
    u8 disc_mode;

    // Minimum advertising interval, if 0 default value is used. In units of 625ms
    u16 itvl_min;
    // Maximum advertising interval, if 0 default value is used. In units of 625ms
    u16 itvl_max;

    //  If set, use High Duty cycle for Directed Advertising
    u8 high_duty_cycle:1;

    // The duration of the advertisement procedure.
    // On expiration, the procedure ends and kLTDeviceBle_Event_Advertise_Complete
    // event is posted. Units are milliseconds.
    // Specify LT_INT_MAX for no expiration.
    s32 duration_ms;
    #define LT_BLE_ADV_FOREVER                 LT_INT_MAX
} LTBleAdvParams;

typedef struct LTBleUpdConnParams {
    // Minimum value for connection interval in 1.25ms units
    u16 itvl_min;

    // Maximum value for connection interval in 1.25ms units
    u16 itvl_max;

    // Connection latency
    u16 latency;

    // Supervision timeout in 10ms units
    u16 supervision_timeout;

    // Minimum connection event length in 1.25ms units
    u16 min_ce_len;

    // Maximum connection event length in 1.25ms units
    u16 max_ce_len;
} LTBleUpdConnParams;

// Represents a ble device event related data.  When such an event occurs, the LTDeviceBLE
// notifies the application by passing an instance of this structure to registered
// callback.
typedef struct LTBleEventData {
    // A discriminated union containing additional details concerning this event.
    union {
        // Posted when there is change in a peer's subscription status.  In this
        // comment, the term "update" is used to refer to either a notification
        // or an indication.  This event is triggered by any of the following
        // occurrences:
        //     o Peer enables or disables updates.
        //     o Connection is about to be terminated and the peer is
        //       subscribed to updates.
        //
        // Valid for the kLTDeviceBle_Event_Subscribe event type
        struct {
            // Whether the peer is currently subscribed to notifications.
            u8 cur_notify:1;

            // Whether the peer is currently subscribed to indications.
            u8 cur_indicate:1;

            // Handle for Characteristic that is (un)subscribed
            LTBleChrHandle hChr;
        } subscribe;

        // Posted when pairing attempt completes with the peer device.
        // This event is triggered by any of the following occurrences:
        //     o Paired successfully with peer device
        //     o Pairing attempt failed.
        //
        // Valid for the kLTDeviceBle_Event_Paired event type
        struct {
            bool pairing_status;
        } pair;
    };
} LTBleEventData;

/**
 * @brief The function prototype for ble-related events (async callback)
 *
 * @param[in] handle        Generic handle
 * @param[in] event         Ble event that triggered this callback
 * @param[in] bleEventData  Ble event related data from ble stack
 * @param[in] data          Client data for this event
 */
typedef void (*LTBleEventProc)(LTHandle handle, u32 event, void * bleEventData, void * data);

// ble mode
typedef enum LTDeviceBle_Mode {
    kLTDeviceBle_Mode_Server = 0,      ///< server mode
    kLTDeviceBle_Mode_Client = 1,      ///< client mode
} LTDeviceBle_Mode;

typedef enum LTDeviceBle_Event {
    kLTDeviceBle_Event_Started               = 1,
    kLTDeviceBle_Event_Advertise_Complete    = 2,
    kLTDeviceBle_Event_Connected             = 3,
    kLTDeviceBle_Event_Disconnected          = 4,
    kLTDeviceBle_Event_Write_Done            = 5,
    kLTDeviceBle_Event_Read_Done             = 6,
    kLTDeviceBle_Event_Conn_Update_Failed    = 7,
    kLTDeviceBle_Event_Subscribe             = 8,
    kLTDeviceBle_Event_Paired                = 9,
    kLTDeviceBle_Event_Conn_Update_Complete  = 10,
    kLTDeviceBle_Event_Nimble_DirAdv_fail    = 11,
} LTDeviceBle_Event;

typedef enum LTDeviceBle_State {
    kLTDeviceBle_State_Off         = 0,    ///< off
    kLTDeviceBle_State_Started     = 1,    ///< started
    kLTDeviceBle_State_Advertising = 2,    ///< advertise
    kLTDeviceBle_State_Connected   = 3,    ///< connected
    kLTDeviceBle_State_ConnectedAdvertising = 4, ///< connected and non-connectable, broadcast advertising
} LTDeviceBle_State;

/*******************************************************************************
 * Bluetooth Device API
 ******************************************************************************/

typedef void (*LTBleStatusProc)(u32 event, void *data);

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceBle, 1);

/**
 *  LT Device interface for Bluetooth LE.
 */
struct LTDeviceBle {

    INHERIT_DEVICE_LIBRARY_BASE

    /**************************************************************************
    ** Bluetooth Device API
    ***************************************************************************/
    bool (*SetService)(LTBleSvcHandle hSvc);
    /**<
     * @brief Set Service and Characteristics data for BLE. Only 1 service with 
     * multiple characteristics is supported.
     * @param[in] hSvc          Service handle
     * @retval true   success
     * @retval false  fail
     */

    bool (*Start)(LTBleDeviceHandle hDevice);
    /**<
     * @brief Set the local name and manufacturer data. Used in advertising packets.
     * @param[in] hDevice           Device handle
     * @retval true   success
     * @retval false  fail
     */

    void (*Stop)(void);
    /**<
      * @brief Stops the BLE.
      */

    bool (*StartAdvertise)(LTBleDeviceHandle hDevice, LTBleAdvFields* advFields, LTBleAdvFields* rspFields, LTBleAdvParams* advParam);
    /**<
     * @brief  Start or update advertising.
     * @param[in] hDevice      Device handle
     * @param[in] advFields    advertisement payload fields
     * @param[in] rspFields    scan response fields (optional, can be null)
     * @param[in] advParam     advertisement paramsters
     */

    bool (*StartDirectedAdvertise)(LTBleDeviceHandle hDevice, LTMacAddress peer);
    /**<
     * @brief  Start directed advertising.
     *         It's directed towards currently paired peer device to which app wants to reconnect.
     *         It doesn't contain any adv params.
     * @param[in] hDevice      Device handle
     * @param[in] peer         Peer macaddr
     */

    void (*StopAdvertise)(void);
    /**<
     * @brief  Stop advertising.
     */

    bool (*Indicate)(LTBleDeviceHandle hDevice, LTBleChrHandle hChr);
    /**<
     * @brief  Indicate on a characteristic
     * @retval true   success
     * @retval false  fail
     */

    bool (*Notify)(LTBleDeviceHandle hDevice, LTBleChrHandle hChr);
    /**<
     * @brief  Notify on a characteristic
     * @retval true   success
     * @retval false  fail
     */

    void (*GetOwnAddress)(LTMacAddress *own);
    /**<
     * @brief Get own MAC address
     *
     */

    void (*GetPeerAddress)(LTMacAddress *peer);
    /**<
     * @brief Get peer MAC address
     * 
     */

    s8 (*GetRSSI)(u16 connHandle, s8* rssi);
    /**<
     * @brief Get Signal Strength
     * 
     */

    void (*OnStatusChange)(LTBleStatusProc proc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData);
    void (*NoStatusChange)(LTBleStatusProc proc);
    LTDeviceBle_State (*GetState)(void);
    /**<
     * @brief Get BLE Controller state
     * 
     */

    bool (*SendPrivVenCommand)(const char *cmd, u8* resp, u32 respLen);
    /**<
     * @brief SendPrivVenCommand will be sending the private vendor command.
     * 
     */

    bool (*SendHciCommand)(const u16 opcode, const u8 *cmd, u32 len,  u8* resp, u32 respLen);
    /**<
     * @brief SendHciCommand will be sending the hci command.
     * 
     */

    void (*EnableLogging)(bool en);
    /**
     * @brief Enable logging at the common nimble layer
     * 
     */

    const char* (*GetDeviceName)(void);
    /**
     * @brief Get Avertisement name
     * 
     */

    bool (*SetMaxTransmitParams)(LTBleDeviceCtx *devCtx, u16 maxTxOctets, u16 maxTxTime);
    /**
     * @brief Set the maximum transmit params for the connection.
     * @param[in] devCtx       Device context
     * @param[in] maxTxOctets  Maximum number of bytes BLE hw can send on a single Link Layer Data Channel PDU
     * @param[in] maxTxTime    Maximum time BLE hw can use for a single Link Layer Data Channel PDU
     * @retval true   success
     * @retval false  fail
     */

    bool (*UpdateConnectionParams)(LTBleDeviceHandle hDevice, LTBleUpdConnParams* updConnParams);
    /**<
     * @brief Update the connection parameters.
     * @param[in] hDevice        Device handle
     * @param[in] updConnParams  connection parameters to update
     * @retval true   success
     * @retval false  fail
     */

    bool (*SecurityInitiate)(LTBleDeviceHandle hDevice);
    /**<
     * @brief Upon connection, application can call this to initiate security procedure.
     * @param[in] hDevice  Device handle
     * @retval true   success
     * @retval false  fail
     */

    bool (*Disconnect)(LTBleDeviceHandle hDevice);
    /**<
     * @brief To terminate an established connection.
     * @param[in] hDevice  Device handle
     * @retval true   success
     * @retval false  fail
     */

    bool (*Reset)(s16 reason);
    /**<
     * @brief Reset the BLE Controller.
     * @param[in] reason  Reset reason code
     * @retval true   success
     * @retval false  fail
     */


    s16 (*GetControllerResetReason)(void);
    /**<
     * @brief Get reason for last Controller reset.
     * @retval reset reason code
     */

    bool (*GetConnectionInfo)(u16 connHandle, LTBleConnectionInfo *info);
    /**<
     * @brief Get BLE connection statistics.
     * @param[in] connHandle  Connection handle
     * @param[out] info       Pointer to structure to fill with connection info
     * @retval true on success, false if not supported or error
     */

    bool (*RegisterPktTimestamp)(u16 att_handle, u16 tag_offset, u8 tag_len,
                                 LTBlePktTimestampCb cb, void *userData);
    /**<
     * @brief Register for packet-level TX/RX timestamp callbacks.
     * @param[in] att_handle   ATT attribute handle to monitor
     * @param[in] tag_offset   Byte offset in packet to copy tag bytes from
     * @param[in] tag_len      Number of tag bytes to copy (max 4)
     * @param[in] cb           Callback invoked on the callers thread
     * @param[in] userData     Opaque pointer passed through to callback
     * @retval true on success, false if not supported
     */

    bool (*UnregisterPktTimestamp)(u16 att_handle);
    /**<
     * @brief Unregister packet timestamp callback.
     * @param[in] att_handle   ATT attribute handle to stop monitoring
     * @retval true on success, false if not supported
     */
};

/*******************************************************************************
 * BLE host driver library interface (ILTHostBle)
*******************************************************************************/
typedef_LTLIBRARY_INTERFACE(ILTHostBle, 1) {
    bool (*SetService) (LTBleSvcHandle hSvc);
    bool (*Start)(LTBleDeviceHandle hDevice);
    void (*Stop)(void);
    bool (*StartAdvertise)(LTBleDeviceHandle hDevice, LTBleAdvFields* advFields, LTBleAdvFields* rspFields, LTBleAdvParams* advParam);
    bool (*StartDirectedAdvertise)(LTBleDeviceHandle hDevice, LTMacAddress peer);
    void (*StopAdvertise)(void);
    bool (*Indicate)(LTBleDeviceHandle hDevice, LTBleChrHandle hChr);
    bool (*Notify)(LTBleDeviceHandle hDevice, LTBleChrHandle hChr);
    bool (*SetMaxTransmitParams)(LTBleDeviceCtx *devCtx, u16 maxTxOctets, u16 maxTxTime);
    bool (*UpdateConnectionParams)(LTBleDeviceHandle hDevice, LTBleUpdConnParams* updConnParams);
    LTDeviceBle_State (*GetState)(void);
    void (*GetOwnAddress)(LTMacAddress *own);
    void (*GetPeerAddress)(LTMacAddress *peer);
    s8   (*GetRSSI)(u16 connHandle, s8* rssi);
    bool (*SendPrivVenCommand)(const char *cmd, u8* resp, u32 respLen);
    bool (*SendHciCommand)(const u16 opcode, const u8 *cmd, u32 len,  u8* resp, u32 respLen);
    void (*EnableLogging)(bool en);
    const char* (*GetDeviceName)(void);
    bool (*SecurityInitiate)(LTBleDeviceHandle hDevice);
    bool (*Disconnect)(LTBleDeviceHandle hDevice);
    bool (*Reset)(s16 reason);
    s16  (*GetControllerResetReason)(void);
    bool (*GetConnectionInfo)(u16 connHandle, LTBleConnectionInfo *info);
    bool (*RegisterPktTimestamp)(u16 att_handle, u16 tag_offset, u8 tag_len,
                                 LTBlePktTimestampCb cb, void *userData);
    bool (*UnregisterPktTimestamp)(u16 att_handle);
} LTLIBRARY_INTERFACE;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_DEVICE_BLE_LTDEVICEBLE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Sep-22   vespasian   created
 *  21-Dec-22   gallienus   restructured and cleaned
 */
