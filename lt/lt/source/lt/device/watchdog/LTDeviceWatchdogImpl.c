/*******************************************************************************
 * <lt/device/watchdog/LTDeviceWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library for watchdog functions
 *
 * Provides general control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/device/config/LTDeviceConfig.h>
#include "ThreadWatchdogManager.h"

/********************************************
 * LTDeviceWatchdogImpl static variables   */
static LTDriverLibrary         *s_pLibWatchdogDriver    = NULL;
static ILTDriverWatchdog       *s_pILTDriverWatchdog    = NULL;
static ILTEvent *               s_iEvent                = NULL;
static LTEvent                  s_hRebootEvent          = 0;
static const LTArgsDescriptor   s_rebootEventArgs       = { 1, {kLTArgType_u32 } };
static LTAtomic                 s_rebootOncer           = { 0 };
static LTOThread               *s_rebootThread          = NULL;
static LTTime                   s_timeout;

/*******************************************************************************
 * Reboot Notify event functions
 * API requested reboot takes place when notification is complete              */
static void DispatchRebootEventCompleteProc(LTEvent hEvent, LTArgs *eventArgs) { LT_UNUSED(hEvent); LT_UNUSED(eventArgs);
    if (s_pILTDriverWatchdog) s_pILTDriverWatchdog->Reboot();
}

static void
DispatchRebootEventProc(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *eventProcClientData) { LT_UNUSED(hEvent); LT_UNUSED(eventArgs);
    LTDeviceWatchdog_NotifyRebootEventProc *rebootNotifyEventProc = (LTDeviceWatchdog_NotifyRebootEventProc *)eventProc;
    rebootNotifyEventProc(eventProcClientData);
}

/*******************************************************************************
 * HardBoot Procedure for ThreadWatchdogManager                               */
static void HardBoot(void) {
    if (s_pILTDriverWatchdog) {
        s_pILTDriverWatchdog->DisableTimer();
        s_pILTDriverWatchdog->SetTimeout(LTTime_Zero());
        s_pILTDriverWatchdog->EnableTimer();
    }
    else {
        if (! LT_GetCore()->AssertFailed(__FILE__, __LINE__, "watchdog.assert.reboot")) LT_GetCore()->DebugBreak();
    }
    while (1) { }
}

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownWatchdogDriver(void) {
    if (s_pLibWatchdogDriver) LT_GetCore()->CloseLibrary((LTLibrary *)s_pLibWatchdogDriver);
    s_pLibWatchdogDriver = NULL;
    s_pILTDriverWatchdog = NULL;
}

/*******************************************************************************
 * Library startup and shutdown:                                              */
static bool LTDeviceWatchdogImpl_LibInit(void) {
    s_pLibWatchdogDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceWatchdog", 0);
    if (!s_pLibWatchdogDriver) { ShutDownWatchdogDriver(); return false; }
    s_pILTDriverWatchdog = lt_getlibraryinterface(ILTDriverWatchdog, s_pLibWatchdogDriver);
    if (!s_pILTDriverWatchdog) { ShutDownWatchdogDriver(); return false; }
    s_iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    s_hRebootEvent = LT_GetCore()->CreateEvent(&s_rebootEventArgs, &DispatchRebootEventProc, &DispatchRebootEventCompleteProc, NULL, NULL);

    return ThreadWatchdogManager_Initialize(&HardBoot);
}

static void LTDeviceWatchdogImpl_LibFini(void) {
    ThreadWatchdogManager_Finalize();

    if (s_hRebootEvent) {
        s_iEvent->Destroy(s_hRebootEvent);
        s_hRebootEvent = 0;
    }
    ShutDownWatchdogDriver();
}

/*********************************************************************************************************************************
 * LTDeviceWatchdog does not use Device Units, as there is typically only one  watchdog in play per core.  If desired, the
 * library can easily convert to the Device Unit model:                                                                          */
static u32 LTDeviceWatchdogImpl_GetNumDeviceUnits(void) { return 0; }
static LTDeviceUnit LTDeviceWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }

static bool LTDeviceWatchdogImpl_NotifyRebootEventInWatchdogRebootThread(void) {
    /* we create a thread to notify the reboot event so that execution of our event dispatch complete proc occurs in our own thread,
       making it deterministic, instead in the whichever thread requested the reboot whose lifecycle and queued workload are non-determinstic */
    s_iEvent->NotifyEvent(s_hRebootEvent, 1);
    return true; /* stick around to field the event complete proc! */
}

/***************************************************************************************************************************************
 * LTDriverWatchdog API:                                                                                                              */
static void LTDeviceWatchdogImpl_Reboot(void)  {
    if ( s_pILTDriverWatchdog && (0 == LTAtomic_FetchAdd(&s_rebootOncer, 1))) {
        s_rebootThread = lt_createobject(LTOThread);
        s_rebootThread->API->SetPriority(s_rebootThread, kLTThread_PriorityHighest);
        s_rebootThread->API->Start(s_rebootThread, "watchdog.reboot", &LTDeviceWatchdogImpl_NotifyRebootEventInWatchdogRebootThread, NULL);
    }
}

static bool LTDeviceWatchdogImpl_ResetTimer(void)           { return s_pILTDriverWatchdog ? s_pILTDriverWatchdog->ResetTimer() : false; }
static bool LTDeviceWatchdogImpl_EnableTimer(void)          { return s_pILTDriverWatchdog ? s_pILTDriverWatchdog->EnableTimer() : false; }
static bool LTDeviceWatchdogImpl_DisableTimer(void)         { return s_pILTDriverWatchdog ? s_pILTDriverWatchdog->DisableTimer() : false; }
static bool LTDeviceWatchdogImpl_IsEnabled(void)            { return s_pILTDriverWatchdog ? s_pILTDriverWatchdog->IsEnabled() : false; }
static bool LTDeviceWatchdogImpl_SetTimeout(LTTime timeout) {
    bool bResult = s_pILTDriverWatchdog && s_pILTDriverWatchdog->SetTimeout(timeout);
    if (bResult) {
        LT_SIZE nMask = LT_GetCore()->Disable();
        s_timeout = timeout;
        LT_GetCore()->Enable(nMask);
    }
    return bResult;
}
static LTTime LTDeviceWatchdogImpl_GetTimeout(void) {
    LT_SIZE nMask = LT_GetCore()->Disable();
    LTTime timeout = s_timeout;
    LT_GetCore()->Enable(nMask);
    return timeout;
}

static LTBootReason LTDeviceWatchdogImpl_GetBootReason(const char ** pReasonString) {
    if (s_pILTDriverWatchdog) {
        return s_pILTDriverWatchdog->GetBootReason(pReasonString);
    } else {
        return kLTBootReason_Undefined;
    }
}

static void LTDeviceWatchdogImpl_GetBootData(const u8 ** pBootData, u32 * nBootDataLength) {
    *pBootData       = NULL;
    *nBootDataLength = 0;
    if (s_pILTDriverWatchdog && s_pILTDriverWatchdog->GetBootData) {
        s_pILTDriverWatchdog->GetBootData(pBootData, nBootDataLength);
    }
}

static void LTDeviceWatchdogImpl_OnRebootNotify(LTDeviceWatchdog_NotifyRebootEventProc * pEventProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    if (s_hRebootEvent) s_iEvent->RegisterForEvent(s_hRebootEvent, pEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTDeviceWatchdogImpl_NoRebootNotify(LTDeviceWatchdog_NotifyRebootEventProc * pEventProc) {
    if (s_hRebootEvent) s_iEvent->UnregisterFromEvent(s_hRebootEvent, pEventProc);
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceWatchdog)
    .Reboot         = LTDeviceWatchdogImpl_Reboot,
    .ResetTimer     = LTDeviceWatchdogImpl_ResetTimer,
    .EnableTimer    = LTDeviceWatchdogImpl_EnableTimer,
    .DisableTimer   = LTDeviceWatchdogImpl_DisableTimer,
    .IsEnabled      = LTDeviceWatchdogImpl_IsEnabled,
    .SetTimeout     = LTDeviceWatchdogImpl_SetTimeout,
    .GetTimeout     = LTDeviceWatchdogImpl_GetTimeout,
    .OnRebootNotify = LTDeviceWatchdogImpl_OnRebootNotify,
    .NoRebootNotify = LTDeviceWatchdogImpl_NoRebootNotify,
    .GetBootReason  = LTDeviceWatchdogImpl_GetBootReason,
    .GetBootData    = LTDeviceWatchdogImpl_GetBootData,
    .WatchThread    = ThreadWatchdogManager_WatchThread,
    .UnwatchThread  = ThreadWatchdogManager_UnwatchThread
LTLIBRARY_DEFINITION
