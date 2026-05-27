/*******************************************************************************
* LTDeviceSleepControlImpl.c
*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/sleepcontrol/LTDeviceSleepControl.h>

DEFINE_LTLOG_SECTION("lt.dev.sleepcontrol");

static LTDriverLibrary       *s_pLibLTDeviceSleepControlDriver = NULL;
static ILTDriverSleepControl *s_pILTDriverLTDeviceSleepControl = NULL;

static bool LTDeviceSleepControlImpl_Init(LTDeviceSleepControl_WakeupSources   wakeupSources,
                                          const LTOThread                      *notificationThread,
                                          LTDeviceSleepControl_BeforeSleepProc *pBeforeSleepTask,
                                          LTThread_TaskProc                    *pAfterSleepTask,
                                          void                                 *pClientData) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->Init(wakeupSources,
                                                      notificationThread,
                                                      pBeforeSleepTask,
                                                      pAfterSleepTask,
                                                      pClientData);
    }
    return false;
}

static LTDeviceSleepControl_WakeReason LTDeviceSleepControlImpl_GetWakeReason(void) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->GetWakeReason();
    }
    return kLTDeviceSleepControl_WakeReason_Unknown;
}

static bool LTDeviceSleepControlImpl_SetSleepDurationLimit(const LTTime sleepDurationLimit) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->SetSleepDurationLimit(sleepDurationLimit);
    }
    return false;
}

static LTTime LTDeviceSleepControlImpl_GetSleepDurationLimit(void) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->GetSleepDurationLimit();
    }
    return LTTime_Zero();
}

static LTTime LTDeviceSleepControlImpl_GetLastActualSleepDuration(void) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->GetLastActualSleepDuration();
    }
    return LTTime_Zero();
}

static LTTime LTDeviceSleepControlImpl_GetWakeTime(void) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->GetWakeTime();
    }
    return LTTime_Zero();
}

static bool LTDeviceSleepControlImpl_AllowSleep(LTDeviceSleepControl_SleepMode sleepMode) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->AllowSleep(sleepMode);
    }
    return false;
}

static bool LTDeviceSleepControlImpl_PreventSleep(void) {
    if (s_pILTDriverLTDeviceSleepControl) {
        return s_pILTDriverLTDeviceSleepControl->PreventSleep();
    }
    return false;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTDeviceSleepControlImpl_LibFini(void) {
    if (s_pLibLTDeviceSleepControlDriver) lt_closelibrary(s_pLibLTDeviceSleepControlDriver);
    s_pLibLTDeviceSleepControlDriver = NULL;
    s_pILTDriverLTDeviceSleepControl = NULL;
}

static bool LTDeviceSleepControlImpl_LibInit(void) {
    s_pLibLTDeviceSleepControlDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceSleepControl", 0);
    if (s_pLibLTDeviceSleepControlDriver) {
        s_pILTDriverLTDeviceSleepControl = lt_getlibraryinterface(ILTDriverSleepControl,
                                                                  s_pLibLTDeviceSleepControlDriver);
        if (s_pILTDriverLTDeviceSleepControl) {
            return true;
        } else {
            lt_closelibrary(s_pLibLTDeviceSleepControlDriver);
        }
    }

    LTLOG_YELLOWALERT("init.fail", "init fail");
    return false;
}

// LTDeviceSleepControl does not use Device Units. Only one instance of LTDeviceSleepControl is allowed.
static u32 LTDeviceSleepControlImpl_GetNumDeviceUnits(void) {
    return 0;
}

static LTDeviceUnit LTDeviceSleepControlImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    LT_UNUSED(deviceUnitNumber);
    return 0;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
// clang-format off
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSleepControl)
    .Init                        = &LTDeviceSleepControlImpl_Init,
    .SetSleepDurationLimit       = &LTDeviceSleepControlImpl_SetSleepDurationLimit,
    .GetSleepDurationLimit       = &LTDeviceSleepControlImpl_GetSleepDurationLimit,
    .GetLastActualSleepDuration  = &LTDeviceSleepControlImpl_GetLastActualSleepDuration,
    .GetWakeReason               = &LTDeviceSleepControlImpl_GetWakeReason,
    .GetWakeTime                 = &LTDeviceSleepControlImpl_GetWakeTime,
    .AllowSleep                  = &LTDeviceSleepControlImpl_AllowSleep,
    .PreventSleep                = &LTDeviceSleepControlImpl_PreventSleep

LTLIBRARY_DEFINITION;
// clang-format on
////////////////////////////////////////////////////////////////////////////////
// class LTDeviceSleepControl public functions
