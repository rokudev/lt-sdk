/*******************************************************************************
 * <lt/media/objectdetection/LTMediaObjectDetection.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Object Detection library - handles object detection on raw NV12 frame crops
 * for on-device object detection pipeline.
 *
 ******************************************************************************/

#include <lt/media/motiondetection/LTMediaMotionDetection.h>

#ifndef LT_INCLUDE_LT_MEDIA_OBJECTDETECTION_LTMEDIAOBJECTDETECTION_H
#define LT_INCLUDE_LT_MEDIA_OBJECTDETECTION_LTMEDIAOBJECTDETECTION_H

LT_EXTERN_C_BEGIN

/*******************************************************************************
 * Constants
 ******************************************************************************/

/* Fixed crop dimensions for object detection processing */
#define FIXED_CROP_WIDTH  480
#define FIXED_CROP_HEIGHT 360

/* Bounding box structure (pixel coordinates, x2/y2 are exclusive end) */
typedef struct {
    u32 x1, y1;  // top-left corner
    u32 x2, y2;  // bottom-right corner
} LTCropBBox;

/*******************************************************************************
 * Object interface
 ******************************************************************************/

typedef_LTObject(LTMediaObjectDetection, 1) {
    /* Process motion event from LTMediaMotionDetection */
    void (*RunObjectDetection)(LTMediaObjectDetection *objDet, LTMediaMotionDetection_Metadata *metadata);

    /* Get object detection results as LTMediaMotionFlags flags */
    LTMediaMotionFlags (*GetResults)(LTMediaObjectDetection *objDet);

    /* Reset object detection results to None */
    void (*ResetResults)(LTMediaObjectDetection *objDet);

} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* LT_INCLUDE_LT_MEDIA_OBJECTDETECTION_LTMEDIAOBJECTDETECTION_H */
