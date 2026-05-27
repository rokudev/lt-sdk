/*******************************************************************************
 * <lt/device/captouch/LTDeviceCapTouch.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceCapTouch.h header for public interface class
 *  LTDeviceCapTouch */

/**
 * @defgroup ltdevice_captouch LTDeviceCapTouch
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for Capacitive Touch sensors.
 *
 * @internal
 *
 * The Device level manages an LTThread which is responsible for notifying all
 * subscribers of touch motion events (via LTEvent). Any number of
 * subscribers is possible, depending on available system resources.
 *
 * The Driver level detects touch motion events, usually via
 * interrupts, and notifies the Device level via QueueTaskProc().
 *
 * LTDeviceCapTouch is a single-unit device; it does not use the multi-unit
 * Device Unit facility.
 *
 * @endinternal
 */

#ifndef LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCH_H
#define LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCH_H

#include <lt/LTObject.h>
#include <lt/device/captouch/LTDeviceCapTouchDefs.h>

LT_EXTERN_C_BEGIN

/* _________________________
   LTDeviceCapTouch types */
typedef void (LTDeviceCapTouch_TriggerEventProc)(u32 numTriggers, void *pClientData);
    /**< EventProc type for receiving cap touch events
     *
     * @param capTouch the device the event is being received from
     * @param numTriggers the number of triggers received since the last TriggerEventProc
     * @param pClientData The client data that was passed in during registration
     */

/* _______________________
   LTDeviceCapTouch API */
typedef_LTObject(LTDeviceCapTouch, 1) {
    /**< LTDeviceCapTouch object API.
     *
     *   @note To use LTDeviceCapTouch call lt_createdeviceobject, register a notification event handler,
     *         and call SetMode(), e.g. <pre>
     *         LTDeviceCapTouch *capTouch = lt_createdeviceobject(LTDeviceCapTouch);
     *         capTouch->API->OnCapTouchRiggerEvent(capTouch, &MyCapTouchTriggerEventProc, NULL, pMyClientData);
     *         capTouch->API->SetMode(capTouch, kLTDeviceCapTouch_Mode_Normal);
     *         </pre>
     */

    void (*OnCapTouchTriggerEvent)(LTDeviceCapTouch *capTouch, LTDeviceCapTouch_TriggerEventProc *triggerEventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData);
        /**< set the event procedure for receipt of cap touch events.
         *
         * @param[in] triggerEventProc The event proc to be called for cap touch events.
         * @param[in] clientDataReleaseProc The function to be called when the client data is released (when NoCapTouchEvent is called or the captouch object is destroyed with registered event handlers).
         * @param[in] clientData The client data passed back to the event callback.
         *
         */

    void (*NoCapTouchTriggerEvent)(LTDeviceCapTouch *capTouch, LTDeviceCapTouch_TriggerEventProc *triggerEventProc);
        /**< unregister a previously registered cap touch event proc
         *
         * @param[in] triggerEventProc The event proc to be unregistered.
         */

    bool (*Initialize)(LTDeviceCapTouch *capTouch, LTDeviceCapTouch_Mode mode);
        /**< initializes the captouch device with the operating mode.
         *
         *  @param[in]  capTouch the cap touch device to set the mode on.
         *  @param[in]  mode the mode to set the capTouch device to.
         *  @return the current operating mode.
         *
         *  @note When the device is created it is created in the
         */

    void (*Enable)(LTDeviceCapTouch *capTouch, bool bEnable);

    LTDeviceCapTouch_Mode (*GetMode)(LTDeviceCapTouch *capTouch);
        /**< gets the current operating mode
         *  @return the current operating mode */

    bool (*IsCapTouchTriggerActive)(LTDeviceCapTouch *capTouch);
        /**< returns whether or not the cap touch trigger is active
          *  @return whether or not the cap touch trigger is active */

    u32 (*GetTotalTriggerCount)(LTDeviceCapTouch *capTouch);
        /**< Gets the cumulative touch trigger count (for MFG testing).
         *  @return The number of touch triggers since last reset. */

    void (*ResetTotalTriggerCount)(LTDeviceCapTouch *capTouch);
        /**< Resets the touch trigger count to zero (for MFG testing). */

    bool (*ResetTest)(LTDeviceCapTouch *capTouch);
        /**< Tests the reset line and SDA state (for MFG testing). */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_CAPTOUCH_LTDEVICECAPTOUCH_H */

/** @} */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-Mar-26   augustus    created from macrinus's device
 */
