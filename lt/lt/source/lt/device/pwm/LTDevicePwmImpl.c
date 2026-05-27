/*******************************************************************************
 * lt/source/lt/device/pmw/LTDevicePwmImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/pwm/LTDevicePwm.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.pwm");

#define LTDEVICEPWM_DO_DLOG   0
#if     LTDEVICEPWM_DO_DLOG
#define DLOG                  LTLOG
#else
#define DLOG                  LTLOG_LOGNULL
#endif

/*******************************************************************************
 * Access to the LTDriverPwm library, through which the Device accesses
 * PWM related functions:                                             */
static LTDriverLibrary * s_pDriver = NULL;

/*******************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of
 * available Device Units from the Driver.
 * If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                  */
static void LTDevicePwmImpl_LibFini(void) {
    DLOG("fini", NULL);
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    s_pDriver = NULL;
}

static bool LTDevicePwmImpl_LibInit(void) {
    DLOG("init", NULL);
    if (   !(s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDevicePwm", 0))
        || !s_pDriver->GetNumDeviceUnits()) { LTDevicePwmImpl_LibFini(); return false; }
    return true;
}

static u32 LTDevicePwmImpl_GetNumDeviceUnits(void) { return s_pDriver->GetNumDeviceUnits(); }

static LTDeviceUnit LTDevicePwmImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    DLOG("get.handle", NULL);
    LTDeviceUnit hDeviceUnit = 0;
    if (nDeviceUnitNumber < s_pDriver->GetNumDeviceUnits())
        hDeviceUnit = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
    return hDeviceUnit;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePwm) LTLIBRARY_DEFINITION;
