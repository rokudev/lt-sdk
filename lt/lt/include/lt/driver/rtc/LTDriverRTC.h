/*******************************************************************************
 * <lt/driver/rtc/LTDriverRTC.h>
 *
 * Driver interface for the System Real-time Clock.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/**
 * @defgroup LTDriverRTC LTDriverRTC
 * @ingroup ltdriver
 * @{
 *
 * @brief LT Driver Library for accessing the System Real-time Clock.
 *
 * LTObjects of type LTDriverRTC are intended for use by LTDeviceRTC ONLY.
 * Do not access the real-time clock directly through the Driver interface.
 *
 */

#ifndef LT_INCLUDE_LT_DRIVER_RTC_LTDRIVERRTC_H
#define LT_INCLUDE_LT_DRIVER_RTC_LTDRIVERRTC_H

#include <lt/LTTypes.h>
#include <lt/core/LTTime.h>
#include <lt/device/rtc/LTDeviceRTC.h>      /* for alarm callback type */

LT_EXTERN_C_BEGIN

/*___________________________________________________________________________________________________
   Driver Interface

   LTObjects of type LTDriverRTC are intended for use by LTDeviceRTC ONLY.
   Do not access the real-time clock directly through the Driver interface.

   IMPLEMENTING DRIVER-LEVEL SUPPORT FOR LTDeviceRTC
   ===========================================================

     LTDriverRTC is designed to make porting to a new platform as easy as possible.
     The Driver-level LT Library interface and specifications are therefore as simple as possible.
     The Driver LT Library (<platform>DriverRealTimeClock) accompanying LTDeviceRTC for a given
     platform must implement the LTObject interface, which is used solely by LTDeviceRTC and is not
     accessed directly by any client.  LTDeviceRTC is the only Library that creates LTDriverRTC
     objects.

     Through the LTDriverRTC interface, LTDeviceRTC interacts directly with a specific device
     instance (currently, the Device and Driver support only one instance).  LTDeviceRTC creates
     (through a call to LTCore's CreateObject()) an LTObject defined by the specialization of
     LTDriverRTC provided by the Driver, then calls the functions
     in the LTDriverRTC interface to operate the platform's underlying Real-time Clock
     hardware.  The client makes calls to the interface of a complementary LTObject provided by
     LTDeviceRTC to conduct these operations in a platform-independent way, and therefore
     never interacts directly with the Driver.

     The interface provides two functions which, respectively:
     - set the current UTC time held by the Real-time Clock hardware
     - get the current time from the Real-time Clock hardware

     The Driver implementation must also provide appropriate initialization of the hardware, and
     deinitialization of the hardware and cleanup upon Library finalization. */

#ifndef DOXY_SKIP // [
typedef_LTObject(LTDriverRTC, 1) {

    bool (*SetTimeUTC)(LTDriverRTC *rtc, LTTime timeUTC) LT_ISR_SAFE;
        /**<
         * @brief sets the real-time clock
         *
         * @param rtc pointer to the LTDriverRTC object
         * @param timeUTC time to which to set the real-time clock
         * @return true upon success, false otherwise
         */

    LTTime (*GetTimeUTC)(LTDriverRTC *rtc) LT_ISR_SAFE;
        /**<
         * @brief obtains the time from the real-time clock.
         *
         * @param rtc pointer to the channel object
         * @return the current UTC time according to the real-time clock
         */


    void (*EnableAlarmInterrupt)(LTDriverRTC *rtc, LTTime alarmTimeUTC, LTDeviceRTC_AlarmInterruptCallback *callback, void *clientData);
        /**<
         * @brief enables the alarm interrupt and supplies a callback function for servicing the interrupt
         *
         * @param rtc pointer to the LTDriverRTC object
         * @param alarmTimeUTC time at which the alarm interrupt should occur
         * @param callback pointer to the callback function
         * @param clientData pointer to client-supplied data passed to the callback
         */

    void (*DisableAlarmInterrupt)(LTDriverRTC *rtc);
        /**<
         * @brief disables the alarm interrupt
         *
         * @param rtc pointer to the LTDriverRTC object
         */
} LTOBJECT_API;
#endif  // DOXY_SKIP ]

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_RTC_LTDRIVERRTC_H */

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  11-Dec-24   constantine Created
 */
