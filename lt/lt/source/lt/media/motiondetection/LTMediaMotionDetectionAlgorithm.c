/*******************************************************************************
 * LT Motion Detection Library ROI Calculation Algorithms.
 *
 * Specifies the algorithms used by the main motion detection library source file
 * as part of the motion event state machine.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/motiondetection/LTMediaMotionDetection.h>
#include <lt/system/settings/LTSystemSettings.h>
#include "LTMediaMotionDetection_Private.h"

DEFINE_LTLOG_SECTION("motion.algo");

// #define P(...) LTLOG_DEBUG(__VA_ARGS__)
#define P(...)
// #define PV(...) LTLOG_VERBOSE(__VA_ARGS__)
#define PV(...)

/** To simulate non-BFS motion detection as a first step to get a motion/no-motion signal, we perform BFS with the
 *  immediate surrounding moxels (including diagonals), i.e.:
 *     1 2 3
 *     4 X 5
 *     6 7 8
 */
#define BFS_DIRECT_NEIGHBOURS 8

/* Forward declarations for structs. */
typedef struct LTMediaMotionDetectionAlgorithm_Data LTMediaMotionDetectionAlgorithm_Data;

/** Callback procedure type for the motion detection ROI calculation algorithm.
 *
 *  A function of this type is used in the motion detection library for outputting motion ROIs. Depending on the
 *  algorithm type, a single ROI or multiple ROIs may be given as output.
 *
 *  @param params - pointer to the list of parameters to be used by the algorithm
 *  @param data - pointer to the list of buffers and private variables to be used during algorithm operation
 *  @param baseMotionOutput - pointer to an input buffer containing the raw moxel grid from the camera video
 *         drivers
 *
 *  @return - whether or not the frame has motion
 */
typedef bool (LTMediaMotionDetectionAlgorithm_ROICalculationProc)(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                                                  LTMediaMotionDetectionAlgorithm_Data *data,
                                                                  LTMediaData *baseMotionOutput);


/** Callback procedure type for the downsampling method used in the motion detection algorithm.
 *
 *  A function of this type is used as part of the current motion detection algorithm for outputting motion ROIs.
 *  As well as downsampling its input (the raw motion output from the camera drivers), applies thresholding to
 *  determine which moxels contain enough motion and counts the number of these moxels in the full frame to
 *  determine an upper bound on the number of labels.
 *
 *  @param params - pointer to the list of parameters to be used by the algorithm
 *  @param data - pointer to the list of buffers to be used during algorithm operation
 *  @param baseMotionOutput - pointer to an input buffer containing the raw moxel grid from the camera video
 *         drivers
 *
 *  @return - the maximum label (the number of moxels after downsampling)
 */
typedef u32 (LTMediaMotionDetectionAlgorithm_DownsamplingProc)(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                                               LTMediaMotionDetectionAlgorithm_Data *data,
                                                               LTMediaData *baseMotionOutput);

/** A struct for holding all of the private data used by the motion detection ROI calculation algorithm during
 *  operation, including buffers as well as some private variables stored for convenience. */
typedef struct LTMediaMotionDetectionAlgorithm_Data {
    /* Buffers for general results gathered during algorithm operation. */
    LTMediaMotionDetection_Metadata motionMetadata; /**< motionMetadata - pointer to motion event metadata, containing solitary 'regions' member which is the ROI array set by the algorithm as output. The total number of ROIs held is determined by the constant MAX_NUM_MOTION_REGIONS. */

    /* Buffers used only by multi-ROI calculations. */
    u32 *motionLabels;  /**< Pointer to a buffer to keep track of the labels given to each moxel during algorithm execution. */
    bool *motionBlockDetection; /**< Pointer to a buffer to keep track of which moxels satisfy the criteria for motion (exceed the motion threshold and are inside the motion zones). This is filled out during the first pass over the moxel grid. */
    u32 *motionIslandSizes; /**< Pointer to a buffer to record the number of moxels in each detected motion island. Only the islands which exceed the minimum required area are considered in the final ranking. */
    u32 *motionEdges; /**< Pointer to buffer to keep track of edges in the connection of islands. For example if motionEdges[i] == j, island (node) i is connected to island (node) j. We only update edges if j < i, since we only care about connecting labels with a larger value to a smaller one. Edges are initialised to LT_U32_MAX rather than 0, for 0 is a valid node. */
    u32 *biggestIslandSizes; /**< Pointer to a buffer containing the sizes of the largest motion islands detected in the last processed frame. The total number of island sizes contained is determined by MAX_NUM_MOTION_REGIONS. */

    /* Buffers used by the motion heatmap feature. */
    float *moxelHeatmap; /**< Pointer to a buffer containing current heatmap values for each moxel in the frame. */
    LTMediaMotionDetection_HeatmapData *heatmapVideoData; /**< Pointer to a buffer containing a modified version of the heatmap values which can be used to stream a visualisation of the heatmap to the camviewer for debug purposes. */

    /* Description Needed */
    u8 *motionTemporalCounter; /**< Description Needed */
    u32 *bfsIndexQueue; /**< Description Needed */

    /* Additional private variables stored for convenience. */
    LTMediaMotionDetectionAlgorithm_DownsamplingProc *pDownsamplingProc; /**< The downsampling function used by the algorithm */
    LTMediaMotionDetectionAlgorithm_ROICalculationProc *pROICalculationProc; /**< The function by the algorithm to check the downsampled motion data for overall motion by calculating output ROIs. */
    u32 downsampledWidth; /**< The number of moxels in each row of downsampled motion data. */
    u32 downsampledHeight; /**< The number of moxels in each column of downsampled motion data. */
    u32 downsampledDataLen; /**< The length of downsampled data; equal to downsampledWidth x downsampledHeight. */
    u8 bfsNeighbours; /**< The number of neighbours around the current moxel to search for connected components during BFS, derived from the Manhattan distance using formula N = 2M^2 + 2M and stored here for convenience. */
} LTMediaMotionDetectionAlgorithm_Data;

typedef_LTObjectImpl(LTMediaMotionDetectionAlgorithm, LTMediaMotionDetectionAlgorithmImpl) {
    LTMediaMotionDetectionAlgorithm_Parameters params;  /**< Publicly configurable algorithm hyperparameters, set in Init(). */
    LTMediaMotionDetectionAlgorithm_Data data;  /**< Private algorithm data, including buffers and some internal parameters. */
} LTOBJECT_API;

/** BFS Neighbour Offsets for Manhattan distance <= 5. */
static const s8 s_bfsHorizontalDistances[60] = {
    1, 0, -1, 0,  // manhattan distance 1
    1, -1, -1, 1, 2, 0, -2, 0, // manhattan distance 2
    3, 2, 1, 0, -1, -2, -3, -2, -1, 0, 1, 2,  // manhattan distance 3
    4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1, 0, 1, 2, 3,  // manhattan distance 4
    5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, // manhattan distance 5
};
static const s8 s_bfsVerticalDistances[60] = {
    0, 1, 0, -1,  // manhattan distance 1
    1, 1, -1, -1, 0, 2, 0, -2, // manhattan distance 2
    0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1,  // manhattan distance 3
    0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1,  // manhattan distance 4
    0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -4, -3, -2, -1,  // manhattan distance 5
};
static const u8 s_minimumBFSDistance = 1;
static const u8 s_maximumBFSDistance = 5;

static bool ApplyHeatmapFilter(LTMediaMotionDetectionAlgorithm_Parameters *params,
                               LTMediaMotionDetectionAlgorithm_Data *data,
                               bool triggered,
                               u32 downsampledIdx) {
    float heatmapValue = data->moxelHeatmap[downsampledIdx];

    if (triggered) {
        heatmapValue = LT_MIN((float)(heatmapValue + params->heatmapParameters.gain), params->heatmapParameters.cap);
    } else {
        heatmapValue = LT_MAX(0.0f, (float)(heatmapValue - params->heatmapParameters.drop));
    }

    data->moxelHeatmap[downsampledIdx] = heatmapValue;
    data->heatmapVideoData->address[downsampledIdx] = (u8)heatmapValue;

    if (heatmapValue >= params->heatmapParameters.threshold) {
        triggered = false;
    }

    return triggered;
}

static bool SingleROICalculationProc(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                     LTMediaMotionDetectionAlgorithm_Data *data,
                                     LTMediaData *baseMotionOutput) {
    /* Takes the base motion output from the media motion source and processes it to decide if motion has occurred.
     * If the motion in any moxel (motion pixel) exceeds the threshold then the frame has motion, return true.
     * Otherwise, return false.
     * We also calculate a single output motion ROI which is used for OSD bounding box coordinates. */

    if (!data->motionMetadata.regions) { return false; }

    bool isMotionFrame = false;

    data->motionMetadata.regions->numRegions = 1;

    /* Calculate tightest rectangle around all moxels containing motion.
     * The rectangle's coordinates will be the min/max x and y values we see among all the moxels inside motion zones
     * containing motion over the threshold.
     * Note: x and y are indices of moxels, not image pixel coordinates. */
    LTMediaMotionDetection_Region *outputROI = &data->motionMetadata.regions->region[0];

    /* Starting values to be overridden by the first moxel containing motion we find. */
    outputROI->x1 = S.horizontalMoxels;
    outputROI->y1 = S.verticalMoxels;
    outputROI->x2 = 0;
    outputROI->y2 = 0;

    for (u32 i = 0; i < baseMotionOutput->nDataLen; i++) {
        bool inZone = !LTMediaMotionDetection_IsMotionZoneEnabled(); // If motion zone is disabled assume all cells are in zone.
        if (!inZone) {
            LTMediaMotionDetection_MotionZoneBitmap *pMotionZoneBitmap;
            GetMotionZoneBitmap(&pMotionZoneBitmap);
            if (pMotionZoneBitmap) {
                u32 wordIndex = i / pMotionZoneBitmap->nBitmapWordLen;
                u32 bitIndex = i % pMotionZoneBitmap->nBitmapWordLen;
                inZone = (pMotionZoneBitmap->pData[wordIndex] & (1 << bitIndex)) != 0;
            } else {
                LTLOG_YELLOWALERT("check.frame.nozone", "Motion zones enabled but no motion zone available. Treating motion zones as disabled.");
                inZone = true;
            }
        }

        if (inZone && (baseMotionOutput->pData[i] >= params->differenceThreshold)) {
            u32 y = i / S.horizontalMoxels;
            u32 x = i % S.horizontalMoxels;

            if (x < outputROI->x1) { outputROI->x1 = x; }
            if (y < outputROI->y1) { outputROI->y1 = y; }
            if (x > outputROI->x2) { outputROI->x2 = x; }
            if (y > outputROI->y2) { outputROI->y2 = y; }

            isMotionFrame = true;
        }
    }
    return isMotionFrame;
}

static u32 DownsamplingMaxPooling(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                  LTMediaMotionDetectionAlgorithm_Data *data,
                                  LTMediaData *baseMotionOutput) {
    /* Downsample and apply max pooling, then check if the max value is above DifferenceThreshold for motion.
       Arrays s_motionBlockDetection and s_motionLabels are set in the process.
       Return max label (ie the number of moxels after downsampling)
       Note: Max pooling, due to its more aggressive tendency in merging moxels compare to average pooling, produces
       larger bounding boxes and looks better in general. */
    u32 label = 0;
    u32 downsampledIdx = 0;

    for (u32 y = 0; y < S.verticalMoxels; y += params->downsamplingScaleFactor) {  // row-wise
        for (u32 x = 0; x < S.horizontalMoxels; x += params->downsamplingScaleFactor) {  // column-wise
            bool inZone = !LTMediaMotionDetection_IsMotionZoneEnabled(); // If motion zone is disabled assume all cells are in zone.
            u8 maximumMoxelValue = 0;
            LTMediaMotionDetection_MotionZoneBitmap *pMotionZoneBitmap;
            GetMotionZoneBitmap(&pMotionZoneBitmap);

            for (u32 p = 0; p < params->downsamplingScaleFactor; p++) {  // row-wise
                // Check if downsampled moxel has motion and boundary condition:
                if (y + p >= S.verticalMoxels) {
                    break;
                }
                for (u32 q = 0; q < params->downsamplingScaleFactor; q++) {  // column-wise
                    // Check if downsampled moxel has motion and boundary condition:
                    if (x + q >= S.horizontalMoxels) {
                        break;
                    }
                    u32 i = (y + p) * S.horizontalMoxels + (x + q);
                    if (LTMediaMotionDetection_IsMotionZoneEnabled() && !inZone) {
                        // if any one of the moxels in the downsampled neighbourhood is in zone, whole downsampled neighbourhood is in zone
                        u32 wordIndex = i / pMotionZoneBitmap->nBitmapWordLen;
                        u32 bitIndex = i % pMotionZoneBitmap->nBitmapWordLen;
                        inZone = (pMotionZoneBitmap->pData[wordIndex] & (1 << bitIndex)) != 0;
                    }

                    if (baseMotionOutput->pData[i] > maximumMoxelValue) {
                        maximumMoxelValue = baseMotionOutput->pData[i];
                    }

                    if (inZone) {
                        /* Update pre-downsampled energy histogram. */
                        AddMoxelToEnergyHistogram(baseMotionOutput->pData[i], true);
                    }
                }
            }

            bool triggered = maximumMoxelValue >= params->differenceThreshold;

            if (params->heatmapParameters.bEnabled) {
                triggered = ApplyHeatmapFilter(params, data, triggered, downsampledIdx);
            }
            if (inZone && triggered) {
                data->motionBlockDetection[downsampledIdx] = true;
                data->motionLabels[downsampledIdx] = label++;
            }
            downsampledIdx++;

            /* Update post-downsampled energy histogram. */
            AddMoxelToEnergyHistogram(maximumMoxelValue, false);
        }
    }
    LT_ASSERT(downsampledIdx == data->downsampledDataLen);
    return label;
}

static u32 DownsamplingAvgPooling(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                  LTMediaMotionDetectionAlgorithm_Data *data,
                                  LTMediaData *baseMotionOutput) {
    /* Same as DownsamplingMaxPooling, except we use average pooling during downsampling instead. */
    u32 label = 0;
    u32 downsampledIdx = 0;

    for (u32 y = 0; y < S.verticalMoxels; y += params->downsamplingScaleFactor) {  // row-wise
        for (u32 x = 0; x < S.horizontalMoxels; x += params->downsamplingScaleFactor) {  // column-wise
            bool inZone = !LTMediaMotionDetection_IsMotionZoneEnabled(); // If motion zone is disabled assume all cells are in zone.
            u32 moxelSum = 0;
            u32 count = 0;
            LTMediaMotionDetection_MotionZoneBitmap *pMotionZoneBitmap;
            GetMotionZoneBitmap(&pMotionZoneBitmap);

            for (u32 p = 0; p < params->downsamplingScaleFactor; p++) {  //row-wise
                // Check for boundary condition:
                if (y + p >= S.verticalMoxels) {
                    break;
                }
                for (u32 q = 0; q < params->downsamplingScaleFactor; q++) {  //column-wise
                    // Check for boundary condition:
                    if (x + q >= S.horizontalMoxels){
                        break;
                    }
                    u32 i = (y + p) * S.horizontalMoxels + (x + q);
                    if (LTMediaMotionDetection_IsMotionZoneEnabled() && !inZone) {
                        // if any one of the moxels in the downsampled neighbourhood is in zone, whole downsampled neighbourhood is in zone
                        u32 wordIndex = i / pMotionZoneBitmap->nBitmapWordLen;
                        u32 bitIndex = i % pMotionZoneBitmap->nBitmapWordLen;
                        inZone = (pMotionZoneBitmap->pData[wordIndex] & (1 << bitIndex)) != 0;
                    }
                    moxelSum += baseMotionOutput->pData[i];
                    count += 1;

                    if (inZone) {
                        /* Update pre-downsampled energy histogram. */
                        AddMoxelToEnergyHistogram(baseMotionOutput->pData[i], true);
                    }
                }
            }
            u32 moxelAverage = moxelSum / count;
            bool triggered = moxelAverage >= params->differenceThreshold;

            if (params->heatmapParameters.bEnabled) {
                triggered = ApplyHeatmapFilter(params, data, triggered, downsampledIdx);
            }

            if (inZone && triggered) {
                data->motionBlockDetection[downsampledIdx] = true;
                data->motionLabels[downsampledIdx] = label++;
            }
            downsampledIdx++;

            /* Update post-downsampled energy histogram. */
            AddMoxelToEnergyHistogram(moxelAverage, false);
        }
    }

    LT_ASSERT(downsampledIdx == data->downsampledDataLen);
    return label;
}

static u32 FindMinLabel(LTMediaMotionDetectionAlgorithm_Data *data,
                        u32 x0,
                        u32 y0) {
    /*  Given a current moxel at (x, y), find the minimum label of the moxels in the left and mid neighbourhood of
     *  current moxel. Pictorially, if the current moxel is N0, we find the minimum labels in N0 to N5
     *  (with whether motion is triggered for the moxel and the boundary condition considered).
     *       N1  N2  --
     *       N3  N0  --
     *       N4  N5  --
     *  Return min_label in the neighbourhood.
     *  Note: We don't need to check all 8 neighbours, because by symmetry and by using motionEdges,
     *  it is enough to capture all connected islands after 2 passes of the moxel array.
     */
    u32 min_label = data->motionLabels[y0 * data->downsampledWidth + x0];
    s64 y = (s64)y0;
    s64 x = (s64)x0;

    for (s64 x_offset = 0; x_offset >= -1; x_offset--) {
        // Checking boundary condition column-wise
        if (x + x_offset < 0) { break; }
        for (s64 y_offset = -1; y_offset <= 1; y_offset++) {
            if (y_offset == 0 && x_offset == 0) { continue; }
            if (y + y_offset >= 0 && y + y_offset < (s64)data->downsampledHeight) { // Checking boundary condition row-wise
                u32 neighbour_idx = (u32)((y + y_offset) * (s64)data->downsampledWidth + (x + x_offset));
                u32 neighbour_label = data->motionLabels[neighbour_idx];
                if (neighbour_label != LT_U32_MAX && neighbour_label < min_label) {
                    min_label = neighbour_label;
                }
            }
        }
    }

    return min_label;
}

static void SetMinLabel(LTMediaMotionDetectionAlgorithm_Data *data, u32 x0, u32 y0) {
    /*  First run FindMinLabel(), then backfill all labels N0 to N5 (see comment in FindMinLabel()) to the minimum
     *  label found. Also update motionEdges for the connected islands. */
    u32 min_label = FindMinLabel(data, x0, y0);
    s64 y = (s64) y0;
    s64 x = (s64) x0;

    for (s64 x_offset = 0; x_offset >= -1; x_offset--) {
        // Checking boundary condition column-wise
        if (x + x_offset < 0) { break; }
        for (s64 y_offset = -1; y_offset <= 1; y_offset++) {
            if (y + y_offset >= 0 && y + y_offset < (s64) data->downsampledHeight) {  // Checking boundary condition row-wise
                u32 neighbour_idx = (u32) ((y + y_offset) * (s64) data->downsampledWidth + (x + x_offset));
                u32 neighbour_label = data->motionLabels[neighbour_idx];
                if (neighbour_label != LT_U32_MAX) {
                    if (min_label < data->motionEdges[neighbour_label]) {
                        data->motionEdges[neighbour_label] = min_label;  // Update edges for connected islands.
                    }
                    data->motionLabels[neighbour_idx] = min_label;
                }
            }
        }
    }
}

static bool UpdateIslands(LTMediaMotionDetectionAlgorithm_Parameters *params,
                          LTMediaMotionDetectionAlgorithm_Data *data,
                          u32 max_label) {
    /* This function is used in CheckFrameForMultipleMotion(...).
     * Note that islands can be thought of as connected components, and a node is just a single island.
     * It updates motionLabels for island identification in a number of stages:
     * 1. First pass:
     *      1a. find locally connected islands and reduce the total number of islands (this speed up step 2);
     *      1b. update motionEdges;
     * 2. Iteratively update edges (motionEdges) to further reduce the number of islands missed out by the first pass;
     * 3. Update motionLabels with the correct island labels (minimum labels).
     */
    bool bMotionDetected = false;

    // Initialise motionEdges for each island (node) to point to itself, ie motionEdges[i] = i
    for (u32 i = 0; i < max_label; i++) {
        data->motionEdges[i] = i;
    }

    // First pass - find locally connected islands, consolidate and reduce the number of islands
    for (u32 y = 0; y < data->downsampledHeight; y++) {
        for (u32 x = 0; x < data->downsampledWidth; x++) {
            u32 i = y * data->downsampledWidth + x;
            if (data->motionBlockDetection[i]) {
                SetMinLabel(data, x, y);
            }
        }
    }

    // Iteratively update motionEdges to reduce further the number of islands
    // Break while loop when we reach node 0, or when no further changes are possible for a given node.
    for (u32 i = 0; i < max_label; i++) {
        u32 current_node = i;
        u32 next_node;
        while (true) {
            next_node = data->motionEdges[current_node];
            if (next_node == 0 || current_node == next_node) { break; }
            current_node = next_node;
        }
        data->motionEdges[i] = next_node;
    }

    // Update motionLabels with the correct island labels (minimum labels).
    for (u32 y = 0; y < data->downsampledHeight; y++) {
        for (u32 x = 0; x < data->downsampledWidth; x++) {
            u32 i = y * data->downsampledWidth + x;
            if (data->motionBlockDetection[i]) {
                data->motionLabels[i] = data->motionEdges[data->motionLabels[i]];
            }
        }
    }

    // Count the number of cells for each label
    for (u32 i = 0; i < data->downsampledDataLen; i++) {
        if (data->motionLabels[i] != LT_U32_MAX) {
            data->motionIslandSizes[data->motionLabels[i]]++;
            if (!bMotionDetected && data->motionIslandSizes[data->motionLabels[i]] >= params->minimumIslandSize) {
                bMotionDetected = true;
            }
        }
    }

    return bMotionDetected;
}

static void ApplyTemporalSmoothing(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                   LTMediaMotionDetectionAlgorithm_Data *data,
                                   bool isMotionDetected) {
    // Apply temporal smoothing - set block detection to true if counter > 0
    if (isMotionDetected) {
        for (u32 i = 0; i < data->downsampledDataLen; i++) {
            if (data->motionBlockDetection[i]) {
                // Motion detected for current moxel - reset counter to max
                data->motionTemporalCounter[i] = params->temporalSmoothingFrames;
            } else if (data->motionTemporalCounter[i] > 0) {
                // No motion for current moxel but counter still active - decrement
                data->motionTemporalCounter[i]--;
            }
            data->motionBlockDetection[i] = (data->motionTemporalCounter[i] > 0);
        }
    } else {
        for (u32 i = 0; i < data->downsampledDataLen; i++) {
            if (data->motionTemporalCounter[i] > 0) {
                // No motion for moxel frame but counter still active - decrement
                data->motionTemporalCounter[i]--;
            }
        }
    }
}

static bool UpdateIslandsBFS(LTMediaMotionDetectionAlgorithm_Parameters *params,
                             LTMediaMotionDetectionAlgorithm_Data *data,
                             u32 max_label,
                             bool bCalculatingROIs) {
    /*  Breadth-first search (BFS) motion detection algorithm. This sub-algorithm is used twice in the
     *  MultiROICalculationProc with different goals:
     *
     *  1. First we obtain a binary motion/no-motion signal. In this case we call with bCalculatingROIs == false so we
     *  return early as soon as any island at least as large as our target minimumIslandSize is found. We also use a
     *  smaller number of BFS neighbours to minimise the number of false positive detections (approximating the
     *  previous version of island search).
     *
     *  2. Once we know there is motion, we call with bCalculatingROIs == true and search with the full set of BFS
     *  neighbours to find connected components (islands) of motion. This larger number of neighbours makes it easier
     *  for components to connect to each other, and gives cleaner ROIs for further processing, but is not suitable for
     *  step 1 as it increases the number of false positives for motion detection. Each connected component will
     *  receive a unique label in the labels array.
     */

    // Reset all labels to LT_U32_MAX (indicating no label assigned yet)
    lt_memset(data->motionLabels, LT_U32_MAX, sizeof(u32) * data->downsampledDataLen);

    // Reset all island sizes to 0
    lt_memset(data->motionIslandSizes, 0, sizeof(u32) * max_label);

    // Reset all s_BFSIndexQueue to 0
    lt_memset(data->bfsIndexQueue, 0, sizeof(u32) * data->downsampledDataLen);

    u32 islandID = 0;
    bool bMotionDetected = false;
    u8 nbNeighbours = bCalculatingROIs ? data->bfsNeighbours : BFS_DIRECT_NEIGHBOURS;

    // Scan all moxels to find unlabelled motion regions
    // TODO: change to start from seeds
    for (u32 y = 0; y < data->downsampledHeight; y++) {
        for (u32 x = 0; x < data->downsampledWidth; x++) {
            u32 idx = y * data->downsampledWidth + x;

            // Skip if this moxel doesn't have motion or already has a label
            if (data->motionLabels[idx] != LT_U32_MAX || !data->motionBlockDetection[idx]) {
                continue;
            }

            // New connected component found - assign a new label
            islandID++;  // Start island labels from 1
            data->motionLabels[idx] = islandID;
            data->motionIslandSizes[islandID] = 1;  // First moxel in this island

            // Initialize s_BFSIndexQueue
            u32 queueFront = 0;
            u32 queueBack = 0;
            data->bfsIndexQueue[queueBack++] = idx;

            // Process BFS queue
            while (queueFront < queueBack) {
                u32 currIdx = data->bfsIndexQueue[queueFront++];
                s8 currY = currIdx / data->downsampledWidth;
                s8 currX = currIdx % data->downsampledWidth;

                // Check all bfs neighbors
                for (int d = 0; d < nbNeighbours; d++) {
                    s8 nextY = currY + s_bfsVerticalDistances[d];
                    if (nextY < 0 || nextY >= (int)data->downsampledHeight) {continue;}

                    s8 nextX = currX + s_bfsHorizontalDistances[d];
                    if (nextX < 0 || nextX >= (int)data->downsampledWidth) {continue;}

                    u32 nextIdx = (u32)nextY * data->downsampledWidth + (u32)nextX;

                    // Check if neighbor has motion and isn't labeled yet
                    if (data->motionLabels[nextIdx] == LT_U32_MAX && data->motionBlockDetection[nextIdx]) {
                        // Label this neighbor with the current label
                        data->motionLabels[nextIdx] = islandID; // also mark it as processed
                        // Increment island size
                        data->motionIslandSizes[islandID]++;
                        // Add to queue for further exploration
                        data->bfsIndexQueue[queueBack++] = nextIdx;

                        if (!bMotionDetected && data->motionIslandSizes[islandID] >= params->minimumIslandSize) {
                            if (!bCalculatingROIs) { return true; }
                            bMotionDetected = true;
                        }
                    }
                }
            }
        }
    }

    return bMotionDetected;
}

static bool MultiROICalculationProc(LTMediaMotionDetectionAlgorithm_Parameters *params,
                                    LTMediaMotionDetectionAlgorithm_Data *data,
                                    LTMediaData *baseMotionOutput) {
    /* Takes the baseMotionOutput from the media motion source, further computes and determines if motion has occurred.
     * If all 3 of the following conditions are met:
     *      a. (motion in any moxel > threshold);
     *      b. (is inside the set motion zone);
     *      c. (the frame has motion ROI > specific minimum size);
     * return true; else return false.
     * We also calculate the N largest output motion ROIs (where N == MAX_NUM_REGIONS).
     */

    if (!params || !data) { return false; }
    LTMediaMotionDetection_Regions *pMotionROIs = data->motionMetadata.regions;
    if (!pMotionROIs) { return false; }

    if (!data->motionLabels || !data->motionBlockDetection || !data->motionIslandSizes || !data->motionEdges) {
        LTLOG_YELLOWALERT("checkMultipleMotion.alloc.err", "Error: Failed to allocate buffer (%lu) for island detection arrays.", LT_Pu32(data->downsampledDataLen));
        return false;
    }

    LT_ASSERT(pMotionROIs->numRegions <= pMotionROIs->maxRegions);

    /* Reset the algorithm's per-frame data buffers */
    lt_memset(data->motionLabels, LT_U32_MAX, sizeof(u32) * data->downsampledDataLen);
    lt_memset(data->motionBlockDetection, false, sizeof(bool) * data->downsampledDataLen);
    lt_memset(data->motionIslandSizes, 0, sizeof(u32) * data->downsampledDataLen);
    lt_memset(data->motionEdges, LT_U32_MAX, sizeof(u32) * data->downsampledDataLen);
    // if (!s_useBFS) {
    //     lt_memset(s_motionEdges, LT_U32_MAX, MotionArraysSizeU32);
    // }

    LTMediaMotionDetection_MotionZoneBitmap *pMotionZoneBitmap;
    GetMotionZoneBitmap(&pMotionZoneBitmap);
    if (LTMediaMotionDetection_IsMotionZoneEnabled() && !pMotionZoneBitmap) {
        LTLOG_REDALERT("check.frame.nozone", "Motion zones enabled but no motion zone available, motion detection failed!");
        return false;
    }

    u32 max_label = data->pDownsamplingProc(params, data, baseMotionOutput);
    /* Update heatmap metadata embedded in the h264 stream after heatmap is updated within downsampling function. */
    if (params->heatmapParameters.bEnabled && S.pDeviceMedia) {
        S.pDeviceMedia->SetProperty("motion.heatmap", (const LTMediaMotionDetection_HeatmapData*)data->heatmapVideoData);
    }

    bool bMotionFrame = false;
    if (max_label > 0) {
        if (params->bEnableBFS) {
            /* We first check if there is motion by using BFS with only the 8 immediate neighbours, and return as
             * soon as we hit the minimum island size for motion early if motion is detected. */
            bMotionFrame = UpdateIslandsBFS(params, data, max_label, false);

            // If there is no motion, we just decrement the temporal smoothing counter.
            ApplyTemporalSmoothing(params, data, bMotionFrame);

            if (bMotionFrame) {
                // Note: data->motionLabels is re-initialized in UpdateIslandsBFS.
                UpdateIslandsBFS(params, data, max_label, true);
            }
        } else {
            bMotionFrame = UpdateIslands(params, data, max_label);
        }
    }

    /* Reset island sizes from last frame. */
    lt_memset(data->biggestIslandSizes, 0, sizeof(u32) * data->motionMetadata.regions->maxRegions);

    if (bMotionFrame) {
        pMotionROIs->numRegions = 0;
        // Find top n islands up to the maximum number
        for (u32 k = 0; k < pMotionROIs->maxRegions; k++) {
            u32 max_idx = LT_U32_MAX;
            u32 max_size = 0;

            // Find the next largest island
            for (u32 i = 0; i < data->downsampledDataLen; i++) {
                if (data->motionIslandSizes[i] > max_size) {
                    max_idx = i;
                    max_size = data->motionIslandSizes[i];
                    P("che.mot.island.size", "Island index: %lu, Island Size: %lu\r\n", LT_Pu32(max_idx), LT_Pu32(max_size));
                }
            }
            data->biggestIslandSizes[k] = max_size;

            // If a valid island is found and the area is bigger than a downsampled moxel
            if ((max_idx != LT_U32_MAX) && (params->minimumIslandSize <= max_size) && (max_size <= data->downsampledDataLen)) {
                data->motionIslandSizes[max_idx] = 0; // Mark as processed
                // Find bounding box coordinates
                LTMediaMotionDetection_Region *outputROI = &pMotionROIs->region[k];
                outputROI->x1 = S.horizontalMoxels;
                outputROI->y1 = S.verticalMoxels;
                outputROI->x2 = 0;
                outputROI->y2 = 0;
                for (u32 i = 0; i < data->downsampledDataLen; i++) {
                    if (data->motionLabels[i] == max_idx) {
                        u32 y = LT_MIN(S.verticalMoxels - 1, (i / data->downsampledWidth) * params->downsamplingScaleFactor);
                        u32 x = LT_MIN(S.horizontalMoxels - 1, (i % data->downsampledWidth) * params->downsamplingScaleFactor);

                        /* When upsampling, for x1, y1 we take the top left hand corner of the downsampled moxel;
                         * but for x2, y2 we take the bottom right hand corner to maximise the ROI - i.e. round up to
                         * odd number if it's even. */
                        if (x < pMotionROIs->region[k].x1) { pMotionROIs->region[k].x1 = x; }
                        if (y < pMotionROIs->region[k].y1) { pMotionROIs->region[k].y1 = y; }

                        /* During upsampling, round the coordinates up from top left to bottom right of the
                         * downsampled moxel. */
                        y = LT_MIN(S.verticalMoxels - 1, y + params->downsamplingScaleFactor - 1);
                        x = LT_MIN(S.horizontalMoxels - 1, x + params->downsamplingScaleFactor - 1);
                        if (x > pMotionROIs->region[k].x2) { pMotionROIs->region[k].x2 = x; }
                        if (y > pMotionROIs->region[k].y2) { pMotionROIs->region[k].y2 = y; }
                    }
                }
                pMotionROIs->numRegions++;
            } else {
                // No more islands are larger than minimum threshold and smaller than maximum threshold
                break;
            }
        }

        P("che.mot.roi.msg.sets", "Bounding Boxes of the Top %lu Largest Sets:\n", LT_Pu32(pMotionROIs->numRegions));
        for (u8 n = 0; n < pMotionROIs->numRegions; n++) {
            P("che.mot.roi.coords", "ROI x1: %lu, y1: %lu, x2: %lu, y2: %lu\r\n",
              LT_Pu32(pMotionROIs->region[n].x1),
              LT_Pu32(pMotionROIs->region[n].y1),
              LT_Pu32(pMotionROIs->region[n].x2),
              LT_Pu32(pMotionROIs->region[n].y2));
        }
    }

    return bMotionFrame;
}

/** The actual ROI calculation functions, order must be the same as in LTMediaMotionROICalculation enum. */
static LTMediaMotionDetectionAlgorithm_ROICalculationProc *s_roiCalculationProcs[kLTMediaMotionROICalculationStrategy_NumStrategies] = {
    &SingleROICalculationProc,
    &MultiROICalculationProc
};

/** The actual downsampling functions, order must be the same as in LTMediaMotionDownsampling enum. */
static LTMediaMotionDetectionAlgorithm_DownsamplingProc *s_downsamplingProcs[kLTMediaMotionDownsamplingStrategy_NumStrategies] = {
    &DownsamplingMaxPooling,
    &DownsamplingAvgPooling,
    NULL   // No downsampling
};

static void FreeAlgorithmData(LTMediaMotionDetectionAlgorithm_Data *pData) {
    if (!pData) { return; }

    lt_free(pData->motionIslandSizes);
    pData->motionIslandSizes = NULL;
    lt_free(pData->motionLabels);
    pData->motionLabels = NULL;
    lt_free(pData->motionBlockDetection);
    pData->motionBlockDetection = NULL;
    lt_free(pData->motionEdges);
    pData->motionEdges = NULL;
    LTMediaMotionDetection_FreeRegionList(pData->motionMetadata.regions);
    pData->motionMetadata.regions = NULL;
    lt_free(pData->biggestIslandSizes);
    pData->biggestIslandSizes = NULL;
    lt_free(pData->motionTemporalCounter);
    pData->motionTemporalCounter = NULL;
    lt_free(pData->bfsIndexQueue);
    pData->bfsIndexQueue = NULL;
    lt_free(pData->moxelHeatmap);
    pData->moxelHeatmap = NULL;
    if (pData->heatmapVideoData) {
        lt_free(pData->heatmapVideoData->address);
        pData->heatmapVideoData->address = NULL;
        lt_free(pData->heatmapVideoData);
        pData->heatmapVideoData = NULL;
    }
}

static void LTMediaMotionDetectionAlgorithmImpl_SetDownsamplingStrategy(LTMediaMotionDetectionAlgorithmImpl *pAlgo,LTMediaMotionDownsamplingStrategy downsamplingStrategy) {
    /* Algorithm Downsampling proc selection */
    if (downsamplingStrategy < kLTMediaMotionDownsamplingStrategy_NumStrategies) {
        pAlgo->data.pDownsamplingProc = s_downsamplingProcs[downsamplingStrategy];
    } else {
        LTLOG_YELLOWALERT("set.downsamplingproc.fail", "Invalid downsampling algorithm selected, set failed.");
    }
}

static void LTMediaMotionDetectionAlgorithmImpl_SetROICalculationStrategy(LTMediaMotionDetectionAlgorithmImpl *pAlgo,  LTMediaMotionROICalculationStrategy roiCalculationStrategy) {
    /* Algorithm ROI Calculation proc selection */
    if (roiCalculationStrategy < kLTMediaMotionROICalculationStrategy_NumStrategies) {
        pAlgo->data.pROICalculationProc = s_roiCalculationProcs[roiCalculationStrategy];
    } else {
        LTLOG_YELLOWALERT("set.noroiproc.fail", "Invalid ROI calculation algorithm selected, set failed.");
    }
}

static bool LTMediaMotionDetectionAlgorithmImpl_CheckFrameForMotion(LTMediaMotionDetectionAlgorithmImpl *pAlgo,
                                                                    LTMediaData *baseMotionOutput) {
    return (pAlgo->data.pROICalculationProc)(&pAlgo->params, &pAlgo->data, baseMotionOutput);
}

static void LTMediaMotionDetectionAlgorithmImpl_GetParameters(LTMediaMotionDetectionAlgorithmImpl *pAlgo,
                                                              LTMediaMotionDetectionAlgorithm_Parameters *pParams) {
    if (pParams) { *pParams = pAlgo->params; }
}

static void LTMediaMotionDetectionAlgorithmImpl_SetDifferenceThreshold(LTMediaMotionDetectionAlgorithmImpl *pAlgo,
                                                                       u8 value) {
    pAlgo->params.differenceThreshold = value;
}

static u8 LTMediaMotionDetectionAlgorithmImpl_GetDifferenceThreshold(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.differenceThreshold;
}

static void LTMediaMotionDetectionAlgorithmImpl_SetMinimumIslandSize(LTMediaMotionDetectionAlgorithmImpl *pAlgo,
                                                                     u32 value) {
    pAlgo->params.minimumIslandSize = value;
}

static u32 LTMediaMotionDetectionAlgorithmImpl_GetMinimumIslandSize(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.minimumIslandSize;
}

static LTMediaMotionDetection_Regions *LTMediaMotionDetectionAlgorithmImpl_GetMotionROIs(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTMediaMotionDetection_Regions *motionROIs = LTMediaMotionDetection_CloneRegionList(pAlgo->data.motionMetadata.regions);
    return motionROIs; /* Note: The caller must free motionROIs. */
}

static LTMediaMotionDetection_Metadata *LTMediaMotionDetectionAlgorithmImpl_GetMotionMetadata(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return &pAlgo->data.motionMetadata;
}

static MotionIslandSizes LTMediaMotionDetectionAlgorithmImpl_GetBiggestIslandSizes(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    MotionIslandSizes biggestIslandSizes = {0};
    lt_memcpy(biggestIslandSizes.sizes, pAlgo->data.biggestIslandSizes, sizeof(u32) * MAX_NUM_MOTION_REGIONS);

    /* Count the number of non-zero island sizes copied. */
    for (u32 i = 0; i < MAX_NUM_MOTION_REGIONS; i++) {
        if (biggestIslandSizes.sizes[i] != 0) {
            biggestIslandSizes.numIslandSizes++;
        } else {
            break; // All zero-sized islands are guaranteed to be after non-zero islands
        }
    }

    return biggestIslandSizes;
}

static void LTMediaMotionDetectionAlgorithmImpl_EnableBFS(LTMediaMotionDetectionAlgorithmImpl *pAlgo, bool bEnable) {
    pAlgo->params.bEnableBFS = bEnable;
}

static bool LTMediaMotionDetectionAlgorithmImpl_IsBFSEnabled(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.bEnableBFS;
}

static void LTMediaMotionDetectionAlgorithmImpl_SetTemporalSmoothingFrames(LTMediaMotionDetectionAlgorithmImpl *pAlgo, u8 temporalSmoothingFrames){
    pAlgo->params.temporalSmoothingFrames = temporalSmoothingFrames;
}

static void LTMediaMotionDetectionAlgorithmImpl_SetBFSDistance(LTMediaMotionDetectionAlgorithmImpl *pAlgo, u8 bfsDistance) {
    /* Validate input. */
    if (bfsDistance < s_minimumBFSDistance) {
        LTLOG_YELLOWALERT("set.bfs.distance.toolow", "Invalid BFS value %lu, setting to %lu.", LT_Pu32(bfsDistance), LT_Pu32(s_minimumBFSDistance));
        bfsDistance = s_minimumBFSDistance;
    } else if (bfsDistance > s_maximumBFSDistance) {
        LTLOG_YELLOWALERT("set.bfs.distance.toohigh", "Invalid BFS value %lu, capping at %lu.", LT_Pu32(bfsDistance), LT_Pu32(s_maximumBFSDistance));
        bfsDistance = s_maximumBFSDistance;
    }
    pAlgo->params.bfsDistance = bfsDistance;

    /* Number of BFS neighbours N for BFS (Manhattan) distance is N = 2M^2 + 2M = 2M(M + 1). */
    pAlgo->data.bfsNeighbours = 2 * bfsDistance * (bfsDistance + 1);
}

static u8 LTMediaMotionDetectionAlgorithmImpl_GetBFSDistance(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.bfsDistance;
}

static bool LTMediaMotionDetectionAlgorithmImpl_IsObjectDetectionEnabled(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.bEnableObjectDetection;
}

static void LTMediaMotionDetectionAlgorithmImpl_EnableHeatmap(LTMediaMotionDetectionAlgorithmImpl *pAlgo, bool bEnable) {
    pAlgo->params.heatmapParameters.bEnabled = bEnable;
}

static bool LTMediaMotionDetectionAlgorithmImpl_IsHeatmapEnabled(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    return pAlgo->params.heatmapParameters.bEnabled;
}

static void LTMediaMotionDetectionAlgorithmImpl_SetHeatmapParameters(LTMediaMotionDetectionAlgorithmImpl *pAlgo, MotionHeatmapParameters *pHeatmapParams) {
    if (pHeatmapParams) { pAlgo->params.heatmapParameters = *pHeatmapParams; }
}

static void LTMediaMotionDetectionAlgorithmImpl_GetHeatmapParameters(LTMediaMotionDetectionAlgorithmImpl *pAlgo, MotionHeatmapParameters *pHeatmapParams) {
    if (pHeatmapParams) { *pHeatmapParams = pAlgo->params.heatmapParameters; }
}

static void LTMediaMotionDetectionAlgorithmImpl_SetHeatmapSensitivity(LTMediaMotionDetectionAlgorithmImpl *pAlgo, u8 motionSensitivity) {
    /* Adjust heatmap sensitivity parameters based on the abstract 0-100 motion sensitivity value.
     * High sensitivity -> low DifferenceThreshold, low heatmap gain, high heatmap drop
     * Low sensitivity -> high DifferenceThreshold, high heatmap gain, low heatmap drop
     * E.g.:
          MotionDetectionSensitivity = 0 => gain = 2.0; drop = 0.01
          MotionDetectionSensitivity = 50 => gain = 1.0; drop = 0.1
          MotionDetectionSensitivity = 100 => gain = 0.5; drop = 0.2
    */
    pAlgo->params.heatmapParameters.drop = (motionSensitivity > 5) ?
                                                   pAlgo->params.heatmapParameters.dropGradient * motionSensitivity :
                                                   0.01;
    if (motionSensitivity <= 50) {
        pAlgo->params.heatmapParameters.gain = pAlgo->params.heatmapParameters.gainInterceptLow - pAlgo->params.heatmapParameters.gainGradientLow * motionSensitivity;
    } else {
        pAlgo->params.heatmapParameters.gain = pAlgo->params.heatmapParameters.gainInterceptHigh - 0.5 * pAlgo->params.heatmapParameters.gainGradientHigh * motionSensitivity;
    }
}

static float LTMediaMotionDetectionAlgorithmImpl_GetHeatmapMin(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTMediaMotionDetectionAlgorithm_Parameters *params = &pAlgo->params;
    LTMediaMotionDetectionAlgorithm_Data *data = &pAlgo->data;

    float heatmapMin = params->heatmapParameters.cap;
    for (u32 i = 0; i < data->downsampledDataLen; i++) {
        heatmapMin = (heatmapMin < data->moxelHeatmap[i]) ? heatmapMin : data->moxelHeatmap[i];
    }
    return heatmapMin;
}

static float LTMediaMotionDetectionAlgorithmImpl_GetHeatmapMax(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTMediaMotionDetectionAlgorithm_Data *data = &pAlgo->data;

    float heatmapMax = 0.0;
    for (u32 i = 0; i < data->downsampledDataLen; i++) {
        heatmapMax = (heatmapMax > data->moxelHeatmap[i]) ? heatmapMax : data->moxelHeatmap[i];
    }
    return heatmapMax;
}

static float LTMediaMotionDetectionAlgorithmImpl_GetHeatmapAvg(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTMediaMotionDetectionAlgorithm_Data *data = &pAlgo->data;

    float heatmapTotal = 0;
    for (u32 i = 0; i < data->downsampledDataLen; i++) {
        heatmapTotal += data->moxelHeatmap[i];
    }
    return heatmapTotal / data->downsampledDataLen;
}

static void LTMediaMotionDetectionAlgorithmImpl_ResetHeatmap(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTMediaMotionDetectionAlgorithm_Data *data = &pAlgo->data;

    for (u32 i = 0; i < data->downsampledDataLen; i++) {
        data->moxelHeatmap[i] = 0.0;
        data->heatmapVideoData->address[i] = (u8)0;
    }
}

void LTMediaMotionDetectionAlgorithmImpl_ApplyConfiguration(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LTSystemSettings *systemSettings = NULL;
    systemSettings = lt_openlibrary(LTSystemSettings);
    if (!systemSettings) {
        LTLOG_YELLOWALERT("apply.cfg.sysset.fail", "Could not open LTSystemSettings while applying ConfigService setting overrides.");
        return;
    }
    s64 value;
    bool ret = false;
    LTMediaMotionDetectionAlgorithm_Parameters *params = &pAlgo->params;
    MotionHeatmapParameters *heatmapParams = &params->heatmapParameters;
    LTMediaMotionDetectionAlgorithm_Data *data = &pAlgo->data;

    /* For each overridable algorithm parameter, check a hidden ConfigService setting to see if the default value needs
     * to be overridden. */

    /* Downsampling Scale Factor */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/downsamplingScaleFactor", &value);
    if (ret && value >= 0) {
        params->downsamplingScaleFactor = (u32)value;
        /* Recalculate other internal downsampling constants based on new factor */
        data->downsampledWidth = S.horizontalMoxels / params->downsamplingScaleFactor + (S.horizontalMoxels % params->downsamplingScaleFactor > 0);
        data->downsampledHeight = S.verticalMoxels / params->downsamplingScaleFactor + (S.verticalMoxels % params->downsamplingScaleFactor > 0);
        data->downsampledDataLen = data->downsampledWidth * data->downsampledHeight;
    }

    /* Downsampling strategy */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/downsamplingStrategy", &value);
    if (ret && value >= 0) {
        LTMediaMotionDetectionAlgorithmImpl_SetDownsamplingStrategy(pAlgo, (LTMediaMotionDownsamplingStrategy)value);
    }

    /* ROI Calculation strategy */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/ROICalculationStrategy", &value);
    if (ret && value >= 0) {
        LTMediaMotionDetectionAlgorithmImpl_SetROICalculationStrategy(pAlgo, (LTMediaMotionROICalculationStrategy)value);
    }

    /* Temporal smoothing frame count. */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/temporalSmoothingFrames", &value);
    if (ret && value >= 0) {
        params->temporalSmoothingFrames = value;
    }

    /* Manhattan distance for finding connected components during BFS. */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/BFSDistance", &value);
    if (ret && value >= 0) {
        LTMediaMotionDetectionAlgorithmImpl_SetBFSDistance(pAlgo, (u8)value);
    }

    ret = CheckHiddenIntegerSetting(systemSettings, "motion/enableObjectDetection", &value);
    if (ret && value >= 0) { params->bEnableObjectDetection = (bool)value; }

    /* Motion heatmap parameter overrides */
    /* Currently overriding the heatmap enable flag here is the only way to enable the heatmap on customer devices. */
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/enableHeatmap", &value);
    if (ret && value >= 0) { heatmapParams->bEnabled = (bool)value; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapCap", &value);
    if (ret && value >= 0) { heatmapParams->cap = (float)value; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapThreshold", &value);
    if (ret && value >= 0) { heatmapParams->threshold = (float)value; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapGainInterceptLow", &value);
    if (ret && value >= 0) { heatmapParams->gainInterceptLow = (float)value / 10000.0; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapGainInterceptHigh", &value);
    if (ret && value >= 0) { heatmapParams->gainInterceptHigh = (float)value / 10000.0; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapGainGradientLow", &value);
    if (ret && value >= 0) { heatmapParams->gainGradientLow = (float)value / 10000.0; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapGainGradientHigh", &value);
    if (ret && value >= 0) { heatmapParams->gainGradientHigh = (float)value / 10000.0; }
    ret = CheckHiddenIntegerSetting(systemSettings, "motion/heatmapDropGradient", &value);
    if (ret && value >= 0) { heatmapParams->dropGradient = (float)value / 10000.0; }

    lt_closelibrary(systemSettings);
}

static bool LTMediaMotionDetectionAlgorithmImpl_Init(LTMediaMotionDetectionAlgorithmImpl *pAlgo,
                                                     LTMediaMotionDetectionAlgorithm_Parameters *pInitialParams) {
    if (!pAlgo || !pInitialParams) { return false; }

    LTMediaMotionDetectionAlgorithm_Data *pData = &pAlgo->data;
    LTMediaMotionDetectionAlgorithm_Parameters *pParams = &pAlgo->params;

    *pParams = *pInitialParams;
    /* If downsampling is disabled but the factor is not set to 1, silently change it. */
    if (pParams->downsamplingStrategy == kLTMediaMotionDownsamplingStrategy_None) { pParams->downsamplingScaleFactor = 1; }
    pData->downsampledWidth = S.horizontalMoxels / pParams->downsamplingScaleFactor + (S.horizontalMoxels % pParams->downsamplingScaleFactor > 0);
    pData->downsampledHeight = S.verticalMoxels / pParams->downsamplingScaleFactor + (S.verticalMoxels % pParams->downsamplingScaleFactor > 0);
    pData->downsampledDataLen = pData->downsampledWidth * pData->downsampledHeight;

    /* Fill in BFS parameters with default parameters, ignoring those supplied in pInitialParams. */
    pParams->bEnableBFS = false;
    pParams->temporalSmoothingFrames = 3;
    LTMediaMotionDetectionAlgorithmImpl_SetBFSDistance(pAlgo, 4);

    pParams->bEnableObjectDetection = false;

    /* Fill in heatmap parameters with default values, ignoring those supplied in pInitialParams.
     * Heatmap feature defaults to enabled. */
    pParams->heatmapParameters = (MotionHeatmapParameters) {
        .bEnabled = true,
        .cap = 200.0, /* Build up to max heatmap cap in 10 secs at 20FPS. */
        .gain = 1.0,
        .drop = 0.03,  /* Corresponds to cool down period of 3.33 minutes from cap value */
        .dropGradient = 0.0006,
        .gainGradientLow = 0.02,
        .gainGradientHigh = 0.02,
        .gainInterceptLow = 2.0,
        .gainInterceptHigh = 1.5,
        .threshold = 100.0
    };

    /* Algorithm Downsampling proc selection */
    LTMediaMotionDetectionAlgorithmImpl_SetDownsamplingStrategy(pAlgo, pParams->downsamplingStrategy);

    /* Algorithm ROI Calculation proc selection */
    LTMediaMotionDetectionAlgorithmImpl_SetROICalculationStrategy(pAlgo, pParams->roiCalculationStrategy);

    /* TODO: Currently disabling downsampling is not supported with multi-ROI detection. */
    if (pParams->roiCalculationStrategy == kLTMediaMotionROICalculationStrategy_MultipleROIs &&
        pParams->downsamplingStrategy == kLTMediaMotionDownsamplingStrategy_None) {
        LTLOG_YELLOWALERT("init.invalid.stat.combo", "Downsampling must be used with multi-ROI calculation, initialisation failed.");
        return false;
    }

    /* Allocate all algorithm buffers regardless of whether they are needed by the specific ROI calculation algorithm,
     * in case the ROI calculation proc changes during the algorithm's lifetime (this happens in the unit tests for
     * example). */
    pData->motionMetadata.regions = LTMediaMotionDetection_MallocateRegionList(MAX_NUM_MOTION_REGIONS);
    if (!pData->motionMetadata.regions) {
        LTLOG_YELLOWALERT("init.regions.oom", "Could not allocate region list for algorithm, initialisation failed.");
        return false;
    }
    pData->motionMetadata.regions->motionFrameSize.width = S.horizontalMoxels;
    pData->motionMetadata.regions->motionFrameSize.height = S.verticalMoxels;
    pData->motionLabels = lt_malloc(sizeof(u32) * pData->downsampledDataLen);
    pData->motionIslandSizes = lt_malloc(sizeof(u32) * pData->downsampledDataLen);
    pData->motionEdges = lt_malloc(sizeof(u32) * pData->downsampledDataLen);
    pData->motionBlockDetection = lt_malloc(sizeof(bool) * pData->downsampledDataLen);
    pData->biggestIslandSizes = lt_malloc(sizeof(u32) * MAX_NUM_MOTION_REGIONS);
    pData->motionTemporalCounter = lt_malloc(sizeof(u8) * pData->downsampledDataLen);
    pData->bfsIndexQueue = lt_malloc(sizeof(u32) * pData->downsampledDataLen);
    pData->moxelHeatmap = lt_malloc(sizeof(float) * pData->downsampledDataLen);
    pData->heatmapVideoData = lt_malloc(sizeof(LTMediaMotionDetection_HeatmapData));
    if (pData->heatmapVideoData) {
        pData->heatmapVideoData->address = lt_malloc(sizeof(u8) * pData->downsampledDataLen);
        pData->heatmapVideoData->length = pData->downsampledDataLen;
    }

    if (!pData->motionLabels || !pData->motionIslandSizes || !pData->motionEdges || !pData->motionBlockDetection || !pData->biggestIslandSizes || !pData->motionTemporalCounter || !pData->bfsIndexQueue || !pData->heatmapVideoData || !pData->heatmapVideoData->address) {
        LTLOG_YELLOWALERT("init.buf.oom", "Could not allocate buffers for motion algorithm, initialisation failed.");
        FreeAlgorithmData(pData);
        return false;
    }
    /* Zero out buffers which are not reset each frame. */
    lt_memset(pData->motionTemporalCounter, 0, sizeof(u8) * pData->downsampledDataLen);
    LTMediaMotionDetectionAlgorithmImpl_ResetHeatmap(pAlgo);

    return true;
}

static bool LTMediaMotionDetectionAlgorithmImpl_ConstructObject(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    LT_UNUSED(pAlgo);
    /* TODO: When parameters to LTObject constructors are supported, the contents of
     * LTMediaMotionDetectionAlgorithmImpl_Init should be added here. */
    return true;
}

static void LTMediaMotionDetectionAlgorithmImpl_DestructObject(LTMediaMotionDetectionAlgorithmImpl *pAlgo) {
    /* Free all of the buffers allocated for this algorithm instance. */
    FreeAlgorithmData(&pAlgo->data);
}

define_LTObjectImplPublic(LTMediaMotionDetectionAlgorithm, LTMediaMotionDetectionAlgorithmImpl,
    Init,
    CheckFrameForMotion,
    GetParameters,
    SetDifferenceThreshold,
    GetDifferenceThreshold,
    SetMinimumIslandSize,
    GetMinimumIslandSize,
    SetDownsamplingStrategy,
    SetROICalculationStrategy,
    GetMotionROIs,
    GetMotionMetadata,
    GetBiggestIslandSizes,
    EnableBFS,
    IsBFSEnabled,
    SetTemporalSmoothingFrames,
    SetBFSDistance,
    GetBFSDistance,
    IsObjectDetectionEnabled,
    EnableHeatmap,
    IsHeatmapEnabled,
    SetHeatmapParameters,
    GetHeatmapParameters,
    SetHeatmapSensitivity,
    GetHeatmapMax,
    GetHeatmapAvg,
    GetHeatmapMin,
    ResetHeatmap,
    ApplyConfiguration
);
