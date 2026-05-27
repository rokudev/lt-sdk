/*******************************************************************************
 * source/lt/device/motor/LTMotorPanTilt.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>
#include <lt/driver/motor/LTDriverMotorPanTilt.h>

DEFINE_LTLOG_SECTION("device.motorpantilt")

typedef enum {
    LTMotorPanTiltState_Unknown = 0,
    LTMotorPanTiltState_Idle,
    LTMotorPanTiltState_Moving,
    LTMotorPanTiltState_Calibrating,
} LTMotorPanTiltState;

enum {
    kLTMotorPanTiltPanTolerance = 1,
    kLTMotorPanTiltTiltTolerance = 1,
};

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTMotorPanTilt, LTMotorPanTiltImpl) {
    LTMutex *mutex;
    LTLibrary *pDriverLibrary;
    ILTDriverMotorPanTilt *iDriver;
    ILTEvent *iEvent;
    LTEvent hMotorMovementEvent;
    LTEvent hMotorStateChangeEvent;
    LTEvent hSentryPathStateChangeEvent;
    LTDriverMotorPanTiltDirection supportedDirections;
    u32 maxPanAngle;
    u32 maxTiltAngle;

    bool bIsOpen;
    LTMotorPanTiltState state;
    LTMotorPanTilt_Position currentPosition;
    LTMotorPanTilt_Position targetPosition;
    LTMotorPanTilt_MovementData movementData;
    LTDriverMotorPanTiltDirection openedDirections;
} LTOBJECT_API;

static const LTArgsDescriptor MotorEventArgs = {2, { kLTArgType_u32, kLTArgType_pointer }};

/* _______________________________
 *  Object private utility functions
*/
static void LTMotorPanTiltImpl_ChangeState(LTMotorPanTiltImpl *motor, LTMotorPanTiltState newState) {
    void *pEventData = NULL;
    LTMotorPanTilt_StateChangeEvent event = kLTMotorPanTilt_StateChangeEvent_Error;
    if (motor->state == newState) return;
    motor->state = newState;
    if (newState == LTMotorPanTiltState_Idle) {
        pEventData = &motor->currentPosition;
        event = kLTMotorPanTilt_StateChangeEvent_Idle;
    } else if (newState == LTMotorPanTiltState_Moving) {
        pEventData = &motor->movementData;
        event = kLTMotorPanTilt_StateChangeEvent_Moving;
    } else if (newState == LTMotorPanTiltState_Calibrating) {
        event = kLTMotorPanTilt_StateChangeEvent_Calibrating;
    }
    motor->iEvent->NotifyEvent(motor->hMotorStateChangeEvent, (u32)event, pEventData);
}

static void LTMotorPanTiltImpl_DispatchMovementEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    LTMotorPanTilt_OnMovementEventProc *callback = (LTMotorPanTilt_OnMovementEventProc *)proc;
    callback((LTMotorPanTilt_MovementChangeEvent)LTArgs_u32At(0, args), (void *)LTArgs_pointerAt(1, args), pClientData);
}

static void LTMotorPanTiltImpl_DispatchStateChangeEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    LTMotorPanTilt_OnStateChangeEventProc *callback = (LTMotorPanTilt_OnStateChangeEventProc *)proc;
    callback((LTMotorPanTilt_StateChangeEvent)LTArgs_u32At(0, args), (void *)LTArgs_pointerAt(1, args), pClientData);
}

static void LTMotorPanTiltImpl_DispatchSentryPathStateChangeEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    LTMotorPanTilt_OnSentryPathStateChangeEventProc *callback = (LTMotorPanTilt_OnSentryPathStateChangeEventProc *)proc;
    callback((LTMotorPanTilt_SentryEvent)LTArgs_u32At(0, args), (void *)LTArgs_pointerAt(1, args), pClientData);
}

static void LTMotorPanTiltImpl_OnMotorEvent(LTDriverMotorPanTiltEvent event, void *pEventData, void *pClientData) {
    LTMotorPanTiltImpl *motor = (LTMotorPanTiltImpl *)pClientData;
    LTMotorPanTilt_Position *position = (LTMotorPanTilt_Position *)pEventData;
    LT_ASSERT(motor != NULL);
    switch(event) {
        case kLTDriverMotorPanTiltEvent_CalibrateStart:
            LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Calibrating);
            break;
        case kLTDriverMotorPanTiltEvent_Moving:
            motor->currentPosition = *position;
            motor->movementData.fromPosition = motor->currentPosition;
            if (motor->state != LTMotorPanTiltState_Moving) {
                motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hMotorMovementEvent, kLTMotorPanTilt_MovementChangeEvent_StartedMovement, &motor->movementData);
            } else {
                motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hMotorMovementEvent, kLTMotorPanTilt_MovementChangeEvent_InProgressMovement, &motor->movementData);
            }
            LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Moving);
            break;
        case kLTDriverMotorPanTiltEvent_Idle:
            motor->currentPosition = *position;
            if (motor->state == LTMotorPanTiltState_Moving) {
                u32 panDiff = lt_labs(motor->movementData.targetPosition.pan - motor->currentPosition.pan);
                u32 tiltDiff = lt_labs(motor->movementData.targetPosition.tilt - motor->currentPosition.tilt);
                if (panDiff <= kLTMotorPanTiltPanTolerance && tiltDiff <= kLTMotorPanTiltTiltTolerance) {
                    motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hMotorMovementEvent, kLTMotorPanTilt_MovementChangeEvent_CompletedMovement, &motor->movementData);
                } else {
                    motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hMotorMovementEvent, kLTMotorPanTilt_MovementChangeEvent_FailedMovement, &motor->movementData);
                }
            }
            LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Idle);
            break;
        case kLTDriverMotorPanTiltEvent_SentryPathPaused:
            motor->currentPosition = *position;
            motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hSentryPathStateChangeEvent, kLTMotorPanTilt_SentryEvent_Paused, &motor->currentPosition);
            LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Idle);
            break;
        case kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint:
            {
                LTMotorPanTilt_SentryWaypoint *waypoint = (LTMotorPanTilt_SentryWaypoint *)pEventData;
                motor->movementData.targetPosition = waypoint->position;
                motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hSentryPathStateChangeEvent, kLTMotorPanTilt_SentryEvent_MovingToWaypoint, waypoint);
                LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Moving);
            }
            break;
        case kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypointComplete:
            {
                LTMotorPanTilt_SentryWaypoint *waypoint = (LTMotorPanTilt_SentryWaypoint *)pEventData;
                motor->currentPosition = waypoint->position;
                motor->iEvent->NotifyEvent(((LTMotorPanTiltImpl *)pClientData)->hSentryPathStateChangeEvent, kLTMotorPanTilt_SentryEvent_Dwelling, waypoint);
                LTMotorPanTiltImpl_ChangeState(motor, LTMotorPanTiltState_Idle);
            }
            break;
        default:
            break;

    }
    LT_UNUSED(pEventData);
    LT_UNUSED(pClientData);
}

/*  ________________________________
 *  Object public API implementation
 */
static bool LTMotorPanTiltImpl_SupportsMotorType(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Type motorType) {
    bool panSupported = (motor->supportedDirections & kLTDriverMotorPanTiltDirection_Pan);
    bool tiltSupported = (motor->supportedDirections & kLTDriverMotorPanTiltDirection_Tilt);

    switch (motorType) {
        case kLTMotorPanTilt_Type_Pan:
            return panSupported;
        case kLTMotorPanTilt_Type_Tilt:
            return tiltSupported;
        case kLTMotorPanTilt_Type_PanTilt:
            return panSupported && tiltSupported;
        default:
            break;
    }
    return false;
}

static bool LTMotorPanTiltImpl_GetProperty(LTMotorPanTiltImpl *motor, const char *name, void *value) {
    return motor->iDriver->GetProperty(name, value);
}

static bool LTMotorPanTiltImpl_SetProperty(LTMotorPanTiltImpl *motor, const char *name, const void *value) {
    return motor->iDriver->SetProperty(name, value);
}

static bool LTMotorPanTiltImpl_OpenMotor(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Type motorType) {
    motor->mutex->API->Lock(motor->mutex);
    if (!LTMotorPanTiltImpl_SupportsMotorType(motor, motorType)) {
        motor->mutex->API->Unlock(motor->mutex);
        return false;
    }

    switch (motorType) {
        case kLTMotorPanTilt_Type_Pan:
            motor->openedDirections = kLTDriverMotorPanTiltDirection_Pan;
            break;
        case kLTMotorPanTilt_Type_Tilt:
            motor->openedDirections = kLTDriverMotorPanTiltDirection_Tilt;
            break;
        case kLTMotorPanTilt_Type_PanTilt:
            motor->openedDirections = kLTDriverMotorPanTiltDirection_Pan | kLTDriverMotorPanTiltDirection_Tilt;
            break;
    }
    motor->bIsOpen = true;
    motor->mutex->API->Unlock(motor->mutex);
    return true;
}

static bool LTMotorPanTiltImpl_IsOpen(LTMotorPanTiltImpl *motor) {
    bool res = false;
    motor->mutex->API->Lock(motor->mutex);
    res = motor->bIsOpen;
    motor->mutex->API->Unlock(motor->mutex);
    return res;
}

static void LTMotorPanTiltImpl_CloseMotor(LTMotorPanTiltImpl *motor) {
    motor->mutex->API->Lock(motor->mutex);
    motor->bIsOpen = false;
    motor->mutex->API->Unlock(motor->mutex);
}

static bool LTMotorPanTiltImpl_GetMinPosition(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Position *position) {
    bool result = false;
    if (!position) return false;
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        result = motor->iDriver->GetMinPosition(position);
    } else {
        *position = (LTMotorPanTilt_Position){};
    }
    motor->mutex->API->Unlock(motor->mutex);
    return result;
}

static bool LTMotorPanTiltImpl_GetMaxPosition(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Position *position) {
    bool result = false;
    if (!position) return false;
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        result = motor->iDriver->GetMaxPosition(position);
    } else {
        *position = (LTMotorPanTilt_Position){};
    }
    motor->mutex->API->Unlock(motor->mutex);
    return result;
}

static bool LTMotorPanTiltImpl_GetCurrentPosition(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Position *pCurrentPosition) {
    bool result = false;
    if (!motor || !pCurrentPosition) return false;
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        *pCurrentPosition = motor->currentPosition;
        result = true;
    } else {
        *pCurrentPosition = (LTMotorPanTilt_Position){};
    }
    motor->mutex->API->Unlock(motor->mutex);
    return result;
}

static void LTMotorPanTiltImpl_MoveToPosition(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Position * position) {
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        motor->movementData.fromPosition = motor->currentPosition;
        motor->movementData.targetPosition = *position;
        if (position->relative) {
            motor->movementData.targetPosition.pan += motor->currentPosition.pan;
            motor->movementData.targetPosition.tilt += motor->currentPosition.tilt;
            motor->movementData.targetPosition.relative = false;
        }
        motor->iDriver->Move(position);
    }
    motor->mutex->API->Unlock(motor->mutex);
}

static bool LTMotorPanTiltImpl_SetSentryPath(LTMotorPanTiltImpl *motor, LTMotorPanTilt_SentryPath *sentryPath) {
    bool res = false;
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        res = motor->iDriver->SetSentryPath(sentryPath);
    }
    motor->mutex->API->Unlock(motor->mutex);
    return res;
}

static bool LTMotorPanTiltImpl_PauseSentryPath(LTMotorPanTiltImpl *motor, bool pause) {
    bool res = false;
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        res = motor->iDriver->PauseSentryPath(pause);
    }
    motor->mutex->API->Unlock(motor->mutex);
    return res;
}

static void LTMotorPanTiltImpl_Reset(LTMotorPanTiltImpl *motor, LTMotorPanTilt_Position *position) {
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        motor->iEvent->NotifyEvent(motor->hMotorStateChangeEvent, kLTMotorPanTilt_StateChangeEvent_Resetting);
        motor->iDriver->Calibrate(position);
    }
    motor->mutex->API->Unlock(motor->mutex);
}

static void LTMotorPanTiltImpl_SetSpeed(LTMotorPanTiltImpl *motor, s32 speed) {
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        motor->iDriver->SetSpeed(speed);
    }
    motor->mutex->API->Unlock(motor->mutex);
}

static void LTMotorPanTiltImpl_StopMotor(LTMotorPanTiltImpl *motor) {
    motor->mutex->API->Lock(motor->mutex);
    if (motor->bIsOpen) {
        motor->iDriver->Stop();
    }
    motor->mutex->API->Unlock(motor->mutex);
}

static void LTMotorPanTiltImpl_IrcutOpen(LTMotorPanTiltImpl *ircut) {
    ircut->mutex->API->Lock(ircut->mutex);
        ircut->iDriver->IrcutOpen();
    ircut->mutex->API->Unlock(ircut->mutex);
}

static void LTMotorPanTiltImpl_IrcutClose(LTMotorPanTiltImpl *ircut) {
    ircut->mutex->API->Lock(ircut->mutex);
        ircut->iDriver->IrcutClose();
    ircut->mutex->API->Unlock(ircut->mutex);
}

static void LTMotorPanTiltImpl_IrcutReset(LTMotorPanTiltImpl *ircut) {
    ircut->mutex->API->Lock(ircut->mutex);
        ircut->iDriver->IrcutReset();
    ircut->mutex->API->Unlock(ircut->mutex);
}

static void LTMotorPanTiltImpl_CalibrationInterval(LTMotorPanTiltImpl *motor, LTTime period) {
    motor->mutex->API->Lock(motor->mutex);
    motor->iDriver->SetProperty("calibrationInterval", &period);
    motor->mutex->API->Unlock(motor->mutex);
}

static void LTMotorPanTiltImpl_OnMotorMovementEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnMovementEventProc callback, void *clientData) {
    motor->iEvent->RegisterForEvent(motor->hMotorMovementEvent, callback, NULL, clientData, false);
}

static void LTMotorPanTiltImpl_NoMotorMovementEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnMovementEventProc callback) {
    motor->iEvent->UnregisterFromEvent(motor->hMotorMovementEvent, callback);
}

static void LTMotorPanTiltImpl_OnMotorStateChangeEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnStateChangeEventProc callback, void *clientData) {
    motor->iEvent->RegisterForEvent(motor->hMotorStateChangeEvent, callback, NULL, clientData, false);
}

static void LTMotorPanTiltImpl_NoMotorStateChangeEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnStateChangeEventProc callback) {
    motor->iEvent->UnregisterFromEvent(motor->hMotorStateChangeEvent, callback);
}

static void LTMotorPanTiltImpl_OnSentryPathStateChangeEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnSentryPathStateChangeEventProc callback, void *clientData) {
    motor->iEvent->RegisterForEvent(motor->hSentryPathStateChangeEvent, callback, NULL, clientData, false);
}

static void LTMotorPanTiltImpl_NoSentryPathStateChangeEvent(LTMotorPanTiltImpl *motor, LTMotorPanTilt_OnSentryPathStateChangeEventProc callback) {
    motor->iEvent->UnregisterFromEvent(motor->hSentryPathStateChangeEvent, callback);
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTMotorPanTiltImpl_DestructObject(LTMotorPanTiltImpl *motor) {
    LTMotorPanTiltImpl_CloseMotor(motor);
    lt_destroyobject(motor->mutex);
    motor->iDriver->NoMotorEvent(LTMotorPanTiltImpl_OnMotorEvent);
    lt_closelibrary(motor->pDriverLibrary);
    lt_destroyhandle(motor->hMotorMovementEvent);
    lt_destroyhandle(motor->hMotorStateChangeEvent);
    lt_destroyhandle(motor->hSentryPathStateChangeEvent);
}

static bool LTMotorPanTiltImpl_ConstructObject(LTMotorPanTiltImpl *motor) {
    // Open the motor driver using info obtained from DeviceConfig
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    const char *driverName = NULL;

    // Initialize the motor object
    motor->bIsOpen = false;
    motor->openedDirections = 0;
    motor->maxPanAngle = 0;
    motor->maxTiltAngle = 0;
    motor->pDriverLibrary = NULL;
    motor->iDriver = NULL;
    motor->iEvent = NULL;
    motor->hMotorMovementEvent = LTHANDLE_INVALID;
    motor->hMotorStateChangeEvent = LTHANDLE_INVALID;
    motor->hSentryPathStateChangeEvent = LTHANDLE_INVALID;
    motor->supportedDirections = 0;

    if (!deviceConfig) {
        LTLOG_REDALERT("init.fail.devcon", "could not open device config");
        return false;
    }

    driverName = deviceConfig->GetDriverAt("LTDeviceMotorPanTilt", 0);
    lt_closelibrary(deviceConfig);

    if (!driverName) {
        LTLOG_YELLOWALERT("init.fail.nodrv", "no driver name found in device config");
        return false;
    }

    do {
        motor->pDriverLibrary = LT_GetCore()->OpenLibrary(driverName);
        motor->iDriver = (ILTDriverMotorPanTilt *)lt_getlibraryinterface(ILTDriverMotorPanTilt, motor->pDriverLibrary);
        if (!motor->iDriver) {
            LTLOG_YELLOWALERT("init.fail.drv.iface", "could not get driver pan tilt interface");
            break;
        }

        motor->mutex = lt_createobject(LTMutex);
        if (!motor->mutex) {
            LTLOG_YELLOWALERT("init.fail.mutex", "could not create mutex");
            break;
        }

        motor->hMotorMovementEvent = LT_GetCore()->CreateEvent(&MotorEventArgs, LTMotorPanTiltImpl_DispatchMovementEvent, NULL, NULL, NULL);
        if (!motor->hMotorMovementEvent) {
            LTLOG_YELLOWALERT("init.fail.move.event", "could not create motor movement event");
            break;
        }

        motor->hMotorStateChangeEvent = LT_GetCore()->CreateEvent(&MotorEventArgs, LTMotorPanTiltImpl_DispatchStateChangeEvent, NULL, NULL, NULL);
        if (!motor->hMotorStateChangeEvent) {
            LTLOG_YELLOWALERT("init.fail.state.event", "could not create motor state change event");
            break;
        }

        motor->hSentryPathStateChangeEvent = LT_GetCore()->CreateEvent(&MotorEventArgs, LTMotorPanTiltImpl_DispatchSentryPathStateChangeEvent, NULL, NULL, NULL);
        if (!motor->hSentryPathStateChangeEvent) {
            LTLOG_YELLOWALERT("init.fail.sentry.event", "could not create sentry path state change event");
            break;
        }

        motor->iDriver->OnMotorEvent(LTMotorPanTiltImpl_OnMotorEvent, motor);

        motor->supportedDirections = motor->iDriver->GetSupportedDirections();
        motor->iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
        return true;
    } while (0);

    LTMotorPanTiltImpl_DestructObject(motor);
    return false;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTMotorPanTilt, LTMotorPanTiltImpl,
    SupportsMotorType,
    GetProperty,
    SetProperty,
    OpenMotor,
    IsOpen,
    CloseMotor,
    GetMinPosition,
    GetMaxPosition,
    GetCurrentPosition,
    MoveToPosition,
    SetSentryPath,
    PauseSentryPath,
    Reset,
    SetSpeed,
    CalibrationInterval,
    OnMotorMovementEvent,
    NoMotorMovementEvent,
    OnMotorStateChangeEvent,
    NoMotorStateChangeEvent,
    OnSentryPathStateChangeEvent,
    NoSentryPathStateChangeEvent,
    StopMotor,
    IrcutOpen,
    IrcutClose,
    IrcutReset
);

define_LTOBJECT_EXPORTLIBRARY(
    LTDeviceMotorPanTilt, 1,
    LTMotorPanTiltImpl
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  10-Nov-23   aurelian    created
 */
