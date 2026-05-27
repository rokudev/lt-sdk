/*******************************************************************************
 * lt/source/lt/device/efuse/LTDeviceEfuseImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/efuse/LTDeviceEfuse.h>
#include <lt/device/config/LTDeviceConfig.h>

/*******************************************************************************
 * Access to the LTDriverEfuse library, through which the Device accesses
 * IR transmit related functions:                                             */
static LTDriverLibrary * s_pDriver = NULL;

/*******************************************************************************
 * Access to the driver's ILTDriverEfuseDeviceUnit interface, through which the 
 * device can access the driver-specific IR transmit related functions:       */
static LTDeviceUnit s_hEfuse = 0;
static ILTDriverEfuseDeviceUnit * s_iEfuse = NULL;

/*******************************************************************************
 * Number of Device Units available according to the Driver:                  */
static u32               s_nNumDeviceUnits = 0;

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownDriver(void) {
    lt_destroyhandle(s_hEfuse);
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    s_pDriver = NULL;
}

/*******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                  */
static bool LTDeviceEfuseImpl_LibInit(void) {
    if (!(s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceEfuse", 0))) return false;
    if (!(s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits())) { ShutDownDriver(); return false; }

    s_hEfuse = s_pDriver->CreateDeviceUnitHandle(0);
    s_iEfuse = lt_gethandleinterface(ILTDriverEfuseDeviceUnit, s_hEfuse);

    return true;
}

static void LTDeviceEfuseImpl_LibFini(void) {
    ShutDownDriver();
}

static u32 LTDeviceEfuseImpl_GetNumDeviceUnits(void) { return s_nNumDeviceUnits; }

static LTDeviceUnit LTDeviceEfuseImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDeviceUnit = 0;
    if (nDeviceUnitNumber < s_nNumDeviceUnits)
        hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
    return hDeviceUnit;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceEfuse)
LTLIBRARY_DEFINITION;
