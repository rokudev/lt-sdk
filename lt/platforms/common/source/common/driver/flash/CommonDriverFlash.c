/*******************************************************************************
 * platforms/common/source/common/driver/flash/CommonDriverFlashImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include "CommonFlashDeviceUnit.h"

/*________________________________________________________
 / CommonDriverFlash driver library macro instatiation */
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFlash, CommonDriverFlash);

/*________________________________________________________________________
 / CommonDriverFlashImpl [macro declared] required prototype functions */
static bool CommonDriverFlashImpl_LibInit(void) { CommonFlashDeviceUnit_Initialize(); return true; }

static void CommonDriverFlashImpl_LibFini(void) {}

static u32 CommonDriverFlashImpl_GetNumDeviceUnits(void) { return 2; }

static LTDeviceUnit CommonDriverFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    return nDeviceUnitNumber <= 1 ? CommonFlashDeviceUnit_CreateHandle() : 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 */
