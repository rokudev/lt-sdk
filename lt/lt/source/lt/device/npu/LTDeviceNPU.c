/******************************************************************************
 * lt/device/npu/LTDeviceNPU.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/npu/LTDeviceNPU.h>
#include <lt/device/npu/LTDriverNPU.h>

DEFINE_LTLOG_SECTION("dev.npu");

static LTDriverNPU* s_driverObj = NULL;

static bool LTDeviceNPUImpl_LoadModel(const void* model_data, LT_SIZE model_size, LTHandle* model_handle) {
    return s_driverObj->API->LoadModel(model_data, model_size, model_handle);
}
static void LTDeviceNPUImpl_UnloadModel(LTHandle model_handle) {
    s_driverObj->API->UnloadModel(model_handle);
}
static bool LTDeviceNPUImpl_GetModelInformation(LTHandle model_handle, LTDeviceNPUModelInfo* info) {
    return s_driverObj->API->GetModelInformation(model_handle, info);
}
static bool LTDeviceNPUImpl_RunModel(LTHandle model_handle) {
    return s_driverObj->API->RunModel(model_handle);
}

typedef_LTObjectImpl(LTDeviceNPU, LTDeviceNPUImpl) { } LTOBJECT_API;

static void LTDeviceNPUImpl_DestructObject(LTDeviceNPUImpl* impl) { LT_UNUSED(impl); }
static bool LTDeviceNPUImpl_ConstructObject(LTDeviceNPUImpl* impl) { LT_UNUSED(impl); return true; }

// Bind public API
define_LTObjectImplPublic(LTDeviceNPU, LTDeviceNPUImpl,
    LoadModel,
    UnloadModel,
    GetModelInformation,
    RunModel
);

static void LTDeviceNPU_LibFini(void) {
    lt_destroyobject(s_driverObj);
    s_driverObj = NULL;
}
static bool LTDeviceNPU_LibInit(void) {
    s_driverObj = lt_createobject(LTDriverNPU);
    if (!s_driverObj) {
        LTLOG_REDALERT("driver.create", "lt_createobject(LTDriverNPU) returned NULL");
        return false; /* Construction must fail so caller sees object not usable */
    }
    return true;
}

define_LTObjectLibrary(1, LTDeviceNPU_LibInit, LTDeviceNPU_LibFini);
