/*******************************************************************************
 * lt/source/lt/device/lightsensor/LTDeviceLightSensor.c
 *
 * LT Device Library for ambient light sensors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/lightsensor/LTDeviceLightSensor.h>

DEFINE_LTLOG_SECTION("dev.lightsensor")

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceLightSensor, LTDeviceLightSensorImpl) {
    LTLibrary            *pDriverLibrary;
    ILTDriverLightSensor *iDriver;
} LTOBJECT_API;


static u16 LTDeviceLightSensorImpl_GetSupportedChannels(LTDeviceLightSensorImpl *lightSensor) {
    if (!lightSensor) {
        return false;
    }
    return lightSensor->iDriver->GetSupportedChannels();
}

static bool LTDeviceLightSensorImpl_GetChannelValue(LTDeviceLightSensorImpl *lightSensor, u16 channel, IlluminanceValue *value) {
    if (!lightSensor || !value) {
        return false;
    }
    return lightSensor->iDriver->GetChannelValue(channel, value);
}

static bool LTDeviceLightSensorImpl_SetIntegrationTime(LTDeviceLightSensorImpl *lightSensor, LTTime integrationTime) {
    if (!lightSensor) {
        return false;
    }
    if (LTTime_IsZero(integrationTime) || LTTime_IsInfinite(integrationTime)) {
        return false;
    }
    return lightSensor->iDriver->SetIntegrationTime(integrationTime);
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceLightSensorImpl_DestructObject(LTDeviceLightSensorImpl *lightSensor) {
    lt_closelibrary(lightSensor->pDriverLibrary);
}

static bool LTDeviceLightSensorImpl_ConstructObject(LTDeviceLightSensorImpl *lightSensor) {
    const char *driverName = NULL;
    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("f.devc.open", "could not open device config");
            break;
        }
        driverName = deviceConfig->GetDriverAt("LTDeviceLightSensor", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
            lt_closelibrary(deviceConfig);
            break;
        }
        lightSensor->pDriverLibrary = LT_GetCore()->OpenLibrary(driverName);
        // only close LTDeviceConfig once driverName is no longer needed, as it becomes invalid
        driverName = NULL;
        lt_closelibrary(deviceConfig);
        if (!lightSensor->pDriverLibrary) {
            LTLOG_YELLOWALERT("f.drv.lib", "could not open driver light sensor library");
            break;
        }
        lightSensor->iDriver        = (ILTDriverLightSensor *)
            lt_getlibraryinterface(ILTDriverLightSensor, lightSensor->pDriverLibrary);
        if (!lightSensor->iDriver) {
            LTLOG_YELLOWALERT("f.drv.iface", "could not get driver light sensor");
            break;
        }

        return true;
    } while (0);

    LTDeviceLightSensorImpl_DestructObject(lightSensor);
    return false;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceLightSensor, LTDeviceLightSensorImpl,
    GetSupportedChannels,
    GetChannelValue,
    SetIntegrationTime,
);

define_LTOBJECT_EXPORTLIBRARY(LTDeviceLightSensor, 1, LTDeviceLightSensorImpl);
