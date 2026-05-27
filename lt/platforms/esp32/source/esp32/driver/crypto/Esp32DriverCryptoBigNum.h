/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoBigNum.h
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

/*******************************************************
 * This is an internal library for little endian big numbers, LSB on the left most.
 * The arithmetic is designed for u32 integers.
 * !!! No sanity check on input. Use with caution !!!
 * 
 * For big endian data, numbers must be converted to little endian first, using ESP32_BN_Copy_B2L.
 * Then the result need to be converted back to big endian, using ESP32_BN_Copy_L2B.
 */

#ifndef PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32BIGNUM_H
#define PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32BIGNUM_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include "Esp32DriverCrypto.h"

/* hardware big number arithmetic */

/**
 * @brief   Clear X, Y and Z RSA regs.
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_ClearRSA(LT_SIZE RSALen);

/**
 * @brief  Set RSA parameters, N and N1, to engine, and R2 and RSALen to context.
 *         So that following RSA computation won't need to set these parameters to engine again.
 *         It must guarantee that the engine's parameters are kept untouched until next explicit BN_Set_Mod.
 *         The following calls are ESP32_BN_Pow_Mod_Preset and ESP32_BN_Multiply_Mod_Preset
 * @param ctx     the RSA context
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param len     the length of N and R2, in u32, len <= RSALen
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 * @return   Error code
 */
LTSystemCryptoResult ESP32_BN_Set_Mod(ESP32_RSA_CTX *ctx, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen);

/**
 * @brief Modular pow two unsigned a and b, i.e. res = a ^ b % n.
 *        Shall only be called after the context is set already (ESP32_BN_Set_Mod)
 * @param ctx  the RSA context
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 */
void ESP32_BN_Pow_Mod_Preset(const ESP32_RSA_CTX *ctx, u32 *res, const u32 *A, const u32 *B);

/**
 * @brief Modular pow two unsigned a and b, i.e. res = a ^ b % n.
 * @param res     the result
 * @param A       the first operand
 * @param B       the second operand
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param len     the length of A, B, N, R2, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 * @note    
 *          Use example:
 *          ESP32_RSA_CTX ctx;
 *          ESP32_BN_Set_Mod(&ctx, N, R2, u256_WORDS, N1, RSA512Len);
 *          u256 res, a, b;
 *          ESP32_BN_Pow_Mod_Preset(&ctx, res, a, b);
 */
void ESP32_BN_Pow_Mod_HW(u32 *res, const u32 *A, const u32 *B, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen);

/**
 * @brief Modular multiply two unsigned a and b, i.e. res = a * b % n.
 *        Shall only be called after the context is set already (ESP32_BN_Set_Mod)
 * @param ctx  the RSA context
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @note
 *          Use example:
 *          ESP32_RSA_CTX ctx;
 *          ESP32_BN_Set_Mod(&ctx, N, R2, u256_WORDS, N1, RSA512Len);
 *          u256 res, a, b;
 *          ESP32_BN_Multiply_Mod_Preset(&ctx, res, a, b);
 */
void ESP32_BN_Multiply_Mod_Preset(const ESP32_RSA_CTX *ctx, u32 *res, const u32 *A, const u32 *B);

/**
 * @brief Modular multiply two unsigned a and b, i.e. res = a * b % n.
 * @param res     the result
 * @param A       the first operand
 * @param B       the second operand
 * @param N       the modulus
 * @param R2      R2, R^2 % N, the length must be RSALen
 * @param len     the length of A, B, N, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Multiply_Mod_HW(u32 *res, const u32 *A, const u32 *B, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen);

/**
 * @brief Multiply two unsigned a and b, i.e. res = a * b.
 * @param res     the result
 * @param A       the first operand
 * @param ALen    the length of a, in u32
 * @param B       the second operand
 * @param BLen    the length of b, in u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Multiply_HW(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *B, LT_SIZE BLen, LT_SIZE RSALen);

/**
 * @brief res = a % N
 * @param res     the result, the length must be NLen in u32
 * @param A       the number
 * @param ALen    the length of A, in u32
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param NLen    the length of N and R2, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Mod_HW(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *N, const u32 *R2, LT_SIZE NLen, u32 N1, LT_SIZE RSALen);

/*********************** software big number arithmetic **********************/

/**
 * @brief  Get the bit at a position of a
 * @param  A    the operand to get the bit
 * @param  pos  the position of bit, [0 ... len*32-1]
 * @param  len  the length of a, in u32
 * @return the bit (1 or 0), -1 if error
 */
int ESP32_BN_Get_Bit(const u32 *A, LT_SIZE pos, LT_SIZE len);

struct MulULocal {
    u32 b[LTSYSTEMCRYPTO_U32_PER_U256 * 4];
    u64 val;
    u32 carry;
};
/**
 * @brief Multiply two unsigned a and b, i.e. res = a * b.
 * @param res   the result
 * @param A     the first operand
 * @param ALen  the length of a, in u32
 * @param B     the second operand
 * @param BLen  the length of b, in u32
 * @param tmp   a temporary buffer holding ALen + BLen of u32 in computation
 */
void ESP32_BN_Multiply_Unsigned(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *B, LT_SIZE BLen, struct MulULocal *tmp);

/**
 * @brief  Subtract two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int ESP32_BN_Subtract_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len);
  
/**
 * @brief  Add two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int ESP32_BN_Add_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len);

/**
 * @brief  Increment unsigned a, i.e. a++
 * @param  A   the first operand
 * @param  len the length of a, in u32
 * @return carry
 */
int ESP32_BN_Increment_Unsigned(u32 *A, LT_SIZE len);

/**
 * @brief  Compare two unsigned a and b
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a and b, in u32
 * @return 0 if equal, 1 if a > b, -1 otherwise
 */
int ESP32_BN_Compare_Unsigned(const u32 *A, const u32 *B, LT_SIZE len);

/**
 * @brief Add two unsigned a and b then modulo, i.e. res = (a + b) % m
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param M    the modulus
 * @param len  length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void ESP32_BN_Add_Mod_Unsigned(u32 *res, const u32 *A, const u32 *B, const u32 *M, LT_SIZE len);

/**
 * @brief Subtract two unsigned a and b then modulo, i.e. res = (a - b) % m
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param M    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void ESP32_BN_Subtract_Mod_Unsigned(u32 *res, const u32 *A, const u32 *B, const u32 *M, LT_SIZE len);

/**
 * @brief XOR two unsigned a and b
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param len  length of res, a, and b, in u32
 */
void ESP32_BN_Xor_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len);

/**
 * @brief Modulo unsigned a, i.e. res = a % m
 * @param res  the result
 * @param A    the operand
 * @param M    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  use this mod only if m is close to 2^len
 */
void ESP32_BN_Mod_Unsigned(u32 *res, const u32 *A, const u32 *M, LT_SIZE len);

/**
 * @brief Copy from big endian src to little endian dst
 * @param dst     the destination buffer
 * @param dstLen  the length of destination buffer, in u8
 * @param src     the source buffer
 * @param srcLen  the length of source buffer, in u8
 * @note  must start copying from the highest address (LSB) of dst to the lowest address (LSB) of dst.
 *        src_len must <= dst_len
 */
void ESP32_BN_Copy_B2L(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen);


/**
 * @brief Copy from little endian src to big endian dst
 * @param dst     destination buffer
 * @param dstLen  length of destination buffer, in u8
 * @param src     source buffer
 * @param srcLen  length of source buffer, in u8
 * @note  must start copying from the lowest address (LSB) of src to the highest address (LSB) of dst.
 *        src_len must <= dst_len
 */
void ESP32_BN_Copy_L2B(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen);

LT_EXTERN_C_END
#endif  // PLATFORMS_ESP32_SOURCE_ESP32_DRIVER_CRYPTO_ESP32BIGNUM_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-May-22   gallienus   created
 */
