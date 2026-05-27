/*******************************************************************************
 * lt/source/lt/device/adc/LTDeviceADCImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/adc/LTDeviceADC.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.adc")
#define P(...)

/* Note:
        LTDeviceADC currently supports the loading and use of one and only one Driver Library.
        This imposed limitation saves code space and RAM usage, and is likely to be consistent
        with all practical use cases in the forseeable future.  If more than one Driver Library
        is needed to support multiple A/D-convert Driver Libraries in a single device, this Device
        Library will have to change to load multiple Driver Libraries in a fashion similar to
        LTDeviceLED. */

static struct {
    LTDriverLibrary *pDriverLibrary;
    ILTDriverADC    *pILTDriverADC;
} s_Driver;

/***********************************************************************************************************************
 * Standard Device Instance access:                                                                                   */

static LTADCChannel *LTDeviceADCImpl_GetChannel(u32 nChannel) {
    return s_Driver.pILTDriverADC->GetChannel(nChannel);
}

static LTADCChannel *LTDeviceADCImpl_GetChannelByName(const char *pChannelName) {
    return s_Driver.pILTDriverADC->GetChannelByName(pChannelName);
}

static u32 LTDeviceADCImpl_EnumerateChannels(LTDeviceADC_EnumerateChannelProc *pChannelEnumerationProc, void *pClientData) {
    return s_Driver.pILTDriverADC->EnumerateChannels(pChannelEnumerationProc, pClientData);
}

static bool ChannelEnumerationProc(const char *pChannelName, void *pClientData) { LT_UNUSED(pChannelName), LT_UNUSED(pClientData); return true; }

static u32 LTDeviceADCImpl_GetNumChannels(void) { return LTDeviceADCImpl_EnumerateChannels(&ChannelEnumerationProc, NULL); }

/***********************************************************************************************************************
 * Library startup and shutdown: Open and close Driver Libraries:                                                     */

static void LTDeviceADCImpl_LibFini(void) {
    P("fini", NULL);
    lt_closelibrary(s_Driver.pDriverLibrary);
    s_Driver.pDriverLibrary = NULL;
    s_Driver.pILTDriverADC = NULL;
}

static bool LTDeviceADCImpl_LibInit(void) {
    P("init", NULL);
    bool bOK = false;
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (deviceConfig) {
        const char *pLibName = deviceConfig->GetDriverAt("LTDeviceADC", 0);
        bOK =   pLibName
             && (s_Driver.pDriverLibrary = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibName))
             && (s_Driver.pILTDriverADC  = lt_getlibraryinterface(ILTDriverADC, s_Driver.pDriverLibrary));
        lt_closelibrary(deviceConfig);
    }
    if (!bOK) {
        LTLOG_YELLOWALERT("drv.lib.0", "%p %p", s_Driver.pDriverLibrary, s_Driver.pILTDriverADC);
        LTDeviceADCImpl_LibFini();
    }
    return bOK;
}

define_LTLIBRARY_ROOT_INTERFACE(LTDeviceADC)
    .GetChannel                     = LTDeviceADCImpl_GetChannel,
    .GetChannelByName               = LTDeviceADCImpl_GetChannelByName,
    .GetNumChannels                 = LTDeviceADCImpl_GetNumChannels,
    .EnumerateChannels              = LTDeviceADCImpl_EnumerateChannels
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Nov-23   nero        created
 *  31-Jan-24   constantine Rework to make generic
 */
