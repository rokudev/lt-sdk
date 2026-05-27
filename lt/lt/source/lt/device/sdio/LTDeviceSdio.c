/*******************************************************************************
 * lt/device/sdio/LTDeviceSdio.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/LTTypes.h>
#include <lt/device/sdio/LTDeviceSdio.h>
#include <lt/driver/sdio/LTDriverSdio.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.sdio");

static LTDriverSdio    *s_pDriver;


/*******************************************************************************
 * Library startup and shutdown:                                              */
static void LTDeviceSdioImpl_LibFini(void) {
    lt_closelibrary(s_pDriver);
}

static bool LTDeviceSdioImpl_LibInit(void) {
    LTDeviceConfig *deviceConfig = ((LTDeviceConfig *)LT_GetCore()->OpenLibrary("LTDeviceConfig"));
    if (deviceConfig) {
        const char *libName = deviceConfig->GetDriverAt("LTDeviceSdio", 0);
        s_pDriver = (LTDriverSdio *)LT_GetCore()->OpenLibrary(libName);
        if (!s_pDriver) {
            LTLOG_YELLOWALERT("no.driver", "can't open SDIO driver library %s", libName);
            return false;
        }
    }
    return true;
}

void LTDeviceSdioImpl_EnumerateCardIds(LTDeviceSdio_EnumerateCardIdsProc *pCardIdProc,
                                       void *pClientData) {
    s_pDriver->EnumerateCardIds(pCardIdProc, pClientData);
}

define_LTLIBRARY_ROOT_INTERFACE (LTDeviceSdio) {} LTLIBRARY_DEFINITION


/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-Feb-24   commodus    created
 */
