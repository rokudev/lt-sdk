/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoEd25519.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

/*******************************************************************************
 IETF RFC 8032, https://www.rfc-editor.org/rfc/rfc8032.html
 NIST FIPS 186-5 (Draft), https://csrc.nist.gov/publications/detail/fips/186/5/draft

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
 *****************************************************************************/

#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "Esp32DriverCrypto.h"
#include "Esp32DriverCryptoBigNum.h"
#include "Esp32DriverCrypto25519.h"

// Crypto constants
/* C25519_L = 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed, curve group order */
static const u32 kC25519_L[LTSYSTEMCRYPTO_U32_PER_U256] = {0x5CF5D3ED,0x5812631A,0xA2F79CD6,0x14DEF9DE,0x00000000,0x00000000,0x00000000,0x10000000};
/* C25519_L1 = -1/L % R, then take the lowest u32 */
static const u32 kC25519_L1 = 0x12547E1B;
/* C25519_Q2 = R^2 % L = 0x9dc924e5a45ffd7e7faf80eb68700589d31cab2023493f73a3dc22242419a0d, must be wide as RSA512 register */
static const u32 kC25519_Q2[LTSYSTEMCRYPTO_U32_PER_U256] = {0x42419A0D,0x3A3DC222,0x023493F7,0x9D31CAB2,0xB6870058,0xE7FAF80E,0x5A45FFD7,0x09DC924E};
/* Ed25519_BP, base point */
static const EdPoint kEd25519_BP = {{0x8F25D51A,0xC9562D60,0x9525A7B2,0x692CC760,0xFDD6DC5C,0xC0A4E231,0xCD6E53FE,0x216936D3},
                                    {0x66666658,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666}};
/* Ed25519_d = 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3 */
static const u32 kEd25519_d[LTSYSTEMCRYPTO_U32_PER_U256] = {0x135978A3,0x75EB4DCA,0x4141D8AB,0x00700A4D,0x7779E898,0x8CC74079,0x2B6FFE73,0x52036CEE};
/* Ed25519_d2 = d * 2 = 0xa406d9dc56dffce7198e80f2eef3d13000e0149a8283b156ebd69b9426b2f146 */
static const u32 kEd25519_d2[LTSYSTEMCRYPTO_U32_PER_U256] = {0x26B2F146,0xEBD69B94,0x8283B156,0x00E0149A,0xEEF3D130,0x198E80F2,0x56DFFCE7,0xA406D9DC};
/* Ed25519_SQ = 2^((N-1)/4) % N = 0x2b8324804fc1df0b2b4d00993dfbd7a72f431806ad2fe478c4ee1b274a0ea0b0 */
static const u32 kEd25519_SQ[LTSYSTEMCRYPTO_U32_PER_U256] = {0x4A0EA0B0,0xC4EE1B27,0xAD2FE478,0x2F431806,0x3DFBD7A7,0x2B4D0099,0x4FC1DF0B,0x2B832480};
/* N58 = (N-5)/8 = 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd */
static const u32 kEd25519_N58[LTSYSTEMCRYPTO_U32_PER_U256] = {0xFFFFFFFD,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x0FFFFFFF};

// Context of EdDSA
typedef struct {
    union {
        struct {
            u32 s[EdDSA_KEY_LENGTH/4];         // 32 Bytes
            u8 prefix[EdDSA_KEY_LENGTH];       // 32 Bytes
        };
        u8 secret[EdDSA_KEY_LENGTH*2];         // secret, 64 Bytes
    };
    u8 pubKey[EdDSA_KEY_LENGTH];               // public, 32 Bytes
    bool bInited;
} ESP32_EdDSA_CTX;

/**
 * @brief Add two points in extended homogeneous space, i.e. res = a + b
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param eb   a temporary buffer for computation
 */
static void ESP32_Ext_Add(const ESP32_RSA_CTX *ctx, EdPointExt *res, const EdPointExt *a, const EdPointExt *b, struct Esp32C25519ExtLocal *eb) {
    // A = (Y1-X1)*(Y2-X2)
    ESP32_BN_Subtract_Mod_Unsigned(eb->H, a->Y, a->X, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Subtract_Mod_Unsigned(eb->A, b->Y, b->X, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->A, eb->H, eb->A);
    // B = (Y1+X1)*(Y2+X2)
    ESP32_BN_Add_Mod_Unsigned(eb->H, a->Y, a->X, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Add_Mod_Unsigned(eb->B, b->Y, b->X, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->B, eb->H, eb->B);
    // E = B-A
    ESP32_BN_Subtract_Mod_Unsigned(eb->E, eb->B, eb->A, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // H = B+A
    ESP32_BN_Add_Mod_Unsigned(eb->H, eb->B, eb->A, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // C = T1*2*d*T2   (A = T1*2*d*T2)
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->A, a->T, kEd25519_d2);
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->A, eb->A, b->T);
    // D = Z1*2*Z2     (B = Z1*2*Z2)
    ESP32_BN_Add_Mod_Unsigned(eb->B, a->Z, a->Z, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->B, eb->B, b->Z);
    // F = D-C         (F = B-A)
    ESP32_BN_Subtract_Mod_Unsigned(eb->F, eb->B, eb->A, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // G = D+C         (B = B+A)
    ESP32_BN_Add_Mod_Unsigned(eb->B, eb->B, eb->A, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    ESP32_BN_Multiply_Mod_Preset(ctx, res->X, eb->E, eb->F);
    // Y3 = G*H        (Y3 = B*H)
    ESP32_BN_Multiply_Mod_Preset(ctx, res->Y, eb->B, eb->H);
    // T3 = E*H
    ESP32_BN_Multiply_Mod_Preset(ctx, res->T, eb->E, eb->H);
    // Z3 = F*G        (Z3 = F*B)
    ESP32_BN_Multiply_Mod_Preset(ctx, res->Z, eb->F, eb->B);
}

/**
 * @brief Double a point in extended homogeneous space, i.e. res = 2*a
 * @param res  the result
 * @param a    the operand to double
 * @param eb   a temporary buffer for computation
 */
static void ESP32_Ext_Double(const ESP32_RSA_CTX *ctx, EdPointExt *res, const EdPointExt *a, struct Esp32C25519ExtLocal *eb) {
    // A = X1^2
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->A, a->X, a->X);
    // B = Y1^2
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->B, a->Y, a->Y);
    // H = A+B
    ESP32_BN_Add_Mod_Unsigned(eb->H, eb->A, eb->B, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // G = A-B            (B = A-B)
    ESP32_BN_Subtract_Mod_Unsigned(eb->B, eb->A, eb->B, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // C = 2*Z1^2         (A = 2*Z1^2)
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->A, a->Z, a->Z);
    ESP32_BN_Add_Mod_Unsigned(eb->A, eb->A, eb->A, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // E = H-(X1+Y1)^2
    ESP32_BN_Add_Mod_Unsigned(eb->E, a->X, a->Y, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    ESP32_BN_Multiply_Mod_Preset(ctx, eb->E, eb->E, eb->E);
    ESP32_BN_Subtract_Mod_Unsigned(eb->E, eb->H, eb->E, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // F = C+G            (F = A+B)
    ESP32_BN_Add_Mod_Unsigned(eb->F, eb->A, eb->B, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    ESP32_BN_Multiply_Mod_Preset(ctx, res->X, eb->E, eb->F);
    // Y3 = G*H
    ESP32_BN_Multiply_Mod_Preset(ctx, res->Y, eb->B, eb->H);
    // T3 = E*H
    ESP32_BN_Multiply_Mod_Preset(ctx, res->T, eb->E, eb->H);
    // Z3 = F*G
    ESP32_BN_Multiply_Mod_Preset(ctx, res->Z, eb->F, eb->B);
}

/**
 * @brief Convert an Edward point to an extended homogeneous point
 * @param res  the result
 * @param a    the operand to convert
 */
static void ESP32_Ed_To_Ext(const ESP32_RSA_CTX *ctx, EdPointExt *res, const EdPoint *a) {
    lt_memcpy(res->X, a->x, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memcpy(res->Y, a->y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memset(res->Z, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    res->Z[0] = 1;
    ESP32_BN_Multiply_Mod_Preset(ctx, res->T, a->x, a->y);
}

/**
 * @brief Convert an extended homogeneous point to an Edward point
 * @param res  the result
 * @param a    the operant to convert
 * @param eb   a temporary buffer for computation
 */
static void ESP32_Ext_To_Ed(const ESP32_RSA_CTX *ctx, EdPoint *res, const EdPointExt *a, struct Esp32C25519ExEdLocal *eb) {
    ESP32_BN_Pow_Mod_Preset(ctx, eb->z1, a->Z, Esp32_GetC25519_N2());
    // x = X/Z
    ESP32_BN_Multiply_Mod_Preset(ctx, res->x, a->X, eb->z1);
    // y = Y/Z
    ESP32_BN_Multiply_Mod_Preset(ctx, res->y, a->Y, eb->z1);
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
static void ESP32_Ext_Multiply_Const(const ESP32_RSA_CTX *ctx, EdPointExt *res, const u256 k, const EdPointExt *a, struct Esp32C25519ExMulLocal *mb) {
    lt_memcpy(&mb->r1, a, sizeof(EdPointExt));

    lt_memset(res, 0, sizeof(EdPointExt));
    res->Y[0] = 1;
    res->Z[0] = 1;

    // Montgomery ladder, multiplication, constant time
    for (int m = 252; m >= 0; --m) {
        if (0 == ESP32_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            ESP32_Ext_Add(ctx, &mb->r1, res, &mb->r1, &mb->eb);
            ESP32_Ext_Double(ctx, res, res, &mb->eb);
        } else {
            ESP32_Ext_Add(ctx, res, res, &mb->r1, &mb->eb);
            ESP32_Ext_Double(ctx, &mb->r1, &mb->r1, &mb->eb);
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
static void ESP32_Ext_Multiply_Normal(const ESP32_RSA_CTX *ctx, EdPointExt *res, const u256 k, const EdPointExt *a, struct Esp32C25519ExMulLocal *mb) {
    lt_memcpy(&mb->r1, a, sizeof(EdPointExt));

    lt_memset(res, 0, sizeof(EdPointExt));
    res->Y[0] = 1;
    res->Z[0] = 1;

    for (int m = 252; m >= 0; --m) {
        ESP32_Ext_Double(ctx, res, res, &mb->eb);
        if (1 == ESP32_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            ESP32_Ext_Add(ctx, res, res, &mb->r1, &mb->eb);
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
static void ESP32_Ext_Multiply_Add(const ESP32_RSA_CTX *ctx, EdPointExt *res, const u256 u, const EdPointExt *a, const u256 v, const EdPointExt *b, struct Esp32C25519ExMulLocal *mb) {
    lt_memset(&mb->r0, 0, sizeof(EdPointExt));
    mb->r0.Y[0] = 1;
    mb->r0.Z[0] = 1;
    ESP32_Ext_Add(ctx, &mb->r1, a, b, &mb->eb);

    int ub, vb;
    for (int m = 252; m >= 0; --m) {
        ESP32_Ext_Double(ctx, &mb->r0, &mb->r0, &mb->eb);
        ub = ESP32_BN_Get_Bit(u, m, LTSYSTEMCRYPTO_U32_PER_U256);
        vb = ESP32_BN_Get_Bit(v, m, LTSYSTEMCRYPTO_U32_PER_U256);
        if (ub == 0 && vb == 1) {
            ESP32_Ext_Add(ctx, &mb->r0, &mb->r0, b, &mb->eb);
        } else if (ub == 1 && vb == 0) {
            ESP32_Ext_Add(ctx, &mb->r0, &mb->r0, a, &mb->eb);
        } else if (ub == 1 && vb == 1) {
            ESP32_Ext_Add(ctx, &mb->r0, &mb->r0, &mb->r1, &mb->eb);
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
static void ESP32_Ed_Multiply_Const(const ESP32_RSA_CTX *ctx, EdPoint *res, const u256 k, const EdPoint *a, struct Esp32C25519EdMulLocal *mb) {
    // map the base point to an extended homogeneous point
    ESP32_Ed_To_Ext(ctx, &mb->P, a);

    // multiply in extended homogeneous space
    ESP32_Ext_Multiply_Const(ctx, &mb->P, k, &mb->P, &mb->mb);

    // map back to Edward point
    ESP32_Ext_To_Ed(ctx, res, &mb->P, &mb->eb);
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
static void ESP32_Ed_Multiply_Normal(const ESP32_RSA_CTX *ctx, EdPoint *res, const u256 k, const EdPoint *a, struct Esp32C25519EdMulLocal *mb) {
    // map the base point to an extended homogeneous point
    ESP32_Ed_To_Ext(ctx, &mb->P, a);

    // multiply in extended homogeneous space
    ESP32_Ext_Multiply_Normal(ctx, &mb->P, k, &mb->P, &mb->mb);

    // map back to Edward point
    ESP32_Ext_To_Ed(ctx, res, &mb->P, &mb->eb);
}

/**
 * @brief  Square root to get x, using u and v. A helper function to LT_Ed_Decode
 *         x = u * v^3 * (u * v^7) ^ (N-5)/8
 * @param res the resulting x coordinate
 * @param u   the u operand
 * @param v   the v operand
 * @return result code
 */
static LTSystemCryptoResult ESP32_Sqrt_ModN(const ESP32_RSA_CTX *ctx, u256 res, u256 u, u256 v) {
    struct SqrtLocal {
        u256 r0;
        u256 v3;
    };
    struct SqrtLocal *tmp = lt_malloc(sizeof(struct SqrtLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, v, v);   // r0 = v^2
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->v3, tmp->r0, v);  // v3 = v^3
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, tmp->r0, tmp->r0); // r0 = v^4
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, tmp->r0, tmp->v3); // r0 = v^7

    // (N-5)/8 = 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd
    // 250 '1', then '0', then '1'
    // r0 = (u * v^7) ^ (N-5)/8 % N
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, u, tmp->r0);   // r0 = u * v^7
    ESP32_BN_Pow_Mod_Preset(ctx, tmp->r0, tmp->r0, kEd25519_N58);

    // x = r0 * u * v^3 % N
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, tmp->r0, u);
    ESP32_BN_Multiply_Mod_Preset(ctx, res, tmp->r0, tmp->v3);

    // r0 = v * x^2 % N
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, res, res);
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, tmp->r0, v);

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Ok;
    if (ESP32_BN_Compare_Unsigned(tmp->r0, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
        // x = x * 2^((N-1)/4) % N
        ESP32_BN_Multiply_Mod_Preset(ctx, res, res, kEd25519_SQ);
        // r0 = v * x^2 % N
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, res, res);
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->r0, tmp->r0, v);
        if (ESP32_BN_Compare_Unsigned(tmp->r0, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
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
 * @param  res the resulting point
 * @param  y    the y coordinate
 * @return result code
 * @note   y includes the true y and the lsb of x
 */
static LTSystemCryptoResult ESP32_Ed_Decode(const ESP32_RSA_CTX *ctx, EdPoint *res, const u256 y) {
    struct DecodeLocal {
        u256 one;
        u256 u;
        u256 v;
    };
    struct DecodeLocal *tmp = lt_malloc(sizeof(struct DecodeLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    lt_memcpy(res->y, y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // step 1
    u32 xlsb = res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] >> 31;
    res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] &= 0x7FFFFFFF;
    if (ESP32_BN_Compare_Unsigned(res->y, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
        return kLTSystemCrypto_Result_WrongVerification;
    }

    // step 2
    lt_memset(tmp->one, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->one[0] = 1;
    // u = y^2 - 1
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->v, res->y, res->y);
    ESP32_BN_Subtract_Mod_Unsigned(tmp->u, tmp->v, tmp->one, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // v = d*y^2 + 1
    ESP32_BN_Multiply_Mod_Preset(ctx, tmp->v, tmp->v, kEd25519_d);
    ESP32_BN_Add_Mod_Unsigned(tmp->v, tmp->v, tmp->one, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);

    // step 3, x = u * v^3 * (u * v^7) ^ (N-5)/8 % N
    LTSystemCryptoResult ret = ESP32_Sqrt_ModN(ctx, res->x, tmp->u, tmp->v);
    if (kLTSystemCrypto_Result_Ok == ret) {
        // step 4
        if ((res->x[0] & 1) != xlsb) {
            ESP32_BN_Subtract_Unsigned(res->x, Esp32_GetC25519_N(), res->x, LTSYSTEMCRYPTO_U32_PER_U256);
        }
    }

    lt_memset(tmp, 0, sizeof(struct DecodeLocal));
    lt_free(tmp);
    return ret;
}

// 5.1.5
/**
 * @brief  Initialize the EdDSA context
 * @param  ctx     the context
 * @param  privKey  the private key, 32 Bytes
 * @return result code
 */
static LTSystemCryptoResult ESP32_Eddsa_Init(ESP32_EdDSA_CTX *ctx, const u8 privKey[EdDSA_KEY_LENGTH]) {
    if (!ctx || !privKey) {
        return kLTSystemCrypto_Result_Null;
    }

    struct InitLocal {
        u256 k;
        EdPoint P;
        ESP32_RSA_CTX rsaCtx;
        struct Esp32C25519EdMulLocal edmb;
    };
    struct InitLocal *tmp = lt_malloc(sizeof(struct InitLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    lt_memset(ctx, 0, sizeof(ESP32_EdDSA_CTX));

    // step 1
    ESP32_SHA512_Digest(privKey, LTSYSTEMCRYPTO_BYTES_PER_U256, ctx->secret);
    // step 2
    ctx->secret[0] &= 0xF8;    // clear 3 lsbs
    ctx->secret[31] &= 0x7F;   // clear 1 msb
    ctx->secret[31] |= 0x40;   // set the 2nd msb
    // step 3
    ESP32_BN_Mod_HW(tmp->k, ctx->s, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L, kC25519_Q2, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L1, LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_BN_Set_Mod(&tmp->rsaCtx, Esp32_GetC25519_N(), Esp32_GetC25519_R2(), LTSYSTEMCRYPTO_U32_PER_U256, Esp32_GetC25519_N1(), LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_Ed_Multiply_Normal(&tmp->rsaCtx, &tmp->P, tmp->k, &kEd25519_BP, &tmp->edmb);
    lt_memset(tmp->k, 0, sizeof(tmp->k));
    ESP32_BN_ClearRSA(LTSYSTEMCRYPTO_RSA512LEN);
    // step 4, public key A[32], i.e. pubKey[32]
    lt_memcpy(ctx->pubKey, tmp->P.y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    ctx->pubKey[31] |= ((tmp->P.x[0] & 1) << 7);

    lt_memset(tmp, 0, sizeof(struct InitLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

static LTSystemCryptoResult ESP32_Eddsa_GenPublicKey(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    ESP32_EdDSA_CTX *ctx = lt_malloc(sizeof(ESP32_EdDSA_CTX));
    if (!ctx) return kLTSystemCrypto_Result_OOM;
    LTSystemCryptoResult ret = ESP32_Eddsa_Init(ctx, privateKey);
    if (kLTSystemCrypto_Result_Ok == ret) lt_memcpy(publicKey, ctx->pubKey, EdDSA_KEY_LENGTH);
    lt_memset(ctx, 0, sizeof(ESP32_EdDSA_CTX));
    lt_free(ctx);
    return ret;
}

// 5.1.6
/**
 * @brief  Sign a block of data to produce a signature
 * @param  ctx       the context
 * @param  data      the data
 * @param  dataLen   the length of data
 * @param  signature  the output signature
 * @param  pubKey     the output public key, used later for verifying signature
 * @return result code
 */
static LTSystemCryptoResult ESP32_Eddsa_Sign(const ESP32_EdDSA_CTX *ctx, const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 pubKey[EdDSA_KEY_LENGTH]) {
    // sanity check
    if (!ctx || !pubKey || !data || !signature) {
        return kLTSystemCrypto_Result_Null;
    }

    struct SignLocal {
        u256 r;
        u256 k;
        u8 h[SHA512_HASH_LENGTH];
        ESP32_SHA512_CTX shaCtx;
        EdPoint P;
        ESP32_RSA_CTX rsaCtx;
        struct Esp32C25519EdMulLocal edmb;
    };
    struct SignLocal *tmp = lt_malloc(sizeof(struct SignLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    // step 1, 5.1.5, init to generate keys in context
    lt_memcpy(pubKey, ctx->pubKey, EdDSA_KEY_LENGTH);

    // step 2
    ESP32_SHA512_Init(&tmp->shaCtx);
    ESP32_SHA512_Update(&tmp->shaCtx, ctx->prefix, LTSYSTEMCRYPTO_BYTES_PER_U256);
    ESP32_SHA512_Update(&tmp->shaCtx, data, dataLen);
    ESP32_SHA512_Finish(&tmp->shaCtx, tmp->h);

    // step 3, string R[32]
    ESP32_BN_Mod_HW(tmp->r, (u32 *)tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, kC25519_L, kC25519_Q2, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L1, LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_BN_Set_Mod(&tmp->rsaCtx, Esp32_GetC25519_N(), Esp32_GetC25519_R2(), LTSYSTEMCRYPTO_U32_PER_U256, Esp32_GetC25519_N1(), LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_Ed_Multiply_Const(&tmp->rsaCtx, &tmp->P, tmp->r, &kEd25519_BP, &tmp->edmb);
    ESP32_BN_ClearRSA(LTSYSTEMCRYPTO_RSA512LEN);
    lt_memcpy(tmp->h, tmp->P.y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->h[31] |= ((tmp->P.x[0] & 1) << 7);
    lt_memcpy(signature, tmp->h, LTSYSTEMCRYPTO_BYTES_PER_U256);   // step 6, R is h, the first half of signature

    // step 4
    ESP32_SHA512_Init(&tmp->shaCtx);
    ESP32_SHA512_Update(&tmp->shaCtx, tmp->h, LTSYSTEMCRYPTO_BYTES_PER_U256);
    ESP32_SHA512_Update(&tmp->shaCtx, ctx->pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
    ESP32_SHA512_Update(&tmp->shaCtx, data, dataLen);
    ESP32_SHA512_Finish(&tmp->shaCtx, tmp->h);
    ESP32_BN_Mod_HW(tmp->k, (u32*)tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, kC25519_L, kC25519_Q2, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L1, LTSYSTEMCRYPTO_RSA512LEN);

    // step 5
    ESP32_BN_Multiply_Mod_HW(tmp->k, tmp->k, ctx->s, kC25519_L, kC25519_Q2, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L1, LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_BN_Add_Mod_Unsigned(tmp->r, tmp->r, tmp->k, kC25519_L, LTSYSTEMCRYPTO_U32_PER_U256);

    // step 6
    lt_memcpy(signature + 32, tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256);  // the second half of signature

    lt_memset(tmp, 0, sizeof(struct SignLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

// 5.1.7
/**
 * @brief  Verify a signature
 * @param  data      the data of the signature
 * @param  dataLen   the length of data
 * @param  signature  the signature to verify
 * @param  pubKey     the public key for verifying signature
 * @return result code
 */
static LTSystemCryptoResult ESP32_Eddsa_Verify(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 pubKey[EdDSA_KEY_LENGTH]) {
    // sanity check
    if (!pubKey || !signature || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    struct VerifyLocal {
        u32 buf[LTSYSTEMCRYPTO_U32_PER_U256];
        u32 h[SHA512_HASH_LENGTH / 4];
        u256 s;
        union {
            ESP32_SHA512_CTX ctx;
            struct {
                EdPointExt left;  // left and right extended points
                EdPointExt right;
                EdPoint A;
                ESP32_RSA_CTX rsaCtx;
                // shared temporary buffer for sub function calls
                union {
                    struct Esp32C25519ExMulLocal emb;
                    struct Esp32C25519ExtLocal eb;
                };
            };
        };
    };
    struct VerifyLocal *tmp = lt_malloc(sizeof(struct VerifyLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        lt_memcpy(tmp->buf, signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (ESP32_BN_Compare_Unsigned(tmp->buf, kC25519_L, LTSYSTEMCRYPTO_U32_PER_U256) >=0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }

        // 5.1.7
        // step 2
        ESP32_SHA512_Init(&tmp->ctx);
        ESP32_SHA512_Update(&tmp->ctx, signature, LTSYSTEMCRYPTO_BYTES_PER_U256);
        ESP32_SHA512_Update(&tmp->ctx, pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        ESP32_SHA512_Update(&tmp->ctx, data, dataLen);
        ESP32_SHA512_Finish(&tmp->ctx, (u8 *)tmp->h);
        u32 *k = tmp->h;         // secrete scalar k[32]
        ESP32_BN_Mod_HW(k, tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, kC25519_L, kC25519_Q2, LTSYSTEMCRYPTO_U32_PER_U256, kC25519_L1, LTSYSTEMCRYPTO_RSA512LEN);

        // step 1
        // step 3, [s]B = R + [k]A
        // check in extended space to avoid Ext_To_Ed computation
        ESP32_BN_Set_Mod(&tmp->rsaCtx, Esp32_GetC25519_N(), Esp32_GetC25519_R2(), LTSYSTEMCRYPTO_U32_PER_U256, Esp32_GetC25519_N1(), LTSYSTEMCRYPTO_RSA512LEN);
        // left is B
        ESP32_Ed_To_Ext(&tmp->rsaCtx, &tmp->left, &kEd25519_BP);
        // right is A
        lt_memcpy(tmp->buf, pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (kLTSystemCrypto_Result_Ok != (ret = ESP32_Ed_Decode(&tmp->rsaCtx, &tmp->A, tmp->buf))) break;
        ESP32_Ed_To_Ext(&tmp->rsaCtx, &tmp->right, &tmp->A);
        // s
        lt_memcpy(tmp->s, signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        // -k
        lt_memset(tmp->buf, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
        ESP32_BN_Subtract_Mod_Unsigned(tmp->buf, tmp->buf, k, kC25519_L, LTSYSTEMCRYPTO_U32_PER_U256);
        // [s]B - [k]A
        ESP32_Ext_Multiply_Add(&tmp->rsaCtx, &tmp->left, tmp->s, &tmp->left, tmp->buf, &tmp->right, &tmp->emb);

        lt_memcpy(tmp->buf, signature, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (kLTSystemCrypto_Result_Ok != (ret = ESP32_Ed_Decode(&tmp->rsaCtx, &tmp->A, tmp->buf))) break;
        ESP32_Ed_To_Ext(&tmp->rsaCtx, &tmp->right, &tmp->A);

        u32 *r = tmp->h + LTSYSTEMCRYPTO_U32_PER_U256;   // l is k
        // lx = lX/lZ == rX/rZ = rx. Hence, lX*rZ == lZ*rX % N
        ESP32_BN_Multiply_Mod_Preset(&tmp->rsaCtx, k, tmp->left.X, tmp->right.Z);
        ESP32_BN_Multiply_Mod_Preset(&tmp->rsaCtx, r, tmp->left.Z, tmp->right.X);
        if (ESP32_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }
        // ly = lY/lZ == rY/rZ = ry. Hence, lY*rZ == lZ*rY % N
        ESP32_BN_Multiply_Mod_Preset(&tmp->rsaCtx, k, tmp->left.Y, tmp->right.Z);
        ESP32_BN_Multiply_Mod_Preset(&tmp->rsaCtx, r, tmp->left.Z, tmp->right.Y);
        if (ESP32_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTSystemCrypto_Result_WrongVerification;
            break;
        }
    } while (0);

    ESP32_BN_ClearRSA(LTSYSTEMCRYPTO_RSA512LEN);
    lt_memset(tmp, 0, sizeof(struct VerifyLocal));
    lt_free(tmp);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoEd25519, LTHardwareCryptoEd25519) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoEd25519_GenPublicKey(const u8 privateKey[EdDSA_KEY_LENGTH], u8 publicKey[EdDSA_KEY_LENGTH]) {
    Esp32DriverCrypto_LockShaMutex();
    ESP32_SHA_Enable();
    ESP32_RSA_Enable();
    LTSystemCryptoResult ret = ESP32_Eddsa_GenPublicKey(privateKey, publicKey);
    ESP32_RSA_Disable();
    ESP32_SHA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    return ret;
}

static LTSystemCryptoResult LTHardwareCryptoEd25519_Sign(const u8 priKey[EdDSA_KEY_LENGTH], const u8 *data, LT_SIZE dataLen, u8 signature[EdDSA_SIGNATURE_LENGTH], u8 pubKey[EdDSA_KEY_LENGTH]) {
    ESP32_EdDSA_CTX *eddsaCtx = lt_malloc(sizeof(ESP32_EdDSA_CTX));
    if (!eddsaCtx) return kLTSystemCrypto_Result_OOM;
    Esp32DriverCrypto_LockShaMutex();
    ESP32_SHA_Enable();
    ESP32_RSA_Enable();
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        if (kLTSystemCrypto_Result_Ok != (ret = ESP32_Eddsa_Init(eddsaCtx, priKey))) {
            break;
        }
        ret = ESP32_Eddsa_Sign(eddsaCtx, data, dataLen, signature, pubKey);
    } while (false);
    ESP32_RSA_Disable();
    ESP32_SHA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    lt_memset(eddsaCtx, 0, sizeof(ESP32_EdDSA_CTX));
    lt_free(eddsaCtx);
    return ret;
}

static LTSystemCryptoResult LTHardwareCryptoEd25519_Verify(const u8 *data, LT_SIZE dataLen, const u8 signature[EdDSA_SIGNATURE_LENGTH], const u8 pubKey[EdDSA_KEY_LENGTH]) {
    Esp32DriverCrypto_LockShaMutex();
    ESP32_SHA_Enable();
    ESP32_RSA_Enable();
    LTSystemCryptoResult ret = ESP32_Eddsa_Verify(data, dataLen, signature, pubKey);
    ESP32_RSA_Disable();
    ESP32_SHA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    return ret;
}

static void LTHardwareCryptoEd25519_DestructObject(LTHardwareCryptoEd25519 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoEd25519_ConstructObject(LTHardwareCryptoEd25519 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEd25519, LTHardwareCryptoEd25519,
    GenPublicKey,
    Sign,
    Verify,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-May-22   gallienus   created
 */