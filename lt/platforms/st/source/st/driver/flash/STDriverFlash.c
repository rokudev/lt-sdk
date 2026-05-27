/*******************************************************************************
 * platforms/st/source/st/driver/flash/STDriverFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>

#include "STFlashDeviceUnit.h"

/*________________________________________________________
 / STDriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, STDriverFlash);


/*________________________________________________________________________
 / STDriverFlash [macro declared] required prototype functions */
static bool STDriverFlashImpl_LibInit(void) {
    STFlashDeviceUnit_Initialize();
    return true;
}

static void STDriverFlashImpl_LibFini(void) {
}

u32
STDriverFlashImpl_GetNumDeviceUnits(void) {
    return 1;
}

LTDeviceUnit
STDriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTHandle hFlashDevice = (0 == nDeviceUnitNumber) ? STFlashDeviceUnit_CreateHandle() : 0;
    return hFlashDevice;
}


/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Feb-26   augustus    created
 */
