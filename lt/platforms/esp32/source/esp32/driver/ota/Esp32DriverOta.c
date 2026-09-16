/******************************************************************************
 * Esp32DriverOta.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/*******************************************************************************
 * NOTE: This is an unimplemented skeleton driver.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/ota/LTDeviceOta.h>

static bool Esp32DriverOta_Init(void) {
    return true;
}

static bool Esp32DriverOta_IsValidated(void) {
    return true;
}

static bool Esp32DriverOta_CheckStorage(u32 imageSize) {
    LT_UNUSED(imageSize);
    return true;
}

static bool Esp32DriverOta_PrepareStorage(u32 imageSize) {
    LT_UNUSED(imageSize);
    return true;
}

static bool Esp32DriverOta_SaveBlock(const u8 *data, u32 dataLen, u32 offsetToSave) {
    LT_UNUSED(data);
    LT_UNUSED(dataLen);
    LT_UNUSED(offsetToSave);
    return false;
}

static bool Esp32DriverOta_VerifyImage(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]) {
    LT_UNUSED(imageSize);
    LT_UNUSED(imageHash);
    return true;
}

static bool Esp32DriverOta_ApplyUpdate(const char *version) {
    LT_UNUSED(version);
    return true;
}

static void Esp32DriverOta_Complete(void) {}

static bool Esp32DriverOta_MarkValidated(void) {
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTOta s_ILTOta;

static u32 Esp32DriverOtaImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit Esp32DriverOtaImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTOta, 1);
}

static bool Esp32DriverOtaImpl_LibInit(void) {
    return true;
}

static void Esp32DriverOtaImpl_LibFini(void) {
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTOta, Esp32DriverOta);

define_LTLIBRARY_INTERFACE(ILTOta) {
    .Init            = &Esp32DriverOta_Init,
    .IsValidated     = &Esp32DriverOta_IsValidated,
    .CheckStorage    = &Esp32DriverOta_CheckStorage,
    .PrepareStorage  = &Esp32DriverOta_PrepareStorage,
    .SaveBlock       = &Esp32DriverOta_SaveBlock,
    .VerifyImage     = &Esp32DriverOta_VerifyImage,
    .ApplyUpdate     = &Esp32DriverOta_ApplyUpdate,
    .Complete        = &Esp32DriverOta_Complete,
    .MarkValidated   = &Esp32DriverOta_MarkValidated,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverOta, (ILTOta))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Sep-26   augustus   created
 */
