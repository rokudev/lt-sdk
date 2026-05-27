/******************************************************************************
 * lt/source/lt/device/config/LTDeviceKonfig.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceKonfig.h>

/************
 * #defines *
 ************/
#define LTDK_KEY_DEVICE                         "/device"           /* /device/0, /device/1, etc. */
#define LTDK_KEY_DEVICE_CLASS                   "class"             /* /device/0/class, /device/1/class, etc. */
#define LTDK_KEY_DEVICE_UNIT                    "unit"              /* /device/0/unit, /device/1/unit, etc. */
#define LTDK_KEY_DEVICE_UNIT_NAME               "name"              /* /device/0/unit/0/name, /device/0/unit/1/name, etc. */
#define LTDK_KEY_DEVICE_UNIT_DRIVER             "driver"            /* /device/0/unit/0/driver, /device/0/unit/1/driver, etc. */
#define LTDK_KEY_DEVICE_UNIT_CONFIG             "config"            /* /device/0/unit/0/config, /device/0/unit/1/config, etc. */
#define LTDK_KEY_DEVICE_UNIT_ACCESS             "access"            /* /device/0/unit/0/access, /device/0/unit/1/access, etc. */
#define LTDK_VALUE_DEVICE_UNIT_ACCESS_EXCLUSIVE "exclusive"         /* /device/n/unit/m/access: "exclusive" */
#define LTDK_KEY_DEVICE_API                     "api"               /* /device/0/api, /device/1/api, etc. */
#define LTDK_VALUE_DEVICE_API_PASSTHRU          "passthru"          /* device/n/api: "passthru" */
#define LTDK_KEY_MEMORY_REGIONS                 "/memory/regions"   /* /memory/regions/0, /memory/regions/1, etc. */
#define LTDK_KEY_MEMORY_REGION_NAME             "name"              /* /memory/regions/0/name, /memory/regions/1/name, etc. */
#define LTDK_KEY_MEMORY_REGION_REGION           "region"            /* /memory/regions/0/region, /memory/regions/1/region, etc */
#define LTDK_VALUE_MAX_DEVICE_CLASS_INDEX       255                 /* max of 256 device classes with index [0..255] */
#define LTDK_VALUE_MAX_DEVICE_UNIT_INDEX        15                  /* max of  16 device units per device class with index [0..15] */

DEFINE_LTLOG_SECTION("dev.konfig");
#define USE_DLOG 1
#if USE_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_NULL
#endif

/* _______________________
  / object impl struct  */
typedef_LTObjectImpl(LTDeviceKonfig, LTDeviceKonfigImpl) {
    const void *tree; /* the resource tree */
} LTOBJECT_API;

/* ________________________
  / object constructors  */
static bool LTDeviceKonfigImpl_ConstructObject(LTDeviceKonfigImpl *konfig) {
    LTLibrary *lib = konfig->API->GetObjectLibrary();
    konfig->tree = lib ? lib->GetResourceTree() : NULL;
    #if USE_DLOG
        if (! lib) { DLOG("constructor", "failed to get konfig library"); }
        else if (! konfig->tree) { DLOG("constructor", "failed to get library resource tree"); }
    #endif
    return konfig->tree != NULL;
}

static void LTDeviceKonfigImpl_DestructObject(LTDeviceKonfigImpl *konfig) {
    LT_UNUSED(konfig);
}

/************************
 * forward declarations *
 ************************/
static s64 LTDeviceKonfigImpl_ReadInteger(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key);

/********************
 * helper functions *
 ********************/
static bool GetChildElement(LTDeviceKonfigImpl *konfig, u32 offset, const char *key, u32 index, LTResourceValue *pValue) {
    if (key && *key) {
        LTResourceValue value;
        if (!LT_GetCore()->ReadResourceValue(konfig->tree, offset, key, &value)) goto bail;
        if (!LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, kLTResourceKey_FirstChild, &value)) goto bail;
        for (u32 i = 0; i < index; ++i) {
            if (!LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, kLTResourceKey_NextSibling, &value)) goto bail;
        }
        if (pValue) *pValue = value;
        return true;
    }
bail:
    return false;
}

static bool MatchObjectStringValue(LTDeviceKonfigImpl *konfig, u32 offset, const char *stringKey, const char *stringVal) {
    LTResourceValue value;
    return (LT_GetCore()->ReadResourceValue(konfig->tree, offset, stringKey, &value) &&
            value.type == kLTResourceValueType_String &&
            (0 == lt_strcmp(value.string, stringVal)))
           ? true : false;
}

static bool GetChildObjectElementWithNameKeyAndName(LTDeviceKonfigImpl *konfig, u32 offset, const char *key, const char * childNameKey, const char * childNameValue, LTResourceValue *pValue, u32 *pIndex) {
    if (key && *key && childNameKey && *childNameKey && childNameValue && *childNameValue) {
        LTResourceValue value;
        if (!LT_GetCore()->ReadResourceValue(konfig->tree, offset, key, &value)) goto bail;
        if (!LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, kLTResourceKey_FirstChild, &value)) goto bail;
        if (pIndex) *pIndex = 0;
        while (1) {
            if (value.type != kLTResourceValueType_Object) goto bail;
            if (MatchObjectStringValue(konfig, value.offset, childNameKey, childNameValue)) {
                if (pValue) *pValue = value;
                return true;
            }
            if (!LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, kLTResourceKey_NextSibling, &value)) goto bail;
            if (pIndex) *pIndex = *pIndex + 1;
        }
    }
bail:
    return false;
}

/*****************
 * api functions *
 *****************/
static LTMemoryRegion LTDeviceKonfigImpl_GetNamedMemoryRegion(LTDeviceKonfigImpl *konfig, const char *name) {
    LTResourceValue value;
    return GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_MEMORY_REGIONS, LTDK_KEY_MEMORY_REGION_NAME, name, &value, NULL)
           ? (LTMemoryRegion)LTDeviceKonfigImpl_ReadInteger(konfig, value.offset, LTDK_KEY_MEMORY_REGION_REGION)
           : (LTMemoryRegion)0;
}

static u32 LTDeviceKonfigImpl_GetNumDeviceClasses(LTDeviceKonfigImpl *konfig) {
    u32 nNumDeviceClasses = LT_GetCore()->CountResourceChildren(konfig->tree, 0, LTDK_KEY_DEVICE);
    return (nNumDeviceClasses > (LTDK_VALUE_MAX_DEVICE_CLASS_INDEX + 1)) ? LTDK_VALUE_MAX_DEVICE_CLASS_INDEX + 1 : nNumDeviceClasses;
}

static const char * LTDeviceKonfigImpl_GetDeviceClassNameAt(LTDeviceKonfigImpl *konfig, u32 index) {
    LTResourceValue value;
    return (index <= LTDK_VALUE_MAX_DEVICE_CLASS_INDEX && GetChildElement(konfig, 0, LTDK_KEY_DEVICE, index, &value) && value.type == kLTResourceValueType_Object &&
            LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, LTDK_KEY_DEVICE_CLASS, &value) && value.type == kLTResourceValueType_String)
           ? value.string : NULL;
}

static u32 LTDeviceKonfigImpl_GetNumDeviceUnits(LTDeviceKonfigImpl *konfig, const char *deviceClass) {
    /* /device/100/unit */
    LTResourceValue value;
    u32     nNumDeviceUnits = GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClass, &value, NULL)
                            ? LT_GetCore()->CountResourceChildren(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT)
                            : 0;
    return (nNumDeviceUnits > (LTDK_VALUE_MAX_DEVICE_UNIT_INDEX + 1)) ? LTDK_VALUE_MAX_DEVICE_UNIT_INDEX + 1 : nNumDeviceUnits;
}

static const char * LTDeviceKonfigImpl_GetDeviceUnitNameAt(LTDeviceKonfigImpl *konfig, u32 deviceClassIndex, u32 deviceUnitIndex) {
    LTResourceValue value;
    if ((! GetChildElement(konfig, 0, LTDK_KEY_DEVICE, deviceClassIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    if ((! GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, deviceUnitIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    return (LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_NAME, &value) &&
            value.type == kLTResourceValueType_String)
            ? value.string
            : NULL;
}

static bool LTDeviceKonfigImpl_GetDeviceClassIndex(LTDeviceKonfigImpl *konfig, const char *deviceClassName, u32 *deviceClassIndexToSet) {
    return (deviceClassIndexToSet &&
            GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClassName, NULL, deviceClassIndexToSet) &&
            (*deviceClassIndexToSet <= LTDK_VALUE_MAX_DEVICE_CLASS_INDEX));
}

static bool LTDeviceKonfigImpl_GetDeviceUnitIndex(LTDeviceKonfigImpl *konfig, u32 nDeviceClassIndex, const char *deviceUnitName, u32 *deviceUnitIndexToSet) {
    LTResourceValue value;
    if (deviceUnitIndexToSet && (nDeviceClassIndex <= LTDK_VALUE_MAX_DEVICE_CLASS_INDEX) &&
        GetChildElement(konfig, 0, LTDK_KEY_DEVICE, nDeviceClassIndex, &value) && (value.type == kLTResourceValueType_Object))
    {
        /* found the device; if we have a deviceUnitName, we look it up; if deviceUnitName is NULL or empty we use the default (index 0) and verify it exists */
        *deviceUnitIndexToSet = 0;
        return (deviceUnitName && *deviceUnitName)
               ? (GetChildObjectElementWithNameKeyAndName(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, LTDK_KEY_DEVICE_UNIT_NAME, deviceUnitName, NULL, deviceUnitIndexToSet) && (*deviceUnitIndexToSet <= LTDK_VALUE_MAX_DEVICE_UNIT_INDEX))
               :  GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, 0, NULL) && (value.type == kLTResourceValueType_Object);
    }
    return false;
}

static bool LTDeviceKonfigImpl_GetDeviceUnitIndices(LTDeviceKonfigImpl *konfig, const char *deviceClassName, const char *deviceUnitName,
                                                    u32 *deviceIndexToSet, u32 *deviceUnitIndexToSet) {

    if (! (deviceClassName && *deviceClassName && deviceIndexToSet && deviceUnitIndexToSet)) return false;

    LTResourceValue value;
    if ((! GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClassName, &value, deviceIndexToSet))
       ||  (*deviceIndexToSet > LTDK_VALUE_MAX_DEVICE_CLASS_INDEX)) return false;

    if (deviceUnitName && *deviceUnitName) {
        /* we have a device unit name, look it up */
        if ((! GetChildObjectElementWithNameKeyAndName(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, LTDK_KEY_DEVICE_UNIT_NAME, deviceUnitName, &value, deviceUnitIndexToSet))
           ||  (*deviceUnitIndexToSet > LTDK_VALUE_MAX_DEVICE_UNIT_INDEX)) return false;
    }
    else {
        /* NULL or empty device unit name, use default */
        *deviceUnitIndexToSet = 0;
        if ((! GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, 0, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    }

    return true;
}

static bool LTDeviceKonfigImpl_GetDevicePassThruAPI(LTDeviceKonfigImpl *konfig, u32 nDeviceIndex) {
    LTResourceValue value;
    if (nDeviceIndex > LTDK_VALUE_MAX_DEVICE_CLASS_INDEX) return false;
    if ((! GetChildElement(konfig, 0, LTDK_KEY_DEVICE, nDeviceIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    return (LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, LTDK_KEY_DEVICE_API, &value) &&
            value.type == kLTResourceValueType_String &&
            (0 == lt_strcmp(value.string, LTDK_VALUE_DEVICE_API_PASSTHRU)));
}

static bool LTDeviceKonfigImpl_GetDeviceUnitExclusivity(LTDeviceKonfigImpl *konfig, u32 nDeviceIndex, u32 nUnitIndex) {
    LTResourceValue value;
    if ((nDeviceIndex > LTDK_VALUE_MAX_DEVICE_CLASS_INDEX) || (nUnitIndex > LTDK_VALUE_MAX_DEVICE_UNIT_INDEX)) return false;
    if ((! GetChildElement(konfig, 0, LTDK_KEY_DEVICE, nDeviceIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    if ((! GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, nUnitIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    return (LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_ACCESS, &value) &&
            value.type == kLTResourceValueType_String &&
            (0 == lt_strcmp(value.string, LTDK_VALUE_DEVICE_UNIT_ACCESS_EXCLUSIVE)));
}

static const char * LTDeviceKonfigImpl_GetDeviceUnitDriver(LTDeviceKonfigImpl *konfig, u32 nDeviceIndex, u32 nUnitIndex) {
    LTResourceValue value;
    if ((! GetChildElement(konfig, 0, LTDK_KEY_DEVICE, nDeviceIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    if ((! GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, nUnitIndex, &value)) || (value.type != kLTResourceValueType_Object)) return false;
    return (LT_GetCore()->ReadResourceValue(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_DRIVER, &value) &&
            value.type == kLTResourceValueType_String)
            ? value.string
            : NULL;
}

static bool LTDeviceKonfigImpl_DeviceClassIsPresent(LTDeviceKonfigImpl *konfig, const char *deviceClass) {
    return GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClass, NULL, NULL);
}

static bool LTDeviceKonfigImpl_DeviceUnitIsPresent(LTDeviceKonfigImpl *konfig, const char *deviceClass, const char *unitName) {
    LTResourceValue value;
    return GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClass, &value, NULL) &&
           GetChildObjectElementWithNameKeyAndName(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, LTDK_KEY_DEVICE_UNIT_NAME, unitName, NULL, NULL);
}

static u32 LTDeviceKonfigImpl_GetDeviceUnitConfigSectionByName(LTDeviceKonfigImpl *konfig, const char *deviceClass, const char *unitName) {
    LTResourceValue value;
    return (GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClass, &value, NULL) &&
             GetChildObjectElementWithNameKeyAndName(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, LTDK_KEY_DEVICE_UNIT_NAME, unitName, &value, NULL))
            ? LT_GetCore()->FindResourceOffset(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_CONFIG) : 0;
}

static u32 LTDeviceKonfigImpl_GetDeviceUnitConfigSectionByNameAndIndex(LTDeviceKonfigImpl *konfig, const char *deviceClass, u32 nUnitIndex) {
    LTResourceValue value;
    return ((nUnitIndex <= LTDK_VALUE_MAX_DEVICE_UNIT_INDEX) &&
            GetChildObjectElementWithNameKeyAndName(konfig, 0, LTDK_KEY_DEVICE, LTDK_KEY_DEVICE_CLASS, deviceClass, &value, NULL) &&
            GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, nUnitIndex, &value) && value.type == kLTResourceValueType_Object)
            ? LT_GetCore()->FindResourceOffset(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_CONFIG) : 0;
}

static u32 LTDeviceKonfigImpl_GetDeviceUnitConfigSectionByIndex(LTDeviceKonfigImpl *konfig, u32 nDeviceIndex, u32 nUnitIndex) {
    LTResourceValue value;
    return ((nDeviceIndex <= LTDK_VALUE_MAX_DEVICE_CLASS_INDEX) && (nUnitIndex <= LTDK_VALUE_MAX_DEVICE_UNIT_INDEX) &&
            GetChildElement(konfig, 0, LTDK_KEY_DEVICE, nDeviceIndex, &value) && (value.type == kLTResourceValueType_Object) &&
            GetChildElement(konfig, value.offset, LTDK_KEY_DEVICE_UNIT, nUnitIndex, &value) && value.type == kLTResourceValueType_Object)
            ? LT_GetCore()->FindResourceOffset(konfig->tree, value.offset, LTDK_KEY_DEVICE_UNIT_CONFIG) : 0;
}

static u32 LTDeviceKonfigImpl_GetDeviceUnitConfigSectionForDeviceOrDriverObject(LTDeviceKonfigImpl *konfig, LTObject *deviceOrDriverObject) {
    u32 nDeviceIndex = 0, nUnitIndex = 0;
    return  LT_GetCore()->GetDeviceAndUnitIndicesFromDeviceOrDriverObject(deviceOrDriverObject, &nDeviceIndex, &nUnitIndex)
            ? LTDeviceKonfigImpl_GetDeviceUnitConfigSectionByIndex(konfig, nDeviceIndex, nUnitIndex)
            : 0;
}

static u32 LTDeviceKonfigImpl_GetArraySubObjectSectionWithName(LTDeviceKonfigImpl *konfig, u32 configSection, const char *arrayKey, const char *name ) {
    LTResourceValue value;
    return (GetChildObjectElementWithNameKeyAndName(konfig, configSection, arrayKey, "name", name, &value, NULL) ? value.offset : 0);
}

static u32 LTDeviceKonfigImpl_GetObjectSection(LTDeviceKonfigImpl *konfig, u32 configSection,  const char *key) {
    LTResourceValue value;
    return (key && *key && LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value) && value.type == kLTResourceValueType_Object) ? value.offset : 0;
}

static const char * LTDeviceKonfigImpl_ReadString(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key) {
    LTResourceValue value;
    return (key && *key && LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value) && value.type == kLTResourceValueType_String) ? value.string : NULL;
}

static s64 LTDeviceKonfigImpl_ReadInteger(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key) {
    LTResourceValue value;
    return (key && *key && LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value) && value.type == kLTResourceValueType_Integer) ? value.integer : 0;
}

static const u8 * LTDeviceKonfigImpl_ReadBinary(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key, u32 *sizeToSet) {
    if (key && *key) {
        LTResourceValue value;
        if (LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value) && value.type == kLTResourceValueType_Binary) {
            if (sizeToSet) *sizeToSet = value.size;
            return value.binary;
        }
    }
    return NULL;
}

static u32 LTDeviceKonfigImpl_GetNumArrayElements(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key) {
    LTResourceValue value;
    return (key && *key && LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value) && value.type == kLTResourceValueType_Array) ? LT_GetCore()->CountResourceChildren(konfig->tree, configSection, key) : 0;
}

static LTResourceValueType LTDeviceKonfigImpl_ReadValueType(LTDeviceKonfigImpl *konfig, u32 configSection, const char *key) {
    LTResourceValue value;
    return (key && *key && LT_GetCore()->ReadResourceValue(konfig->tree, configSection, key, &value)) ? value.type : kLTResourceValueType_None;
}

/******************
 * object binding *
 ******************/

define_LTObjectImplPublic(LTDeviceKonfig, LTDeviceKonfigImpl,

    GetNamedMemoryRegion,

    GetNumDeviceClasses,
    GetDeviceClassNameAt,
    GetDeviceClassIndex,

    GetNumDeviceUnits,
    GetDeviceUnitNameAt,
    GetDeviceUnitIndex,

    GetDeviceUnitIndices,
    GetDevicePassThruAPI,
    GetDeviceUnitExclusivity,
    GetDeviceUnitDriver,

    DeviceClassIsPresent,
    DeviceUnitIsPresent,

    GetDeviceUnitConfigSectionByName,
    GetDeviceUnitConfigSectionByNameAndIndex,
    GetDeviceUnitConfigSectionByIndex,
    GetDeviceUnitConfigSectionForDeviceOrDriverObject,
    GetArraySubObjectSectionWithName,
    GetObjectSection,

    ReadString,
    ReadInteger,
    ReadBinary,
    GetNumArrayElements,
    ReadValueType);


#if 0
// Enable when we get rid of LTDeviceConfig and rename LTDeviceKonfig to LTDeviceConfig.
define_LTObjectLibrary(1, NULL, NULL);
#endif

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  30-Aug-22   aurelian    created
 */
