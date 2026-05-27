/*******************************************************************************
 * <lt/device/keypad/LTDeviceKeypad_KeyNames.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * @file LTDeviceKeypad_KeyNames.h header for LTObject LTDeviceKeypad_KeyNames
 * @brief provide object that translates between key pad key values and key names
 *
 * LTDeviceKeypad_KeyNames is an object that provides functions to get a keypad key's
 * value from its name and its name from its value.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYNAMES_H
#define LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYNAMES_H

#include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>

LT_EXTERN_C_BEGIN;

typedef_LTObject(LTDeviceKeypad_KeyNames, 1) {
    LTKey (* GetKeyValueFromName)(const char *keyName);
        /**< returns the LTDeviceKeypad_Key enum value for the key with specified name
         *
         * @param keyName the name of the button key to retrieve the id for
         * @return the LTKey enum value for the button with specified name or kLTKey_Invalid if no such button exists
         *
         */
    const char * (* GetKeyNameFromValue)(LTKey key);
        /**< returns the name for the specified LTKey button key enum value
         *
         * @param key the LTKey to retrieve the name for in the range of [kLTKey_FirstButton .. kLTKey_LastButton]
         * @return the string name of the key or NULL if no such key exists
         * @note the string returned by %GetKeyNameFromValue is only valid for the lifetime of the LTDeviceKeypad_KeyNames object
         */
} LTOBJECT_API;

LT_EXTERN_C_END;

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYNAMES_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Jan-25   augustus    created
 */
