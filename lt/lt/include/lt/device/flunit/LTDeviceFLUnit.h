/******************************************************************************
 * <lt/device/flunit/LTDeviceFLUnit.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/
/**
 * @defgroup ltdevice_flunit LTDeviceFLUnit
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for Floodlight Accessory Unit.
 */

#ifndef LT_INCLUDE_LT_DEVICE_FLUNIT_LTDEVICEFLUNIT_H
#define LT_INCLUDE_LT_DEVICE_FLUNIT_LTDEVICEFLUNIT_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

// Command values as per FL's protocol doc
typedef enum {
    LT_FL_CMD_GET_BRIGHTNESS_PARAMETER          = 0x44,
    LT_FL_CMD_GET_BRIGHTNESS_PARAMETER_RESP     = 0x45,
    LT_FL_CMD_SET_BRIGHTNESS_PARAMETER          = 0x46,
    LT_FL_CMD_SET_BRIGHTNESS_PARAMETER_RESP     = 0x47,
    LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE          = 0x48,
    LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE_RESP     = 0x49,
    LT_FL_CMD_GET_LIGHT_TOTAL_WORKING_TIME      = 0x4A,
    LT_FL_CMD_GET_LIGHT_TOTAL_WORKING_TIME_RESP = 0x4B,
    LT_FL_CMD_GET_PIR_TRIGGER_STATE             = 0x4C,
    LT_FL_CMD_GET_PIR_TRIGGER_STATE_RESP        = 0x4D,
    LT_FL_CMD_GET_POWER_AMPLIFIER_MODE          = 0x4E,
    LT_FL_CMD_GET_POWER_AMPLIFIER_MODE_RESP     = 0x4F,
    LT_FL_CMD_SET_POWER_AMPLIFIER_MODE          = 0x50,
    LT_FL_CMD_SET_POWER_AMPLIFIER_MODE_RESP     = 0x51,
    LT_FL_CMD_FILL_LIGHT_BRIGHTNESS_CHANGE      = 0x52,
    LT_FL_CMD_FILL_LIGHT_BRIGHTNESS_CHANGE_RESP = 0x53,
    LT_FL_CMD_GET_PIR_PARAMETER                 = 0x58,
    LT_FL_CMD_GET_PIR_PARAMETER_RESP            = 0x59,
    LT_FL_CMD_SET_PIR_PARAMETER                 = 0x5A,
    LT_FL_CMD_SET_PIR_PARAMETER_RESP            = 0x5B,
    LT_FL_CMD_GET_PIR_SILENT_TIME               = 0x5C,
    LT_FL_CMD_GET_PIR_SILENT_TIME_RESP          = 0x5D,
    LT_FL_CMD_SET_PIR_SILENT_TIME               = 0x5E,
    LT_FL_CMD_SET_PIR_SILENT_TIME_RESP          = 0x5F,
    LT_FL_CMD_GET_LIGHT_ON_OFF                  = 0x60,
    LT_FL_CMD_GET_LIGHT_ON_OFF_RESP             = 0x61,
    LT_FL_CMD_GET_SCM_RESTART_REASON            = 0x6C,
    LT_FL_CMD_GET_SCM_RESTART_REASON_RESP       = 0x6D,
    LT_FL_CMD_GET_PIR_TRIGGER_INFORMATION       = 0x96,
    LT_FL_CMD_GET_PIR_TRIGGER_INFORMATION_RESP  = 0x97,
} LTDriverFLUnitCmdType;

// Events triggers from FL Device
typedef enum {
    kLTDriverFLUnitEvent_Idle = 0x0101,
    kLTDriverFLUnitEvent_PirTrigger,
    kLTDriverFLUnitEvent_PhotoSensorTrigger,
    kLTDriverFLUnitEvent_Connected,
    kLTDriverFLUnitEvent_Disconnected,
} LTDriverFLUnitEvent;

/**<
 * @brief The function prototype for FL Driver events (async callback),
 *  *        only exposed to LTDeviceFLUnit.
 *  *
 * @param[in] event     triggered event type
 * @param[in] eventData data from driver specific to the event
 * @param[in] data      Client data for this event
 */
typedef void (*LTDriverFLUnitEventProc)(LTDriverFLUnitEvent event, void *eventData, void *clientData);

/**<
 * @brief The function prototype for FL Device events (async callback),
 *          exposed to any app/upper layer that will call it.
 *
 * @param[in] event     triggered event type
 * @param[in] eventData data from driver specific to the event
 * @param[in] data      client data for this event
 */
typedef void (*LTDeviceFLUnitEventProc)(LTDriverFLUnitEvent event, void *eventData, void *clientData);

/**
 *  LT Driver interface for Floodlight Accessory Unit.
 */
typedef_LTLIBRARY_INTERFACE(ILTDriverFLUnit, 1) {
    bool (*Start)(void);
    /**<
     * @brief Start thread for polling change in the floodlight status
     * @retval true  success
     * @retval false fail
     */

    bool (*SetCommand)(u8 cmdType, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send command for which data needs to set to FL
     * @param[in] cmdType    command value as per LTDriverFLUnitCmdType
     * @param[in] data       pointer to data needs to be set
     * @param[in] dataLength length of the data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*GetCommand)(u8 cmdType, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send command for which data needs to get from FL
     * @param[in] cmdType    command value as per LTDriverFLUnitCmdType
     * @param[in] data       pointer to buffer needs to be return response on
     * @param[in] dataLength length of expected data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*SetGetCommand)(u8 cmdType, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send command for which data needs to get data on behalf
     *         of the data that is transferred
     * @param[in]      cmdType    command value as per LTDriverFLUnitCmdType
     * @param[in][out] data       pointer to buffer needs to be return response on
     * @param[in]      dataLength length of expected data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*SetupDevice)(void);
    /**<
     * @brief Function to check access to and open FL USB port  
     * @retval true   if successfully opens FL USB port
     * @retval false  if fails to open FL USB port
     */

    void (*OnStatusChange)(LTDriverFLUnitEventProc proc, void *clientData);
    /**<
     * @brief Function to register callback for the events from FL
     * @param[in] proc       callback to register
     * @param[in] clientData Client data for this event
     */

    bool (*NoStatusChange)(LTDriverFLUnitEventProc proc);
    /**<
     * @brief Function to de-register callback for the events from FL
     * @param[in] proc callback to de-register
     * @retval true  success
     * @retval false fail
     */
}
LTLIBRARY_INTERFACE;

/**
 *  LT Device interface for Floodlight Accessory Unit.
 */
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFLUnit, 1) {
    /**************************************************************************
     ** FloodLight Accessory Device API
     ***************************************************************************/
    bool (*GetDeviceType)(LTHandle *flunit, u8 *type);
    /**<
     * @brief Function to get device type of an accessory (currently FL is an accessory
     *          with device type 0x43)
     * @param[in]  flunit FL device handle
     * @param[out] type   pointer to buffer
     * @retval true  success
     * @retval false fail
     */

    bool (*GetMacID)(LTHandle *flunit, u8 *macId);
    /**<
     * @brief Get mac address of FL device
     * @param[in]  flunit FL device handle
     * @param[out] macId  pointer to buffer of type u64
     * @retval true  success
     * @retval false fail
     */

    bool (*AuthenticateDevice)(LTHandle *flunit);
    /**<
     * @brief Send authenticate success to FL device
     * @param[in] flunit FL device handle
     * @retval true  success
     * @retval false fail
     */

    bool (*FirmwareVersion)(LTHandle *flunit, u32 *version);
    /**<
     * @brief Get firmware version of FL device
     * @param[in]  flunit  FL device handle
     * @param[out] version pointer to u32 var
     * @retval true  success
     * @retval false fail
     */

    bool (*HardwareVersion)(LTHandle *flunit, u32 *version);
    /**<
     * @brief Get hardware version of FL device
     * @param[in]  flunit  FL device handle
     * @param[out] version pointer to u32 var
     * @retval true  success
     * @retval false fail
     */

    bool (*GetEncryptedKey)(LTHandle *flunit, u8 *encryptedKey);
    /**<
     * @brief Get 16 byte encrypted key against some 16 byte random number
     * @param[in]      flunit       FL device handle
     * @param[in][out] encryptedKey pointer to 16 byte random number
     * @retval true  success
     * @retval false fail
     */

    bool (*GetWatchdogTimer)(LTHandle *flunit, u16 *watchdogTimer);
    /**<
     * @brief Get watchdog reset time
     * @param[in]  flunit        FL device handle
     * @param[out] watchdogTimer pointer to u16 var
     * @retval true  success
     * @retval false fail
     */

    bool (*SetWatchdogTimer)(LTHandle *flunit, u16 watchdogTimer);
    /**<
     * @brief Set watchdog reset time
     * @param[in]  flunit        FL device handle
     * @param[out] watchdogTimer pointer to u16 var
     * @retval true   success
     * @retval false  fail
     */

    bool (*SCMRestart)(LTHandle *flunit);
    /**<
     * @brief Actively restart the FL device
     * @param[in] flunit FL device handle
     * @retval true  success
     * @retval false fail
     */

    bool (*SCMRestartReason)(LTHandle *flunit, u8 *reason);
    /**<
     * @brief Get SCM restart reason of FL device
              Single-chip Microcomputer Reset/Restart Reason
                1 Electric reset
                2 Software reset
                3 Hardware watchdog reset
                4 Pin external input is reset
                5 Software watchdog (USB communication heartbeat) reset
     * @param[in]  flunit FL device handle
     * @param[out] reason pointer to u8 var
     * @retval true  success
     * @retval false fail
     */

    bool (*SetCommand)(LTHandle *flunit, LTDriverFLUnitCmdType cmdType, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send any feature specific command such PIR, Flash On/Off,
     *         Photosensor etc. to write into FL device
     * @param[in] flunit     FL device handle
     * @param[in] cmdType    command number
     * @param[in] data       pointer to data to set
     * @param[in] dataLength data length
     * @retval true  success
     * @retval false fail
     */

    bool (*GetCommand)(LTHandle *flunit, LTDriverFLUnitCmdType cmdType, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send any feature specific command such PIR, Flash On/Off,
     *         Photosensor etc. to get data from FL device
     * @param[in]  flunit     FL device handle
     * @param[in]  cmdType    command number
     * @param[out] data       pointer to data to get
     * @param[in]  dataLength expected data length
     * @retval true  success
     * @retval false fail
     */

    bool (*IsMounted)(LTHandle *flunit);
    /**<
     * @brief Function to get status of flag FL port mount
     * @param[in] flunit FL device handle
     * @retval    true   if FL port is opened and false otherwise
     */

    void (*OnConnectionChange)(LTHandle *flunit, LTDeviceFLUnitEventProc proc, void *pClientData);
    /**<
     * @brief Registration of connection event listener
     * @param[in] flunit      FL device handle
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoConnectionChange)(LTHandle *flunit, LTDeviceFLUnitEventProc proc);
    /**<
     * @brief De-Registration of connection event listener
     * @param[in] flunit FL device handle
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */

    void (*OnPIRTrigger)(LTHandle *flunit, LTDeviceFLUnitEventProc proc, void *pClientData);
    /**<
     * @brief Registration of PIR specific events listener
     * @param[in] flunit      FL device handle
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoPIRTrigger)(LTHandle *flunit, LTDeviceFLUnitEventProc proc);
    /**<
     * @brief De-Registration of PIR events listener
     * @param[in] flunit FL device handle
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */
}
LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif  // #ifndef LT_INCLUDE_LT_DEVICE_FLUNIT_LTDEVICEFLUNIT_H
