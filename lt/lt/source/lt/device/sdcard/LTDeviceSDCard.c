/*******************************************************************************
 * lt/source/lt/device/sdcard/LTDeviceSDCard.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/sdcard/LTDeviceSDCard.h>
#include <lt/device/config/LTDeviceConfig.h>

static LTDriverLibrary * s_pSDCardDriver = NULL;

static bool LTDeviceSDCardImpl_LibInit(void) {
    s_pSDCardDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceSDCard", 0);
    if (!s_pSDCardDriver) return false;

    if (s_pSDCardDriver->GetNumDeviceUnits() == 0) {
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pSDCardDriver);
        s_pSDCardDriver = NULL;
        return false;
    }

    return true;
}

static void LTDeviceSDCardImpl_LibFini(void) {
    if (s_pSDCardDriver) {
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pSDCardDriver);
        s_pSDCardDriver = NULL;
    }
}

static u32
LTDeviceSDCardImpl_GetNumDeviceUnits(void) {
    if (!s_pSDCardDriver) return 0;
    return s_pSDCardDriver->GetNumDeviceUnits();
}

static LTDeviceUnit
LTDeviceSDCardImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LTDeviceUnit hSDCardUnit = 0;
    if (s_pSDCardDriver && nDeviceUnitNum < s_pSDCardDriver->GetNumDeviceUnits()) {
        hSDCardUnit = s_pSDCardDriver->CreateDeviceUnitHandle(nDeviceUnitNum);
    }
    return hSDCardUnit;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSDCard)
LTLIBRARY_DEFINITION;
