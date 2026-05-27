/*******************************************************************************
 * LTMediaMotionDetection library private header file
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_SOURCE_LT_MEDIA_LTMEDIAMOTIONDETECTION_PRIVATE_H
#define LT_SOURCE_LT_MEDIA_LTMEDIAMOTIONDETECTION_PRIVATE_H

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

LT_EXTERN_C_BEGIN

/* Global declarations shared between LTMediaMotionDetection source files. */

/** Global library handles and constants */
extern struct Globals {
    LTDeviceMedia *pDeviceMedia;

    /* Moxels (motion pixels) are rectangular (not necessarily square) subdivisions of a camera image used in
     * motion detection calculations. */
    u32 horizontalMoxels;
    u32 verticalMoxels;
} s_LTMediaMotionDetection_Private;

/* Global functions */
extern bool LTMediaMotionDetection_IsMotionZoneEnabled(void);
extern void GetMotionZoneBitmap(LTMediaMotionDetection_MotionZoneBitmap **pMotionZoneBitmap);
extern void AddMoxelToEnergyHistogram(u8 moxelValue, bool preDownsampling);
extern bool CheckHiddenIntegerSetting(LTSystemSettings *systemSettings, const char *settingName, s64 *value);

#define S s_LTMediaMotionDetection_Private

LT_EXTERN_C_END
#endif /* #ifndef LT_SOURCE_LT_MEDIA_LTMEDIAMOTIONDETECTION_PRIVATE_H */
