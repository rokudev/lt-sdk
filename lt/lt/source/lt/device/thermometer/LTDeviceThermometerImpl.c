/*******************************************************************************
 * lt/source/lt/device/thermometer/LTDeviceThermometerImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/thermometer/LTDeviceThermometer.h>
#include <lt/device/config/LTDeviceConfig.h>

/*******************************************************************************
 * Access to the LTDriverThermometer library, through which the Device accesses
 * temperature-sensor-related functions:                                      */
static LTDriverLibrary * s_pDriver = NULL;

/*******************************************************************************
 * Number of Device Units available according to the Driver:                  */
static u32               s_nNumDeviceUnits = 0;

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownDriver(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    s_pDriver = NULL;
}

/***************************************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                                                              */
static bool LTDeviceThermometerImpl_LibInit(void) {
    if (!(s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceThermometer", 0))) return false;
    if (!(s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits())) { ShutDownDriver(); return false; }
    return true;
}

static void LTDeviceThermometerImpl_LibFini(void) { ShutDownDriver(); }

static u32 LTDeviceThermometerImpl_GetNumDeviceUnits(void) { return s_nNumDeviceUnits; }

static LTDeviceUnit LTDeviceThermometerImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDeviceUnit = 0;
    if (nDeviceUnitNumber < s_nNumDeviceUnits)
        hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
    return hDeviceUnit;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceThermometer) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Jan-21   constantine created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
