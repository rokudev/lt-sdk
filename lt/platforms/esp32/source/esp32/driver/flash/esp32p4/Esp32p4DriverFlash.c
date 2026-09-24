/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32p4/Esp32p4DriverFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>

#include "Esp32p4FlashDeviceUnit.h"

/*_________________________________________________________
 / Esp32p4DriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, Esp32p4DriverFlash);

/*_________________________________________________________________________
 / Esp32p4DriverFlashImpl [macro declared] required prototype functions */
static bool Esp32p4DriverFlashImpl_LibInit(void) { return Esp32p4FlashDeviceUnit_Initialize(); }

static void Esp32p4DriverFlashImpl_LibFini(void) { Esp32p4FlashDeviceUnit_Finalize(); }

static u32 Esp32p4DriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit Esp32p4DriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? Esp32p4FlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created, from the esp32c3 driver
 */
