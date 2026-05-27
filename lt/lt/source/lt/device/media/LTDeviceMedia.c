/*******************************************************************************
 * lt/source/lt/device/media/LTDeviceMedia.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("ltdevicemedia");

/*******************************************************************************
 * Access to the LTDriverMedia library, through which the Device accesses
 * media-related functions:                                                     */
typedef struct {
    LTDriverLibrary * pDriver;
    ILTDriverMedia  * pILTDriver;
} MediaDriver;

static LTArray              * s_pMediaDrivers       = NULL;

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownDriver(void) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        LT_GetCore()->CloseLibrary((LTLibrary *)pMediaDriver->pDriver);
        pMediaDriver->pDriver = NULL;
    }
    lt_destroyobject(s_pMediaDrivers);
    s_pMediaDrivers = NULL;
}

/***************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open the Driver Library.  If successful, get the number of available Device Units
 * from the Driver.  If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                                      */
static void LTDeviceMediaImpl_LibFini(void);
static bool LTDeviceMediaImpl_LibInit(void) {
    if (!(s_pMediaDrivers = LTArray_CreateStructArray(sizeof(MediaDriver)))) {
        LTDeviceMediaImpl_LibFini();
        return false;
    }
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (deviceConfig) {
        MediaDriver mediaDriver;
        u32 nNumDrivers = deviceConfig->GetNumDrivers("LTDeviceMedia");
        for (u32 i = 0; i < nNumDrivers; i++) {
            const char *pLibraryName = deviceConfig->GetDriverAt("LTDeviceMedia", i);
            if (!(mediaDriver.pDriver = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibraryName))) {
                LTLOG("enum.lib", "Failed to open library %s", pLibraryName);
                lt_closelibrary(deviceConfig);
                LTDeviceMediaImpl_LibFini();
                return false;
            }
            if (!(mediaDriver.pILTDriver = lt_getlibraryinterface(ILTDriverMedia, mediaDriver.pDriver))) {
                LTLOG("enum.lib", "Failed to get interface");
                lt_closelibrary(deviceConfig);
                LTDeviceMediaImpl_LibFini();
                return false;
            }
            s_pMediaDrivers->API->Append(s_pMediaDrivers, &mediaDriver);
        }
        lt_closelibrary(deviceConfig);
    }
    if (s_pMediaDrivers->API->GetCount(s_pMediaDrivers) == 0) {
        LTLOG_YELLOWALERT("media.no-drivers", "No media drivers were found");
        LTDeviceMediaImpl_LibFini();
        return false;
    }
    return true;
}

static void LTDeviceMediaImpl_LibFini(void) {
    ShutDownDriver();
}

static u32 LTDeviceMediaImpl_GetNumDeviceUnits(void) { return 1; }

static LTDeviceUnit LTDeviceMediaImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}

static bool
LTDeviceMediaImpl_SupportsFormat(LTMediaFormat * pFormat) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        if (pMediaDriver->pILTDriver->SupportsFormat(pFormat)) {
            return true;
        }
    }
    return false;
}

static bool
LTDeviceMediaImpl_GetProperty(const char *name, void *value) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        if(pMediaDriver->pILTDriver->GetProperty(name, value)) {
            return true;
        }
    }
    return false;
}

static bool
LTDeviceMediaImpl_SetProperty(const char *name, const void *value) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        if (pMediaDriver->pILTDriver->SetProperty(name, value)) return true;
    }
    return false;
}

static LTMediaSource
LTDeviceMediaImpl_OpenSource(LTMediaFormat * pFormat) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        if (pMediaDriver->pILTDriver->SupportsFormat(pFormat)) {
            return pMediaDriver->pILTDriver->OpenSource(pFormat);
        }
    }
    return 0;
}

static LTMediaSink
LTDeviceMediaImpl_OpenSink(LTMediaFormat * pFormat) {
    for (u32 i = 0; i < s_pMediaDrivers->API->GetCount(s_pMediaDrivers); ++i) {
        MediaDriver * pMediaDriver = s_pMediaDrivers->API->Get(s_pMediaDrivers, i, NULL);
        if (pMediaDriver->pILTDriver->SupportsFormat(pFormat)) {
            return pMediaDriver->pILTDriver->OpenSink(pFormat);
        }
    }
    return 0;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceMedia) {
    .SupportsFormat = &LTDeviceMediaImpl_SupportsFormat,
    .GetProperty    = &LTDeviceMediaImpl_GetProperty,
    .SetProperty    = &LTDeviceMediaImpl_SetProperty,
    .OpenSource     = &LTDeviceMediaImpl_OpenSource,
    .OpenSink       = &LTDeviceMediaImpl_OpenSink,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDeviceMedia, (LTMediaNaluSeiImpl));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Jan-21   constantine created
 *  03-Apr-23   augustus    load drivers from LTDeviceConfig specification
 */
