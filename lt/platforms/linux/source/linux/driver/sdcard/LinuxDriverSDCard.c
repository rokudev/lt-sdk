/*******************************************************************************
 * platforms/linux/source/linux/driver/sdcard/LinuxDriverSDCard.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/sdcard/LTDeviceSDCard.h>

#include "LinuxSDCardUnit.h"

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceSDCard, LinuxDriverSDCard);

static bool LinuxDriverSDCardImpl_LibInit(void) {
    return LinuxSDCardDeviceUnit_Init();
}

static void LinuxDriverSDCardImpl_LibFini(void) {
    LinuxSDCardDeviceUnit_Fini();
}

u32 LinuxDriverSDCardImpl_GetNumDeviceUnits(void) {
    return 1;
}

LTDeviceUnit LinuxDriverSDCardImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    LTHandle hSDCardDevice = LinuxSDCardDeviceUnit_CreateHandle();
    return hSDCardDevice;
}
