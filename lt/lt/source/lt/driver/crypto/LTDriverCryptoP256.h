/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoP256.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOP256_H
#define LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOP256_H

#include <lt/LT.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTDriverCrypto.h"
#include "LTDriverCryptoBigNum.h"

LT_EXTERN_C_BEGIN

// Jacobian projective coordinates of a P256 point: x = X/Z^2, y = Y/Z^3
typedef struct P256PointExt {
    u256 X;
    u256 Y;
    u256 Z;
    bool inf;  // true; if the point is infinity.
} P256PointExt;

// data struct for temporary data, so we don't typedef it.
struct P256ModLocal {
    u32 r[LTSYSTEMCRYPTO_U32_PER_U256 + 1];
    u32 t[LTSYSTEMCRYPTO_U32_PER_U256 + 1];
};

struct P256MulPLocal {
    u32 rab[LTSYSTEMCRYPTO_U32_PER_U256 * 2];
    union {
        struct P256ModLocal mdl;
        struct MulULocal ml;
    };
};

struct P256DoubleLocal {
    u256 Ysq;
    u256 A;
    u256 B;
    u256 C;
    u256 D;
    struct P256MulPLocal ml;
};

struct P256AddLocal {
    union {
        struct {
            u256 Zsq;
            u256 A;
            u256 B;
            u256 C;
            u256 D;
            u256 E;
            u256 F;
            struct P256MulPLocal ml;
        };
        struct P256DoubleLocal dl;
    };
};

struct P256InvLocal {
    u256 r0;
    u256 r1;
    union {
        struct MulULocal ml;
        struct MtMulLocal mml;
    };
};

struct P256E2PLocal {
    u256 Zinv;
    union {
        struct P256InvLocal il;
        struct P256MulPLocal ml;
    };
};

struct P256ScaLocal {
    P256PointExt r0;
    P256PointExt r1;
    union {
        struct P256AddLocal al;
        struct P256DoubleLocal dl;
        struct P256E2PLocal el;
    };
};

struct P256ValPLocal {
    u256 l;
    u256 r;
    struct P256MulPLocal ml;
};

struct P256ValFLocal {
    P256Point v;
    union {
        struct P256ValPLocal pl;
        struct P256ScaLocal sl;
    };
};

struct RndLocal {
    u32 H[SHA256_HASH_LENGTH / 4];
    u32 K[SHA256_HASH_LENGTH / 4];
    u32 V[SHA256_HASH_LENGTH / 4];
    LT_HMAC_SHA256_CTX hmacHashCtx;
    LT_HMAC_CTX_Impl hmacCtx;
};

struct PubLocal {
    P256Point u;
    u256 d;
    struct P256ScaLocal sl;
};

/**
 * @brief res = a + b
 *
 * @param res  the result
 * @param a    the first curve point
 * @param b    the second curve point
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Add(P256Point *res, const P256Point *a, const P256Point *b, struct P256ScaLocal *tmp);

/**
 * @brief res = k*Q, constant time, Montgomery ladder
 *        Sign must use this scalar multiplication for security.
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Const(P256Point *res, const u256 k, const P256Point *q, struct P256ScaLocal *tmp);

/**
 * @brief res = k*Q, double and add
 *        Faster than constant-time, but only use in public key generation and signature verification
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Normal(P256Point *res, const u256 k, const P256Point *q, struct P256ScaLocal *tmp);

/**
 * @brief res = u * G + v * Q, Shamir's look up
 *        Only use in signature verification
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Add(P256Point *res, const u256 u, const P256Point *g, const u256 v, const P256Point *q, struct P256ScaLocal *tmp);

/**
 * @brief Check if a is on curve, partial public key validation
 *        NIST SP 800-186, D.1.1.1
 *
 * @param a   the curve point to check
 * @param tmp a temporary buffer for computation
 * @return true
 * @return false
 */
bool LT_P256_Validate_Partial(const P256Point *a, struct P256ValPLocal *tmp);

/**
 * @brief Check if a is on curve, full public key validation
 *        NIST SP 800-186, D.1.1.2
 *
 * @param a   the curve point to check
 * @param tmp a temporary buffer for computation
 * @return true
 * @return false
 */
bool LT_P256_Validate_Full(const P256Point *a, struct P256ValFLocal *tmp);

/**
 * @brief Inversion mod N using Montgomery reduction
 *        res = a^-1 = a^(N-2) % N, because Fermat's theorem 1 = a^(N-1) % N
 *        Only works for the constant N
 *        N-2 = 0xffffffff,00000000,ffffffff,ffffffff,bce6faad,a7179e84,f3b9cac2,fc63254F
 * @param res  the result
 * @param a    the operand to inverse
 * @param tmp  a temporary buffer for computation
 * @note  both a and res are normal u256
 */
void LT_P256_Inverse_ModN_Mont(u256 res, const u256 a, struct P256InvLocal *tmp);

const u32* LT_GetP256_N(void);
u32 LT_GetP256_N1(void);
const u32* LT_GetP256_RN2(void);

LT_EXTERN_C_END
#endif   // LT_SOURCE_LT_DRIVER_CRYPTO_LTDRIVERCRYPTOP256_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  04-Apr-22   gallienus   created
 */
