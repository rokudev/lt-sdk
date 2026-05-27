/*******************************************************************************
 * lt/source/lt/device/usb/LTDeviceUsbClient.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/usb/LTDeviceUsbClient.h>

//DEFINE_LTLOG_SECTION("dev.usbclient");
/*******************************************************************************
 * Access to the LTDeviceUsbClient library, through which the Device accesses
 * USB related functions:                                             */
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

/*******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                  */
static bool LTDeviceUsbClientImpl_LibInit(void) {
    s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceUsbClient", 0);
    if (!s_pDriver) {
        return false;
    }

    s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits();
    if (!s_nNumDeviceUnits) {
        ShutDownDriver();
        return false;
    }
    return true;
}

static void LTDeviceUsbClientImpl_LibFini(void) {
    ShutDownDriver();
}

static u32 LTDeviceUsbClientImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits;
}

static LTDeviceUnit LTDeviceUsbClientImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDeviceUnit = LTHANDLE_INVALID;
    if (nDeviceUnitNumber < s_nNumDeviceUnits) {
        hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
    }
    return hDeviceUnit;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceUsbClient) LTLIBRARY_DEFINITION;
