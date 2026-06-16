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

#ifndef EYECLOPS_EYECLOPS_CONNECTION_H
#define EYECLOPS_EYECLOPS_CONNECTION_H

#include "EyeClops.h"
#include <lt/net/core/LTNetCore.h>

typedef_LTObject(EyeClopsConnection, 1) {
    void                        (* Initialize)(EyeClopsConnection *connection, EyeClops *eyeclops, LTSocket hSocket);
    void                        (* FirstFrameAvailable)(EyeClopsConnection *connection);
    void                        (* ReadReady)(EyeClopsConnection *connection);
    void                        (* WriteReady)(EyeClopsConnection *connection);
    LTSocket                    (* GetSocket)(EyeClopsConnection *connection);
    LTDeviceVideo_VideoData *   (* GetFrameInUse)(EyeClopsConnection *connection);
    EyeClopsConnection *        (* GetNext)(EyeClopsConnection *connection);
    void                        (* SetNext)(EyeClopsConnection *connection, EyeClopsConnection *next);

} LTOBJECT_API;

#endif /* #ifndef EYECLOPS_EYECLOPS_CONNECTION_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-26   augustus    created
 */
