/*******************************************************************************
 * include/lt/device/motor/LTDeviceMotorPanTilt.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_MOTOR_LTDEVICEMOTORPANTILT_H
#define ROKU_LT_INCLUDE_LT_DEVICE_MOTOR_LTDEVICEMOTORPANTILT_H
#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef enum LTMotorPanTilt_StateChangeEvent {
    kLTMotorPanTilt_StateChangeEvent_Idle,
    kLTMotorPanTilt_StateChangeEvent_Moving,
    kLTMotorPanTilt_StateChangeEvent_Calibrating,
    kLTMotorPanTilt_StateChangeEvent_Resetting,
    kLTMotorPanTilt_StateChangeEvent_Error,
} LTMotorPanTilt_StateChangeEvent;

typedef enum LTMotorPanTilt_MovementChangeEvent {
    kLTMotorPanTilt_MovementChangeEvent_StartedMovement,
    kLTMotorPanTilt_MovementChangeEvent_InProgressMovement,
    kLTMotorPanTilt_MovementChangeEvent_CompletedMovement,
    kLTMotorPanTilt_MovementChangeEvent_FailedMovement,
} LTMotorPanTilt_MovementChangeEvent;

typedef enum LTMotorPanTilt_SentryEvent {
    kLTMotorPanTilt_SentryEvent_MovingToWaypoint, // Motor is moving to the next waypoint
    kLTMotorPanTilt_SentryEvent_Paused,           // Sentry path is paused
    kLTMotorPanTilt_SentryEvent_Dwelling,         // Motor is dwelling at the current waypoint
} LTMotorPanTilt_SentryEvent;

typedef enum LTMotorPanTilt_Type {
    kLTMotorPanTilt_Type_Pan,
    kLTMotorPanTilt_Type_Tilt,
    kLTMotorPanTilt_Type_PanTilt
} LTMotorPanTilt_Type;

typedef struct LTMotorPanTilt_Position {
    s32  pan;
    s32  tilt;
    bool relative;
	u8   padding[7];
} LTMotorPanTilt_Position;

LT_STATIC_ASSERT_SIZE_32_64(LTMotorPanTilt_Position, 16, 16)

typedef struct LTMotorPanTilt_SentryWaypoint {
    LTTime dwellTime;
    LTMotorPanTilt_Position position;
} LTMotorPanTilt_SentryWaypoint;

LT_STATIC_ASSERT_SIZE_32_64(LTMotorPanTilt_SentryWaypoint, 24, 24)

typedef struct LTMotorPanTilt_SentryPath {
    u32 numWaypoints;
	u32 reserved; /**< make sure waypoints starts on 64 bit boundary for LTTime */
    /**< number of waypoints */
    LTMotorPanTilt_SentryWaypoint waypoints[4];
    /**< array of waypoints */
} LTMotorPanTilt_SentryPath;

typedef struct LTMotorPanTilt_MovementData {
    LTMotorPanTilt_Position fromPosition;
    LTMotorPanTilt_Position targetPosition;
} LTMotorPanTilt_MovementData;

typedef void (LTMotorPanTilt_OnStateChangeEventProc)(LTMotorPanTilt_StateChangeEvent event, void *eventData, void * pClientData);
/**< callback for motor state events.
 *
 *   This callback is called to provide the client with motor state events.
 *
 *   @param event the motor state event type which has occurred.
 *   @param eventData (optional) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *                    The following #event types and corresponding #eventData types are supported:
 *                         LTMotorPanTiltEvent_Moving:       LTMotorPanTiltMovementData ({current_position|target_position})
 *                         LTMotorPanTiltEvent_Idle:         LTMotorPanTilt_Position ({current_position})
 *                         LTMotorPanTiltEvent_Calibrating:  None
 *                         LTMotorPanTiltEvent_Resetting:    None
 *                         LTMotorPanTiltEvent_Failed:       None
 *                         LTMotorPanTiltEvent_Error:        LTMotorPanTiltLog ({driver_failures})
 *   @param pClientData the client data that was specified when the callback was registered.
 */

typedef void LTMotorPanTilt_OnMovementEventProc(LTMotorPanTilt_MovementChangeEvent event, void *eventData, void * pClientData);
/**< callback for motor state events.
 *
 *   This callback is called to provide the client with motor state events.
 *
 *   @param event the motor movement event type which has occurred.
 *   @param eventData (optional) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *                    The following #event types and corresponding #eventData types are supported:
 *                         kLTMotorPanTilt_MovementChangeEvent_StartedMovement:       LTMotorPanTiltMovementData ({current_position|target_position})
 *                         kLTMotorPanTilt_MovementChangeEvent_InProgressMovement:    LTMotorPanTiltMovementData ({current_position|target_position})
 *                         kLTMotorPanTilt_MovementChangeEvent_CompletedMovement:     LTMotorPanTiltMovementData ({current_position|target_position})
 *   @param pClientData the client data that was specified when the callback was registered.
 */

typedef void LTMotorPanTilt_OnSentryPathStateChangeEventProc(LTMotorPanTilt_SentryEvent event, void *eventData, void *pClientData);
/**<
 * Callback for motor sentry path events.
 *
 * This callback is called to provide the client with motor sentry path events.
 *
 * @param event the motor sentry path event type which has occurred.
 * @param eventData (optional) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *                The following #event types and corresponding #eventData types are supported:
 *                       LTMotorPanTiltSentryEvent_MovingToWayPoint: LTMotorPanTiltSentryWayPoint
 *                       LTMotorPanTiltSentryEvent_Paused:           LTMotorPanTilt_Position ({current_position})
 *                       LTMotorPanTiltSentryEvent_Dwelling:         LTMotorPanTiltSentryWayPoint
*/

typedef_LTObject(LTMotorPanTilt, 1) {
    bool (* SupportsMotorType)(LTMotorPanTilt *motor, LTMotorPanTilt_Type motorType);
        /**< checks if the motor driver supports the given motor type ({pan | tilt | both}).
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pMotorType the motor type.
         *   @return true if the driver supports the provided motor type, else false.
         */

    bool (* GetProperty)(LTMotorPanTilt *motor, const char *name, void *value);
        /**< gets a motor property by name.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param name the property to be get.
         *   @param value the property value to be get.
         *   @return true if the driver supports the property, else false.
         */

    bool (* SetProperty)(LTMotorPanTilt *motor, const char *name, const void *value);
        /**< sets a motor property by name.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param name the property to be set.
         *   @param args the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    bool (* OpenMotor)(LTMotorPanTilt *motor, LTMotorPanTilt_Type motorType);
        /**< opens a motor which supports the given motor type (pan/tilt/both).
         *
         *   Attempts to open and configure the motor with the given type. If
         *   the motor type is available then it will be configured
         *   as specified and the motor handle returned.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pMotorType the motor type that is being requested.
         *   @return true if the type is supported/available, else false.
         */

    bool (* IsOpen)(LTMotorPanTilt *motor);
        /**< checks if the motor is open.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @return true if the motor is open, else false.
         */

    void (* CloseMotor)(LTMotorPanTilt *motor);
        /**< closes a motor.
         *
         * After calling this function, IsOpen() should return false.
         *
         *  @param motor Pointer to LTMotorPanTilt instance.
         */

    bool (* GetMinPosition)(LTMotorPanTilt *motor, LTMotorPanTilt_Position *pMinPosition);
        /**< gets the minimum position for the motor.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pMinPosition pointer to LTPanTilt_MotorPosition that will get set with the motor minimum position
         *
         *   @return true if the minimum position was successfully retrieved, else false.
         */

    bool (* GetMaxPosition)(LTMotorPanTilt *motor, LTMotorPanTilt_Position *pMaxPosition);
        /**< gets the maximum position for the motor.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pMaxPosition pointer to LTPanTilt_MotorPosition that will get set with the motor maximum position
         *
         *   @return true if the maximum position was successfully retrieved, else false.
         */

    bool (* GetCurrentPosition)(LTMotorPanTilt *motor, LTMotorPanTilt_Position *pCurrentPosition);
        /**< gets the current position of the motor.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCurrentPosition Pointer to the LTMotorPanTilt_Position structure to fill
         *          with current position of the motor.
         *   @return true if the current position was successfully retrieved, else false.
         */

    void (* MoveToPosition)(LTMotorPanTilt *motor, LTMotorPanTilt_Position *motorPosition);
        /**< Puts a request in the queue for the Move to a specified position (in degrees).
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param motorPosition pointer to LTMotorPanTilt_Position struct containing the target position we want to move to.
         *   @return none.
         */

    bool (* SetSentryPath)(LTMotorPanTilt *motor, LTMotorPanTilt_SentryPath *sentryPath);
        /**< Sets a sentry path for the motor.
         *
         *   If a previous sentry path was set, it will be replaced by the new one.
         *   The lifetime of the sentryPath is managed by the driver and will be freed when the sentry path is no longer needed.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param sentryPath pointer to LTMotorPanTilt_SentryPath containing the sentry path to set for the motor.
         *   @return true if the sentry path was successfully set, else false.
         */

    bool (* PauseSentryPath)(LTMotorPanTilt *motor, bool pause);
        /**< Pauses/Unpauses following sentry path.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pause true to pause the sentry path, false to unpause.
         *   @return true if the sentry path was successfully paused, else false.
         */

    void (* Reset)(LTMotorPanTilt *motor, LTMotorPanTilt_Position *position);
        /**< clears out old motor data/state and restarts the motor.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param position additional position after resetting
         */

    bool (*SetSpeed)(LTMotorPanTilt *motor, s32 speed);
        /**< set the speed of the motor in unit of 100 steps/s, range 1-9.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param speed value.
         */

    bool (* CalibrationInterval)(LTMotorPanTilt *motor, LTTime period);
        /**< How often to re-calibrate the motor.
         *
         *   @param period The motor will be recalibrated every period.
		 *
		 *   @note the period passed in should have millisecond resolution.
         */

    void (* OnMotorMovementEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnMovementEventProc *pCallback, void *pClientData);
        /**< registers the callback for motor movement events.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCallback the callback.
         *   @param pClientData the client provided data to be delivered in the callback.
         */

    void (* NoMotorMovementEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnMovementEventProc *pCallback);
        /**< unregisters the callback for motor movement events.
        *
        *    @param motor Pointer to LTMotorPanTilt instance.
        *    @param pCallback the callback.
        */

    void (* OnMotorStateChangeEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnStateChangeEventProc *pCallback, void *pClientData);
         /**< registers the callback for motor state change events.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCallback the callback.
         *   @param pClientData the client provided data to be delivered in the callback.
         */

    void (* NoMotorStateChangeEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnStateChangeEventProc *pCallback);
        /**< unregisters the callback for motor state change events.
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCallback the callback.
         */

    void (* OnSentryPathStateChangeEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnSentryPathStateChangeEventProc *pCallback, void *pClientData);
        /**< Registers the callback for motor sentry events ({start | progress | completion | time}).
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCallback the callback.
         *   @param pClientData the client provided data to be delivered to the callback.
         */

    void (* NoSentryPathStateChangeEvent)(LTMotorPanTilt *motor, LTMotorPanTilt_OnSentryPathStateChangeEventProc *pCallback);
        /**< Unregisters the callback for motor sentry events ({start | progress | completion | time}).
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         *   @param pCallback the callback.
         */
    void (* StopMotor)(LTMotorPanTilt *motor);
        /**< Puts a request in the queue for the motor to a stop
         *
         *   @param motor Pointer to LTMotorPanTilt instance.
         */
    void (*IrcutOpen)(LTMotorPanTilt *ircut);
        /**< Open Ircut
         *
         */
    void (*IrcutClose)(LTMotorPanTilt *ircut);
        /**< Close Ircut
         *
         */
    void (*IrcutReset)(LTMotorPanTilt *ircut);
        /**< Reset Ircut
         *
         */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_DEVICE_MOTOR_LTDEVICEMOTORPANTILT_H
