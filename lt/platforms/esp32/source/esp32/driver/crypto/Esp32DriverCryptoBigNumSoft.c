/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoBigNumSoft.c
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

/*******************************************************
 * The software half of the big number library: the arithmetic that touches no
 * hardware and is therefore shared by every chip in this platform.  The RSA
 * peripheral half lives in <chip>/Esp32DriverCryptoBigNum.c.
 *
 * This is an internal library for little endian big numbers, LSB on the left most.
 * The arithmetic is designed for u32 integers.
 * !!! No sanity check on input. Use with caution !!!
 * 
 * For big endian data, numbers must be converted to little endian first, using ESP32_BN_Copy_B2L.
 * Then the result need to be converted back to big endian, using ESP32_BN_Copy_L2B.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include "Esp32DriverCryptoBigNum.h"

/*********************** software big number arithmetic **********************/

/**
 * @brief  Get the bit at a position of a
 * @param  A    the operand to get the bit
 * @param  pos  the position of bit, [0 ... len*32-1]
 * @param  len  the length of a, in u32
 * @return the bit (1 or 0), -1 if error
 */
int ESP32_BN_Get_Bit(const u32 *A, LT_SIZE pos, LT_SIZE len) {
    if (pos >= (len << 5)) {
        return -1; // out of range
    }
    u32 w = pos >> 5;
    u32 b = pos & 31;
    return (A[w] >> b) & 1;
}

/**
 * @brief Multiply two unsigned a and b, i.e. res = a * b.
 * @param res   the result
 * @param A     the first operand
 * @param ALen  the length of a, in u32
 * @param B     the second operand
 * @param BLen  the length of b, in u32
 * @param tmp   a temporary buffer holding aLen + bLen of u32 in computation
 */
void ESP32_BN_Multiply_Unsigned(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *B, LT_SIZE BLen, struct MulULocal *tmp) {
    LT_SIZE i;
    LT_SIZE j;
    lt_memset(tmp->b, 0, (ALen + BLen) * sizeof(u32));
    for (i = 0; i < ALen; ++i) {
        tmp->carry = 0;
        for (j = 0; j < BLen; ++j) {
            tmp->val = tmp->b[i + j] + ((u64)A[i]) * ((u64)B[j]) + tmp->carry;
            tmp->b[i + j] = (u32)tmp->val;
            tmp->carry = (tmp->val >> 32);
        }
        tmp->b[i + BLen] = tmp->carry;
    }
    lt_memcpy(res, tmp->b, (ALen + BLen) * sizeof(u32));
}

/**
 * @brief  Subtract two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int ESP32_BN_Subtract_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len) {
    LT_SIZE i;
    u64 r;
    u32 c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)A[i]) - B[i] - c;
        res[i] = (u32)r;
        c = ((r >> 32) != 0);
    }
    return c;
}

/**
 * @brief  Add two unsigned a and b, i.e. res = a - b.
 * @param  res  the result
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a, b, and res, in u32
 * @return carry
 */
int ESP32_BN_Add_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len) {
    LT_SIZE i;
    u64 r;
    u32 c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)A[i]) + B[i] + c;
        res[i] = (u32)r;
        c = r >> 32;
    }
    return c;
}

/**
 * @brief  Increment unsigned a, i.e. a++
 * @param  A   the first operand
 * @param  len the length of a, in u32
 * @return carry
 */
int ESP32_BN_Increment_Unsigned(u32 *A, LT_SIZE len) {
    int c = 1;
    for(LT_SIZE i = 0; i < len; ++i) {
        ++A[i];
        if (A[i] != 0) {
            c = 0;
            break;
        }
    }
    return c;
}

/**
 * @brief  Compare two unsigned a and b
 * @param  A    the first operand
 * @param  B    the second operand
 * @param  len  the length of a and b, in u32
 * @return 0 if equal, 1 if a > b, -1 otherwise
 */
int ESP32_BN_Compare_Unsigned(const u32 *A, const u32 *B, LT_SIZE len) {
    for (int i = len - 1; i >= 0; --i) {
        if (A[i] > B[i]) {
            return  1;
        }
        if (A[i] < B[i]) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Add two unsigned a and b then modulo, i.e. res = (a + b) % m
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param M    the modulus
 * @param len  length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void ESP32_BN_Add_Mod_Unsigned(u32 *res, const u32 *A, const u32 *B, const u32 *M, LT_SIZE len) {
    int c = ESP32_BN_Add_Unsigned(res, A, B, len);
    if (c || ESP32_BN_Compare_Unsigned(res, M, len) >= 0) {
        ESP32_BN_Subtract_Unsigned(res, res, M, len);
    }
}

/**
 * @brief Subtract two unsigned a and b then modulo, i.e. res = (a - b) % m
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param M    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  a < mod and b < mod
 */
void ESP32_BN_Subtract_Mod_Unsigned(u32 *res, const u32 *A, const u32 *B, const u32 *M, LT_SIZE len) {
    if (ESP32_BN_Subtract_Unsigned(res, A, B, len)) {
        ESP32_BN_Add_Unsigned(res, res, M, len);
    }
}

/**
 * @brief XOR two unsigned a and b
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @param len  length of res, a, and b, in u32
 */
void ESP32_BN_Xor_Unsigned(u32 *res, const u32 *A, const u32 *B, LT_SIZE len) {
    for (LT_SIZE i = 0; i < len; ++i) {
        res[i] = A[i] ^ B[i];
    }
}

/**
 * @brief Modulo unsigned a, i.e. res = a % m
 * @param res  the result
 * @param A    the operand
 * @param M    the modulus
 * @param len  the length of res, a, b and mod, in u32
 * @note  use this mod only if m is close to 2^len
 */
void ESP32_BN_Mod_Unsigned(u32 *res, const u32 *A, const u32 *M, LT_SIZE len) {
    lt_memcpy(res, A, len << 2);
    while (ESP32_BN_Compare_Unsigned(res, M, len) > 0) {
        ESP32_BN_Subtract_Unsigned(res, res, M, len);
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
void ESP32_BN_Copy_B2L(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen) {
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
 */
void ESP32_BN_Copy_L2B(u8 *dst, LT_SIZE dstLen, const u8 *src, LT_SIZE srcLen) {
    if (srcLen > dstLen) {
        return;
    }
    LT_SIZE i;
    for (i=0, dst += (dstLen - 1); i < srcLen; ++i, --dst, ++src) {
        *dst = *src;
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-May-22   gallienus   created
 *  27-Aug-26   claudius    split out of Esp32DriverCryptoBigNum.c, so the esp32
 *                          and esp32s3 drivers share it
 */
