/*******************************************************************************
 * lt/media/cameraexposure/LTCameraExposure.h
 *
 * LT Object for calculating camera exposure parameters based on ambient light
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_MEDIA_CAMERAEXPOSURE_LTCAMERAEXPOSURE_H
#define LT_INCLUDE_LT_MEDIA_CAMERAEXPOSURE_LTCAMERAEXPOSURE_H
#include <lt/device/lightsensor/LTDeviceLightSensor.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/LTObject.h>
LT_EXTERN_C_BEGIN

/**
 * Result codes for camera exposure operations
 */
typedef enum {
    kLTCameraExposure_Result_Success,
    kLTCameraExposure_Result_Failure,
    kLTCameraExposure_Result_NotInitialized,
} LTCameraExposureResult;

/**
 * Structure for camera exposure parameters
 * Focused only on essential exposure parameters
 */
typedef struct {
    u32 exposureTime;           /* Exposure time in microseconds */
    u32 analogGain;             /* Analog gain value (a_gain) */
    u32 digitalGain;            /* Digital gain value (d_gain) */
    u32 ispDGain;               /* ISP digital gain value (isp_d_gain) */
    bool requiresNightVision;   /* True if night vision is required for the current light level */
    bool artificialLight;       /* True if the ambient light is artificially lit (e.g. from a spotlight) */
} LTCameraExposureParams;

/**
 * Object Interface for LTCameraExposure
 */
typedef_LTObject(LTCameraExposure, 1) {
    /**
     * Save ambient light value to allow calculation of camera exposure parameters.
     *
     * @param lightValue Ambient light sensor value
     */
    void (*SetAmbientLightLevel)(IlluminanceValue lightValue);

    /**
     * Set lighting condition flags that can be used to initialise the camera sensor day/night mode
     *
     * @param artificialLight True if the ambient light is artificially lit (e.g. from a spotlight)
     * @param belowDuskThreshold True if the ambient light is below the dusk threshold
     * @param belowDarkThreshold True if the ambient light is below the dark threshold
     */
    void (*SetLightingConditionFlags)(bool artificialLight, bool belowDuskThreshold, bool belowDarkThreshold);

    /**
     * Set current night vision mode that can be used to initialise the camera sensor day/night mode
     *
     * @param nightVisionMode The current night vision mode
     * @param nightVisionConditions The conditions under which the night vision mode is applied
     */
    void (*SetNightVisionMode)(LTMediaNightVisionMode nightVisionMode, LTMediaNightVisionCondition nightVisionConditions);

    /**
     * Get night vision mode.
     *
     * @return The current night vision mode
     */
    LTMediaNightVisionMode (*GetNightVisionMode)(void);

    /**
     * Get the current camera exposure parameters.
     *
     * @param pParams Pointer to store for exposure parameters
     * @return Result code indicating success or failure
     */
    LTCameraExposureResult (*RetrieveExposureParams)(LTCameraExposureParams *pParams);

} LTOBJECT_API;

LT_EXTERN_C_END
#endif // LT_INCLUDE_LT_MEDIA_CAMERAEXPOSURE_LTCAMERAEXPOSURE_H
