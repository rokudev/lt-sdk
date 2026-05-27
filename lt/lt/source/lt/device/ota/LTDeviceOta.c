/*******************************************************************************
 * LTDeviceOta.c: OTA device
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/ota/LTDeviceOta.h>

DEFINE_LTLOG_SECTION("dev.ota");

/** Standard LT Interfaces ****************************************************/

static LTDriverLibrary *s_driver = NULL;
static LTHandle s_hOta = 0;
static ILTOta *s_iOta = NULL;

static bool LTDeviceOta_Init(void) {
    return s_iOta->Init();
}

static bool LTDeviceOta_IsValidated(void) {
    return s_iOta->IsValidated();
}

static bool LTDeviceOta_CheckStorage(u32 imageSize) {
    return s_iOta->CheckStorage(imageSize);
}

static bool LTDeviceOta_PrepareStorage(u32 imageSize) {
    return s_iOta->PrepareStorage(imageSize);
}

static bool LTDeviceOta_SaveBlock(const u8 *data, u32 dataLen, u32 offsetToSave)  {
    return s_iOta->SaveBlock(data, dataLen, offsetToSave);
}

static bool LTDeviceOta_VerifyImage(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]) {
    return s_iOta->VerifyImage(imageSize, imageHash);
}

static bool LTDeviceOta_ApplyUpdate(const char *version) {
    return s_iOta->ApplyUpdate(version);
}

static void LTDeviceOta_Complete(void) {
    s_iOta->Complete();
}

static bool LTDeviceOta_MarkValidated(void) {
    return s_iOta->MarkValidated();
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void ShutdownDevice(void) {
    lt_destroyhandle(s_hOta);
    s_hOta = 0;
    s_iOta = NULL;
    lt_closelibrary(s_driver);
    s_driver = NULL;
}

static void LTDeviceOtaImpl_LibFini(void) {
    ShutdownDevice();
}

static bool LTDeviceOtaImpl_LibInit(void) {
    do {
        s_driver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceOta", 0);
        if (!s_driver) break;
        s_hOta = s_driver->CreateDeviceUnitHandle(0);
        if (!s_hOta) break;
        s_iOta = lt_gethandleinterface(ILTOta, s_hOta);
        return true;
    } while (0);

    ShutdownDevice();
    LTLOG_YELLOWALERT("init.fail", "init fail");
    return false;
}

static u32 LTDeviceOtaImpl_GetNumDeviceUnits(void) {
    return s_driver->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceOtaImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    return s_driver->CreateDeviceUnitHandle(deviceUnitNumber);
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceOta) {
    .Init           = &LTDeviceOta_Init,
    .IsValidated    = &LTDeviceOta_IsValidated,
    .CheckStorage   = &LTDeviceOta_CheckStorage,
    .PrepareStorage = &LTDeviceOta_PrepareStorage,
    .SaveBlock      = &LTDeviceOta_SaveBlock,
    .VerifyImage    = &LTDeviceOta_VerifyImage,
    .ApplyUpdate    = &LTDeviceOta_ApplyUpdate,
    .Complete       = &LTDeviceOta_Complete,
    .MarkValidated  = &LTDeviceOta_MarkValidated,
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Aug-23   gallienus   created
 */
