/*******************************************************************************
 * MockLinuxDriverKeypad.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
/** @file MockLinuxDriverKeypad.c Implementation of mock keypad driver for Linux */

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/driver/keypad/LTDriverKeypad.h>
#include <lt/device/keypad/LTDeviceKeypad_KeyNames.h>

/*_____________________________
  MockLinuxDriverKeypad #defines */
DEFINE_LTLOG_SECTION("linux.drv.keypad");
#define DO_DLOG 0
    #if DO_DLOG
        #define DLOG LTLOG
    #else
        #define DLOG LTLOG_LOGNULL
    #endif

/*___________________________________________________________
  MockLinuxDriverKeypad LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDriverKeypad, MockLinuxDriverKeypad) {
                                                                //  12 bytes for base LTObject
                                                                //   4 bytes padding because following members are in nested struct
                                                                //  __ with 64 bit value in it (LTTime)
                                                                //  16
    LTDriverKeypad_KeyInputProc *keyInputProc;                  //   4 bytes - 20
    void                        *keyInputClientData;            //   4 bytes - 24
} LTOBJECT_API;
LT_STATIC_ASSERT_SIZE_32_64(MockLinuxDriverKeypad, 24, 32)

/*_______________________________
  LTDriverKeypad API functions */
static void MockLinuxDriverKeypad_Start(MockLinuxDriverKeypad *keypad, LTDriverKeypad_KeyInputProc *keyInputProc, void *clientData) {
    // set keyInputProc and keyInputClientData
    keypad->keyInputProc = keyInputProc;
    keypad->keyInputClientData = clientData;
}

static void MockLinuxDriverKeypad_Stop(MockLinuxDriverKeypad *keypad) {
    LT_UNUSED(keypad);
}

static bool MockLinuxDriverKeypad_IsKeyDown(MockLinuxDriverKeypad *keypad, LTKey key) {
    LT_UNUSED(keypad);
    LT_UNUSED(key);
    return false;
}

static bool MockLinuxDriverKeypad_IsAnyKeyDown(MockLinuxDriverKeypad *keypad) {
    LT_UNUSED(keypad);
    return false;
}

/*_________________________________
  MockLinuxDriverKeypad constructors */
static bool MockLinuxDriverKeypad_ConstructObject(MockLinuxDriverKeypad *keypad) {
    LT_UNUSED(keypad);
    const char *unit = "unknown";
    DLOG("construct", "unit is %s", unit);
    return true;
}

static void MockLinuxDriverKeypad_DestructObject(MockLinuxDriverKeypad *keypad) {
    LT_UNUSED(keypad);
}

/*_____________________________
  LTDriverKeypad api binding */
define_LTObjectImplPublic(LTDriverKeypad, MockLinuxDriverKeypad,
    Start,
    Stop,
    IsKeyDown,
    IsAnyKeyDown
);

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Mar-26   augustus    created
 */

