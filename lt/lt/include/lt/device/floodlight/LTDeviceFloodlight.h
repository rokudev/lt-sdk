/*******************************************************************************
 * lt/device/floodlight/LTDeviceFloodlight.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHT_LTDEVICEFLOODLIGHT_H
#define ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHT_LTDEVICEFLOODLIGHT_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/**
 * Current brightness is the temporary brightness level during adjustment/accommodation, 
 * before reaching the target/object brightness
 */
typedef enum {
    kLight_ObjectBrightness = true,
    kLight_CurrentBrightness = false,
} LightBrightnessType;

typedef_LTObject(LTDeviceFloodlight, 1) {
    bool (*SetObjectBrightness)(LTDeviceFloodlight *light, u32 brightness);
    /**< @brief Set object/target brightness at which light will glow
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[in] brightness  brightness value in percent (0-100%)
     *   @return true on success, false on failure
     */

    void (*GetObjectBrightness)(LTDeviceFloodlight *light, u32 *brightness);
    /**< @brief Get object/target brightness
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[out] brightness  brightness value in percent
     *   @return
     */

    bool (*GetCurrentBrightness)(LTDeviceFloodlight *light, u32 *brightness);
    /**< @brief Get current/transient brightness value at any point of time,
     *           even during brightness levels change
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[out] brightness  brightness value in percent
     *   @return true if success, false otherwise
     */

    bool (*SetFlashFrequency)(LTDeviceFloodlight *light, u32 flashFrequency);
    /**< @brief Set flash frequency of FL light in Hertz
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[in] flashFrequency  flash frequency value (0-10Hz)
     *   @return true  on success, false  on failure
     */

    bool (*GetFlashFrequency)(LTDeviceFloodlight *light, u32 *flashFrequency);
    /**< @brief Get flash frequency of FL light in Hertz
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[out] flashFrequency  flash frequency value (0-10Hz)
     *   @return  flash frequency value
     */

    bool (*SetAccommodationTime)(LTDeviceFloodlight *light, u32 accomodation);
    /**< @brief Set accommodation time, i.e, transient time between changes of
     *           brighntess levels
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[in] accommodation value in unit of 100 milliseconds
     *   @return true  on success, false  on failure
     */

    void (*GetAccommodationTime)(LTDeviceFloodlight *light, u32 *accomodationTime);
    /**< @brief Get accommodation time, i.e, transient time between changes of
     *           brighntess levels
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @param[out] accommodation value in unit of 100 milliseconds
     *   @return
     */

    bool (*LightOn)(LTDeviceFloodlight *light);
    /**< @brief Turn the FL light on. Brightness will can be set with SetObjectBrightness()
     *
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @return true  on success, false  on failure
     */

    bool (*LightOff)(LTDeviceFloodlight *light);
    /**< @brief Turn the FL light off.
     *
     *   @param[in] light  pointer to LTDeviceFloodlight object
     *   @return true  on success, false  on failure
     */
} LTOBJECT_API;

typedef_LTLIBRARY_INTERFACE(ILTDriverFloodlight, 1) {
    bool (*SetBrightness)(u32 brightness, LightBrightnessType type);
    /**< @brief Set light brightness at which light will glow
     *   @param[in] brightness  brightness value in percent (0-100%)
     *   @param[in] type  LightBrightnessType (Object brightness/ Current Brightness)
     *   @return true  on success, false  on failure
     */

    bool (*GetBrightness)(u32 *brightness, LightBrightnessType type);
    /**< @brief Get the light brightness
     *   @param[in] type  LightBrightnessType (Object brightness/ Current Brightness)
     *   @param[out] brightness brightness value in percent (0-100%)
     *   @return  brightness value in percent
     */

    bool (*SetFlashFrequency)(u32 flashFrequency);
    /**< @brief Set flash frequency of FL light in Hertz
     *   @param[in] flashFrequency  flash frequency value (0-10Hz)
     *   @return true  on success, false  on failure
     */

    bool (*GetFlashFrequency)(u32 *flashFrequency);
    /**< @brief Get flash frequency of FL light in Hertz
     *   @param[out] flashFrequency  flash frequency value
     *   @return true on success, false otherwise
     */

    bool (*SetAccomodationTime)(u32 accomodationTime);
    /**< @brief Set accommodation time, i.e, transient time between changes of
     *           brighntess levels
     *   @param[in] accommodation value in unit of 100 milliseconds
     *   @return true  on success, false  on failure
     */

    void (*GetAccomodationTime)(u32 *accomodationTim);
    /**< @brief Get accommodation time, i.e, transient time between changes of
     *           brighntess levels
     *   @param[out] accommodation value in unit of 100 milliseconds
     *   @return
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_DEVICE_FLOODLIGHT_LTDEVICEFLOODLIGHT_H