/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoBigNum.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/*******************************************************
 * This is an internal library for little endian big numbers, LSB on the left most.
 * The arithmetic is designed for u32 integers.
 * !!! No sanity check on input. Use with caution !!!
 *
 * For big endian data, numbers must be converted to little endian first, using LT_BN_Copy_B2L.
 * Then the result need to be converted back to big endian, using LT_BN_Copy_L2B.
 */

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTDriverCryptoBigNum.h"

/**
 * @brief  Get the bit at a position of a
 * @param  a    the operand to get the bit
 * @param  pos  the position of bit, [0 ... len*32-1]
 * @param  len  the length of a, in u32
 * @return the bit (1 or 0), -1 if error
 */
int LT_BN_Get_Bit(const u32 *a, LT_SIZE pos, LT_SIZE len) {
    if (pos >= (len << 5)) {
        return -1; // out of range
    }
    u32 w = pos >> 5;
    u32 b = pos & 31;
    return (a[w] >> b) & 1;
}

/**
 * @brief Multiply two unsigned a and b, i.e. res = a * b.
 * @param res   the result
 * @param a     the first operand
 * @param aLen  the length of a, in u32
 * @param b     the second operand
 * @param bLen  the length of b, in u32
 * @param tmp   a temporary buffer holding aLen + bLen of u32 in computation
 */
void LT_BN_Multiply_Unsigned(u32 *res, const u32 *a, LT_SIZE aLen, const u32 *b, u32 bLen, struct MulULocal *tmp) {
    LT_SIZE i;
    LT_SIZE j;
    lt_memset(tmp->b, 0, (aLen + bLen) * sizeof(u32));
    for (i = 0; i < aLen; ++i) {
        tmp->carry = 0;
        for (j = 0; j < bLen; ++j) {
            tmp->val = tmp->b[i + j] + ((u64)a[i]) * ((u64)b[j]) + tmp->carry;
            tmp->b[i + j] = (u32)tmp->val;
            tmp->carry = (tmp->val >> 32);
        }
        tmp->b[i + bLen] = tmp->carry;
    }
    lt_memcpy(res, tmp->b, (aLen + bLen) * sizeof(u32));
}

/**
 * @brief  Subtract two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  a    the first operand
 * @param  b    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int LT_BN_Subtract_Unsigned(u32 *res, const u32 *a, const u32 *b, LT_SIZE len) {
    LT_SIZE i;
    u64 r;
    u32 c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)a[i]) - b[i] - c;
        res[i] = (u32)r;
        c = ((r >> 32) != 0);
    }
    return c;
}

/**
 * @brief  Add two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  a    the first operand
 * @param  b    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int LT_BN_Add_Unsigned(u32 *res, const u32 *a, const u32 *b, LT_SIZE len) {
    LT_SIZE i;
    u64 r;
    u32 c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)a[i]) + b[i] + c;
        res[i] = (u32)r;
        c = r >> 32;
    }
    return c;
}

/**
 * @brief  Increment unsigned a, i.e. a++
 * @param  a   the first operand
 * @param  len the length of a, in u32
 * @return carry
 */
int LT_BN_Increment_Unsigned(u32 *a, LT_SIZE len) {
    int c = 1;
    for(LT_SIZE i = 0; i < len; ++i) {
        ++a[i];
        if (a[i] != 0) {
            c = 0;
            break;
        }
    }
    return c;
}

/**
 * @brief  Compare two unsigned a and b
 * @param  a    the first operand
 * @param  b    the second operand
 * @param  len  the length of a and b, in u32
 * @return 0 if equal, 1 if a > b, -1 otherwise
 */
int LT_BN_Compare_Unsigned(const u32 *a, const u32 *b, LT_SIZE len) {
    for (int i = len - 1; i >= 0; --i) {
        if (a[i] > b[i]) {
            return  1;
        }
        if (a[i] < b[i]) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Add two unsigned a and b then modulo, i.e. res = (a + b) % m
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param m    the modulus
 * @param len  length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void LT_BN_Add_Mod_Unsigned(u32 *res, const u32 *a, const u32 *b, const u32 *m, LT_SIZE len) {
    int c = LT_BN_Add_Unsigned(res, a, b, len);
    if (c || LT_BN_Compare_Unsigned(res, m, len) >= 0) {
        LT_BN_Subtract_Unsigned(res, res, m, len);
    }
}

/**
 * @brief Subtract two unsigned a and b then modulo, i.e. res = (a - b) % m
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param m    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void LT_BN_Subtract_Mod_Unsigned(u32 *res, const u32 *a, const u32 *b, const u32 *m, LT_SIZE len) {
    if (LT_BN_Subtract_Unsigned(res, a, b, len)) {
        LT_BN_Add_Unsigned(res, res, m, len);
    }
}

/**
 * @brief XOR two unsigned a and b
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param len  length of res, a, and b, in u32
 */
void LT_BN_Xor_Unsigned(u32 *res, const u32 *a, const u32 *b, LT_SIZE len) {
    for (LT_SIZE i = 0; i < len; ++i) {
        res[i] = a[i] ^ b[i];
    }
}

/**
 * @brief Modulo unsigned a, i.e. res = a % m
 * @param res  the result
 * @param a    the operand
 * @param m    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  use this mod only if m is close to 2^len
 */
void LT_BN_Mod_Unsigned(u32 *res, const u32 *a, const u32 *m, LT_SIZE len) {
    if (res != a) lt_memcpy(res, a, len << 2);
    while (LT_BN_Compare_Unsigned(res, m, len) > 0) {
        LT_BN_Subtract_Unsigned(res, res, m, len);
    }
}

/**
 * @brief Copy from big endian src to little endian dst
 * @param dst     the destination buffer
 * @param dstLen  the length of destination buffer, in u8
 * @param src     the source buffer
 * @param srcLen  the length of source buffer, in u8
 * @note  must start copying from the highest address (LSB) of dst to the lowest address (LSB) of dst.
 *        src_len must <= dst_len
 */
void LT_BN_Copy_B2L(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen) {
    if (srcLen > dstLen) {
        return;
    }
    LT_SIZE i;
    for (i = 0, src += (srcLen - 1); i < srcLen; ++i, ++dst, --src) {
        *dst = *src;
    }
}

/**
 * @brief Copy from little endian src to big endian dst
 * @param dst     destination buffer
 * @param dstLen  length of destination buffer, in u8
 * @param src     source buffer
 * @param srcLen  length of source buffer, in u8
 * @note  must start copying from the lowest address (LSB) of src to the highest address (LSB) of dst.
 *        src_len must <= dst_len
 *        dst cannot be src
 */
void LT_BN_Copy_L2B(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen) {
    if (srcLen > dstLen) {
        return;
    }
    LT_SIZE i;
    for (i=0, dst += (dstLen - 1); i < srcLen; ++i, --dst, ++src) {
        *dst = *src;
    }
}

/**
 * @brief Montgomery reduction on multiprecision integers, i.e. res = t * 1/R % n
 *        MultiPrecisionREDC at https://en.wikipedia.org/wiki/Montgomery_modular_multiplication
 *
 *    N1 = -1/N % R, n0 = N1[0]
 *    R*R1 - N*N1 = 1  or R1 = 1/R % N
 *    R2 = (R*R) % N
 *
 * @param res  the result after reduction
 * @param t    the operand to be reduced (length: 2*u256_WORDS)
 * @param n    the prime modulus
 * @param n0   the reduction constant
 * @note    t is changed by this function.
 */
void LT_BN_Redux_Mont(u256 res, u32 t[2 * LTSYSTEMCRYPTO_U32_PER_U256], const u256 n, const u32 n0) {
    u32 i, j;
    u32 m, c, ct;
    u64 x;

    for (i = 0, ct = 0; i < LTSYSTEMCRYPTO_U32_PER_U256; i++) {
        c = 0;
        m = t[i] * n0;
        for (j = 0; j < LTSYSTEMCRYPTO_U32_PER_U256; j++) {
            x = t[i + j] + ((u64)m * (u64)n[j]) + c;
            t[i + j] = (u32)x;
            c = (x >> 32);
        }
        for (j = i + LTSYSTEMCRYPTO_U32_PER_U256; j < (2 * LTSYSTEMCRYPTO_U32_PER_U256); j++) {
            x = (u64)t[j] + c;
            t[j] = (u32)x;
            c = (x >> 32);
        }
        ct += c;
    }

    c = LT_BN_Subtract_Unsigned(t, t + LTSYSTEMCRYPTO_U32_PER_U256, n, LTSYSTEMCRYPTO_U32_PER_U256); // t-n
    if (ct || !c) lt_memcpy(res, t, LTSYSTEMCRYPTO_BYTES_PER_U256);     // s = t-n, when t >= n
    else lt_memcpy(res, t + LTSYSTEMCRYPTO_U32_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);   // s = t
}

/**
 * @brief Multiplication using Montgomery reduction, i.e. res = a * b * 1/R % n
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param n    the prime modulus
 * @param n0   the reduction constant
 * @param tmp  a temporary buffer for computation
 */
void LT_BN_Multiply_Mont(u256 res, const u256 a, const u256 b, const u256 n, const u32 n0, struct MtMulLocal *tmp) {
    LT_BN_Multiply_Unsigned(tmp->t, a, LTSYSTEMCRYPTO_U32_PER_U256, b, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ml);
    LT_BN_Redux_Mont(res, tmp->t, n, n0);
}

/**
 * @brief Modulo using Montgomery reduction, i.e. res = a % mod
 * @param res  the result
 * @param a    the operand
 * @param r2   the reverse reduction constant
 * @param n    the prime modulus
 * @param n0   the reduction constant
 * @param tmp  a temporary buffer for computation
 * @note  a and res are in normal space
 */
void LT_BN_Mod_Mont(u256 res, u32 a[2 * LTSYSTEMCRYPTO_U32_PER_U256], const u256 r2, const u256 n, const u32 n0, struct MtMulLocal *tmp) {
    LT_BN_Redux_Mont(res, a, n, n0);
    LT_BN_Multiply_Mont(res, res, r2, n, n0, tmp);
}

/**
 * @brief Check if a is 0, constant time
 * @param a    the operand
 * @param len  the length, in u32
 * @return true if a is 0
 * @return false if a is not 0
 */
bool LT_BN_IsZero(u32 *a, LT_SIZE len) {
    u32 b = a[0];
    for (LT_SIZE i = 1; i < len; ++i) b |= a[i];
    return b == 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Feb-22   gallienus   created
 */
