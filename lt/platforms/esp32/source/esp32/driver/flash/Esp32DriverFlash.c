/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32DriverFlashImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>

#include "Esp32FlashDeviceUnit.h"

/*_________________________________________________________
 / Esp32DriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, Esp32DriverFlash);

/*_________________________________________________________________________
 / Esp32DriverFlashImpl [macro declared] required prototype functions */
static bool Esp32DriverFlashImpl_LibInit(void) { return Esp32FlashDeviceUnit_Initialize(); }

static void Esp32DriverFlashImpl_LibFini(void) { Esp32FlashDeviceUnit_Finalize(); }

static u32 Esp32DriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit Esp32DriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? Esp32FlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-May-22   vitellius   created
 */
