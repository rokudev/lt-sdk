/*******************************************************************************
 * <lt/device/pir/LTDevicePIRDefs.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_PIR_LTDEVICEPIRDEFS_H
#define LT_INCLUDE_LT_DEVICE_PIR_LTDEVICEPIRDEFS_H

typedef struct {
    u8 nDriverId;
    /**< an ID that is sent to the driver by the device in the StartMotionDetection() call to identify the driver */
    u8 nDeviceIndex;
    /**< the driver's device index that detected the change in motion */
    u8 bMotion;
    /**< true of there was motion detected; false otherwise */
    u8 RSVD;
    /**< reserved. Added for alignment and not used */
} LTDevicePIRMotionEvent;
/**< This struct is sent from the driver to the device in the client data parameter of the TaskProc */

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_PIR_LTDEVICEPIRDEFS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Feb-22   vitellius   created
 */
