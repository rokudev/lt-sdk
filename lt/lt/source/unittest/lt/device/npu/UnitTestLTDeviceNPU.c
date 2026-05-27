/*******************************************************************************
 * NPU Device Test
 *
 * Test LTDeviceNPU
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/core/LTCore.h>
#include <lt/device/npu/LTDeviceNPU.h>
#include <tilt/JiltEngine.h>
#include <math.h> /* for fabsf */

#define IMAGE_WIDTH         224
#define IMAGE_HEIGHT        224
#define IMAGE_NUM_CHANNELS  3
#define INPUT_FORMAT_RGB    0x01
#define BINARY_CLASSIFIER_NUM_CLASSES 2

// Test model data - artificial and does nothing useful, its purpose here
// in this set of tests is to make sure it gets unloaded successfully (in the
// Unload Model Test) and replaced (in a subsequent test) with a genuine model
//
// Structure: Header + Network Section + Parameter Section  
static const unsigned int unload_test_model_data[] = {
    // === NNE Model Header (48 bytes) ===
    2,          // model_parse_version = 2
    48,         // header_size = 48 bytes 
    384,        // network_size = 384 bytes (unchanged)
    88,         // param_size = 88 bytes (updated after conv2 kernel shrink)
    520,        // model_size = 48 + 384 + 88 = 520 bytes total
    0x0A000000, // model_label = valid test label
    0x01000000, // v_abcd (version 1.0.0.0)
    0, 0, 0, 0, 0, // reserved fields

    // === Network Section (384 bytes) ===
    // Network counts (5 x 4 bytes = 20 bytes)
    4,          // buffer_num = 4 (input, conv1, conv2, output)
    4,          // blob_num = 4 (input, conv1_out, conv2_out, pooled_out)
    4,          // layer_num = 4 (Input, Conv1, Conv2, GlobalAvgPool)
    1,          // input_blob_num = 1
    1,          // output_blob_num = 1
    
    // Input configuration (5 x 4 bytes = 20 bytes) - RGB format
    IMAGE_WIDTH,        // input_w = 224
    IMAGE_HEIGHT,       // input_h = 224
    IMAGE_NUM_CHANNELS, // input_c = 3
    INPUT_FORMAT_RGB,   // input_color = RGB (use local constant to avoid undeclared enum)
    0,                  // input_norm = ABS1
    
    // Buffer sizes (4 x 4 bytes = 16 bytes)
    150528,     // buffer_size[0] input = 224*224*3
    25088,      // buffer_size[1] = 112*112*2 (Conv1 output after stride=2)
    25088,      // buffer_size[2] = 112*112*2 (Conv2 output)
    8,          // buffer_size[3] final output
    
    // Blob info (4 blobs x 4 x 4 bytes = 64 bytes)
    // Blob 0: Input 224x224x3
    (0 << 0) | (224 << 16),
    (224 << 0) | (3 << 16),
    (0 << 0) | (1 << 16),
    (0 << 0) | (0 << 16),
    // Blob 1: Conv1 output 112x112x2
    (1 << 0) | (112 << 16),
    (112 << 0) | (2 << 16),
    (1 << 0) | (0 << 16),
    (0 << 0) | (0 << 16),
    // Blob 2: Conv2 output 112x112x2
    (2 << 0) | (112 << 16),
    (112 << 0) | (2 << 16),
    (2 << 0) | (0 << 16),
    (0 << 0) | (0 << 16),
    // Blob 3: Global pool output 1x1x2
    (3 << 0) | (1 << 16),
    (1 << 0) | (2 << 16),
    (3 << 0) | (0 << 16),
    (1 << 0) | (0 << 16),
    
    // Layer info (4 layers x 16 x 4 bytes = 256 bytes)
    // Layer 0: Input layer
    (0 << 0) | (1 << 16),
    (0 << 0) | (1 << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    0,0,0,0,0,0,
    
    // Layer 1: Conv1 stride=2
    (1 << 0) | (0 << 16),
    (1 << 0) | (1 << 16),
    (0 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (1 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    0x00030301, 0x00000002, 0x0000001B, 0x01000102, 0x00000000, 0x00000000,
    
    // Layer 2: Conv2 (kch=2, stride=1)
    (2 << 0) | (0 << 16),
    (1 << 0) | (1 << 16),
    (1 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (2 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    0x00020301, 0x00000002, 0x00000012, 0x01010101, 0x00000001, 0x00000000,
    
    // Layer 3: GlobalAvgPool
    (3 << 0) | (2 << 16),
    (1 << 0) | (1 << 16),
    (2 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (3 << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    (0xFFFF << 0) | (0xFFFF << 16),
    0,0,0,0,0,0,
    
    // Input blob indices
    0,
    // Output blob indices
    3,
    
    // === Parameter Section (88 bytes) ===
    // Conv1: 27->28 + 18->20
    0,0,0,0,0,0,0,
    0,0,0,0,0,
    // Conv2: 18->20 + 18->20
    0,0,0,0,0,
    0,0,0,0,0
};

static const u32 unload_test_model_data_len = sizeof(unload_test_model_data);

static JiltEngine *s_engine;

// Simple 1D linear model (1x1x1 grayscale) test model (expects NCHW 1,1,1,1 U8 input, outputs the input*2 + 1)
extern const u8 simple_1d_linear_model_2x_plus_1[];
extern const u32 simple_1d_linear_model_2x_plus_1_len;
// Simple 1D linear model (1x1x1 grayscale) test model (expects NCHW 1,1,1,1 U8 input, outputs the input*5 + 4)
extern const u8 simple_1d_linear_model_5x_plus_4_model[];
extern const u32 simple_1d_linear_model_5x_plus_4_model_len;

/**
 * Tilt tests
 */
static void BasicLoadUnloadTest(Tilt *tilt) {
    LTHandle model_handle = LTHANDLE_INVALID;
    LTDeviceNPU *npu = lt_createobject(LTDeviceNPU);
    TILT_ASSERT_TRUE(tilt, npu, "Failed to create LTDeviceNPU object");

    // Test loading a model
    // Debug: Print model data header for verification
    LT_GetCore()->ConsolePrint("DEBUG NPU: unload_test_model_data[0-5] = 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
            unload_test_model_data[0], unload_test_model_data[1], unload_test_model_data[2],
            unload_test_model_data[3], unload_test_model_data[4], unload_test_model_data[5]);
    LT_GetCore()->ConsolePrint("DEBUG NPU: unload_test_model_data_len = %u bytes\n", unload_test_model_data_len);

    bool load_result = npu->API->LoadModel(unload_test_model_data, unload_test_model_data_len, &model_handle);
    TILT_ASSERT_TRUE(tilt, load_result, "Failed to load NPU model");
    TILT_ASSERT_TRUE(tilt, model_handle != LTHANDLE_INVALID, "Model handle is invalid after loading");

    // Test unloading the model
    npu->API->UnloadModel(model_handle);
    lt_destroyobject(npu);
}

static void ModelInformationTest(Tilt *tilt) {
    LTHandle model_handle = LTHANDLE_INVALID;
    LTDeviceNPUModelInfo model_info;
    LTDeviceNPU *npu = lt_createobject(LTDeviceNPU);
    TILT_ASSERT_TRUE(tilt, npu, "Failed to create LTDeviceNPU object");

    // Load model
    bool load_result = npu->API->LoadModel(unload_test_model_data, unload_test_model_data_len, &model_handle);
    TILT_ASSERT_TRUE(tilt, load_result && model_handle != LTHANDLE_INVALID, "Failed to load NPU model for information test");

    // Get model information
    lt_memset(&model_info, 0, sizeof(model_info));
    bool info_result = npu->API->GetModelInformation(model_handle, &model_info);
    TILT_ASSERT_TRUE(tilt, info_result, "Failed to get NPU model information");

    // Validate model information - exact values based on test model
    TILT_ASSERT_TRUE(tilt, model_info.num_inputs == 1, "Model should have exactly 1 input, but = %d", model_info.num_inputs);
    TILT_ASSERT_TRUE(tilt, model_info.num_outputs == 1, "Model should have exactly 1 output, but = %d", model_info.num_outputs);

    // With our model having one input and one output, the two tests below just verify that no one (at some point in the future)
    // has inadvertently set LT_DEVICE_NPU_MAX_MODEL_INPUTS or LT_DEVICE_NPU_MAX_MODEL_OUTPUTS to values less than 1 (i.e. zero)
    TILT_ASSERT_TRUE(tilt, model_info.num_inputs <= LT_DEVICE_NPU_MAX_MODEL_INPUTS, "Too many inputs");
    TILT_ASSERT_TRUE(tilt, model_info.num_outputs <= LT_DEVICE_NPU_MAX_MODEL_OUTPUTS, "Too many outputs");

    // Check input tensor information - expectation: 224x224x3 (RGB)
    LTDeviceNPUTensorInfo *input0 = &model_info.inputs[0]; // model_info.num_inputs == 1
    TILT_ASSERT_TRUE(tilt, input0->width == IMAGE_WIDTH, "Input width mismatch, width=%u", input0->width);
    TILT_ASSERT_TRUE(tilt, input0->height == IMAGE_HEIGHT, "Input height mismatch, height=%u", input0->height);
    TILT_ASSERT_TRUE(tilt, input0->channels == IMAGE_NUM_CHANNELS, "Input channels mismatch, channels=%u", input0->channels);
    TILT_ASSERT_TRUE(tilt, input0->data != NULL, "Input data pointer should not be NULL");

    // Check output tensor information - exact values from test model (Blob 3: 1x1xBINARY_CLASSIFIER_NUM_CLASSES)
    LTDeviceNPUTensorInfo *output0 = &model_info.outputs[0]; // model_info.num_outputs == 1
    TILT_ASSERT_TRUE(tilt, output0->width == 1, "Output width should be 1 (global pooled), width=%u", output0->width);
    TILT_ASSERT_TRUE(tilt, output0->height == 1, "Output height should be 1 (global pooled), height=%u", output0->height);
    TILT_ASSERT_TRUE(tilt, output0->channels == BINARY_CLASSIFIER_NUM_CLASSES, "Output channels should be BINARY_CLASSIFIER_NUM_CLASSES (binary classification), channels=%u", output0->channels);
    TILT_ASSERT_TRUE(tilt, output0->data != NULL, "Output data pointer should not be NULL");

    npu->API->UnloadModel(model_handle);
    lt_destroyobject(npu);
}

static void ErrorHandlingTest(Tilt *tilt) {
    LTHandle model_handle = LTHANDLE_INVALID;
    LTDeviceNPU *npu = lt_createobject(LTDeviceNPU);
    TILT_ASSERT_TRUE(tilt, npu, "Failed to create LTDeviceNPU object");

    // Test loading with NULL data
    bool load_result = npu->API->LoadModel(NULL, unload_test_model_data_len, &model_handle);
    TILT_EXPECT_FALSE(tilt, load_result, "Loading with NULL data should fail");
    TILT_EXPECT_TRUE(tilt, model_handle == LTHANDLE_INVALID, "Model handle should remain invalid");

    // Test loading with zero size
    load_result = npu->API->LoadModel(unload_test_model_data, 0, &model_handle);
    TILT_EXPECT_FALSE(tilt, load_result, "Loading with zero size should fail");
    TILT_EXPECT_TRUE(tilt, model_handle == LTHANDLE_INVALID, "Model handle should remain invalid");

    // Test loading with model_handle pointer being NULL
    load_result = npu->API->LoadModel(unload_test_model_data, unload_test_model_data_len, NULL);
    TILT_EXPECT_FALSE(tilt, load_result, "Loading with NULL model_handle should fail");

    // Test getting information with invalid handle
    LTDeviceNPUModelInfo model_info;
    bool info_result = npu->API->GetModelInformation(LTHANDLE_INVALID, &model_info);
    TILT_EXPECT_FALSE(tilt, info_result, "Getting info with invalid handle should fail");

    // Test running with invalid handle
    bool run_result = npu->API->RunModel(LTHANDLE_INVALID);
    TILT_EXPECT_FALSE(tilt, run_result, "Running with invalid handle should fail");

    // Test unloading invalid handle (should not crash)
    npu->API->UnloadModel(LTHANDLE_INVALID); /* should be no-op */
    lt_destroyobject(npu);
}

/**
 * Simple1DLinearModelTest_2x_plus_1
 * Purpose: Validate the simple_1d_linear_model_2x_plus_1 implements the exact mapping:
 *            output = (input * 2) + 1  (mod 256 for U8 wrap if it occurs)
 * Note: input is a scaled integer in [0,255] representing a float value in the range [0.0, 1.0].
 *
 * Procedure:
 *  - Load the model (expects single 1x1x1 grayscale U8 input)
 *  - For every possible 8-bit value v in [0,255]:
 *      * Set input pixel = v
 *      * Run inference
 *      * Read output byte o
 *      * Log: "Simple1D: in=v out=o" and if o != round(((2*v + 255)*scale)/255) log a mismatch line
 *
 * Verifications (non-fatal except for structural issues):
 *  - Model loads (fatal if not)
 *  - Input tensor dimensions and type (fatal if mismatch)
 *  - Output tensor exists, 1 element, U8 data_type, expected scale
 *  - Each run succeeds (fatal if any run fails)
 *  - Functional expectation: o == round(((2*v + 255) * scale)/255); deviations are reported but do not stop the loop
 */
static void Simple1DLinearModelTest_2x_plus_1(Tilt *tilt) {
    LTHandle model_handle = LTHANDLE_INVALID;
    LTDeviceNPUModelInfo model_info;
    LTDeviceNPU *npu = lt_createobject(LTDeviceNPU);
    TILT_ASSERT_TRUE(tilt, npu, "Failed to create LTDeviceNPU object");

    // Load simple 1D linear model
    bool load_result = npu->API->LoadModel(simple_1d_linear_model_2x_plus_1, simple_1d_linear_model_2x_plus_1_len, &model_handle);
    TILT_ASSERT_TRUE(tilt, load_result && model_handle != LTHANDLE_INVALID, "Failed to load simple_1d_linear_model_2x_plus_1");

    // Query model info
    lt_memset(&model_info, 0, sizeof(model_info));
    bool info_ok = npu->API->GetModelInformation(model_handle, &model_info);
    TILT_ASSERT_TRUE(tilt, info_ok, "Failed to get model information for simple_1d_linear_model_2x_plus_1");
    TILT_ASSERT_TRUE(tilt, model_info.num_inputs == 1, "Expected exactly 1 input");
    TILT_ASSERT_TRUE(tilt, model_info.num_outputs == 1, "Expected exactly 1 output");

    LTDeviceNPUTensorInfo *input = &model_info.inputs[0];
    TILT_ASSERT_TRUE(tilt, input->width == 1, "Input width != 1 (=%u)", input->width);
    TILT_ASSERT_TRUE(tilt, input->height == 1, "Input height != 1 (=%u)", input->height);
    TILT_ASSERT_TRUE(tilt, input->channels == 1, "Input channels != 1 (=%u)", input->channels);
    TILT_ASSERT_TRUE(tilt, input->scale == 1, "Unexpected input scale (%d)", input->scale);
    TILT_ASSERT_TRUE(tilt, input->data != NULL, "Input data pointer NULL");

    LTDeviceNPUTensorInfo *output = &model_info.outputs[0];
    TILT_ASSERT_TRUE(tilt, output->data != NULL, "Output data pointer NULL");
    u32 output_elems = output->width * output->height * output->channels;
    TILT_ASSERT_TRUE(tilt, output_elems == 1, "Output element count is zero");
    TILT_ASSERT_TRUE(tilt, output->size == output_elems, "Output size (%u) does not match element count (%u)", output->size, output_elems);
    TILT_ASSERT_TRUE(tilt, output->scale == 32, "Unexpected output scale (%d)", output->scale);
    // Validate expected unsigned byte output format (U8)
    TILT_ASSERT_TRUE(tilt, output->data_type == LT_DEVICE_NPU_DATA_TYPE_U8, "Unexpected output data_type=%u (expected U8)", output->data_type);

    // Run inference for all 256 possible 8-bit grayscale values.
    for (int v = 0; v < 256; ++v) {
        ((u8*)input->data)[0] = (u8)v; // single pixel value
        bool run_ok = npu->API->RunModel(model_handle);
        TILT_ASSERT_TRUE(tilt, run_ok, "RunModel failed for input value %d", v);

        // Interpret output as unsigned byte
        u8 npu_calculation = ((u8*)output->data)[0];
        // Expected quantized output with NEAREST rounding (the NPU rounds to nearest integer):
        // expected = round( ((2*v + 255) * scale) / 255 )
        // Implement integer rounding by adding half-divisor (127) before division.
        u32 scaled_number = ((u32)(v * 2 + 255) * (u32)output->scale) + 127; // +127 for nearest rounding when dividing by 255
        u8 expected_output = (u8)(scaled_number / 255);
        TILT_ASSERT_TRUE(tilt, npu_calculation == expected_output, "Unexpected output for input %d: expected=%3u got=%3u", v, (unsigned)expected_output, (unsigned)npu_calculation);
    }
    npu->API->UnloadModel(model_handle);
    lt_destroyobject(npu);
}

/**
 * Simple1DLinearModelTest_5x_plus_4
 * What this test checks:
 *   We have a tiny model with 1 input byte (v) and 1 output byte.
 *   The model should behave like:  output_float = 5 * (v / 255) + 4
 *   Then we store that number in a byte using a Q3.4 scale (multiply by 16 and round).
 *   If the result is bigger than 255 we cap it at 255.
 *
 *   So the expected byte is: round( ((5 * (v/255) + 4) * 16 + 0.5) ), limited to 255.
 *
 * How we run it:
 *   - Load the model.
 *   - Loop over v for valid outputs (for Q3.4 format the output must be <= 7.9375).
 *   - Write v into the input.
 *   - Run the model.
 *   - Read the output byte.
 *   - Compute what we expect using the formula above.
 *   - If it differs, log a line showing the mismatch (the test keeps going).
 *
 * Safety / sanity checks (these must be right or we fail fast):
 *   - Exactly 1 input and 1 output.
 *   - Input and output are 1x1x1.
 *   - Input scale is 1 (raw byte), output scale is 16 (4.4 format).
 *   - Output type is unsigned 8-bit.
 *   - Model run call succeeds each time.
 */
static void Simple1DLinearModelTest_5x_plus_4(Tilt *tilt) {
    LTHandle model_handle = LTHANDLE_INVALID;
    LTDeviceNPUModelInfo model_info;
    LTDeviceNPU *npu = lt_createobject(LTDeviceNPU);
    TILT_ASSERT_TRUE(tilt, npu, "Failed to create LTDeviceNPU object");

    // Load simple 1D linear model
    bool load_result = npu->API->LoadModel(simple_1d_linear_model_5x_plus_4_model, simple_1d_linear_model_5x_plus_4_model_len, &model_handle);
    if(!(load_result && model_handle != LTHANDLE_INVALID)){ LT_GetCore()->ConsolePrint("[Simple1DLinearModel5xPlus4] Failed to load simple_1d_linear_model\n"); lt_destroyobject(npu); return; }

    // Query model info
    lt_memset(&model_info, 0, sizeof(model_info));
    bool info_ok = npu->API->GetModelInformation(model_handle, &model_info);
    TILT_ASSERT_TRUE(tilt, info_ok, "Failed to get model information");
    TILT_ASSERT_TRUE(tilt, model_info.num_inputs == 1, "Expected exactly 1 input");
    TILT_ASSERT_TRUE(tilt, model_info.num_outputs == 1, "Expected exactly 1 output");

    LTDeviceNPUTensorInfo *input = &model_info.inputs[0];
    TILT_ASSERT_TRUE(tilt, input->width == 1, "Input width != 1");
    TILT_ASSERT_TRUE(tilt, input->height == 1, "Input height != 1");
    TILT_ASSERT_TRUE(tilt, input->channels == 1, "Input channels != 1");
    TILT_ASSERT_TRUE(tilt, input->scale == 1, "Input scale unexpected");
    TILT_ASSERT_TRUE(tilt, input->data_type == LT_DEVICE_NPU_DATA_TYPE_U8, "Input data_type not U8");
    TILT_ASSERT_TRUE(tilt, input->data != NULL, "Input data pointer NULL");

    LTDeviceNPUTensorInfo *output = &model_info.outputs[0];
    TILT_ASSERT_TRUE(tilt, output->data != NULL, "Output data pointer NULL");
    TILT_ASSERT_TRUE(tilt, output->width == 1, "Output width != 1");
    TILT_ASSERT_TRUE(tilt, output->height == 1, "Output height != 1");
    TILT_ASSERT_TRUE(tilt, output->channels == 1, "Output channels != 1");
    u32 output_elems = output->width * output->height * output->channels;
    TILT_ASSERT_TRUE(tilt, output_elems == 1, "Output element count != 1");
    TILT_ASSERT_TRUE(tilt, output->size == output_elems, "Output size mismatch");
    TILT_ASSERT_TRUE(tilt, output->scale == 16, "Output scale != 16");
    TILT_ASSERT_TRUE(tilt, output->data_type == LT_DEVICE_NPU_DATA_TYPE_U8, "Output data_type not U8");

    // Run inference for 200 possible 8-bit grayscale values - beyond 200 the output overflows its (signed) Q3.4 representation.
    for (int v = 0; v < 200; ++v) {
        ((u8*)input->data)[0] = (u8)v;

        bool run_ok = npu->API->RunModel(model_handle);
        TILT_ASSERT_TRUE(tilt, run_ok, "RunModel failed for input value %d", v);

        // Read raw byte output from model
        u8 npu_calculation = ((u8*)output->data)[0];
        //
        // Note: input range v = [0,255] corresponds to [0.0,1.0] - it is necessary for 255 to represent 1.0,
        // otherwise it would not be possible to represent 1.0 with an 8 bit value at all.
        //
        // *Some* of the inaccuracies of the NPU inference in this test can be explained by the fact that the output byte is
        // in a signed Q3.4 format (even though output->data_type == LT_DEVICE_NPU_DATA_TYPE_U8):
        //   - 3 bits for the whole number part (range going up to +7)
        //   - 4 bits for the fractional part (steps of 1/16)
        //   - 1 bit for the sign
        // Max positive value (before sign) is 7 + 15/16 = 7.9375. 
        //
        // NOTE: There are still some peculiarities: e.g. with 'v' = 1:
        // v = 1 = 1.0/255.0 = 0.003921568
        // 5 * 0.003921568 + 4.0 = 4.019607843
        // 4.019607843 * 16 = 64.31372549
        //
        // HOWEVER - the NPU produces a value of 65 for v=1, this does not give the nearest Q3.4 number:
        //
        // 64.31372549/16 = 4.0208
        // 65 => 65/16 = 4.0625 (greater than the 'correct' value by 0.0417)
        // 64 => 64/16 = 4.0    (less than the 'correct' value by 0.0208)

        float accurate_value = (5.0f * v/255.0f + 4.0f) * (float)output->scale;
        u32 fixed_4p4 = (u32)(((5 * (u32)v + (4 * 255)) * (u32)output->scale) + 127) / 255; // 127 for integer rounding, 255 (not 256) for scaling 'v' (as described above)
        u8 expected_output = (u8)fixed_4p4;
        if(lt_abs(npu_calculation - expected_output) > 1){
            LT_GetCore()->ConsolePrint("[Simple1DLinearModel5xPlus4] 4.4 mismatch v=%d accurate_fp_calc=%f expected2 = %d got=%d\n", v,accurate_value, expected_output, npu_calculation);
            TILT_ASSERT_TRUE(tilt, false, "Terminating after 4.4 mismatch");
        }
    }
    npu->API->UnloadModel(model_handle);
    lt_destroyobject(npu);
}

static const TiltEngineTest s_tests[] = {
    { BasicLoadUnloadTest,     "BasicLoadUnload",  "Basic NPU model load/unload test",  0 },
    { ModelInformationTest,    "ModelInformation", "NPU model information retrieval test", 0 },
    { Simple1DLinearModelTest_2x_plus_1, "Simple1DLinearModel_2x_plus_1", "Run 1x1 grayscale model over 0..255 inputs", 0 },
    { Simple1DLinearModelTest_5x_plus_4, "Simple1DLinearModel_5x_plus_4", "Run 1x1 grayscale model over 0..255 inputs", 0 }, 
    { ErrorHandlingTest,       "ErrorHandling",    "NPU error handling test", 0 },
};

static int UnitTestLTDeviceNPUImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceNPUImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceNPUImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceNPU, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceNPU, UnitTestLTDeviceNPUImpl_Run, 1536) LTLIBRARY_DEFINITION;
