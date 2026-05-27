/*******************************************************************************
 * <lt/device/rtc/LTDeviceRTC.h>
 *
 * Device interface for the System Real-time Clock.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/**
 * @defgroup LTDeviceRTC LTDeviceRTC
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for accessing the System Real-time Clock.
 *
 * This Library provides access to a platform's Real-time Clock, which is
 * represented, and operated on, an LTDriverRTC object, implemented
 * by the underlying Driver Library.
 */

#ifndef LT_INCLUDE_LT_DEVICE_RTC_LTDEVICERTC_H
#define LT_INCLUDE_LT_DEVICE_RTC_LTDEVICERTC_H

#include <lt/LTTypes.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*______________________________________________________________________________
   Real-time Clock Object Interface
        In order to access the System Real-time Clock, a client
        1. creates a LTDeviceRTC object,
        2. calls SetTimeUTC() to set the time,
        3. calls GetTimeUTC() to obtain the time.

        Por Ejemplo:

            LTDeviceRTC *rtc = lt_createobject(LTDeviceRTC);
            LTTime timeUTC = rtc->API->GetTimeUTC();
            (Adjust the time)
            rtc->API->SetTimeUTC(timeUTC);

        When finished, the client may dispose of the object with:

            lt_destroyobject(rtc);
*/

typedef void (LTDeviceRTC_AlarmInterruptCallback)(void *clientData) LT_ISR_SAFE;
    /**<
     * @brief callback function for the alarm interrupt
     *
     * This callback is called by the Driver if an alarm time has been set, the alarm is enabled,
     * and the alarm time has been reached.
     *
     * @param clientData client-supplied data pointer passed to the callback when the interrupt occurs
     */

typedef_LTObject(LTDeviceRTC, 1) {

    bool (*SetTimeUTC)(LTDeviceRTC *rtc, LTTime timeUTC) LT_ISR_SAFE;
        /**<
         * @brief sets the real-time clock
         *
         * @param rtc pointer to the LTDeviceRTC object
         * @param timeUTC time to which to set the real-time clock
         * @return true upon success, false otherwise
         *
         * @see GetTimeUTC
         */

    LTTime (*GetTimeUTC)(LTDeviceRTC *rtc) LT_ISR_SAFE;
        /**<
         * @brief obtains the time from the real-time clock.
         *
         * @param rtc pointer to the channel object
         * @return the current UTC time according to the real-time clock
         *
         * @see SetTimeUTC
         */

    void (*EnableAlarmInterrupt)(LTDeviceRTC *rtc, LTTime alarmTimeUTC, LTDeviceRTC_AlarmInterruptCallback *callback, void *clientData);
        /**<
         * @brief enables the alarm interrupt, sets the alarm time, and supplies a callback function for servicing the interrupt
         *
         * Upon reaching the alarm time, the callback function is called with the clientData pointer, unless the alarm is disabled
         * before the alarm time is reached.
         *
         * @param rtc pointer to the LTDeviceRTC object
         * @param alarmTimeUTC time at which the alarm interrupt should occur
         * @param callback pointer to the callback function
         * @param clientData pointer to client-supplied data passed to the callback
         *
         * @see DisableAlarmInterrupt, LTDeviceRTC_AlarmInterruptCallback
         */

    void (*DisableAlarmInterrupt)(LTDeviceRTC *rtc);
        /**<
         * @brief disables the alarm interrupt
         *
         * @param rtc pointer to the LTDeviceRTC object
         *
         * @see EnableAlarmInterrupt
         */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_RTC_LTDEVICERTC_H */

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Nov-24   constantine Created
 */
