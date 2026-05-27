/*******************************************************************************
 * lt/source/lt/device/flpir/LTDeviceFlPIR.c
 *
 * LT Device Library for the floodlight PIR sensors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/flpir/LTDeviceFlPIR.h>

DEFINE_LTLOG_SECTION("ldev.flpir");

/*******************************************************************************
 * macros
 ******************************************************************************/
#define MIN_EVENT_DETECT_DELAY    1000
#define MAX_EVENT_DETECT_DELAY    3000
#define ROUNDING_TO_NEAREST_INT   50
#define RANGE_TO_PERCENT          100
#define MAX_SENSITIVITY           255

typedef enum {
    kMotionStateEventIdle = 0,
    kMotionStateEventChange
} MotionEventType;

static const LTArgsDescriptor MotionEventArgs = {1, {kLTArgType_u32}};
typedef void MotionEventProc(MotionEventType eventType, void *pClientData);

/*  ___________________________
 *  Object private data members
 */
typedef struct {
    u8 pirSensitivity;
    u8 pirZone;
    u8 pirAlgo;
} PirParameters;

typedef_LTObjectImpl(LTDeviceFlPIR, LTDeviceFlPIRImpl) {
    LTLibrary       *pDriverLibrary;
    ILTDriverFlPIR  *iDriverFlPIR;
    LTDriverLibrary *pDriverLib;
    LTEvent          hMotionEvent;
    ILTEvent        *iEvent;
    PirParameters    pirSettings;
} LTOBJECT_API;

static bool LTDeviceFlPIRImpl_SetPIRSensitivity(LTDeviceFlPIRImpl *flPir, u8 sensistivityVal) {
    if (!flPir) return false;
    PirParameters pirParameters;
    if (sensistivityVal > RANGE_TO_PERCENT) sensistivityVal = RANGE_TO_PERCENT;
    flPir->pirSettings.pirSensitivity = ((sensistivityVal * MAX_SENSITIVITY) + ROUNDING_TO_NEAREST_INT) / RANGE_TO_PERCENT;
    pirParameters.pirSensitivity = flPir->pirSettings.pirSensitivity;
    pirParameters.pirZone = flPir->pirSettings.pirZone;
    pirParameters.pirAlgo = flPir->pirSettings.pirAlgo;
    if (!flPir->iDriverFlPIR->SetPIRSettings((u8 *)&pirParameters)) {
        LTLOG_YELLOWALERT("f.setpirsens", "Failed to SetPIRSensitivity");
        return false;
    }
    return true;
}

static bool LTDeviceFlPIRImpl_GetPIRSensitivity(LTDeviceFlPIRImpl *flPir, u8 *sensistivityVal) {
    if (!flPir || !sensistivityVal) return false;
    if (!flPir->iDriverFlPIR->GetPIRSettings((u8 *)&(flPir->pirSettings))) {
        LTLOG_YELLOWALERT("f.getpirsens", "Failed to GetPIRSensitivity");
        return false;
    }
    *sensistivityVal = (((flPir->pirSettings.pirSensitivity * RANGE_TO_PERCENT) + (MAX_SENSITIVITY >> 1 )) / MAX_SENSITIVITY);
    return true;
}

static bool LTDeviceFlPIRImpl_SetPIRZone(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIRZone pirZone) {
    if (!flPir) return false;
    PirParameters pirParameters;
    if (pirZone > (kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right)) {
        pirZone = kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right;
    }
    flPir->pirSettings.pirZone = pirZone;
    pirParameters.pirSensitivity = flPir->pirSettings.pirSensitivity;
    pirParameters.pirZone = flPir->pirSettings.pirZone;
    pirParameters.pirAlgo = flPir->pirSettings.pirAlgo;
    if (!flPir->iDriverFlPIR->SetPIRSettings((u8 *)&pirParameters)) {
        LTLOG_YELLOWALERT("f.setpirzone", "Failed to SetPIRZone");
        return false;
    }
    return true;
}

static bool LTDeviceFlPIRImpl_GetPIRZone(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIRZone *pirZone) {
    if (!flPir || !pirZone) return false;
    if (!flPir->iDriverFlPIR->GetPIRSettings((u8 *)&(flPir->pirSettings))) {
        LTLOG_YELLOWALERT("f.getpirzone", "Failed to GetPIRZone");
        return false;
    }
    *pirZone = flPir->pirSettings.pirZone;
    return true;
}

static bool LTDeviceFlPIRImpl_SetPIRAlgo(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIRAlgo pirAlgo) {
    if (!flPir) return false;
    PirParameters pirParameters;
    if ((pirAlgo != kLTDeviceFlPIRAlgo_Off) && (pirAlgo != kLTDeviceFlPIRAlgo_HualaiActivate)) {
        pirAlgo = kLTDeviceFlPIRAlgo_Off;
    }
    flPir->pirSettings.pirAlgo = pirAlgo;
    pirParameters.pirSensitivity = flPir->pirSettings.pirSensitivity;
    pirParameters.pirZone = flPir->pirSettings.pirZone;
    pirParameters.pirAlgo = flPir->pirSettings.pirAlgo;
    if (!flPir->iDriverFlPIR->SetPIRSettings((u8 *)&pirParameters)) {
        LTLOG_YELLOWALERT("f.setpiralgo", "Failed to SetPIRAlgo");
        return false;
    }
    return true;
}

static bool LTDeviceFlPIRImpl_GetPIRAlgo(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIRAlgo *pirAlgo) {
    if (!flPir || !pirAlgo) return false;
    if (!flPir->iDriverFlPIR->GetPIRSettings((u8 *)&(flPir->pirSettings))) {
        LTLOG_YELLOWALERT("f.getpiralgo", "Failed to GetPIRAlgo");
        return false;
    }
    *pirAlgo = flPir->pirSettings.pirAlgo;
    return true;
}

static void LTDeviceFlPIRImpl_OnMotionEvent(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIR_OnMotionStateChangeEventProc *callback, void *clientData) {
    if (!flPir || !callback) return;
    flPir->iEvent->RegisterForEvent(flPir->hMotionEvent, callback, NULL, clientData, false);
}

static bool LTDeviceFlPIRImpl_NoMotionEvent(LTDeviceFlPIRImpl *flPir, LTDeviceFlPIR_OnMotionStateChangeEventProc *callback) {
    if (!flPir || !callback) return false;
    return flPir->iEvent->UnregisterFromEvent(flPir->hMotionEvent, callback);
}

static void LTDeviceFlPIRImpl_OnPIREventProc(LTDriverFlPIREvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pEventData);
    if (!pClientData) return;
    LTDeviceFlPIRImpl *flPir = (LTDeviceFlPIRImpl *)pClientData;
    if (event == kLTDriverFlPIREvent_MotionDetected) {
        flPir->iEvent->NotifyEvent(flPir->hMotionEvent, kLTDriverFlPIREvent_MotionDetected);
    }
}

static bool LTDeviceFlPIRImpl_MotionDelay(LTDeviceFlPIRImpl *flPir, u16 motionDelay) {
    if (!flPir) return false;
    if (motionDelay >= MIN_EVENT_DETECT_DELAY && motionDelay <= MAX_EVENT_DETECT_DELAY) {
        flPir->iDriverFlPIR->EventTriggerDelay(motionDelay);
    } else {
        flPir->iDriverFlPIR->EventTriggerDelay(MIN_EVENT_DETECT_DELAY);
    }
    return true;
}

static void MotionEventDispatch(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    MotionEventType  eventType = (MotionEventType)LTArgs_u32At(0, args);
    MotionEventProc *callback  = (MotionEventProc *)proc;
    callback(eventType, pClientData);
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceFlPIRImpl_DestructObject(LTDeviceFlPIRImpl *flPir) {
    if (flPir->hMotionEvent != LTHANDLE_INVALID) {
        lt_destroyhandle(flPir->hMotionEvent);
    }
    if (flPir->iDriverFlPIR) {
        flPir->iDriverFlPIR->NoPIREvent(LTDeviceFlPIRImpl_OnPIREventProc);
    }
    lt_closelibrary(flPir->pDriverLibrary);
}

static bool LTDeviceFlPIRImpl_ConstructObject(LTDeviceFlPIRImpl *flPir) {
    const char *driverName = NULL;
    LTCore     *pCore      = LT_GetCore();
    flPir->pirSettings                = (PirParameters) {};
    flPir->pirSettings.pirSensitivity = 100; /* default PIR sensitivity value */
    flPir->pirSettings.pirZone        = kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right;  /* default PIR zone value */
    flPir->pirSettings.pirAlgo        = kLTDeviceFlPIRAlgo_Off;                                                          /* default PIR algorithm value */
    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("f.devconf", "could not open device config");
            break;
        }

        driverName = deviceConfig->GetDriverAt("LTDeviceFlPIR", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
            break;
        }
        lt_closelibrary(deviceConfig);
        flPir->pDriverLibrary = LT_GetCore()->OpenLibrary(driverName);
        flPir->iDriverFlPIR   = (ILTDriverFlPIR *)lt_getlibraryinterface(ILTDriverFlPIR, flPir->pDriverLibrary);
        if (!flPir->iDriverFlPIR) {
            LTLOG_YELLOWALERT("f.drv.iface", "could not get driver FlPIR interface");
            break;
        }
        flPir->hMotionEvent = pCore->CreateEvent(&MotionEventArgs, MotionEventDispatch, NULL, NULL, NULL);
        if (!flPir->hMotionEvent) {
            LTLOG_YELLOWALERT("f.evt", "Failed to create motion event");
            break;
        }
        flPir->iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
        flPir->iDriverFlPIR->OnPIREvent(LTDeviceFlPIRImpl_OnPIREventProc, flPir);
        return true;
    } while (0);

    LTDeviceFlPIRImpl_DestructObject(flPir);
    return false;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceFlPIR, LTDeviceFlPIRImpl,
    SetPIRSensitivity,
    GetPIRSensitivity,
    SetPIRZone,
    GetPIRZone,
    SetPIRAlgo,
    GetPIRAlgo,
    OnMotionEvent,
    NoMotionEvent,
    MotionDelay
);

define_LTOBJECT_EXPORTLIBRARY(LTDeviceFlPIR, 1, LTDeviceFlPIRImpl);
