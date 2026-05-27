/*******************************************************************************
 * LT Object Detection library.
 *
 * Object Detection Library - handles YUV420 raw frame capture from video
 * driver for on-device object detection pipeline.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/media/objectdetection/LTMediaObjectDetection.h>
#include <lt/device/video/LTDeviceVideo.h>
#include <lt/media/motiondetection/LTMediaMotionDetection.h>

/*____________________________________
  LTMediaObjectDetection.c #defines */
DEFINE_LTLOG_SECTION("objdet");

// #define P(...) LTLOG_DEBUG(__VA_ARGS__)
#define P(...)
// #define PV(...) LTLOG_VERBOSE(__VA_ARGS__)
#define PV(...)

// Crop data structure
typedef struct {
    u32     width;          // Crop width
    u32     height;         // Crop height
    u8      *yData;         // Y plane data
    u8      *uvData;        // UV plane data
    u32     timestamp;      // Crop timestamp
} LTMediaObjectDetectionCroppedFrame;

/*_________________________________________________________________________________
  LTMediaObjectDetection.c static variables managed by LibInit/LibFini */
static LTOThread                          *s_hObjectDetectionThread = NULL;
static LTDeviceVideo                      *s_libVideo               = NULL;
static LTMediaObjectDetectionCroppedFrame *s_pCroppedFrame          = NULL;
static u8                                 *s_fixedCropBuffer        = NULL;
static u32                                s_fixedCropBufferSize     = 0;

/* Motion ROI tracking */
static u32 s_frameWidth;   // Will be set at runtime based on actual sensor resolution
static u32 s_frameHeight;  // Will be set at runtime based on actual sensor resolution
static u32 s_moxelWidth         = 32;  // Alta dimension
static u32 s_moxelHeight        = 24;  // Alta dimension
static u32 s_ROICenterX         = 0;
static u32 s_ROICenterY         = 0;
static u32 s_lastProcessingTime = 0;
static LTCropBBox s_cropBox    = {0, 0, FIXED_CROP_WIDTH, FIXED_CROP_HEIGHT};

/* Object detection configuration */
static u32 s_interval           = 1000; // Mock processing delay milliseconds
static bool s_bBusy             = false; // Whether pipeline is busy processing (prevents concurrent captures)
static LTMediaMotionFlags s_detectionResults = kLTMediaMotionFlags_All; // default results: all true for all objects

/* Use HD channel for better quality input to the cropping */
static LTDeviceVideo_Channel channel = kLTDeviceVideo_Channel_IspHD;

static inline u32 BBoxCenterX(const LTCropBBox *b){ return (b->x1 + b->x2) / 2; }
static inline u32 BBoxCenterY(const LTCropBBox *b){ return (b->y1 + b->y2) / 2; }

typedef_LTObjectImpl(LTMediaObjectDetection, LTMediaObjectDetectionImpl) {
} LTOBJECT_API;

/*________________________________________________
  Internal (static) functions */

/* Adjust crop region to ensure it's exactly FIXED_CROP_WIDTH x FIXED_CROP_HEIGHT
 * while keeping the bounding box center aligned with crop center when possible
*/
static void AdjustCropCentre(LTCropBBox *box) {
    /* Calculate original bounding box center */
    u32 bbox_center_x = BBoxCenterX(box);
    u32 bbox_center_y = BBoxCenterY(box);

    /* Calculate ideal crop region centered on bounding box */
    box->x1 = (bbox_center_x >= FIXED_CROP_WIDTH / 2) ? bbox_center_x - FIXED_CROP_WIDTH / 2 : 0;
    box->y1 = (bbox_center_y >= FIXED_CROP_HEIGHT / 2) ? bbox_center_y - FIXED_CROP_HEIGHT / 2 : 0;
    box->x2 = box->x1 + FIXED_CROP_WIDTH;
    box->y2 = box->y1 + FIXED_CROP_HEIGHT;

    // Clamp horizontally
    if (box->x2 > s_frameWidth) {
        u32 overflow = box->x2 - s_frameWidth;
        box->x1 = (box->x1 >= overflow) ? box->x1 - overflow : 0;
        box->x2 = box->x1 + FIXED_CROP_WIDTH;
    }
    // Clamp vertically
    if (box->y2 > s_frameHeight) {
        u32 overflow = box->y2 - s_frameHeight;
        box->y1 = (box->y1 >= overflow) ? box->y1 - overflow : 0;
        box->y2 = box->y1 + FIXED_CROP_HEIGHT;
    }

    s_ROICenterX = BBoxCenterX(box);
    s_ROICenterY = BBoxCenterY(box);

    LTLOG_DEBUG("pad.crop.region",
                "Bbox center (%u,%u) -> Crop region (%u,%u)-(%u,%u) size %ux%u",
                bbox_center_x, bbox_center_y, box->x1, box->y1, box->x2, box->y2,
                box->x2 - box->x1, box->y2 - box->y1);

}

/* Fill in the cropped frame data structure with current frame information */
static void FillCroppedFrameData(void) {
    if (!s_pCroppedFrame) {
        return;
    }

    /* Set cropped frame data in main fields */
    s_pCroppedFrame->width = FIXED_CROP_WIDTH;
    s_pCroppedFrame->height = FIXED_CROP_HEIGHT;
    s_pCroppedFrame->yData = s_fixedCropBuffer;
    s_pCroppedFrame->uvData = s_fixedCropBuffer + (s_pCroppedFrame->width * s_pCroppedFrame->height);
    s_pCroppedFrame->timestamp = (u32)LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime());
    LTLOG_DEBUG("capture.cropbox",
                        "Crop centered: box (%u,%u)-(%u,%u) center (%u,%u)",
                        s_cropBox.x1, s_cropBox.y1, s_cropBox.x2, s_cropBox.y2,
                        s_ROICenterX, s_ROICenterY);
}

/* Processing completion callback - called when simulated processing is done */
static void ObjectDetectionProcessingComplete(void *pClientData) {
    LT_UNUSED(pClientData);

    if (!s_bBusy) {
        return;
    }

    /* TODO: create a table to store object detection results for multiple frames */
    s_detectionResults = kLTMediaMotionFlags_All;

    /*
    * TODO: Replace this timer with actual ML processing
    * and call ObjectDetectionProcessingComplete() when processing finishes
    */
    /* Kill this timer to make it one-shot (prevent repeating) */
    s_hObjectDetectionThread->API->KillTimer(s_hObjectDetectionThread, ObjectDetectionProcessingComplete, NULL);

    /* Mark processing as complete */
    s_bBusy = false;
    LTLOG_DEBUG("processing.complete", "Object detection processing completed. Ready for next frame.");
}

static void ObjectDetectionCaptureProc(void *pClientData) {

    /* Start new processing with current metadata */
    LTMediaMotionDetection_Metadata *metadata = (LTMediaMotionDetection_Metadata *)pClientData;

    /* Start timing the processing */
    LTTime startTime = LT_GetCore()->GetKernelTime();

    if (metadata && metadata->regions && metadata->regions->numRegions > 0) {
        LTMediaMotionDetection_Region *pROI = &metadata->regions->region[0];

        /* Convert moxel coordinates to pixel coordinates */
        s_cropBox.x1 = (pROI->x1 * s_frameWidth)  / s_moxelWidth;
        s_cropBox.y1 = (pROI->y1 * s_frameHeight) / s_moxelHeight;
        s_cropBox.x2 = (pROI->x2 * s_frameWidth)  / s_moxelWidth;
        s_cropBox.y2 = (pROI->y2 * s_frameHeight) / s_moxelHeight;

        /* Shift and adjust the crop region to fixed size while centering on motion ROI */
        AdjustCropCentre(&s_cropBox);
    } else {
        LTLOG_DEBUG("metadata.no.roi", "No ROI data in metadata - using previous ROI");
    }

    // captureLen = -1 if CaptureCrop fails
    s32 captureLen = s_libVideo->CaptureCrop(
        channel,
        kLTDeviceVideo_Frame_Yuv420,
        s_fixedCropBuffer,
        FIXED_CROP_WIDTH, FIXED_CROP_HEIGHT, s_ROICenterX, s_ROICenterY
    );

    if (captureLen > 0) {
        /* Fill the crop data that will be passed to NPU */
        FillCroppedFrameData();

        /* Calculate and update processing time (before event notification) */
        LTTime endTime = LT_GetCore()->GetKernelTime();
        s_lastProcessingTime = (u32)LTTime_GetMicroseconds(LTTime_Subtract(endTime, startTime));
        s_hObjectDetectionThread->API->SetTimer(s_hObjectDetectionThread, LTTime_Milliseconds(s_interval),
                                                ObjectDetectionProcessingComplete, NULL, NULL);

        LTLOG("capture.frame.success", "Frame center (%u,%u), %u bytes, took %u us",
              s_ROICenterX, s_ROICenterY, captureLen, s_lastProcessingTime);

    } else {
        /* Capture error */
        s_bBusy = false;
        LTLOG_DEBUG("capture.frame.error", "Frame capture failed with error");
    }
}

static bool LTMediaObjectDetection_ThreadInit(void) {
    LTLOG_DEBUG("thread.init.success", "Object detection thread initialized successfully");
    return true;
}

static void LTMediaObjectDetection_ThreadExit(void) {
    /* Kill any pending timer and mark motion as inactive */
    s_hObjectDetectionThread->API->KillTimer(s_hObjectDetectionThread, ObjectDetectionProcessingComplete, NULL);
    LTLOG_DEBUG("thread.exit", "Object detection thread exited");
}


/*___________________________________________________
  Object/Library API functions (externally accessible) */
// New API function for LTMediaMotionDetection to call when motion is detected
static void LTMediaObjectDetectionImpl_RunObjectDetection(LTMediaObjectDetectionImpl *objDet, LTMediaMotionDetection_Metadata *metadata) {
    LT_UNUSED(objDet);

    /* Check if we're busy processing */
    if (s_bBusy) {
        LTLOG_DEBUG("object.detection.busy", "Skipping motion event");
        return;
    }

    /* Set busy flag to prevent concurrent processing */
    s_bBusy = true;

    /* Queue the processing task to run on the object detection thread */
    s_hObjectDetectionThread->API->QueueTaskProcIfRequired(s_hObjectDetectionThread, ObjectDetectionCaptureProc, NULL, metadata);
}

static LTMediaMotionFlags LTMediaObjectDetectionImpl_GetResults(LTMediaObjectDetectionImpl *objDet) {
    LT_UNUSED(objDet);
    return s_detectionResults;
}

static void LTMediaObjectDetectionImpl_ResetResults(LTMediaObjectDetectionImpl *objDet) {
    LT_UNUSED(objDet);
    ObjectDetectionProcessingComplete(NULL);
}

static bool LTMediaObjectDetectionImpl_ConstructObject(LTMediaObjectDetectionImpl *objDet) {
    LT_UNUSED(objDet);
    return true;
}

static void LTMediaObjectDetectionImpl_DestructObject(LTMediaObjectDetectionImpl *objDet) {
    LT_UNUSED(objDet);
}

static void LTMediaObjectDetection_LibFini(void);  // Forward declaration

static bool LTMediaObjectDetection_LibInit(void) {
    do {
        s_pCroppedFrame = lt_malloc(sizeof(LTMediaObjectDetectionCroppedFrame));
        if (!s_pCroppedFrame) break;

        /* Allocate fixed crop buffer - for standard fixed cropping */
        s_fixedCropBufferSize = FIXED_CROP_WIDTH * FIXED_CROP_HEIGHT * 3 / 2;  // NV12 format
        s_fixedCropBuffer = lt_malloc(s_fixedCropBufferSize);
        if (!s_fixedCropBuffer) break;

        if (!(s_libVideo = lt_openlibrary(LTDeviceVideo))) break;

        /* Get actual frame resolution at runtime - needed for Taft's different sensor (1536x1536) */
        LTMediaResolution hdResolution = {0};
        if (!s_libVideo->GetParam(kLTDeviceVideo_Param_ResolutionHD, &hdResolution)) {
            LTLOG_YELLOWALERT("lib.init.resolution.fail", "Failed to get HD resolution from video device");
            break;
        }
        s_frameWidth = hdResolution.width;
        s_frameHeight = hdResolution.height;
        LTLOG("lib.init.resolution", "Detected frame resolution: %ux%u", s_frameWidth, s_frameHeight);

        if (!(s_hObjectDetectionThread = lt_createobject(LTOThread))) break;

        if (!s_hObjectDetectionThread->API->StartSynchronous(s_hObjectDetectionThread, "ObjectDetection", LTMediaObjectDetection_ThreadInit, LTMediaObjectDetection_ThreadExit)) break;

        LTLOG("lib.init.success", "Object detection library initialized and started");
        return true;

    } while(false);

    LTLOG_YELLOWALERT("lib.init.fail", "Failed to initialize LTMediaObjectDetection");
    LTMediaObjectDetection_LibFini();
    return false;
}

static void LTMediaObjectDetection_LibFini(void) {
    /* Stop and cleanup object detection thread */
    if (s_hObjectDetectionThread) {
        s_hObjectDetectionThread->API->Terminate(s_hObjectDetectionThread);
        s_hObjectDetectionThread->API->WaitUntilFinished(s_hObjectDetectionThread, LTTime_Infinite());
        lt_destroyobject(s_hObjectDetectionThread);
        s_hObjectDetectionThread = NULL;
    }

    lt_closelibrary(s_libVideo);
    s_libVideo = NULL;

    lt_free(s_pCroppedFrame);
    s_pCroppedFrame = NULL;

    lt_free(s_fixedCropBuffer);
    s_fixedCropBuffer = NULL;
    s_fixedCropBufferSize = 0;

    LTLOG("lib.cleanup.success", "Object detection library cleaned up");
}

define_LTObjectImplPublic(LTMediaObjectDetection, LTMediaObjectDetectionImpl,
    RunObjectDetection,
    GetResults,
    ResetResults,
);

define_LTObjectLibrary(1, LTMediaObjectDetection_LibInit, LTMediaObjectDetection_LibFini);
