/*******************************************************************************
 * LT Object Detection Post-Processing Implementation
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

#include <lt/media/objectdetection/ObjectDetectionPostProcessing.h>
#include <lt/media/objectdetection/LTMediaObjectDetection.h>
#include <lt/core/LTCore.h>

/*____________________________________
  LTMediaObjectDetection.c #defines */
DEFINE_LTLOG_SECTION("objdet_postprocessing");

/*******************************************************************************
 * Constants and Configuration Data
 ******************************************************************************/

/* Internal constants */
#define CONFIDENCE_SCALE_FACTOR 1000    // Scale factor for integer confidence
#define IOU_SCALE_FACTOR        100     // Scale factor for integer IoU (0-100%)
#define BACKGROUND_CLASS_ID     0       // Background class identifier
#define OBJECT_CLASS_ID         1       // Object class identifier

/* Logging tags for different pipeline operations */
#define TAG_INT_PIPELINE        "obj.det.post.process.int"
#define TAG_FLOAT_PIPELINE      "obj.det.post.process.float"
#define TAG_VALIDATE_INPUTS     "obj.det.validate.pipeline.inputs"
#define TAG_APPLY_NMS           "obj.det.apply.nms"
#define TAG_LABEL_CONFIDENCE    "obj.det.get.label.and.confidence.int"
#define TAG_LAYER_DETECTIONS    "obj.det.process.layer.detections"

/*******************************************************************************
 * Internal Data Structures for Processing (Private Implementation)
 ******************************************************************************/


/* Performance-critical inline functions for embedded optimization */
static inline bool boxes_can_overlap(const Bbox* box1, const Bbox* box2) {
    return !(box1->x2 <= box2->x1 || box2->x2 <= box1->x1 ||
             box1->y2 <= box2->y1 || box2->y2 <= box1->y1);
}

/* Quantized softmax lookup tables for hardware optimization
  • Index represents: score1 - score0 (difference between foreground and background scores)
  • Value represents: Confidence × 1000 (0-1000 range instead of 0.0-1.0)
  • If score0 > score1, we skip detection. Hence Index 0: Equal scores → 50% confidence → value 500
  • Higher indices: Larger differences → higher confidence → values approaching 1000
*/
const unsigned int softmax_table_class2_scale32[160] = {
    500, 508, 516, 523, 531, 539, 547, 554, 562, 570, 577, 585, 593, 600, 608, 615,
    622, 630, 637, 644, 651, 658, 665, 672, 679, 686, 693, 699, 706, 712, 719, 725,
    731, 737, 743, 749, 755, 761, 766, 772, 777, 783, 788, 793, 798, 803, 808, 813,
    818, 822, 827, 831, 835, 840, 844, 848, 852, 856, 860, 863, 867, 871, 874, 877,
    881, 884, 887, 890, 893, 896, 899, 902, 905, 907, 910, 912, 915, 917, 920, 922,
    924, 926, 928, 930, 932, 934, 936, 938, 940, 942, 943, 945, 947, 948, 950, 951,
    953, 954, 955, 957, 958, 959, 960, 962, 963, 964, 965, 966, 967, 968, 969, 970,
    971, 972, 972, 973, 974, 975, 976, 976, 977, 978, 978, 979, 980, 980, 981, 981,
    982, 983, 983, 984, 984, 985, 985, 985, 986, 986, 987, 987, 988, 988, 988, 989,
    989, 989, 990, 990, 990, 991, 991, 991, 991, 992, 992, 992, 992, 993, 993, 993
};

const unsigned int softmax_table_class2_scale16[80] = {
    500, 516, 531, 547, 562, 577, 593, 608, 622, 637, 651, 665, 679, 693, 706, 719,
    731, 743, 755, 766, 777, 788, 798, 808, 818, 827, 835, 844, 852, 860, 867, 874,
    881, 887, 893, 899, 905, 910, 915, 920, 924, 928, 932, 936, 940, 943, 947, 950,
    953, 955, 958, 960, 963, 965, 967, 969, 971, 972, 974, 976, 977, 978, 980, 981,
    982, 983, 984, 985, 986, 987, 988, 988, 989, 990, 990, 991, 991, 992, 992, 993
};

const unsigned int softmax_table_class2_scale8[40] = {
    500, 531, 562, 593, 622, 651, 679, 706, 731, 755, 777, 798, 818, 835, 852, 867,
    881, 893, 905, 915, 924, 932, 940, 947, 953, 958, 963, 967, 971, 974, 977, 980,
    982, 984, 986, 988, 989, 990, 991, 992
};

/* SSD model configuration */
static const int feature_Ws[NUM_LAYERS] = {40, 20, 10, 5};
static const int feature_Hs[NUM_LAYERS] = {30, 15, 8, 4};

static const int anchor_counts[NUM_LAYERS] = {3, 2, 2, 3};
static const int min_boxes_0[6] = {10, 10, 16, 16, 24, 24};
static const int min_boxes_1[4] = {32, 32, 48, 48};
static const int min_boxes_2[4] = {64, 64, 96, 96};
static const int min_boxes_3[6] = {128, 128, 192, 192, 256, 256};

/* SSD hyperparameters */
static const float center_variance = 0.1f;
static const float size_variance = 0.2f;


/* Forward declaration of fast_exp from model_utils.c to avoid header conflicts */
extern float fast_exp(float x);

/* Forward declaration of softmax_per_line from model_utils.c */
extern int softmax_per_line(float* data, int num_rows, int num_cols);

/* Forward declarations for shared utility functions for both int and float pipelines */
static int init_layer_configs(LayerConfig* configs);
static int validate_pipeline_inputs(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs, Param_Input_S input_param, Param_Output_S* output_param);
static void populate_output(Bbox* boxes, int count, Param_Output_S* output_param);
int bubble_sort_detections(OrderScore* scores, int count);
void cleanup_memory(int count, ...);
void convert_chw_to_hwc_float(float* input, float* output, int w, int h, int channels);
static void convert_locations_to_boxes(float loc0, float loc1, float loc2, float loc3,
                                       float prior_center_x, float prior_center_y,
                                       float prior_w, float prior_h,
                                       float* bbox_x1, float* bbox_y1,
                                       float* bbox_x2, float* bbox_y2);


/*******************************************************************************
 * Integer Pipeline Implementation
 ******************************************************************************/
/* Forward declarations */

/* Main integer post-processing pipeline for quantized SSD object detection
 *
 * Processes quantized neural network outputs using:
 * - Hardware-optimized lookup tables for quantized softmax
 * - Float operations for location decoding
 * - Pure integer arithmetic for NMS
 */
int post_process_int(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                     Param_Input_S input_param, Param_Output_S* output_param) {

    // Validate inputs
    if (validate_pipeline_inputs(input_blobs, output_blobs, input_param, output_param) < 0) {
        LTLOG_YELLOWALERT(TAG_INT_PIPELINE, "validate_pipeline_inputs() failed.");
        return -1;
    }

    // Initialize layer configurations
    LayerConfig layer_configs[NUM_LAYERS];
    if (init_layer_configs(layer_configs) < 0) {
        return -1;
    }

    // Allocate working memory for pure integer processing
    Bbox* boxes = lt_malloc(MAX_DETECTIONS * sizeof(Bbox));
    OrderScore* scores = lt_malloc(MAX_DETECTIONS * sizeof(OrderScore));

    if (!boxes || !scores) {
        LTLOG_YELLOWALERT(TAG_INT_PIPELINE, "Memory allocation failed for working arrays.");
        cleanup_memory(2, boxes, scores);
        return -1;
    }

    // Process quantized data directly and create detections.
    // Confidence filtering is applied here, but bboxes are not sorted at this stage.
    int count = process_quantized_detections(input_blobs, output_blobs, layer_configs, boxes, scores, input_param);
    if (count < 0) {
        LTLOG_YELLOWALERT(TAG_INT_PIPELINE, "Failed to process quantized detections");
        cleanup_memory(2, boxes, scores);
        return -1;
    }

    // Apply NMS using consolidated function (integer mode true)
    // We sort detections by confidence and suppress overlaps here.
    count = apply_nms(boxes, scores, count, input_param.iou_threshold, true);
    if (count < 0) {
        LTLOG_YELLOWALERT(TAG_INT_PIPELINE, "NMS failed");
        cleanup_memory(2, boxes, scores);
        return -1;
    }

    // Populate output structure
    populate_output(boxes, count, output_param);

    // Cleanup
    cleanup_memory(2, boxes, scores);

    return 0;
}

/* Initialize SSD layer configurations with feature map dimensions and anchor settings
 *
 * Sets up layer-specific parameters for SSD object detection:
 * - Feature map dimensions for each detection layer
 * - Number of anchors per layer
 * - Anchor box size configurations (min_boxes arrays)
 */
static int init_layer_configs(LayerConfig* configs) {
    if (!configs) return -1;

    const int* min_boxes_arrays[NUM_LAYERS] = {
        min_boxes_0, min_boxes_1, min_boxes_2, min_boxes_3
    };

    for (int i = 0; i < NUM_LAYERS; i++) {
        configs[i].feature_w = feature_Ws[i];
        configs[i].feature_h = feature_Hs[i];
        configs[i].anchor_count = anchor_counts[i];
        configs[i].min_boxes = (int*)min_boxes_arrays[i];
    }
    return 0;
}

/* Process all quantized detection layers for integer pipeline
 * - Iterates through all detection layers and spatial locations
 * - Validates blob quantization scales
 * - Applies confidence filtering and accumulates valid detections
 * - Returns the number of detections found, or -1 on error
 */
int process_quantized_detections(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                                 LayerConfig* layer_configs, Bbox* boxes, OrderScore* scores,
                                 Param_Input_S input_param) {
    /* Input Processing
    • Extracts input dimensions from the first blob (input image width/height)
    • Converts confidence threshold from float (0.0-1.0) to integer scale (0-1000) for quantized processing
    • Validates quantization scales for each layer (must be 8, 16, or 32 for scores; >0 for locations)
    */
    int input_w = input_blobs->array_blobs[0].w;
    int input_h = input_blobs->array_blobs[0].h;
    int confidence_threshold = (int)(input_param.confidence_threshold * CONFIDENCE_SCALE_FACTOR);

    /* Layer-by-Layer Processing
    • Iterates through 4 SSD detection layers (different feature map sizes: 40×30, 20×15, 10×8, 5×4)
    • Extracts layer configuration: feature map dimensions (w×h) and anchor count per location
    • Gets quantized data pointers: score_data and loc_data from neural network output blobs
    */
    int count = 0;
    for (int layer = 0; layer < NUM_LAYERS && count < MAX_DETECTIONS; layer++) {
        LayerConfig* config = &layer_configs[layer];
        INPUT_OUTPUT_BLOB_INFO_S score_blob = output_blobs->array_blobs[layer];
        INPUT_OUTPUT_BLOB_INFO_S loc_blob = output_blobs->array_blobs[layer + 4];

        // Process detections for this layer (inline process_layer_detections)
        // Validate quantization scales
        if ((score_blob.scale != 8 && score_blob.scale != 16 && score_blob.scale != 32) ||
            loc_blob.scale <= 0) {
            LTLOG_YELLOWALERT(TAG_LAYER_DETECTIONS,
                             "Invalid quantization scale: score=%d, loc=%d",
                             score_blob.scale, loc_blob.scale);
            return -1;
        }

        int w = config->feature_w;
        int h = config->feature_h;
        int c = config->anchor_count;

        signed char* score_data = (signed char*)score_blob.VirAddr;
        signed char* loc_data = (signed char*)loc_blob.VirAddr;

        /* Process each spatial location and anchor
        • Nested loops: For each (y,x) position in the feature map
        • For each anchor: At every spatial location (typically 2-3 anchors per location)
        • Extracts quantized scores using CHW format indexing: score_data[y * w * c * NUM_CLASSES + (a * NUM_CLASSES) * w + x]
        */
        for (int y = 0; y < h && count < MAX_DETECTIONS; y++) {
            for (int x = 0; x < w && count < MAX_DETECTIONS; x++) {
                for (int a = 0; a < c && count < MAX_DETECTIONS; a++) {

                    // Process anchor detection (inline process_anchor_detection)
                    // Extract quantized scores (calculate memory offset and matching AK indexing exactly)
                    signed char score0 = score_data[y * w * c * NUM_CLASSES + (a * NUM_CLASSES) * w + x];
                    signed char score1 = score_data[y * w * c * NUM_CLASSES + (a * NUM_CLASSES + 1) * w + x];

                    // Get label and confidence (for a single anchor box) using integer lookup
                    int label, confidence;
                    int result = get_label_and_confidence_int(score0, score1, score_blob.scale, &label, &confidence);

                    // Skip background (label=0) and apply confidence filtering
                    if (result < 0 || label == BACKGROUND_CLASS_ID || confidence < confidence_threshold) {
                        continue;
                    }

                    // Extracts location regression data (loc0, loc1, loc2, loc3) from quantized format
                    // Converts to float coordinates by dividing by quantization scale
                    float loc0 = loc_data[y * w * c * BBOX_COORDS + (a * BBOX_COORDS) * w + x] / (float)loc_blob.scale;
                    float loc1 = loc_data[y * w * c * BBOX_COORDS + (a * BBOX_COORDS + 1) * w + x] / (float)loc_blob.scale;
                    float loc2 = loc_data[y * w * c * BBOX_COORDS + (a * BBOX_COORDS + 2) * w + x] / (float)loc_blob.scale;
                    float loc3 = loc_data[y * w * c * BBOX_COORDS + (a * BBOX_COORDS + 3) * w + x] / (float)loc_blob.scale;

                    // Get anchor dimensions
                    float prior_anchor_w = config->min_boxes[a * 2];
                    float prior_anchor_h = config->min_boxes[a * 2 + 1];

                    // Calculate prior anchor center and size
                    float prior_center_x = (x + 0.5f) / (float)w;
                    float prior_center_y = (y + 0.5f) / (float)h;
                    float prior_w = prior_anchor_w / (float)input_w;
                    float prior_h = prior_anchor_h / (float)input_h;

                    // Use shared SSD coordinate transformation
                    float bbox_x1_norm, bbox_y1_norm, bbox_x2_norm, bbox_y2_norm;
                    convert_locations_to_boxes(loc0, loc1, loc2, loc3,
                                                 prior_center_x, prior_center_y, prior_w, prior_h,
                                                 &bbox_x1_norm, &bbox_y1_norm, &bbox_x2_norm, &bbox_y2_norm);

                    // Convert to absolute pixel coordinates
                    int bbox_x1 = (int)(bbox_x1_norm * input_w);
                    int bbox_y1 = (int)(bbox_y1_norm * input_h);
                    int bbox_x2 = (int)(bbox_x2_norm * input_w);
                    int bbox_y2 = (int)(bbox_y2_norm * input_h);

                    // clamp image boundaries and Store detection
                    boxes[count].x1 = LT_MAX(0, bbox_x1);
                    boxes[count].y1 = LT_MAX(0, bbox_y1);
                    boxes[count].x2 = LT_MIN(bbox_x2, input_w - 1);
                    boxes[count].y2 = LT_MIN(bbox_y2, input_h - 1);
                    
                    // Calculate area with sanity check for unreasonably large boxes
                    int box_area = (bbox_x2 - bbox_x1) * (bbox_y2 - bbox_y1);
                    if (box_area > (FIXED_CROP_WIDTH * FIXED_CROP_HEIGHT)) {
                        LTLOG_YELLOWALERT(TAG_INT_PIPELINE, 
                                         "Suspiciously large box area: %d pixels (crop is %dx%d=%d)", 
                                         box_area, FIXED_CROP_WIDTH, FIXED_CROP_HEIGHT, 
                                         FIXED_CROP_WIDTH * FIXED_CROP_HEIGHT);
                    }
                    boxes[count].area = box_area;
                    boxes[count].exist = 1;
                    boxes[count].label = label;
                    boxes[count].score = confidence;

                    scores[count].score = confidence;
                    scores[count].originalOrder = count;
                    count++;
                }
            }
        }
    }
    return count;
}

/* Calculates IoU using pure integer arithmetic
 * - Returns IoU as integer percentage (0-100)
 * - Uses standard intersection/union formula
 */
int calculate_bbox_iou_int(Bbox* box1, Bbox* box2) {
    if (!box1 || !box2) {
        return 0;
    }

    // Calculate intersection coordinates
    int max_x = LT_MAX(box1->x1, box2->x1);
    int max_y = LT_MAX(box1->y1, box2->y1);
    int min_x = LT_MIN(box1->x2, box2->x2);
    int min_y = LT_MIN(box1->y2, box2->y2);

    // Early exit: check if boxes can possibly overlap
    if (!boxes_can_overlap(box1, box2)) {
        return 0; // No overlap possible
    }

    // Calculate intersection dimensions
    int intersection_w = LT_MAX(0, min_x - max_x);
    int intersection_h = LT_MAX(0, min_y - max_y);
    int intersection_area = intersection_w * intersection_h;

    // Calculate union area
    int union_area = box1->area + box2->area - intersection_area;

    // Return IoU as percentage (0-100) using integer arithmetic
    return (union_area > 0) ? (intersection_area * IOU_SCALE_FACTOR) / union_area : 0;
}

/* Get label and confidence from quantized scores using hardware lookup tables
 *
 * Performs quantized softmax using pre-computed lookup tables:
 * - Compares background (score0) vs foreground (score1) scores from quantized data
 * - Uses hardware-optimized lookup tables for different quantization scales to convert quantized scores to confidence
 * - Update the values of label (0=background, 1=foreground) and confidence (0-1000)
 */
int get_label_and_confidence_int(signed char score0, signed char score1,
                                 int scale, int* label, int* confidence) {

    if (!label || !confidence) return -1;

    if (score0 > score1) {
        *label = 0;
        *confidence = 0;
        return 0;
    }

    const unsigned int* lookup_table;
    unsigned int clip_max;

    switch (scale) {
        case 32:
            lookup_table = softmax_table_class2_scale32;
            clip_max = 159;
            break;
        case 16:
            lookup_table = softmax_table_class2_scale16;
            clip_max = 79;
            break;
        case 8:
            lookup_table = softmax_table_class2_scale8;
            clip_max = 39;
            break;
        default:
            LTLOG_YELLOWALERT(TAG_LABEL_CONFIDENCE, "Unsupported quantization scale: %d", scale);
            return -1;
    }

    unsigned int index = (unsigned int)LT_MIN((unsigned int)(score1 - score0), clip_max);
    *confidence = lookup_table[index];
    *label = 1;

    return 0;
}


/*******************************************************************************
 * Float Pipeline Implementation
 ******************************************************************************/

/* Forward declarations */
int calculate_total_priors(LayerConfig* layer_configs);
int generate_prior_anchors(PriorAnchor* priors, LayerConfig* layer_configs, int input_w, int input_h);
static int process_float_scores(BLOB_VECTOR_S* output_blobs, LayerConfig* layer_configs, float* score_data, int total_priors);
static int process_float_locations(BLOB_VECTOR_S* output_blobs, LayerConfig* layer_configs, float* location_data);
int convert_locations_to_boxes_wrapper(float* location_data, PriorAnchor* priors, int total_priors);
int filter_detections_by_confidence(float* score_data, float* location_data, Bbox* boxes, OrderScore* scores, int total_priors, int input_w, int input_h, float confidence_threshold);

/* Main floating-point post-processing pipeline for SSD object detection
 *
 * Processes floating-point neural network outputs:
 * - Standard floating-point softmax for score normalization
 * - Precise floating-point coordinate transformations
 * - Modular design with separate functions for each processing stage
 */
int post_process_float(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                       Param_Input_S input_param, Param_Output_S* output_param) {

    // Validate inputs
    if (validate_pipeline_inputs(input_blobs, output_blobs, input_param, output_param) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "validate_pipeline_inputs() failed.");
        return -1;
    }

    // Initialize layer configurations
    LayerConfig layer_configs[NUM_LAYERS];
    if (init_layer_configs(layer_configs) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to initialize SSD layer configurations");
        return -1;
    }

    // Calculate total number of priors
    int total_priors = calculate_total_priors(layer_configs);
    if (total_priors < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to calculate total priors");
        return -1;
    }

    // Allocate working memory
    PriorAnchor* priors = lt_malloc(total_priors * sizeof(PriorAnchor));
    float* score_data = lt_malloc(total_priors * NUM_CLASSES * sizeof(float));
    float* location_data = lt_malloc(total_priors * BBOX_COORDS * sizeof(float));
    Bbox* boxes = lt_malloc(MAX_DETECTIONS * sizeof(Bbox));
    OrderScore* scores = lt_malloc(MAX_DETECTIONS * sizeof(OrderScore));

    if (!priors || !score_data || !location_data || !boxes || !scores) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to allocate working memory");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Extract input dimensions
    int input_w = input_blobs->array_blobs[0].w;
    int input_h = input_blobs->array_blobs[0].h;

    // Generate prior anchors
    if (generate_prior_anchors(priors, layer_configs, input_w, input_h) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to generate prior anchors");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Process score data. score_data will contain the softmax-normalized scores after this call.
    if (process_float_scores(output_blobs, layer_configs, score_data, total_priors) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to process scoress");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Process location data
    if (process_float_locations(output_blobs, layer_configs, location_data) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to process locations");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Convert location regression outputs to absolute corner coordinates
    if (convert_locations_to_boxes_wrapper(location_data, priors, total_priors) < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to convert locations to boxes");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Filter detections by confidence threshold and create final boxes
    int count = filter_detections_by_confidence(score_data, location_data, boxes, scores, total_priors,
                                                input_w, input_h, input_param.confidence_threshold);
    if (count < 0) {
        LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE, "Failed to filter detections by confidence");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Apply non-maximum suppression using consolidated function (float mode)
    count = apply_nms(boxes, scores, count, input_param.iou_threshold, false);
    if (count < 0) {
        LTLOG_YELLOWALERT(TAG_APPLY_NMS, "NMS failed");
        cleanup_memory(5, boxes, scores, score_data, location_data, priors);
        return -1;
    }

    // Populate output structure
    populate_output(boxes, count, output_param);

    // Cleanup
    cleanup_memory(5, boxes, scores, score_data, location_data, priors);

    // LTLOG_INFO("Float post-processing completed successfully");
    return 0;
}

/* Calculate total number of prior anchors across all SSD layers
 *
 * Computes the total number of anchor boxes for memory allocation:
 * - Sums feature_w * feature_h * anchor_count for each layer
 * - Used for allocating prior anchors and data arrays in float pipeline
 */
int calculate_total_priors(LayerConfig* layer_configs) {
    if (!layer_configs) return -1;

    int total_priors = 0;
    for (int i = 0; i < NUM_LAYERS; i++) {
        total_priors += layer_configs[i].feature_w * layer_configs[i].feature_h *
                        layer_configs[i].anchor_count;
    }
    return total_priors;
}

/* Generate prior anchor boxes for all SSD detection layers
 *
 * Creates default anchor boxes at each feature map location:
 * - Iterates through all detection layers and feature map positions
 * - Generates anchor boxes with predefined aspect ratios and sizes
 * - Normalizes anchor coordinates to [0,1] range relative to input image
 */
int generate_prior_anchors(PriorAnchor* priors, LayerConfig* layer_configs,
                          int input_w, int input_h) {

    if (!priors || !layer_configs) return -1;

    int count = 0;

    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        LayerConfig* config = &layer_configs[layer];
        int scale_w = config->feature_w;
        int scale_h = config->feature_h;

        for (int y = 0; y < scale_h; y++) {
            for (int x = 0; x < scale_w; x++) {
                for (int a = 0; a < config->anchor_count; a++) {
                    float center_x = (x + 0.5f) / scale_w;
                    float center_y = (y + 0.5f) / scale_h;
                    float width = (float)config->min_boxes[a * 2] / input_w;
                    float height = (float)config->min_boxes[a * 2 + 1] / input_h;

                    priors[count].center_x = center_x;
                    priors[count].center_y = center_y;
                    priors[count].width = width;
                    priors[count].height = height;

                    count++;
                }
            }
        }
    }

    return count;
}

/* Aggregate and process score data from all detection layers (float pipeline)
 *
 * Processes floating-point classification scores:
 * - Converts CHW format to HWC for processing
 * - Aggregates scores from all layers into unified array
 * - Applies softmax normalization
 */
int process_float_scores(BLOB_VECTOR_S* output_blobs, LayerConfig* layer_configs,
                         float* score_data, int total_priors) {

    if (!output_blobs || !layer_configs || !score_data) return -1;

    int offset = 0;

    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        INPUT_OUTPUT_BLOB_INFO_S blob = output_blobs->array_blobs[layer];
        LayerConfig* config = &layer_configs[layer];

        int w = config->feature_w;
        int h = config->feature_h;
        int c = blob.c;  // Should be anchor_count * num_classes

        // Convert CHW to HWC format and copy to score_data
        convert_chw_to_hwc_float((float*)blob.VirAddr, score_data + offset, w, h, c);
        offset += w * h * c;
    }

    // Apply softmax per line (per detection)
    return softmax_per_line(score_data, total_priors, NUM_CLASSES);
}

/* Aggregate and process location data from all detection layers (float pipeline)
 *
 * Processes floating-point bounding box regression outputs:
 * - Converts CHW format to HWC for processing
 * - Aggregates location regression data from all layers
 */
int process_float_locations(BLOB_VECTOR_S* output_blobs,
                            LayerConfig* layer_configs, float* location_data) {

    if (!output_blobs || !layer_configs || !location_data) return -1;

    int offset = 0;

    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        INPUT_OUTPUT_BLOB_INFO_S blob = output_blobs->array_blobs[layer + 4];
        LayerConfig* config = &layer_configs[layer];

        int w = config->feature_w;
        int h = config->feature_h;
        int c = blob.c;  // Should be anchor_count * BBOX_COORDS

        // Convert CHW to HWC format and copy to location_data
        convert_chw_to_hwc_float((float*)blob.VirAddr, location_data + offset, w, h, c);
        offset += w * h * c;
    }

    return 0;
}

/* Convert SSD location regression outputs to absolute corner coordinates
 *
 * Transforms neural network regression outputs to bounding box coordinates:
 * - Applies SSD-style coordinate decoding using center/size variance
 * - Converts from center-based to corner coordinates using exponential scaling
 * - Transforms relative coordinates to absolute pixel coordinates
 *
 * Wrapper function used to share logic between integer and float pipelines
 */
int convert_locations_to_boxes_wrapper(float* location_data, PriorAnchor* priors, int total_priors) {
    if (!location_data || !priors) return -1;

    for (int i = 0; i < total_priors; i++) {
        float* loc = &location_data[i * BBOX_COORDS];
        PriorAnchor* prior = &priors[i];

        // Use shared SSD coordinate transformation
        convert_locations_to_boxes(loc[0], loc[1], loc[2], loc[3],
                                   prior->center_x, prior->center_y, prior->width, prior->height,
                                   &location_data[i * BBOX_COORDS], &location_data[i * BBOX_COORDS + 1],
                                   &location_data[i * BBOX_COORDS + 2], &location_data[i * BBOX_COORDS + 3]);
    }

    return 0;
}

/* Filter detections by confidence threshold and create final bounding boxes
 *
 * Creates final detection candidates:
 * - Applies confidence threshold filtering
 * - Converts normalized coordinates to absolute pixel coordinates
 * - Clamps coordinates to image boundaries
 */
int filter_detections_by_confidence(float* score_data, float* location_data,
                                    Bbox* boxes, OrderScore* scores, int total_priors,
                                    int input_w, int input_h, float confidence_threshold) {
    if (!score_data || !location_data || !boxes || !scores) {
        return -1;
    }

    int count = 0;

    for (int i = 0; i < total_priors; i++) {
        float confidence = score_data[i * NUM_CLASSES + 1]; // Class 1 score

        if (confidence > confidence_threshold) {
            if (count >= MAX_DETECTIONS) {
                break;
            }

            boxes[count].x1 = LT_MAX(0, (int)(location_data[i * BBOX_COORDS] * input_w));
            boxes[count].y1 = LT_MAX(0, (int)(location_data[i * BBOX_COORDS + 1] * input_h));
            boxes[count].x2 = LT_MIN((int)(location_data[i * BBOX_COORDS + 2] * input_w), input_w - 1);
            boxes[count].y2 = LT_MIN((int)(location_data[i * BBOX_COORDS + 3] * input_h), input_h - 1);

            // Calculate area with sanity check for unreasonably large boxes
            int box_area = (boxes[count].x2 - boxes[count].x1) * (boxes[count].y2 - boxes[count].y1);
            if (box_area > (FIXED_CROP_WIDTH * FIXED_CROP_HEIGHT)) {
                LTLOG_YELLOWALERT(TAG_FLOAT_PIPELINE,
                                 "Suspiciously large box area: %d pixels (crop is %dx%d=%d)",
                                 box_area, FIXED_CROP_WIDTH, FIXED_CROP_HEIGHT,
                                 FIXED_CROP_WIDTH * FIXED_CROP_HEIGHT);
            }
            boxes[count].area = box_area;
            boxes[count].exist = 1;
            boxes[count].label = 1;
            boxes[count].score = (int)(confidence * CONFIDENCE_SCALE_FACTOR);

            scores[count].score = (int)(confidence * CONFIDENCE_SCALE_FACTOR);
            scores[count].originalOrder = count;

            count++;
        }
    }

    return count;
}


/*******************************************************************************
 * Shared Utility Functions
 ******************************************************************************/

/* Validate input parameters for both integer and float pipelines
 *
 * Performs comprehensive validation:
 * - Checks for null pointers in blob vectors and output parameter
 * - Validates expected number of input/output blobs (1 input, 8 outputs)
 * - Validates threshold ranges
 */
static int validate_pipeline_inputs(BLOB_VECTOR_S* input_blobs, BLOB_VECTOR_S* output_blobs,
                                    Param_Input_S input_param, Param_Output_S* output_param) {

    if (!input_blobs || !output_blobs) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "NULL blob vectors provided");
        return -1;
    }

    if (!output_param) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "NULL output parameter provided");
        return -1;
    }

    if (!input_blobs->array_blobs) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "NULL input array_blobs pointer");
        return -1;
    }

    if (!output_blobs->array_blobs) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "NULL output array_blobs pointer");
        return -1;
    }

    if (input_blobs->blob_cnt != 1) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "Expected 1 input blob, got %d", input_blobs->blob_cnt);
        return -1;
    }

    if (output_blobs->blob_cnt != 8) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "Expected 8 output blobs, got %d", output_blobs->blob_cnt);
        return -1;
    }

    if (input_param.confidence_threshold < 0.0f || input_param.confidence_threshold >= 1.0f) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "Invalid confidence threshold: %f", input_param.confidence_threshold);
        return -1;
    }

    if (input_param.iou_threshold < 0.0f || input_param.iou_threshold > 1.0f) {
        LTLOG_YELLOWALERT(TAG_VALIDATE_INPUTS, "Invalid IoU threshold: %f", input_param.iou_threshold);
        return -1;
    }

    return 0;
}

/* Apply Non-Maximum Suppression with selectable arithmetic mode
 *
 * Removes overlapping detections using either integer or floating-point IoU:
 * - Sorts detections by confidence score (ascending order)
 * - Processes from highest to lowest confidence
 * - Suppresses overlapping boxes using selected IoU calculation method
 * - Returns the number of kept detections, or -1 on error
 *
 * @param use_int: true for integer arithmetic (hardware optimized), false for float precision
 */
int apply_nms(Bbox* boxes, OrderScore* scores, int count, float iou_threshold, bool use_int) {
    if (!boxes || !scores || count <= 0) {
        return -1;
    }

    // Just 1 detection and nothing to sort
    if (count < 2) {
        return count;
    }

    // Sort by confidence (ascending order)
    if (bubble_sort_detections(scores, count) < 0) {
        return -1;
    }

    int* kept_indices = lt_malloc(count * sizeof(int));
    if (!kept_indices) {
        LTLOG_YELLOWALERT(TAG_APPLY_NMS, "Failed to allocate NMS working memory");
        return -1;
    }

    int kept_count = 0;

    // Convert threshold for integer mode (percentage 0-100)
    int iou_threshold_int = use_int ? (int)(iou_threshold * IOU_SCALE_FACTOR) : 0;

    // Process from highest to lowest confidence NMS algorithm
    for (int i = count - 1; i >= 0; i--) {
        int current_idx = scores[i].originalOrder;

        if (current_idx < 0 || !boxes[current_idx].exist) {
            continue;
        }

        // Keep this detection
        kept_indices[kept_count++] = current_idx;
        boxes[current_idx].exist = 0; // Mark as processed

        // Suppress overlapping detections
        for (int j = 0; j < count; j++) {
            if (!boxes[j].exist || j == current_idx) {
                continue;
            }

            // Calculate IoU using selected method (Integer or Float) and check threshold
            bool should_suppress = false;
            if (use_int) {
                // Use integer IoU calculation
                int iou_percentage = calculate_bbox_iou_int(&boxes[j], &boxes[current_idx]);
                should_suppress = (iou_percentage > iou_threshold_int);
            } else {
                // Use floating-point IoU calculation
                float iou = calculate_bbox_iou(&boxes[j], &boxes[current_idx]);
                should_suppress = (iou > iou_threshold);
            }

            if (should_suppress) {
                boxes[j].exist = 0;

                // Mark corresponding score as invalid
                for (int k = 0; k < count; k++) {
                    if (scores[k].originalOrder == j) {
                        scores[k].originalOrder = -1;
                        break;
                    }
                }
            }
        }
    }

    // Restore kept boxes and compact array
    for (int i = 0; i < kept_count; i++) {
        boxes[kept_indices[i]].exist = 1;
        if (i != kept_indices[i]) {
            boxes[i] = boxes[kept_indices[i]];
        }
    }

    lt_free(kept_indices);
    return kept_count;
}

/* Populate output structure with final detection results after NMS
 *
 * Converts internal detection format to API output format:
 * - Copies bounding box coordinates and confidence scores
 * - Respects maximum output limit
 */
static void populate_output(Bbox* boxes, int count, Param_Output_S* output_param) {

    if (!boxes || !output_param) return;

    output_param->obj_num = LT_MIN(count, MAX_NUM_OF_BOX);

    for (int i = 0; i < output_param->obj_num; i++) {
        output_param->obj_info[i].box.x1 = boxes[i].x1;
        output_param->obj_info[i].box.y1 = boxes[i].y1;
        output_param->obj_info[i].box.x2 = boxes[i].x2;
        output_param->obj_info[i].box.y2 = boxes[i].y2;
        output_param->obj_info[i].confidence = boxes[i].score;
        output_param->obj_info[i].label = boxes[i].label;
    }
}

/* Convert CHW (Channel-Height-Width) to HWC (Height-Width-Channel) format
 *
 * Rearranges neural network output tensor format:
 * - Converts from CHW (standard NN format) to HWC (processing format)
 * - Handles in-place conversion using temporary buffer when needed
 */
void convert_chw_to_hwc_float(float* input, float* output,
                             int w, int h, int channels) {
    if (!input || !output) return;

    if (input == output) {
        float* temp = lt_malloc(w * h * channels * sizeof(float));
        if (!temp) return;

        for (int c = 0; c < channels; c++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    temp[y * w * channels + x * channels + c] =
                        input[c * h * w + y * w + x];
                }
            }
        }
        lt_memcpy(output, temp, w * h * channels * sizeof(float));
        lt_free(temp);

    } else {
        for (int c = 0; c < channels; c++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    output[y * w * channels + x * channels + c] =
                        input[c * h * w + y * w + x];
                }
            }
        }
    }
}

/* Sort detection scores in ascending order using bubble sort
 *
 * Sorts OrderScore array by confidence scores for NMS processing:
 * - Uses bubble sort (suitable for small detection counts)
 * - Sorts in ascending order (lowest confidence first)
 * - NMS processes from highest confidence (end of array) backwards
 */
int bubble_sort_detections(OrderScore* scores, int count) {
    if (!scores || count <= 0) return -1;

    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (scores[j].score > scores[j + 1].score) {
                OrderScore temp = scores[j];
                scores[j] = scores[j + 1];
                scores[j + 1] = temp;
            }
        }
    }
    return 0;
}

/* Calculate Intersection over Union (IoU) between two bounding boxes
 *
 * Computes standard IoU metric for NMS overlap detection:
 * - Calculates intersection and union areas
 * - Returns IoU ratio: intersection_area / union_area
 */
float calculate_bbox_iou(Bbox* box1, Bbox* box2) {
    if (!box1 || !box2) return 0.0f;

    float max_x = LT_MAX(box1->x1, box2->x1);
    float max_y = LT_MAX(box1->y1, box2->y1);
    float min_x = LT_MIN(box1->x2, box2->x2);
    float min_y = LT_MIN(box1->y2, box2->y2);

    float intersection_w = LT_MAX(0.0f, min_x - max_x);
    float intersection_h = LT_MAX(0.0f, min_y - max_y);
    float intersection_area = intersection_w * intersection_h;
    float union_area = box1->area + box2->area - intersection_area;

    return (union_area > 0.0f) ? (intersection_area / union_area) : 0.0f;
}

/* Shared SSD coordinate transformation core logic between integer and float pipelines
 *
 * Applies SSD-style coordinate decoding using center/size variance:
 * - Transforms regression outputs to center coordinates and dimensions
 * - Uses exponential scaling for size adjustments
 * - Converts to corner coordinates (x1, y1, x2, y2)
 */
static void convert_locations_to_boxes(float loc0, float loc1, float loc2, float loc3,
                                           float prior_center_x, float prior_center_y,
                                           float prior_w, float prior_h,
                                           float* bbox_x1, float* bbox_y1,
                                           float* bbox_x2, float* bbox_y2) {

    // Apply SSD coordinate transformation
    float det_center_x = loc0 * center_variance * prior_w + prior_center_x;
    float det_center_y = loc1 * center_variance * prior_h + prior_center_y;
    float det_w = fast_exp(loc2 * size_variance) * prior_w;
    float det_h = fast_exp(loc3 * size_variance) * prior_h;

    // Convert to corner coordinates
    *bbox_x1 = det_center_x - det_w / 2;
    *bbox_y1 = det_center_y - det_h / 2;
    *bbox_x2 = det_center_x + det_w / 2;
    *bbox_y2 = det_center_y + det_h / 2;
}

/* Cleanup variable number of allocated memory pointers
 *
 * Safely deallocates all working memory using variadic arguments:
 * - First argument is the count of pointers to free
 * - Followed by the void* pointers to free
 * - Handles null pointer checks before deallocation (lt_free is safe with NULL)
 * - Centralizes memory management for better maintainability
 *
 * Usage: cleanup_memory(5, boxes, scores, score_data, location_data, priors);
 */
void cleanup_memory(int count, ...) {
    lt_va_list args;
    lt_va_start(args, count);

    for (int i = 0; i < count; i++) {
        void* ptr = lt_va_arg(args, void*);
        lt_free(ptr);  // lt_free handles NULL pointers safely
    }

    lt_va_end(args);
}
