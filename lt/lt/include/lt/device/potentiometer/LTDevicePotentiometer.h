/*******************************************************************************
 * <lt/device/potentiometer/LTDevicePotentiometer.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library providing access to potentiometer
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_POTENTIOMETER_LTDEVICEPOTENTIOMETER_H
#define LT_INCLUDE_LT_DEVICE_POTENTIOMETER_LTDEVICEPOTENTIOMETER_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/**< Error codes */
typedef enum {
    kLTDevicePotentiometer_Success         = 0,
    /**< Success */
    kLTDevicePotentiometer_DeviceBusy      = 1,
    /**< Device busy - write in progress (retry) */
    kLTDevicePotentiometer_InvalidHandle   = 2,
    /**< Invalid LTDeviceUnit handle */
    kLTDevicePotentiometer_ReadError       = 3,
    /**< I2C read failed */
    kLTDevicePotentiometer_WriteError      = 4,
    /**< I2C write failed */
} LTDevicePotentiometerResult;


typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDevicePotentiometer, 1) LTLIBRARY_EMPTY_INTERFACE;

typedef_LTLIBRARY_INTERFACE(ILTDriverPotentiometer, 1) {

    LTDevicePotentiometerResult (* SetDPOTValue)(LTDeviceUnit hDeviceUnit, u32 value);
        /**< Sets the value of the DPOT register for the given device unit
         *  @param hDeviceUnit the handle to the device unit
         *  @param value the DPOT value to set
         *  @return an LTDevicePotentiometerResult code */

    u32 (* GetDPOTValue)(LTDeviceUnit hDeviceUnit);
        /**< Gets the value of the DPOT register for the given device unit
         *  @param hDeviceUnit the handle to the device unit
         *  @return the DPOT value */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_POTENTIOMETER_LTDEVICEPOTENTIOMETER_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Jan-22   vitellius   created
 */
