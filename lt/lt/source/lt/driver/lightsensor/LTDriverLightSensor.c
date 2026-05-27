/*******************************************************************************
 * lt/source/lt/driver/lightsensor/LTDriverLightSensor.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Linux LT Driver Library for Floodlight light sensor
 ******************************************************************************/
/** @file LTDriverLightSensor.c Implementation of Floodlight Light sensor
 */

#include <lt/core/LTCore.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>
#include <lt/device/lightsensor/LTDeviceLightSensor.h>

DEFINE_LTLOG_SECTION("ldrv.lightsensor");

typedef struct {
    u16 VisibleLight;
    u16 IRLight;
} LightSensorInfo;

static LTHandle s_handle;
static LTDeviceFLUnit *s_pFlUnit;

/*******************************************************************************
 *
 * static helpers
 *
 *******************************************************************************/

 static bool GetVisiblePhotoSensor(IlluminanceValue *lightSensorData) {
    LightSensorInfo lightInfo;
    if (!lightSensorData) return false;
    if (s_pFlUnit) {
        if (!s_pFlUnit->GetCommand(&s_handle,
                                   LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE,
                                   (u8 *)&lightInfo,
                                   sizeof(&lightInfo))) {
            LTLOG_YELLOWALERT("f.get.visi", "Failed to GetVisiblePhotoSensor");
            return false;
        }
        /* The device reports unspecified raw values from the photoresist,
         * so we cannot scale to millilux here!
         */
        *lightSensorData = lightInfo.VisibleLight;
        return true;
    }
    return false;
}

static bool GetIRPhotoSensor(IlluminanceValue *lightSensorData) {
    LightSensorInfo lightInfo;
    if (s_pFlUnit) {
        if (!s_pFlUnit->GetCommand(&s_handle,
                                   LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE,
                                   (u8 *)&lightInfo,
                                   sizeof(&lightInfo))) {
            LTLOG_YELLOWALERT("f.get.ir", "Failed to GetIRPhotoSensor");
            return false;
        }
        *lightSensorData = lightInfo.IRLight;
        return true;
    }
    return false;
}

/*******************************************************************************
 *
 * driver interface implementation
 *
 *******************************************************************************/

u16 LTDriverLightSensorImpl_GetSupportedChannels(void) {
    return kLTDeviceLightSensor_Channel_Visible | kLTDeviceLightSensor_Channel_IR;
}

bool LTDriverLightSensorImpl_GetChannelValue(u16 channel, IlluminanceValue *value) {
    if (!value) return false;

    switch (channel) {
        case kLTDeviceLightSensor_Channel_Visible:
            return GetVisiblePhotoSensor(value);
        case kLTDeviceLightSensor_Channel_IR:
            return GetIRPhotoSensor(value);
        default:
            return false;
    }
}

static void LTDriverLightSensorImpl_LibFini(void) {
    lt_destroyhandle(s_handle);
    lt_closelibrary(s_pFlUnit);
}

static bool LTDriverLightSensorImpl_LibInit(void) {
    s_pFlUnit = NULL;
    do {
        if (!(s_pFlUnit = lt_openlibrary(LTDeviceFLUnit))) {
            LTLOG_YELLOWALERT("f.lib.flunit", "Error opening lib LTDeviceFLUnit");
            break;
        }
        if (!(s_handle = s_pFlUnit->CreateDeviceUnitHandle(0))) {
            LTLOG_YELLOWALERT("f.handle", "Error creating device unit handle");
            break;
        }
        if(!s_pFlUnit->IsMounted(&s_handle)) {
            LTLOG_YELLOWALERT("f.mounted", "Photosensor device not found");
            break;
        }
        if (!s_pFlUnit->AuthenticateDevice(&s_handle)) {
            LTLOG_YELLOWALERT("f.auth", "Failed to authenticating device");
            break;
        }
        return true;
    } while (0);

    LTDriverLightSensorImpl_LibFini();
    return false;
}

/*___________________________________________________
 / DriverLightSensor library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverLightSensor, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverLightSensor) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverLightSensor) {
    .GetSupportedChannels = &LTDriverLightSensorImpl_GetSupportedChannels,
    .GetChannelValue      = &LTDriverLightSensorImpl_GetChannelValue
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverLightSensor, (ILTDriverLightSensor));
