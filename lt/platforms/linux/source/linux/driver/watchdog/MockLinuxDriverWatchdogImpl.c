/*******************************************************************************
 * platforms/linux/source/linux/driver/watchdog/MockLinuxDriverWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Mock Linux LT Driver Library for watchdog functions
 *
 * Provides Linux-specific control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 ******************************************************************************/
/** @file MockLinuxDriverWatchdogImpl.c Implementation of watchdog driver for Linux
 */

#include <stdio.h>

#include <lt/core/LTCore.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

static bool s_bWatchdogEnabled = false;

/*******************************************************************************
 * ILTDriverWatchdog implementation                                           */

static bool LinuxWatchdog_ResetTimer(void) {
    return true;
}

static bool LinuxWatchdog_EnableTimer(void) {
    s_bWatchdogEnabled = true;
    return true;
}

static bool LinuxWatchdog_DisableTimer(void) {
    s_bWatchdogEnabled = false;
    return true;
}

static bool LinuxWatchdog_IsEnabled(void) {
    return s_bWatchdogEnabled;
}

static bool LinuxWatchdog_SetTimeout(LTTime timeout) {
    LT_UNUSED(timeout);
    return true;
}

static void LinuxWatchdog_Reboot(void) {
    printf("\nReboot not enabled on this platform\n");
}

static LTBootReason LinuxWatchdog_GetBootReason(const char ** pReasonString) {
    static const char * reasonString = "Not implemented";
    if (pReasonString) *pReasonString = reasonString;
    return kLTBootReason_Undefined;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot = LinuxWatchdog_Reboot,
    .ResetTimer = LinuxWatchdog_ResetTimer,
    .EnableTimer = LinuxWatchdog_EnableTimer,
    .DisableTimer = LinuxWatchdog_DisableTimer,
    .IsEnabled = LinuxWatchdog_IsEnabled,
    .SetTimeout = LinuxWatchdog_SetTimeout,
    .GetBootReason = LinuxWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(MockLinuxDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, MockLinuxDriverWatchdog);

static bool MockLinuxDriverWatchdogImpl_LibInit(void) {
    LinuxWatchdog_DisableTimer();
    return true;
}

static void MockLinuxDriverWatchdogImpl_LibFini(void) {}

static u32 MockLinuxDriverWatchdogImpl_GetNumDeviceUnits(void) { return 0; }
LTDeviceUnit MockLinuxDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }
