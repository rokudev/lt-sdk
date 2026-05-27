/*******************************************************************************
 * <lt/device/identity/LTManufacturingIdentity.h> LTManufacturingIdentity
 *
 * Interface for a manufacturing driver present only at the manufacturing time
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_IDENTITY_LTMANUFACTURINGIDENTITY_H
#define LT_INCLUDE_LT_DEVICE_IDENTITY_LTMANUFACTURINGIDENTITY_H

#include <lt/LTTypes.h>
#include <lt/device/identity/LTDeviceIdentity.h>

LT_EXTERN_C_BEGIN

/* These are the sizes of character arrays that contain manufacturing data sent to and from the
 * device. All recently used manufacturing interfaces used hexadecimal character strings to encode
 * any binary data written to the device at the manufacturing time.
 * The terminating '\0' character is included in the following sizes, so for example ChipId can
 * have 8 characters at most. Some of the sizes, MaxPidLen, for example are larger than the currently
 * used corresponding fields, therefore all LTManufacturingIdentity implementations must ensure that
 * the terminating character is added at the end of the character string, which may not be the last
 * byte of the allocated space. */
enum {
    kLTManufacturingIdentity_ChipIdLen   = 9,
    kLTManufacturingIdentity_MaxPidLen   = 6,
    kLTManufacturingIdentity_PlatformNum = 3,
    kLTManufacturingIdentity_KeyCheckLen = 3,
    kLTManufacturingIdentity_YearLen     = 5,
    kLTManufacturingIdentity_MonthLen    = 3,
};

/* The functions in this library should not be invoked at any time other than during the manufacturing
 * process. However, it is responsibility of the build and the configuration system to avoid the
 * inclusion of the manufacturing libraries in the production firmware. The functions implementing
 * this interface are not expected to check if they are being used in a manufacturing firmware. */
typedef_LTLIBRARY_ROOT_INTERFACE(LTManufacturingIdentity, 1) {

    bool (*GetChipId)(char chipId[kLTManufacturingIdentity_ChipIdLen]);
        /**<
         * @brief Get chip ID of the device
         *
         * @returns true on success, false otherwise */

    bool (*SetChipId)(const char chipId[kLTManufacturingIdentity_ChipIdLen]);
        /**<
         * @brief Set chip ID of the device
         *
         * @param chipId
         *
         * @returns true on success, false otherwise */

    bool (*GetPid)(char pid[kLTManufacturingIdentity_MaxPidLen]);
        /**<
         * @brief Return Product ID, if set, in the supplied buffer
         *
         * @param pid buffer large enough to accept kLTManufacturingIdentity_MaxPidLen bytes
         *
         * @returns true on success, false otherwise
         *
         * Product ID in usually encoded in the first four characters of Device ID */

    bool (*SetBoardRevision)(u32 revision);
        /**<
         * @brief Set Board Revision
         *
         * @param revision number that represents the board revision, mostly between 0 and 9
         *
         * @returns true on success, false otherwise
         *
         * Most platforms determine this number internally, and don't implement this API. On such
         * platforms the functions must return false. Board revision number is set externally on
         * multi-SoC platforms where the board revision is derived on only one of the SoCs and then
         * supplied to other SoCs. */

    bool (*SetPid)(const char pid[kLTManufacturingIdentity_MaxPidLen]);
        /**<
         * @brief Set Product ID
         *
         * @param pid Product ID as a C string
         *
         * @returns true on success, false otherwise */

    bool (*GetPlatformNum)(char pNum[kLTManufacturingIdentity_PlatformNum]);
        /**<
         * @brief Return Platform Number, if set, in the supplied buffer
         *
         * @param pNum buffer large enough to accept kLTManufacturingIdentity_PlatformNum bytes
         *
         * @returns true on success, false otherwise
         *
         * If Platform Number is available, at the return the buffer will contain 2-character string.
         * Currently defined values are "00" for Alta and "01" for Ranger. Other values may be added
         * as new platforms are introduced. */

    bool (*SetPlatformNum)(const char pNum[kLTManufacturingIdentity_PlatformNum]);
        /**<
         * @brief Set Platform Number
         *
         * @param pNum 2-character string  ("00" for Alta, "01" for Ranger)
         *
         * @returns true on success, false otherwise */

    bool (*GetKeyCheck)(char pKey[kLTManufacturingIdentity_KeyCheckLen]);
        /**<
         * @brief Return Key Check value, if set, in the supplied buffer
         *
         * @param pNum buffer large enough to accept kLTManufacturingIdentity_KeyCheckLen bytes
         *
         * @returns true on success, false otherwise
         *
         * If KeyCheck value is used, at the return the buffer will contain 2-character string.
         * The Key Check value is a hash of the keys written to eFuse or the provisioning partition.
         * It can be used to verify the integrity of the device keys. */

    bool (*SetKeyCheck)(const char pkey[kLTManufacturingIdentity_KeyCheckLen]);
        /**<
         * @brief Set KeyCheck
         *
         * @param pNum 2-character string representing a hexadecimal number calculated from the
         *             device keys
         *
         * @returns true on success, false otherwise */

    u8 (*GetMfgMonth)(void);
        /**<
         * @brief Get the manufacturing month as an integer between 1 and 12
         *
         * @returns integer between 1 and 12, or 0 if the manufacturing month cannot be determined
         */

    bool (*SetMfgMonth)(u8 month);
        /**<
         * @brief Set the manufacturing month as an integer between 1 and 12
         *
         * @returns true on success, false otherwise */

    u16 (*GetMfgYear)(void);
        /**<
         * @brief Get the manufacturing year as an integer (2024 and above)
         *
         * @returns integer greater than 2024, or 0 if the manufacturing year cannot be determined
         */

    bool (*SetMfgYear)(u16 year);
        /**<
         * @brief Set the manufacturing year as an integer (2024 and above)
         *
         * @returns true on success, false otherwise */

    bool (*SetAESKey)(const char aesKey[2 * kLTIdentityDeviceAESKeyBytes], u8 nKeyIndex);
        /**<
         * @brief Set device specific AES key to the value provided by the caller.
         *
         * @param aesKey    a caller-supplied hexadecimal digit array
         *
         * @param nKeyIndex currently valid indices:
         *                  1 for device-specific AESKey1,
         *                  2 for product-specific AESKey2
         *                  3 only for ak3918x SoCs
         *
         * @returns true if the key is successfully written, false otherwise */

    bool (*SetUds)(const char uds[2 * kLTIdentityUdsBytes]);
        /**<
         * @brief Set device specific UDS to the value provided by the caller.
         *
         * @param uds    a caller-supplied hexadecimal digit array
         *
         * @returns true if UDS is successfully written, false otherwise */

    /* eFuse APIs */
    void (*SetRealEfuseMode)(bool bEnable);
        /**<
         * @brief Direct eFuse operations to the real eFuse or the debug setup
         *
         * @param bEnable if true, all the following efuse read and writes are performed on the
         *                real eFuse; if false, the debug version of eFuse is used.
         *
         * @returns true on success, false otherwise */

    u32 (*SetEfuse)(const char *pEfuseContent);
        /**<
         * @brief Set the complete eFuse content
         *
         * @param pEfuseContent the string containing the values to be written to eFuse. The number
         *                      and format of the parameters is platform-specific.
         *
         * @returns 0 on success, non-zero error code otherwise */

    bool (*LockEfuse)(void);
        /**<
         * @brief Prevent further changing of eFuse content
         *
         * @returns true on success, false otherwise */

    u32 (*ReadEfuse)(u8 *pEfuseContent, LT_SIZE maxLen);
        /**<
         * @brief Copy the eFuse content to the supplied buffer
         *
         * @param pEfuseContent buffer to receive the copied eFuse content
         * @param maxLen        maximum number of bytes to copy to the buffer
         *
         * @returns number of bytes written
         */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_IDENTITY_LTMANUFACTURINGIDENTITY_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-24   commodus    created
 */
