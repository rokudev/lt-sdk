/*******************************************************************************
 * lt/driver/power/LTDriverPower.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DRIVER_POWER_LTDERIVERPOWER_H
#define ROKU_LT_INCLUDE_LT_DRIVER_POWER_LTDERIVERPOWER_H

#include <lt/device/power/LTDevicePower.h>

LT_EXTERN_C_BEGIN

typedef_LTObject(LTDriverPower, 1) {

    LTTime (*EnterSleep)(LTTime softwareRequiredWakeupTime, LTDevicePower_WakeupReason *wakeupReasonToSet);
    /**< Puts the device into low power sleep mode
      *
      * EnterSleep puts the device in low power sleep mode, waking up at the softwareRequiredWakeupTime or
      * or any time sooner due to non-software wakeup sources, setting the wakeupReason and returning the duration
      * of time spent in sleep.
      *
      * @param softwareRequiredWakeupTime the absolute kernel time that the software requires the system awaken from
      *        sleep mode or LTTime_Zero() if there is no software required wakeup time.
      * @param wakeupReasonToSet a pointer to the LTDevicePower_WakeupReason variable to contain the wakeup reason when
      *        the function returns
      * @return the actual time duration the system spent in low power sleep
      *
      */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif // ROKU_LT_INCLUDE_LT_DEVICE_POWER_LTDEVICEPOWER_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Mar-25   augustus    created
 */
