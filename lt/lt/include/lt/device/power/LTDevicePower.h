/*******************************************************************************
 * lt/device/power/LTDevicePower.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_POWER_LTDEVICEPOWER_H
#define ROKU_LT_INCLUDE_LT_DEVICE_POWER_LTDEVICEPOWER_H

#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

typedef_LTENUM_SIZED(LTDevicePower_WakeupReason, u32) {
    kLTDevicePower_WakeupReason_Unknown = 0,
    kLTDevicePower_WakeupReason_SoftwareWakeupTimeout,
    kLTDevicePower_WakeupReason_MotionEvent,
    kLTDevicePower_WakeupReason_ButtonPress,
    kLTDevicePower_WakeupReason_WiFiPacket,
    kLTDevicePower_WakeupReason_ChargeEvent,
};

typedef_LTObject(LTDevicePower, 1) {

    void (* EnableSleepMode)(LTTime idleDelay, LTTime minimumSleepDuration);
        /**< enables low power sleep mode
         *
         * call %EnableSleepMode to enable the system to enter low power sleep mode
         * each time all threads in the system are idle for the specified idleDelay
         *
         * @param idleDelay the time for all threads to be idle before the system enters low power sleep mode
         *        or LTTime_Zero() to use the system default idleDelay specified in the LTDevicePower section of LTDeviceConfig.json
         * @param minimumSleepDuration the minimum duration that must exist before a software wakeup would otherwise occur
         *        in order for sleep mode to be entered or LTTime_Zero() to use the system default minimum sleep duration
         *        specified in the LTDevicePower section of LTDeviceConfig.json
         *
         * @note low power sleep mode is disabled by default and will not actuate until EnableSleepMode() is called
         *
         * @note If EnableSleepMode is called when sleep mode is already enabled, then sleep mode will remain enabled
         *       and be adjusted to use the new idleDelay and minimumSleepDuration parameters passed in
         *
         * @see DisableSleepMode, IsSleepModeEnabled
         */

    void (* DisableSleepMode)(void);
        /**< disables low power sleep mode
         *
         * call DisableSleepMode to disable low power sleep mode
         *
         * @note LTDevicePower operates LTCore's low power sleep mode and this function globally disables that operation.
         *       This function should not be used for the prevention of sleep mode entry while sleep mode is enabled.  To prevent
         *       the system from entering sleep mode during normal sleep mode enablement, use the functions LTCore->DisallowSleepMode() and LTCore->ReallowSleepMode.
         *
         */

    bool (* IsSleepModeEnabled)(void);
        /**< reports whether sleep mode is enabled or not
         *
         * call %IsSleepModeEnabled to determine whether or not sleep mode is enabled
         *
         * @return whether or not sleep mode is enabled
         *
         */

    LTDevicePower_WakeupReason (* GetLastWakeupReason)(void);
        /**< @brief gets last wake up reason
         *
         * @return the last wakeup reason
         */

    const char * (* WakeupReasonToString)(LTDevicePower_WakeupReason wakeupReason);
        /**< returns the string name of LTDevicePower_WakeupReason enum values
         *
         *  WakeupReasonToString() is provided for the purpose of logging and printing
         *  LTDevicePower_WakeupReason enum values as human-readable strings.  These strings are
         *  substrings of the enum value labels and are provided for debugging support.
         *  The names returned are the unique part of the enum label, e.g.
         *  WakeupReasonToString(kLTDevicePower_WakeupReason_MotionEvent) returns "MotionEvent".
         *
         *  @param wakeupReason - the wakeupReason to get the string name of
         *  @return the string name of the wakeupReason enum value
         */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif // ROKU_LT_INCLUDE_LT_DEVICE_POWER_LTDEVICEPOWER_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Mar-25   augustus    moved LTDriverPower into lt/include/lt/driver/power/LTDriverPower.h
 */
