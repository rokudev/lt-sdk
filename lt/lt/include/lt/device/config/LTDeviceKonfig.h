/******************************************************************************
 * lt/include/lt/device/config/LTDeviceKonfig.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************
 *
 * NOTE NOTE NOTE: This file declares an LTObject called LTDeviceKonfig.
 *    FYI FTI FYI: Plan is to rename it to LTDeviceConfig when the phase-out
 *                 of the existing LTDeviceConfig is complete.
 *
 * @defgroup ltdevice_config LTDeviceKonfig
 * @ingroup  ltdevice
 * @{
 * @brief LTDeviceKonfig object for accessing the platform's supported device hardware and configuration thereof
 * _____________________________________________________________________________________________________________
 * USAGE
 *
 *   The LTDeviceKonfig object is used to obtain device class, device unit, and device driver configuration values
 *   at runtime.  Since reading configuration variables for each device unit is typically done only once when
 *   the device unit is first accessed, it is recommended that device classes and drivers do not Initialize
 *   all units on startup, nor enumerate what units are available on startup, because this information is only
 *   needed when the unit is initialized.  Drivers should not enumerate  units and create static structures
 *   with the values copied.  They should wait until someone instantiates a driver unit class and then in the
 *   driver unit constructor (or anywhere else one might want to use LTDeviceKonfig), create an LTDeviceKonfig object,
 *   read and use the values you need, and then destroy it.  It has been designed to operate efficiently
 *   in this manner:<pre>
 *      LTDeviceKonfig *config = lt_createobject(LTDeviceKonfig); / * always available, no penalty to construct  * /
 *      config->API->CallSomeApiFunctions();                      / * NULL check not needed, just create and use * /
 *      lt_destroyobject(config);                                 / * and destroy when done - no penalty to destruct * / </pre>
 * _____________________________________________________________________________________________________________
 * FUNCTIONS and BENEFITS
 *
 *     A. For Application object developers:
 *        1. Enumerate  platform supported device classes and units with the functions: %GetNumDevices(), %GetDeviceAt(), %GetNumUnits(), and %GetUnitAt().
 *           @see GetNumDevices, GetDeviceAt, GetNumUnits, GetUnitAt
 *        2. Applications rarely use LTDeviceKonfig; what they do is create LTDevice objects with or without specifying unit name.
 *           i.   App Developers use lt_createobject(deviceclass) to create a device object that operates its platform default device unit
 *           ii.  App Developers use lt_createdriverobject(deviceClass, "unitName") to create a device object that operates a specifically named unit
 *
 *     B. For LTDevice object developers:
 *        1. Automatically create the proper driver object for any device unit given only its device class and unit name.
 *             LTDriverWiFi *driverForWLAN0           = (LTDriverWiFi *)config->API->CreateDriverObject("LTDeviceWiFi", "WLAN0");
 *             LTDriverWiFi *driverForDefaultWiFiUnit = (LTDriverWiFi *)config->API->CreateDriverObject("LTDeviceWiFi", NULL);  / * pass NULL unit name for default unit * /</pre>
 *             @see CreateDriverObject
 *        2. Easily retrieve LTDevice specific platform configuration parameters (rare if never, config params typically associated with driver not device).
 *           @see GetDeviceSection, ReadString, ReadInteger, ReadBinary
 *
 *     C. For LTDriver object implementers:
 *        1. Easily retrieve driver specific and unit specific platform configuration parameters.
 *           @see GetDriverLibrarySection, GetDriverUnitSection, ReadString, ReadInteger, ReadBinary
 *
 *     @note %ReadString() and %ReadBinary() return pointers to data that is only valid for the lifetime of
 *           the LTDeviceKonfig object instance.  To use LTDeviceKonfig returned string and binary data properly,
 *           you must only do one or more of the following three things (ordered by preference):
 *             a. Use the string or binary data completely (stop accessing it) before destroying the LTDeviceKonfig object.
 *             b. Hold on to the LTDeviceKonfig object and don't destroy it until your library closes.
 *                As your library is closing, you no longer need access the binary or string data and you then
 *                destroy the LTDeviceKonfig object. (This is just a long variant of item a.)
 *             c. Create, use and Destroy the LTDeviceKonfig object as recommended in the USAGE section above,
 *                but copy any string or binary data you wish to preserve into your own buffer before destroying
 *                the LTDeviceKonfig object.
 *                596F752068617665206E6F206368616E636520746F20737572766976652C206D616B6520796F75722074696D652E
 */

#ifndef LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICEKONFIG_H
#define LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICEKONFIG_H

#include <lt/core/LTCore.h>
LT_EXTERN_C_BEGIN

typedef_LTObject(LTDeviceKonfig, 1) { /* temporary name */

    /* Getting an LTMemoryRegion by name */
    LTMemoryRegion (* GetNamedMemoryRegion)(LTDeviceKonfig *konfig, const char *name);
        /**< gets an LTMemoryRegion by name
         *
         *   This function gets an LTMemoryRegion by name that is suitable for
         *   passing into lt_malloc_from_region.  If the region doesn't exist
         *   then the LTMemoryRegion returned will cause lt_malloc_from_region
         *   to behave as if lt_malloc was called.
         *
         *   @param konfig the LTDeviceKonfig object
         *   @param name the name of the memory region to get
         *   @return the LTMemoryRegion if found, otherwise (LTMemoryRegion)0;
         *
         *   @note This function reads memory region values specified in LTDeviceConfig.json in the form:<pre>
         *         {
         *           "memory": {
         *                       "regions": [ { "name": "audio", "region": 1 },
         *                                    { "name": "video", "region": 2 }
         *                                  ]
         *                     }
         *         }
         *         </pre>
         *         The region numbers must be valid indices into reserved memory regions defined
         *         in the LTHeapConfig struct in the platform variant's LTCoreBSP implementation.
         */

    /* Enumerating device classes and device units */
    u32          (* GetNumDeviceClasses)(LTDeviceKonfig *konfig);
    const char * (* GetDeviceClassNameAt)(LTDeviceKonfig *konfig, u32 deviceClassIndex);
    u32          (* GetNumDeviceUnits)(LTDeviceKonfig *konfig, u32 deviceClassIndex);
    const char * (* GetDeviceUnitNameAt)(LTDeviceKonfig *konfig, u32 deviceClassIndex, u32 deviceUnitIndex);

    /* obtaining device class and device unit indices (by order in LTDeviceConfig.json */
    bool         (* GetDeviceClassIndex)(LTDeviceKonfig *konfig, const char *deviceClassName, u32 *deviceClassIndexToSet);
    bool         (* GetDeviceUnitIndex)(LTDeviceKonfig *konfig, u32 deviceClassIndex, const char *deviceUnitName, u32 *deviceUnitIndexToSet);
    bool         (* GetDeviceUnitIndices)(LTDeviceKonfig *konfig,
                                          const char *deviceClassName, const char *deviceUnitName,
                                          u32 *deviceIndexToSet, u32 *deviceUnitIndexToSet);
                    /**< gets the indices for device class and device unit
                     *
                     *   This function is primarily a convenience function for LTCore to aid in the
                     *   proper construction and management of device objects and driver objects created using
                     *   lt_createdeviceobject() and lt_createdriverobject_fordevice().  It may be
                     *   of interest to other clients for calling other LTDeviceKonfig functions that
                     *   take indices as an alternative to names.  (Lookup by index is slightly faster).
                     *
                     *   @param konfig the LTDeviceKonfig object
                     *   @param deviceClassName the device class name being queried
                     *   @param deviceUnitName  the device class' unit name being queried
                     *   @param deviceIndexToSet a pointer to a u32 that will receive the device index
                     *   @param deviceUnitIndexToSet a pointer to a u32 that will receive the device unit index
                     *   @return true if the device and unit were found and all indices were set; false otherwise
                     */

    bool         (* GetDevicePassThruAPI)(LTDeviceKonfig *konfig, u32 deviceClassIndex);
        /**< gets whether or not the device has a passthru api to the driver
         *
         * @param konfig            the LTDeviceKonfig object
         * @param deviceClassIndex  the index of the device class
         * @return             whether or not the device has a pass-through api to the driver
         *
         * @note               This function is used in the implementation of lt_createdeviceobject
         *                     It is likely of only curious interest to other clients.
         *
         * @note               A device has a pass thru api when /device/n/api: is "passthru"
         */

/* Determining whether or not a device unit requires exclusive access

   If exclusive access is true; only the first lt_createdeviceobject() call for the device/unit combo will succeed,
   available exclusively only to that caller until the device object is lt_destroyed
   at which point it becomes creatable again exclusively for the next lt_createdeviceobject() caller
   on that device/unit combo.

   If exclusive access is false; multiple lt_createdeviceobject() calls for the same device/unit
   will return the singleton device object instance for that device/unit combo with reference count
   incremented.  The singleton device object for that device/unit combo will only be destroyed
   when all callers of lt_createdeviceobject() call lt_destry on the object.

   Non-exclusive access is the default.   To mark a device unit with exclusive access, add the
   json key value pair: "access" : "exclusive" (all lower case) to /device/N/unit/M/, e.g.
       { device: [ { "class": "LTDeviceKeypad",
                      "unit": [ {   "name": "KEYPAD1",
                                  "driver": "MockLinuxDriverKeypad",
                                  "access": "exclusive"
       }         ] }          ] }
   */
   bool         (* GetDeviceUnitExclusivity)(LTDeviceKonfig *konfig, u32 nDeviceIndex, u32 nUnitIndex);
        /**< gets whether or not the device unit features exclusive access
         *
         * @param konfig       the LTDeviceKonfig object
         * @param nDeviceIndex the index of the device class
         * @param nUnitIndex   the index of the device class' device unit
         * @return             whether or not the device unit features exclusive access
         *
         * @note               This function is used in the implementation of lt_createdeviceobject
         *                     device units' exclusivity requirements.  It is likely of only curious
         *                     interest to other clients.  lt_createdeviceobject() will succeed or return
         *                     NULL and that is all most clients need to know.
         */

   const char * (* GetDeviceUnitDriver)(LTDeviceKonfig *konfig, u32 nDeviceIndex, u32 nUnitIndex);
        /**< gets the object name of the driver for the device unit
         *
         * @param konfig       the LTDeviceKonfig object
         * @param nDeviceIndex the index of the device class
         * @param nUnitIndex   the index of the device class' device unit
         * @return             the name of the driver for the device unit
         *
         * @note               This function is used in the implementation of lt_createdriverobject_fordevice
         *                     It is likely of only curious interest to other clients.
         */

/* Determining whether or not a device class or device unit is present */
    bool         (* DeviceClassIsPresent)(LTDeviceKonfig *konfig, const char *deviceClass);
    bool         (* DeviceUnitIsPresent)(LTDeviceKonfig *konfig, const char *deviceClass, const char *unitName);



 /* Reading device unit config variables */
    u32          (* GetDeviceUnitConfigSectionByName)(LTDeviceKonfig *konfig, const char *deviceClass, const char *unitName);
        // returns device unit config section for /device/N/unit/M/config where /device/N/class == deviceClass and /device/N/unit/M/name == unitName

    u32          (* GetDeviceUnitConfigSectionByNameAndIndex)(LTDeviceKonfig *konfig, const char *deviceClass, u32 nUnitIndex);
        // returns device unit config section for /device/<deviceClassName>/unit/nUnitIndex/config

    u32          (* GetDeviceUnitConfigSectionByIndex)(LTDeviceKonfig *konfig, u32 nDeviceIndex, u32 nUnitIndex);
        // returns device unit config section for /device/nDeviceIndex/unit/nUnitIndex/config; faster than ByName

    u32          (* GetDeviceUnitConfigSectionForDeviceOrDriverObject)(LTDeviceKonfig *konfig, LTObject *deviceOrDriverObject);
        // returns device unit config section for the device unit being operated by a device or driver object

    u32          ( * GetArraySubObjectSectionWithName)(LTDeviceKonfig *konfig, u32 configSection, const char *arrayKey, const char *name);
        // gets the object with name key name, e.g. { name: <name> }, from of array objects with key arrayKey
        // e.g. for example, to get the object with specific name out of the device unit config section's array with key arrayKey:
        //      if device unit config section contains an array called items: <pre>
        //         / * items: [ { name: "foo", data: "foodata" }, { name: "bar", data: "bardata" } * /
        //
        //          u32 section = konfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(konfig, deviceOrDriverObject);
        //          u32 namedFooObjectSectionFromItemsArray = konfig->API->GetArraySubObjectSectionWithName(konfig, section, "items", "foo"); / * returns the object with name:"foo" from items array * /
        //          const char *dataString = konfig->API->ReadString(konfig, namedFooObjectSectionFromItemsArray, "data") / * returns * / "fooData"
        //      </pre>

    u32          ( * GetObjectSection)(LTDeviceKonfig *konfig, u32 section, const char *objectKey);
        // gets the object section with objectKey,
        // e.g. for example, to get the object section with key "foo" out of the configSection:<pre>
        //         / * config: { foo: { data: "foodata" }, " "bar" : { data: "bardata" } } * /
        //
        //          u32 section = konfig->API->GetDeviceUnitConfigSectionForDeviceOrDriverObject(konfig, deviceOrDriverObject);
        //          u32 fooObjectSection = konfig->API->GetObjectSection(konfig, section, ""foo"); / * returns the object with name:"foo" from items array * /
        //      </pre>

    const char * (*ReadString) (LTDeviceKonfig *konfig, u32 configSection, const char *key);
        /**< reads a string from a device unit's config section
          *
          *  @param konfig the konfig
          *  @param configSection the device unit config section to read from
          *  @param key the key name of the string variable to read
          *  @return the string value of the variable or NULL if no such string variable exists
          *  @note the returned string is only valid while the LTDeviceKonfig object is instantiated
          *        and must not be accessed after the object is destroyed.
          *  @note configSection must be a valid section returned by GetDeviceClassConfigSection or GetDeviceUnitConfigSection
          *  @see GetDeviceClassConfigSection, GetDeviceUnitConfigSection
          */

    s64 (*ReadInteger)(LTDeviceKonfig *konfig, u32 configSection, const char *key);
        /**< reads an integer from a device unit's config section
          *
          *  @param konfig the konfig
          *  @param configSection the device unit config section to read from
          *  @param key the key name of the integer variable to read
          *  @return the value of the variable or 0 if no such integer variable exists
          *  @note if 0 is returned, one can determine if the integer variable exists as 0 or doesn't exist by calling ReadValueType():<pre>
          *        bool bIntegerVariableExists = (kLTResourceValueType_Integer == konfig->API->ReadValueType(konfig, key));
          *        </pre>
          *  @note configSection must be a valid section returned by GetDeviceClassConfigSection or GetDeviceUnitConfigSection
          *  @see GetDeviceClassConfigSection, GetDeviceUnitConfigSection, ReadValueType
          */

    const u8 * (*ReadBinary)(LTDeviceKonfig *konfig, u32 configSection, const char *key, u32 *sizeToSet);
        /**< reads binary data from a device unit's config section
          *
          *  @param configSection the device unit config section to read from
          *  @param key the key name of the binary  variable to read
          *  @param sizeToSet a pointer to a u32 that will receive the size of the binary data or NULL if size information is not needed
          *  @return a pointer to the binary data in the device tree or NULL if the requested binary data variable does not exist
          *  @note the returned pointer is only valid while the LTDeviceKonfig object is instantiated
          *        and must not be accessed after the object is destroyed.
          *  @note configSection must be a valid section returned by GetDeviceClassConfigSection or GetDeviceUnitConfigSection
          *  @see GetDeviceClassConfigSection, GetDeviceUnitConfigSection
          */

    u32 (*GetNumArrayElements)(LTDeviceKonfig *konfig, u32 configSection, const char *key);
        /**< gets the number of array elements in an array variable from a device unit's config section
          *
          *  @param configSection the device unit config section to read from
          *  @param key the key name of the array variable to get the number of elements of
          *  @return the number of elements in the array or 0 if no such array exists
          *  @note configSection must be a valid section returned by GetDeviceClassConfigSection or GetDeviceUnitConfigSection
          *  @see GetDeviceClassConfigSection, GetDeviceUnitConfigSection
          */

    LTResourceValueType (*ReadValueType)(LTDeviceKonfig *konfig, u32 configSection, const char *key);
        /**< reads the value type of a variable from a device unit's config section
          *
          *  @param configSection the device unit config section to read from
          *  @param key the key name of the variable to read the value type from
          *  @return the LTResourceValueType type of the variable
          */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_CONFIG_LTDEVICEKONFIG_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Apr-25   augustus    created
 */

/*
 *    _________________________________________________________________________
 ===> SPECIFICATION: LTDeviceConfig.json contents for new object based driver model
 *
 *    The DeviceConfig.json file contains a top level json object with 2 required elements, as described below.
 *    Note: * indicates optional element; all others required.
 *
 * -- File: DeviceConfig.json
 *     { -- TOP LEVEL: 1 required element, "device": [ARRAY of OBJECTS {}, {}, {} ,...]; one optional element, "extra-libs": [ARRAY of STRINGS "", "", ...]
 *         "device": [ -- required array of device specification objects, each with 2 required elements { class: "STRING", "unit": [ARRAY of OBJECTS {}, {}, ...] }
 *             { -- device 0
 *                 "class"      : "<device-class>",                -- required device class name (aka object/api name), e.g.  "LTDeviceWatchdog"; note: must contain substring "Device" in name
 *                *"device-lib" : "<device-lib-name>"              -- optional name of lib containing device object impl; RECOMMENDATION: do not specify; will use device-class as lib name
 *                *"driver-api" : "<driver-api-name>",             -- optional driver api name; use device-class to bypass device object and go driver direct; if unspecified, replaces device class substring "Device" with "Driver" for driver api name, e.g. LTDeviceWatchdog -> LTDriverWatchdog
 *                *"config"     : {<class specific config data>}   -- optional device class configuration specific to device class operation, not specific nor relevant to device driver unit configuration
 *                 "unit        : [                                -- required array of [at least 1] device unit objects, each with 2 required elements { name: "STRING", "driver": "STRING" }
 *                     { -- unit 0 (default unit)
 *                         "name"       : "<unit-name>",           -- short unit name, e.g. Wlan0, max 15 characters
 *                         "driver"     : "<driver-object-name>",  -- the name of the driver object specialization that operates this unit, e.g. "s91xDriverWiFi"
 *                        *"driver-lib" : "<driver-lib-name>",     -- optional name of library that contains driver object implementation, if unspecified, same as driver, e.g. "s91xDriverWiFi"
 *                        *"config"     : {<unit specific data>}   -- optional unit specific configuration data
 *                     },
 *                    *{ -- unit 1, (additional units optional)
 *                         "name"       : "<unit-name>",           -- short unit name, e.g., Wlan1, max 15 characters
 *                         "driver"     : "<driver-object-name>",  -- may be same driver object used for other units (e.g. "s91xDriverWiFi"), or may be different object
 *                        *"driver-lib" : "<driver-lib-name>",     -- optional name of library that contains driver object implementation, if unspecified, same as driver, e.g. "s91xDriverWiFi"
 *                        *"config"     : {<unit specific data>}   -- optional unit specific configuration data
 *                     }
 *                 ]
 *             }
 *         ],
 *
 *        *"extra-libs": [     -- an array of names of libraries for build inclusion required by driver libraries specified explicitly or implitly in the preceding devices array, e.g. LTThirdPartLWIP.
 *                             -- Note: do not put platform mastering nor vendor sdk libraries here.  Those go in LT_PLATFORM_MASTERING_PROJECTS in Makefile.config.
 *                             -- Do not put libraries used by drivers specified herein in LTProductConfig.json.  e.g. Don't put LTThirdPartyLWIP in an LTProductConfig.json, put it in "extra-libs" here.
 *             "<lib name 1>",
 *            *"<lib name 2>",
 *            *"<lib name 3>"
 *         ],
 *     }
 *    _________________________________________________________________________
 */
