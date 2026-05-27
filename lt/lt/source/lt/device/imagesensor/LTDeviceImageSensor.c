/******************************************************************************
 * lt/source/lt/device/imagesensor/LTDeviceImageSensor.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.imagesensor");

/******************************************************************************
 * Access to the LTDriverImageSensor library, through which the Device accesses
 * Image sensor related functions:
 */
static LTDriverLibrary      *s_pDriver         = NULL;
static ILTDriverImageSensor *s_ILTDriver       = NULL;

/******************************************************************************
 * Number of Device Units available according to the Driver:
 */
static u32               s_nNumDeviceUnits = 0;

/******************************************************************************
 * Unload the Driver library:
 */
static void ShutDownDriver(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    s_pDriver = NULL;
}

/******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.
 */
static bool LTDeviceImageSensorImpl_LibInit(void) {
    s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceImageSensor", 0);
    if (s_pDriver) {
        s_ILTDriver = lt_getlibraryinterface(ILTDriverImageSensor, s_pDriver);
        if (s_ILTDriver) {
            s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits();
            if (!s_nNumDeviceUnits) { ShutDownDriver(); }
        }
    }
    if (!s_nNumDeviceUnits) {
        LTLOG("lib.init.fail", "image sensor init failed");
        return false;
    }
    return true;
}

static void LTDeviceImageSensorImpl_LibFini(void) {
    if (s_nNumDeviceUnits) { ShutDownDriver(); }
}

/*******************************************************************************
 * Device Unit access and conversion:
 */
static u32 LTDeviceImageSensorImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits;
}

static LTDeviceUnit LTDeviceImageSensorImpl_CreateDeviceUnitHandle(
    u32 nDeviceUnitNumber) {

    LTDeviceUnit hDeviceUnit = 0;
    if (s_nNumDeviceUnits) {
        if (nDeviceUnitNumber < s_nNumDeviceUnits) {
            hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
        }
        nDeviceUnitNumber -= s_nNumDeviceUnits;
    }
    return hDeviceUnit;
}

static void LTDeviceImageSensor_OnImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc, void *clientData) {
    s_ILTDriver->OnImageSensorEvent(hUnit, eventProc, clientData);
}

static void LTDeviceImageSensor_NoImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc) {
    s_ILTDriver->NoImageSensorEvent(hUnit, eventProc);
}

static void LTDeviceImageSensor_PowerOn(LTDeviceUnit hUnit) {
    s_ILTDriver->PowerOn(hUnit);
}

static void LTDeviceImageSensor_PowerOff(LTDeviceUnit hUnit) {
    s_ILTDriver->PowerOff(hUnit);
}

static bool LTDeviceImageSensor_SetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *attrValue) {
    return s_ILTDriver->SetAttribute(hUnit, attr, attrValue);
}

static bool LTDeviceImageSensor_GetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *attrValue) {
    return s_ILTDriver->GetAttribute(hUnit, attr, attrValue);
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceImageSensor)
    .OnImageSensorEvent = LTDeviceImageSensor_OnImageSensorEvent,
    .NoImageSensorEvent = LTDeviceImageSensor_NoImageSensorEvent,
    .PowerOn            = LTDeviceImageSensor_PowerOn,
    .PowerOff           = LTDeviceImageSensor_PowerOff,
    .SetAttribute       = LTDeviceImageSensor_SetAttribute,
    .GetAttribute       = LTDeviceImageSensor_GetAttribute,
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Jan-24   trajan      created
 */
