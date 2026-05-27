/*******************************************************************************
 * platforms/common/source/common/driver/flash/CommonFlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/device/flash/LTDeviceFlash.h>

/*__________________________________________________________________
 / CommonFlashDeviceUnit initialization and Handle Creation function */
void CommonFlashDeviceUnit_Initialize(void);
LTDeviceUnit CommonFlashDeviceUnit_CreateHandle(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-20   augustus    created
 */
