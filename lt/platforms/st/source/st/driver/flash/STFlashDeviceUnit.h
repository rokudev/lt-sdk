/*******************************************************************************
 * platforms/st/source/st/driver/flash/STFlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/device/flash/LTDeviceFlash.h>

/*__________________________________________________________________
 / STFlashDeviceUnit initialization and Handle Creation function */
void STFlashDeviceUnit_Initialize(void);
LTDeviceUnit STFlashDeviceUnit_CreateHandle(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Feb-26   augustus    created
 */
