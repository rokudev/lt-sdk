/******************************************************************************
 * lt/source/lt/device/battery/LTDeviceBatteryImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/battery/LTDeviceBattery.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("lt.dev.battery");

/******************************************************************************
 * Access to the LTDriverBattery library, through which the Device accesses
 * Battery-related functions:
 */

static struct statics {
    LTDriverLibrary *pDriverLib;
    ILTDriverBattery *pILTDriverBattery;
} S;

/*******************************************************************************
 * Device Unit access and conversion:
 */
static LTBatterySystem *LTDeviceBatteryImpl_GetBatterySystemByName(const char *name) {
    if (!S.pILTDriverBattery) return NULL;
    return S.pILTDriverBattery->GetBatterySystemByName(name);
}

static LTBatterySystem *LTDeviceBatteryImpl_GetBatterySystem(u32 index) {
    if (!S.pILTDriverBattery) return NULL;
    return S.pILTDriverBattery->GetBatterySystem(index);
}

static const char *LTDeviceBatteryImpl_GetNameFromIndex(u32 index) {
    if (!S.pILTDriverBattery) return NULL;
    return S.pILTDriverBattery->GetNameFromIndex(index);
}

/******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.
 */

static void LTDeviceBatteryImpl_LibFini(void) {
    lt_closelibrary(S.pDriverLib);
    S.pDriverLib = NULL;
    S.pILTDriverBattery = NULL;
}

static bool LTDeviceBatteryImpl_LibInit(void) {
    bool bOK = false;
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (deviceConfig) {
        const char *pLibName = deviceConfig->GetDriverAt("LTDeviceBattery", 0);
        bOK =   pLibName
             && (S.pDriverLib = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibName))
             && (S.pILTDriverBattery  = lt_getlibraryinterface(ILTDriverBattery, S.pDriverLib));
        lt_closelibrary(deviceConfig);
    }
    if (!bOK) {
        LTLOG_YELLOWALERT("drv.lib.0", "%p %p", S.pDriverLib, S.pILTDriverBattery);
        LTDeviceBatteryImpl_LibFini();
    }
    return bOK;
}

define_LTLIBRARY_ROOT_INTERFACE(LTDeviceBattery)
    .GetBatterySystemByName = LTDeviceBatteryImpl_GetBatterySystemByName,
    .GetBatterySystem       = LTDeviceBatteryImpl_GetBatterySystem,
    .GetNameFromIndex       = LTDeviceBatteryImpl_GetNameFromIndex
LTLIBRARY_DEFINITION;
