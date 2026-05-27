/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCrypto25519.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTDriverCrypto25519.h"
#include "LTDriverCryptoBigNum.h"

/*******************************************************************************
 Curve 25519 constants
 const u32 kC25519_N[LTSYSTEMCRYPTO_U32_PER_U256];    // Curve 25519 modulus
 const u32 kC25519_L[LTSYSTEMCRYPTO_U32_PER_U256];    // Curve 25519 group order
 const u32 kC25519_N1;                                // -1/N % R, then take the lowest u32
 const u32 kC25519_R2[LTSYSTEMCRYPTO_U32_PER_U256];   // R^2 % N
 const u32 kC25519_N2[LTSYSTEMCRYPTO_U32_PER_U256];   // N - 2
 const u32 kC25519_L1;                                // -1/L % R, then take the lowest u32
 const u32 kC25519_Q2[LTSYSTEMCRYPTO_U32_PER_U256];   // R^2 % L
 ******************************************************************************/

// Shared curve constants for X25519 and Ed25519
/* C25519_N = 0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed, curve modulus */
static const u32 kC25519_N[LTSYSTEMCRYPTO_U32_PER_U256] = {0xFFFFFFED,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x7FFFFFFF};
/* C25519_L = 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed, curve group order */
static const u32 kC25519_L[LTSYSTEMCRYPTO_U32_PER_U256] = {0x5CF5D3ED,0x5812631A,0xA2F79CD6,0x14DEF9DE,0x00000000,0x00000000,0x00000000,0x10000000};
/* LB = 0xfffffffffffffffffffffffffffffffeb2106215d086329a7ed9ce5a30a2c131bL */
static const u32 kEd25519_LB[9] = {0x0A2C131B,0xED9CE5A3,0x086329A7,0x2106215D,0xFFFFFFEB,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x0000000F};

const u32 *LT_GetC25519_N(void) {
    return kC25519_N;
}

const u32 *LT_GetC25519_L(void) {
    return kC25519_L;
}

/**
 * @brief res = (a * b) % N. N is (2^255 - 19)
 *
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param tmp  a temporary buff for computation
 */
void LT_C25519_Multiply_ModN(u256 res, const u256 a, const u256 b, struct C25519MulNLocal *tmp) {
    LT_BN_Multiply_Unsigned(tmp->r, a, LTSYSTEMCRYPTO_U32_PER_U256, b, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ub);

    // NIST SP 800-186, B = A % N = (38 * A1 + A0) % 2N,
    // We reduce by B = A % N = (38 * A1 + A0) % N
    // calculate 38 * A1 + A0
    u64 s = 0; // sum
    u32 c = 0; // carry
    u32 i;
    for (i = 0; i < 8; ++i) {
        s = c + (u64)tmp->r[i] + ((u64)tmp->r[i + 8]) * 38;
        tmp->r[i] = (u32)s;
        c = s >> 32;
    }
    // reduce bit 255 (2^255 % N = 19) and higher ((A[8] * 2^256) % N = (A[8] * 38) % N)
    c = (tmp->r[7] >> 31) * 19 + c * 38;
    tmp->r[7] &= 0x7FFFFFFF;
    for (i = 0; i < 8 && c; ++i) {
        s = tmp->r[i] + (u64)c;
        tmp->r[i] = (u32)s;
        c = s >> 32;
    }
    // check if u[7...0] >= (2^255 - 19)
    // u[15...8] = u[7...0] + 19
    c = 19;
    for (i = 0; i < 8; ++i) {
        s = tmp->r[i] + (u64)c;
        tmp->r[i + 8] = (u32)s;
        c = s >> 32;
    }

    // if u[7...0] > N, then u[15] >= 0x80000000
    if (tmp->r[15] >= 0x80000000) {
        tmp->r[15] &= 0x7FFFFFFF; // u[15...8] - 2^255 = u[7...0] + 19 - 2^255 = u[7...0] - (2^255 - 19)
        lt_memcpy(res, &tmp->r[8], LTSYSTEMCRYPTO_BYTES_PER_U256);
    } else {
        lt_memcpy(res, &tmp->r[0], LTSYSTEMCRYPTO_BYTES_PER_U256);
    }
}

/**
 * @brief Inversion mod N
 *        res = a^-1 = a^(N-2) % N, because Fermat's theorem 1 = a^(N-1) % N
 *        Only works for the constant N
 * @param res  the result
 * @param a    the operand to inverse
 * @param tmp  a temporary buffer for computation
 * @note  both a and res are normal u256
 */
void LT_C25519_Inverse_ModN(u256 res, const u256 a, struct C25519InvLocal *tmp) {
    // Montgomery ladder, multiplication
    // bit[254] = 1
    lt_memcpy(tmp->r0, a, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r0, tmp->r0, &tmp->nb);
    // bit[253...5] = 1
    for (u32 m = 253; m >= 5; --m) {
        LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r1, &tmp->nb);
        LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->r1, &tmp->nb);
    }
    // bit[4] = 0
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r0, tmp->r1, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r0, &tmp->nb);
    // bit[3] = 1
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r1, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->r1, &tmp->nb);
    // bit[2] = 0
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r0, tmp->r1, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r0, &tmp->nb);
    // bit[1] = 1
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r1, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->r1, &tmp->nb);
    // bit[0] = 1
    LT_C25519_Multiply_ModN(res, tmp->r0, tmp->r1, &tmp->nb);
}

/**
 * @brief Barrett reduction, res = a % L
 *        Precompute:
 *            b = u32 = 2^32, k = ceil(log2(L)) = 8
 *            LB = floor(b^2k / L), has 9 u32 numbers
 *        Reduction:
 *            r = floor(a * LB / b^2k)
 *            r = r * L
 *            r = a - r
 *            if (r >= L) r = r - L
 *            res = r
 *
 * @param res  the result of reduction
 * @param a    the number to reduce
 * @param len  the length of a, in u32
 * @param tmp  a temporary buffer for computation
 */
void LT_C25519_ModL(u256 res, const u32 *a, LT_SIZE len, struct C25519ModLLocal *tmp) {
    // r[16] = floor(a * LB / b^2k)
    LT_BN_Multiply_Unsigned(tmp->r, a, len, kEd25519_LB, LTSYSTEMCRYPTO_U32_PER_U256 + 1, &tmp->ub);
    // r = r * L
    LT_BN_Multiply_Unsigned(tmp->r, tmp->r + LTSYSTEMCRYPTO_U32_PER_U256 * 2, len + 1 - LTSYSTEMCRYPTO_U32_PER_U256, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ub);
    // r = a - r
    LT_BN_Subtract_Unsigned(tmp->r, a, tmp->r, len);
    if (LT_BN_Compare_Unsigned(tmp->r, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
        LT_BN_Subtract_Unsigned(tmp->r, tmp->r, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256);
    }
    lt_memcpy(res, tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256);
}

/**
 * @brief res = (a * b) % L, using Barrett reduction
 *
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param tmp  a temporary buffer for computation
 */
void LT_C25519_Multiply_ModL(u256 res, const u256 a, const u256 b, struct C25519MulLLocal *tmp) {
    LT_BN_Multiply_Unsigned(tmp->r, a, LTSYSTEMCRYPTO_U32_PER_U256, b, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ub);
    LT_C25519_ModL(res, tmp->r, LTSYSTEMCRYPTO_U32_PER_U256 * 2, &tmp->lb);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Feb-22   gallienus   created
 */
