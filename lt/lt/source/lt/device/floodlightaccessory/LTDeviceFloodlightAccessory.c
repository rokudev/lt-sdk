/*******************************************************************************
 * lt/source/lt/device/floodlightaccessory/LTDriverFloodlightAccessory.c
 *
 * LTDeviceFloodlightAccessory.c: Floodlight Accessory Implementation
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/floodlightaccessory/LTDeviceFloodlightAccessory.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/driver/floodlightaccessory/LTDriverFloodlightAccessory.h>

DEFINE_LTLOG_SECTION("dev.FloodlightAccessory");

/* _____________________________
/  object impl struct typedef */
typedef_LTObjectImpl(LTDeviceFloodlightAccessory, LTDeviceFloodlightAccessoryImpl) {
} LTOBJECT_API;

typedef struct {
    LTDriverFloodlightAccessory *iFloodlightAccessoryDriver;
    LTEvent          h_pirTriggerEvent;
    LTEvent          h_photoTriggerEvent;
    LTEvent          h_connectionEvent;
    LTEvent          h_overheatedEvent;
    bool             isMounted;
} FloodlightAccessoryDriver;

static FloodlightAccessoryDriver s_instance;
static ILTEvent  *s_iEvent;
static ILTThread *s_iLTThread;
static LTThread   s_hThread;
static const LTArgsDescriptor s_ConnectionEventArgs   = {1, {kLTArgType_u32}};
static const LTArgsDescriptor s_PirTriggerEventArgs   = {2, {kLTArgType_u32, kLTArgType_pointer}};
static const LTArgsDescriptor s_PhotoTriggerEventArgs = {2, {kLTArgType_u32, kLTArgType_pointer}};
static const LTArgsDescriptor s_OverheatedEventArgs   = {2, {kLTArgType_u32, kLTArgType_pointer}};

static bool LTDeviceFloodlightAccessoryImpl_SetCommand(LTDeviceFloodlightAccessory_Endpoint endpoint, LTDeviceFloodlightAccessory_Setting setting, u8 *data, u32 dataLength) {
    if (dataLength) {
        if (!data) return false;
    }
    return s_instance.iFloodlightAccessoryDriver->API->SetCommand(endpoint, setting, data, dataLength);
}

static bool LTDeviceFloodlightAccessoryImpl_GetCommand(LTDeviceFloodlightAccessory_Endpoint endpoint, LTDeviceFloodlightAccessory_Setting setting, u8 *data, u32 dataLength) {
    if (!data) return false;
    return s_instance.iFloodlightAccessoryDriver->API->GetCommand(endpoint, setting, data, dataLength);
}

static void LTDeviceFloodlightAccessoryImpl_OnPIRTrigger(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData) {
    if (!proc) return;
    s_iEvent->RegisterForEvent(s_instance.h_pirTriggerEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFloodlightAccessoryImpl_NoPIRTrigger(LTDeviceFloodlightAccessoryEventProc proc) {
    return s_iEvent->UnregisterFromEvent(s_instance.h_pirTriggerEvent, proc);
}

static void LTDeviceFloodlightAccessoryImpl_OnConnectionChange(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData) {
    if (!proc) return;
    return s_iEvent->RegisterForEvent(s_instance.h_connectionEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFloodlightAccessoryImpl_NoConnectionChange(LTDeviceFloodlightAccessoryEventProc proc) {
    return s_iEvent->UnregisterFromEvent(s_instance.h_connectionEvent, proc);
}

static void LTDeviceFloodlightAccessoryImpl_OnFloodlightBrightnessChange(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData) {
    if (!proc) return;
    return s_iEvent->RegisterForEvent(s_instance.h_photoTriggerEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFloodlightAccessoryImpl_NoFloodlightBrightnessChange(LTDeviceFloodlightAccessoryEventProc proc) {
    return s_iEvent->UnregisterFromEvent(s_instance.h_photoTriggerEvent, proc);
}

static void LTDeviceFloodlightAccessoryImpl_OnFloodlightOverheatedState(LTDeviceFloodlightAccessoryEventProc proc, void *pClientData) {
    if (!proc) return;
    return s_iEvent->RegisterForEvent(s_instance.h_overheatedEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFloodlightAccessoryImpl_NoFloodlightOverheatedState(LTDeviceFloodlightAccessoryEventProc proc) {
    return s_iEvent->UnregisterFromEvent(s_instance.h_overheatedEvent, proc);
}

static void LTDeviceFloodlightAccessoryImpl_OnStatusChangeProc(LTDeviceFloodlightAccessory_Event event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    switch (event) {
        case kLTDeviceFloodlightAccessory_Event_PirTrigger:
            s_iEvent->NotifyEvent(s_instance.h_pirTriggerEvent, event);
            break;
        case kLTDeviceFloodlightAccessory_Event_PhotoSensorTrigger:
            s_iEvent->NotifyEvent(s_instance.h_photoTriggerEvent, event, pEventData);
            break;
        case kLTDeviceFloodlightAccessory_Event_Connected:
            if (!s_instance.iFloodlightAccessoryDriver->API->InitAccessory()) break;
            s_iEvent->NotifyEvent(s_instance.h_connectionEvent, event);
            break;
        case kLTDeviceFloodlightAccessory_Event_Disconnected:
            s_iEvent->NotifyEvent(s_instance.h_connectionEvent, event);
            break;
        case kLTDeviceFloodlightAccessory_Event_Overheated:
            s_iEvent->NotifyEvent(s_instance.h_overheatedEvent, event, pEventData);
            break;
        case kLTDeviceFloodlightAccessory_Event_BrightnessChange:
            s_iEvent->NotifyEvent(s_instance.h_photoTriggerEvent, event, pEventData);
            break;
        default:
            break;
    }
}

static void LTDeviceFloodlightAccessoryImpl_DispatchConnectionEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = NULL;
    LTDeviceFloodlightAccessory_Event eventType = (LTDeviceFloodlightAccessory_Event)LTArgs_u32At(0, args);
    (*(LTDeviceFloodlightAccessoryEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDeviceFloodlightAccessoryImpl_DispatchPIREvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = LTArgs_pointerAt(1, args);
    LTDeviceFloodlightAccessory_Event eventType = (LTDeviceFloodlightAccessory_Event)LTArgs_u32At(0, args);
    (*(LTDeviceFloodlightAccessoryEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDeviceFloodlightAccessoryImpl_DispatchPhotoEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = LTArgs_pointerAt(1, args);
    LTDeviceFloodlightAccessory_Event eventType  = (LTDeviceFloodlightAccessory_Event)LTArgs_u32At(0, args);
    (*(LTDeviceFloodlightAccessoryEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDeviceFloodlightAccessoryImpl_DispatchOverheatedEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = LTArgs_pointerAt(1, args);
    LTDeviceFloodlightAccessory_Event eventType = (LTDeviceFloodlightAccessory_Event)LTArgs_u32At(0, args);
    (*(LTDeviceFloodlightAccessoryEventProc)proc)(eventType, pEventData, pClientData);
}

static bool LTDeviceFloodlightAccessoryImpl_IsMounted(void) {
    return s_instance.isMounted;
}

static bool OnStartThread(void) {
    s_instance.iFloodlightAccessoryDriver->API->OnStatusChange(LTDeviceFloodlightAccessoryImpl_OnStatusChangeProc,
                                                                (void *)s_instance.iFloodlightAccessoryDriver);
    return true;
}

static void OnEndThread(void) {
    s_instance.iFloodlightAccessoryDriver->API->NoStatusChange(LTDeviceFloodlightAccessoryImpl_OnStatusChangeProc);
}

static void LTDeviceFloodlightAccessory_LibFini(void) {
    if (s_instance.h_photoTriggerEvent) s_iEvent->Destroy(s_instance.h_photoTriggerEvent);
    if (s_instance.h_pirTriggerEvent)   s_iEvent->Destroy(s_instance.h_pirTriggerEvent);
    if (s_instance.h_connectionEvent)   s_iEvent->Destroy(s_instance.h_connectionEvent);
    if (s_instance.h_overheatedEvent)   s_iEvent->Destroy(s_instance.h_overheatedEvent);
    if (s_hThread) {
        s_iLTThread->Terminate(s_hThread);
        s_iLTThread->WaitUntilFinished(s_hThread, LTTime_Infinite());
        s_iLTThread->Destroy(s_hThread);
        s_hThread = 0;
    }
    lt_destroyobject(s_instance.iFloodlightAccessoryDriver);
    lt_memcpy(&s_instance, 0, sizeof(s_instance));
}

static bool LTDeviceFloodlightAccessory_LibInit(void) {
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    do {
        if (deviceConfig) {
            const char *driverName = deviceConfig->GetDriverAt("LTDeviceFloodlightAccessory", 0);
            if (!driverName) {
                LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
                break;
            }
            // create the driver object
            if (NULL == (s_instance.iFloodlightAccessoryDriver = (LTDriverFloodlightAccessory *)lt_createobject_named("LTDriverFloodlightAccessory", driverName))) {
                LTLOG_YELLOWALERT("f.create.driver.object", "failed to create LTDriverFloodlightAccessory object %s", driverName);
                break;
            }
        }
        s_instance.isMounted = s_instance.iFloodlightAccessoryDriver->API->SetupDevice();
        if (!s_instance.isMounted || !s_instance.iFloodlightAccessoryDriver->API->InitAccessory()) break;
        s_instance.iFloodlightAccessoryDriver->API->Start();

        s_instance.h_pirTriggerEvent = LT_GetCore()->CreateEvent(&s_PirTriggerEventArgs,
                                                               LTDeviceFloodlightAccessoryImpl_DispatchPIREvent,
                                                               NULL, NULL, NULL);
        if (!s_instance.h_pirTriggerEvent) break;
        s_instance.h_photoTriggerEvent = LT_GetCore()->CreateEvent(&s_PhotoTriggerEventArgs,
                                                                LTDeviceFloodlightAccessoryImpl_DispatchPhotoEvent,
                                                                NULL, NULL, NULL);
        if (!s_instance.h_photoTriggerEvent) break;
        s_instance.h_connectionEvent = LT_GetCore()->CreateEvent(&s_ConnectionEventArgs,
                                                                LTDeviceFloodlightAccessoryImpl_DispatchConnectionEvent,
                                                                NULL, NULL, NULL);
        if (!s_instance.h_connectionEvent) break;
        s_instance.h_overheatedEvent = LT_GetCore()->CreateEvent(&s_OverheatedEventArgs,
                                                                LTDeviceFloodlightAccessoryImpl_DispatchOverheatedEvent,
                                                                NULL, NULL, NULL);
        if (!s_instance.h_overheatedEvent) break;
        s_iLTThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        if (s_iLTThread == NULL) {
            LTLOG_YELLOWALERT("init.thread", "Failed to get IThread interface");
            break;
        }
        s_hThread = LT_GetCore()->CreateThread("devFloodlightAccessoryEvent");
        if (s_hThread == 0) {
            LTLOG_YELLOWALERT("init.create.thread", "Failed to create the device thread");
            break;
        }
        s_iLTThread->Start(s_hThread, OnStartThread, OnEndThread);
        s_iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
        lt_closelibrary(deviceConfig);
        return true;
    } while (0);

    lt_closelibrary(deviceConfig);
    LTDeviceFloodlightAccessory_LibFini();
    return false;
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceFloodlightAccessoryImpl_DestructObject(LTDeviceFloodlightAccessoryImpl *accessory) {
    LT_UNUSED(accessory);
}

static bool LTDeviceFloodlightAccessoryImpl_ConstructObject(LTDeviceFloodlightAccessoryImpl *accessory) {
    LT_UNUSED(accessory);
    return true;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceFloodlightAccessory, LTDeviceFloodlightAccessoryImpl,
    SetCommand,
    GetCommand,
    IsMounted,
    OnConnectionChange,
    NoConnectionChange,
    OnPIRTrigger,
    NoPIRTrigger,
    OnFloodlightBrightnessChange,
    NoFloodlightBrightnessChange,
    OnFloodlightOverheatedState,
    NoFloodlightOverheatedState

);

define_LTObjectLibrary(1, LTDeviceFloodlightAccessory_LibInit, LTDeviceFloodlightAccessory_LibFini);
