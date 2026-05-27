/*******************************************************************************
 * <lt/system/usb/LTSystemUsbBusManager.h>   LTSystemUsbBusManager lib root interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBBUSMANAGER_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBBUSMANAGER_H

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
LT_EXTERN_C_BEGIN

typedef void (*LTSystemUsbBusManager_BusReassignedCallback)(bool acquired, void *clientData);
/**< Callback to signal USB bus reassignment.
 *
 * This will be called on the thread that requested the bus.
 * If acquired is true, then the callee is free to start using the USB bus.
 * If acquired is false, then the callee must release the USB bus before returning from this function.
 *
 * No functions of LTSystemUsbBusManager can be called from this callback due to locking.
 *
 * @param[in] acquired True if the bus was acquired, false if released.
 * @param[in] clientData Client data passed to the callback.
 */

typedef void (*LTSystemUsbBusManager_ListModesCallback)(const char *mode, void *clientData);
/**< Callback to list USB bus modes.
 *
 * @param[in] mode The name of the USB bus mode.
 * @param[in] clientData Client data passed to the callback.
 */

/**
 * @struct LTSystemUsbBusManager
 * @brief The API for USB bus management.
 *
 * This library orchestrates access to USB hardware between multiple operating modes.
 * An operating mode is a specific configuration of the USB device, such as
 * "Mailbox over CDC" or "Floodlight host controller".
 * As only one of these modes can access the USB bus at a time, they need to coordinate
 * through this library.
 *
 * Instead of directly creating a USB driver when a library wants to use it,
 * it registers a callback with this library for the particular mode that it operates.
 * When that mode is selected, it receives a callback where it can create the USB driver
 * and start using the USB bus.
 * When another mode is selected, it receives a callback to release the USB bus.
 * It must release the bus immediately, and wait for it to be potentially selected again.
 *
 * Mode selection can be done via the LTSystemUsbBusManager::ChangeMode function.
 *
 * A list of supported modes need to be provided in the product config file.
 * A default mode can also be specified, which will be selected when the library is initialized.
 * The selection logic will allocate the bus in this priority:
 * 1. The selected mode, if it has a callback registered.
 * 2. The default mode, if it is specified in the config and it has a callback registered.
 * Therefore, until the default or the selected mode is registered, the bus will not be allocated.
 */

typedef_LTObject(LTSystemUsbBusManager, 1) {

    INHERIT_LIBRARY_BASE

    bool (*OnModeChange)(const char *mode, LTSystemUsbBusManager_BusReassignedCallback cb, void *clientData);
    /**< Register interest in using the USB bus.
     *
     * There can only be one assigned callback for a given mode at a time.
     * 
     * @param[in] mode The USB bus mode to register.
     * @param[in] cb Callback to be called when the bus is acquired or released.
     * @param[in] clientData Client data to be passed to the callback.
     * @return True if the callback was registered, false otherwise.
     */

    void (*NoModeChange)(const char *mode);
    /**< Inform the manager that the bus is no longer needed.
     *
     * If the bus is currently held by the caller, OnModeChange will be called to release it.
     *
     * @param[in] mode The USB bus mode to unregister.
     */

     bool (*ChangeMode)(const char *mode);
    /**< Force the USB bus to change to a new mode.
     * 
     * This method will return before the bus is actually changed.
     *
     * @param[in] mode The USB bus mode to switch to.
     * @return True if the mode exists, false otherwise.
     */

    LTString (*GetCurrentMode)(void);
    /**< Get the current USB bus mode.
     *
     * @return The name of the current USB bus mode. The caller is responsible for freeing the returned string.
     */

     void (*ListModes)(LTSystemUsbBusManager_ListModesCallback callback, void *clientData);
    /**< List all available USB bus modes.
     *
     * @param[in] callback Callback to be called for each mode.
     * @param[in] clientData Client data to be passed to the callback.
     */
}

LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBBUSMANAGER_H */
