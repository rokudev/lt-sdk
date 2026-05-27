/*******************************************************************************
 * LTDeviceQrreader.c: QR code reader
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/qrreader/LTDeviceQrreader.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.qr");

/** Standard LT Interfaces ****************************************************/

static LTDriverLibrary *s_driver = NULL;
static LTHandle s_hQrreader = 0;
static ILTQrreader *s_qrreader = NULL;


bool LTDeviceQrreader_Start(bool bStopOnDecode) {
    return s_qrreader->Start(bStopOnDecode);
}

void LTDeviceQrreader_Stop(void) {
    s_qrreader->Stop();
}

bool LTDeviceQrreader_GetCode(char *code, u16 *codeLen) {
    return s_qrreader->GetCode(code, codeLen);
}

void LTDeviceQrreader_OnStatusChange(LTDeviceQrreaderEventProc proc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData) {
    s_qrreader->OnStatusChange(proc, clientDataReleaseProc, clientData);
}

void LTDeviceQrreader_NoStatusChange(LTDeviceQrreaderEventProc proc) {
    s_qrreader->NoStatusChange(proc);
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void ShutdownDevice(void) {
    lt_destroyhandle(s_hQrreader);
    s_hQrreader = 0;
    s_qrreader = NULL;
    lt_closelibrary(s_driver);
    s_driver = NULL;
}

static void LTDeviceQrreaderImpl_LibFini(void) {
    ShutdownDevice();
}

static bool LTDeviceQrreaderImpl_LibInit(void) {
    do {
        s_driver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceQrreader", 0);
        if (!s_driver) break;
        s_hQrreader = s_driver->CreateDeviceUnitHandle(0);
        if (!s_hQrreader) break;
        s_qrreader = lt_gethandleinterface(ILTQrreader, s_hQrreader);
        return true;
    } while (0);

    ShutdownDevice();
    LTLOG_YELLOWALERT("init.fail", "init fail");
    return false;
}

static u32 LTDeviceQrreaderImpl_GetNumDeviceUnits(void) {
    return s_driver->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceQrreaderImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    return s_driver->CreateDeviceUnitHandle(deviceUnitNumber);
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceQrreader) {
    .Start           = &LTDeviceQrreader_Start,
    .Stop            = &LTDeviceQrreader_Stop,
    .GetCode         = &LTDeviceQrreader_GetCode,
    .OnStatusChange  = &LTDeviceQrreader_OnStatusChange,
    .NoStatusChange  = &LTDeviceQrreader_NoStatusChange,
} LTLIBRARY_DEFINITION;

