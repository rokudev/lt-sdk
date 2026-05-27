/*******************************************************************************
 * <lt/device/powersubswitch/LTDevicePowerSubswitch.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltdevice_powersubswitch LTDevicePowerSubswitch
 * @ingroup ltdevice
 * @{
 *
 * @brief LTDevicePowerSubswitch - for power switching subsidiary devices.
 */

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_POWERSUBSWITCH_LTDEVICEPOWERSUBSWITCH_H
#define ROKU_LT_INCLUDE_LT_DEVICE_POWERSUBSWITCH_LTDEVICEPOWERSUBSWITCH_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/** LTDevicePowerSubswitch represents a power subswitch used to control power to
 *  to a subsidiary device, such as a secondary SOC whose power state is goverened
 *  by the primary SOC via, for example, control of a power IC connected to the primary
 *  SOC via GPIO, i2c, serial interface, etc.  LTDevicePowerSubswitch represents the
 *  power switches that control subsidiary devices connected in such a fashion.
 *
 *  To create an LTDevicePowerSubswitch for use, call lt_createdeviceobject(LTDevicePowerSubswitch, "<desired subswitch unit name>");
 *  For example, to create a subswitch instance for power controlling a subsidiary media soc, one would do as follows:
 *    LTDevicePowerSubswitch *subswitch = lt_createdeviceobject(LTDevicePowerSubswitch, "media.soc");
 *    ______
 *    CAVEAT: lt_createdeviceobject is not yet functional.  Until it is, use lt_createobject() and SetUnitName():
 *            LTDevicePowerSubswitch *subswitch;
 *            if ((subswitch = lt_createobject(LTDevicePowerSubswitch))) subswitch->API->SetUnitName(subswitch, "media.soc");
 *
 *  IMPORTANT: Multiple subswitch instances may be created for any single device subswitch unit.  If any one subswitch
 *             instance of a given subswitch unit is switched on, then the subsidiary device controlled by that subswitch unit
 *             is powered on and will remain powered on while any one subswitch instance of a subswitch unit remains switched on.
 *             Only when all subswitch instances of a given subswitch unit are switched off
 *             will the power to the subsidiary device be turned off.
 *
 */

typedef_LTObject(LTDevicePowerSubswitch, 1) {

    void         (* SetUnitName)(LTDevicePowerSubswitch *subswitch, const char *unitName);
        /**< sets the power subswitch unit name for the power subswitch instance.
         *
         * @param unitName the power subswitch device unit name to set
         *
         * @note unitName has a max character limit of 15. Any additional characters are ignored.  NULL and empty
         *       unitName strings have no effect and are ignored.  Once a unitName is set it cannot be changed.
         *
         * @note unit names are specified in a platform variant's LTDeviceConfig.json file as follows:
         *       Where N indexes such that /device/N/class == "LTDevicePowerSubswitch", unit names are given as:
         *           /device/N/unit/0/name,
         *          /device/N/unit/1/name,
         *         /device/N/unit/2/name, etc.
         *
         *       The device unit names available for any given device class may easily be determined programmatically
         *       at runtime using the LTDeviceKonfig object as follows: <pre>
         *       LTDeviceKonfig *konfig = lt_createobject(LTDeviceKonfig);
         *       u32 nUnits = konfig->API->GetNumDeviceUnits(konfig, "LTDevicePowerSubswitch");
         *       for (u32 i = 0; i < nUnits; i++) {
         *           const char *unitName = konfig->API->GetDeviceUnitAt(konfig, i, "LTDevicePowerSubswitch");
         *           lt_consoleprint("Found unit named: %s\n", unitName);
         *       }
         *       lt_destroyobject(konfig);
         *       / * note: strings retrieved from LTDeviceKonfig instances are only valid until the instance is destroyed * /
         *       / * note: LTDeviceKonfig will be renamed LTDeviceConfig once the legacy driver model is phased out. * /
         *       </pre>
         *
         *       Device unit names are commonly well known names, and are always platform agnostic.
         *       For example, in the Ranger product, the subsidiary SOC under power subswitch control is referred to
         *       by the generic unit name "media.soc" and not by any vendor or platform name or number.
         *       To reiterate, "media.soc" is used, None of "anyka", "anyka.soc", "ak3918", nor "ak3918.soc" names
         *       are suitable as unit names as those names are very platform/vendor specific.
         *
         * @note The function SetUnitName is temporary, and will be removed when lt_createdeviceobject is implemented
         *       or when lt_createobject can successfully take a unit name as a parameter.
         */

    const char * (* GetUnitName)(LTDevicePowerSubswitch *subswitch);
        /**< returns the device unit name of the power subsidiary subswitch
         *
         * @return the device unit name of the power subswitch
         *
         * @note the returned string is only valid for the lifetime of the power subswitch object instance
         *
         */

    void         (* SwitchOn)(LTDevicePowerSubswitch *subswitch);
        /**< turns this power subsidiary subswitch instance on
         *
         * Call SwitchOn to turn on or maintain the power on state of the subsidiary device unit.
         * If the subsidiary device unit is powered off, it will be powered on and it will remain powered on
         * while this subswitch instance is switched on.
         *
         * @see SwitchOff, TurnAllSwitchesOff
         *
         */
    void         (* SwitchOff)(LTDevicePowerSubswitch *subswitch);
        /**< turns this power subsidiary subswitch instance off
         *
         * Call SwitchOff to relinquish interest in maintaining power to the subsidiary device unit.
         * The subsidiary device unit will be powered off only when all power subswitch instances of its device
         * subswitch unit type are all turned off.
         *
         * @see SwitchOn, HowManySwitchesAreOn
         *
         */
    bool         (* SwitchOnIfDevicePowered)(LTDevicePowerSubswitch *subswitch);
        /**< turns this power subsidiary subswitch instance on iff the subsidiary device unit is already powered on
         *
         * Call SwitchOnIfDevicePowered to maintain the power on state of the subsidiary device unit when it is already powered on.
         * If the subsidiary device unit is powered off, this function will have no effect and will return false.
         * If the subsidiary device unit is already powered on, this subswitch instance will switch on, the function will return true
         * and the device will remain powered on while this subswitch instance is switched on.
         *
         * @param subswitch the subswitch instance to switch on if the device under control is already powered
         * @return true if the device was already on
         *
         * @see SwitchOn, SwitchOff, TurnAllSwitchesOff
         *
         */

    bool         (* IsSwitchOn)(LTDevicePowerSubswitch *subswitch);
        /**< determines whether or not this subswitch instance is switched on or not
         *
         * @return whether or not this subswitch instance is switched on
         *
         * @note The first instance of a power subswitch created for a particular device subswitch unit type
         *       will be created with it's subswitch either on or off, reflecting the state of the actual subsidiary device
         *       hardware under power control.  Subsuquent instances of the same device subswitch unit type will
         *       always be created in the subswitch off state.
         *
         * @note This function only reports that status of this subswitch instance, it does not give any information about
         *       power state of the subsidiary device under power control.  For that information, call %IsDevicePoweredOn
         * @see  IsDevicePoweredOn
         */

    bool         (* IsDevicePoweredOn)(LTDevicePowerSubswitch *subswitch);
        /**< determines whether or not the subsidiary device under power control is currently powered on or off
         *
         * Call %IsDevicePoweredOn to determine whether or not the actual device under power control os powered on or off
         *
         * @see IsSwitchOn
         */

    u32          (* HowManySwitchesAreOn)(LTDevicePowerSubswitch *subswitch);
        /**< determines how many power subswitch instances of the same device power subswitch unit type are currently switched on
         *
         * @return the number of power subswitch instances of the same device power subswitch unit type are currently switched on
         */

    void         (* TurnAllSwitchesOff)(LTDevicePowerSubswitch *subswitch);
        /**< turns off all power subswitch instances of the same subswitch unit type, unconditionally powering off the subswitched device unit
         *
         * Call TurnAllSwitchesOff() to unconditionally turn off the switches across all subswitch instances of the same device unit type.
         * This will cause an unconditional power off of the subswitched device unit hardware.
         *
         */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_DEVICE_POWERSUBSWITCH_LTDEVICEPOWERSUBSWITCH_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Mar-25   augustus    created
 *  06-May-25   augustus    added SwitchOnIfDevicePowered
 */
