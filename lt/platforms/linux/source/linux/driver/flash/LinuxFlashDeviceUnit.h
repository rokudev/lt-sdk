/*******************************************************************************
 * platforms/linux/source/linux/driver/flash/LinuxFlashDeviceUnit.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/device/flash/LTDeviceFlash.h>

/*__________________________________________________________________
 / LinuxSparseFlashDeviceUnit initialization and Handle Creation function */
void LinuxFlashDeviceUnit_Initialize(void);
void LinuxFlashDeviceUnit_Finalize(void);
LTDeviceUnit LinuxFlashDeviceUnit_CreateHandle(void);
