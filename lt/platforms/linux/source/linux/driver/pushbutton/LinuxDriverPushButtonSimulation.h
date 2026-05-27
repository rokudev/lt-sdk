/*****************************************************************************************
 * platforms/linux/source/linux/driver/pushbutton/LinuxDriverPushButtonSimulation.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Button-press and -release simulation for Unit Test
 *
 ****************************************************************************************/
/** @file LinuxDriverPushButtonSimulation.h Implementation of pushbutton driver for Linux
 */

#ifndef PLATFORMS_LINUX_SOURCE_LINUX_DRIVER_PUSHBUTTON_LINUXDRIVERPUSHBUTTONSIMULATION_H
#define PLATFORMS_LINUX_SOURCE_LINUX_DRIVER_PUSHBUTTON_LINUXDRIVERPUSHBUTTONSIMULATION_H

void (PushButtonISR)(u32 nPushButtonIndex, bool bPress);

bool BeginButtonSimulation(PushButtonISR * pPushButtonISR);
bool EndButtonSimulation(void);

#endif /* #ifndef PLATFORMS_LINUX_SOURCE_LINUX_DRIVER_PUSHBUTTON_LINUXDRIVERPUSHBUTTONSIMULATION_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Feb-21   constantine created
 */
