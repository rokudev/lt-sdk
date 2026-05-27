/*******************************************************************************
 * <lt/device/keyinput/LTDeviceKeyInput.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceKeyInput.h header for public interface class
 *  LTDeviceKeyInput */

#ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEKEYINPUT_H
#define LT_INCLUDE_LT_DEVICE_LTDEVICEKEYINPUT_H

#include <lt/LT.h>
#include <lt/device/keyinput/LTDeviceKeyInputDefs.h>

LT_EXTERN_C_BEGIN

typedef void (LTDeviceKeyInputCallback)(LTDeviceKeyInputValue, void * pClientData);

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceKeyInput, 1) {
    /**< Interface for handling key-press events */
    bool (* Initialize)(LTOThread * pOThread);
        /**< Initializes the device and driver
         *  
         *  The KeyInput will run along the %LTThread referred to by %pOThread, reporting key events to the registered callbacks.
         *  @note if %pOThread is NULL, the KeyInput will create its own thread along which to run.
         *
         *   @param pOThread the thread to use for notifications
         *   @return true if successful, false otherwise */
    void (*Enable)(bool bEnable);
        /**< Enables and disables the device and driver
         *   @param bEnable true to enable, false to disable */
    void (*RegisterForKeyPress)(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< registers the given callback to receive key press events
         *   @param pCallback the callback for receiving key press events
         *   @param pReleaseProc callback for reclaiming memory used for client data
         *   @param pClientData client data that will be passed to the callback */
    void (*UnregisterFromKeyPress)(LTDeviceKeyInputCallback * pCallback);
        /**< unregisters the given callback from receiving key press events
         *   @param pCallback the client's callback */
    void (*RegisterForKeyRelease)(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< registers the given callback to receive key release events
         *   @param pCallback the callback for receiving key release events
         *   @param pReleaseProc callback for reclaiming memory used for client data
         *   @param pClientData client data that will be release to the callback */
    void (*UnregisterFromKeyRelease)(LTDeviceKeyInputCallback *pCallback);
        /**< unregisters the given callback from receiving key release events
         *   @param pCallback the client's callback */
    void (*RegisterForKeyHold)(LTDeviceKeyInputCallback *pCallback, LTDeviceKeyInputValue nKey, LTTime tmHoldTime, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< registers the given callback to receive key hold events
         *   @param pCallback the callback for receiving key hold events
         *   @param nKey the key for the hold event
         *   @param tmHoldTime the time the key is held before sending the notification. Minimum time is one second
         *   @param pReleaseProc callback for reclaiming memory used for client data
         *   @param pClientData client data that will be passed to the callback */
    void (*UnregisterFromKeyHold)(LTDeviceKeyInputCallback *pCallback, LTDeviceKeyInputValue nKey);
        /**< unregisters the given callback from receiving key hold events
         *   @param pCalback the client's callback
         *   @param nKey the key to unregister from */
    void (*RegisterForKeyUnstuck)(LTDeviceKeyInputCallback *pCallback, LTThread_ClientDataReleaseProc *pReleaseProc, void *pClientData);
        /**< registers the given callback to receive key unstuck events
         *   @param pCallback the callback for receiving key release events
         *   @param pReleaseProc callback for reclaiming memory used for client data
         *   @param pClientData client data that will be passed to the callback */
    void (*UnregisterFromKeyUnstuck)(LTDeviceKeyInputCallback *pCallback);
        /**< unregisters the given callback from receiving key unstuck events
         *   @param pCallback the client's callback */
    void (*HandleKeyStuck)(void);
        /**< Informs the device that there is a key stuck. The driver will rescan the keys and send a key unstuck event if it's cleared */
    bool (*IsAnyKeyDown)(void);
        /**< Reports whethere there is any key pressed
         *   @return true if a key is pressed, false otherwise */
    LTDeviceKeyInputEvent (*GetLastKeyInputEvent)(void);
        /**< reports the last key input event
         *   @return the last key input event that was reported */
    const char *(*GetKeyNameFromKeyValue)(LTDeviceKeyInputValue key);
        /**< returns the name of the given key
         *   @param key the value of the key
         *   @return the name of the given key */
    LTDeviceKeyInputValue (*GetKeyValueFromKeyName)(const char *key);
        /**< returns the value of the given key name
         *   @param key the name of the key
         *   @return the value of the given key name */
} LTLIBRARY_INTERFACE;

/* This interface is to be used only by LTDeviceKeyInput. */
typedef_LTLIBRARY_INTERFACE(ILTDriverKeyInput, 1) {
    /**< Interface for accessing the key input driver */
    void (*Enable)(bool bEnable);
        /**< Enables and disables the driver
         *   @param bEnable true to enable, false to disable */
    void (*StartNotification)(LTThread_TaskProc *pKeyPressTaskProc,
                              LTThread_TaskProc *pAllReleasedTaskProc,
                              LTThread_TaskProc *pKeyUnstuckTaskProc,
                              LTThread_TaskProc *pNoStuckKeyTaskProc);
        /**< Starts key input notifications to the given callbacks
         *   @param pKeyPressTaskProc key-pressed callback.
         *          The pClientData parameter passed up in the LTThread_TaskProc
         *          is a pointer to a u32 that holds the key value and pressed flag.
         *          The device/user is responsible for freeing pClientData
         *   @param pAllReleasedTaskProc key released callback
         *   @param pKeyUnstuckTaskProc key unstuck callback
         *   @param pNoStuckKeyTaskProc no stuck key callback */
    void (*StopNotification)(void);
        /**< Stops all notifications */
    void (*HandleKeyStuck)(void);
        /**< Informs the driver that there is a key stuck */
    void (*ClearKeyStuck)(void);
        /**< Clears the key stuck state */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEKEYINPUT_H */
/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-19   constantine created
 *  05-Jan-20   constantine moved Doxymentation to LTDeviceKeyInput.dox
 *  24-Jan-20   constantine moved key-value-to-name lookup to LTDeviceKeyInput
 *  01-Jun-20   constantine Add API and functions to handle stuck-key condition
 *  14-Sep-21   constantine Convert KeyInput to redux LT C interface
 *  04-Nov-21   vitellius   Updated the device interface to match requirements and added documentation
 *  04-Nov-21   vitellius   Updated the device interface to match requirements and added documentation
 *  17-Oct-22   augustus    added LTThread_ClientDataReleaseProc to registration functions
 */
