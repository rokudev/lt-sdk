/*******************************************************************************
 * Esp32DriverSleepControlImpl.c
 *
 * Esp32 LT Driver Library for sleep control functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

// clang-format off
#include <lt/core/LTCore.h>
#include <lt/device/sleepcontrol/LTDeviceSleepControl.h>

DEFINE_LTLOG_SECTION("esp32.drv.sleep.control");

static LTTime                                s_lastActualSleepDuration  = LTTimeInitializer_Zero();
static LTTime                                s_sleepDurationLimit       = LTTimeInitializer_Zero();
static LTTime                                s_wakeUpTime               = LTTimeInitializer_Zero();
static LTTime                                s_enterSleepIdleDelay      = LTTimeInitializer_Milliseconds(1);
static LTDeviceSleepControl_WakeReason       s_wakeReason               = kLTDeviceSleepControl_WakeReason_Unknown;
static u32                                   s_disallowanceGrant        = 0;

static bool Esp32DriverSleepControlImpl_Init(LTDeviceSleepControl_WakeupSources   wakeupSources,
                                             const LTOThread                      *notificationThread,
                                             LTDeviceSleepControl_BeforeSleepProc *pBeforeSleepTask,
                                             LTThread_TaskProc                    *pAfterSleepTask,
                                             void                                 *pClientData) {
    LT_UNUSED(wakeupSources);
    LT_UNUSED(notificationThread);
    LT_UNUSED(pBeforeSleepTask);
    LT_UNUSED(pAfterSleepTask);
    LT_UNUSED(pClientData);
    return false;
}

static void EnterShipmode(void) {
}

static bool Esp32DriverSleepControlImpl_AllowSleep(LTDeviceSleepControl_SleepMode sleepMode) {
    bool ret = true;
    switch (sleepMode) {
        case kLTDeviceSleepControl_SleepMode_Sleep:
            if (!LTTime_IsZero(s_enterSleepIdleDelay)) {
                LT_GetCore()->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(s_enterSleepIdleDelay, LTTime_Zero());
                if (s_disallowanceGrant != 0) {
                    LT_GetCore()->ReallowSleepMode(s_disallowanceGrant);
                    s_disallowanceGrant = 0;
                }
            } else {
                LTLOG_YELLOWALERT("allow.sleep.fail",  NULL);
                ret = false;
            }
            break;
        case kLTDeviceSleepControl_SleepMode_Ship:
            EnterShipmode();
            ret = false; // EnterShipmode() should not return
            break;
        default:
            break;
    }
    return ret;
}

static bool Esp32DriverSleepControlImpl_PreventSleep(void) {
    if (s_disallowanceGrant != 0 ) {
        return false;
    }
    s_disallowanceGrant = LT_GetCore()->DisallowSleepMode();

    return true;
}

static bool Esp32DriverSleepControlImpl_DisableWakeupSource(LTDeviceSleepControl_WakeupSources wakeupSource) {
    LT_UNUSED(wakeupSource);
    return true;
}

static bool Esp32DriverSleepControlImpl_SetSleepDurationLimit(const LTTime sleepDurationLimit) {
    s_sleepDurationLimit = sleepDurationLimit;
    return true;
}

static LTTime Esp32DriverSleepControlImpl_GetSleepDurationLimit(void) {
    return s_sleepDurationLimit;
}

static LTTime Esp32DriverSleepControlImpl_GetLastActualSleepDuration(void) {
    return s_lastActualSleepDuration;
}

static LTDeviceSleepControl_WakeReason Esp32DriverSleepControlImpl_GetWakeReason(void) {
    return s_wakeReason;
}

static LTTime Esp32DriverSleepControlImpl_GetWakeTime(void) {
    return s_wakeUpTime;
}
/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTDriverSleepControl s_ILTDriverLTDeviceSleepControl;

static u32 Esp32DriverSleepControlImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit Esp32DriverSleepControlImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverLTDeviceSleepControl, 1);
}

static bool Esp32DriverSleepControlImpl_LibInit(void) {
    return true;
}

static void Esp32DriverSleepControlImpl_LibFini(void) {
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverSleepControl, Esp32DriverSleepControl);
// clang-format off
define_LTLIBRARY_INTERFACE(ILTDriverSleepControl){
    .Init                       = &Esp32DriverSleepControlImpl_Init,
    .SetSleepDurationLimit      = &Esp32DriverSleepControlImpl_SetSleepDurationLimit,
    .GetSleepDurationLimit      = &Esp32DriverSleepControlImpl_GetSleepDurationLimit,
    .GetLastActualSleepDuration = &Esp32DriverSleepControlImpl_GetLastActualSleepDuration,
    .GetWakeReason              = &Esp32DriverSleepControlImpl_GetWakeReason,
    .GetWakeTime                = &Esp32DriverSleepControlImpl_GetWakeTime,
    .AllowSleep                 = &Esp32DriverSleepControlImpl_AllowSleep,
    .PreventSleep               = &Esp32DriverSleepControlImpl_PreventSleep,
    .DisableWakeupSource        = &Esp32DriverSleepControlImpl_DisableWakeupSource
}
LTLIBRARY_DEFINITION;
// clang-format on

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverSleepControl, (ILTDriverSleepControl))

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *   31-May-2024    snahibin      created
 */
