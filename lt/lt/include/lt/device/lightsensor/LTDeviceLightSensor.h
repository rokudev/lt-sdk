/*******************************************************************************
 * lt/device/lightsensor/LTDeviceLightSensor.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_LIGHTSENSOR_LTDEVICELIGHTSENSOR_H
#define ROKU_LT_INCLUDE_LT_DEVICE_LIGHTSENSOR_LTDEVICELIGHTSENSOR_H
#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

enum {
    kLTDeviceLightSensor_IlluminanceValue_Min = 0,
    /* 32 bits in millilux comes out to over 4 million lux, an order of magnitude above direct sunlight */
    kLTDeviceLightSensor_IlluminanceValue_Max = LT_U32_MAX,
};

/* Light value in millilux ranging from kLTDeviceLightSensor_IlluminanceValue_Min to kLTDeviceLightSensor_IlluminanceValue_Max */
typedef u32 IlluminanceValue;

enum {
    kLTDeviceLightSensor_Channel_Visible = (1 << 0), /* Visible light (white) */
    kLTDeviceLightSensor_Channel_IR      = (1 << 1), /* Infrared light */
    kLTDeviceLightSensor_Channel_Red     = (1 << 2), /* Red visible */
    kLTDeviceLightSensor_Channel_Green   = (1 << 3), /* Green visible */
    kLTDeviceLightSensor_Channel_Blue    = (1 << 4), /* Blue visible */
};

typedef_LTObject(LTDeviceLightSensor, 1) {
    u16 (*GetSupportedChannels)(LTDeviceLightSensor *lightSensor);
    /**< Get the light channels supported by this light sensor
     *
     * @param[in]  lightSensor a pointer to the LTDeviceLightSensor object
     * @retval one or more kLTDeviceLightSensor_Channel flags
     */

    bool (*GetChannelValue)(LTDeviceLightSensor *lightSensor, u16 channel, IlluminanceValue *value);
    /**< Get the light info for the specified channel from the light sensor
     *
     * @param[in]  lightSensor a pointer to the LTDeviceLightSensor object
     * @param[in]  channel a single supported kLTDeviceLightSensor_Channel flag to measure
     * @param[out] value pointer to the buffer to store the output value
     * @retval true if successfully get light value info and false otherwise
     */

     bool (*SetIntegrationTime)(LTDeviceLightSensor *lightSensor, LTTime integrationTime);
    /**< Set the desired integration time for the light sensor.
     *
     *  If the requested time is outside the supported range, no change will happen and the
     *  function will return false. If the requested integration time is within the supported range,
     *  but is not supported exactly, the closest *slower* integration time will be used to try to
     *  maintain precision.
     *
     * @param[in]  lightSensor a pointer to the LTDeviceLightSensor object
     * @param[in]  integrationTime the desired integration time for the light sensor
     * @retval true if the integration time was successfully set, false otherwise
     */

} LTOBJECT_API;

typedef_LTLIBRARY_INTERFACE(ILTDriverLightSensor, 1) {
    u16 (*GetSupportedChannels)(void);
    /**< Get the light channels supported by this light sensor
     *
     * @retval one or more kLTDeviceLightSensor_Channel flags
     */

    bool (*GetChannelValue)(u16 channel, IlluminanceValue *value);
    /**< Get the light info for the specified channel from the light sensor
     *
     * @param[in]  channel a single supported kLTDeviceLightSensor_Channel flag to measure
     * @param[out] value pointer to the buffer to store the output value
     * @retval true if successfully get light value info and false otherwise
     */

    bool (*SetIntegrationTime)(LTTime integrationTime);
    /**< Set the desired integration time for the light sensor.
     *
     *  If the requested time is outside the supported range, no change will happen and the
     *  function will return false. If the requested integration time is within the supported range,
     *  but is not supported exactly, the closest *slower* integration time will be used to try to
     *  maintain precision.
     *
     * @param[in]  lightSensor a pointer to the LTDeviceLightSensor object
     * @param[in]  integrationTime the desired integration time for the light sensor
     * @retval true if the integration time was successfully set, false otherwise
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_DEVICE_LIGHTSENSOR_LTDEVICELIGHTSENSOR_H
