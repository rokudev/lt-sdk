/*******************************************************************************
 * lt/device/power/LTDevicePower.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DRIVER_POWER_LTDRIVERPOWERCLIENT_H
#define ROKU_LT_INCLUDE_LT_DRIVER_POWER_LTDRIVERPOWERCLIENT_H

#include <lt/device/power/LTDevicePower.h>

LT_EXTERN_C_BEGIN

typedef_LTObject(LTDriverPowerClient, 2) {

    /* Individual device drivers that need to provide special handling to go into and out of sleep mode
       need to provide a specialization of LTDriverPowerClient where the specialization name is a concatination
       of the name of the driver and optionally the unit, e.g. on a device with only one flash unit,
       the flash driver library would create the following specialization:
          typedef_LTObjectImpl_Public(LTDriverPowerClient, LTDriverFlash);
       On a device with more than one flash unit, e.g. a system with 2 flashes with unit names Flash0 and Flash1
       then there would be two specializations as follows:
          typedef_LTObjectImpl_Public(LTDriverPowerClient, LTDriverFlash/Flash0);
          typedef_LTObjectImpl_Public(LTDriverPowerClient, LTDriverFlash/Flash1);
       The important thing is that whatever the specialization name is, that is the string that is listed in array order
       for bringup/initialization order, and teardown order is the reverse. */

    void (* GoToSleep)(LTDriverPowerClient *client);
        /**< Instructs device driver to go to sleep, unconditionally; may be NULL for a client with nothing to do in sleep
         *
         * %GoToSleep is called by LTDevicePower in the context of the LTDevicePower thread,
         * running at the highest priority in the sysetm *WITH INTERRUPTS DISABLED*.  It's purpose is
         * is to instruct the driver to put the hardware under its control into a sleep state.  The GoToSleep
         * function must not do anything to invoke the scheduler like try to lock a mutex, and, since interrupts
         * are disabled, if it does, the system will likely hang or have other unexpected results.   Pretend you
         * are running in ISR context but don't use LTLOG or LTLOG_STOMP.  THIS MEANS YOU!
         *
         * @param  client the power driver client
         *
         * @note This function is called with interrupts disabled.  Act accordingly.
         *       The %GoToSleep function implementation should only operate
         *       the hardware to put it into a sleep mode.  The order of GoToSleep invocations is
         *       listed explicitly in LTDeviceConfig.json for each platform variant in reverse order.
         *       (The order in LTDeviceConfig.json is given in initialization dependency (Wakeup) order
         *       and LTDevicePower calls GoToSleep functions in that reverse order).
         *
         * @note If you want to invoke the scheduler as a result of a going to sleep notification, use
         *       LTCore->OnSleepAction.   Don't do it here.
         *
         * @note each GoToSleep function should not use more than 128 bytes of stack
         *
         */

    void (* WakeFromSleep)(LTDriverPowerClient *client);
        /**< Instructs device driver to WakeFromSleep, unconditionally; may be NULL when %GoToSleep is
         *
         * %WakeFromSleep is called by LTDevicePower prior to the resumption of LT OS Facilities.
         * The same caveats regarding execution context and use of the scheduler apply as advised
         * in the documentation for %GoToSleep.  Ignore at your own peril.
         *
         * @param  client the power driver client
         *
         * @see GoToSleep
         *
         * @note each WakeFromSleep function should not use more than 128 bytes of stack
         * @note At this point the kernel time returned by LT_GetCore()->GetKernelTime() should not be
         *       relied upon except to determine durations between invocations called exclusively within this function.
         *
         */

    bool (* IsReadyToSleep)(LTDriverPowerClient *client, LTTime *pWakeupTime);
        /**< Optional (may be NULL); returns false to skip hardware sleep for this attempt
         *
         * %IsReadyToSleep is called after every %GoToSleep, in the same reverse order as %GoToSleep (the first client
         * listed is asked last), immediately before the power driver enters sleep, under the same constraints as
         * %GoToSleep.  A client may lower *pWakeupTime to wake up earlier, e.g. when %GoToSleep put hardware to sleep
         * that wakes up on its own deadline; clients asked after it must not lower it further if it has committed the
         * deadline to hardware.  If any client returns false, the remaining
         * clients are not asked and the power driver is not invoked, but every %WakeFromSleep is still called so that
         * clients can restore their state, and the wakeup reason is set to kLTDevicePower_WakeupReason_SleepSkipped.
         *
         * @param  client       the power driver client
         * @param  pWakeupTime  the absolute kernel time to wake up at, never zero; may only be lowered
         * @return true if the hardware under the client's control can enter sleep now
         */

    void (* EnterShipmode)(LTDriverPowerClient *client);
        /**< Optional (may be NULL); puts the hardware under the client's control into ship mode, with no way back
         *
         * %EnterShipmode is called by LTDevicePower->EnterShipmode, in the same reverse order as %GoToSleep, before
         * the power driver's own EnterShipmode.  Unlike %GoToSleep it runs on the power thread with interrupts
         * enabled and may block, e.g. to program a charger over I2C.  Finish the work before returning: work
         * queued to another thread may never run.  A client may end ship mode itself (a charger cutting the
         * power); it is then listed first, so it runs last.  There is no matching exit: if ship mode returns,
         * the caller of LTDevicePower->EnterShipmode recovers.
         *
         * @param  client the power driver client
         */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif // ROKU_LT_INCLUDE_LT_DRIVER_POWER_LTDRIVERPOWERCLIENT_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Mar-25   augustus    created
 */
