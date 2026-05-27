/*******************************************************************************
 * lt/source/lt/driver/flpir/LTDriverFlPIR.c
 *
 * LT Driver Library for floodlight PIR sensor access
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>
#include <lt/device/flpir/LTDeviceFlPIR.h>

DEFINE_LTLOG_SECTION("ldrv.flpir");

#define HIGHEST_PRIORITY 30
#define PIR_SETTING_LEN  3

static const LTArgsDescriptor s_PIREventArgs = {1, {kLTArgType_u32}};
static LTEvent         s_hPIREvent;
static LTCore         *s_pCore;
static LTThread        s_hThread;
static LTDeviceFLUnit *s_deviceFlUnit;
static LTMutex        *s_flPirMutex;
static ILTEvent       *s_iEvent;
static ILTThread      *s_iThread;
static LTTime          s_CurrTime;
static LTTime          s_PrevTime;
static u16             s_eventTimeoutMilliSec = 1000;
static LTHandle        s_handle;

static void FlPIRCallback(LTDriverFLUnitEvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(pEventData);
    if (event == kLTDriverFLUnitEvent_PirTrigger) {
        s_CurrTime = s_pCore->GetKernelTime();
        if ((LTTime_GetMilliseconds(s_CurrTime) - LTTime_GetMilliseconds(s_PrevTime)) > s_eventTimeoutMilliSec) {
            s_iEvent->NotifyEvent(s_hPIREvent, kLTDriverFLUnitEvent_PirTrigger);
            s_PrevTime = s_pCore->GetKernelTime();
        }
    }
}

static void PIREventDispatch(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    void *pEventData = NULL;
    LTDriverFlPIREvent eventType = (LTDriverFlPIREvent)LTArgs_u32At(0, args);
    if (!proc) return;
    ((LTDriverFlPIREventProc *)proc)(eventType, pEventData, pClientData);
}

static bool ThreadInit(void) {
    s_flPirMutex->API->Lock(s_flPirMutex);
    s_deviceFlUnit->OnPIRTrigger(&s_handle, FlPIRCallback, NULL);
    s_flPirMutex->API->Unlock(s_flPirMutex);
    return true;
}

static void ThreadDeInit(void) {
    s_flPirMutex->API->Lock(s_flPirMutex);
    s_deviceFlUnit->NoPIRTrigger(&s_handle, FlPIRCallback);
    s_flPirMutex->API->Unlock(s_flPirMutex);
}

/*******************************************************************************
 *
 * driver interface implementation
 *
 *******************************************************************************/
static void LTDriverFlPIRImpl_OnPIREvent(LTDriverFlPIREventProc proc, void *pClientData) {
    if (!proc || !pClientData) return;
    s_PrevTime = s_pCore->GetKernelTime();
    s_iEvent->RegisterForEvent(s_hPIREvent, proc, NULL, pClientData, false);
}

static bool LTDriverFlPIRImpl_NoPIREvent(LTDriverFlPIREventProc proc) {
    if (!proc) return false;
    return s_iEvent->UnregisterFromEvent(s_hPIREvent, proc);
}

static void LTDriverFlPIRImpl_EventTriggerDelay(u16 delayMillisec) {
    s_eventTimeoutMilliSec = delayMillisec;
}

static bool LTDriverFlPIRImpl_SetPIRSettings(u8 *data) {
    if (!data) return false;
    s_flPirMutex->API->Lock(s_flPirMutex);
    if (s_deviceFlUnit) {
        if (!s_deviceFlUnit->SetCommand(&s_handle, LT_FL_CMD_SET_PIR_PARAMETER, data, PIR_SETTING_LEN)) {
            LTLOG_YELLOWALERT("f.setpir", "Failed to SetPIRSettings");
            s_flPirMutex->API->Unlock(s_flPirMutex);
            return false;
        }
        s_flPirMutex->API->Unlock(s_flPirMutex);
        return true;
    }
    s_flPirMutex->API->Unlock(s_flPirMutex);
    return false;
}

static bool LTDriverFlPIRImpl_GetPIRSettings(u8 *data) {
    if (!data) return false;
    s_flPirMutex->API->Lock(s_flPirMutex);
    if (s_deviceFlUnit) {
        if (!s_deviceFlUnit->GetCommand(&s_handle, LT_FL_CMD_GET_PIR_PARAMETER, data, PIR_SETTING_LEN)) {
            LTLOG_YELLOWALERT("f.getpir", "Failed to GetPIRSettings");
            s_flPirMutex->API->Unlock(s_flPirMutex);
            return false;
        }
        s_flPirMutex->API->Unlock(s_flPirMutex);
        return true;
    }
    s_flPirMutex->API->Unlock(s_flPirMutex);
    return false;
}

/*******************************************************************************
 * Library initialization and deinitialization
 *******************************************************************************/
static void LTDriverFlPIRImpl_LibFini(void) {
    if (s_hPIREvent != LTHANDLE_INVALID) {
        lt_destroyhandle(s_hPIREvent);
    }
    s_iThread->Terminate(s_hThread);
    s_iThread->WaitUntilFinished(s_hThread, LTTime_Infinite());
    s_iThread->Destroy(s_hThread);
    lt_destroyhandle(s_handle);
    lt_closelibrary(s_deviceFlUnit);
    lt_destroyobject(s_flPirMutex);
    s_flPirMutex = NULL;
}

static bool LTDriverFlPIRImpl_LibInit(void) {
    s_pCore = LT_GetCore();
    do {
        if (!(s_iThread = lt_getlibraryinterface(ILTThread, s_pCore))
            || !(s_iEvent = lt_getlibraryinterface(ILTEvent, s_pCore))) {
            LTLOG_YELLOWALERT("f.init", "failed to get lib interface thread");
            break;
        }
        s_flPirMutex = lt_createobject(LTMutex);
        s_hPIREvent  = s_pCore->CreateEvent(&s_PIREventArgs, PIREventDispatch, NULL, NULL, NULL);
        if (!s_hPIREvent) {
            break;
        }
        s_hThread = s_pCore->CreateThread("flpirdrv");
        if (!s_hThread) {
            LTLOG_YELLOWALERT("f.thread", "failed to allocate thread");
            break;
        }
        if (!(s_deviceFlUnit = lt_openlibrary(LTDeviceFLUnit))) {
            LTLOG_YELLOWALERT("f.init.flunit", "Error opening library LTDeviceFlUnit");
            break;
        }
        if (!(s_handle = s_deviceFlUnit->CreateDeviceUnitHandle(0))) {
            LTLOG_YELLOWALERT("f.handle", "Error creating device unit handle");
            break;
        }
        if(!s_deviceFlUnit->IsMounted(&s_handle)) {
            LTLOG_YELLOWALERT("f.mounted", "PIR device not found");
            break;
        }
        if (!s_deviceFlUnit->AuthenticateDevice(&s_handle)) {
            LTLOG_YELLOWALERT("f.init.authen", "Failed to Authenticate floodlight");
            break;
        }
        u8 data[3] = {250,
                    (kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right),
                    kLTDeviceFlPIRAlgo_Off}; /* Setting default PIR parameters sensitivity:250, zone:0xe0, algorithm:0 */
        if (!s_deviceFlUnit->SetCommand(&s_handle, LT_FL_CMD_SET_PIR_PARAMETER, data, sizeof(data))) {
            LTLOG_YELLOWALERT("f.init.set", "Failed to SET PIR PARAMETER");
            break;
        }
        s_iThread->Start(s_hThread, ThreadInit, ThreadDeInit);
        return true;
    } while(0);

    LTDriverFlPIRImpl_LibFini();
    return false;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverFlPIR, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverFlPIR) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverFlPIR) {
    .SetPIRSettings    = LTDriverFlPIRImpl_SetPIRSettings,
    .GetPIRSettings    = LTDriverFlPIRImpl_GetPIRSettings,
    .OnPIREvent        = LTDriverFlPIRImpl_OnPIREvent,
    .NoPIREvent        = LTDriverFlPIRImpl_NoPIREvent,
    .EventTriggerDelay = LTDriverFlPIRImpl_EventTriggerDelay
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverFlPIR, (ILTDriverFlPIR));
