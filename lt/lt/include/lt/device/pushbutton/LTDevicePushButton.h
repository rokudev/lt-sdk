/*******************************************************************************
 * <lt/device/button/LTDevicePushButton.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_pushbutton LTDevicePushButton
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for Pushbuttons.
 *
 * @internal
 *
 * The Device level manages a LTThread which is responsible for notifying all
 * subscribers of button press and release events (via LTEvent). Any number of
 * subscribers is possible, depending on available system resources.
 *
 * The Driver level detects button press and release events, usually via
 * interrupts, and notifies the Device level via QueueTaskProc().
 *
 * LTDevicePushButton does not use the Device Unit facility; the Driver interface
 * is accessed only by LTDevicePushButton.
 *
 * @endinternal
 */

#ifndef LT_INCLUDE_LT_DEVICE_PUSHBUTTON_LTDEVICEPUSHBUTTON_H
#define LT_INCLUDE_LT_DEVICE_PUSHBUTTON_LTDEVICEPUSHBUTTON_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef void (LTDevicePushButtonCallback)(u32 nIndex, void *pClientData);
    /**< Callback type for Push Button presses and releases.
     *   @param[in] nIndex The index of the button pressed or released.
     *   @param[in] pClientData Pointer to client data provisioned during registration */

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePushButton, 1);

/**
 *  LT Device interface for physical push buttons
 */
struct LTDevicePushButton {
    INHERIT_DEVICE_LIBRARY_BASE

    bool (*GetPushButtonNameFromIndex)(u32 nIndex, char *pPushButtonNameToSet, LT_SIZE nStringSizeBytes);
        /**< Obtain the name of a Push Button from the given index.
         *   @param[in]  nIndex The index of the Push Button for which to obtain the name.
         *   @param[out] pPushButtonNameToSet The string to which to write the name.
         *   @param[in]  nStringSizeBytes The number of bytes (including terminator) allowed to write to pPushButtonNameToSet.
         *   @return     true and set *pPushButtonNameToSet with at most nStringSizeBytes if nIndex is valid,
         *               false otherwise. */

    bool (*GetPushButtonIndexFromName)(char const *pPushButtonName, u32 *pIndexToSet);
        /**< Obtain the index of a named Push Button.
         *   @param[in]  pPushButtonName The name of the LED Push Button for which to retrieve the unit number.
         *   @param[out] pIndexToSet Pointer to u32 to set with index for named Push Button.
         *   @return     true and set *pIndexToSet if an LED Group by the given name exists, false otherwise. */

    /**********************************************************************************************************************
      ********************************************************************************************************************
       * Push Button register/unregister interface                                                                     */

    void (*RegisterForButtonPress)(u32 nIndex, LTDevicePushButtonCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< Request a callback upon press of a Push button.
         *   @param[in] nIndex The index of the Push Button to which to subscribe.
         *   @param[in] pCallback Address of function to call on Push Button press.
         *   @param[in] pReleaseProc callback for reclaiming memory used for client data
         *   @param[in] pClientData Client data pointer, to be passed back to callback function upon press. */

    void (*RegisterForButtonRelease)(u32 nIndex, LTDevicePushButtonCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< Request a callback upon release of a Push button.
         *   @param[in] nIndex The index of the Push Button to which to subscribe.
         *   @param[in] pCallback Address of function to call on Push Button release.
         *   @param[in] pReleaseProc callback for reclaiming memory used for client data
         *   @param[in] pClientData Client data pointer, to be passed back to callback function upon release. */

    void (*UnregisterForButtonPress)(u32 nIndex, LTDevicePushButtonCallback *pCallback);
        /**< Withdraw request for callbacks for a Push Button press.
         *   @param[in] nIndex The index of the Push Button to which to unsubscribe.
         *   @param[in] pCallback The address of the callback function being detached. */

    void (*UnregisterForButtonRelease)(u32 nIndex, LTDevicePushButtonCallback *pCallback);
        /**< Withdraw request for callbacks for a Push Button release.
         *   @param[in] nIndex The index of the Push Button to which to unsubscribe.
         *   @param[in] pCallback The address of the callback function being detached. */

    bool (*IsButtonPressed)(u32 nIndex);
        /**< Obtain the current, debounced state of a PushButton.
         *   @param[in] nIndex The index of the Push Button to examine.
         *   @return    true if the current button state is pressed, false if released.
         */
};

#ifndef DOXY_SKIP // [

/* LTDevicePushButton does not use the Device Unit facility; the Driver interface is accessed only by LTDevicePushButton. */

typedef_LTLIBRARY_INTERFACE(ILTDriverPushButton, 1) {
    bool (*GetPushButtonNameFromIndex)(u32 nIndex, char *pPushButtonNameToSet, LT_SIZE nStringSizeBytes);
        /**< Obtain the name of a Push Button from the given index.
         *   @param[in]  nIndex The index of the Push Button for which to obtain the name.
         *   @param[out] pPushButtonNameToSet Pointer to string to which to write the name.
         *   @param[in]  nStringSizeBytes How many bytes (including terminator) allowed to write to %pPushButtonNameToSet.
         *   @return     true and set *pPushButtonNameToSet with at most %nStringSizeBytes if %nIndex is valid,
         *               false otherwise. */

    bool (*GetPushButtonIndexFromName)(char const *pPushButtonName, u32 *pIndexToSet);
        /**< Obtain the index of a named Push Button.
         *   @param[in]  pPushButtonName The name of the LED Push Button for which to retrieve the unit number.
         *   @param[out] pIndexToSet Pointer to u32 to set with index for named Push Button.
         *   @return     true and set *pIndexToSet if an LED Group by the given name exists, false otherwise. */

    void (*Connect)(LTThread hThread, LTThread_TaskProc *pPressDispatchProc, LTThread_TaskProc *pReleaseDispatchProc, u32 nDeviceUnitIndexBase);
        /**< Pass the Driver the LTThread handle and procs for press and release events.
         *   The press or release function is called with the index of the button pressed or released cast
         *   to the client-data pointer.
         *   @param[in] hThread The thread on which to queue procs to handle presses and releases.
         *   @param[in] pPressDispatchProc The function to call to notify of presses.
         *   @param[in] pReleaseDispatchProc The function to call to notify of releases.
         *   @param[in] nDeviceUnitIndexBase The first Device Unit index (from the perspective of the Device Library)
         *                                   provided by this Driver. */

    void (*Disconnect)(void);
        /**< Instruct the Driver to stop calling the press and release dispatch functions (see Connect above). */

    bool (*IsButtonPressed)(u32 nIndex);
        /**< Obtain the current, debounced state of a PushButton.
         *   @param[in] nIndex The index of the Push Button to examine.
         *   @return    true if the current button state is pressed, false if released.
         */
} LTLIBRARY_INTERFACE;
#endif  // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_PUSHBUTTON_LTDEVICEPUSHBUTTON_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-21   constantine created
 */
