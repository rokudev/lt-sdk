/*******************************************************************************
 * <lt/driver/powersubswitch/LTDriverPowerSubswitch.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/


#ifndef ROKU_LT_INCLUDE_LT_DRIVER_POWERSUBSWITCH_LTDRIVERPOWERSUBSWITCH_H
#define ROKU_LT_INCLUDE_LT_DRIVER_POWERSUBSWITCH_LTDRIVERPOWERSUBSWITCH_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/**
 * @defgroup ltdriver_powersubswitch LTDriverPowerSubswitch
 * @ingroup  ltdevice
 * @{
 * @brief LTDriverPowerSubswitch - driver for power switching subsidiary devices.
 *
 * LTDriverPowerSubswitch represents a power switch used to control power to
 * to a subsidiary device, such as a secondary SOC whose power state is goverened
 * by the primary SOC via, for example, control of a power IC that supplies power to
 * the secondary IOC, and is connected to the primary
 * SOC via GPIO, i2c, serial interface, etc.  LTDriverPowerSubswitch represents a single
 * Power on/off control power switch for the device under control.
 *
 * Driver implementations need not worry about thread safety.  Thread safety is guaranteed
 * at the device level.  Driver implementation objects will only be instantiated once per unit,
 * each unit instance will only be called to SetUnitName with a valid unit name, once only,
 * and driver implementations will only be called to PowerOn when power is off and to PowerOff
 * when power is on.
 * Note this relies on a functional IsDevicePoweredOn() function and that the power state
 * of a device doesn't change outside of the knowledge of the driver. */

/*____________________________________________
 / LTDriverPowerSubswitch driver object API */
typedef_LTObject(LTDriverPowerSubswitch, 1) {

    void         (* SetUnitName)(LTDriverPowerSubswitch *subswitch, const char *unitName);
        /**< sets the power subswitch unit name for the driver subswitch instance.
         *
         * @param unitName the power subswitch device unit name to set.
         *
         * @note unitName has a max character limit of 15. Any additional characters are ignored.  NULL and empty
         *       unitName strings have no effect and are ignored.  Once a unitName is set it cannot be changed.
         *
         * @note This function, SetUnitName, is temporary, and will be removed when lt_createdeviceobject is implemented
         *       or when lt_createobject can successfully take a unit name as a parameter.
         */

    const char * (* GetUnitName)(LTDriverPowerSubswitch *subswitch);
        /**< returns the device unit name of the power sub switch
         *
         * @return the device unit name of the power sub switch
         *
         * @note the returned string is only valid for the lifetime of the power subswitch object instance
         *
         */

    void         (* PowerOn)(LTDriverPowerSubswitch *subswitch);
        /**< Powers on the device under subswitch control
         * @see PowerOff, IsDevicePoweredOn
         *
         */
    void         (* PowerOff)(LTDriverPowerSubswitch *subswitch);
        /**< Powers off the device under subswitch control
         * @see PowerOn, IsDevicePoweredOn
         *
         */

    bool         (* IsDevicePoweredOn)(LTDriverPowerSubswitch *subswitch);
        /**< determines whether or not the device under driver power control is currently powered on or off
         *
         * Call %IsDevicePoweredOn to determine whether or not the actual device under power control is powered on or off
         *
         * @see IsSwitchOn
         */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_DRIVER_POWERSUBSWITCH_LTDRIVERPOWERSUBSWITCH_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Mar-25   augustus    created
 */
