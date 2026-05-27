/******************************************************************************
 * lt/source/lt/device/config/LTDeviceConfig.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>

/************
 * #defines *
 ************/
#define LTDC_CONFIG_NET_TRANSPORT   "/config/net/transport" /* /config/net/transport/0/lib, /config/net/transport/1/lib, etc. */
#define LTDC_CONFIG_DEVICE          "/config/device"        /* /config/device/0/lib, /config/device/1/lib, etc. */
#define LTDC_DRIVER                 "driver"                /* /config/device/0/driver/0/lib, /config/device/0/driver/1/lib, /config/device/1/driver/0/lib, etc. */
#define LTDC_LIB                    "lib"
#define LTDC_UNITS                  "units"
#define LTDC_TREE                   LT_GetLTDeviceConfig()->GetResourceTree()

/************************
 * forward declarations *
 ************************/
LTDeviceConfig * LT_GetLTDeviceConfig(void);
static u32 LTDeviceConfig_GetDeviceSection(const char *deviceName);

/********************
 * helper functions *
 ********************/

static bool GetChildElement(u32 offset, const char *key, u32 index, LTResourceValue *pValue) {
    LTResourceValue value;
    if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, offset,       key,                         &value)) return false;
    if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, kLTResourceKey_FirstChild,   &value)) return false;
    for (u32 i = 0; i < index; ++i) {
        if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, kLTResourceKey_NextSibling, &value)) return false;
    }
    *pValue = value;
    return true;
}

static const char *GetChildLibraryAt(u32 offset, const char *key, u32 index) {
    if (index > 255) return NULL;
    LTResourceValue value;
    if (!GetChildElement(offset, key, index, &value))                                return NULL;
    if (value.type != kLTResourceValueType_Object)                                   return NULL;
    if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, LTDC_LIB, &value)) return NULL;
    if (value.type != kLTResourceValueType_String)                                   return NULL;
    return value.string;
}

static u32 GetLibrarySection(u32 offset, const char *parentKey, const char* libName) {
    if (NULL == libName || 0 == *libName) return 0;
    LTResourceValue value;

    // Read the parent array
    if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, offset, parentKey, &value)) return 0;
    if (value.type != kLTResourceValueType_Array) return 0;

    // Read the first array element
    if (!LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, kLTResourceKey_FirstChild, &value)) return 0;
    if (value.type != kLTResourceValueType_Object) return 0;

    // Locate an array element containing "lib": libName
    do {
        LTResourceValue libValue;
        if (LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, LTDC_LIB, &libValue) &&
            (libValue.type == kLTResourceValueType_String) &&
            (lt_strcmp(libName, libValue.string) == 0)) {
            return value.offset;
        }
    } while (LT_GetCore()->ReadResourceValue(LTDC_TREE, value.offset, kLTResourceKey_NextSibling, &value));

    return 0;
}

/*****************
 * init and fini *
 *****************/
static bool LTDeviceConfigImpl_LibInit(void) {
    return true;
}

static void LTDeviceConfigImpl_LibFini(void) {
}

/*****************
 * api functions *
 *****************/
static const char * LTDeviceConfig_GetDefaultNetTransport(void) {
    return GetChildLibraryAt(0, LTDC_CONFIG_NET_TRANSPORT, 0);
}

static u32 LTDeviceConfig_GetNumNetTransports(void) {
    return LT_GetCore()->CountResourceChildren(LTDC_TREE, 0, LTDC_CONFIG_NET_TRANSPORT);
}

static const char * LTDeviceConfig_GetNetTransportAt(u32 index) {
    return GetChildLibraryAt(0, LTDC_CONFIG_NET_TRANSPORT, index);
}

/*{
  "config":{
       "net":{ "transport":[{ "lib":"LTDriverNetTransportLwipWiFi" }]},
    "device":[
          {    "lib": "LTDeviceConfig"      },
          {    "lib": "LTDevicePins",
            "driver":[{ "lib": "Esp32DriverPins",
                           "units":{ "0":{ "name":"led",    "gpio":19 },
                                     "1":{ "name":"relay",  "gpio":16 },
                                     "2":{ "name":"button", "gpio":15 } }}]}],
*/

static u32 LTDeviceConfig_GetNumDevices(void) {
    return  LT_GetCore()->CountResourceChildren(LTDC_TREE, 0, LTDC_CONFIG_DEVICE);
}

static const char * LTDeviceConfig_GetDeviceAt(u32 index) {
    return GetChildLibraryAt(0, LTDC_CONFIG_DEVICE, index);
}

static u32 LTDeviceConfig_GetNumDrivers(const char *deviceName) {
    /* /config/device/100/driver/100/lib */
    u32 deviceSection = LTDeviceConfig_GetDeviceSection(deviceName);
    if (0 == deviceSection) return 0;
    /* /driver/100/lib */
    return LT_GetCore()->CountResourceChildren(LTDC_TREE, deviceSection, LTDC_DRIVER);
}

static const char * LTDeviceConfig_GetDriverAt(const char *deviceName, u32 index) {
    if (index > 255) return NULL;
    u32 deviceSection = LTDeviceConfig_GetDeviceSection(deviceName);
    if (0 == deviceSection) return 0;

    return GetChildLibraryAt(deviceSection, LTDC_DRIVER, index);
}

static u32 LTDeviceConfig_GetDeviceSection(const char *deviceName) {
    /* returns offset of, e.g., /config/device/12 */
    return GetLibrarySection(0, LTDC_CONFIG_DEVICE, deviceName);
}

static u32 LTDeviceConfig_GetDriverSection(const char *deviceName, const char *driverName) {
    /* returns offset of, e.g., /config/device/12/driver/11 */
    u32 deviceSection = LTDeviceConfig_GetDeviceSection(deviceName);
    if (0 == deviceSection) return 0;
    return GetLibrarySection(deviceSection, LTDC_DRIVER, driverName);
}

static u32 LTDeviceConfig_GetNumDeviceUnits(u32 driverSection) {
    /* /config/device/LTDeviceLED/driver/lib/Esp32DriverLED/units */
    if (0 == driverSection) return 0;
    return LT_GetCore()->CountResourceChildren(LTDC_TREE, driverSection, LTDC_UNITS);
}

static u32 LTDeviceConfig_GetDeviceUnitSectionAt(u32 driverSection, u32 index) {
    LTResourceValue value;
    if (!GetChildElement(driverSection, LTDC_UNITS, index, &value)) return 0;
    return value.offset;
}

static const char * LTDeviceConfig_ReadString(u32 section, const char *key) {
    if (key && *key) {
        LTResourceValue value;
        if (LT_GetCore()->ReadResourceValue(LTDC_TREE, section, key, &value) && value.type == kLTResourceValueType_String) return value.string;
    }
    return NULL;
}

static s64 LTDeviceConfig_ReadInteger(u32 section, const char *key) {
    if (key && *key) {
        LTResourceValue value;
        if (LT_GetCore()->ReadResourceValue(LTDC_TREE, section, key, &value) && value.type == kLTResourceValueType_Integer) return value.integer;
    }
    return 0;
}

static const u8 * LTDeviceConfig_ReadBinary(u32 section, const char *key, u32 *sizeToSet) {
    if (key && *key) {
        LTResourceValue value;
        if (LT_GetCore()->ReadResourceValue(LTDC_TREE, section, key, &value) && value.type == kLTResourceValueType_Binary) {
            if (sizeToSet) *sizeToSet = value.size;
            return value.binary;
        }
    }
    return NULL;
}

/*****************************************
 * LTDeviceConfig root interface binding *
 *****************************************/
define_LTLIBRARY_ROOT_INTERFACE(LTDeviceConfig) {

    .GetDefaultNetTransport     = LTDeviceConfig_GetDefaultNetTransport,
    .GetNumNetTransports        = LTDeviceConfig_GetNumNetTransports,
    .GetNetTransportAt          = LTDeviceConfig_GetNetTransportAt,

    .GetNumDevices              = LTDeviceConfig_GetNumDevices,
    .GetDeviceAt                = LTDeviceConfig_GetDeviceAt,
    .GetNumDrivers              = LTDeviceConfig_GetNumDrivers,
    .GetDriverAt                = LTDeviceConfig_GetDriverAt,

    .GetDeviceSection           = LTDeviceConfig_GetDeviceSection,
    .GetDriverSection           = LTDeviceConfig_GetDriverSection,
    .GetNumDeviceUnits          = LTDeviceConfig_GetNumDeviceUnits,
    .GetDeviceUnitSectionAt     = LTDeviceConfig_GetDeviceUnitSectionAt,

    .ReadString                 = LTDeviceConfig_ReadString,
    .ReadInteger                = LTDeviceConfig_ReadInteger,
    .ReadBinary                 = LTDeviceConfig_ReadBinary

} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  30-Aug-22   aurelian    created
 */
