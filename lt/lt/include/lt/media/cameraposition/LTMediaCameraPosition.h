/*******************************************************************************
 * <lt/position/camera/LTMediaCameraPosition.h> LTMediaCameraPosition
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup LTMediaCameraPosition LTMediaCameraPosition
 * @{
 *
 * @brief LT Library for controlling position
 *
 * This Library provides control logic to a platform's camera motor.
 */

#ifndef LT_INCLUDE_LT_POSITION_LTMEDIACAMERAPOSITION_H
#define LT_INCLUDE_LT_POSITION_LTMEDIACAMERAPOSITION_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>

LT_EXTERN_C_BEGIN

/*_______________________________________________________________________
  LTMediaCameraPosition types */

typedef enum {
    kLTMediaCameraPosition_IdleMode = 0,
    kLTMediaCameraPosition_SentryMode = 1,
    kLTMediaCameraPosition_TrackingMode = 2,
    kLTMediaCameraPosition_SentryAndTrackingMode = kLTMediaCameraPosition_SentryMode | kLTMediaCameraPosition_TrackingMode,
} LTMediaCameraPosition_Mode;

typedef enum {
  kLTMediaCameraPositioningStateChangeEvent_Off,
  kLTMediaCameraPositioningStateChangeEvent_Sentry,
  kLTMediaCameraPositioningStateChangeEvent_Tracking,
  kLTMediaCameraPositioningStateChangeEvent_SentryAndTracking,
  kLTMediaCameraPositioningStateChangeEvent_Manual,
} LTMediaCameraPositionStateChangeEvent;

typedef void (LTMediaCameraPosition_OnStateChangeEventProc)(LTMediaCameraPositionStateChangeEvent state, void *pClientData);
/**<
 * Callback for cameraposition state change events.
 *
 * This callback is called to provide the client with position state change events.
 *
 * @param state the cameraposition state change event type which has occurred.
 * @param eventData (not implemented) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *                The following #event types and corresponding #eventData types are supported:
 *                       kLTMediaCameraPositioningStateChangeEvent_Off:            None
 *                       kLTMediaCameraPositioningStateChangeEvent_Sentry:         LTMediaCameraPositionSentryWaypointsList
 *                       kLTMediaCameraPositioningStateChangeEvent_Tracking:       None
 *                       kLTMediaCameraPosition_SentryAndTrackingMode:             LTMediaCameraPositionSentryWaypointsList
 * @param pClientData client supplied optional context data that is passed back to the client's event callback during event notification.
 */

/* ______________________________________________________________________________
 * Library Root Interface
 */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTMediaCameraPosition, 1);

struct LTMediaCameraPositionApi {
    INHERIT_LIBRARY_BASE
    void (*OnStateChangeEvent)(LTMediaCameraPosition_OnStateChangeEventProc *pStateChangeEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< Registers a state change event callback procedure for receipt of state events as they occur.
          *
          *  @param pStateChangeEventProc the clients event callback procedure.
          *  @param pClientData client supplied optional context data that is passed back to the client's event callback during event notification.
          */

    void (*NoStateChangeEvent)(LTMediaCameraPosition_OnStateChangeEventProc *pStateChangeEventProc);
        /**< Unregisters a previously registered event callback. */

    void (*Switch)(LTMediaCameraPosition_Mode mode);
        /**< Puts the camera motor in sentry mode.
          *
          *  @param mode the mode to switch to.
          */

    LTMediaCameraPosition_Mode (*GetMode)(void);
        /**< Returns the camera position mode. */

    bool (*MarkWaypoint)(u8 waypointIndex);
        /*< Marks a waypoint in the sentry path.
        * @param waypointIndex the index of the waypoint to mark with the current position in the custom sentry path.
        */

    bool (*SetNewSentryPath)(LTMotorPanTilt_SentryPath *newPath);
        /*< Sets a new sentry path.
        * @param newPath the new path to set.
        */

    void (*GetSentryPath)(LTMotorPanTilt_SentryPath **sentry);
        /*< Get a current sentry path.
        * @param sentry pointer to saved LTMotorPanTilt_SentryPath struct.
        */

    void (*ClearCustomSentryPath)(void);
        /*< Clears custom sentry path and replace with default path.
        */

    bool (*SaveSentryPath)(void);
        /*< Save sentry path to LTSystemSettings.
        *  @return true on success, false on failure
        */

    bool (*ResetMotor)(LTMotorPanTilt_Position position);
        /*< Resets the motor.
        * @param position additional offset after resetting
        */

    void (*SetSpeed)(s32 speed);
        /*< Sets the speed of the of the motor.
        * @param speed value in the range of 1 to 9.
        */

    s32 (*GetSpeed)(void);
        /*< Get the speed of the of the motor.
        */

    bool (*GetCurrentPosition)(LTMotorPanTilt_Position *position);
        /*< Gets the current position of the motor.
        * @param position the current position of the motor.
        */

    void (*MoveManually)(LTMotorPanTilt_Position position);
        /*< Moves the motor to a position manually.
        * @param position the position to move to.
        */

    void (*DeferMotionDetection)(void);
        /*< Defer motion detection engine if motor state is idle.
        */

    void (*StopMotor)(void);
        /*< Stops the motor manually
        */

    void (*IsVideoRotation)(bool videoRotation);
    /*< Sets the video rotation flag
        */
};

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_POSITION_LTMEDIACAMERAPOSITION_H */

/** @} */