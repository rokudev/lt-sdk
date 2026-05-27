/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCrypto25519.h
 *
 * Internal functions for EdDSA and ECDHE only
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO25519_H
#define LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO25519_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCryptoDefs.h>
#include "LTDriverCryptoBigNum.h"
#include "LTDriverCrypto.h"

LT_EXTERN_C_BEGIN

// Extended homogeneous coordinates
typedef struct EdPointExt {
    u256 X;  // x = X/Z
    u256 Y;  // y = Y/Z
    u256 Z;
    u256 T;  // x*y = T/Z
} EdPointExt;

// data struct for temporary data, so we don't typedef it.
struct C25519MulNLocal {
    u32 r[LTSYSTEMCRYPTO_U32_PER_U256 * 2];
    struct MulULocal ub;
};

struct C25519ModLLocal {
    u32 r[LTSYSTEMCRYPTO_U32_PER_U256 * 4];
    struct MulULocal ub;
};

struct C25519MulLLocal {
    u32 r[LTSYSTEMCRYPTO_U32_PER_U256 * 2];
    union {
        struct MulULocal ub;
        struct C25519ModLLocal lb;
    };
};

struct C25519InvLocal {
    u256 r0;
    u256 r1;
    struct C25519MulNLocal nb;
};

struct C25519ExEdLocal {
    u256 z1;
    union {
        struct C25519InvLocal ib;
        struct C25519MulNLocal nb;
    };
};

struct C25519ExtLocal {
    u256 A;
    u256 B;
    u256 E;
    u256 F;
    u256 H;
    struct C25519MulNLocal nb;
};

struct C25519ExMulLocal {
    EdPointExt r0;
    EdPointExt r1;
    struct C25519ExtLocal eb;
};

struct C25519EdMulLocal {
    EdPointExt P;
    union {
        struct C25519MulNLocal nb;
        struct C25519ExMulLocal mb;
        struct C25519ExEdLocal eb;
    };
};

struct C25519XMulLocal {
    u256 x1;
    u256 x2;
    u256 z2;
    u256 x3;
    u256 z3;
    u256 A;
    u256 B;
    u256 C;
    u256 D;
    u32 *X2;
    u32 *Z2;
    u32 *X3;
    u32 *Z3;
    u32 *Tmp2;
    u32 *Tmp3;
    union {
        struct C25519InvLocal ib;
        struct C25519MulNLocal nb;
    };
};

/**
 * @brief res = (a * b) % N. N is (2^255 - 19)
 *
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param tmp  a temporary buffer for computation
 */
void LT_C25519_Multiply_ModN(u256 res, const u256 a, const u256 b, struct C25519MulNLocal *tmp);

/**
 * @brief Inversion mod N
 *        res = a^-1 = a^(N-2) % N, because Fermat's theorem 1 = a^(N-1) % N
 *        Only works for the constant N
 * @param res  the result
 * @param a    the operand to inverse
 * @param tmp  a temporary buffer for computation
 * @note  both a and res are normal u256
 */
void LT_C25519_Inverse_ModN(u256 res, const u256 a, struct C25519InvLocal *tmp);

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
void LT_C25519_ModL(u256 res, const u32 *a, LT_SIZE len, struct C25519ModLLocal *tmp);

/**
 * @brief res = (a * b) % L, using Barrett reduction
 *
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param tmp  a temporary buffer for computation
 */
void LT_C25519_Multiply_ModL(u256 res, const u256 a, const u256 b, struct C25519MulLLocal *tmp);

const u32 *LT_GetC25519_N(void);
const u32 *LT_GetC25519_L(void);

LT_EXTERN_C_END
#endif   // LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTO25519_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Feb-22   gallienus   created
 */
