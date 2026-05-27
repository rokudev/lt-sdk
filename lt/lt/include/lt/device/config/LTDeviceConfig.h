/******************************************************************************
 * lt/include/lt/device/config/LTDeviceConfig.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICECONFIG_H
#define LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICECONFIG_H

#include <lt/core/LTCore.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_ROOT_INTERFACE(LTDeviceConfig, 1) {

    const char * (*GetDefaultNetTransport)(void);
        /**< gets the name of the network transport driver library that implements the default LTNetCore network transport *on this device */

    u32 (*GetNumNetTransports)(void);
        /**< gets the number of distinct network transports available for use on this device */

    const char * (*GetNetTransportAt)(u32 index);
        /**< gets the name of the network transport driver library at index index where valid values of index are from [0..GetNumNetTransports() -1] */

    u32 (*GetNumDevices)(void);
        /** Gets the number of device libraries available for use on this device. */

    const char * (*GetDeviceAt)(u32 index);
        /**< gets the name of the device library at index index where valid values of index are from [0..GetNumDevices() - 1] */

    u32 (*GetNumDrivers)(const char *deviceName);
        /**< Gets the number of driver libraries available for use with the named device library.
          *
          *  call %GetNumDrivers() to determine how many driver libraries are available for use with the
          *  specified device library.  Typically this function is called in the implementation of each
          *  device library to determine how many driver libraries it has available to load.
          *
          *  @param deviceName the name of the device library, e.g. "LTDeviceFlash"
          *  @return the number of driver libraries available to load for the specified device library
          */

    const char * (*GetDriverAt)(const char *deviceName, u32 index);
        /**< gets the name of individual driver libraries for a specified device
          *
          *  call %GetDriverAt() to get the name of each specific driver library available for use with
          *  the device library specified.  Typically this function is called in the implementation of each
          *  device library to determine the names of the driver libraries it has available to load.
          *
          *  @param deviceName the name of the device library, e.g. "LTDeviceFlash"
          *  @param index the index of the driver library to get the name of.  Valid values are [0..GetNumDrivers()-1]
          *  @return the name of the driver library to load
          *
          *  @note The return value is only valid while the LTDeviceConfig library is loaded.
          *
          */

    u32 (*GetDeviceSection)(const char *deviceName);
        /**< gets an identifier representing the config section in the device tree for the specified device library name
          *
          *  call %GetDeviceSection() to return an identifier for the section in the device tree
          *  that corresponds to the named device library
          *
          *  @param deviceName the name of the device library to get the device tree section for
          *  @return an identifier representing the section or 0 if no section exists for the specified device library name
          *
          *  @note The return value is only valid while the LTDeviceConfig library is loaded.
          *
          */

    u32 (*GetDriverSection)(const char *deviceName, const char *driverName);
        /**< gets an identifier representing the config section in the device tree for the specified driver
          *
          *  call %GetDriverSection() to return an identifier for the section in the device tree
          *  that corresponds to the named driver library for the specified device library
          *
          *  @param deviceName the name of the device library the driver is written for
          *  @param driverName the name of the driver library to get the device tree section for
          *  @return an identifier representing the section or 0 if no section exists for the specified device and driver names
          *
          */

    u32 (*GetNumDeviceUnits)(u32 driverSection);
        /**< gets the number of Device Unit configurations available for use with the driver at the section.
          *
          *  call %GetNumDeviceUnits() to determine how many Device Unit configurations are available for use
          *  with the driver library specified by the section.  Typically this function is called during
          *  initialization of a driver library to inform allocation of heap storage for Device-Unit-instance
          *  data structures.
          *
          *  @param driverSection the section corresponding to the driver configuration (use %GetDriverSection() to obtain)
          *  @return the number of Device Unit configurations provided by the driver library on the platform
          *
          *  @note The return value is only valid while the LTDeviceConfig library is loaded.
          *
          *  @see %GetDriverSection() %GetDeviceUnitSectionAt()
          *
          */

    u32 (*GetDeviceUnitSectionAt)(u32 driverSection, u32 index);
        /**< gets the section of the nth Device Unit configuration listed for driver configuration at the section.
          *
          *  call %GetDeviceUnitAt() to get the section of each specific Device Unit available for use with
          *  the driver library specified by the section.  Typically this function is called during the
          *  initialization of a driver library to ininitialize each individual Device Unit.
          *
          *  @param driverSection the section corresponding to the driver configuration (use %GetDriverSection() to obtain)
          *  @param index the index of the Device Unit corresponding to the configuration
          *  @return the section of the configuration information for the Device Unit, or 0 if configuration
          *          information for a Device Unit with the given index does not exist
          *
          *  @note The return value is only valid while the LTDeviceConfig library is loaded.
          *
          *  @see %GetDriverSection() %GetNumDeviceUnits()
          *
          */

    const char * (*ReadString) (u32 section, const char *key);
        /**< reads a string from the device tree
          *
          *  @param section the device tree section to read the string from
          *  @param key the key name of the variable to read
          *  @return the string value of the variable or NULL if no such variable exists
          *  @note the returned string is only valid while the LTDeviceConfig library is loaded
          *        either leave the library open, copy the string, or use and abandon the return value before closing the library
          */

    s64 (*ReadInteger)(u32 section, const char *key);
        /**< reads an integer from the device tree
          *
          *  @param section the device tree section to read the integer from
          *  @param key the key name of the integer variable to read
          *  @return the value of the variable or 0 if no such integer variable exists
          */

    const u8 * (*ReadBinary)(u32 section, const char *key, u32 *sizeToSet);
        /**< reads binary data from the device tree
          *
          *  @param section the device tree section to read the binary data from
          *  @param key the key name of the binary  variable to read
          *  @param sizeToSet a pointer to a u32 that will receive the size of the binary data or NULL if size information is not needed
          *  @return a pointer to the binary data in the device tree or NULL if the requested binary data variable does not exist
          *  @note the returned pointer is only valid while the LTDeviceConfig library is loaded
          *        either leave the library open, copy the data, or use the data and abandon the returned pointer before closing the library
          */

} LTLIBRARY_INTERFACE;


LT_INLINE LTDriverLibrary * LTDeviceConfig_OpenDriverLibForDevice(const char *deviceName, u32 index) {
    LTLibrary *driverLib = NULL;
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (deviceConfig) {
        const char *libName = deviceConfig->GetDriverAt(deviceName, index);
        if (libName && (driverLib = LT_GetCore()->OpenLibrary(libName)) && driverLib->GetInterfaceType() != kLTInterfaceType_DriverLibraryRoot) {
            lt_closelibrary(driverLib); driverLib = NULL;
        }
        lt_closelibrary(deviceConfig);
    }
    return (LTDriverLibrary *)driverLib;
}
    /**< quick and dirty inline helper function to load a driver library for a given device
      *
      *  @param deviceName the name of the device library for which to load the driver library
      *  @param index which driver library to load (typically 0, meaning the first or default driver for that device library)
      *  @return a pointer to opened driver library or NULL if no such library exists at index, or the library failed to load
      *  @note be sure to close the driver library when it is no longer needed
      */

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICECONFIG_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  30-Aug-22   aurelian    created
 *  24-Mar-23   augustus    doxygenated
 */
