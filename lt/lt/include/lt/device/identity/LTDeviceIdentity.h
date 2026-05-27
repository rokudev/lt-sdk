/*******************************************************************************
 * <lt/device/identity/LTDeviceIdentity.h> LTDeviceIdentity
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_IDENTITY_LTDEVICEIDENTITY_H
#define LT_INCLUDE_LT_DEVICE_IDENTITY_LTDEVICEIDENTITY_H

#include <lt/LTTypes.h>
#include <lt/core/LTSecurity.h>
#include <lt/system/crypto/LTSystemCrypto.h>

LT_EXTERN_C_BEGIN

enum {
    kLTIdentityDeviceAESKeyBytes = AES128_KEY_LENGTH,
    kLTIdentityUdsBytes          = SHA256_HASH_LENGTH,
};

typedef_LTLIBRARY_ROOT_INTERFACE(LTDeviceIdentity, 1) {
    const char *(*GetManufacturer)(void);
        /**<
         * @brief Return Device Manufacturer.
         *
         * @returns valid Device Manufacturer, or an empty string if not available */

    const char *(*GetModel)(void);
        /**<
         * @brief Return Device Model.
         *
         * @returns valid Device Model, or an empty string if not available */

    const char *(*GetType)(void);
        /**<
         * @brief Return Device Type.
         *         *
         * @returns valid Device Type, or an empty string if not available */

    const char *(*GetDeviceId)(void);
        /**<
         * @brief Return Device ID.
         *         *
         * @returns valid Device ID, or an empty string if Device ID
         *          is not available */

    const char *(*GetSerialNumber)(void);
        /**<
         * @brief Return Serial Number.
         *         *
         * @returns valid Serial Number  or an empty string if Serial Number is not available */

    void (*GetMac)(u8 mac[6]);
        /**<
         * @brief Get mac address of the device */

    u32 (*GetBoardRevision)(void);
        /**<
         * @brief Get the revision number of the circuit board in the device
         *
         * @returns the board revision number
         *          LT_U32_MAX on platforms that do not support board revision */

    bool (*GetAESKey)(u8 aesKey[kLTIdentityDeviceAESKeyBytes], u8 nKeyIndex);
        /**<
         * @brief Return device specific AES key in buffer provided by the caller.
         *
         * @param aesKey    a caller-supplied byte array
         *
         * @param nKeyIndex currently valid indices:
         *                  1 for device-specific AESKey1,
         *                  2 for product-specific AESKey2
         *
         * @returns true if the buffer contains a valid key upon return, false otherwise */

    bool (*SetUds)(const u8 uds[kLTIdentityUdsBytes]);
        /**<
         * @brief Set Unique Device Secret (UDS)
         *        Only set once. If set again, will return false
         *
         * @param uds  the UDS bytes
         *
         * @returns true on success, false otherwise */

    bool (*GetUds)(u8 uds[kLTIdentityUdsBytes]);
        /**<
         * @brief Set or get Unique Device Secret (UDS)
         *        Only set once. If set again, will return false
         *
         * @param uds  the UDS bytes
         *
         * @returns true on success, false otherwise */

    bool (*IsValid)(void);
        /**<
         * @brief Check if a device has a valid identity.
         * @returns true if identity is available and valid, otherwise false.
         */

    LTSecurityStatus (*IsUnsecured)(void);
        /**<
         * @brief Check if a device is not secured.
         * @returns kLTSecurityStatus_Success if device is unsecured.
         * @returns Any other value may be returned if device is secured.
         */

    LTSecurityStatus (*IsManufacturingFirmware)(void);
        /**<
         * @brief Check if a device is running a manufacturing firmware.
         * @returns kLTSecurityStatus_Success if device is running a manufacturing firmware.
         * @returns Any other value may be returned if device is not running a manufacturing
         *          firmware.
         */

    bool (*InstallLTAT)(const u8 *ltat, u32 size);
        /**<
         * @brief Install or Remove LT Authentication Token (LTAT).
         * @info Sanity checks are performed on LTAT before installation.
         *
         * @param ltat  pointer to complete LTAT buffer, including header, body and signature.
         *              If ltat is NULL then the LTAT is erased from the device.
         * @param len   the size of LTAT in bytes, ignored if ltat is NULL.
         * @returns true if the LTAT was successfully written or erased.
         * @returns false if provided LTAT failed sanity checking or another error occurred.
         */

    LTSecurityStatus (*CheckLTATClaims)(LTSecurityClaimGroup group, LTSecurityClaimMask mask);
        /**<
         * @brief Check LT Authentication Token (LTAT) claims.
         *
         * @param group LTAT claim group to check.
         * @param mask Mask of bits to check in the claim group.
         *
         * @returns kLTSecurityStatus_Success if all bits in mask are present in an authenticated LTAT.
         * @returns Any other value may be returned if LTAT doesn't exist or claim check fails.
         */
} LTLIBRARY_INTERFACE;

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_IDENTITY_LTDEVICEIDENTITY_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  03-Mar-23   commodus    created
 *  11-Nov-24   commodus    removed GetChipId
 */
