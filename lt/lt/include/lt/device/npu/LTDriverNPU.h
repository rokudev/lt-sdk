/*******************************************************************************
 * <lt/device/npu/LTDriverNPU.h>
 *
 * Driver-level NPU LTObject interface.
 *
 * This object encapsulates platform NPU initialization (clock + NNE lib)
 * and exposes model lifecycle operations.
 * Version: 1.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_NPU_LTDRIVERNPU_H
#define LT_INCLUDE_LT_DEVICE_NPU_LTDRIVERNPU_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/device/npu/LTDeviceNPU.h>

LT_EXTERN_C_BEGIN

typedef_LTObject(LTDriverNPU, 1) {
    /* Model lifecycle operations (no implicit self parameter provided by macro binding) */
    bool (*LoadModel)(const void *model_data, LT_SIZE model_size, LTHandle *model_handle);
    void (*UnloadModel)(LTHandle model_handle);
    bool (*GetModelInformation)(LTHandle model_handle, LTDeviceNPUModelInfo *info);
    bool (*RunModel)(LTHandle model_handle);
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* LT_INCLUDE_LT_DEVICE_NPU_LTDRIVERNPU_H */
