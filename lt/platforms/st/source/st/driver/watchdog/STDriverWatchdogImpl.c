/*******************************************************************************
 * platforms/st/source/st/driver/watchdog/STDriverWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/
/** @file STDriverWatchdogImpl.c Implementation of watchdog driver for ST
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

static bool s_bWatchdogEnabled = false;

/*******************************************************************************
 * ILTDriverWatchdog implementation                                           */

static bool STDriverWatchdog_ResetTimer(void) {
    return true;
}

static bool STDriverWatchdog_EnableTimer(void) {
    s_bWatchdogEnabled = true;
    return true;
}

static bool STDriverWatchdog_DisableTimer(void) {
    s_bWatchdogEnabled = false;
    return true;
}

static bool STDriverWatchdog_IsEnabled(void) {
    return s_bWatchdogEnabled;
    return true;
}

static bool STDriverWatchdog_SetTimeout(LTTime timeout) {
    LT_UNUSED(timeout);
    return true;
}

static void STDriverWatchdog_Reboot(void) {
}

static LTBootReason STDriverWatchdog_GetBootReason(const char ** pReasonString) {
    static const char *reasonString = "Power On";
    *pReasonString = reasonString;
    return kLTBootReason_PowerOn;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot = STDriverWatchdog_Reboot,
    .ResetTimer = STDriverWatchdog_ResetTimer,
    .EnableTimer = STDriverWatchdog_EnableTimer,
    .DisableTimer = STDriverWatchdog_DisableTimer,
    .IsEnabled = STDriverWatchdog_IsEnabled,
    .SetTimeout = STDriverWatchdog_SetTimeout,
    .GetBootReason = STDriverWatchdog_GetBootReason,

}LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(STDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, STDriverWatchdog);

static bool STDriverWatchdogImpl_LibInit(void) {
    STDriverWatchdog_DisableTimer();
    return true;
}

static void STDriverWatchdogImpl_LibFini(void) {}

static u32 STDriverWatchdogImpl_GetNumDeviceUnits(void) { return 0; }
LTDeviceUnit STDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Jan-21   constantine created
 *  11-Jan-21   augustus    copied from Dan's Halford version
 */
