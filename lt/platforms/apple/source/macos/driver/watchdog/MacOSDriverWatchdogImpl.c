/*******************************************************************************
 * platforms/macos/source/macos/driver/watchdog/MacOSDriverWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * MacOS LT Driver Library for watchdog functions
 *
 * Provides MacOS-specific control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 ******************************************************************************/
/** @file MacOSDriverWatchdogImpl.c Implementation of watchdog driver for MacOS
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

static bool s_bWatchdogEnabled = false;

/*******************************************************************************
 * ILTDriverWatchdog implementation                                           */

static bool MacOSWatchdog_ResetTimer(void) {
    return true;
}

static bool MacOSWatchdog_EnableTimer(void) {
    s_bWatchdogEnabled = true;
    return true;
}

static bool MacOSWatchdog_DisableTimer(void) {
    s_bWatchdogEnabled = false;
    return true;
}

static bool MacOSWatchdog_IsEnabled(void) {
    return s_bWatchdogEnabled;
}

static bool MacOSWatchdog_SetTimeout(LTTime timeout) {
    LT_UNUSED(timeout);
    return true;
}

static void MacOSWatchdog_Reboot(void) {
}

static LTBootReason MacOSWatchdog_GetBootReason(const char ** pReasonString) {
    static const char * reasonString = "Not implemented";
    if (pReasonString) *pReasonString = reasonString;
    return kLTBootReason_Undefined;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot = MacOSWatchdog_Reboot,
    .ResetTimer = MacOSWatchdog_ResetTimer,
    .EnableTimer = MacOSWatchdog_EnableTimer,
    .DisableTimer = MacOSWatchdog_DisableTimer,
    .IsEnabled = MacOSWatchdog_IsEnabled,
    .SetTimeout = MacOSWatchdog_SetTimeout,
    .GetBootReason = MacOSWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(MacOSDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, MacOSDriverWatchdog);

static bool MacOSDriverWatchdogImpl_LibInit(void) {
    MacOSWatchdog_DisableTimer();
    return true;
}

static void MacOSDriverWatchdogImpl_LibFini(void) {}

static u32 MacOSDriverWatchdogImpl_GetNumDeviceUnits(void) { return 0; }
LTDeviceUnit MacOSDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Jan-21   constantine created
 *  11-Jan-21   augustus    copied from Dan's Halford version
 */
