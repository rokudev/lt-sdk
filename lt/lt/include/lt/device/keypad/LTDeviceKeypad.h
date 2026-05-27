/*******************************************************************************
 * <lt/device/keyinput/LTDeviceKeypad.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * @file LTDeviceKeypad.h header for LTObject LTDeviceKeypad
 * @brief provide object that translates between key pad key values and key names
 *
 * LTDeviceKeyPad_KeyNames is an object that provides functions to get a keypad
 * key's value from its name and its name from its value.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD_H
#define LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD_H

#include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>

LT_EXTERN_C_BEGIN;

typedef void (LTDeviceKeypad_RawKeyInputEventProc)(u32 *pRawKeys, u32 nNumKeys, void *pClientData);

typedef_LTObject(LTDeviceKeypad, 1) {

    void (* OnKeyInput)(LTDeviceKeypad *keypad, LTDeviceKeypad_RawKeyInputEventProc *pEventProc, void *pClientData);
    /**< registers a key input event handler which receives sequences of raw key events */

    void (* NoKeyInput)(LTDeviceKeypad *keypad, LTDeviceKeypad_RawKeyInputEventProc *pEventProc);
    /**< unregisters a previously registered key input event handler */

    void (* Start)(LTDeviceKeypad *keypad);
    /**< starts the receipt of keys */

    void (* Stop)(LTDeviceKeypad *keypad);
    /**< stops the receipt of keys */

    bool (* IsKeyDown)(LTDeviceKeypad *keypad, LTKey key);
    /**< determines whether or not a key is currently down
     *
     *   @param keypad the keypad to ask
     *   @param key the LTKey value to ask about
     *   @return whether or not the key is currently down or false if the key is not present on the keypad
     *
     *   @note this function returns the current hardware state of a key and is unnaffected by
     *         LTDeviceKeypad software programmatic dispatch of key events from the functions
     *         %DispatchKeyDown, %DispatchKeyUp, %DispatchKeyPress, and %DispatchRawKeySequence
     */

    bool (* IsAnyKeyDown)(LTDeviceKeypad *keypad);
    /**< determines whether or not any key is down */

    LTKey (* GetStuckKey)(LTDeviceKeypad *keypad);
    /**< returns the stuck key, or kLTKey_Invalid if no key is stuck (held beyond the stuck key threshold) */

    void (* DispatchKeyDown)(LTDeviceKeypad *keypad, LTKey key);
    /**< programmatically dispatch a single key down event */

    void (* DispatchKeyUp)(LTDeviceKeypad *keypad, LTKey key);
    /**< programmatically dispatch a single key up event */

    void (* DispatchKeyPress)(LTDeviceKeypad *keypad, LTKey key);
    /**< programmatically dispatch a key up and key down event for a single key */

    void (* DispatchRawKeySequence)(LTDeviceKeypad *keypad, u32 *pRawKeys, u32 nNumKeys);
    /**< programmatically dispatch a sequence of key events */

} LTOBJECT_API;

LT_EXTERN_C_END;

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Jan-25   augustus    created from LTDeviceKeypad
 */
