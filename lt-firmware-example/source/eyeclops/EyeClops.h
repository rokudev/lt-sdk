/*******************************************************************************
 * lt-firmware-example/source/eyeclops/EyeClops.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * library private header file for EyeClops Object
 *******************************************************************************/

#ifndef EYECLOPS_EYECLOPS_H
#define EYECLOPS_EYECLOPS_H

#include <lt/device/video/LTDeviceVideo.h>

typedef struct EyeClopsConnection EyeClopsConnection;

typedef_LTObject(EyeClops, 1) {
    LTDeviceVideo_VideoData *   (* GetLatestFrame)(EyeClops *eyeclops);
    void                        (* ReclaimVideoData)(EyeClops *eyeclops, LTDeviceVideo_VideoData *videoData);
    void                        (* TerminateConnection)(EyeClops *eyeclops, EyeClopsConnection *connection);
} LTOBJECT_API;

#endif /* #ifndef EYECLOPS_EYECLOPS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-26   augustus    created
 */
