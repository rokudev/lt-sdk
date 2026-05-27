/*******************************************************************************
 * platforms/macos/source/macos/driver/flash/MacOSDriverFlashImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include "MacOSFlashDeviceUnit.h"

/*________________________________________________________
 / MacOSDriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, MacOSDriverFlash);

/*________________________________________________________________________
 / MacOSDriverFlashImpl [macro declared] required prototype functions */
static bool MacOSDriverFlashImpl_LibInit(void) { MacOSFlashDeviceUnit_Initialize(); return true; }

static void MacOSDriverFlashImpl_LibFini(void) {  MacOSFlashDeviceUnit_Finalize(); }

static u32 MacOSDriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit MacOSDriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? MacOSFlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 */
