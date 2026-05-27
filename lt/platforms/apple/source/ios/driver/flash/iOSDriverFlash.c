/*******************************************************************************
 * platforms/apple/source/iOS/driver/flash/iOSDriverFlashImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include "iOSFlashDeviceUnit.h"

/*________________________________________________________
 / iOSDriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, iOSDriverFlash);

/*________________________________________________________________________
 / iOSDriverFlashImpl [macro declared] required prototype functions */
static bool iOSDriverFlashImpl_LibInit(void) { iOSFlashDeviceUnit_Initialize(); return true; }

static void iOSDriverFlashImpl_LibFini(void) {}

static u32 iOSDriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit iOSDriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? iOSFlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 */
