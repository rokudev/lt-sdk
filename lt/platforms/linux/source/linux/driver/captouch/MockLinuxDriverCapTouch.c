/*******************************************************************************
 * MockLinuxDriverCapTouch.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/
/** @file MockLinuxDriverCapTouch.c Implementation of mock capTouch driver for Linux */

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/driver/captouch/LTDriverCapTouch.h>

/*___________________________________________________________
  MockLinuxDriverCapTouch LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDriverCapTouch, MockLinuxDriverCapTouch) {
                                                                //  12 bytes for base LTObject
                                                                //   4 bytes padding because following members are in nested struct
                                                                //  __ with 64 bit value in it (LTTime)
                                                                //  16
    LTDriverCapTouch_MotionProc       *capTouchMotionProc;      //   4 bytes - 20
    void                              *capTouchClientData;      //   4 bytes - 24
} LTOBJECT_API;
LT_STATIC_ASSERT_SIZE_32_64(MockLinuxDriverCapTouch, 24, 32)

/*_________________________________
  MockLinuxDriverCapTouch constructors */
static bool MockLinuxDriverCapTouch_ConstructObject(MockLinuxDriverCapTouch *capTouch) {
    LT_UNUSED(capTouch);
    return true;
}

static void MockLinuxDriverCapTouch_DestructObject(MockLinuxDriverCapTouch *capTouch) {
    LT_UNUSED(capTouch);
}

/*_______________________________
  LTDriverCapTouch API functions */
static bool MockLinuxDriverCapTouch_Initialize(MockLinuxDriverCapTouch *capTouch, LTDeviceCapTouch_Mode mode, LTThread hNotificationThread, LTDriverCapTouch_MotionProc *pMotionProc, void *pClientData) {
    LT_UNUSED(mode);
    LT_UNUSED(hNotificationThread);
    capTouch->capTouchMotionProc = pMotionProc;
    capTouch->capTouchClientData = pClientData;
    return true;
}

static LTDeviceCapTouch_Mode MockLinuxDriverCapTouch_GetMode(LTDriverCapTouch *capTouchDriver) {
    LT_UNUSED(capTouchDriver);
    return kLTDeviceCapTouch_Mode_Unset;
}

static void MockLinuxDriverCapTouch_Enable(LTDriverCapTouch *capTouchDriver, bool bEnable) {
    LT_UNUSED(capTouchDriver);
    LT_UNUSED(bEnable);
}

/*_____________________________
  LTDriverCapTouch api binding */
define_LTObjectImplPublic(LTDriverCapTouch, MockLinuxDriverCapTouch,
    Initialize,
    GetMode,
    Enable
);

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Mar-26   augustus    created
 */

