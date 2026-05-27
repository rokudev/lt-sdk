/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoAes128.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOAES128_H
#define LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOAES128_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/**
 * @brief Schedule key
 * @param key  the key
 * @param ek   the expanded key after scheduling
 */
void LT_AES128_Keysched(const u8 key[AES128_KEY_LENGTH], u32 ek[44]);

/**
 * @brief Encrypt data
 * @param input  the input data
 * @param ek     the expanded key
 * @param output the output data after encryption
 * @note  the length of data must be 16 Bytes
 */
void LT_AES128_Encrypt(const u8 input[AES128_BLOCK_LENGTH], const u32 ek[44], u8 output[AES128_BLOCK_LENGTH]);

/**
 * @brief Decrypt data
 * @param input  the input data
 * @param ek     the expanded key
 * @param output the output data after decryption
 * @note  the length of data must be 16 Bytes
 */
void LT_AES128_Decrypt(const u8 input[AES128_BLOCK_LENGTH], const u32 ek[44], u8 output[AES128_BLOCK_LENGTH]);

LT_EXTERN_C_END
#endif  // LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOAES128_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Feb-22   gallienus   created
 */
