/*******************************************************************************
 * <lt/device/thermometer/LTDeviceThermometer.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library providing access to temperature sensors
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_THERMOMETER_LTDEVICETHERMOMETER_H
#define LT_INCLUDE_LT_DEVICE_THERMOMETER_LTDEVICETHERMOMETER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

enum { /**< Returned by the Driver if there is a problem reading the sensor.
            This is an absurd temperature, well below absolute zero. */
       kLTDeviceThermometerErrorTemperature = -6000 };


typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceThermometer, 1) LTLIBRARY_EMPTY_INTERFACE;
    /**< DOCUMENTATION_NEEDED */

typedef_LTLIBRARY_INTERFACE(ILTThermometerDeviceUnit, 1) {

    s32 (* Read)(LTDeviceUnit hThermometerUnit);
        /**< Read the temperature from a temperature sensor.
         *   @param hThermometerUnit the Device Unit LTHandle of the Thermometer Device Unit
         *                           to access.  Call LTDeviceThermometer_CreateDeviceUnitHandle()
         *                           to obtain a Device Unit handle.
         *   @return temperature in degrees milligrade (0.1 degrees C). */

    /* Future expansion:
     *   Allow for:
     *   - Configuration of temperature sensors
     *   - Shutdown into low-power mode (if such mode switching cannot conveniently be
     *     added to create/destroy handle functions)
     *
     *   Currently, reading temperature is not a time-consuming activity (less than 1ms).
     *   It may become necessary to add a callback mechanism upon transaction completion. */
} LTLIBRARY_INTERFACE;
    /**< LTDeviceThermometer Device Unit Interface for temperature sensors. */

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_THERMOMETER_LTDEVICETHERMOMETER_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Jan-21   constantine created
 */
