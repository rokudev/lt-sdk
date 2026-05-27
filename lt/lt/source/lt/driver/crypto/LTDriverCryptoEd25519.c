/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoEd25519.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/*******************************************************************************
 IETF RFC 8032, https://www.rfc-editor.org/rfc/rfc8032.html
 NIST FIPS 186-5, https://csrc.nist.gov/publications/detail/fips/186/5/final

Section 3.

a * x^2 + y^2 = 1 + d * x^2 * y^2 % p

Points on the curve form a group under addition, (x3, y3) = (x1, y1)
+ (x2, y2), with the formulas

            x1 * y2 + x2 * y1                y1 * y2 - a * x1 * x2
x3 = --------------------------,   y3 = ---------------------------
        1 + d * x1 * x2 * y1 * y2          1 - d * x1 * x2 * y1 * y2

Section 5.1

+-----------+-------------------------------------------------------+
| Parameter | Value                                                 |
+-----------+-------------------------------------------------------+
|     p     | p of edwards25519 in [RFC7748] (i.e., 2^255 - 19)     |
|     b     | 256                                                   |
|  encoding | 255-bit little-endian encoding of {0, 1, ..., p-1}    |
|  of GF(p) |                                                       |
|    H(x)   | SHA-512(dom2(phflag,context)||x) [RFC6234]            |
|     c     | base 2 logarithm of cofactor of edwards25519 in       |
|           | [RFC7748] (i.e., 3)                                   |
|     n     | 254                                                   |
|     d     | d of edwards25519 in [RFC7748] (i.e., -121665/121666  |
|           | = 370957059346694393431380835087545651895421138798432 |
|           | 19016388785533085940283555)                           |
|     a     | -1                                                    |
|     B     | (X(P),Y(P)) of edwards25519 in [RFC7748] (i.e., (1511 |
|           | 22213495354007725011514095885315114540126930418572060 |
|           | 46113283949847762202, 4631683569492647816942839400347 |
|           | 5163141307993866256225615783033603165251855960))      |
|     L     | order of edwards25519 in [RFC7748] (i.e.,             |
|           | 2^252+27742317777372353535851937790883648493).        |
|   PH(x)   | x (i.e., the identity function)                       |
+-----------+-------------------------------------------------------+

Section 5.1.4

A point (x,y) is represented in extended homogeneous coordinates
(X, Y, Z, T), with x = X/Z, y = Y/Z, x * y = T/Z.
The following formulas for adding two points, (x3,y3) =
(x1,y1)+(x2,y2), on twisted Edwards curves with a=-1, square a, and
non-square d are described in Section 3.1 of [Edwards-revisited] and
in [EFD-TWISTED-ADD].  They are complete, i.e., they work for any
pair of valid input points.

    A = (Y1-X1)*(Y2-X2)
    B = (Y1+X1)*(Y2+X2)
    C = T1*2*d*T2
    D = Z1*2*Z2
    E = B-A
    F = D-C
    G = D+C
    H = B+A
    X3 = E*F
    Y3 = G*H
    T3 = E*H
    Z3 = F*G

For point doubling, (x3,y3) = (x1,y1)+(x1,y1), one could just
substitute equal points in the above (because of completeness, such
substitution is valid) and observe that four multiplications turn
into squares.  However, using the formulas described in Section 3.2
of [Edwards-revisited] and in [EFD-TWISTED-DBL] saves a few smaller
operations.

    A = X1^2
    B = Y1^2
    C = 2*Z1^2
    H = A+B
    E = H-(X1+Y1)^2
    G = A-B
    F = C+G
    X3 = E*F
    Y3 = G*H
    T3 = E*H
    Z3 = F*G

 Curve 25519 constants
 const u32 kC25519_N[LTSYSTEMCRYPTO_U32_PER_U256];    // Curve 25519 modulus
 const u32 kC25519_L[LTSYSTEMCRYPTO_U32_PER_U256];    // Curve 25519 group order
 const u32 kC25519_N1;                                // -1/N % R, then take the lowest u32
 const u32 kC25519_R2[LTSYSTEMCRYPTO_U32_PER_U256];   // R^2 % N
 const u32 kC25519_N2[LTSYSTEMCRYPTO_U32_PER_U256];   // N - 2
 const u32 kC25519_L1;                                // -1/L % R, then take the lowest u32
 const u32 kC25519_Q2[LTSYSTEMCRYPTO_U32_PER_U256];   // R^2 % L

 Ed25519 constants
 const u32 kEd25519_d[LTSYSTEMCRYPTO_U32_PER_U256];
 const u32 kEd25519_d2[LTSYSTEMCRYPTO_U32_PER_U256];  // d * 2
 const u32 kEd25519_SQ[LTSYSTEMCRYPTO_U32_PER_U256];  // SQ = 2^((N-1)/4) % N
 const EdPoint kEd25519_BP;                           // Ed25519 base point
*****************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTDriverCrypto.h"
#include "LTDriverCrypto25519.h"
#include "LTDriverCryptoBigNum.h"

/* Ed25519_BP, base point */
static const EdPoint kEd25519_BP = {{0x8F25D51A,0xC9562D60,0x9525A7B2,0x692CC760,0xFDD6DC5C,0xC0A4E231,0xCD6E53FE,0x216936D3},
                                    {0x66666658,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666}};
/* Ed25519_d = 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3 */
static const u32 kEd25519_d[LTSYSTEMCRYPTO_U32_PER_U256] = {0x135978A3,0x75EB4DCA,0x4141D8AB,0x00700A4D,0x7779E898,0x8CC74079,0x2B6FFE73,0x52036CEE};
/* Ed25519_d2 = d * 2 = 0xa406d9dc56dffce7198e80f2eef3d13000e0149a8283b156ebd69b9426b2f146 */
static const u32 kEd25519_d2[LTSYSTEMCRYPTO_U32_PER_U256] = {0x26B2F146,0xEBD69B94,0x8283B156,0x00E0149A,0xEEF3D130,0x198E80F2,0x56DFFCE7,0xA406D9DC};
/* Ed25519_SQ = 2^((N-1)/4) % N = 0x2b8324804fc1df0b2b4d00993dfbd7a72f431806ad2fe478c4ee1b274a0ea0b0 */
static const u32 kEd25519_SQ[LTSYSTEMCRYPTO_U32_PER_U256] = {0x4A0EA0B0,0xC4EE1B27,0xAD2FE478,0x2F431806,0x3DFBD7A7,0x2B4D0099,0x4FC1DF0B,0x2B832480};

typedef struct LT_EdDSA_CTX {
    union {
        struct {
            u32 s[EdDSA_KEY_LENGTH/4];         // 32 Bytes
            u8 prefix[EdDSA_KEY_LENGTH];       // 32 Bytes
        };
        u8 secret[EdDSA_KEY_LENGTH*2];         // secret, 64 Bytes
    };
    u8 pubKey[EdDSA_KEY_LENGTH];               // public, 32 Bytes
    bool bInited;
} LT_EdDSA_CTX;

/**
 * @brief Add two points in extended homogeneous space, i.e. res = a + b
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param eb   a temporary buffer for computation
 */
static void LT_Ext_Add(EdPointExt *res, const EdPointExt *a, const EdPointExt *b, struct C25519ExtLocal *eb) {
    // A = (Y1-X1)*(Y2-X2)
    LT_BN_Subtract_Mod_Unsigned(eb->H, a->Y, a->X, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(eb->A, b->Y, b->X, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->A, eb->H, eb->A, &eb->nb);
    // B = (Y1+X1)*(Y2+X2)
    LT_BN_Add_Mod_Unsigned(eb->H, a->Y, a->X, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(eb->B, b->Y, b->X, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->B, eb->H, eb->B, &eb->nb);
    // E = B-A
    LT_BN_Subtract_Mod_Unsigned(eb->E, eb->B, eb->A, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // H = B+A
    LT_BN_Add_Mod_Unsigned(eb->H, eb->B, eb->A, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // C = T1*2*d*T2   (A = T1*2*d*T2)
    LT_C25519_Multiply_ModN(eb->A, a->T, kEd25519_d2, &eb->nb);
    LT_C25519_Multiply_ModN(eb->A, eb->A, b->T, &eb->nb);
    // D = Z1*2*Z2     (B = Z1*2*Z2)
    LT_BN_Add_Mod_Unsigned(eb->B, a->Z, a->Z, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->B, eb->B, b->Z, &eb->nb);
    // F = D-C         (F = B-A)
    LT_BN_Subtract_Mod_Unsigned(eb->F, eb->B, eb->A, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // G = D+C         (B = B+A)
    LT_BN_Add_Mod_Unsigned(eb->B, eb->B, eb->A, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    LT_C25519_Multiply_ModN(res->X, eb->E, eb->F, &eb->nb);
    // Y3 = G*H        (Y3 = B*H)
    LT_C25519_Multiply_ModN(res->Y, eb->B, eb->H, &eb->nb);
    // T3 = E*H
    LT_C25519_Multiply_ModN(res->T, eb->E, eb->H, &eb->nb);
    // Z3 = F*G        (Z3 = F*B)
    LT_C25519_Multiply_ModN(res->Z, eb->F, eb->B, &eb->nb);
}

/**
 * @brief Double a point in extended homogeneous space, i.e. res = 2*a
 * @param res  the result
 * @param a    the operand to double
 * @param eb   a temporary buffer for computation
 */
static void LT_Ext_Double(EdPointExt *res, const EdPointExt *a, struct C25519ExtLocal *eb) {
    // A = X1^2
    LT_C25519_Multiply_ModN(eb->A, a->X, a->X, &eb->nb);
    // B = Y1^2
    LT_C25519_Multiply_ModN(eb->B, a->Y, a->Y, &eb->nb);
    // H = A+B
    LT_BN_Add_Mod_Unsigned(eb->H, eb->A, eb->B, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // G = A-B            (B = A-B)
    LT_BN_Subtract_Mod_Unsigned(eb->B, eb->A, eb->B, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // C = 2*Z1^2         (A = 2*Z1^2)
    LT_C25519_Multiply_ModN(eb->A, a->Z, a->Z, &eb->nb);
    LT_BN_Add_Mod_Unsigned(eb->A, eb->A, eb->A, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // E = H-(X1+Y1)^2
    LT_BN_Add_Mod_Unsigned(eb->E, a->X, a->Y, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->E, eb->E, eb->E, &eb->nb);
    LT_BN_Subtract_Mod_Unsigned(eb->E, eb->H, eb->E, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // F = C+G            (F = A+B)
    LT_BN_Add_Mod_Unsigned(eb->F, eb->A, eb->B, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    LT_C25519_Multiply_ModN(res->X, eb->E, eb->F, &eb->nb);
    // Y3 = G*H
    LT_C25519_Multiply_ModN(res->Y, eb->B, eb->H, &eb->nb);
    // T3 = E*H
    LT_C25519_Multiply_ModN(res->T, eb->E, eb->H, &eb->nb);
    // Z3 = F*G
    LT_C25519_Multiply_ModN(res->Z, eb->F, eb->B, &eb->nb);
}

/**
 * @brief Convert an Edward point to an extended homogeneous point
 * @param res  the result
 * @param a    the operand to convert
 * @param nb   a temporary buffer for computation
 */
static void LT_Ed_To_Ext(EdPointExt *res, const EdPoint *a, struct C25519MulNLocal *nb) {
    lt_memcpy(res->X, a->x, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memcpy(res->Y, a->y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memset(res->Z, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    res->Z[0] = 1;
    LT_C25519_Multiply_ModN(res->T, a->x, a->y, nb);
}

/**
 * @brief Convert an extended homogeneous point to an Edward point
 * @param res  the result
 * @param a    the operant to convert
 * @param eb   a temporary buffer for computation
 */
static void LT_Ext_To_Ed(EdPoint *res, const EdPointExt *a, struct C25519ExEdLocal *eb) {
    LT_C25519_Inverse_ModN(eb->z1, a->Z, &eb->ib);
    // x = X/Z
    LT_C25519_Multiply_ModN(res->x, a->X, eb->z1, &eb->nb);
    // y = Y/Z
    LT_C25519_Multiply_ModN(res->y, a->Y, eb->z1, &eb->nb);
}

/**
 * @brief res = k*a, extended homogeneous point multiplication using Montgomery ladder
 *        Use in signature generation, constant time
 * @param res  the resulting product
 * @param k    the scalar to multiply
 * @param a    the extended point to multiply
 * @param mb   a temporary buffer for computation
 * @note  use this multiplication only if k < L, L = 2^252+xxx
 */
static void LT_Ext_Multiply_Const(EdPointExt *res, const u256 k, const EdPointExt *a, struct C25519ExMulLocal *mb) {
    lt_memcpy(&mb->r1, a, sizeof(EdPointExt));

    lt_memset(res, 0, sizeof(EdPointExt));
    res->Y[0] = 1;
    res->Z[0] = 1;

    // Montgomery ladder, multiplication, constant time
    for (int m = 252; m >= 0; --m) {
        if (0 == LT_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            LT_Ext_Add(&mb->r1, res, &mb->r1, &mb->eb);
            LT_Ext_Double(res, res, &mb->eb);
        } else {
            LT_Ext_Add(res, res, &mb->r1, &mb->eb);
            LT_Ext_Double(&mb->r1, &mb->r1, &mb->eb);
        }
    }
}

/**
 * @brief res = k*a, extended homogeneous point multiplication
 *        Use only in public key generation
 * @param res  the resulting product
 * @param k    the scalar to multiply
 * @param a    the extended point to multiply
 * @param mb   a temporary buffer for computation
 * @note  use this multiplication only if k < L, L = 2^252+xxx
 */
static void LT_Ext_Multiply_Normal(EdPointExt *res, const u256 k, const EdPointExt *a, struct C25519ExMulLocal *mb) {
    lt_memcpy(&mb->r1, a, sizeof(EdPointExt));

    lt_memset(res, 0, sizeof(EdPointExt));
    res->Y[0] = 1;
    res->Z[0] = 1;

    for (int m = 252; m >= 0; --m) {
        LT_Ext_Double(res, res, &mb->eb);
        if (1 == LT_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            LT_Ext_Add(res, res, &mb->r1, &mb->eb);
        }
    }
}

/**
 * @brief res = u*a + v*b, extended homogeneous point multiplication and addition
 *        Use only in signature verification
 * @param res  the resulting product
 * @param k    the scalar to multiply
 * @param a    the extended point to multiply
 * @param mb   a temporary buffer for computation
 * @note  use this multiplication only if k < L, L = 2^252+xxx
 */
static void LT_Ext_Multiply_Add(EdPointExt *res, const u256 u, const EdPointExt *a, const u256 v, const EdPointExt *b, struct C25519ExMulLocal *mb) {
    lt_memset(&mb->r0, 0, sizeof(EdPointExt));
    mb->r0.Y[0] = 1;
    mb->r0.Z[0] = 1;
    LT_Ext_Add(&mb->r1, a, b, &mb->eb);

    int ub, vb;
    for (int m = 252; m >= 0; --m) {
        LT_Ext_Double(&mb->r0, &mb->r0, &mb->eb);
        ub = LT_BN_Get_Bit(u, m, LTSYSTEMCRYPTO_U32_PER_U256);
        vb = LT_BN_Get_Bit(v, m, LTSYSTEMCRYPTO_U32_PER_U256);
        if (ub == 0 && vb == 1) {
            LT_Ext_Add(&mb->r0, &mb->r0, b, &mb->eb);
        } else if (ub == 1 && vb == 0) {
            LT_Ext_Add(&mb->r0, &mb->r0, a, &mb->eb);
        } else if (ub == 1 && vb == 1) {
            LT_Ext_Add(&mb->r0, &mb->r0, &mb->r1, &mb->eb);
        }
    }
    lt_memcpy(res, &mb->r0, sizeof(EdPointExt));
}

/**
 * @brief Point multiplication on the Ed curve
 *        Use in signature generation, constant time
 * @param res  the resulting point
 * @param k    the scalar to multiply
 * @param a    the point to multiply
 * @param mb   a temporary buffer for computation
 * @note  use this multiplication only if k < L, L = 2^252+xxx
 */
static void LT_Ed_Multiply_Const(EdPoint *res, const u256 k, const EdPoint *a, struct C25519EdMulLocal *mb) {
    // map the base point to an extended homogeneous point
    LT_Ed_To_Ext(&mb->P, a, &mb->nb);

    // multiply in extended homogeneous space
    LT_Ext_Multiply_Const(&mb->P, k, &mb->P, &mb->mb);

    // map back to Edward point
    LT_Ext_To_Ed(res, &mb->P, &mb->eb);
}

/**
 * @brief Point multiplication on the Ed curve
 *        Use only in public key generation
 * @param res  the resulting point
 * @param k    the scalar to multiply
 * @param a    the point to multiply
 * @param mb   a temporary buffer for computation
 * @note  use this multiplication only if k < L, L = 2^252+xxx
 */
static void LT_Ed_Multiply_Normal(EdPoint *res, const u256 k, const EdPoint *a, struct C25519EdMulLocal *mb) {
    // map the base point to an extended homogeneous point
    LT_Ed_To_Ext(&mb->P, a, &mb->nb);

    // multiply in extended homogeneous space
    LT_Ext_Multiply_Normal(&mb->P, k, &mb->P, &mb->mb);

    // map back to Edward point
    LT_Ext_To_Ed(res, &mb->P, &mb->eb);
}

/**
 * @brief  Square root to get x, using u and v. A helper function to LT_Ed_Decode
 *         x = u * v^3 * (u * v^7) ^ (N-5)/8
 * @param res the resulting x coordinate
 * @param u   the u operand
 * @param v   the v operand
 * @return result code
 */
static LTSystemCryptoResult LT_Sqrt_ModN(u256 res, u256 u, u256 v) {
    struct SqrtLocal {
        u256 r0;
        u256 r1;
        u256 v3;
        struct C25519MulNLocal nb;
    };
    struct SqrtLocal *tmp = lt_malloc(sizeof(struct SqrtLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    LT_C25519_Multiply_ModN(tmp->r1, v, v, &tmp->nb);   // r1 = v^2
    LT_C25519_Multiply_ModN(tmp->v3, tmp->r1, v, &tmp->nb);  // v3 = v^3
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->r1, &tmp->nb); // r1 = v^4
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->v3, &tmp->nb); // r1 = v^7

    // (N-5)/8 = 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd
    // 250 '1', then '0', then '1'
    // r0 = (u * v^7) ^ (N-5)/8 % N
    // bit[251] = 1
    LT_C25519_Multiply_ModN(tmp->r0, u, tmp->r1, &tmp->nb);   // r0 = u * v^7
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r0, tmp->r0, &tmp->nb);
    // bit[250...2] = 1
    for (u32 m = 250; m >= 2; --m) {
        LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r1, &tmp->nb);
        LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, tmp->r1, &tmp->nb);
    }
    // bit[1] = 0
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r0, tmp->r1, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r0, &tmp->nb);
    // bit[0] = 1
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, tmp->r1, &tmp->nb);

    // x = r0 * u * v^3 % N
    LT_C25519_Multiply_ModN(tmp->r0, tmp->r0, u, &tmp->nb);
    LT_C25519_Multiply_ModN(res, tmp->r0, tmp->v3, &tmp->nb);

    // r1 = v * x^2 % N
    LT_C25519_Multiply_ModN(tmp->r1, res, res, &tmp->nb);
    LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, v, &tmp->nb);

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Ok;
    if (LT_BN_Compare_Unsigned(tmp->r1, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
        // x = x * 2^((N-1)/4) % N
        LT_C25519_Multiply_ModN(res, res, kEd25519_SQ, &tmp->nb);
        // r1 = v * x^2 % N
        LT_C25519_Multiply_ModN(tmp->r1, res, res, &tmp->nb);
        LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, v, &tmp->nb);
        if (LT_BN_Compare_Unsigned(tmp->r1, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
        }
    }

    lt_memset(tmp, 0, sizeof(struct SqrtLocal));
    lt_free(tmp);
    return ret;
}

// 5.1.3
/**
 * @brief Decode the y coordinate to the full Ed point on the curve
 * @param  res  the resulting point
 * @param  y    the y coordinate
 * @return result code
 * @note   y includes the true y and the lsb of x
 */
static LTSystemCryptoResult LT_Ed_Decode(EdPoint *res, const u256 y) {
    struct DecodeLocal {
        u256 one;
        u256 u;
        u256 v;
        struct C25519MulNLocal nb;
    };
    struct DecodeLocal *tmp = lt_malloc(sizeof(struct DecodeLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    lt_memcpy(res->y, y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // step 1
    u32 xlsb = res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] >> 31;
    res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] &= 0x7FFFFFFF;
    if (LT_BN_Compare_Unsigned(res->y, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
        return kLTSystemCrypto_Result_WrongVerification;
    }

    // step 2
    lt_memset(tmp->one, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->one[0] = 1;
    // u = y^2 - 1
    LT_C25519_Multiply_ModN(tmp->u, res->y, res->y, &tmp->nb);
    lt_memcpy(tmp->v, tmp->u, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(tmp->u, tmp->u, tmp->one, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // v = d*y^2 + 1
    LT_C25519_Multiply_ModN(tmp->v, tmp->v, kEd25519_d, &tmp->nb);
    LT_BN_Add_Mod_Unsigned(tmp->v, tmp->v, tmp->one, LT_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // step 3, x = u * v^3 * (u * v^7) ^ (N-5)/8 % N
    LTSystemCryptoResult ret = LT_Sqrt_ModN(res->x, tmp->u, tmp->v);
    if (kLTSystemCrypto_Result_Ok == ret) {
        // step 4
        if ((res->x[0] & 1) != xlsb) {
            LT_BN_Subtract_Unsigned(res->x, LT_GetC25519_N(), res->x, LTSYSTEMCRYPTO_U32_PER_U256);
        }
    }

    lt_memset(tmp, 0, sizeof(struct DecodeLocal));
    lt_free(tmp);
    return ret;
}

// 5.1.5
/**
 * @brief  Initialize the EdDSA context
 * @param  ctx      the context
 * @param  privKey  the private key, 32 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_Eddsa_Init(LT_EdDSA_CTX *ctx, const u8 privKey[EdDSA_KEY_LENGTH]) {
    if (!ctx || !privKey) {
        return kLTSystemCrypto_Result_Null;
    }

    struct InitLocal {
        u256 k;
        EdPoint P;
        union {
            struct C25519ModLLocal modb;
            struct C25519EdMulLocal edmb;
        };
    };
    struct InitLocal *tmp = lt_malloc(sizeof(struct InitLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    lt_memset(ctx, 0, sizeof(LT_EdDSA_CTX));

    // step 1
    LT_SHA512_Digest(privKey, LTSYSTEMCRYPTO_BYTES_PER_U256, ctx->secret);
    // step 2
    ctx->secret[0]  &= 0xF8;   // clear 3 lsbs
    ctx->secret[31] &= 0x7F;   // clear 1 msb
    ctx->secret[31] |= 0x40;   // set the 2nd msb
    // step 3
    LT_C25519_ModL(tmp->k, ctx->s, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->modb);
    LT_Ed_Multiply_Normal(&tmp->P, tmp->k, &kEd25519_BP, &tmp->edmb);
    lt_memset(tmp->k, 0, sizeof(tmp->k));
    // step 4, public key A[32], i.e. pubKey[32]
    lt_memcpy(ctx->pubKey, tmp->P.y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    ctx->pubKey[31] |= ((tmp->P.x[0] & 1) << 7);
    ctx->bInited = true;

    lt_memset(tmp, 0, sizeof(struct InitLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Generate public key from private key
 * @param  privateKey  the private key, 32 Bytes
 * @param  pubKey      the output public key, 32 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_Eddsa_GenPublicKey(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    LT_EdDSA_CTX *ctx = lt_malloc(sizeof(LT_EdDSA_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;
    LTSystemCryptoResult ret = LT_Eddsa_Init(ctx, privateKey);
    if (kLTSystemCrypto_Result_Ok == ret) lt_memcpy(publicKey, ctx->pubKey, EdDSA_KEY_LENGTH);
    lt_memset(ctx, 0, sizeof(LT_EdDSA_CTX));
    lt_free(ctx);
    return ret;
}

// 5.1.6
/**
 * @brief  Sign a block of data to produce a signature
 * @param  ctx        the context
 * @param  data       the data
 * @param  dataLen    the length of data
 * @param  signature  the output signature
 * @param  pubKey     the output public key, used later for verifying signature
 * @return result code
 */
LTSystemCryptoResult LT_Eddsa_Sign(const LT_EdDSA_CTX *ctx, const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 pubKey[EdDSA_KEY_LENGTH]) {
    // sanity check
    if (!ctx || !ctx->bInited || !pubKey || !data || !signature) {
        return kLTSystemCrypto_Result_Null;
    }

    struct SignLocal {
        u256 r;
        u256 k;
        u8 h[SHA512_HASH_LENGTH];
        LT_SHA512_CTX shaCtx;
        EdPoint P;
        union {
            struct C25519ModLLocal modb;
            struct C25519MulLLocal mmb;
            struct C25519EdMulLocal edmb;
        };
    };
    struct SignLocal *tmp = lt_malloc(sizeof(struct SignLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    // step 1, 5.1.5, init to generate keys in context
    lt_memcpy(pubKey, ctx->pubKey, EdDSA_KEY_LENGTH);

    // step 2
    LT_SHA512_Init(&tmp->shaCtx);
    LT_SHA512_Update(&tmp->shaCtx, ctx->prefix, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_SHA512_Update(&tmp->shaCtx, data, dataLen);
    LT_SHA512_Finish(&tmp->shaCtx, tmp->h);

    // step 3, string R[32]
    LT_C25519_ModL(tmp->r, (u32 *)tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, &tmp->modb);
    LT_Ed_Multiply_Const(&tmp->P, tmp->r, &kEd25519_BP, &tmp->edmb);
    lt_memcpy(tmp->h, tmp->P.y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->h[31] |= ((tmp->P.x[0] & 1) << 7);
    lt_memcpy(signature, tmp->h, LTSYSTEMCRYPTO_BYTES_PER_U256);   // step 6, R is h, the first half of signature

    // step 4
    LT_SHA512_Init(&tmp->shaCtx);
    LT_SHA512_Update(&tmp->shaCtx, tmp->h, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_SHA512_Update(&tmp->shaCtx, ctx->pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_SHA512_Update(&tmp->shaCtx, data, dataLen);
    LT_SHA512_Finish(&tmp->shaCtx, tmp->h);
    LT_C25519_ModL(tmp->k, (u32*)tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, &tmp->modb);

    // step 5
    LT_C25519_Multiply_ModL(tmp->k, tmp->k, ctx->s, &tmp->mmb);
    LT_BN_Add_Mod_Unsigned(tmp->r, tmp->r, tmp->k, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256);

    // step 6
    lt_memcpy(signature + 32, tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256);  // the second half of signature

    lt_memset(tmp, 0, sizeof(struct SignLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

// 5.1.7
/**
 * @brief  Verify a signature
 * @param  data       the data of the signature
 * @param  dataLen    the length of data
 * @param  signature  the signature to verify
 * @param  pubKey     the public key for verifying signature
 * @return result code
 */
LTSystemCryptoResult LT_Eddsa_Verify(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 pubKey[EdDSA_KEY_LENGTH]) {
    // sanity check
    if (!pubKey || !signature || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    struct VerifyLocal {
        u32 buf[LTSYSTEMCRYPTO_U32_PER_U256];
        u32 h[SHA512_HASH_LENGTH / 4];
        u256 s;
        union {
            LT_SHA512_CTX ctx;
            struct {
                EdPointExt left;  // left and right extended points
                EdPointExt right;
                EdPoint A;
                // shared temporary buffer for sub function calls
                union {
                    struct C25519ModLLocal modb;
                    struct C25519MulNLocal nb;
                    struct C25519ExMulLocal emb;
                    struct C25519ExtLocal eb;
                };
            };
        };
    };
    struct VerifyLocal *tmp = lt_malloc(sizeof(struct VerifyLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        lt_memcpy(tmp->buf, signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (LT_BN_Compare_Unsigned(tmp->buf, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256) >=0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }

        // 5.1.7
        // step 2
        LT_SHA512_Init(&tmp->ctx);
        LT_SHA512_Update(&tmp->ctx, signature, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_SHA512_Update(&tmp->ctx, pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_SHA512_Update(&tmp->ctx, data, dataLen);
        LT_SHA512_Finish(&tmp->ctx, (u8 *)tmp->h);
        u32 *k = tmp->h;         // secrete scalar k[32]
        LT_C25519_ModL(k, tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, &tmp->modb);

        // step 1
        // step 3, [s]B = R + [k]A, so [s]B - [k]A = R
        // check in extended space to avoid Ext_To_Ed computation
        // left is B
        LT_Ed_To_Ext(&tmp->left, &kEd25519_BP, &tmp->nb);
        // right is A
        lt_memcpy(tmp->buf, pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (kLTSystemCrypto_Result_Ok != (ret = LT_Ed_Decode(&tmp->A, tmp->buf))) break;
        LT_Ed_To_Ext(&tmp->right, &tmp->A, &tmp->nb);
        // s
        lt_memcpy(tmp->s, signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        // -k
        lt_memset(tmp->buf, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_BN_Subtract_Mod_Unsigned(tmp->buf, tmp->buf, k, LT_GetC25519_L(), LTSYSTEMCRYPTO_U32_PER_U256);
        // [s]B - [k]A
        LT_Ext_Multiply_Add(&tmp->left, tmp->s, &tmp->left, tmp->buf, &tmp->right, &tmp->emb);

        lt_memcpy(tmp->buf, signature, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (kLTSystemCrypto_Result_Ok != (ret = LT_Ed_Decode(&tmp->A, tmp->buf))) break;
        LT_Ed_To_Ext(&tmp->right, &tmp->A, &tmp->nb);

        u32 *r = tmp->h + LTSYSTEMCRYPTO_U32_PER_U256;   // l is k
        // lx = lX/lZ == rX/rZ = rx. Hence, lX*rZ == lZ*rX % N
        LT_C25519_Multiply_ModN(k, tmp->left.X, tmp->right.Z, &tmp->nb);
        LT_C25519_Multiply_ModN(r, tmp->left.Z, tmp->right.X, &tmp->nb);
        if (LT_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }
        // ly = lY/lZ == rY/rZ = ry. Hence, lY*rZ == lZ*rY % N
        LT_C25519_Multiply_ModN(k, tmp->left.Y, tmp->right.Z, &tmp->nb);
        LT_C25519_Multiply_ModN(r, tmp->left.Z, tmp->right.Y, &tmp->nb);
        if (LT_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }
        ret = kLTSystemCrypto_Result_Ok;
    } while (0);

    lt_memset(tmp, 0, sizeof(struct VerifyLocal));
    lt_free(tmp);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoEd25519, LTSoftwareCryptoEd25519) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoEd25519_GenPublicKey(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    return LT_Eddsa_GenPublicKey(privateKey, publicKey);
}

static LTSystemCryptoResult LTSoftwareCryptoEd25519_Sign(const u8 priKey[EdDSA_KEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 pubKey[EdDSA_KEY_LENGTH]) {
    LT_EdDSA_CTX *eddsaCtx = lt_malloc(sizeof(LT_EdDSA_CTX));
    if (!eddsaCtx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        eddsaCtx->bInited = false;
        if (kLTSystemCrypto_Result_Ok != (ret = LT_Eddsa_Init(eddsaCtx, priKey))) {
            break;
        }
        ret = LT_Eddsa_Sign(eddsaCtx, data, dataLen, signature, pubKey);
    } while (false);

    lt_memset(eddsaCtx, 0, sizeof(LT_EdDSA_CTX));
    lt_free(eddsaCtx);
    return ret;
}

static LTSystemCryptoResult LTSoftwareCryptoEd25519_Verify(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 pubKey[EdDSA_KEY_LENGTH]) {
    return LT_Eddsa_Verify(data, dataLen, signature, pubKey);
}

static void LTSoftwareCryptoEd25519_DestructObject(LTSoftwareCryptoEd25519 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoEd25519_ConstructObject(LTSoftwareCryptoEd25519 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEd25519, LTSoftwareCryptoEd25519,
    GenPublicKey,
    Sign,
    Verify,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Feb-22   gallienus   created
 */
