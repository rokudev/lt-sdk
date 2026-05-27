/*******************************************************************************
 * <lt/device/authentication/LTDeviceAuthentication.h> LTDeviceAuthentication
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_AUTHENTICATION_LTDEVICEAUTHENTICATION_H
#define LT_INCLUDE_LT_DEVICE_AUTHENTICATION_LTDEVICEAUTHENTICATION_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/schell/LTSystemSchell.h>

LT_EXTERN_C_BEGIN

enum {
    kLTAuthenticationKeyBytes = AES128_BLOCK_LENGTH,
};

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceAuthentication, 1);

struct LTDeviceAuthentication {

    INHERIT_DEVICE_LIBRARY_BASE
    bool (*CalculateAuthKey)(u8 authKey[kLTAuthenticationKeyBytes]);
        /**<
         * @brief Calculate authentication key using device-specific algorithm.
         *
         * @param authKey Output buffer for authentication key (16 bytes)
         * @returns true if authentication key was successfully calculated, false otherwise */

    bool (*AuthEncode)(u8 const in[kLTAuthenticationKeyBytes], u8 out[kLTAuthenticationKeyBytes]);
        /**<
         * @brief Encode data using authentication key.
         *
         * @param in  Input data (16 bytes)
         * @param out Output encoded data (16 bytes)
         * @returns true if encoding was successful, false otherwise */

    bool (*AuthDecode)(u8 const in[kLTAuthenticationKeyBytes], u8 out[kLTAuthenticationKeyBytes]);
        /**<
         * @brief Decode data using authentication key.
         *
         * @param in  Input encoded data (16 bytes)
         * @param out Output decoded data (16 bytes)
         * @returns true if decoding was successful, false otherwise */

    void (*GetRandom16)(u8 data[kLTAuthenticationKeyBytes]);
        /**<
         * @brief Generate 16 bytes of random data.
         *
         * @param data Output buffer for random data (16 bytes) */

    bool (*ValidateKeyCheck)(LTSystemSchell *shell);
        /**<
         * @brief Validate key check efuse data for authentication.
         *
         * @param shell Shell interface for debug output
         * @returns true if key check validation passed, false otherwise */

    bool (*RunAuthentication)(LTSystemSchell *shell);
        /**<
         * @brief Run authentication protocol demonstration.
         *
         * @param shell Shell interface for debug output
         * @returns true if authentication protocol completed successfully, false otherwise */

    bool (*IsSecureDevice)(void);
        /**<
         * @brief Check if device is configured as secure.
         *
         * @returns true if device is secure, false otherwise */

    bool (*IsUsingDevKeys)(void);
        /**<
         * @brief Check if device is using DEV (development) crypto keys.
         *
         * @returns true if device is using DEV keys, false otherwise */

    u32 (*GetChipID)(void);
        /**<
         * @brief Get device chip ID.
         *
         * @returns chip ID or 0xffffffff if not available */

};

typedef_LTLIBRARY_INTERFACE(ILTDriverAuthenticationDeviceUnit, 1) {

    /*
        Note: Any or all of these device unit functions may be implemented or
        omitted in order to provide device-specific logic. Any functions not
        implemented will default back to the common implementation of
        LTDeviceAuthentication.

        IOW, none of these need to be implemented unless there's a platform
        specific need for something other than the default behavior.
    */

    bool (*AES_Encode_ECB)(const u8 *keyPtr, const u8 *input, u8 *output, LT_SIZE dataLen);
        /**<
         * @brief Encode data using AES in ECB mode (emulated via CBC with zero IV).
         *
         * @param keyPtr  Input key pointer (nullptr uses AES_KEY_1 from efuse)
         * @param input   Input data to encode
         * @param output  Output buffer for encoded data
         * @param dataLen Length of data to encode
         * @returns true if encoding was successful, false otherwise */

    bool (*AES_Decode_ECB)(const u8 *keyPtr, const u8 *input, u8 *output, LT_SIZE dataLen);
        /**<
         * @brief Decode data using AES in ECB mode (emulated via CBC with zero IV).
         *
         * @param keyPtr  Input key pointer (nullptr uses AES_KEY_1 from efuse)
         * @param input   Input data to decode
         * @param output  Output buffer for decoded data
         * @param dataLen Length of data to decode
         * @returns true if decoding was successful, false otherwise */

    bool (*IsSecureDevice)(void);
        /**<
         * @brief Check if device is configured as secure.
         *
         * @returns true if device is secure, false otherwise */

    bool (*IsUsingDevKeys)(void);
        /**<
         * @brief Check if device is using DEV (development) crypto keys.
         *
         * @returns true if device is using DEV keys, false otherwise */

    u32 (*GetChipID)(void);
        /**<
         * @brief Get device chip ID.
         *
         * @returns chip ID or 0xffffffff if not available */

    u32 (*GetChipIDForKeyCheck)(void);
        /**<
         * @brief Get chip ID for key validation (handles platform-specific byte swap).
         *
         * @returns chip ID for key check or 0xffffffff if not available */

    bool (*ValidateKeyCheck)(LTSystemSchell *shell);
        /**<
         * @brief Validate key check efuse data for authentication.
         *
         * @param shell Shell interface for debug output
         * @returns true if key check validation passed, false otherwise */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_AUTHENTICATION_LTDEVICEAUTHENTICATION_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Jul-25   claudius    created from RCUCrypto functionality
 */
