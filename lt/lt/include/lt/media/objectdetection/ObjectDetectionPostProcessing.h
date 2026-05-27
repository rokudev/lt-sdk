/*******************************************************************************
 * LT Object Detection Post-Processing Header
 *
 * Clean, modular implementation of SSD-style object detection post-processing
 * with both integer and floating-point pipelines. Compatible with AK NNE
 * interface but with improved code structure and readability.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef _LT_MEDIA_OBJECTDETECTION_POST_PROCESSING_H_
#define _LT_MEDIA_OBJECTDETECTION_POST_PROCESSING_H_

#include <lt/core/LTCore.h>
#include <lt/media/objectdetection/LTMediaObjectDetection.h>

LT_EXTERN_C_BEGIN

/*******************************************************************************
 * Constants
 ******************************************************************************/

#define MAX_NUM_OF_BOX  20

/* Following constants exposed for unittests*/
#define MAX_DETECTIONS  200
#define NUM_LAYERS      4
#define NUM_CLASSES     2
#define BBOX_COORDS     4    // Number of bounding box coordinates (x1, y1, x2, y2)

/*******************************************************************************
 * Data Structures - Clean Object Detection Interface
 ******************************************************************************/

/* Internal bounding box representation with additional processing fields */
typedef struct {
    int score;
    int x1, y1, x2, y2;
    int area;
    int exist;              // State flag: 1=active, 0=processed/suppressed
    int label;              // Object class identifier
} Bbox;

/* Score ordering structure for NMS sorting */
typedef struct {
    int score;              // Confidence score (for sorting)
    int originalOrder;      // Original index in detection array
} OrderScore;

/* SSD layer configuration structure */
typedef struct {
    int feature_w, feature_h;
    int anchor_count;
    int* min_boxes;
} LayerConfig;

/* Prior anchor structure for SSD */
typedef struct {
    float center_x, center_y;
    float width, height;
} PriorAnchor;

/* Object information structure */
typedef struct {
    int label;
    int confidence;
    LTCropBBox box;
} OBJECT_INFO;

/* Input parameters structure */
typedef struct {
    float confidence_threshold;
    float iou_threshold;
} Param_Input_S;

/* Output parameters structure */
typedef struct {
    int obj_num;
    OBJECT_INFO obj_info[MAX_NUM_OF_BOX];
} Param_Output_S;

/* Blob information structure */
typedef struct {
    int w, h, c;        // Width, height, channels
    void* VirAddr;      // Virtual address for data (Only needed for output blobs. For input blobs set to NULL)
    int scale;          // Quantization scale: {8, 16, 32} for int8, int16, int32
} INPUT_OUTPUT_BLOB_INFO_S;

/* Blob vector structure */
typedef struct {
    INPUT_OUTPUT_BLOB_INFO_S* array_blobs;
    int blob_cnt;
} BLOB_VECTOR_S;

/*******************************************************************************
 * Main Post-Processing Functions
 ******************************************************************************/

/* Integer quantized post-processing pipeline */
int post_process_int(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                     Param_Input_S input_param, Param_Output_S* output_param);

/* Floating-point post-processing pipeline */
int post_process_float(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                       Param_Input_S input_param, Param_Output_S* output_param);

/*******************************************************************************
 * Utility Functions for Testing and Advanced Usage
 ******************************************************************************/

/* Apply Non-Maximum Suppression with selectable arithmetic mode */
int apply_nms(Bbox* boxes, OrderScore* scores, int count, float iou_threshold, bool use_int);

/* Process quantized detections from neural network outputs (integer pipeline) */
int process_quantized_detections(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                                LayerConfig* layer_configs, Bbox* boxes, OrderScore* scores,
                                Param_Input_S input_param);

/* Get label and confidence from quantized scores using lookup tables */
int get_label_and_confidence_int(signed char score0, signed char score1,
                                int scale, int* label, int* confidence);

/* Calculate IoU using integer arithmetic (returns percentage 0-100) */
int calculate_bbox_iou_int(Bbox* box1, Bbox* box2);

/* Calculate IoU using floating-point arithmetic (returns ratio 0.0-1.0) */
float calculate_bbox_iou(Bbox* box1, Bbox* box2);

/* Sort detection scores in ascending order using bubble sort */
int bubble_sort_detections(OrderScore* scores, int count);

/* Cleanup variable number of allocated memory pointers using variadic arguments */
void cleanup_memory(int count, ...);

/* Calculate total number of prior anchors across all SSD layers */
int calculate_total_priors(LayerConfig* layer_configs);

/* Convert CHW (Channel-Height-Width) to HWC (Height-Width-Channel) format */
void convert_chw_to_hwc_float(float* input, float* output, int w, int h, int channels);

/* Filter detections by confidence threshold and create final bounding boxes */
int filter_detections_by_confidence(float* score_data, float* location_data, Bbox* boxes, OrderScore* scores,
                                   int total_priors, int input_w, int input_h, float confidence_threshold);

/* Generate prior anchor boxes for all SSD detection layers */
int generate_prior_anchors(PriorAnchor* priors, LayerConfig* layer_configs, int input_w, int input_h);

/* Convert SSD location regression outputs to absolute corner coordinates */
int convert_locations_to_boxes_wrapper(float* location_data, PriorAnchor* priors, int total_priors);

LT_EXTERN_C_END

#endif /* _LT_MEDIA_OBJECTDETECTION_POST_PROCESSING_H_ */
