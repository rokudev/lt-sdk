/******************************************************************************
 * <lt/device/floodlightaccessory/LTDeviceFloodlightAccessory.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/
/**
 * @defgroup ltdevice_floodlightaccessory LTDeviceFloodlightAccessory
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for Floodlight Accessory.
 */

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHTACCESSORY_LTDEVICEFLOODLIGHTACCESSORY_H
#define ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHTACCESSORY_LTDEVICEFLOODLIGHTACCESSORY_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef_LTENUM_SIZED(LTDeviceFloodlightAccessory_Endpoint, u8) {
    kLTDeviceFloodlightAccessory_Endpoint_PIR = 0,
    kLTDeviceFloodlightAccessory_Endpoint_Floodlight,
    kLTDeviceFloodlightAccessory_Endpoint_System
};

typedef_LTENUM_SIZED(LTDeviceFloodlightAccessory_Setting, u8) {
    kLTDeviceFloodlightAccessory_Setting_Brightness = 1,
    kLTDeviceFloodlightAccessory_Setting_FlashFrequency,
    kLTDeviceFloodlightAccessory_Setting_FadeTime,
    kLTDeviceFloodlightAccessory_Setting_AccomodationTime,
    kLTDeviceFloodlightAccessory_Setting_Sensitivity,
    kLTDeviceFloodlightAccessory_Setting_Zones,
    kLTDeviceFloodlightAccessory_Setting_CooldownTime,
    kLTDeviceFloodlightAccessory_Setting_Algo,
    kLTDeviceFloodlightAccessory_Setting_BootReason,
    kLTDeviceFloodlightAccessory_Setting_Uptime,
    kLTDeviceFloodlightAccessory_Setting_Restart,
    kLTDeviceFloodlightAccessory_Setting_FWupdate,
    kLTDeviceFloodlightAccessory_Setting_Ping,
    kLTDeviceFloodlightAccessory_Setting_LogLevel,
    kLTDeviceFloodlightAccessory_Setting_Authenticate,
    kLTDeviceFloodlightAccessory_Setting_Handshake,
    kLTDeviceFloodlightAccessory_Setting_FwVersion,
    kLTDeviceFloodlightAccessory_Setting_HwVersion,
    kLTDeviceFloodlightAccessory_Setting_MacId,
    kLTDeviceFloodlightAccessory_Setting_DeviceType,
    kLTDeviceFloodlightAccessory_Setting_EncryptedKey,
    kLTDeviceFloodlightAccessory_Setting_WatchdogTimer
};

// Events triggers from floodlight accessory Device
typedef_LTENUM_SIZED(LTDeviceFloodlightAccessory_Event, u32) {
    kLTDeviceFloodlightAccessory_Event_Idle = 0x0101,
    kLTDeviceFloodlightAccessory_Event_PirTrigger,
    kLTDeviceFloodlightAccessory_Event_PhotoSensorTrigger,
    kLTDeviceFloodlightAccessory_Event_BrightnessChange,
    kLTDeviceFloodlightAccessory_Event_Overheated,
    kLTDeviceFloodlightAccessory_Event_Connected,
    kLTDeviceFloodlightAccessory_Event_Disconnected,
};

/**<
 * @brief The function prototype for floodlight Device events (async callback),
 *          exposed to any app/upper layer that will call it.
 * @param[in] event       triggered event type
 * @param[in] eventData   data from driver specific to the event
 * @param[in] pClientData client data for this event
 */
typedef void (*LTDeviceFloodlightAccessoryEventProc)(LTDeviceFloodlightAccessory_Event event, void *eventData, void *pClientData);

/**
 *  LT Device interface for Floodlight Accessory.
 */
typedef_LTObject(LTDeviceFloodlightAccessory, 1) {
    /**************************************************************************
     ** FloodLight Accessory Device API
     ***************************************************************************/
    bool (*SetCommand)(LTDeviceFloodlightAccessory_Endpoint endpoint, LTDeviceFloodlightAccessory_Setting setting, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send any LTDeviceFloodlightAccessory_Setting for an LTDeviceFloodlightAccessory_Endpoint
     *        to write data into floodlight accessory
     * @param[in] endpoint   the endpoint of floodlight accessory intended
     * @param[in] setting    the data to set to setting
     * @param[in] data       pointer to data to set
     * @param[in] dataLength length of the data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*GetCommand)(LTDeviceFloodlightAccessory_Endpoint endpoint, LTDeviceFloodlightAccessory_Setting setting, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send any LTDeviceFloodlightAccessory_Setting for an LTDeviceFloodlightAccessory_Endpoint
     *        to get data from floodlight accessory
     * @param[in]  endpoint   the endpoint of floodlight accessory
     * @param[in]  setting    the data to get of setting
     * @param[out] data       pointer to data to get
     * @param[in]  dataLength length of the data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*IsMounted)(void);
    /**<
     * @brief Function to get status of floodlight accessory port mount
     * @retval    true   if floodlight accessory serial port is opened and false otherwise
     */

    void (*OnConnectionChange)(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData);
    /**<
     * @brief Registration of connection event listener
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoConnectionChange)(LTDeviceFloodlightAccessoryEventProc proc);
    /**<
     * @brief De-Registration of connection event listener
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */

    void (*OnPIRTrigger)(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData);
    /**<
     * @brief Registration of PIR specific events listener
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoPIRTrigger)(LTDeviceFloodlightAccessoryEventProc proc);
    /**<
     * @brief De-Registration of PIR events listener
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */

    void (*OnFloodlightBrightnessChange)(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData);
    /**<
     * @brief Registration of floodlight brightness change event listener
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoFloodlightBrightnessChange)(LTDeviceFloodlightAccessoryEventProc proc);
    /**<
     * @brief De-Registration of floodlight brightness change event listener
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */

    void (*OnFloodlightOverheatedState)(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData);
    /**<
     * @brief Registration of floodlight overheat event listener
     * @param[in] proc        callback function
     * @param[in] pClientData client data for this event
     */

    bool (*NoFloodlightOverheatedState)(LTDeviceFloodlightAccessoryEventProc proc);
    /**<
     * @brief De-Registration of floodlight overheat event listener
     * @param[in] proc callback function
     * @retval true  success
     * @retval false fail
     */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif  // #ifndef ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHTACCESSORY_LTDEVICEFLOODLIGHTACCESSORY_H
