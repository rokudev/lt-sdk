/*******************************************************************************
 * LT Object Detection Model Utilities Header
 *
 * Function declarations for post-processing utilities for object detection
 * models including mathematical functions, tensor operations, non-maximum
 * suppression, confidence filtering, coordinate transformations, and inference
 * result processing.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_MEDIA_OBJECTDETECTION_MODEL_UTILS_H
#define LT_MEDIA_OBJECTDETECTION_MODEL_UTILS_H

#include <stdbool.h>

LT_EXTERN_C_BEGIN

/* Constants for object detection post-processing */
#define DEFAULT_IOU_THRESHOLD        0.5f    // Default IoU threshold for NMS
#define DEFAULT_CONFIDENCE_THRESHOLD 0.5f    // Default confidence threshold
#define MAX_DETECTIONS_PER_FRAME     100     // Maximum detections per frame
#define MIN_BOX_SIZE                 4.0f    // Minimum bounding box size (pixels)

/* Mathematical utility functions */
float fast_exp(float x);
float sigmoid(float x);
float softmax(float x, float* input_array, int array_size);
int softmax_per_line(float* data, int num_rows, int num_cols);

LT_EXTERN_C_END

#endif /* LT_MEDIA_OBJECTDETECTION_MODEL_UTILS_H */
