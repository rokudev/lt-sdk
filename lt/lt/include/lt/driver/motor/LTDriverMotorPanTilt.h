/*******************************************************************************
 * <lt/driver/identity/LTDriverMotorPanTilt.h> LTDriverMotor
 *
 * Interface for a singleton driver for Pan and Tilt motor control.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_MOTOR_LTDEVICEIDENTITY_H
#define LT_INCLUDE_LT_DRIVER_MOTOR_LTDEVICEIDENTITY_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef enum {
    kLTDriverMotorPanTiltDirection_Pan = 1,
    kLTDriverMotorPanTiltDirection_Tilt = 2
} LTDriverMotorPanTiltDirection;

typedef enum {
    kLTDriverMotorPanTiltEvent_Idle = 0x0101,
    kLTDriverMotorPanTiltEvent_Moving,
    kLTDriverMotorPanTiltEvent_Stopped,
    kLTDriverMotorPanTiltEvent_CalibrateStart,
    kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypoint,
    kLTDriverMotorPanTiltEvent_SentryPathPaused,
    kLTDriverMotorPanTiltEvent_SentryPathMoveToWaypointComplete,
    kLTDriverMotorPanTiltEvent_Error = 0x01FF
} LTDriverMotorPanTiltEvent;

typedef void (*LTDriverMotorPanTiltEventProc)(LTDriverMotorPanTiltEvent event, void *eventData, void *pClientData);

typedef_LTLIBRARY_INTERFACE(ILTDriverMotorPanTilt, 1) {
    LTDriverMotorPanTiltDirection (*GetSupportedDirections)(void);
    /**<
     * @brief Get the supported directions of the motor.
     *
     * @returns the supported directions of the motor
     */

    bool (*GetProperty)(const char *name, void *value);
    /**<
     * @brief Get a property of the motor.
     *
     * @param name the name of the property
     * @param value a pointer to the value of the property
     *
     * @returns true if the property was successfully retrieved, false otherwise
     */

    bool (*SetProperty)(const char *name, const void *value);
    /**<
     * @brief Set a property of the motor.
     *
     * @param name the name of the property
     * @param value a pointer to the value of the property
     *
     * @returns true if the property was successfully set, false otherwise
     */

    void (*Calibrate)(LTMotorPanTilt_Position * position);
    /**<
     * @brief Calibrate the motor. Asynchronous.
     *        On start, kLTDriverMotorPanTiltEvent_CalibrateStart is emitted.
     *        On completion, kLTDriverMotorPanTiltEvent_Idle is emitted.
     * @param position additional position after resetting
     */

    void (*GetPosition)(LTMotorPanTilt_Position *position);
    /**<
     * @brief Get the current position of the motor.
     *
     * @param position the current position of the motor
    */

    bool (*GetMaxPosition)(LTMotorPanTilt_Position *position);
    /**<
     * @brief Get the maximum position of the motor.
     *
     * @param position the maximum position of the motor
     *
     * @returns true if the maximum position was successfully retrieved, false otherwise
     */

    bool (*GetMinPosition)(LTMotorPanTilt_Position *position);
    /**<
     * @brief Get the minimum position of the motor.
     *
     * @param position the minimum position of the motor
     *
     * @returns true if the minimum position was successfully retrieved, false otherwise
     */

    bool (*Move)(LTMotorPanTilt_Position * position);
    /**<
     * @brief Move the motor to a position.
     *
     * @param position Target position
     *
     * @returns true if move is successfully scheduled, false otherwise
     */

    bool (*SetSentryPath)(LTMotorPanTilt_SentryPath *sentryPath);
    /**<
     * @brief Set the sentry path for the motor.
     *        If the motor is currently moving, it will stop and move to the first waypoint in the sentry path.
     *        If a previous sentry path was set, it will be replaced by this one.
     *
     * @param sentryPath the sentry path for the motor. NULL clears any sentry path the motor is following
     *        Lifetime of the sentryPath is managed by the driver and will be freed when the sentry path is
     *        replaced or the driver is destroyed.
     *
     * @returns true if the sentry path was successfully set, false otherwise
     */

    bool (*PauseSentryPath)(bool pause);
    /**<
     * @brief Pause or resume following the sentry path.
     *
     * @param pause true to pause the sentry path, false to resume
     *
     * @returns true if the sentry path was successfully paused or resumed, false otherwise
     */

    void (*Stop)(void);
    /**<
     * @brief Stop the motor immediately. Asynchronous.
     */

    void (*SetSpeed)(s32 speed);
    /**<
     * @brief Set the speed of the motor.
     *
     * @param speed the speed of the motor in steps per second
     */

    void (*OnMotorEvent)(LTDriverMotorPanTiltEventProc proc, void *clientData);
    /**<
     * @brief Register a callback for motor events.
     *
     * @param proc the callback function
     * @param clientData the client data to pass to the callback
     */

    void (*NoMotorEvent)(LTDriverMotorPanTiltEventProc proc);
    /**<
     * @brief Unregister a callback for motor events.
     *
     * @param proc the callback function
     */

    bool (*IrcutOpen)(void);
    /**
     * @brief Open ircut
     */

     bool (*IrcutClose)(void);
    /**
     * @brief Close ircut
     */

     bool (*IrcutReset)(void);
    /**
     * @brief Reset ircut
     */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_MOTOR_LTDEVICEIDENTITY_H */
