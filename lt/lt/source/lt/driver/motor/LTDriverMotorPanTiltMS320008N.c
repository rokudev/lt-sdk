/*******************************************************************************
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for Pan and Tilt Motor
 ******************************************************************************/
/** @file LTDriverMotorPanTiltMS320008N.c Implementation of Pan and Tilt Motor
 */


#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>
#include <lt/driver/motor/LTDriverMotorPanTilt.h>
#include "motor.h"

DEFINE_LTLOG_SECTION("drv.motor.ms320008N")

#define MIN_SPEED 1
#define MAX_SPEED 9

typedef enum {
    kMotorState_Idle,
    kMotorState_Moving,
    kMotorState_Calibrating,
    kMotorState_Error
} MotorState;

typedef s32 (MotorIOCb)(s32 command, unsigned long arg);
static const LTTime kMS320008N_PollInterval = LTTimeInitializer_Milliseconds(10);

static struct statics {
    LTCore                 *pCore;
    ILTEvent               *iEvent;
    ILTThread              *iThread;
    LTThread                hWorkerThread;
    LTEvent                 hMotorEvent;
    MotorState              state;
    LTMotorPanTilt_Position currentPosition;
    MotorStatus             motorState;
    LTMotorPanTilt_SentryPath    *sentryPath;
    u32                           sentryPathIndex;
    LTMotorPanTilt_SentryWaypoint currentWaypoint;  // For event dispatch, since sentry path could be cleared before
                                                    // event is dispatched
    bool                          sentryPathPaused;
    bool          calibrationMove;
    bool          ascendingSentryPath;
    LTTime        calibrationInterval;
    MotorIOCb    *pfnMotorIO;  // Pointer to a motor I/O callback function
} S;

static const LTArgsDescriptor s_motorEventArgs = {1, {kLTArgType_u32}};

/************ Forward declarations ******************/
static void LTDriverMotorPanTiltMS320008NImpl_CalibrateProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_MotorPollTimerProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_MoveProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_MoveProcRelease(LTThread_ReleaseReason releaseReason, void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_SetSentryPathProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_StopProc(void *pClientData);
static void LTDriverMotorPanTiltMS320008NImpl_SetSpeedProc(void *pClientData);
static bool LTDriverMotorPanTiltMS320008NImpl_Move(LTMotorPanTilt_Position *position);

static s32 pulseToAngle(s32 steps) {
    return ((float)steps * MOTORCOMMAND_STEP_ANGLE / (kMotorMicrostepPerFullStep * 2));
}

/** Conversion of angle to pulse */
static s32 angleToPulse(s32 angle) {
    return (s32)(angle * kMotorMicrostepPerFullStep * 2 / MOTORCOMMAND_STEP_ANGLE);    // Convert angle to pulses
}

static void LTDriverMotorPanTiltMS320008NImpl_MotorPollTimerProc(void *pClientData) {
    if (S.state == kMotorState_Error) return;
    S.pfnMotorIO(kMotorAction_GetStatus, (unsigned long)&S.motorState);
    S.currentPosition.pan  = pulseToAngle(S.motorState.x);
    S.currentPosition.tilt = pulseToAngle(S.motorState.y);
    LT_UNUSED(pClientData);
    if (S.state == kMotorState_Calibrating) {
        if (S.motorState.status == kMotorOperationalStatus_Stop) {
            S.state = kMotorState_Idle;
            S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Idle);
        }
    } else if (S.state == kMotorState_Moving) {
        if (S.motorState.status == kMotorOperationalStatus_Running) {
            S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Moving);
        } else {
            S.state = kMotorState_Idle;
            if (!S.sentryPath) {
                S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Idle);
            } else {
                if (S.sentryPathPaused) {
                    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_SentryPathPaused);
                } else {
                    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypointComplete);

                    if (LTTime_IsInfinite(S.sentryPath->waypoints[S.sentryPathIndex].dwellTime)) {
                        // Infinite dwell time, end of sentry path
                        lt_free(S.sentryPath);
                        S.sentryPath      = NULL;
                        S.sentryPathIndex = 0;
                        S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Idle);
                    } else {
                        // Dwell
                        S.iThread->SetTimer(S.hWorkerThread,
                                            S.sentryPath->waypoints[S.sentryPathIndex].dwellTime,
                                            LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc,
                                            NULL,
                                            NULL);
                    }
                }
            }
        }
    }
}

static LTDriverMotorPanTiltDirection LTDriverMotorPanTiltMS320008NImpl_GetSupportedDirections(void) {
    return kLTDriverMotorPanTiltDirection_Pan | kLTDriverMotorPanTiltDirection_Tilt;
}

static void LTDriverMotorPanTiltMS320008NImpl_CalibrateTerminateProc(void *pClientData) {
    LT_UNUSED(pClientData);
    S.calibrationMove = false;
    S.iThread->KillTimer(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_CalibrateTerminateProc, NULL);
}

static void LTDriverMotorPanTiltMS320008NImpl_CalibrateProc(void *pClientData) {
    LTMotorPanTilt_Position *calibrateOffset = NULL;
    if (pClientData) calibrateOffset = (LTMotorPanTilt_Position *)pClientData;
    S.state = kMotorState_Calibrating;
    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_CalibrateStart);
    u32 motorSpeed = (u32)kDefaultMotorSpeed;
    LTLOG("cali.start", "Setting default speed: %d", motorSpeed);
    S.pfnMotorIO(kMotorAction_MotorSpeed, (unsigned long)&motorSpeed);
    motor_reset_info_s motorResetState = (motor_reset_info_s){};
    S.pfnMotorIO(kMotorAction_Reset, (unsigned long)&motorResetState);
    S.calibrationMove = true;
    if (calibrateOffset) LTDriverMotorPanTiltMS320008NImpl_MoveProc(calibrateOffset);
}

static void LTDriverMotorPanTiltMS320008NImpl_CalibrateProcRelease(LTThread_ReleaseReason releaseReason,
                                                                   void                  *pClientData) {
    LT_UNUSED(releaseReason);
    LTMotorPanTilt_Position *newPos = (LTMotorPanTilt_Position *)pClientData;
    S.calibrationMove               = false;
    lt_free(newPos);
}

static void LTDriverMotorPanTiltMS320008NImpl_MoveProc(void *pClientData) {
    LTMotorPanTilt_Position *newPos = (LTMotorPanTilt_Position *)pClientData;
    MotorData data;
    if (S.state == kMotorState_Error) {
        LTLOG_REDALERT("move.fail.error", "Motor is in error, please reset");
        return;
    }
    if (!S.calibrationMove) {
        S.state = kMotorState_Moving;
    }
    S.pfnMotorIO(kMotorAction_GetStatus, (unsigned long)&S.motorState);
    if (newPos->relative) {
        data.deltaX = angleToPulse(newPos->pan);
        data.deltaY = angleToPulse(newPos->tilt);
        LTLOG_DEBUG("info.move.r", "relative move %d %d (%d %d)", data.deltaX, data.deltaY, S.motorState.x, S.motorState.y);
    } else {
        data.deltaX = angleToPulse(lt_abs(newPos->pan)) - S.motorState.x;
        data.deltaY = angleToPulse(lt_abs(newPos->tilt)) - S.motorState.y;
        LTLOG_DEBUG("info.move.a", "absolute move %d %d (%d %d)", data.deltaX, data.deltaY, S.motorState.x, S.motorState.y);
    }
    // Apply angle limits
    if (S.motorState.x + data.deltaX > (s32)MAX_PAN_MOTOR_PULSE) data.deltaX = (s32)MAX_PAN_MOTOR_PULSE - S.motorState.x;
    if (S.motorState.x + data.deltaX < 0) data.deltaX = -S.motorState.x;
    if (S.motorState.y + data.deltaY > (s32)MAX_TILT_MOTOR_PULSE) data.deltaY = (s32)MAX_TILT_MOTOR_PULSE - S.motorState.y;
    if (S.motorState.y + data.deltaY < 0) data.deltaY = -S.motorState.y;

    LTLOG_DEBUG("info.move.pulse", "pulse to motor-> %d %d", data.deltaX, data.deltaY);
    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Moving);                                                                                                     
    S.pfnMotorIO(kMotorAction_Move, (unsigned long)&data);
}

static void LTDriverMotorPanTiltMS320008NImpl_MoveProcRelease(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    LTMotorPanTilt_Position *newPos = (LTMotorPanTilt_Position *)pClientData;
    S.calibrationMove               = false;
    lt_free(newPos);
}

static void LTDriverMotorPanTiltMS320008NImpl_SetSentryPathProc(void *pClientData) {
    LTMotorPanTilt_SentryPath *sentryPath = (LTMotorPanTilt_SentryPath *)pClientData;
    S.iThread->KillTimer(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc, NULL);
    lt_free(S.sentryPath);
    S.sentryPath       = sentryPath;
    S.sentryPathIndex  = 0;
    S.sentryPathPaused = false;
    if (!S.sentryPath) {
        // Sentry path cleared
        return;
    }
    S.currentWaypoint = S.sentryPath->waypoints[S.sentryPathIndex];  // Save current waypoint for event dispatch
    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint);
    LTDriverMotorPanTiltMS320008NImpl_Move(&S.sentryPath->waypoints[S.sentryPathIndex].position);
}

static void LTDriverMotorPanTiltMS320008NImpl_PauseSentryPathProc(void *pClientData) {
    bool pause = (bool)pClientData;
    if (pause) {
        S.sentryPathPaused = true;
        S.pfnMotorIO(kMotorAction_Stop, 0);
        S.iThread->KillTimer(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc, NULL);
        // Pause notification will be sent at the next motor status poll when motor stops
    } else {
        S.sentryPathPaused = false;
        if (S.sentryPath) {
            S.currentWaypoint = S.sentryPath->waypoints[S.sentryPathIndex];  // Save current waypoint for event dispatch
            S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint);
            LTDriverMotorPanTiltMS320008NImpl_Move(&S.sentryPath->waypoints[S.sentryPathIndex].position);
        }
    }
}

static void LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc(void *pClientData) {
    S.iThread->KillTimer(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_DwellTimerProc, pClientData);
    if (S.sentryPathIndex == (S.sentryPath->numWaypoints - 1))
        S.ascendingSentryPath = false;
    else if (S.sentryPathIndex == 0)
        S.ascendingSentryPath = true;
    S.ascendingSentryPath ? ++S.sentryPathIndex : --S.sentryPathIndex;
    S.currentWaypoint = S.sentryPath->waypoints[S.sentryPathIndex];  // Save current waypoint for event dispatch
    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint);
    LTDriverMotorPanTiltMS320008NImpl_Move(&S.sentryPath->waypoints[S.sentryPathIndex].position);
}

static void LTDriverMotorPanTiltMS320008NImpl_StopProc(void *pClientData) {
    LT_UNUSED(pClientData);
    S.pfnMotorIO(kMotorAction_Stop, 0);
    S.iEvent->NotifyEvent(S.hMotorEvent, kLTDriverMotorPanTiltEvent_Stopped);
}

static void LTDriverMotorPanTiltMS320008NImpl_SetSpeedProc(void *pClientData) {
    u32 speed = (u32)pClientData;
    if (S.state == kMotorState_Error) {
        LTLOG_REDALERT("setspeed.fail.error", "Motor is in error, please reset");
        return;
    }
    u32 actualSpeed = kMinimumMotorSpeed + (speed - MIN_SPEED) * (kMaximumMotorSpeed - kMinimumMotorSpeed) / (MAX_SPEED - MIN_SPEED);
    if (actualSpeed < kMinimumMotorSpeed || actualSpeed > kMaximumMotorSpeed) {
        LTLOG_YELLOWALERT("speed.out.of.range", "User speed %d is out of range", speed);
        return;
    }
    S.pfnMotorIO(kMotorAction_MotorSpeed, (unsigned long)&actualSpeed);
}

static void LTDriverMotorPanTiltMS320008NImpl_Calibrate(LTMotorPanTilt_Position *position) {
    LTLOG("cali", "Calibrating Pan and Tilt Motor");
    LTMotorPanTilt_Position *calibOffset = NULL;
    if (position) {
        calibOffset = (LTMotorPanTilt_Position *)lt_malloc(sizeof(LTMotorPanTilt_Position));
        if (!calibOffset) {
            LTLOG_REDALERT("cali.fail.malloc", "Failed to allocate memory for calibration offset");
            return;
        }
        *calibOffset = *position;
        LTLOG("cali.offset", "relative: %s pan: %d tilt: %d", calibOffset->relative ? "true" : "false", calibOffset->pan, calibOffset->tilt);
    }
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_CalibrateProc, LTDriverMotorPanTiltMS320008NImpl_CalibrateProcRelease, calibOffset);
}

static bool LTDriverMotorPanTiltMS320008NImpl_GetProperty(const char *name, void *value) {
    if (lt_strcmp(name, "calibrationInterval") == 0) {
        *(LTTime *)value = S.calibrationInterval;
        return true;
    }
    return false;
}

static bool LTDriverMotorPanTiltMS320008NImpl_SetProperty(const char *name, const void *value) {
    if (lt_strcmp(name, "calibrationInterval") == 0) {
        S.calibrationInterval = *(LTTime *)value;
        if (LTTime_IsInfinite(S.calibrationInterval)) {
            S.iThread->KillTimer(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_CalibrateProc, NULL);
        } else {
            S.iThread->SetTimer(S.hWorkerThread, S.calibrationInterval, LTDriverMotorPanTiltMS320008NImpl_CalibrateTerminateProc, NULL, NULL);
            LTDriverMotorPanTiltMS320008NImpl_CalibrateProc(NULL);
        }
        return true;
    } else if (lt_strcmp(name, "motoriofn") == 0) {
        // Substitute motor fn. For testing purposes only.
        // A test harness would set this property to a mock function to intercept motor commands.
        // Driver will be returned to normal operation on next library re-open.
        LTLOG_YELLOWALERT("MotorIOCb", "motor IO function is substituted for testing");
        S.pfnMotorIO = value;
        return true;
    }
    return false;
}

static void DispatchMotorEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    void *pEventData = NULL;
    LTDriverMotorPanTiltEvent eventType  = (LTDriverMotorPanTiltEvent)LTArgs_u32At(0, args);
    if (!proc) return;
    if (eventType == kLTDriverMotorPanTiltEvent_Idle || eventType == kLTDriverMotorPanTiltEvent_Moving
        || eventType == kLTDriverMotorPanTiltEvent_SentryPathPaused) {
        pEventData = &S.currentPosition;
    } else if (eventType == kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint
               || eventType == kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypointComplete) {
        pEventData = &S.currentWaypoint;
    }
    (*(LTDriverMotorPanTiltEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDriverMotorPanTiltMS320008NImpl_GetPosition(LTMotorPanTilt_Position *position) {
    position->pan      = S.motorState.x;
    position->tilt     = S.motorState.y;
    position->relative = false;
}

static bool LTDriverMotorPanTiltMS320008NImpl_GetMaxPosition(LTMotorPanTilt_Position *position) {
    if (!position) return false;
    position->pan      = kMotorAngle_XMax;
    position->tilt     = kMotorAngle_YMax;
    position->relative = false;
    return true;
}

static bool LTDriverMotorPanTiltMS320008NImpl_GetMinPosition(LTMotorPanTilt_Position *position) {
    if (!position) return false;
    position->pan      = 0;
    position->tilt     = 0;
    position->relative = false;
    return true;
}

static bool LTDriverMotorPanTiltMS320008NImpl_Move(LTMotorPanTilt_Position *position) {
    LTLOG_DEBUG("move", "## Move to %d, %d, %s", position->pan, position->tilt, position->relative ? "relative" : "absolute");
    LTMotorPanTilt_Position *newPos = lt_malloc(sizeof(LTMotorPanTilt_Position));  // this allocated memory gets freed
                                                                                   // in MoveProcRelease function
    *newPos = *position;
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_MoveProc, LTDriverMotorPanTiltMS320008NImpl_MoveProcRelease, newPos);
    return true;
}

static bool LTDriverMotorPanTiltMS320008NImpl_SetSentryPath(LTMotorPanTilt_SentryPath *sentryPath) {
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_SetSentryPathProc, NULL, sentryPath);
    return true;
}

static bool LTDriverMotorPanTiltMS320008NImpl_PauseSentryPath(bool pause) {
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_PauseSentryPathProc, NULL, (void *)pause);
    return true;
}

static void LTDriverMotorPanTiltMS320008NImpl_Stop(void) {
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_StopProc, NULL, NULL);
}

static void LTDriverMotorPanTiltMS320008NImpl_SetSpeed(s32 speedSps) {
    S.iThread->QueueTaskProc(S.hWorkerThread, LTDriverMotorPanTiltMS320008NImpl_SetSpeedProc, NULL, (void *)speedSps);
}

static void LTDriverMotorPanTiltMS320008NImpl_OnMotorEvent(LTDriverMotorPanTiltEventProc proc, void *pClientData) {
    S.iEvent->RegisterForEvent(S.hMotorEvent, proc, NULL, pClientData, false);
}

static void LTDriverMotorPanTiltMS320008NImpl_NoMotorEvent(LTDriverMotorPanTiltEventProc proc) {
    S.iEvent->UnregisterFromEvent(S.hMotorEvent, proc);
}

static bool LTDriverMotorPanTiltMS320008NImpl_IrcutOpen(void) {
    return IrcutOpen();
}

static bool LTDriverMotorPanTiltMS320008NImpl_IrcutClose(void) {
    return IrcutClose();
}

static bool LTDriverMotorPanTiltMS320008NImpl_IrcutReset(void) {
    return IrcutReset();
}

/*________________________________________
/ LTDriverMedia library initialization */
static void LTDriverMotorPanTiltMS320008NImpl_LibFini(void) {
    S.iThread->Terminate(S.hWorkerThread);
    S.iThread->WaitUntilFinished(S.hWorkerThread, LTTime_Infinite());
    S.iEvent->Destroy(S.hMotorEvent);
    lt_destroyhandle(S.hWorkerThread);
    lt_free(S.sentryPath);
    S.sentryPath  = NULL;
    S.hMotorEvent = 0;
    MotorClose();
}

static bool LTDriverMotorPanTiltMS320008NImpl_LibInit(void) {
    bool result           = false;
    S.pCore               = LT_GetCore();
    S.iThread             = lt_getlibraryinterface(ILTThread, S.pCore);
    S.iEvent              = lt_getlibraryinterface(ILTEvent, S.pCore);
    S.calibrationInterval = LTTime_Infinite();
    S.state               = kMotorState_Idle;
    S.sentryPath          = NULL;
    S.sentryPathIndex     = 0;
    S.pfnMotorIO          = MotorOpen();
    do {
        S.hMotorEvent = S.pCore->CreateEvent(&s_motorEventArgs, DispatchMotorEvent, NULL, NULL, NULL);
        if (!S.hMotorEvent) {
            LTLOG_REDALERT("init.fail.event", "could not create motor event");
            break;
        }
        S.hWorkerThread = S.pCore->CreateThread("MotorPanTiltWorker");
        if (!S.hWorkerThread) {
            LTLOG_REDALERT("init.fail.thread", "could not create worker thread");
            break;
        }
        S.iThread->SetStackSize(S.hWorkerThread, 1024);
        S.iThread->Start(S.hWorkerThread, NULL, NULL);
        S.iThread->SetTimer(S.hWorkerThread, kMS320008N_PollInterval, LTDriverMotorPanTiltMS320008NImpl_MotorPollTimerProc, NULL, NULL);
        result = true;
    } while (0);
    if (!result) {
        LTLOG_REDALERT("init.fail.result", "Failed to Init MotorPanTiltI2c");
        LTDriverMotorPanTiltMS320008NImpl_LibFini();
    } else {
        LTDriverMotorPanTiltMS320008NImpl_Calibrate(NULL);
    }

    return result;
}

/*___________________________________________________
 / LTDriverMotorPanTiltMS320008N library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverMotorPanTiltMS320008N, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverMotorPanTiltMS320008N)
LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverMotorPanTilt){
    .GetSupportedDirections = &LTDriverMotorPanTiltMS320008NImpl_GetSupportedDirections,
    .GetProperty            = &LTDriverMotorPanTiltMS320008NImpl_GetProperty,
    .SetProperty            = &LTDriverMotorPanTiltMS320008NImpl_SetProperty,
    .Calibrate              = &LTDriverMotorPanTiltMS320008NImpl_Calibrate,
    .GetPosition            = &LTDriverMotorPanTiltMS320008NImpl_GetPosition,
    .GetMaxPosition         = &LTDriverMotorPanTiltMS320008NImpl_GetMaxPosition,
    .GetMinPosition         = &LTDriverMotorPanTiltMS320008NImpl_GetMinPosition,
    .Move                   = &LTDriverMotorPanTiltMS320008NImpl_Move,
    .SetSentryPath          = &LTDriverMotorPanTiltMS320008NImpl_SetSentryPath,
    .PauseSentryPath        = &LTDriverMotorPanTiltMS320008NImpl_PauseSentryPath,
    .Stop                   = &LTDriverMotorPanTiltMS320008NImpl_Stop,
    .SetSpeed               = &LTDriverMotorPanTiltMS320008NImpl_SetSpeed,
    .OnMotorEvent           = &LTDriverMotorPanTiltMS320008NImpl_OnMotorEvent,
    .NoMotorEvent           = &LTDriverMotorPanTiltMS320008NImpl_NoMotorEvent,
    .IrcutOpen              = &LTDriverMotorPanTiltMS320008NImpl_IrcutOpen,
    .IrcutClose             = &LTDriverMotorPanTiltMS320008NImpl_IrcutClose,
    .IrcutReset             = &LTDriverMotorPanTiltMS320008NImpl_IrcutReset,
}
LTLIBRARY_DEFINITION
;

LTLIBRARY_EXPORT_INTERFACES(LTDriverMotorPanTiltMS320008N, (ILTDriverMotorPanTilt));
