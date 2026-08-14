/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32s3/Esp32s3DriverFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>

#include "Esp32s3FlashDeviceUnit.h"

/*_________________________________________________________
 / Esp32s3DriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, Esp32s3DriverFlash);

/*_________________________________________________________________________
 / Esp32s3DriverFlashImpl [macro declared] required prototype functions */
static bool Esp32s3DriverFlashImpl_LibInit(void) { return Esp32s3FlashDeviceUnit_Initialize(); }

static void Esp32s3DriverFlashImpl_LibFini(void) { Esp32s3FlashDeviceUnit_Finalize(); }

static u32 Esp32s3DriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit Esp32s3DriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? Esp32s3FlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
