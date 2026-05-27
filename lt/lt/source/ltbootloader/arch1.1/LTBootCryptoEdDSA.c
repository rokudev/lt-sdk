/*******************************************************************************
 * LTBootCryptoEdDSA.c                                             LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTBoot.h"
#include "LTBootCrypto.h"

struct MulULocal {
    u32 b[LTSYSTEMCRYPTO_U32_PER_U256 * 4];
    u64 val;
    u32 carry;
};

// Extended homogeneous coordinates
typedef struct EdPointExt {
    u256 X;  /* x = X/Z */
    u256 Y;  /* y = Y/Z */
    u256 Z;
    u256 T;  /* x*y = T/Z */
} EdPointExt;

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

/* 0xfffffffffffffffffffffffffffffffeb2106215d086329a7ed9ce5a30a2c131b */
static const u32 s_kEd25519_LB[9] = {
    0x0A2C131B, 0xED9CE5A3, 0x086329A7, 0x2106215D, 0xFFFFFFEB,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0000000F
};

// crypto constants
static const LTBootCryptoConsts s_cryptoConsts = {
    /* Ed25519_BP */
    {{0x8F25D51A,0xC9562D60,0x9525A7B2,0x692CC760,0xFDD6DC5C,0xC0A4E231,0xCD6E53FE,0x216936D3},
     {0x66666658,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666,0x66666666}},
    /* Ed25519_N = 0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed, modulus */
    {0xFFFFFFED,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x7FFFFFFF},
    /* Ed25519_L = 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed, group order */
    {0x5CF5D3ED,0x5812631A,0xA2F79CD6,0x14DEF9DE,0x00000000,0x00000000,0x00000000,0x10000000},
    /* Ed25519_N1 = -1/N % R, then take the lowest u32 */
    0x286BCA1B,
    /* Ed25519_R2 = R^2 % N = 0x1fd110, must be wide as RSA512 register */
    {0x001FD110,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000},
    /* Ed25519_N2 = N - 2 */
    {0xFFFFFFEB,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x7FFFFFFF},
    /* Ed25519_L1 = -1/L % R, then take the lowest u32 */
    0x12547E1B,
    /* Ed25519_Q2 = R^2 % L = 0x9dc924e5a45ffd7e7faf80eb68700589d31cab2023493f73a3dc22242419a0d, must be wide as RSA512 register */
    {0x42419A0D,0x3A3DC222,0x023493F7,0x9D31CAB2,0xB6870058,0xE7FAF80E,0x5A45FFD7,0x09DC924E},
    /* Ed25519_d = 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3 */
    {0x135978A3,0x75EB4DCA,0x4141D8AB,0x00700A4D,0x7779E898,0x8CC74079,0x2B6FFE73,0x52036CEE},
    /* Ed25519_d2 = d * 2 = 0xa406d9dc56dffce7198e80f2eef3d13000e0149a8283b156ebd69b9426b2f146 */
    {0x26B2F146,0xEBD69B94,0x8283B156,0x00E0149A,0xEEF3D130,0x198E80F2,0x56DFFCE7,0xA406D9DC},
    /* Ed25519_SQ = 2^((N-1)/4) % N = 0x2b8324804fc1df0b2b4d00993dfbd7a72f431806ad2fe478c4ee1b274a0ea0b0 */
    {0x4A0EA0B0,0xC4EE1B27,0xAD2FE478,0x2F431806,0x3DFBD7A7,0x2B4D0099,0x4FC1DF0B,0x2B832480},
    /* X25519_A24 = 121665 = 0x1DB41 */
    {0x0001DB41,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000},
    /* X25519 base point (generator) */
    {0x00000009,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000},
};

static int LT_BN_Get_Bit(const u32 *a, u32 pos, u32 len) {
    if (pos >= (len << 5)) {
        return -1; /* out of range */
    }
    u32 w = pos >> 5;
    u32 b = pos & 31;
    return (a[w] >> b) & 1;
}

static void LT_BN_Multiply_Unsigned(u32 *res, const u32 *a, u32 aLen, const u32 *b, u32 bLen, struct MulULocal *tmp) {
    LTBootPlatform_memset(tmp->b, 0, (aLen + bLen) * sizeof(u32));
    for (u32 i = 0; i < aLen; ++i) {
        tmp->carry = 0;
        for (u32 j = 0; j < bLen; ++j) {
            tmp->val = tmp->b[i + j] + ((u64)a[i]) * ((u64)b[j]) + tmp->carry;
            tmp->b[i + j] = (u32)tmp->val;
            tmp->carry = (tmp->val >> 32);
        }
        tmp->b[i + bLen] = tmp->carry;
    }
    LTBootPlatform_memcpy(res, tmp->b, (aLen + bLen) * sizeof(u32));
}

static int LT_BN_Subtract_Unsigned(u32 *res, const u32 *a, const u32 *b, u32 len) {
    u64 r;
    u32 i, c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)a[i]) - b[i] - c;
        res[i] = (u32)r;
        c = ((r >> 32) != 0);
    }
    return c;
}

static int LT_BN_Add_Unsigned(u32 *res, const u32 *a, const u32 *b, u32 len) {
    u64 r;
    u32 i, c;
    for (i = 0, c = 0; i < len; ++i) {
        r = ((u64)a[i]) + b[i] + c;
        res[i] = (u32)r;
        c = r >> 32;
    }
    return c;
}

static int LT_BN_Compare_Unsigned(const u32 *a, const u32 *b, u32 len) {
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

static void LT_BN_Add_Mod_Unsigned(u32 *res, const u32 *a, const u32 *b, const u32 *m, u32 len) {
    int c = LT_BN_Add_Unsigned(res, a, b, len);
    if (c || LT_BN_Compare_Unsigned(res, m, len) >= 0) {
        LT_BN_Subtract_Unsigned(res, res, m, len);
    }
}

static void LT_BN_Subtract_Mod_Unsigned(u32 *res, const u32 *a, const u32 *b, const u32 *m, u32 len) {
    if (LT_BN_Subtract_Unsigned(res, a, b, len)) {
        LT_BN_Add_Unsigned(res, res, m, len);
    }
}

/* (a * b) % N. N is (2^255 - 19) */
static void LT_C25519_Multiply_ModN(u256 res, const u256 a, const u256 b, struct C25519MulNLocal *tmp) {
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
        LTBootPlatform_memcpy(res, &tmp->r[8], sizeof(u256));
    } else {
        LTBootPlatform_memcpy(res, &tmp->r[0], sizeof(u256));
    }
}

/*
 * Barrett reduction, res = a % L
 *        Precompute:
 *            b = u32 = 2^32, k = ceil(log2(L)) = 8
 *            LB = floor(b^2k / L), has 9 u32 numbers
 *        Reduction:
 *            r = floor(a * LB / b^2k)
 *            r = r * L
 *            r = a - r
 *            if (r >= L) r = r - L
 *            res = r
 */
static void LT_C25519_ModL(u256 res, const u32 *a, u32 len, struct C25519ModLLocal *tmp) {
    // r[16] = floor(a * LB / b^2k)
    LT_BN_Multiply_Unsigned(tmp->r, a, len, s_kEd25519_LB, LTSYSTEMCRYPTO_U32_PER_U256 + 1, &tmp->ub);
    // r = r * L
    LT_BN_Multiply_Unsigned(tmp->r, tmp->r + LTSYSTEMCRYPTO_U32_PER_U256 * 2, len + 1 - LTSYSTEMCRYPTO_U32_PER_U256, s_cryptoConsts.kEd25519_L, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ub);
    // r = a - r
    LT_BN_Subtract_Unsigned(tmp->r, a, tmp->r, len);
    if (LT_BN_Compare_Unsigned(tmp->r, s_cryptoConsts.kEd25519_L, LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
        LT_BN_Subtract_Unsigned(tmp->r, tmp->r, s_cryptoConsts.kEd25519_L, LTSYSTEMCRYPTO_U32_PER_U256);
    }
    LTBootPlatform_memcpy(res, tmp->r, sizeof(u256));
}

/* Add two points in extended homogeneous space, i.e. res = a + b */
static void LT_Ext_Add(EdPointExt *res, const EdPointExt *a, const EdPointExt *b, struct C25519ExtLocal *eb) {
    // A = (Y1-X1)*(Y2-X2)
    LT_BN_Subtract_Mod_Unsigned(eb->H, a->Y, a->X, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(eb->A, b->Y, b->X, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->A, eb->H, eb->A, &eb->nb);
    // B = (Y1+X1)*(Y2+X2)
    LT_BN_Add_Mod_Unsigned(eb->H, a->Y, a->X, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(eb->B, b->Y, b->X, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->B, eb->H, eb->B, &eb->nb);
    // E = B-A
    LT_BN_Subtract_Mod_Unsigned(eb->E, eb->B, eb->A, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // H = B+A
    LT_BN_Add_Mod_Unsigned(eb->H, eb->B, eb->A, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);

    // C = T1*2*d*T2   (A = T1*2*d*T2)
    LT_C25519_Multiply_ModN(eb->A, a->T, s_cryptoConsts.kEd25519_d2, &eb->nb);
    LT_C25519_Multiply_ModN(eb->A, eb->A, b->T, &eb->nb);
    // D = Z1*2*Z2     (B = Z1*2*Z2)
    LT_BN_Add_Mod_Unsigned(eb->B, a->Z, a->Z, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->B, eb->B, b->Z, &eb->nb);
    // F = D-C         (F = B-A)
    LT_BN_Subtract_Mod_Unsigned(eb->F, eb->B, eb->A, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // G = D+C         (B = B+A)
    LT_BN_Add_Mod_Unsigned(eb->B, eb->B, eb->A, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    LT_C25519_Multiply_ModN(res->X, eb->E, eb->F, &eb->nb);
    // Y3 = G*H        (Y3 = B*H)
    LT_C25519_Multiply_ModN(res->Y, eb->B, eb->H, &eb->nb);
    // T3 = E*H
    LT_C25519_Multiply_ModN(res->T, eb->E, eb->H, &eb->nb);
    // Z3 = F*G        (Z3 = F*B)
    LT_C25519_Multiply_ModN(res->Z, eb->F, eb->B, &eb->nb);
}

/* Double a point in extended homogeneous space, i.e. res = 2*a */
static void LT_Ext_Double(EdPointExt *res, const EdPointExt *a, struct C25519ExtLocal *eb) {
    // A = X1^2
    LT_C25519_Multiply_ModN(eb->A, a->X, a->X, &eb->nb);
    // B = Y1^2
    LT_C25519_Multiply_ModN(eb->B, a->Y, a->Y, &eb->nb);
    // H = A+B
    LT_BN_Add_Mod_Unsigned(eb->H, eb->A, eb->B, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // G = A-B            (B = A-B)
    LT_BN_Subtract_Mod_Unsigned(eb->B, eb->A, eb->B, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);

    // C = 2*Z1^2         (A = 2*Z1^2)
    LT_C25519_Multiply_ModN(eb->A, a->Z, a->Z, &eb->nb);
    LT_BN_Add_Mod_Unsigned(eb->A, eb->A, eb->A, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // E = H-(X1+Y1)^2
    LT_BN_Add_Mod_Unsigned(eb->E, a->X, a->Y, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_C25519_Multiply_ModN(eb->E, eb->E, eb->E, &eb->nb);
    LT_BN_Subtract_Mod_Unsigned(eb->E, eb->H, eb->E, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // F = C+G            (F = A+B)
    LT_BN_Add_Mod_Unsigned(eb->F, eb->A, eb->B, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);

    // X3 = E*F
    LT_C25519_Multiply_ModN(res->X, eb->E, eb->F, &eb->nb);
    // Y3 = G*H
    LT_C25519_Multiply_ModN(res->Y, eb->B, eb->H, &eb->nb);
    // T3 = E*H
    LT_C25519_Multiply_ModN(res->T, eb->E, eb->H, &eb->nb);
    // Z3 = F*G
    LT_C25519_Multiply_ModN(res->Z, eb->F, eb->B, &eb->nb);
}

/* Convert an Edward point to an extended homogeneous point */
static void LT_Ed_To_Ext(EdPointExt *res, const EdPoint *a, struct C25519MulNLocal *nb) {
    LTBootPlatform_memcpy(res->X, a->x, sizeof(u256));
    LTBootPlatform_memcpy(res->Y, a->y, sizeof(u256));
    LTBootPlatform_memset(res->Z, 0, sizeof(u256));
    res->Z[0] = 1;
    LT_C25519_Multiply_ModN(res->T, a->x, a->y, nb);
}

/* u*a + v*b, extended homogeneous point multiplication and addition
 *        Use only in signature verification */
static void LT_Ext_Multiply_Add(EdPointExt *res, const u256 u, const EdPointExt *a, const u256 v, const EdPointExt *b, struct C25519ExMulLocal *mb) {
    LTBootPlatform_memset(&mb->r0, 0, sizeof(EdPointExt));
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
    LTBootPlatform_memcpy(res, &mb->r0, sizeof(EdPointExt));
}

static struct Sqrt {
    u256 r0;
    u256 r1;
    u256 v3;
    struct C25519MulNLocal nb;
} s_sqrt;

/* Square root to get x, using u and v. A helper function to LT_Ed_Decode
 *         x = u * v^3 * (u * v^7) ^ (N-5)/8 */
static LTBootSecurityCheck LT_Sqrt_ModN(u256 res, u256 u, u256 v) {
    struct Sqrt *tmp = &s_sqrt;

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

    LTBootSecurityCheck ret = kLTBootSecurityCheck_Pass;
    if (LT_BN_Compare_Unsigned(tmp->r1, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
        // x = x * 2^((N-1)/4) % N
        LT_C25519_Multiply_ModN(res, res, s_cryptoConsts.kEd25519_SQ, &tmp->nb);
        // r1 = v * x^2 % N
        LT_C25519_Multiply_ModN(tmp->r1, res, res, &tmp->nb);
        LT_C25519_Multiply_ModN(tmp->r1, tmp->r1, v, &tmp->nb);
        if (LT_BN_Compare_Unsigned(tmp->r1, u, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTBootSecurityCheck_Fail;
        }
    }

    LTBootPlatform_memset(tmp, 0, sizeof(struct Sqrt));
    return ret;
}

static struct Decode {
    u256 one;
    u256 u;
    u256 v;
    struct C25519MulNLocal nb;
} s_decode;

/* 5.1.3  Decode the y coordinate to the full Ed point on the curve */
static LTBootSecurityCheck LT_Ed_Decode(EdPoint *res, const u256 y) {
    struct Decode *tmp = &s_decode;
    LTBootPlatform_memcpy(res->y, y, sizeof(u256));
    // step 1
    u32 xlsb = res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] >> 31;
    res->y[LTSYSTEMCRYPTO_U32_PER_U256 - 1] &= 0x7FFFFFFF;
    if (LT_BN_Compare_Unsigned(res->y, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
        return kLTBootSecurityCheck_Fail;
    }

    // step 2
    LTBootPlatform_memset(tmp->one, 0, sizeof(u256));
    tmp->one[0] = 1;
    // u = y^2 - 1
    LT_C25519_Multiply_ModN(tmp->u, res->y, res->y, &tmp->nb);
    LTBootPlatform_memcpy(tmp->v, tmp->u, sizeof(u256));
    LT_BN_Subtract_Mod_Unsigned(tmp->u, tmp->u, tmp->one, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);
    // v = d*y^2 + 1
    LT_C25519_Multiply_ModN(tmp->v, tmp->v, s_cryptoConsts.kEd25519_d, &tmp->nb);
    LT_BN_Add_Mod_Unsigned(tmp->v, tmp->v, tmp->one, s_cryptoConsts.kEd25519_N, LTSYSTEMCRYPTO_U32_PER_U256);

    // step 3, x = u * v^3 * (u * v^7) ^ (N-5)/8 % N
    LTBootSecurityCheck ret = LT_Sqrt_ModN(res->x, tmp->u, tmp->v);
    if (kLTBootSecurityCheck_Pass == ret) {
        // step 4
        if ((res->x[0] & 1) != xlsb) {
            LT_BN_Subtract_Unsigned(res->x, s_cryptoConsts.kEd25519_N, res->x, LTSYSTEMCRYPTO_U32_PER_U256);
        }
    }

    LTBootPlatform_memset(tmp, 0, sizeof(struct Decode));
    return ret;
}

static struct Verify {
   u32 buf[LTSYSTEMCRYPTO_U32_PER_U256];
   u32 h[SHA512_HASH_LENGTH / 4];
   u256 s;
   union {
       LT_SHA512_CTX ctx;
       struct {
           EdPointExt left;  // left and right extended points
           EdPointExt right;
           EdPoint A;
           // shared tempory buffer for sub function calls
           union {
               struct C25519ModLLocal modb;
               struct C25519MulNLocal nb;
               struct C25519ExMulLocal emb;
               struct C25519ExtLocal eb;
           };
       };
   };
} s_verify;

/* 5.1.7 */
LTBootSecurityCheck
LTBootCrypto_Ed25519_Verify(const u8 * data,
                               u32 dataLen,
                               const u8 signature[EdDSA_SIGNATURE_LENGTH],
                               const u8 pubKey[EdDSA_KEY_LENGTH],
                               LTBootSecurityCheck * pCheck) {
    struct Verify * tmp = &s_verify;
    enum { kOffsetValue = 0xba5eba11 };

    volatile LTBootSecurityCheck ret = kLTBootSecurityCheck_Pass - 5*kOffsetValue;
    do {
        LTBootPlatform_memcpy(tmp->buf, signature + sizeof(u256), sizeof(u256));
        if (LT_BN_Compare_Unsigned(tmp->buf, s_cryptoConsts.kEd25519_L, LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
            ret = kLTBootSecurityCheck_Fail;
            break;
        }
        ret += kOffsetValue;

        // 5.1.7
        // step 2
        LTBootPlatform_SHA512_Init(&tmp->ctx);
        LTBootPlatform_SHA512_Update(&tmp->ctx, signature, sizeof(u256));
        LTBootPlatform_SHA512_Update(&tmp->ctx, pubKey, sizeof(u256));
        LTBootPlatform_SHA512_Update(&tmp->ctx, data, dataLen);
        LTBootPlatform_SHA512_Finish(&tmp->ctx, (u8 *)tmp->h);

        u32 *k = tmp->h;         // secret scalar k[32]
        LT_C25519_ModL(k, tmp->h, LTSYSTEMCRYPTO_U32_PER_U256 * 2, &tmp->modb);

        // step 1
        // step 3, [s]B = R + [k]A, so [s]B - [k]A = R
        // check in extended space to avoid Ext_To_Ed computation
        // left is B
        LT_Ed_To_Ext(&tmp->left, &s_cryptoConsts.kEd25519_BP, &tmp->nb);
        // right is A
        LTBootPlatform_memcpy(tmp->buf, pubKey, sizeof(u256));
        if (LT_Ed_Decode(&tmp->A, tmp->buf) != kLTBootSecurityCheck_Pass) {
            ret = kLTBootSecurityCheck_Fail;
            break;
        }
        ret += kOffsetValue;
        LT_Ed_To_Ext(&tmp->right, &tmp->A, &tmp->nb);
        // s
        LTBootPlatform_memcpy(tmp->s, signature + sizeof(u256), sizeof(u256));
        // -k
        LTBootPlatform_memset(tmp->buf, 0, sizeof(u256));
        LT_BN_Subtract_Mod_Unsigned(tmp->buf, tmp->buf, k, s_cryptoConsts.kEd25519_L, LTSYSTEMCRYPTO_U32_PER_U256);
        // [s]B - [k]A
        LT_Ext_Multiply_Add(&tmp->left, tmp->s, &tmp->left, tmp->buf, &tmp->right, &tmp->emb);

        LTBootPlatform_memcpy(tmp->buf, signature, sizeof(u256));

        if (LT_Ed_Decode(&tmp->A, tmp->buf) != kLTBootSecurityCheck_Pass) {
            ret = kLTBootSecurityCheck_Fail;
            break;
        }
        ret += kOffsetValue;
        LT_Ed_To_Ext(&tmp->right, &tmp->A, &tmp->nb);
        u32 *r = tmp->h + LTSYSTEMCRYPTO_U32_PER_U256;   // l is k
        // lx = lX/lZ == rX/rZ = rx. Hence, lX*rZ == lZ*rX % N
        LT_C25519_Multiply_ModN(k, tmp->left.X, tmp->right.Z, &tmp->nb);
        LT_C25519_Multiply_ModN(r, tmp->left.Z, tmp->right.X, &tmp->nb);
        if (LT_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTBootSecurityCheck_Fail;
            break;
        }
        ret += kOffsetValue;
        // ly = lY/lZ == rY/rZ = ry. Hence, lY*rZ == lZ*rY % N
        LT_C25519_Multiply_ModN(k, tmp->left.Y, tmp->right.Z, &tmp->nb);
        LT_C25519_Multiply_ModN(r, tmp->left.Z, tmp->right.Y, &tmp->nb);
        if (LT_BN_Compare_Unsigned(k, r, LTSYSTEMCRYPTO_U32_PER_U256) != 0) {
            ret = kLTBootSecurityCheck_Fail;
            break;
        }
        ret += kOffsetValue;
    } while (0);
    if (ret == kLTBootSecurityCheck_Pass) {
        *pCheck = ret;
    }
    LTBootPlatform_memset(tmp, 0, sizeof(struct Verify));
    return ret;
}

