/*******************************************************************************
 * platforms/macos/source/macos/driver/flash/MacOSFlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/device/flash/LTDeviceFlash.h>

/*__________________________________________________________________
 / MacOSFlashDeviceUnit initialization and Handle Creation function */
void MacOSFlashDeviceUnit_Initialize(void);
void MacOSFlashDeviceUnit_Finalize(void);
LTDeviceUnit MacOSFlashDeviceUnit_CreateHandle(void);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-20   augustus    created
 */
