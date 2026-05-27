/*******************************************************************************
 * lt/source/lt/device/floodlight/LTDeviceFloodlight.c
 *
 * LT Device Library for the floodlight light
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/floodlight/LTDeviceFloodlight.h>

DEFINE_LTLOG_SECTION("dev.floodlight")

enum {
    kMinBrightness = 0,
    kMaxBrightness = 100  /*percent*/
};

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceFloodlight, LTDeviceFloodlightImpl) {
    ILTDriverFloodlight *iDriver;
    LTLibrary  *pDriverLibrary;
    u32        objBrightness;
} LTOBJECT_API;

static bool LTDeviceFloodlightImpl_LightOff(LTDeviceFloodlightImpl *light) {
    if (!light) return false;
    return light->iDriver->SetBrightness(kMinBrightness, kLight_ObjectBrightness);
}

static bool LTDeviceFloodlightImpl_LightOn(LTDeviceFloodlightImpl *light) {
    if (!light) return false;
    return light->iDriver->SetBrightness(light->objBrightness, kLight_ObjectBrightness);
}

static bool LTDeviceFloodlightImpl_SetObjectBrightness(LTDeviceFloodlightImpl *light, u32 brightness) {
    if (!light) return false;
    if (brightness > kMaxBrightness) {
        brightness = kMaxBrightness;
    }
    light->objBrightness = brightness;
    return true;
}

static void LTDeviceFloodlightImpl_GetObjectBrightness(LTDeviceFloodlightImpl *light, u32 *brightness) {
    if (!light || !brightness) return;
    *brightness = light->objBrightness;
}

static bool LTDeviceFloodlightImpl_GetCurrentBrightness(LTDeviceFloodlightImpl *light, u32 *brightness) {
    return (!light || !brightness) ? false : light->iDriver->GetBrightness(brightness, kLight_CurrentBrightness);
}

static bool LTDeviceFloodlightImpl_SetFlashFrequency(LTDeviceFloodlightImpl *light, u32 flashFrequency) {
    return (!light) ? false : light->iDriver->SetFlashFrequency(flashFrequency);
}

static bool LTDeviceFloodlightImpl_GetFlashFrequency(LTDeviceFloodlightImpl *light, u32 *flashFrequency) {
    return (!light || !flashFrequency) ? false : light->iDriver->GetFlashFrequency(flashFrequency);
}

static bool LTDeviceFloodlightImpl_SetAccommodationTime(LTDeviceFloodlightImpl *light, u32 accomodationTime) {
    return (light ? light->iDriver->SetAccomodationTime(accomodationTime) : false);
}

static void LTDeviceFloodlightImpl_GetAccommodationTime(LTDeviceFloodlightImpl *light, u32 *accomodationTime) {
    if (!light || !accomodationTime) return;
    light->iDriver->GetAccomodationTime(accomodationTime);
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceFloodlightImpl_DestructObject(LTDeviceFloodlightImpl *light) {
    lt_closelibrary(light->pDriverLibrary);
}

static bool LTDeviceFloodlightImpl_ConstructObject(LTDeviceFloodlightImpl *light) {
    const char *driverName;
    light->objBrightness = kMaxBrightness;

    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("init.devconfig", "could not open LTDeviceConfig");
            break;
        }

        driverName = deviceConfig->GetDriverAt("LTDeviceFloodlight", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("init.loadlib", "no driver %s found in device config", driverName);
            lt_closelibrary(deviceConfig);
            break;
        }
        light->pDriverLibrary = LT_GetCore()->OpenLibrary(driverName);
        lt_closelibrary(deviceConfig);

        light->iDriver = (ILTDriverFloodlight *)lt_getlibraryinterface(ILTDriverFloodlight, light->pDriverLibrary);
        if (!light->iDriver) {
            LTLOG_YELLOWALERT("init.iface", "could not get driver interface");
            break;
        }
        return true;
    } while (0);

    LTDeviceFloodlightImpl_DestructObject(light);
    return false;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceFloodlight,LTDeviceFloodlightImpl,
    LightOff,
    LightOn,
    GetCurrentBrightness,
    SetObjectBrightness,
    GetObjectBrightness,
    SetFlashFrequency,
    GetFlashFrequency,
    SetAccommodationTime,
    GetAccommodationTime, 
);

define_LTOBJECT_EXPORTLIBRARY(LTDeviceFloodlight, 1, LTDeviceFloodlightImpl);
