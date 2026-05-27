/*******************************************************************************
 * <lt/media/motiondetection/LTMediaMotionDetection.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
/** @file LTMediaMotionDetection.h header for LTMediaMotionDetection library root interface.
  */

#ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMOTIONDETECTION_H
#define LT_INCLUDE_LT_MEDIA_LTMEDIAMOTIONDETECTION_H

#include <lt/core/LTCore.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/device/media/LTDeviceMedia.h>

LT_EXTERN_C_BEGIN

#define MOTION_SENSITIVITY_BUCKETS 11 // number of distinct motion sensitivity threshold values: one value for each of [0/10/20/30/40/50/60/70/80/90/100]
#define MAX_NUM_MOTION_REGIONS 3 // maximum number of motion ROIs that can be output by the motion algorithm

/** LTMediaMotionState is used by motion events to signal motion state changes to clients. */
typedef enum {
    kLTMediaMotionState_Stopped,  /**< The current  motion event has ended */
    kLTMediaMotionState_Started,  /**< A new motion event has started */
    kLTMediaMotionState_Snapshot, /**< A snapshot is available for the current motion event */
    kLTMediaMotionState_Aborted,  /**< The current externally-triggered motion event has been aborted as no moxel motion was detected */

    kLTMediaMotionState_NumEvents,
} LTMediaMotionState;

/** LTMediaZoneState is used by detectionZone events to signal zone state changes to clients. */
typedef enum {
    kLTMediaMotionState_SetMotionZone,
    kLTMediaMotionState_EnableMotionZone,
} LTMediaZoneState;

/** The reasons that a motion event can be started. */
typedef enum {
    kLTMediaMotionCause_Moxel,  /**< The current motion event was triggered by meeting moxel threshold */
    kLTMediaMotionCause_PIR,    /**< The current motion event was triggered by an external PIR event */
} LTMediaMotionCause;

/** Flags for on-device CV categorization of motion events
 *  If the device has no on-device CV capabilities, these flags will be ignored
 */
typedef_LTENUM_SIZED(LTMediaMotionFlags, u16) {
    kLTMediaMotionFlags_None    = 0,
    kLTMediaMotionFlags_Person  = 1 << 0,
    kLTMediaMotionFlags_Package = 1 << 1,
    kLTMediaMotionFlags_Pet     = 1 << 2,
    kLTMediaMotionFlags_Dog     = 1 << 3,
    kLTMediaMotionFlags_Cat     = 1 << 4,
    kLTMediaMotionFlags_Vehicle = 1 << 5,
    kLTMediaMotionFlags_Box     = 1 << 6,
    kLTMediaMotionFlags_Car     = 1 << 7,
    kLTMediaMotionFlags_Face    = 1 << 8,
    kLTMediaMotionFlags_Other   = 1 << 9,
    kLTMediaMotionFlags_All     = (kLTMediaMotionFlags_Other << 1) - 1
};

/** Options for timing motion detection performance. */
typedef enum {
    kLTMediaMotionTimingMode_Off,   /* Don't do any timing */
    kLTMediaMotionTimingMode_CheckMotion,   /* Only time calls to CheckFrameFor(Multiple)Motion() */
    kLTMediaMotionTimingMode_FullMotionProc,    /* Time the full MediaSourceMotionEventProc() call */
} LTMediaMotionTimingMode;

/** Represents a region of motion detection interest.
 *
 *  %LTMediaMotionDetection_Region is used to identify a single rectangular
 *  region in which motion detection occurred - an ROI or region of interest.
 *  One or more region structs are elements of struct %LTMediaMotionDetection_Regions
 *
 *  @see LTMediaMotionDetection_Regions
 */
typedef struct LTMediaMotionDetection_Region {
    u32 x1; /**< X-coordinate of top left ROI vertex. */
    u32 y1; /**< Y-coordinate of top left ROI vertex. */
    u32 x2; /**< X-coordinate of bottom right ROI vertex. */
    u32 y2; /**< Y-coordinate of bottom right ROI vertex. */
} LTMediaMotionDetection_Region;

/** Represents a set of motion detection regions.
 *
 *  %LTMediaMotionDetection_Regions is notified to clients via the motion event to identify the regions in which motion
 *  was detected.
 *
 *  @see LTMediaMotionDetection_Regions
 */
typedef struct LTMediaMotionDetection_Regions {
    /****************************** DANGER! ******************************
     **** If this structure changes size (added or removed elements,  ****
     **** then you MUST also update the inline function               ****
     **** LTMediaMotionDetection_GetRegionListAllocationSize() below. ****
     *********************************************************************
     */
    u32                             numRegions;       /**< 0 means no valid regions, otherwise region[0] .. region[numRegions-1] are valid */
    u32                             maxRegions;       /**< the max number of regions the list can hold */
    LTMediaResolution               motionFrameSize;  /**< width and height of a motion frame */
    LTMediaMotionDetection_Region   region[];         /**< array of numRegions motion regions of interest */
} LTMediaMotionDetection_Regions;

/** Represents metadata for a motion event.
 *
 *  %LTMediaMotionDetection_Metadata contains a pointer to the regions of interest in the image where motion was
 *  detected.
 */
typedef struct LTMediaMotionDetection_Metadata {
    LTMediaMotionDetection_Regions* regions;
} LTMediaMotionDetection_Metadata;

/** Provides a motion event snapshot and associated metadata. */
typedef struct LTMediaMotionDetection_Snapshot {
    LTMediaKind nKind;              /**< The kind of snapshot (i.e. Image SD or HD) */
    LTMediaEncoding nEncoding;      /**< The encoding of the snapshot (i.e. JPEG)) */
    bool bNightVisionActive;        /**< Whether night vision was active when the snapshot was taken */
    u8 curSnapshotIndex;            /**< 0-based index of the current snapshot */
    u8 maxSnapshotCount;            /**< The maximum number of snapshots that may be provided for this motion event */
    u64 nTimestamp;                 /**< The timestamp of the snapshot */
    u32 nImageLen;                  /**< The length of the image data in bytes */
    LTMediaResolution resolution;   /**< The resolution of the image */
    u32 imageBufferID;              /**< The ID of the image buffer (for accessing an image on a remote SoC via an IPC mechanism) */
    u8 *pImageData;                 /**< A pointer to the raw full image data (mutually-exclusive with pImageBuffer) */
    LTBuffer *pImageBuffer;         /**< A pointer to buffered image data, allowing streaming of the image (mutually-exclusive with pImageData) */
    LTMediaMotionDetection_Regions *pMotionROIs;
        /**< A pointer to the regions of interest in the image where motion was detected in descending order of
         *   ranking by "energy"; the ROIs are of a minimum size and ranked by proximity to the centre of the image.
         */
} LTMediaMotionDetection_Snapshot;

/** The set of sensitivity parameters associated with a particular sensitivity value (0-100). These sensitivity
 *  values are grouped into buckets, where all values in the bucket share the same parameters. */
typedef struct MotionSensitivityParameters {
    u8 sensitivityThreshold;
    u8 minimumIslandSize;
} MotionSensitivityParameters;

/** This typedef controls the size of each motion zone bitmap word. */
typedef u16 LTMediaMotionDetection_MotionZoneBitmapWord;

/** Bitmap for enabling and disabling pixel blocks (moxels) in motion detection. This data structure is used to
 *  implement the detection zones, and its size is determined by the size of the base motion output from the video
 *  driver (platform-specific). */
typedef struct LTMediaMotionDetection_MotionZoneBitmap {
    LTMediaMotionDetection_MotionZoneBitmapWord *pData;   /**< A pointer to the bitmap data array of words. Every bit enables/disables one motion block (moxel) in the frame. */
    u32 nBitmapWords;   /**< The number of words in the bitmap array.*/
    u32 nBitmapWordLen; /**< The number of bits in each word in the array. */
} LTMediaMotionDetection_MotionZoneBitmap;

/** A struct for holding motion zone data as stored in system settings. This is different to
 *  LTMediaMotionDetection_MotionZoneBitmap because the dimensions of the bitmap are dictated by the mobile app
 *  rather than the dimensions of the base motion detection output. We keep a copy of the zone setting bitmap in
 *  RAM so that the mobile app can query it to display the current setting to the user in mobile app settings. This
 *  avoids us needing to read the value from flash every time it is queried. */
typedef struct LTMediaMotionDetection_MotionZoneBitGrid {
    u16 width;  /**< The horizontal resolution for the zone setting. */
    u16 height; /**< The vertical resolution for the zone setting. */
    LT_SIZE bitmapSize;  /**< The number of bytes allocated for the bitmap, should be (width * height + 7) / 8. */
    u8 bitmap[]; /**< The motion zone bitmap as stored in settings. */
} LTMediaMotionDetection_MotionZoneBitGrid;

/** Helper struct for passing data on the largest motion islands found in a given frame. */
typedef struct MotionIslandSizes {
    u32 sizes[MAX_NUM_MOTION_REGIONS]; /**< A list of island sizes. */
    u32 numIslandSizes; /**< The number of non-zero island sizes provided in the sizes array. */
} MotionIslandSizes;

/** Downsampling callback procedure selection enum */
typedef_LTENUM_SIZED(LTMediaMotionDownsamplingStrategy, u8) {
    kLTMediaMotionDownsamplingStrategy_MaxPooling,
    kLTMediaMotionDownsamplingStrategy_AveragePooling,
    kLTMediaMotionDownsamplingStrategy_None,
    kLTMediaMotionDownsamplingStrategy_NumStrategies /* The number of distinct available Downsampling strategies. */
};

/** ROI Calculation callback procedure selection enum */
typedef_LTENUM_SIZED(LTMediaMotionROICalculationStrategy, u8) {
    kLTMediaMotionROICalculationStrategy_SingleROI,
    kLTMediaMotionROICalculationStrategy_MultipleROIs,
    kLTMediaMotionROICalculationStrategy_NumStrategies /* The number of distinct available ROI Calculation strategies. */
};

/** Metadata used for streaming the heatmap to the camviewer for debug. */
typedef struct LTMediaMotionDetection_HeatmapData {
    u8 *address;
    u32 length;
} LTMediaMotionDetection_HeatmapData;

/** Parameters used by the motion heatmap feature. */
typedef struct MotionHeatmapParameters {
    bool bEnabled; /**< Flag controlling whether heatmap feature is enabled. */
    float cap; /**< Maximum heatmap value a single moxel can have. */
    float threshold; /**< Heatmap value beyond which moxels will not trigger. */
    float gain; /**< The amount by which the heatmap value for a given moxel increases each frame that the moxel contains motion. */
    float drop; /**< The amount by which the heatmap value for a given moxel decreases each frame that the moxel does not motion. */
    /* Heatmap gain follows two distinct linear mappings for low (<=50) and high (>50) motion sensitivities. */
    float gainInterceptLow;  /**< y-intercept for heatmap gain mapping at low (<=50) motion sensitivities. */
    float gainInterceptHigh; /**< y-intercept for heatmap gain mapping at high (>50) motion sensitivities. */
    float gainGradientLow;   /**< Gradient for heatmap gain mapping at low (<=50) motion sensitivities. */
    float gainGradientHigh;  /**< Gradient for heatmap gain mapping at high (>50) motion sensitivities. */
    float dropGradient;  /**< Maps motion sensitivity value to heatmap drop value. */
} MotionHeatmapParameters;

/** A struct for holding all of an LTMediaMotionDetectionAlgorithm instance's hyperparameters. These represent
 *  configurable parts of the algorithm that can be optimised to improve motion detection performance.
 */
typedef struct LTMediaMotionDetectionAlgorithm_Parameters {
    u32 differenceThreshold; /**< The algorithm's threshold for an individual moxel to be deemed to contain motion */
    u32 downsamplingScaleFactor; /**< The factor N by which to reduce the base motion input in each dimension during downsampling; moxels in NxN groups will be downsampled into a single moxel */
    u32 minimumIslandSize; /**< The minimum number of downsampled moxels an island must be contain to be considered an ROI; used to eliminate noise */
    LTMediaMotionDownsamplingStrategy downsamplingStrategy; /**< The strategy used by the algorithm for downsampling input moxel frames. */
    LTMediaMotionROICalculationStrategy roiCalculationStrategy; /**< The strategy used by the algorithm for calculating output ROIs. */
    bool bEnableBFS; /**< Flag controlling whether we use BFS during ROI detection. */
    u8 temporalSmoothingFrames; /**< The number N of frames (current frame and the preceding N-1 frames) over which to apply temporal smoothing. */
    u8 bfsDistance; /**< Manhattan distance used in BFS controlling how far around a moxel we search for connected components. */
    bool bEnableObjectDetection; /**< Flag controlling whether object detection is applied during motion detection. */
    MotionHeatmapParameters heatmapParameters; /**< Parameters controlling functioning of the motion heatmap feature. */
} LTMediaMotionDetectionAlgorithm_Parameters;

/** The %LTMediaMotionDetectionAlgorithm object represents a specific algorithm configuration which can be used to
 *  detect motion.
 *
 *  An algorithm instance must first be initialised with %Init before it can be used. A binary motion/no-motion
 *  result for a given frame of moxel input can be determined using the %CheckFrameForMotion method, which also
 *  populates a set of output motion ROIs which can be retrieved using the %GetMotionROIs method.
 *
 *  See %LTMediaMotionDetectionAlgorithm_Parameters for a list of configurable algorithm hyperparameters. */
typedef_LTObject(LTMediaMotionDetectionAlgorithm, 1) {

    /* ----- Parameterised object constructor ----- */

    bool (* Init)(LTMediaMotionDetectionAlgorithm *pAlgo,
                  LTMediaMotionDetectionAlgorithm_Parameters *pInitialParams);
    /**< Initialises the algorithm instance with the provided hyperparameters.
     *
     *   @param pAlgo - the algorithm instance to initialise.
     *   @param pInitialParams - the parameters to initialise the algorithm instance with.
     *
     *   @note The heatmap parameters in pParams are not currently used and must be set with %SetHeatmapParameters().
     *   The same goes for the parameters related to BFS and temporal smoothing, these must be set with %EnableBFS,
     *   %SetTemporalSmoothingFrames and %SetBFSDistance, respectively.
     *   @note It is not valid to call %Init more than once on the same algorithm object, as this will cause a memory
     *   leak. You must instead destroy the current object and create a new one.
     */

    /* ----- Main algorithm execution function ----- */

    bool (* CheckFrameForMotion)(LTMediaMotionDetectionAlgorithm *pAlgo,
                                 LTMediaData *baseMotionOutput);
    /**< Runs the algorithm on an input moxel frame.
      *
      *  This performs two main tasks:
      *  1. It gives a binary yes/no answer on whether the frame contained "relevant" motion.
      *  2. According to the algorithm parameters chosen, one or more regions of interest (ROIs) of motion are
      *  calculated. These ROIs can be used to determine which frame(s) should be selected to send to downstream tasks
      *  such as object detection.
      *
      *  @param pAlgo - the algorithm instance which will process the motion frame.
      *  @param baseMotionOutput - the input motion frame to process.
      *
      *  @return - whether the frame contains motion of interest
      *
      *  @note - the actual ROIs detected in the frame should be retrieved by calling the algorithm's GetMotionROIs()
      *  API.
      */

    /* ----- Algorithm parameter accessors/mutators ----- */

    void (* GetParameters)(LTMediaMotionDetectionAlgorithm *pAlgo, LTMediaMotionDetectionAlgorithm_Parameters *pParams);
    /**< Retrieves all of the algorithm hyperparameters.
     *
     *   @param pAlgo - the algorithm instance to retrieve the values from.
     *   @param pParams - pointer to a struct containing space for all of the retrieved algorithm parameters.
     */

    void (* ApplyConfiguration)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Reads all motion algorithm configuration (non-user) settings and overrides the corresponding runtime
     *   parameters in the algorithm instance with the read values.
     *
     *   @note This function must be called after %Init is called, or the applied configuration will be overridden.
     *
     *   @param pAlgo - the algorithm instance which will have its parameters overridden.
     */

    void (* SetDifferenceThreshold)(LTMediaMotionDetectionAlgorithm *pAlgo,
                                    u8 differenceThreshold);
    /**< Sets the algorithm's motion sensitivity threshold used during ROI calculation.
     *
     *   @param pAlgo - the algorithm instance to modify.
     *   @param differenceThreshold - the new motion sensitivity threshold to set.
     */

    u8 (* GetDifferenceThreshold)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the algorithm's motion sensitivity threshold used during ROI calculation.
     *
     *   @param pAlgo - the algorithm instance to retrieve the value from.
     *
     *   @return - the algorithm's motion sensitivity threshold.
     */

    void (* SetMinimumIslandSize)(LTMediaMotionDetectionAlgorithm *pAlgo,
                                  u32 minimumIslandSize);
    /**< Sets the minimum number of contiguous moxels a detected motion island must have to be considered as an ROI.
     *
     *   @param pAlgo - the algorithm instance to modify.
     *   @param minimumIslandSize - the new minimum island size to set.
     */

    u32 (* GetMinimumIslandSize)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the minimum number of contiguous moxels a detected motion island must have to be considered as an ROI.
     *
     *   @param pAlgo - the algorithm instance to retrieve the value from.
     *
     *   @return - the algorithm's minimum island size.
     */

    void (* SetDownsamplingStrategy)(LTMediaMotionDetectionAlgorithm *pAlgo,
                                     LTMediaMotionDownsamplingStrategy downsamplingStrategy);
    /**< Set the strategy to be used by the algorithm for downsampling input moxel frames for further processing.
     *
     *   @param pAlgo - the algorithm instance to modify.
     *   @param downsamplingStrategy - the new downsampling strategy to use. @see LTMediaMotionDownsampling for the
     *          downsampling strategies that are available.
     */

    void (* SetROICalculationStrategy)(LTMediaMotionDetectionAlgorithm *pAlgo,
                                       LTMediaMotionROICalculationStrategy roiCalculationStrategy);
    /**< Set the strategy to be used by the algorithm for calculate output motion ROIs.
     *
     *   @param pAlgo - the algorithm instance to modify.
     *   @param roiCalculationStrategy - the new ROI calculation strategy to use. @see LTMediaMotionROICalculation for
     *          the ROI calculation strategies that are available.
     */

    /* ----- Algorithm data accessors ----- */

    LTMediaMotionDetection_Regions *(* GetMotionROIs)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Returns a copy of the algorithm's current detected motion ROIs (from the last frame processed).
     *
     *   @note The caller must free the ROIs struct copy once done with it.
     *
     *   @param pAlgo - the algorithm instance to get the motion ROIs from.
     *
     *   @return - a pointer to a struct containing the motion ROIs list. The struct contains a *region* member which
     *   is the actual ROI list containing *numRegions* ROIs. See %LTMediaMotionDetection_Regions.
     */

    LTMediaMotionDetection_Metadata *(* GetMotionMetadata)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Returns a pointer to the algorithm's current motion metadata (from the last frame processed).
     *
     *   @param pAlgo - the algorithm instance to get the metadata from.
     *
     *   @return - a pointer to the motion metadata, which should be treated as read-only.
     */

    MotionIslandSizes (* GetBiggestIslandSizes)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Returns a copy of the sizes of the algorithm's current biggest motion islands (from the last frame processed).
     *
     *   @param pAlgo - the algorithm instance to get the largest island sizes from.
     *
     *   @return - a struct containing the sizes of the biggest motion islands detected in the last frame
     *             processed by the algorithm. See %MotionIslandSizes.
     */

    /* ----- Motion ROI Stabilisation accessors/mutators ----- */

    void (* EnableBFS) (LTMediaMotionDetectionAlgorithm *pAlgo, bool bEnable);
    /**< Enables/disables the motion breadth-first search feature for ROI stabilisation.
     *
     *   @param pAlgo - the algorithm instance to enable/disable BFS for.
     *   @param bEnable - true to enable, false to disable.
     */

    bool (* IsBFSEnabled)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Checks whether the motion breadth-first search feature is enabled for the algorithm.
     *
     *   @param pAlgo - the algorithm instance to check for BFS enabled/disabled status.
     *
     *   @return - whether the BFS feature is enabled on the algorithm instance.
     */

    void (* SetTemporalSmoothingFrames)(LTMediaMotionDetectionAlgorithm *pAlgo, u8 temporalSmoothingFrames);
    /**< Sets the number of motion frames across which to apply temporal smoothing for ROI stabilisation.
     *
     *   The count will include the current frame and count backwards, so setting to 1 is equivalent to disabling
     *   temporal smoothing.
     *
     *   @param pAlgo - the algorithm instance to set the number of temporal smoothing frames for.
     */

    void (* SetBFSDistance)(LTMediaMotionDetectionAlgorithm *pAlgo, u8 bfsDistance);
    /**< Sets the Manhattan (BFS) distance for the motion breadth-first search feature.
     *
     *   Supported BFS distances are 1-5. Anything outside this range will be clamped rounded up or down as necessary.
     *
     *   @param pAlgo - the algorithm instance to set the Manhattan (BFS) distance for.
     */

    u8 (* GetBFSDistance)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the Manhattan (BFS) distance for the motion breadth-first search feature.
     *
     *   @param pAlgo - the algorithm instance to get the Manhattan (BFS) distance for.
     */

    /* ----- Motion Heatmap accessors/mutators ----- */

    bool (* IsObjectDetectionEnabled)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Checks whether the object detection feature is enabled for the algorithm.
     *
     *   @param pAlgo - the algorithm instance to check for object detection enabled/disabled status.
     *
     *   @return - whether the object detection feature is enabled on the algorithm instance.
     */

    void (* SetHeatmapParameters)(LTMediaMotionDetectionAlgorithm *pAlgo, MotionHeatmapParameters *pHeatmapParams);
    /**< Sets all of the heatmap hyperparameters in one go.
     *
     *   @param pHeatmapParams - pointer to a struct containing all of the heatmap parameters to set.
     *
     *   @note None of the struct members can be empty, so if you do not wish to set certain members then you should
     *         first call GetHeatmapParameters() and modify the desired struct members before setting.
     */

    void (* GetHeatmapParameters)(LTMediaMotionDetectionAlgorithm *pAlgo, MotionHeatmapParameters *pHeatmapParams);
    /**< Retrieves all of the heatmap hyperparameters.
     *
     *   @param pAlgo - the algorithm instance to retrieve the values from.
     *   @param pHeatmapParams - pointer to a struct containing space for all of the retrieved heatmap parameters.
     */

    void (* ResetHeatmap)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Resets the moxel heatmap to zero so that it looks like no motion occurred recently. Used for testing only.
     *
     *   @param pAlgo - the algorithm instance to reset the heatmap for.
     */

    void (* EnableHeatmap)(LTMediaMotionDetectionAlgorithm *pAlgo, bool bEnable);
    /**< Enables/disables the motion heatmap feature.
     *
     *   @param pAlgo - the algorithm instance to enable/disable the heatmap for.
     *   @param bEnable - true to enable, false to disable.
     */

    bool (* IsHeatmapEnabled)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Checks whether the heatmap feature is enabled for the algorithm.
     *
     *   @param pAlgo - the algorithm instance to check for heatmap enabled/disabled status.
     *
     *   @return - whether the heatmap feature is enabled on the algorithm instance.
     */

    void (* SetHeatmapSensitivity)(LTMediaMotionDetectionAlgorithm *pAlgo, u8 motionSensitivity);
    /**< Adjusts internal heatmap parameters related to sensitivity based on the motion sensitivity user setting
     *   (0-100).
     *
     *   @param pAlgo - the algorithm instance to modify.
     *   @param motionSensitivity - the new motion sensitivity setting value to base heatmap sensitivity parameters on.
     */

    float (* GetHeatmapMax)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the maximum heatmap value currently in the heatmap.
     *
     *   @param pAlgo - the algorithm instance to check the heatmap for.
     *
     *   @return - the maximum heatmap value found in the algorithm's heatmap.
     */

    float (* GetHeatmapAvg)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the mean heatmap value across all values currently in the heatmap.
     *
     *   @param pAlgo - the algorithm instance to check the heatmap for.
     *
     *   @return - the mean heatmap value for the algorithm's heatmap.
     */

    float (* GetHeatmapMin)(LTMediaMotionDetectionAlgorithm *pAlgo);
    /**< Gets the minimum heatmap value currently in the heatmap.
     *
     *   @param pAlgo - the algorithm instance to check the heatmap for.
     *
     *   @return - the minimum heatmap value found in the algorithm's heatmap.
     */

} LTOBJECT_API;

/** Callback procedure type for notification of motion detection events, signalling motion state changes.
 *
 *  Clients register event procedures of this type by calling %OnMotionEvent() and unregister them by calling
 *  %OffMotionEvent().
 *
 *  @param motionState: The new state of the motion event. Used to indicate when motion has started or stopped.
 *  @param motionUuid: unique identifier for an ongoing motion event between the Started and Stopped motion states.
 *  @param motionSnapshot: when motionState==_Snapshot, this points to a snapshot structure containing the JPEG
 *         image, otherwise it's NULL.
 *  @param clientData: the clientData supplied when the event proc was registered.
 *
 *  @see OnMotionEvent, OffMotionEvent
 */
typedef void (LTMediaMotionDetection_MotionEventProc)(LTMediaMotionState motionState,
                                                      u8 *motionUuid,
                                                      LTMediaMotionDetection_Snapshot *motionSnapshot,
                                                      void *clientData);

/* Callback procedure type for notification of detection zone state enable and set custom zone events,
 *
 * Clients register event procedures of this type by calling %OnZoneStateEvent() and unregister them by calling
 * %NoZoneStateEvent().
 *
 * @param zoneState: The motion detection zone setting enabled event and custom detection zone set event.
 * @param clientData: the clientData supplied when the event proc was registered.
 *
 * @see OnZoneStateEvent, NoZoneStateEvent
 */
typedef void LTMediaMotionDetection_ZoneStateEventProc(LTMediaZoneState zoneState,
                                                       void *clientData);


/* Callback procedure type for notification of motion metadata events.
 *
 * Clients register event procedures of this type by calling %OnMetadataEvent() and unregister them by calling
 * %OffMetadataEvent().
 *
 * @param metadata - The struct containing metadata to be sent to the client. This contains a pointer and size for
 *        a MessagePack containing
 *        metadata about the motion event, such as the detected regions of interest for motion. It also contains
 *        an optional timestamp.
 * @param clientData - the clientData supplied when the event proc was registered.
 *
 * @see OnMetadataEvent, OffMetadataEvent
 */
typedef void (LTMediaMotionDetection_MetadataEventProc)(LTMediaMotionDetection_Metadata *metadata, u8 *motionUuid, void *clientData);


TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTMediaMotionDetection, 1);
struct LTMediaMotionDetectionApi {
    INHERIT_LIBRARY_BASE

    bool (* StartDetectionEngine)(bool bTakeSnapshots);
        /**< Starts motion detection on the video data from the default video capture LTMediaSource
          *  as provided by the LTDeviceMedia library used by the implementation.
          *
          *  @param bTakeSnapshots - specify whether to take motion JPEG snapshots.
          *  @return - true if the motion detection engine was started successfully, otherwise false
          */

    bool (* PauseMotionDetection)(void);
        /**< Temporarily pause motion detection.
          *
          *  Use the %ResumeMotionDetection() call to restart, but be aware that other events in the system may
          *  result in detection restarting automatically sooner - this request is not "sticky".
          *
          *  @return - true if detection is now paused (or was already paused), false if the detection engine is not
          *            currently running
          */

    bool (* ResumeMotionDetection)(void);
        /**< Resume motion detection if it is currently paused.
          *
          *  @return - true if detection was resumed (or already active), false if the detection engine is not
          *            currently running
          */

    void (* StopDetectionEngine)(void);
        /**< Stops motion detection that was previously started by %StartDetectionEngine() */

    bool (* IsDetectionEngineRunning)(void);
        /**< determines whether or not the detection engine is running
          *
          * @return whether or not the detection engine is running
          */

    bool (* ExternalTrigger)(LTMediaMotionCause trigger);
        /**< indicate that an external trigger for motion detection has occcurred.
         *
         *   An external trigger is used to bypass the automatic moxel frame analysis and
         *   force a motion event to start, for example from a PIR sensor. However after
         *   the motion event starts the regular moxel analysis will continue to run and
         *   the event will be ABORTED if no further motion is detected.
         *
         *   @param trigger - the cause of the motion event
         *   @return true if the trigger was accepted, or false if the detection engine is not running
         */

    void (* SetMotionZones)(LTMediaMotionDetection_MotionZoneBitGrid *pBitGrid);
        /**< Sets the motion zones for the motion detection engine from the given zone setting bitGrid, scaling
          *  appropriately using the supplied bitGrid's width and height.
          *  This function should be called to apply new detection zones without restarting the motion detection engine
          *  whenever the motion zone settings are updated.
          *
          *  @param *pBitGrid - pointer to a motion zone settings bitGrid.
          *
          *  @note it is assumed that the size of pBitGrid->bitmap is the minimum number of bytes that can hold (width
          *  x height) bits. It is the caller's responsibility to guarantee this.
          */

    void (* GetMotionZones)(void **valueOut, u32 *valueOutSize);
        /**< Reads the motion zone setting from a copy kept in library RAM.
          *
          *  @param valueOut - a pointer to pointer which will be set to point to the zone setting bitGrid if the
          *  library has one.
          *  @param valueOutSize - this will be set to the size of the zone setting in bytes.
          *
          *  @note - If the setting has not been set yet, the caller will see that *valueOut is a null pointer. In
          *  practice this should not happen because the library will set a default internal zone setting upon
          *  initialisation.
          */

    bool (* IsMotionZoneEnabled)(void);
        /**< Determines whether or not the motion zone is enabled.
          *
          *  If motion zone is disabled, the whole frame is scanned for motion.
          *
          *  @return Whether or not the motion zone is enabled.
          */

    void (* EnableMotionZones)(bool enable);
        /**< Enable/disable motion zone.
          *
          *  If motion zone is disabled, the whole frame is scanned for motion.
          *
          *  @param enable - Enable or disable motion zones.
          */

    void (* OnMotionEvent)(LTMediaMotionDetection_MotionEventProc *pMotionEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void * pClientData);
        /**< Registers a motion event callback procedure for receipt of motion detection events as they occur.
          *  More than one client can register for notification and each client is automatically called back in their
          *  own thread context (i.e. which ever thread a client uses to call this registration function will be the
          *  hread that that respective client will have their callback function called on.
          *
          *  @param pMotionEventProc the clients event callback procedure
          *  @param pClientDataReleaseProc the clients clientData release proc that gets called when the client calls
          *         OffMotionEvent to unregister, or when this library is unloaded with clients still registered for
          *         this event
          *  @param pClientData client supplied optional context data that is passed back to the client's event
          *         callback during event notification
          */

    void (* NoMotionEvent)(LTMediaMotionDetection_MotionEventProc *pMotionEventProc);
        /**< Unregisters a previously registered event callback. */

    void (* OnMetadataEvent)(LTMediaMotionDetection_MetadataEventProc *pMetadataEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< Registers a motion event callback procedure for receipt of motion detection events as they occur.
          *  More than one client can register for notification and each client is automatically called back in their
          *  own thread context (i.e. whichever thread a client uses to call this registration function will be the
          *  thread that that respective client will have their callback function called on.
          *
          *  @param pMetadataEventProc the client event callback procedure
          *  @param pClientDataReleaseProc the client's clientData release proc that gets called when the client calls
          *         OffMetadataEvent to unregister, or when this library is unloaded with clients still registered for
          *         this event
          *  @param pClientData client supplied optional context data that is passed back to the client's event
          *         callback during event notification
          */

    void (* NoMetadataEvent)(LTMediaMotionDetection_MetadataEventProc *pMetadataEventProc);
        /**< Unregisters a previously registered event callback.
         *
         *   @param pMetadataEventProc - the metadata event proc to unregister.
         */

    void (* OnZoneStateEvent)(LTMediaMotionDetection_ZoneStateEventProc *pZoneStateEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< Registers a motion detection zone state change event callback procedure for receipt of motion detection
         *   zone setting enabled event and custom detection zone set event.
         *   More than one client can register for notification and each client is automatically called back in their
         *   own thread context (i.e. whichever thread a client uses to call this registration function will be the
         *   thread that that respective client will have their callback function called on.
         *
         *   @param pZoneStateEventProc the client event callback procedure
         *   @param pClientDataReleaseProc the client's clientData release proc that gets called when the client calls
         *          NoZoneStateEvent to unregister, or when this library is unloaded with clients still registered for
         *          this event
         *   @param pClientData client supplied optional context data that is passed back to the client's event callback
         *          during event notification
         */

    void (* NoZoneStateEvent)(LTMediaMotionDetection_ZoneStateEventProc *pZoneStateEventProc);
        /**< Unregisters a previously registered event callback.
         *
         *   @param pZoneStateEventProc - the zone state event proc to unregister.
         */

    void (* SetSensitivity)(u8 value);
        /**< Sets the motion detection sensitivity. The supplied value should be between 0 and 100, and will be capped
          *  at 100 if too high. This 0-100 value is an abstract user-facing setting and gives control
          *  of several internal motion sensitivity parameters.
          *
          *  @param value - the new motion detection sensitivity value to set.
          */

    u8 (* GetSensitivity)(void);
        /**< Returns the current motion detection sensitivity value. */

    void (* ApplyConfiguration)(void);
        /**< Reads all motion detection configuration (non-user) settings and overrides the corresponding runtime
          *  parameters with the read values.
          *
          *  This function is exposed so that the application's MotionDetectionController can immediately apply
          *  configuration updates as they arrive. */

    /* ----- Motion detection unit testing / debugging API. ----- */

    void (* SetMotionTimingMode)(LTMediaMotionTimingMode mode);
    /**< Set the mode for displaying timing information of motion detection
     *
     * @param mode - 0 for off, 1 for timing only the motion frame checking logic, 2 for timing the whole motion
     *        proc call including taking a snapshot and the motion state machine.
     */

    void (*GetMotionStateParameters)(u32 *pMinMotionFramesForEvent, u32 *pMinFramesUntilEventEnd, u32 *pMaxFramesUntilFirstSnapshot, u32 *pMaxFramesForBestSnapshot, u32 *pMinMotionFramesEventDuration);
    /**< Get the motion detection state machine parameters. Passing NULL for a parameter skips fetching that parameter.
     *
     *   @param pMinMotionFramesForEvent - pointer to retrieve the minimum number of consecutive motion frames required
     *          to start a motion event.
     *   @param pMinFramesUntilEventEnd - pointer to retrieve the minimum number of consecutive non-motion frames
     *          required to end a motion event.
     *   @param pMaxFramesUntilFirstSnapshot - pointer to retrieve the maximum number of frames that can pass in a
     *          motion event before the first snapshot must be notified.
     *   @param pMaxFramesForBestSnapshot - pointer to retrieve the maximum number of frames since a given snapshot is
     *          taken that we will assess for a better one (if there is no improvement).
     *   @param pMinMotionFramesEventDuration - pointer to retrieve the minimum number of frames a motion event must
     *          last after starting.
     */

    void (* GetMotionState)(LTMediaMotionState *pMotionState, u32 *pFramesOfMotion, u32 *pFramesNoMotion, float *pBestBoundingBoxWeighting, u32 *pFramesSinceSnapshotAvailable, u32 *pFramesSinceMotionStart, u8 *pSnapshotsUploaded);
    /**< Get the motion detection state machine data. Passing NULL for a parameter skips fetching that parameter.
     *
     *   @param pMotionState - pointer to retrieve the current state of the motion event.
     *   @param pFramesOfMotion - pointer to retrieve the current consecutive motion frame count.
     *   @param pFramesNoMotion - pointer to retrieve the current consecutive no-motion frame count.
     *   @param pBestBoundingBoxWeighting - pointer to retrieve the motion event's current best bounding box weighting
     *          score.
     *   @param pFramesSinceSnapshotAvailable - pointer to retrieve the number of frames since the last snapshot was
     *          taken.
     *   @param pFramesSinceMotionStart - pointer to retrieve the number of frames since the motion event started
     *          (inclusive)
     *   @param pSnapshotsUploaded - pointer to retrieve the number of snapshots uploaded in the current motion event.
     */

    void (* GetMotionZoneBitmap)(LTMediaMotionDetection_MotionZoneBitmap **ppMotionZoneBitmap);
    /**< Gets the motion zone bitmap.
     *
     *   @param ppMotionZoneBitmap - pointer to retrieve a pointer to the motion zone bitmap.
    */

    void (* ResetMotionState)(void);
    /**< Reset the motion detection state machine data to initial values. */

    void (* SetMotionEventStatsLogging)(bool bLogMotionEventStats);
    /**< Sets whether or not to log motion event stats when motion events start and stop.
     *
     *   @param bLogMotionEventStats - whether or not to log motion event stats.
     *
     *   @note This API only exists as a workaround in the unit tests. The motion event stats logging takes a
     *         significant amount of processing time and causes the unit tests in UnitTestLTMediaMotionDetection
     *         related to timing to fail. See ASPEN-8614 for the work to fix this.
     */

    void (* SetMotionEventMaxSnapshots)(u8 maxSnapshots);
    /**< Sets the maximum number of snapshots that will be sent per motion event.
     *
     *   A motion event may have less than this number if it ends early or criteria
     *   for taking a new snapshot are not met.
     *
     *   @param maxSnapshots The maximum number of snapshots to send.
     */

    u8 (* GetMotionEventMaxSnapshots)(void);
    /**< Gets the maximum number of snapshots that will be sent per motion event.
     *
     *   @return The maximum number of snapshots to send.
     */

    void (* SetDetectionFlags)(LTMediaMotionFlags flags);
    /**< Sets CV detection flags for motion detection, representing object classes of interest for the user.
     *
     *   @param flags - a bitmap representing the user's new object classes of interest.
     */

    LTMediaMotionFlags (* GetDetectionFlags)(void);
    /**< Gets CV detection flags for motion detection, representing object classes of interest for the user.
     *
     *   @return - a bitmap representing the user's object classes of interest.
     */

    void (* SetMinFramesMotionEventDuration)(u32 minFramesMotionEventDuration);
    /** Sets the minimum number of frames a motion event can last. Once an event starts it is guaranteed to not end
     *  until this number of frames is hit.
     *
     *  @param minFramesMotionEventDuration - the new minimum number of frames a motion event can last.
     */

    u32 (* GetMotionFrameCount)(void);
    /** Gets the total number of motion frames detected since counting started.
     *
     *  @return - the current total number of motion frames detected.
    */

    void (* ResetMotionFrameCount)(void);
    /** Reset the current detected motion frame count back to zero. */

    LTMediaMotionDetectionAlgorithm *(* GetActiveAlgorithm)(void);
    /** Return a pointer to the motion detection library's currently active motion algorithm object.
     *
     *  @note The caller must call lt_destroyobject() on their object pointer when they have finished using it. This
     *  does not destroy the underlying algorithm object until all other clients have also called lt_destroyobject() on
     *  their reference to the object.
     *
     *  @return - a pointer to the motion algorithm object being used by LTMediaMotionDetection.
     */
};

/*_____________________________________________________________________________________
  INLINE REGION LIST HELPER FUNCTIONS for struct LTMediaMotionDetection_Regions
    These helper functions assist in:
     o determining size requirements for LTMediaMotionDetection_Regions structs
     o allocating and cloning LTMediaMotionDetection_Regions structs
     o freeing LTMediaMotionDetection_Regions structs */

/** Returns the size requirement in bytes of struct LTMediaMotionDetection_Regions, given the maxRegions it should hold.
 *
 *  @param maxRegions the maximum number of regions the list will be able to hold
 *  @return the size in bytes required for a region list that can hold the specified number of maxRegions */
LT_INLINE LT_SIZE LTMediaMotionDetection_GetRegionListAllocationSize(u32 maxRegions) {
    return maxRegions ? maxRegions * sizeof(LTMediaMotionDetection_Region) + sizeof(u32) * 2 + sizeof(LTMediaResolution) : 0;
    /* YELLOW ALERT: Note the hardcoded (sizeof(u32) * 2) to account for the sizes of numRegions and maxRegions
     *               because sizeof(LTMediaMotionDetection_Regions) is compiler dependent with region[] at the end
     *               (Same as if it were region[0]. JUST MAKE SURE THIS FUNCTION IS UPDATED WHENEVER THE STRUCT CHANGES!
     *               and follow the LT padding rule - manually order members from large sizes to small sizes to ensure
     *               no padding gaps on both 32 and 64 bit systems exist, using u8 reserved[2] or whatever to pad when
     *               necessary. Never let the compiler pad for you! Never use __attribute(packed) either.
     *               The council has spoken. */
}

/** Mallocs a new LTMediaMotionDetection_Regions struct with  room to hold the specified number of maxRegions
 *  @param maxRegions the maximum number of regions the new regions list should hold
 *  @return a freshly allocated and zeroed region list
 *  @note it is the responsibility of the caller to call LTMediaMotionDetection_FreeRegionList to free
 *        the newly allocated region list that is returned from this function.
 *  @see LTMediaMotionDetection_FreeRegionList() */
LT_INLINE LTMediaMotionDetection_Regions * LTMediaMotionDetection_MallocateRegionList(LT_SIZE maxRegions) {
    LT_SIZE bytes = LTMediaMotionDetection_GetRegionListAllocationSize(maxRegions);
    LTMediaMotionDetection_Regions *regions = bytes ? lt_malloc(bytes) : NULL;
    if (regions) { lt_memset(regions, 0, bytes); regions->maxRegions = maxRegions; }
    return regions;
}

/** Clones a LTMediaMotionDetection_Regions struct into a newly allocated one.
 *  @param regions the region list to clone
 *  @return a new freshly allocated region list that is a clone of the specified region list
 *  @note it is the responsibility of the caller to call LTMediaMotionDetection_FreeRegionList to free
 *        the newly allocated region list that is returned from this function.
 *  @see LTMediaMotionDetection_FreeRegionList() */
LT_INLINE LTMediaMotionDetection_Regions * LTMediaMotionDetection_CloneRegionList(const LTMediaMotionDetection_Regions *regions) {
    LT_SIZE bytes = LTMediaMotionDetection_GetRegionListAllocationSize(regions ? regions->maxRegions : 0);
    LTMediaMotionDetection_Regions *regionsClone = bytes ? lt_malloc(bytes) : NULL;
    if (regionsClone) { lt_memcpy(regionsClone, regions, bytes); }
    return regionsClone;
}

/** Clones a LTMediaMotionDetection_HeatmapData struct into a newly allocated one.
 *  @param heatmapData the heatmap data to clone
 *  @return a new freshly allocated heatmap data that is a clone of the specified heatmap data
 *  @note it is the responsibility of the caller to call lt_free to free
 *        the newly allocated heatmap data that is returned from this function.
 *  @see lt_free() */
LT_INLINE LTMediaMotionDetection_HeatmapData * LTMediaMotionDetection_CloneHeatmap(const LTMediaMotionDetection_HeatmapData *heatmapData) {
    if (!heatmapData || !heatmapData->length || !heatmapData->address) {
        return NULL;
    }
    LTMediaMotionDetection_HeatmapData *heatmapDataClone = lt_malloc(sizeof(LTMediaMotionDetection_HeatmapData));
    if (heatmapDataClone) {
        heatmapDataClone->address = lt_malloc(sizeof(u8) * heatmapData->length);
        if (heatmapDataClone->address) {
            lt_memcpy(heatmapDataClone->address, heatmapData->address, sizeof(u8) * heatmapData->length);
            heatmapDataClone->length = heatmapData->length;
        } else {
            lt_free(heatmapDataClone);
            heatmapDataClone = NULL;
        }
    }
    return heatmapDataClone;
}

/** Frees allocated region lists, e.g. like those returned from %MallocateRegionList() and %CloneRegionList()
 *  @param regions the region list to free
 *  @note currently lists are alloced and freed in these functions with lt_malloc() and lt_free().
 *        The right to change this is reserved, therefore one should only use this function on lists returned
 *        by the mallocate and clone functions herein.
 *  @see LTMediaMotionDetection_MallocateRegionList(), LTMediaMotionDetection_CloneRegionList() */
LT_INLINE void LTMediaMotionDetection_FreeRegionList(LTMediaMotionDetection_Regions *regions) {
    lt_free(regions);
}

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMOTIONDETECTION_H */
