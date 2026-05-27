/******************************************************************************
 *
 * LTDeviceSlider: Generic Slider Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library providing access to sliders
 *
 *****************************************************************************/

/**
 * @defgroup ltdevice_slider LTDeviceSlider
 * @ingroup  ltdevice
 * @{
 *
 * @brief Keeps track of a one-dimensional variable whose value changes with the up and down
 * movements, and short and long presses on the slider keys.
 *
 * A client of the device specifies the range, the initial value and other parameters that control
 * how the slider events should translate to changes in the tracked variable.
 * The library clients can subscribe to the periodic or event-driven updates to receive the value.
 */

#ifndef LTDEVICESLIDER_H
#define LTDEVICESLIDER_H

#include <lt/LTTypes.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/**
 * @brief Callback type for reporting value changes to the clients.
 *
 * @param[in] hDevice     Device Unit handle associated with the slider reporting the value
 * @param[in] nValue      Current value of the tracked variable
 * @param[in] pClientData Any extra data supplied to the client's callback
 */
typedef void (LTDeviceSliderCallback)(LTDeviceUnit hDevice, s32 nValue, void * pClientData);

/******************************************************************************
 ** Slider Device API
 *****************************************************************************/

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSlider, 1) LTLIBRARY_EMPTY_INTERFACE
    /**
     * @struct LTDeviceSlider
     * @brief Main interface for creating DeviceSlider units.
     */

typedef_LTLIBRARY_INTERFACE(ILTSliderDeviceUnit, 1) {
    /**
     * @struct ILTDeviceSlider
     * @brief Interface for controlling slider units.
     */

    bool (* Initialize)(LTDeviceUnit hDevice, s32 nMinValue, s32 nMaxValue,
                        s32 nInitialValue, u32 nMaxStep);
    /**< Set the minimum, the maximum and the initial value of the tracked
     *   variable
     *   @param[in] hDevice    Device Unit handle
     *   @param[in] nMinValue  lower bound for the tracked variable, must be lower
     *                         than @a nMaxValue
     *   @param[in] nMaxValue  @brief upper bound for the tracked variable, must be higher
     *                         than @a nMinValue @n
     *                         The range for @a nMinValue and @a nMaxValue is
     *                         <tt>[LT_S32_MIN +1 , LT_S32_MAX]</tt>. LT_S32_MIN is reserved as
     *                         an error code returned by the functions that normally
     *                         return the value of the tracked variable.
     *   @param[in] nInitValue must be between @a nMinValue and @a nMaxValue
     *   @param[in] nMaxStep   maximum change allowed in one slider movement
     *
     *   The parameter @a nMaxStep limits how much the value can change at once.
     *   If the argument passed in @a nMaxStep is 0, it is treated as if there
     *   are no limits and the value can change from @a nMinValue to @a nMaxValue
     *   or the other way around in one go. The limit for @a nMaxStep is
     *   (@a nMaxValue - @a nMinValue).
     *
     *   @return true if successful, false if the arguments are not valid */

    bool (* RegisterForUpdates)(LTDeviceUnit hDevice, LTTime tmUpdatePeriod,
                                LTDeviceSliderCallback * pCallback, void * pClientData);
    /**< Request a periodic or an event-driven callback
     *   @param[in] hDevice        Device Unit handle
     *   @param[in] tmUpdatePeriod update interval @n
     *                             If the value passed in @a tmUpdatePeriod is @p LTTime(0),
     *                             @a pCallback is invoked after each slider event. Otherwise,
     *                             @a pCallback is invoked at intervals of length @a tmUpdatePeriod.
     *   @param[in] pCallback      callback function
     *   @param[in] pClientData    client data pointer, to be passed back to the callback function
     *
     *   This function will immediately enable the slider unit associated with @a hDevice to start
     *   changing the the tracked variable, and the changes will be reported through @ pCallback.
     *   Only one callback function can be registered at a time, and if a callback is already
     *   registered for @a hDevice, that callback will be replaced with @a pCallback.
     *
     *   @note The callback function is always invoked in the thread from which this function was
     *         called.
     *
     *   @return                   false if invalid handle passed, true otherwise */

    void (* UnregisterFromUpdates)(LTDeviceUnit hDevice, LTDeviceSliderCallback * pCallback);
    /**< Remove the callback function
     *   @param[in] hDevice   Device Unit handle
     *   @param[in] pCallback callback function to unregister
     *
     *   This function will immediately freeze the tracked variable at its current value and any
     *   further slider events will be ignored.*/

    bool (* SetValue)(LTDeviceUnit hDevice, s32 nValue);
    /**< Set the tracked variable to a new value
     *   @param[in] hDevice Device Unit handle
     *   @param[in] nValue  new value
     *   @return            false if new value is not between specified bounds, true otherwise */

    s32 (* GetValue)(LTDeviceUnit hDevice);
    /**< Retrieve the current value of the tracked variable
     *   @param[in] hDevice  Device Unit handle
     *   @return             current value, or @p LT_S32_MIN if @a hDevice is an invalid handle */

} LTLIBRARY_INTERFACE;

/** @} */

/******************************************************************************
 * Private Device-Driver API, must not be used by the device library clients
 */

typedef enum {
    kLTSlider_EventTouch,
    kLTSlider_EventPress,
    kLTSlider_EventSwipe
} LTSliderEvent;

typedef void (LTDriverSlider_ISRDispatchProc)(void * pData);

typedef_LTLIBRARY_INTERFACE(ILTDriverSlider, 1) {
    u32  (* ReadValue)(LTDeviceUnit hSlider);
        /**< Read the current value of the key status register
         *   @param hSlider     hardware unit handle
         */
    u32  (* GetKeyCount)(LTDeviceUnit hSlider);
        /**< Report the number of keys on a slider unit or 0 if error
         *   @param hSlider     hardware unit handle
         */
    void (* SetISRDispatchProc)(LTDeviceUnit hSlider,
                                LTDriverSlider_ISRDispatchProc * pCallback,
                                void * pClientData);
        /**< Callback to report an interrupt on a slider unit to the device
         *   The callback runs on an internal device thread.
         *   @param hSlider     hardware unit handle
         *   @param pCallback   interrupt callback, if NULL it removes a callback
         *   @param pClientData client data that identifies the hardware unit
         */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_SLIDER_LTDEVICESLIDER_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-22   commodus    created
 */
