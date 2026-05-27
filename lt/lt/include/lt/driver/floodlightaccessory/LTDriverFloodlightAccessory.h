/******************************************************************************
 * <lt/driver/floodlightaccessory/LTDriverFloodlightAccessory.h>
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

#ifndef ROKU_LT_INCLUDE_LT_DRIVER_FLOODLIGHTACCESSORY_LTDRIVERFLOODLIGHTACCESSORY_H
#define ROKU_LT_INCLUDE_LT_DRIVER_FLOODLIGHTACCESSORY_LTDRIVERFLOODLIGHTACCESSORY_H

#include <lt/device/floodlightaccessory/LTDeviceFloodlightAccessory.h>

typedef void (*LTDriverFloodlightAccessoryEventProc)(LTDeviceFloodlightAccessory_Event event, void *eventData, void *clientData);
/**<
 * @brief The function prototype for floodlight Driver events (async callback),
 *        only exposed to LTDeviceFloodlightAccessory.
 * @param[in] event     triggered event type
 * @param[in] eventData data from driver specific to the event
 * @param[in] data      Client data for this event
 */

/**
 *  LT Driver interface for Floodlight Accessory.
 */
typedef_LTObject(LTDriverFloodlightAccessory, 1) {
    bool (*Start)(void);
    /**<
     * @brief Start thread for polling events in the floodlight accessory
     * @retval true  success
     * @retval false fail
     */

    bool (*SetCommand)(u8 endpoint, u8 setting, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send request to set data to setting of an endpoint of floodlight accessory
     * @param[in] endpoint   the endpoint of accessory intended setting fall under
     * @param[in] setting    setting the data need to be set to
     * @param[in] data       pointer to data to be set
     * @param[in] dataLength length of the data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*GetCommand)(u8 endpoint, u8 setting, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send request to get data of setting of an endpoint of floodlight accessory
     * @param[in] endpoint   the endpoint of accessory intended setting fall under
     * @param[in] setting    setting the data to be get from
     * @param[out] data      pointer to data to get
     * @param[in] dataLength length of expected data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*SetGetCommand)(u8 endpoint, u8 setting, u8 *data, u32 dataLength);
    /**<
     * @brief Function to send command for which data needs to get data on behalf of the data that is
     *        transferred
     * @param[in]      endpoint   the endpoint of accessory intended setting fall under
     * @param[in]      setting    setting the data to be get from
     * @param[in][out] data       pointer to buffer needs to be return response on
     * @param[in]      dataLength length of expected data (in bytes)
     * @retval true  success
     * @retval false fail
     */

    bool (*InitAccessory)(void);
    /**<
     * @brief Function to run floodlight accessory initialization sequence
     * @retval true   if successfully initialized the floodlight accessory
     * @retval false  if fails to initialize the floodlight accessory
     */

    bool (*SetupDevice)(void);
    /**<
     * @brief Function to check access to and open floodlight accessory serial port
     * @retval true   if successfully opens floodlight accessory serial port
     * @retval false  if fails to open floodlight accessory serial port
     */

    void (*OnStatusChange)(LTDriverFloodlightAccessoryEventProc proc, void *clientData);
    /**<
     * @brief Function to register callback for the events from floodlight accessory
     * @param[in] proc       Callback to register
     * @param[in] clientData Client data for this event
     */

    bool (*NoStatusChange)(LTDriverFloodlightAccessoryEventProc proc);
    /**<
     * @brief Function to de-register callback for the events from floodlight accessory
     * @param[in] proc Callback to de-register
     * @retval true  success
     * @retval false fail
     */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif  // #ifndef ROKU_LT_INCLUDE_LT_DRIVER_FLOODLIGHTACCESSORY_LTDRIVERFLOODLIGHTACCESSORY_H