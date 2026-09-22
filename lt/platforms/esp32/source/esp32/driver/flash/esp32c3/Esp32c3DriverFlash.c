/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32c3/Esp32c3DriverFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>

#include "Esp32c3FlashDeviceUnit.h"

/*_________________________________________________________
 / Esp32c3DriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, Esp32c3DriverFlash);

/*_________________________________________________________________________
 / Esp32c3DriverFlashImpl [macro declared] required prototype functions */
static bool Esp32c3DriverFlashImpl_LibInit(void) { return Esp32c3FlashDeviceUnit_Initialize(); }

static void Esp32c3DriverFlashImpl_LibFini(void) { Esp32c3FlashDeviceUnit_Finalize(); }

static u32 Esp32c3DriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit Esp32c3DriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? Esp32c3FlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
