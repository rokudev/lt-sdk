/*******************************************************************************
 * lt/source/lt/device/keypad/LTDeviceKeypad_KeyNames.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/device/keypad/LTDeviceKeypad_KeyNames.h>
#undef  LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_H
#define LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_GENNAMETABLE
#include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>

#include <lt/core/LTCore.h>

/*___________________________________________________
  LTDeviceKeypad_KeyNamesImpl private data members */
typedef_LTObjectImpl(LTDeviceKeypad_KeyNames, LTDeviceKeypad_KeyNamesImpl) {
} LTOBJECT_API;

/*___________________________________________
  LTDeviceKeypad_KeyNamesImpl constructors */
static bool LTDeviceKeypad_KeyNamesImpl_ConstructObject(LTDeviceKeypad_KeyNamesImpl *keyNames) {
    LT_UNUSED(keyNames);
    return true;
}

static void LTDeviceKeypad_KeyNamesImpl_DestructObject(LTDeviceKeypad_KeyNamesImpl *keyNames) {
    LT_UNUSED(keyNames);
}

/*________________________________________
  LTDeviceKeypad_KeyNames API functions */
static LTKey LTDeviceKeypad_KeyNamesImpl_GetKeyValueFromName(const char *keyName) {
    LTKey key = kLTKey_Invalid;

    if (keyName && *keyName) {
        u32 nKeys = sizeof(s_LTKeyButtonNames) / sizeof(s_LTKeyButtonNames[0]);
        for (u32 i = 0; i < nKeys; i++) {
            if (0 == lt_strcmp(keyName, s_LTKeyButtonNames[i])) {
                key = kLTKey_FirstButton + i;
                break;
            }
        }
    }

    return key;
}

static const char * LTDeviceKeypad_KeyNamesImpl_GetKeyNameFromValue(LTKey key) {
    return LTKey_IsButton(key) ? s_LTKeyButtonNames[LTKey_KeyVal(key) - kLTKey_FirstButton] : "Invalid";
}

/*______________________________________
  LTDeviceKeypad_KeyNames api binding */
define_LTObjectImplPublic(LTDeviceKeypad_KeyNames, LTDeviceKeypad_KeyNamesImpl,
    GetKeyValueFromName,
    GetKeyNameFromValue
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jan-25   augustus    created
 */
