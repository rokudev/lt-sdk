/*******************************************************************************
 * <lt/driver/keypad/LTDriverKeypad.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_KEYPAD_LTDRIVERKEYPAD_H
#define LT_INCLUDE_LT_DRIVER_KEYPAD_LTDRIVERKEYPAD_H

#include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>

LT_EXTERN_C_BEGIN;

typedef void (LTDriverKeypad_KeyInputProc)(u32 *pRawKeys, u32 nNumKeys, void *pClientData) LT_ISR_SAFE;

typedef_LTObject(LTDriverKeypad, 1) {
    void (* Start)(LTDriverKeypad *keypad, LTDriverKeypad_KeyInputProc *keyInputProc, void *clientData);
    void (* Stop)(LTDriverKeypad *keypad);
    bool (* IsKeyDown)(LTDriverKeypad *keypad, LTKey key);
    bool (* IsAnyKeyDown)(LTDriverKeypad *keypad);
    LTKey (* GetStuckKey)(LTDriverKeypad *keypad);  /**< returns the stuck key, or kLTKey_Invalid if no key is stuck */
} LTOBJECT_API;

LT_EXTERN_C_END;

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_KEYPAD_LTDRIVERKEYPAD_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Jan-25   augustus    created
 */
