/******************************************************************************
 * lt/source/lt/product/config/LTProductConfig.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/product/config/LTProductConfig.h>

/********************
 * static variables *
 ********************/
 const LTResourceTree *s_resourceTree = NULL;

static u32 LTProductConfig_GetLibraryConfigSection(const char *libraryName) {
    LTResourceValue value;
    if (libraryName && *libraryName &&
        LT_GetCore()->ReadResourceValue(s_resourceTree, 0, "config/lib", &value) && (value.type == kLTResourceValueType_Object) &&
        LT_GetCore()->ReadResourceValue(s_resourceTree, value.offset, libraryName, &value) && (value.type == kLTResourceValueType_Object)) return value.offset;
    return 0;
}

static const char * LTProductConfig_ReadString(u32 section, const char *key) {
    LTResourceValue value;
    if (key && *key && LT_GetCore()->ReadResourceValue(s_resourceTree, section, key, &value) && value.type == kLTResourceValueType_String) return value.string;
    return NULL;
}

static s64 LTProductConfig_ReadInteger(u32 section, const char *key) {
    LTResourceValue value;
    if (key && *key && LT_GetCore()->ReadResourceValue(s_resourceTree, section, key, &value) && value.type == kLTResourceValueType_Integer) return value.integer;
    return 0;
}

static bool LTProductConfig_IsIntegerZero(u32 section, const char *key) {
    LTResourceValue value;
    if (key && *key && LT_GetCore()->ReadResourceValue(s_resourceTree, section, key, &value) && value.type == kLTResourceValueType_Integer && value.integer == 0) return true;
    return false;
}

static const u8 * LTProductConfig_ReadBinary(u32 section, const char *key, u32 *sizeToSet) {
    LTResourceValue value;
    if (key && *key && LT_GetCore()->ReadResourceValue(s_resourceTree, section, key, &value) && value.type == kLTResourceValueType_Binary) {
        if (sizeToSet) *sizeToSet = value.size;
        return value.binary;
    }
    return NULL;
}

static bool LTProductConfig_ReadFirstItemInArray(u32 section, const char *key, LTResourceValue *value) {
    LTResourceValue val;
    if (key && *key && value &&
        LT_GetCore()->ReadResourceValue(s_resourceTree, section, key, &val) &&
        val.type == kLTResourceValueType_Array &&
        LT_GetCore()->ReadResourceValue(s_resourceTree, val.offset, kLTResourceKey_FirstChild, &val))
    {
        *value = val;
        return true;
    }
    return false;
}

static bool LTProductConfig_ReadNextItemInArray(LTResourceValue *value) {
    return (value && LT_GetCore()->ReadResourceValue(s_resourceTree, value->offset, kLTResourceKey_NextSibling, value));
}

/*****************
 * init and fini *
 *****************/
static LTProductConfig s_LTProductConfig;
static bool LTProductConfigImpl_LibInit(void) {
    s_resourceTree = s_LTProductConfig.GetResourceTree();
    return true;
}

static void LTProductConfigImpl_LibFini(void) {
}

/*****************************************
 * LTProductConfig root interface binding *
 *****************************************/
define_LTLIBRARY_ROOT_INTERFACE(LTProductConfig) {
    .GetLibraryConfigSection        = &LTProductConfig_GetLibraryConfigSection,
    .ReadString                     = &LTProductConfig_ReadString,
    .ReadInteger                    = &LTProductConfig_ReadInteger,
    .IsIntegerZero                  = &LTProductConfig_IsIntegerZero,
    .ReadBinary                     = &LTProductConfig_ReadBinary,
    .ReadFirstItemInArray           = &LTProductConfig_ReadFirstItemInArray,
    .ReadNextItemInArray            = &LTProductConfig_ReadNextItemInArray,
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Apr-23   trajan      created
 */
