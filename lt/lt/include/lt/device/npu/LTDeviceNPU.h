/******************************************************************************
 * <lt/device/video/LTDeviceNPU.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_NPU_LTDEVICENPU_H
#define LT_INCLUDE_LT_DEVICE_NPU_LTDEVICENPU_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

// NPU data type enumeration
typedef enum {
  LT_DEVICE_NPU_DATA_TYPE_U8 = 0x0,
  LT_DEVICE_NPU_DATA_TYPE_S8 = 0x1,
  LT_DEVICE_NPU_DATA_TYPE_F32 = 0x2,
  LT_DEVICE_NPU_DATA_TYPE_U32 = 0x3,
  LT_DEVICE_NPU_DATA_TYPE_S32 = 0x4,
  LT_DEVICE_NPU_DATA_TYPE_RESERVED = 0x5
} LTDeviceNPUDataType;

// NPU data arrangement type enumeration
typedef enum {
  LT_DEVICE_NPU_DATA_ARRANGE_HWC = 0x0,
  LT_DEVICE_NPU_DATA_ARRANGE_CHW = 0x1,
  LT_DEVICE_NPU_DATA_ARRANGE_HCW = 0x2,
  LT_DEVICE_NPU_DATA_ARRANGE_RESERVED = 0x3
} LTDeviceNPUDataArrangeType;

typedef struct {
  u32 height;
  u32 width;
  u32 channels;
  u32 scale;
  void* data;
  u32 size;
  LTDeviceNPUDataType data_type;
  LTDeviceNPUDataArrangeType data_arrange_type;
} LTDeviceNPUTensorInfo;

#define LT_DEVICE_NPU_MAX_MODEL_INPUTS 8
#define LT_DEVICE_NPU_MAX_MODEL_OUTPUTS 8

typedef struct {
  u32 num_inputs;
  u32 num_outputs;
  LTDeviceNPUTensorInfo inputs[LT_DEVICE_NPU_MAX_MODEL_INPUTS];
  LTDeviceNPUTensorInfo outputs[LT_DEVICE_NPU_MAX_MODEL_OUTPUTS];
} LTDeviceNPUModelInfo;

// clang-format off
typedef_LTObject(LTDeviceNPU, 1) {
  bool (*LoadModel)(const void* model_data, LT_SIZE model_size, LTHandle* model_handle);
  /**< Load (parse/register) a model.
   *   Preconditions:
   *     - model_data != NULL
   *     - model_size > 0
   *     - model_handle != NULL
   *   Behaviour:
   *     - On success returns true and *model_handle is set to a valid handle.
   *     - On failure returns false and *model_handle is set to / left as LTHANDLE_INVALID.
   *     - The caller retains ownership of model_data.
   *     - Fails if arguments invalid, parse/allocation/NPU creation fails, or platform limits exceeded.
   *   @param[in]  model_data   Pointer to model data.
   *   @param[in]  model_size   Size in bytes of model data.
   *   @param[out] model_handle Receives model handle (LTHANDLE_INVALID on failure).
   *   @return true on success, false otherwise.
   */
  void (*UnloadModel)(LTHandle model_handle);
  /**< Unload a previously loaded model.
   *   Behaviour:
   *     - Safe to call with LTHANDLE_INVALID (no-op).
   *     - Releases all resources associated with the handle; any LTDeviceNPUModelInfo data pointers
   *       obtained for this model become invalid after return.
   *   @param model_handle Model handle returned by LoadModel (or LTHANDLE_INVALID).
   */
  bool (*GetModelInformation)(LTHandle model_handle, LTDeviceNPUModelInfo* info);
  /**< Populate model tensor metadata and buffer pointers.
   *   Preconditions:
   *     - model_handle is a valid handle returned by LoadModel.
   *     - info != NULL.
   *   Behaviour:
   *     - Returns false for invalid handle or NULL info.
   *     - On success fills info->num_inputs/outputs (clamped to LT_DEVICE_NPU_MAX_MODEL_INPUTS/OUTPUTS) and
   *       per-tensor fields (width/height/channels/scale/size/data_type/data_arrange_type).
   *     - input[i].data points to writable model-owned buffer for supplying input tensor data.
   *     - output[i].data points to read-only model-owned buffer updated by RunModel.
   *     - The data pointers remain valid until UnloadModel for this handle.
   *   @param[in]  model_handle Valid model handle.
   *   @param[out] info         Destination for model information (must not be NULL).
   *   @return true on success, false otherwise.
   */
  bool (*RunModel)(LTHandle model_handle);
  /**< Execute inference synchronously for the given model.
   *   Preconditions:
   *     - model_handle is a valid handle returned by LoadModel.
   *     - Caller has populated input tensor buffers (via pointers from GetModelInformation) as required.
   *   Behaviour:
   *     - Blocks until inference completes; updates output tensor buffers in place.
   *     - Returns false if handle invalid or the underlying NPU run fails.
   *   Thread-safety:
   *     - Calls for the same model_handle must be serialized by the caller.
   *   @param model_handle Valid model handle.
   *   @return true on success, false otherwise.
   */
} LTOBJECT_API;
// clang-format on

LT_EXTERN_C_END
#endif  // LT_INCLUDE_LT_DEVICE_NPU_LTDEVICENPU_H
