/*******************************************************************************
 * LT Media Camera Position library.
 *
 * Pan Camera Position Library - handles motor movement logic.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/cameraposition/LTMediaCameraPosition.h>
#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/LTTypes.h>
#include <lt/system/settings/LTSystemSettings.h>
DEFINE_LTLOG_SECTION("cameraposition");

/*
* Open issues:
* - need to fix the hack/bug where the motor state is not updated correctly.
* Notes:
* - the following names: sentry path, waypoint list, custom path, and scan stop list are used interchangeably across the fw/cloud/mobile app.
* - the sentry path is allocated repeatedly here because the motor driver takes ownership of the path and frees it when the path is completed. This should probably be changed to a single allocation and pass read-only to the motor driver.
* - currently, we use a fixed size for the sentry path. This would probably make more sense as a dynamic data structure that can change size to allow arbitrary length paths.
*/

#define SENTRY_PATH_NUM_WAYPOINTS 4
#define LAST_DPAD_PAN             "iot/slide_pan"
#define LAST_DPAD_TILT            "iot/slide_tilt"
#define PRESET_PAN                "iot/preset_pan"
#define PRESET_TILT               "iot/preset_tilt"
#define SENTRYMODE                "iot/scanMode"
#define MOTIONDETECTIONZONEENABLE "iot/motionDetectionZoneEnable"

enum {
    kLTMotorPanTiltPanTolerance = 1,
    kLTMotorPanTiltTiltTolerance = 1,
};

typedef enum {
    LTMotorMinSpeed     = 1,
    LTMotorMaxSpeed     = 9,
    LTMotorDefaultSpeed = 5,
} LTMotorSpeed;

/* Duplicate enum from lt/source/lt/device/motor/LTDeviceMotorPanTilt.c, should probably be in the header file instead. */
typedef enum {
    LTMotorPanTiltState_Unknown = 0,
    LTMotorPanTiltState_Idle,
    LTMotorPanTiltState_Moving,
    LTMotorPanTiltState_Calibrating,
} LTMotorPanTiltState;

/*_______________________________________________________________________
  LTMediaCameraPosition.c static variables. */
static u32 MoxelWidth = 0;
static u32 MoxelHeight = 0;
static double DegreesPerPixelHorizontal;
static double DegreesPerPixelVertical;
static bool isCalibrationDone = false;
static bool s_IsVideoRotated = false;

/*_______________________________________________________________________
  LTMediaCameraPosition.c static constants. */
static const u8 SecondsToWait = 15;

/*_______________________________________________________________________
  LTMediaCameraPosition.c static variables managed by LibInit/LibFini */
static struct Statics {
    LTCore *core;
    LTMediaMotionDetection *motion;
    LTDeviceMedia *media;
    LTThread cameraPositionThread;
    ILTThread *iThread;
    ILTEvent *iEvent;
    LTMotorPanTiltState motorState;
    LTMediaCameraPosition_Mode mode;
    LTMotorPanTilt_Position originalPosition;
    LTMotorPanTilt_Position dpadPosition;
    LTTime returnToPositionTimeout;
    bool bNeedToReturnToPreviousPosition;
    bool bNeedToRecordPosition;
    bool bSentryPathActive;
    LTMotorPanTilt_SentryPath customPath;
    u8 motorSpeed;
    bool isManualMove;
    u8 roiBoundaryPercentageX;
    u8 roiBoundaryPercentageY;
    u16 FieldOfViewHorizontal;
    u16 FieldOfViewVertical;
    LTEvent hStateChangeEvent;
    LTMotorPanTilt *pMotorPanTilt;
    LTSystemSettings *pSystemSettings;
    LTMutex *mutex;
} S;

static const LTArgsDescriptor s_CameraPositionStateChangeEventArgs = {1, {kLTArgType_s32}};

/*________________________________________________
  LTMediaCameraPosition.c forward declarations */
static bool LTMediaCameraPosition_ThreadInit(void);
static void LTMediaCameraPosition_ThreadExit(void);
static void LTMediaCameraPositionImpl_LibFini(void);
static void LTMediaCameraPosition_SwitchMode(LTMediaCameraPosition_Mode mode);
static bool LTMediaCameraPosition_SetCustomSentryPath(void);
static bool LTMediaCameraPosition_SaveSentryPath(void);

static void LTMediaCameraPosition_SetStateToIdleProc(void *pClientData) {
    LT_UNUSED(pClientData);

    /* This function is part of the hack where the event notifications for OnMotorMovementEvents do not seem to trigger on the correct scenario. I.e. it seems to tell us the motor completed a move before it actually finished moving. */
    S.iThread->KillTimer(S.cameraPositionThread, LTMediaCameraPosition_SetStateToIdleProc, NULL);
    S.motion->ResumeMotionDetection();
}

static void LTMediaCameraPosition_LoadDefaultSentryPath(void) {
    /* Initialise the waypoints to (60,22), (150,22), (210,22) and (300,22). */
    u32 panInitScanStop = 0;
    for (u32 i = 0; i < SENTRY_PATH_NUM_WAYPOINTS; i++) {
        S.customPath.waypoints[i].position.pan = (panInitScanStop += (i % 2) ? 90 : 60);
        S.customPath.waypoints[i].position.tilt = 22;
        S.customPath.waypoints[i].position.relative = false;
        S.customPath.waypoints[i].dwellTime = LTTime_Milliseconds(10000);
    }
    S.customPath.numWaypoints = SENTRY_PATH_NUM_WAYPOINTS;
    LTMediaCameraPosition_SaveSentryPath();
}

static void LTMediaCameraPosition_ReturnToPreviousPosition(void *pClientData) {
    LT_UNUSED(pClientData);
    /* This is the callback for when the 15s timer expires. */

    S.iThread->KillTimer(S.cameraPositionThread, LTMediaCameraPosition_ReturnToPreviousPosition, NULL);
    /* Grab the current position. */

    LTMotorPanTilt_Position currPosition;
    S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, &currPosition);
    if (S.motion->IsMotionZoneEnabled() && (S.mode == kLTMediaCameraPosition_TrackingMode || S.mode == kLTMediaCameraPosition_IdleMode)) {
        if (S.bNeedToReturnToPreviousPosition) {
            u32 panDiff = lt_labs(currPosition.pan - S.originalPosition.pan);
            u32 tiltDiff = lt_labs(currPosition.tilt - S.originalPosition.tilt);

            /* Ensuring that new position actually differs from old one before issuing move. */
            if ((panDiff > kLTMotorPanTiltPanTolerance) || (tiltDiff > kLTMotorPanTiltTiltTolerance)) {
                S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &S.originalPosition);
                S.bNeedToReturnToPreviousPosition = false;
            }
        }
    } else if ((S.mode == kLTMediaCameraPosition_TrackingMode || S.mode == kLTMediaCameraPosition_IdleMode) && S.motorState == LTMotorPanTiltState_Idle && !S.bNeedToRecordPosition) {
        /* returns to a last d-pad position in case camera tracking or idle mode */
        u32 panDiff = lt_labs(currPosition.pan - S.dpadPosition.pan);
        u32 tiltDiff = lt_labs(currPosition.tilt - S.dpadPosition.tilt);
        if ((panDiff > kLTMotorPanTiltPanTolerance) || (tiltDiff > kLTMotorPanTiltTiltTolerance)) {
            S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &S.dpadPosition);
        }
    }

    if (S.bNeedToRecordPosition) {
        S.bNeedToRecordPosition = false;
        S.pSystemSettings->SetIntegerValue(LAST_DPAD_PAN, S.dpadPosition.pan);
        S.pSystemSettings->SetIntegerValue(LAST_DPAD_TILT, S.dpadPosition.tilt);
    }
}

static void ResetReturnToPositionTimer(void) {
    S.bNeedToReturnToPreviousPosition = true;

    S.iThread->KillTimer(S.cameraPositionThread, LTMediaCameraPosition_ReturnToPreviousPosition, NULL);
    /* New 15s timer. */
    S.iThread->SetTimer(S.cameraPositionThread, S.returnToPositionTimeout, LTMediaCameraPosition_ReturnToPreviousPosition, NULL, NULL);
}

static void OnSentryPathEvent(LTMotorPanTilt_SentryEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(pClientData); LT_UNUSED(eventData);

    switch (event) {
        case kLTMotorPanTilt_SentryEvent_MovingToWaypoint:
            LTLOG("sentry.moving", "moving to waypoint");
            break;
        case kLTMotorPanTilt_SentryEvent_Dwelling:
            LTLOG("sentry.dwelling", "at waypoint");
            break;
        case kLTMotorPanTilt_SentryEvent_Paused:
            LTLOG("sentry.paused", "at waypoint");
            break;
        default:
            LTLOG_YELLOWALERT("sentry.default", "unknown sentry path event: %d", event);
            break;
    }
}

static void OnMotionEvent(LTMediaMotionState motionState, u8 *motionUuid, LTMediaMotionDetection_Snapshot *motionSnapshot, void *clientData) {
    LT_UNUSED(clientData); LT_UNUSED(motionUuid); LT_UNUSED(motionSnapshot);

    switch (motionState) {
        case kLTMediaMotionState_Started:
            if (S.mode == kLTMediaCameraPosition_SentryAndTrackingMode && S.bSentryPathActive) {
                if (S.pMotorPanTilt) S.pMotorPanTilt->API->PauseSentryPath(S.pMotorPanTilt, true);
                S.bSentryPathActive = false;
            }
            break;
        case kLTMediaMotionState_Stopped:
            if (S.mode == kLTMediaCameraPosition_SentryAndTrackingMode && !S.bSentryPathActive) {
                S.motion->PauseMotionDetection();
                if (S.pMotorPanTilt) S.pMotorPanTilt->API->PauseSentryPath(S.pMotorPanTilt, false);
                S.bSentryPathActive = true;
            }
            break;
        default:
            /* Nothing to do in the other states. */
            break;
    }
}

static void OnZoneStateEvent(LTMediaZoneState ZoneState, void *clientData) {
    LT_UNUSED(clientData);
    switch (ZoneState) {
        case kLTMediaMotionState_EnableMotionZone:
            ResetReturnToPositionTimer();
            /* motion detection Zone: incompatible with sentry mode, so we switch mode. */
            if (S.mode == kLTMediaCameraPosition_SentryMode || S.mode == kLTMediaCameraPosition_SentryAndTrackingMode) {
                LTMediaCameraPosition_SwitchMode(S.mode & ~kLTMediaCameraPosition_SentryMode);
                S.pSystemSettings->SetIntegerValue(SENTRYMODE, 0);
            }
            break;
        case kLTMediaMotionState_SetMotionZone:
            if (isCalibrationDone) {
                S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, &S.originalPosition);
                S.pSystemSettings->SetIntegerValue(PRESET_PAN, S.originalPosition.pan);
                S.pSystemSettings->SetIntegerValue(PRESET_TILT, S.originalPosition.tilt);
            }
            break;
        default:
            /* Nothing to do in the other states. */
            break;
    }
}

static void OnMetadataEvent(LTMediaMotionDetection_Metadata *metadata, u8 *motionUuid, void *clientData) {
    LT_UNUSED(clientData); LT_UNUSED(motionUuid);

    /* Early return if not in the correct mode. */
    if ((S.mode != kLTMediaCameraPosition_TrackingMode && S.mode != kLTMediaCameraPosition_SentryAndTrackingMode) || S.motorState == LTMotorPanTiltState_Moving) {
        return;
    }

    /* SentryAndTracking specific behaviour: disable sentry and mark sentry path active as false/stopped. */
    if (S.mode == kLTMediaCameraPosition_SentryAndTrackingMode && S.bSentryPathActive)  {
        /* Pause sentry. */
        if (S.pMotorPanTilt) S.pMotorPanTilt->API->PauseSentryPath(S.pMotorPanTilt, true);
        S.bSentryPathActive = false;
        return;
    }

    if (metadata->regions->numRegions == 0) {
        LTLOG_DEBUG("onmetadata", "no regions found");
        return;
    }

    u32 midpointX = (metadata->regions->region[0].x1 + metadata->regions->region[0].x2) / 2;
    u32 midpointY = (metadata->regions->region[0].y1 + metadata->regions->region[0].y2) / 2;
    LTLOG_DEBUG("onmetadata.target", "roi midpoint @ (%lu,%lu)", LT_Pu32(midpointX), LT_Pu32(midpointY));

    /* Check if roi centre is within movement boundaries. */
    bool bWithinX = (midpointX >= (MoxelWidth *  (S.roiBoundaryPercentageX / 100))) || (midpointX <= (MoxelWidth * ((100 - S.roiBoundaryPercentageX) / 100)));
    bool bWithinY = (midpointY >= (MoxelHeight * (S.roiBoundaryPercentageY / 100))) || (midpointY <= (MoxelHeight * ((100 - S.roiBoundaryPercentageY) / 100)));

    if (!(bWithinX || bWithinY)) {
        /* If the motion is not within the outer boundary in either the x nor y then we return since that means the motion is already within focus.
           Suppose we partition our window to have a 3x3 grid and the motion is in the centre, then we don't need to move the camera.
           We can think of the bWithinX and bWithinY as the outer boundary or mask of the grid.
           If the motion is within the boundary then we need to move the camera to bring it into focus (i.e. place the motion at the centre of our view).
            [ XY | Y | XY ]
            [ X  |   |  X ]
            [ XY | Y | XY ]
        */
        return;
    } /* Outer boundary; carry on with the movement. */

    //LTLOG("onmetadata.motion", "converting to degrees");

    /*
        Calculate the relative movement needed to center the motion.
        We subtract the midpoint from the center of the frame to get the offset.
        Then we convert this pixel offset to degrees using the degrees per pixel ratio.
    */
    s32 diffX = midpointX - (MoxelWidth / 2);
    s32 diffY = (MoxelHeight / 2) - midpointY; /* Note: Y is inverted in image coordinates. */

    /* Convert pixel diffs to degree changes. */
    LTMotorPanTilt_Position relMotorPosition;
    relMotorPosition.pan = (s32)(diffX * DegreesPerPixelHorizontal);
    relMotorPosition.tilt = (s32)(diffY * DegreesPerPixelVertical);

    /* Only move in directions where motion is outside the ROI boundary. */
    relMotorPosition.pan *= bWithinX;
    relMotorPosition.tilt *= bWithinY;

    if (s_IsVideoRotated) {
        relMotorPosition.pan *= -1;
        relMotorPosition.tilt *= -1;
    }
    LTLOG_DEBUG("onmetadata.issuing.move", "aiming for (x,y) -> (%ld,%ld) ", LT_Ps32(relMotorPosition.pan), LT_Ps32(relMotorPosition.tilt));

    /* Move the camera to the calculated position. */
    relMotorPosition.relative = true;
    S.motorState = LTMotorPanTiltState_Moving;
    S.motion->PauseMotionDetection();
    if (S.pMotorPanTilt) S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &relMotorPosition);

}

static void OnMotorMovementEvent(LTMotorPanTilt_MovementChangeEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);

    LTMotorPanTilt_MovementData *pMotorMovementData = (LTMotorPanTilt_MovementData *)eventData;
    LT_UNUSED(pMotorMovementData);

    switch (event) {
        case kLTMotorPanTilt_MovementChangeEvent_StartedMovement:
        case kLTMotorPanTilt_MovementChangeEvent_InProgressMovement:
            break;
        case kLTMotorPanTilt_MovementChangeEvent_FailedMovement:
            if (!S.bNeedToRecordPosition) { // deliberate stop leads to failed event, ignoring "failed" log for this scenario
                LTLOG_DEBUG("onmotormovement.failed", "failed on our way to (%ld,%ld)", LT_Ps32(pMotorMovementData->fromPosition.pan), LT_Ps32(pMotorMovementData->fromPosition.tilt));
            }
            ResetReturnToPositionTimer();
            break;
        case kLTMotorPanTilt_MovementChangeEvent_CompletedMovement:
            isCalibrationDone = true;
            LTLOG("onmotormovement.complete", "completed movement");
            ResetReturnToPositionTimer();
            break;
        default:
            LTLOG_YELLOWALERT("onmotormovement", "unknown movement event: %d", event);
            break;
    }
}

static void OnMotorStateChangeEvent(LTMotorPanTilt_StateChangeEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(eventData);

    switch (event) {
        case kLTMotorPanTilt_StateChangeEvent_Resetting:
        case kLTMotorPanTilt_StateChangeEvent_Calibrating:
            S.motion->PauseMotionDetection();  /* Falls through */
        case kLTMotorPanTilt_StateChangeEvent_Moving:
            S.motorState = LTMotorPanTiltState_Moving;
            if (S.mode != kLTMediaCameraPosition_IdleMode) {
                S.motion->PauseMotionDetection();
            }
            break;
        case kLTMotorPanTilt_StateChangeEvent_Idle:
            //S.motion->ResumeMotionDetection();    //this should be the correct behaviour
            S.motorState = LTMotorPanTiltState_Idle;
            /* HACK: In reality, we should be simply setting the motor to idle and resuming MD. However, due to an apparent bug this doesn't work and we need to introduce an artificial delay to get the correct behaviour. */
            if (S.mode == kLTMediaCameraPosition_SentryAndTrackingMode || S.mode == kLTMediaCameraPosition_TrackingMode) {
                S.iThread->SetTimer(S.cameraPositionThread, LTTime_Milliseconds(1000), LTMediaCameraPosition_SetStateToIdleProc, NULL, NULL);
            }
            break;
        default:
            LTLOG_YELLOWALERT("onmotorstatechange", "unknown state event: %d", event);
            break;
    }
}

/*___________________________________________________
  Library public interface function implementation */

static void LTMediaCameraPosition_SwitchMode(LTMediaCameraPosition_Mode newMode) {
    if (S.mode == newMode) {
        /* Early return. */
        return;
    }
    LTMediaCameraPositionStateChangeEvent stateChangeEvent;

    /* Cleanup current mode. */
    switch (S.mode) {
        case kLTMediaCameraPosition_SentryMode:
        case kLTMediaCameraPosition_SentryAndTrackingMode:
            /* Stop the sentry path when leaving these modes. */
            if (S.pMotorPanTilt) S.pMotorPanTilt->API->SetSentryPath(S.pMotorPanTilt, NULL);
            S.bSentryPathActive = false;
            break;
        case kLTMediaCameraPosition_TrackingMode:
            /* Pause motion detection when leaving tracking mode. */
            S.motion->PauseMotionDetection();
            //S.bNeedToRecordPosition = true;
            break;
        default:
            /* Currently there are no specific cleanup needed for other modes. */
            break;
    }

    /* Setup new mode. */
    S.mode = newMode;
    switch (newMode) {
        case kLTMediaCameraPosition_IdleMode:
            /* Off/Idle: pause all movement (motor movement or sentry paths) but continue to get motion events. */
            /* Defaulting back to indoor camera behaviour. */
            S.motion->ResumeMotionDetection();
            ResetReturnToPositionTimer();
            if (S.pMotorPanTilt) S.pMotorPanTilt->API->SetSentryPath(S.pMotorPanTilt, NULL);
            S.bSentryPathActive = false;
            S.motorState = LTMotorPanTiltState_Idle;
            stateChangeEvent = kLTMediaCameraPositioningStateChangeEvent_Off;
            break;
        case kLTMediaCameraPosition_SentryMode:
            /* Sentry: incompatible with motion detection and zones, so we disable them. */
            if (S.motion->IsMotionZoneEnabled()) {
                S.motion->EnableMotionZones(false);
                S.pSystemSettings->SetIntegerValue(MOTIONDETECTIONZONEENABLE, false);
            }
            S.bSentryPathActive = true;
            S.motion->PauseMotionDetection();
            LTMediaCameraPosition_SetCustomSentryPath();
            stateChangeEvent = kLTMediaCameraPositioningStateChangeEvent_Sentry;
            break;
        case kLTMediaCameraPosition_TrackingMode:
            /* Tracking: resume motion detection in order to track motion */
            S.motion->ResumeMotionDetection();
            ResetReturnToPositionTimer();
            stateChangeEvent = kLTMediaCameraPositioningStateChangeEvent_Tracking;
            break;
        case kLTMediaCameraPosition_SentryAndTrackingMode:
            /* SentryTracking: combines behaviour from both sentry and tracking. */
            if (S.motion->IsMotionZoneEnabled()) {
                S.motion->EnableMotionZones(false);
                S.pSystemSettings->SetIntegerValue(MOTIONDETECTIONZONEENABLE, false);
            }
            S.bSentryPathActive = true;
            S.motion->PauseMotionDetection();
            LTMediaCameraPosition_SetCustomSentryPath();
            stateChangeEvent = kLTMediaCameraPositioningStateChangeEvent_SentryAndTracking;
            break;
        default:
            LTLOG_YELLOWALERT("switch.mode", "Unknown mode %d", newMode);
            return;
    }

    S.iEvent->NotifyEvent(S.hStateChangeEvent, stateChangeEvent);
}

static void LTMediaCameraPosition_MoveManually(LTMotorPanTilt_Position direction) {
    /* Manual moves coming via d-pad. */

    /* Activity, restart timer. */
    S.motorState = LTMotorPanTiltState_Moving;
    if (S.pMotorPanTilt) {
        if (!direction.relative) {
            S.isManualMove = true;
            S.pMotorPanTilt->API->SetSpeed(S.pMotorPanTilt, S.motorSpeed);
            S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &direction);
        } else {
            if (s_IsVideoRotated) {
                direction.pan = -(direction.pan);
                direction.tilt = -(direction.tilt);
            }
            S.isManualMove = true;
            S.pMotorPanTilt->API->SetSpeed(S.pMotorPanTilt, S.motorSpeed);
            S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &direction);
            S.motion->PauseMotionDetection();
            if (direction.relative && !direction.pan) {
                /* storing d-pad tilt to record in supportedSetting*/
                LTMotorPanTilt_Position currPosition;
                S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, &currPosition);
                currPosition.tilt += direction.tilt;
                S.dpadPosition.tilt = currPosition.tilt;
            }
        }
        S.bNeedToRecordPosition = true;
    }
}

static bool LTMediaCameraPosition_SetCustomSentryPath(void) {
    if (S.customPath.numWaypoints == 0 || S.customPath.numWaypoints > SENTRY_PATH_NUM_WAYPOINTS) {
        LTLOG_YELLOWALERT("custom.sentry.invalid", "Invalid custom sentry path");
        return false;
    }
    LTMotorPanTilt_SentryPath *sentryPath = lt_malloc(sizeof(LTMotorPanTilt_SentryPath));
    if (!sentryPath) {
        LTLOG_YELLOWALERT("custom.sentry.oom", "Failed to allocate memory for custom sentry path");
        return false;
    }

    lt_memcpy(sentryPath, &(S.customPath), sizeof(LTMotorPanTilt_SentryPath));

    if (S.pMotorPanTilt) {
        bool result = S.pMotorPanTilt->API->SetSentryPath(S.pMotorPanTilt, sentryPath);
        if (!result) {
            lt_free(sentryPath);
            LTLOG_YELLOWALERT("custom.sentry.set.fail", "Failed to set custom sentry path");
        }
        return result;
    }

    return false;
}

static bool LTMediaCameraPosition_SetNewSentryPath(LTMotorPanTilt_SentryPath *newPath) {
    S.mutex->API->Lock(S.mutex);
    if (!newPath || newPath->numWaypoints == 0 || newPath->numWaypoints > SENTRY_PATH_NUM_WAYPOINTS) {
        LTLOG_YELLOWALERT("new.sentry.invalid", "Invalid new sentry path");
        S.mutex->API->Unlock(S.mutex);
        return false;
    }

    /* Free the existing custom path. */
    lt_memset(&(S.customPath), 0, sizeof(LTMotorPanTilt_SentryPath));

    lt_memcpy(&(S.customPath), newPath, sizeof(LTMotorPanTilt_SentryPath));
    /* Set the new path. */
    LTLOG_SERVER("new.sentry.added", "auto-scan is %s", S.bSentryPathActive ? "enabled" : "disabled");
    S.mutex->API->Unlock(S.mutex);
    return S.bSentryPathActive ? LTMediaCameraPosition_SetCustomSentryPath() : true;
}

static void LTMediaCameraPosition_GetSentryPath(LTMotorPanTilt_SentryPath **sentry) {
    S.mutex->API->Lock(S.mutex);
    *sentry = &(S.customPath);
    S.mutex->API->Unlock(S.mutex);
}

static bool LTMediaCameraPosition_ResetMotor(LTMotorPanTilt_Position position) {
    if (S.pMotorPanTilt) {
        S.pMotorPanTilt->API->Reset(S.pMotorPanTilt, &position);
        return true;
    }
    return false;
}

static void LTMediaCameraPosition_OnStateChangeEvent(LTMediaCameraPosition_OnStateChangeEventProc *pStateChangeEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, S.hStateChangeEvent);
    iEvent->RegisterForEvent(S.hStateChangeEvent, pStateChangeEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTMediaCameraPosition_NoStateChangeEvent(LTMediaCameraPosition_OnStateChangeEventProc *pStateChangeEventProc) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, S.hStateChangeEvent);
    iEvent->UnregisterFromEvent(S.hStateChangeEvent, pStateChangeEventProc);
}

static LTMediaCameraPosition_Mode LTMediaCameraPosition_GetMode(void) {
    return S.mode;
}

static void LTMediaCameraPosition_StopMotor(void) {
    if (S.pMotorPanTilt) {
        S.pMotorPanTilt->API->StopMotor(S.pMotorPanTilt);
        S.pMotorPanTilt->API->SetSpeed(S.pMotorPanTilt, LTMotorDefaultSpeed);
        S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, &S.dpadPosition);
        S.bNeedToRecordPosition = true;
        ResetReturnToPositionTimer();
        S.motion->ResumeMotionDetection();
    }
}
static void LTMediaCameraPosition_IsVideoRotation(bool videoRotation) {
    s_IsVideoRotated = videoRotation;
}

static void LTMediaCameraPosition_SetSpeed(s32 speed) {
    if (S.pMotorPanTilt) {
        if ((speed < LTMotorMinSpeed) || (speed > LTMotorMaxSpeed)) {
            LTLOG("fail.motor.speed.set", "could not set motor speed, out of range");
            return;
        }
        S.motorSpeed = speed;
    }
}

static s32 LTMediaCameraPosition_GetSpeed(void) {
    if (S.pMotorPanTilt) return S.motorSpeed;
    return 0;
}

static bool LTMediaCameraPosition_GetCurrentPosition(LTMotorPanTilt_Position *position) {
    if (!position || !S.pMotorPanTilt) {
        return false;
    }

   return S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, position);
}

/*_________________________________
  Library private implementation */
static bool LTMediaCameraPosition_ThreadInit(void) {
    do {
        if (!(S.pMotorPanTilt = lt_createobject(LTMotorPanTilt))) {
            LTLOG_YELLOWALERT("threadinit.fail.motor.create", "could not create pan-tilt motor object");
            break;
        }

        if (!(S.pMotorPanTilt->API->OpenMotor(S.pMotorPanTilt, kLTMotorPanTilt_Type_PanTilt))) {
            LTLOG_YELLOWALERT("threadinit.fail.motor.open", "could not open pan-tilt motor object");
            break;
        }

        //moving motor to last position
        S.pSystemSettings->GetIntegerValue(PRESET_PAN, (s64*)&S.originalPosition.pan);
        S.pSystemSettings->GetIntegerValue(PRESET_TILT, (s64*)&S.originalPosition.tilt);

        S.pSystemSettings->GetIntegerValue(LAST_DPAD_PAN, (s64*)&S.dpadPosition.pan);
        S.pSystemSettings->GetIntegerValue(LAST_DPAD_TILT, (s64*)&S.dpadPosition.tilt);
        if (S.pMotorPanTilt) {
            S.pMotorPanTilt->API->SetSpeed(S.pMotorPanTilt, S.motorSpeed);
            S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &S.dpadPosition);
        }

        isCalibrationDone = false;
        /* Register for events. */
        S.motion->OnMetadataEvent(OnMetadataEvent, NULL, NULL);
        S.motion->OnMotionEvent(OnMotionEvent, NULL, NULL);
        S.motion->OnZoneStateEvent(OnZoneStateEvent, NULL, NULL);
        S.pMotorPanTilt->API->OnSentryPathStateChangeEvent(S.pMotorPanTilt, OnSentryPathEvent, NULL);
        S.pMotorPanTilt->API->OnMotorMovementEvent(S.pMotorPanTilt, OnMotorMovementEvent, NULL);
        S.pMotorPanTilt->API->OnMotorStateChangeEvent(S.pMotorPanTilt, OnMotorStateChangeEvent, NULL);

        return true;
    } while (false);
    return false;
}

static void LTMediaCameraPosition_StateChangeEventDispatchProc(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTMediaCameraPosition_OnStateChangeEventProc *callback = (LTMediaCameraPosition_OnStateChangeEventProc *)pEventProc;
    LTMediaCameraPositionStateChangeEvent eventState = (LTMediaCameraPositionStateChangeEvent)LTArgs_s32At(0, pEventArgs);
    callback(eventState, pEventProcClientData);
}

static void LTMediaCameraPosition_StateChangeEventDispatchCompleteProc(LTEvent hEvent, LTArgs *pEventArgs) {
    LT_UNUSED(hEvent); LT_UNUSED(pEventArgs);
}

static void LTMediaCameraPosition_ThreadExit(void) {
    LTLOG_DEBUG("threadexit", "closing LTMediaCameraPosition");
    /* NOTE: Not sure if this is correct: need to de-register OnMetadataEvent before destroying the motor as otherwise we get a race condition where after the motor is destroyed, OnMetadataEvent fires and tries to make a call to MoveToPosition on the motor object. */
    S.motion->NoMetadataEvent(OnMetadataEvent);
    S.motion->NoMotionEvent(OnMotionEvent);
    S.motion->NoZoneStateEvent(OnZoneStateEvent);
    if (S.pMotorPanTilt) {
        S.pMotorPanTilt->API->NoMotorMovementEvent(S.pMotorPanTilt,OnMotorMovementEvent);
        S.pMotorPanTilt->API->NoSentryPathStateChangeEvent(S.pMotorPanTilt, OnSentryPathEvent);
        /* BUG?: if we don't disable sentry path, the camera keeps on moving even after closing the motor...*/
        S.pMotorPanTilt->API->SetSentryPath(S.pMotorPanTilt, NULL);
        S.pMotorPanTilt->API->CloseMotor(S.pMotorPanTilt);
        lt_destroyobject(S.pMotorPanTilt);
        S.pMotorPanTilt = NULL;
        lt_closelibrary(S.pSystemSettings);
    }
}

static bool LTMediaCameraPosition_MarkWaypoint(u8 index) {
    if (index >= SENTRY_PATH_NUM_WAYPOINTS) {
        return false;
    }
    S.mutex->API->Lock(S.mutex);
    /* Save current position in slot at index. */
    if (S.pMotorPanTilt) {
        LTMotorPanTilt_Position pos;
        if (S.pMotorPanTilt->API->GetCurrentPosition(S.pMotorPanTilt, &pos)) {
            S.customPath.waypoints[index].position = pos;
            S.customPath.waypoints[index].dwellTime = LTTime_Milliseconds(10000);
            if ((index + 1) > (u8)S.customPath.numWaypoints) S.customPath.numWaypoints++;
            LTMediaCameraPosition_SaveSentryPath();
            if (S.bSentryPathActive) LTMediaCameraPosition_SetCustomSentryPath();
        }
        else {
            LTLOG("debug.setwaypoints.fail.get.position", "could not set current position");
            S.mutex->API->Unlock(S.mutex);
            return false;
        }
    }
    S.mutex->API->Unlock(S.mutex);
    return true;
}

static void LTMediaCameraPosition_ClearCustomSentryPath(void) {
    S.mutex->API->Lock(S.mutex);
    LTMediaCameraPosition_LoadDefaultSentryPath();
    if (S.mode == kLTMediaCameraPosition_SentryMode || S.mode == kLTMediaCameraPosition_SentryAndTrackingMode) {
        LTMediaCameraPosition_SetCustomSentryPath();
    }
    S.mutex->API->Unlock(S.mutex);
}

static bool LTMediaCameraPosition_SaveSentryPath(void) {
    static const u32 strSize = 80;
    char *sentryPathStr = lt_malloc(strSize);
    if (!sentryPathStr) {
        LTLOG("debug.setwaypoints.oom", "Failed to allocate memory for sentry path string");
        return false;
    }
    int offset = 0;
    offset += lt_snprintf(sentryPathStr + offset, strSize - offset, "%u", S.customPath.numWaypoints);
    for (u32 i = 0; i < S.customPath.numWaypoints; i++) {
        offset += lt_snprintf(sentryPathStr + offset, strSize - offset, ":%d:%d:%u:%d",
                            S.customPath.waypoints[i].position.pan,
                            S.customPath.waypoints[i].position.tilt,
                            (u32)LTTime_GetSeconds(S.customPath.waypoints[i].dwellTime),
                            S.customPath.waypoints[i].position.relative ? 1 : 0);
    }
    if (!S.pSystemSettings->SetStringValue("iot/scanStops", sentryPathStr)) {
        LTLOG_YELLOWALERT("setwaypoints.save", "cannot save waypoints in \"iot/scanStops\" key");
        lt_free(sentryPathStr);
        return false;
    }
    lt_free(sentryPathStr);
    return true;
}

static void LTMediaCameraPosition_StartMotionDetectionEngineProc(void *pClientData) {
    LT_UNUSED(pClientData);
    if (S.motorState == LTMotorPanTiltState_Idle) {
        S.iThread->KillTimer(S.cameraPositionThread, LTMediaCameraPosition_StartMotionDetectionEngineProc, NULL);
        S.motion->OnMotionEvent(OnMotionEvent, NULL, NULL);
        S.motion->StartDetectionEngine(true);
    }
}

static void LTMediaCameraPosition_DeferMotionDetection(void) {
    // ASPEN-8871 Start Motion detection engine if motor state is Idle
    if (S.motion) {
        if (S.motorState == LTMotorPanTiltState_Idle) {
            S.motion->OnMotionEvent(OnMotionEvent, NULL, NULL);
            S.motion->StartDetectionEngine(true);
        } else {
            S.iThread->SetTimer(S.cameraPositionThread, LTTime_Milliseconds(500), LTMediaCameraPosition_StartMotionDetectionEngineProc, NULL, NULL);
        }
    }
}

/*_____________________________________
  Library Initialization and Cleanup */

static bool LTMediaCameraPositionImpl_LibInit(void) {
    /* Initialise the statics */
    S = (struct Statics) {
        .core = LT_GetCore(),
        .motorState = LTMotorPanTiltState_Idle,
        .mode = kLTMediaCameraPosition_IdleMode,
        .returnToPositionTimeout = LTTime_Seconds(SecondsToWait),
        .bSentryPathActive = false,
        .bNeedToReturnToPreviousPosition = false,
        .bNeedToRecordPosition = true,
        .originalPosition = {0},
        .hStateChangeEvent = LT_GetCore()->CreateEvent(&s_CameraPositionStateChangeEventArgs,
                                                  (void *)&LTMediaCameraPosition_StateChangeEventDispatchProc,
                                                  &LTMediaCameraPosition_StateChangeEventDispatchCompleteProc,
                                                  NULL,
                                                  NULL)
    };

    do {
        /* Open the libraries. */
        if (!(S.motion = lt_openlibrary(LTMediaMotionDetection))) {
            LTLOG("init.fail.motion", "failed to open LTMediaMotionDetection");
            break;
        }

        if (!(S.media = lt_openlibrary(LTDeviceMedia))) {
            LTLOG("init.fail.media", "failed to open LTDeviceMedia");
            break;
        }

        LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
        if (!productConfig) {
            LTLOG("init.fail.productconfig", "failed to open LTProductConfig");
            break;
        }

        if(!(S.mutex   = lt_createobject(LTMutex))) {
            LTLOG("init.fail.mutex", "failed to create mutex object");
            break;
        }

        /* Get the configuration section. */
        u32 libSection = productConfig->GetLibraryConfigSection("LTMediaCameraPosition");
        if (libSection == 0) {
            LTLOG("init.fail.libsection", "failed to get library section for LTMediaCameraPosition");
            lt_closelibrary(productConfig);
            break;
        }

        S.roiBoundaryPercentageX = productConfig->ReadInteger(libSection, "ROIBoundaryPercentageX");
        S.roiBoundaryPercentageY = productConfig->ReadInteger(libSection, "ROIBoundaryPercentageY");
        if (S.roiBoundaryPercentageX == 0 || S.roiBoundaryPercentageY == 0) {
            LTLOG("init.fail.roiboundary", "failed to get ROI boundary percentage x or y");
            lt_closelibrary(productConfig);
            break;
        }

        S.FieldOfViewHorizontal = productConfig->ReadInteger(libSection, "FieldOfViewHorizontal");
        S.FieldOfViewVertical   = productConfig->ReadInteger(libSection, "FieldOfViewVertical");
        if (S.FieldOfViewHorizontal == 0 || S.FieldOfViewVertical == 0) {
            LTLOG("init.fail.fieldofview", "failed to get field of view (horizontal or vertical)");
            lt_closelibrary(productConfig);
            break;
        }

        /* Closing here since we do not need it anymore. */
        lt_closelibrary(productConfig);

        /* Get the moxel size. */
        LTMediaResolution moxelSize;
        if (!S.media->GetProperty("resolution.motion", &moxelSize)) {
            LTLOG("init.fail.moxel", "failed to get moxel size");
            break;
        }

        /* Calculate degrees per pixel. */
        MoxelWidth = moxelSize.width;
        MoxelHeight = moxelSize.height;
        DegreesPerPixelHorizontal = (double) (S.FieldOfViewHorizontal / MoxelWidth);
        DegreesPerPixelVertical = (double) (S.FieldOfViewVertical / MoxelHeight);

        S.isManualMove = false;
        S.motorSpeed = LTMotorDefaultSpeed;
        /* Event system. */
        S.iEvent = lt_getlibraryinterface(ILTEvent, S.core);
        if (!S.iEvent) {
            LTLOG("init.fail.event", "failed to get ILTEvent interface");
            break;
        }

        if (!(S.pSystemSettings = lt_openlibrary(LTSystemSettings))) {
            LTLOG_YELLOWALERT("threadinit.fail.system.settings", "could not open LTSystemSettings lib");
            break;
        }

        /* Create the thread. */
        S.cameraPositionThread = S.core->CreateThread("CameraPosition");
        if (!S.cameraPositionThread) {
            LTLOG("init.fail.thread", "failed to create thread");
            break;
        }

        /* Set the stack size and start the thread. */
        S.iThread = lt_gethandleinterface(ILTThread, S.cameraPositionThread);
        S.iThread->SetStackSize(S.cameraPositionThread, 1024);
        S.iThread->Start(S.cameraPositionThread, LTMediaCameraPosition_ThreadInit, LTMediaCameraPosition_ThreadExit);

        return true;

    } while (false);

    LTMediaCameraPositionImpl_LibFini();

    return false;
}

static void LTMediaCameraPositionImpl_LibFini(void) {
    LTLOG_DEBUG("libfinish", "closing LTMediaCameraPosition");
    /* QUESTION: What is the difference between iThread->Destroy and iThread->Terminate? */
    lt_destroyobject(S.mutex);
    S.mutex = NULL;
    if (S.iThread) {
        S.iThread->Destroy(S.cameraPositionThread);
    }

    /* Closing the libraries. */
    lt_closelibrary(S.media);
    lt_closelibrary(S.motion);

    /* Destroying the events. */
    S.core->Destroy(S.hStateChangeEvent);

    /* Resetting the statics. */
    S = (struct Statics){0};
}

/*________________________________________________________
  LTMediaCameraPosition library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaCameraPosition)
    .OnStateChangeEvent     = LTMediaCameraPosition_OnStateChangeEvent,
    .NoStateChangeEvent     = LTMediaCameraPosition_NoStateChangeEvent,

    .SetSpeed               = LTMediaCameraPosition_SetSpeed,
    .GetSpeed               = LTMediaCameraPosition_GetSpeed,
    .Switch                 = LTMediaCameraPosition_SwitchMode,
    .GetMode                = LTMediaCameraPosition_GetMode,
    .SetNewSentryPath       = LTMediaCameraPosition_SetNewSentryPath,
    .GetSentryPath          = LTMediaCameraPosition_GetSentryPath,
    .ResetMotor             = LTMediaCameraPosition_ResetMotor,
    .GetCurrentPosition     = LTMediaCameraPosition_GetCurrentPosition,
    .MoveManually           = LTMediaCameraPosition_MoveManually,
    .MarkWaypoint           = LTMediaCameraPosition_MarkWaypoint,
    .ClearCustomSentryPath  = LTMediaCameraPosition_ClearCustomSentryPath,
    .SaveSentryPath         = LTMediaCameraPosition_SaveSentryPath,
    .DeferMotionDetection   = LTMediaCameraPosition_DeferMotionDetection,
    .StopMotor              = LTMediaCameraPosition_StopMotor,
    .IsVideoRotation        = LTMediaCameraPosition_IsVideoRotation
LTLIBRARY_DEFINITION;
