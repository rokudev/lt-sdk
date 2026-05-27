/*******************************************************************************
 * platforms/linux/source/linux/driver/flash/LinuxDriverFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include "LinuxFlashDeviceUnit.h"

/*________________________________________________________
 / LinuxFlashdriver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, LinuxDriverFlash);

/*________________________________________________________________________
 / LinuxFlashImpl [macro declared] required prototype functions */
static bool LinuxDriverFlashImpl_LibInit(void) { LinuxFlashDeviceUnit_Initialize(); return true; }

static void LinuxDriverFlashImpl_LibFini(void) { LinuxFlashDeviceUnit_Finalize(); }

static u32 LinuxDriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit LinuxDriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? LinuxFlashDeviceUnit_CreateHandle() : 0;
}
