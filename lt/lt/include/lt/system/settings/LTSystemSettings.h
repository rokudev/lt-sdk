/*******************************************************************************
 * <lt/system/settings/LTSystemSettings.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltsystem_settings LTSystemSettings
 * @ingroup ltsystem
 * @{
 *
 * @brief LT Library for general data access and storage.
 *
 * Settings are stored as (key, value) pairs in an unified namespace. Key names
 * are full paths to settings using '/' as a path name separator. Settings support
 * the storage of integer, string and binary data.
 *
 * Flash Settings Sections
 *
 *  Flashed settings can be changed up to the physical limit of flash erase
 *  cycles supported by the underlying flash device.
 *
 *  Settings sections are power-cycle tolerant* and intended for the storage
 *  of small values. Settings are written to flash 5 seconds after the last API
 *  call that modifies these settings.
 *
 * Power-Cycle Tolerance
 *
 *  Settings will fall back to prior settings if a power cycle or write error
 *  occurs during a flash write operation.
 *
 * Settings Usage
 *
 *  Settings read operations are fairly expensive, so it is recommended that
 *  settings are read once at application startup and cached in RAM for
 *  subsequent use.
 *
 *  Settings write operations are also expensive, so it is recommended that
 *  values are first checked against cached RAM values to prevent unnecessary
 *  calls to the settings write API and excessive flash wear.
 */

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SETTINGS_LTSYSTEMSETTINGS_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_SETTINGS_LTSYSTEMSETTINGS_H

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>

LT_EXTERN_C_BEGIN

typedef enum {
    kLTSystemSettingsDataType_Integer       = 'i', /**< Signed 64-bit integer setting */
    kLTSystemSettingsDataType_String        = 's', /**< String setting */
    kLTSystemSettingsDataType_Binary        = 'b', /**< Binary blob setting */
    kLTSystemSettingsDataType_Deleted       = 'd', /**< Deleted setting */
} LTSystemSettingsDataType;

typedef bool (LTSystemSettings_EnumProc)(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData);
    /**< Settings enumeration callback function.
     *
     * @param[in] pKey Full name of matched setting.
     * @param[in] pKeySuffix Suffix of matched setting.
     * @param[in] type Type of matched setting.
     * @param[in] pClientData Pointer to private data supplied to call to EnumerateSettingsWithPrefix().
     * @return    true to continue enumeration process.
     * @return    false to abort enumeration process, ending with this key. */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTSystemSettings, 1);

struct LTSystemSettingsApi {

    INHERIT_LIBRARY_BASE

    bool (* GetIntegerValue)(const char * pKey, s64 * pValueToSet);
        /**< Get an integer setting.
         *
         * @param[in]  pKey Name of setting to fetch.
         * @param[out] pValueToSet Where to store fetched value (unless NULL).
         * @return     true if value successfully written to pValueToSet.
         * @return     true if pValueToSet is NULL and an integer setting named pKey exists.
         * @return     false if not found or data type is incorrect. */

    bool (* SetIntegerValue)(const char * pKey, s64 nValue);
        /**<  Set an integer setting, creating new entry or replacing existing entry as needed.
         *
         * @param[in] pKey  Name of setting to store value into.
         * @param[in] nValue Integer value to store.
         * @return    true if setting stored successfully.
         * @return    false on failure. */

    bool (* GetStringValue)(const char * pKey, LTString * ltstring);
        /**< Get a null-terminated string setting value.
         * @note The provided LT String is set to the string value using ltstring_set() semantics.
         *       This LT String must be freed when it is no longer needed.
         * @see ltstring_set() and LTStdlib.h for more information on LT Strings.
         *
         * @param[in]  pKey Name of setting to fetch.
         * @param[out] ltstring LT String in which to set the string value (unless NULL).
         * @return     true if value exists and is successfully retrieved into ltstring.
         * @return     true if ltstring is NULL and a string setting named pKey exists.
         * @return     false if not found or data type is incorrect. */

    bool (* SetStringValue)(const char * pKey, const char * pValue);
        /**< Store a string value into a setting, creating new entry or replacing existing entry as needed.
         *
         * @param[in] pKey Name of setting to store value into.
         * @param[in] pValue String value to store.
         * @return    true if string is successfully stored in setting.
         * @return    false on failure. */

    bool (* GetBinaryValue)(const char * pKey, u8 * pValueToSet, u32 * pSizeInBytes);
        /**< Get a binary setting.
         *
         * @param[in]     pKey Name of setting to fetch.
         * @param[out]    pValueToSet Where to store fetched value (unless NULL).
         * @param[in,out] pSizeInBytes Size of buffer to copy value into. If buffer is too small, no copy will be performed.
         *                             Function returns actual size of value in *pSizeInBytes, even if copy is not performed.
         * @return        true if binary data fits and read successfully into pValueToSet.
         * @return        true if pValueToSet is NULL and a binary setting named pKey exists.
         * @return        false if not found, data type is incorrect or value is larger than *pSizeInBytes. */

    bool (* SetBinaryValue)(const char * pKey, const u8 * pValue, u32 nSizeInBytes);
        /**< Store a binary value into a setting, creating new entry or replacing existing entry as needed.
         *
         * @param[in] pKey Name of setting to store value into.
         * @param[in] pValue Binary value to store.
         * @param[in] nSizeInBytes Length of binary value to store in bytes.
         * @return    true if binary data is successfully stored in setting.
         * @return    false on failure. */

    bool (* DeleteSetting)(const char * pKey);
        /**< Delete a setting.
         *
         * @param[in] pKey Name of setting to delete.
         * @return    true if deletion request is accepted (even if setting does not exist).
         * @return    false if cannot accept request. */

    bool (* DeleteSettingsWithPrefix)(const char * pKeyPrefix);
        /**< Delete settings with the matching prefix.
         *
         * @param[in] pKeyPrefix Prefix of keys for the settings to be deleted.
         * @return    true if deletion request is accepted (even if no settings match prefix).
         * @return    false if cannot accept request. */

    bool (* EnumerateSettingsWithPrefix)(const char * pKeyPrefix, LTSystemSettings_EnumProc * pCallback, void * pClientData);
        /**< Invoke provided callback function on settings with the matching prefix.
         *   The provided callback function is allowed to read, add, delete or change settings. However, this callback
         *   MUST NOT perform prefix operations; in other words it MUST NOT invoke DeleteSettingsWithPrefix() or
         *   EnumerateSettingsWithPrefix(). Note that callbacks that change settings that match the current pKeyPrefix
         *   can result in non-deterministic operation, as the changed setting may or may not have been visited by the
         *   current enumeration. This will of course not be an issue if the callback manipulates the same setting for
         *   which it is invoked.
         *
         * @param[in] pKeyPrefix Prefix of keys for the settings to be enumerated.
         * @param[in] pCallback Callback function invoked for each setting with the matching prefix.
         * @param[in] pClientData Client-provided data to supply to callback function.
         * @return    true if fully enumerated: all callbacks returned true OR if no settings matched prefix.
         * @return    false if enumeration aborted: any callback returned false or there was an internal error. */

    bool (* EraseSection)(const char * pSectionPrefix);
        /**< Delete all settings in the section and erase non-volatile memory associated with the section (if any).
          *
          * @param[in] pSectionPrefix Prefix of keys in section to be erased.
          * @return    true if erase was successful, false otherwise.
          */

    bool (* Flush)(const char * pSectionPrefix);
        /**< Synchronously flushes settings to persistent storage.
          *
          * @note This function is provided to flush settings from sections that require an explicit flush
          *       such as Write-Once sections -or- to flush all Read/Write settings before a reboot. It should
          *       not be used for any other purpose.
          * @note Settings will also automatically be flushed upon closure of the LTSystemSettings library.
          *
          * @param[in] pSectionPrefix Prefix of section that requires explicit flushing. Set to NULL to flush
          *            all Read/Write sections, which should only be done before a reboot.
          * @return    true if flush succeeds, false otherwise.
          */
};

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SETTINGS_LTSYSTEMSETTINGS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 */
