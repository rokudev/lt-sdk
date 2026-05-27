/*******************************************************************************
 * <lt/driver/identity/LTDriverIdentity.h> LTDriverIdentity
 *
 * Interface for a singleton driver with no configuration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_IDENTITY_LTDEVICEIDENTITY_H
#define LT_INCLUDE_LT_DRIVER_IDENTITY_LTDRIVERIDENTITY_H

#include <lt/LTTypes.h>
#include <lt/core/LTSecurity.h>
#include <lt/device/identity/LTDeviceIdentity.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverIdentity, 1) {

    const char *(*GetDeviceId)(void);
        /**<
         * @brief Return Device ID.
         *
         * Device ID for IoT products starts with "S0" or "S1".
         *
         * @returns valid Device ID (starts with "S0" or "S1"), or an empty string if Device ID
         *          is not available */

    const char *(*GetSerialNumber)(void);
        /**<
         * @brief Return Serial Number.
         *
         * Serial Number for IoT products starts with "X0".
         *
         * @returns valid Serial Number (starts with "X0"), or an empty string if Serial Number
         *          is not available */

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

    bool (*GetUds)(u8 uds[kLTIdentityUdsBytes]);
        /**<
         * @brief Set or get Unique Device Secret (UDS)
         *        Only set once. If set again, will return false
         *
         * @param uds  the UDS bytes
         *
         * @returns true on success, false otherwise
         */

    bool (*SetUds)(const u8 uds[kLTIdentityUdsBytes]);
        /**<
         * @brief Set Unique Device Secret (UDS)
         *        Only set once. If set again, will return false
         *
         * @param uds  the UDS bytes
         *
         * @returns true on success, false otherwise
         */

    bool (*IsValid)(void);
        /**<
         * @brief Check if a device has a valid identity.
         * @returns true if identity is available and valid, otherwise false.
         * @ Note when implementing IsValid() in a driver, and there are no suitable checks,
         *   then make IsValid() simply return true;
         */

    LTSecurityStatus (*IsUnsecured)(void);
        /**<
         * @brief Check if a device is unsecured.
         * @returns kLTSecurityStatus_Success if device is unsecured.
         * @returns any other value may be returned if device is secured.
         * @ Note when implementing IsUnsecured() in a driver, and there is no suitable way to
         *   make the determination, simply return kLTSecurityStatus_Fail.  This signifies that
         *   the device is secured.
         */

    u32 (*GetLTATSize)(void);
        /**<
         * @brief Obtain total size of the LTAT in bytes, including signature.
         * @returns number of bytes, zero if LTAT is not supported.
         */

    LTSecurityStatus (*AuthenticateLTAT)(LTSecurityLTATPayload * pLTAT, u32 * pClaimMask);
        /**<
         * @brief Authenticate signature of LT Authentication Token (LTAT).
         * @note Function must check the signature of the token and set the claim mask.
         *       This function assumes the magic number, version and identity have already
         *       been checked.
         * @param pLTAT Pointer to LTAT payload followed by signature.
         * @param pClaimMask Pointer to claim mask to set if LTAT authenticated.
         * @returns kLTSecurityStatus_Success if LTAT is authentic.
         * @returns any other value may be returned if authentication fails.
         */

    LTSecurityStatus (*CheckLTATClaims)(LTSecurityClaimGroup group, LTSecurityClaimMask mask);
        /**<
         * @brief Check LT Authentication Token (LTAT) claims.
         * @note This is an optional function that can be implemented by an identity driver
         *       that manages LTAT claims internally. If this function is not implemented
         *       the LTDeviceIdentity library will try to read an LTAT from the appropriate
         *       flash partition and parse it to check the claims.
         * @param group LTAT claim group to check.
         * @param mask Mask of bits to check in the claim group.
         * @returns kLTSecurityStatus_Success if all bits in mask are present in an authenticated LTAT.
         * @returns Any other value may be returned if LTAT doesn't exist or claim check fails.
         */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_IDENTITY_LTDRIVERIDENTITY_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  07-Mar-23   commodus    created
 *  11-Nov-24   commodus    removed GetChipId
 */
