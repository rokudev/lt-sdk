/*******************************************************************************
 * platforms/common/source/common/driver/watchdog/CommonDriverWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Common LT Driver Library for watchdog functions
 *
 * Provides Common-specific control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 ******************************************************************************/
/** @file CommonDriverWatchdogImpl.c Implementation of watchdog driver for Common
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

static bool s_bWatchdogEnabled = false;

/*******************************************************************************
 * ILTDriverWatchdog implementation                                           */

static bool CommonWatchdog_ResetTimer(void) {
    return true;
}

static bool CommonWatchdog_EnableTimer(void) {
    s_bWatchdogEnabled = true;
    return true;
}

static bool CommonWatchdog_DisableTimer(void) {
    s_bWatchdogEnabled = false;
    return true;
}

static bool CommonWatchdog_IsEnabled(void) {
    return s_bWatchdogEnabled;
}

static bool CommonWatchdog_SetTimeout(LTTime timeout) {
    LT_UNUSED(timeout);
    return true;
}

static bool CommonWatchdog_Reboot(void) {
}

static LTBootReason CommonWatchdog_GetBootReason(const char ** pReasonString) {
    static const char * reasonString = "Not implemented";
    if (pReasonString) *pReasonString = reasonString;
    return kLTBootReason_Undefined;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot = CommonWatchdog_Reboot,
    .ResetTimer = CommonWatchdog_ResetTimer,
    .EnableTimer = CommonWatchdog_EnableTimer,
    .DisableTimer = CommonWatchdog_DisableTimer,
    .IsEnabled = CommonWatchdog_IsEnabled,
    .SetTimeout = CommonWatchdog_SetTimeout,
    .GetBootReason = CommonWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(CommonDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, CommonDriverWatchdog);

static bool CommonDriverWatchdogImpl_LibInit(void) {
    CommonWatchdog_DisableTimer();
    return true;
}

static void CommonDriverWatchdogImpl_LibFini(void) {}

static u32 CommonDriverWatchdogImpl_GetNumDeviceUnits(void) { return 0; }
LTDeviceUnit CommonDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }
