/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoP256.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/*******************************************************************************
 NIST SP 800-186, https://csrc.nist.gov/publications/detail/sp/800-186/final
 NIST FIPS 186-5, https://csrc.nist.gov/publications/detail/fips/186/5/final
 IETF RFC 6979,   https://www.rfc-editor.org/rfc/rfc6979

 secp256r1   |  prime256v1   |   NIST P-256

 Curves in Short-Weierstrass Form
 y^2 = x^3 + a*x + b

 Implementation of Modular Arithmetic
 B = A % P
 B = (T + 2*S1 + 2*S2 + S3 + S4 - D1 - D2 - D3 - D4) % P
 T = ( A7 || A6 || A5 || A4 || A3 || A2 || A1 || A0 )
 S1 = ( A15 || A14 || A13 || A12 || A11 || 0 || 0 || 0 )
 S2 = ( 0 || A15 || A14 || A13 || A12 || 0 || 0 || 0 )
 S3 = ( A15 || A14 || 0 || 0 || 0 || A10 || A9 || A8 )
 S4 = ( A8 || A13 || A15 || A14 || A13 || A11 || A10 || A9 )
 D1 = ( A10 || A8 || 0 || 0 || 0 || A13 || A12 || A11 )
 D2 = ( A11 || A9 || 0 || 0 || A15 || A14 || A13 || A12 )
 D3 = ( A12 || 0 || A10 || A9 || A8 || A15 || A14 || A13 )
 D4 = ( A13 || 0 || A11 || A10 || A9 || 0 || A15 || A14 )

 Point double in Jacobian coordinates
 (X3 : Y3 : Z3) = 2 (X1 : Y1 : Z1), where
 A = 4 * X1 * Y1^2, B = 8 * Y1^4 ,
 C = 3 * (X1 - Z1^2) * (X1 + Z1^2),
 D = -2 * A + C^2,
 X3 = D;
 Y3 = C * (A – D) – B;
 Z3 = 2 * Y1 * Z1

 Point addition in Jacobian coordinates, full addition
 (X3 : Y3 : Z3) = (X1 : Y1 : Z1) + (X2 : Y2 : Z2), where
 A = X1 * Z2^2
 B = X2 * Z1^2
 C = Y1 * Z2^3
 D = Y2 * Z1^3
 E = B - A
 F = D - C
 X3 = F^2 - E^3 - 2 * A * E^2
 Y3 = F * (A * E^2 - X3) - C * E^3
 Z3 = E * Z1 * Z2

 Point addition in Jacobian and affine coordinates, mixed addition
 (X3 : Y3 : Z3) = (X1 : Y1 : Z1) + (X2 : Y2 : 1), where
 A = X2 * Z1^2
 B = Y2 * Z1^3
 C = A - X1
 D = B - Y1
 X3 = D^2 - C^3 - 2 * X1 * C^2
 Y3 = D * (X1 * C^2 - X3) - Y1 * C^3
 Z3 = C * Z1

 P256 constants
 const u32 kP256_P[LTSYSTEMCRYPTO_U32_PER_U256 + 1];  // P256 modulus, the last 0 is for LT_P256_Unsigned_ModP optimization.
 const u32 kP256_a[LTSYSTEMCRYPTO_U32_PER_U256];      // y^2 = x^3 + a*x + b
 const u32 kP256_b[LTSYSTEMCRYPTO_U32_PER_U256];
 const u32 kP256_N[LTSYSTEMCRYPTO_U32_PER_U256];      // P256 group order
 const u32 kP256_N1;                                  // -1/N % R, then take the lowest u32
 const u32 kP256_RN2[LTSYSTEMCRYPTO_U32_PER_U256];    // R^2 % N
 const u32 kP256_MN1[LTSYSTEMCRYPTO_U32_PER_U256];    // R % N
 const u32 kP256_P1;                                  // -1/P % R, then take the lowest u32
 const u32 kP256_RP2[LTSYSTEMCRYPTO_U32_PER_U256];    // R^2 % P
 const u32 kP256_MP1[LTSYSTEMCRYPTO_U32_PER_U256];    // R % P
 const P256Point kP256_G;                             // P256 generator
 ******************************************************************************/

#include <lt/LT.h>
#include "LTDriverCryptoP256.h"

/* kP256_N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551,  P256 group order*/
static const u32 kP256_N[LTSYSTEMCRYPTO_U32_PER_U256] = {0xfc632551,0xf3b9cac2,0xa7179e84,0xbce6faad,0xffffffff,0xffffffff,0x00000000,0xffffffff};
/* kP256_N1 = -1/N % R = 0x60d06633a9d6281c50fe77ecc588c6f648c944087d74d2e4ccd1c8aaee00bc4f, then take the lowest u32 */
static const u32 kP256_N1 = 0xee00bc4f;
/* kP256_RN2 = R^2 % N = 0x66e12d94f3d956202845b2392b6bec594699799c49bd6fa683244c95be79eea2 */
static const u32 kP256_RN2[LTSYSTEMCRYPTO_U32_PER_U256] = {0xbe79eea2,0x83244c95,0x49bd6fa6,0x4699799c,0x2b6bec59,0x2845b239,0xf3d95620,0x66e12d94};
/* kP256_P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff, modulus, the last 0 is for LT_P256_Unsigned_ModP optimization. */
static const u32 kP256_P[LTSYSTEMCRYPTO_U32_PER_U256 + 1] = {0xffffffff,0xffffffff,0xffffffff,0x00000000,0x00000000,0x00000000,0x00000001,0xffffffff,0x00000000};
/* kP256_a = -3 % P = 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc, y^2 = x^3 + a*x + b */
static const u32 kP256_a[LTSYSTEMCRYPTO_U32_PER_U256] = {0xfffffffc,0xffffffff,0xffffffff,0x00000000,0x00000000,0x00000000,0x00000001,0xffffffff};
/* kP256_b = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b */
static const u32 kP256_b[LTSYSTEMCRYPTO_U32_PER_U256] = {0x27d2604b,0x3bce3c3e,0xcc53b0f6,0x651d06b0,0x769886bc,0xb3ebbd55,0xaa3a93e7,0x5ac635d8};
/* kP256_MN1 = R % N = 0xffffffff00000000000000004319055258e8617b0c46353d039cdaaf */
static const u32 kP256_MN1[LTSYSTEMCRYPTO_U32_PER_U256] = {0x039cdaaf,0x0c46353d,0x58e8617b,0x43190552,0x00000000,0x00000000,0xffffffff,0x00000000};
/* kP256_P1 = -1/P % R = 0xffffffff00000002000000000000000000000001000000000000000000000001 */
static const u32 kP256_P1 = 0x00000001;
/* kP256_RP2 = R^2 % P = 0x4fffffffdfffffffffffffffefffffffbffffffff0000000000000003 */
static const u32 kP256_RP2[LTSYSTEMCRYPTO_U32_PER_U256] = {0x00000003,0x00000000,0xffffffff,0xfffffffb,0xfffffffe,0xffffffff,0xfffffffd,0x00000004};
/* kP256_MP1 = R % P = 0xffffffff00000000000000004319055258e8617b0c46353d039cdaaf */
static const u32 kP256_MP1[LTSYSTEMCRYPTO_U32_PER_U256] = {0x00000001,0x00000000,0x00000000,0xffffffff,0xffffffff,0xffffffff,0xfffffffe,0x00000000};

const u32* LT_GetP256_N(void) {
    return kP256_N;
}

u32 LT_GetP256_N1(void) {
    return kP256_N1;
}

const u32* LT_GetP256_RN2(void) {
    return kP256_RN2;
}

/**
 * @brief res = a % P
 * @param res  the result, u256
 * @param a    the operand to module, u512
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Unsigned_ModP(u256 res, const u32 a[LTSYSTEMCRYPTO_U32_PER_U256 * 2], struct P256ModLocal *tmp) {
    tmp->r[LTSYSTEMCRYPTO_U32_PER_U256] = 0;
    tmp->t[LTSYSTEMCRYPTO_U32_PER_U256] = 0;
    // B = (T + 2*S1 + 2*S2 + S3 + S4 - D1 - D2 - D3 - D4) % P
    // T = ( A7 || A6 || A5 || A4 || A3 || A2 || A1 || A0 )
    lt_memcpy(tmp->r, a, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // S1 = ( A15 || A14 || A13 || A12 || A11 || 0 || 0 || 0 )
    lt_memcpy(tmp->t + 3, a + 11, LTSYSTEMCRYPTO_BYTES_PER_U32 * 5);
    lt_memset(tmp->t, 0, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // S2 = ( 0 || A15 || A14 || A13 || A12 || 0 || 0 || 0 )
    tmp->t[7] = 0;
    lt_memcpy(tmp->t + 3, a + 12, LTSYSTEMCRYPTO_BYTES_PER_U32 * 4);
    lt_memset(tmp->t, 0, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // S3 = ( A15 || A14 || 0 || 0 || 0 || A10 || A9 || A8 )
    tmp->t[7] = a[15];
    tmp->t[6] = a[14];
    lt_memcpy(tmp->t, a + 8, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    lt_memset(tmp->t + 3, 0, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // S4 = ( A8 || A13 || A15 || A14 || A13 || A11 || A10 || A9 )
    tmp->t[7] = a[8];
    tmp->t[6] = a[13];
    lt_memcpy(tmp->t, a + 9, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    lt_memcpy(tmp->t + 3, a + 13, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Add_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // D1 = ( A10 || A8 || 0 || 0 || 0 || A13 || A12 || A11 )
    tmp->t[7] = a[10];
    tmp->t[6] = a[8];
    lt_memcpy(tmp->t, a + 11, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    lt_memset(tmp->t + 3, 0, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Subtract_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // D2 = ( A11 || A9 || 0 || 0 || A15 || A14 || A13 || A12 )
    tmp->t[7] = a[11];
    tmp->t[6] = a[9];
    lt_memcpy(tmp->t, a + 12, LTSYSTEMCRYPTO_BYTES_PER_U32 * 4);
    lt_memset(tmp->t + 4, 0, LTSYSTEMCRYPTO_BYTES_PER_U32 * 2);
    LT_BN_Subtract_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // D3 = ( A12 || 0 || A10 || A9 || A8 || A15 || A14 || A13 )
    tmp->t[7] = a[12];
    tmp->t[6] = 0;
    lt_memcpy(tmp->t, a + 13, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    lt_memcpy(tmp->t + 3, a + 8, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Subtract_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // D4 = ( A13 || 0 || A11 || A10 || A9 || 0 || A15 || A14 )
    tmp->t[7] = a[13];
    tmp->t[6] = 0;
    tmp->t[2] = 0;
    lt_memcpy(tmp->t, a + 14, LTSYSTEMCRYPTO_BYTES_PER_U32 * 2);
    lt_memcpy(tmp->t + 3, a + 9, LTSYSTEMCRYPTO_BYTES_PER_U32 * 3);
    LT_BN_Subtract_Unsigned(tmp->r, tmp->r, tmp->t, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
    // modulo
    if (tmp->r[LTSYSTEMCRYPTO_U32_PER_U256] & 0x80000000) {
        // negative
        while (tmp->r[LTSYSTEMCRYPTO_U32_PER_U256] != 0) {
            LT_BN_Add_Unsigned(tmp->r, tmp->r, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
        }
    } else {
        // positive
        while (tmp->r[LTSYSTEMCRYPTO_U32_PER_U256] != 0 || LT_BN_Compare_Unsigned(tmp->r, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256) > 0) {
            LT_BN_Subtract_Unsigned(tmp->r, tmp->r, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256 + 1);
        }
    }
    // result
    lt_memcpy(res, tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256);
}

/**
 * @brief res = (a * b) % N
 *
 * @param res  the result
 * @param a    the first operand
 * @param b    the second operand
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Multiply_ModP(u256 res, const u256 a, const u256 b, struct P256MulPLocal *tmp) {
    LT_BN_Multiply_Unsigned(tmp->rab, a, LTSYSTEMCRYPTO_U32_PER_U256, b, LTSYSTEMCRYPTO_U32_PER_U256, &tmp->ml);
    LT_P256_Unsigned_ModP(res, tmp->rab, &tmp->mdl);
}

/**
 * @brief double a point in Jacobian coordinates
 *
 *    (X3 : Y3 : Z3) = 2 (X1 : Y1 : Z1)
 *    A = 4 * X1 * Y1^2, B = 8 * Y1^4 ,
 *    C = 3 * (X1 - Z1^2) * (X1 + Z1^2),
 *    D = -2 * A + C^2,
 *    X3 = D;
 *    Y3 = C * (A – D) – B;
 *    Z3 = 2 * Y1 * Z1
 *
 * @param res  the result Jacobian point
 * @param a    the Jacobian point to double
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Double_Jac(P256PointExt *res, const P256PointExt *a, struct P256DoubleLocal *tmp) {
    res->inf = a->inf;
    if (res->inf) return;
    // A = (4 * X1 * Y1 * Y1) % P
    LT_P256_Multiply_ModP(tmp->Ysq, a->Y, a->Y, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->A, a->X, tmp->Ysq, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->A, tmp->A, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->A, tmp->A, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // B = (8 * Y1 * Y1 * Y1 * Y1) % P
    LT_P256_Multiply_ModP(tmp->B, tmp->Ysq, tmp->Ysq, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->B, tmp->B, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->B, tmp->B, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->B, tmp->B, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // C = (3 * (X1 - Z1 * Z1) * (X1 + Z1 * Z1)) % P
    LT_P256_Multiply_ModP(tmp->Ysq, a->Z, a->Z, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(tmp->D, a->X, tmp->Ysq, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->C, a->X, tmp->Ysq, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(tmp->C, tmp->C, tmp->D, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->D, tmp->C, tmp->C, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->C, tmp->C, tmp->D, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // D = (-2 * A + C * C) % P
    LT_P256_Multiply_ModP(tmp->D, tmp->C, tmp->C, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(tmp->D, tmp->D, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(tmp->D, tmp->D, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // X3 = D % P
    lt_memcpy(res->X, tmp->D, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // Y3 = (C * (A - D) - B) % P
    lt_memcpy(tmp->Ysq, a->Y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(res->Y, tmp->A, tmp->D, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(res->Y, tmp->C, res->Y, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->Y, res->Y, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // Z3 = (2 * Y1 * Z1) % P
    LT_P256_Multiply_ModP(res->Z, tmp->Ysq, a->Z, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(res->Z, res->Z, res->Z, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
}

/**
 * @brief res = a + b, add two points in Jacobian coordinates
 *
 *    (X3 : Y3 : Z3) = (X1 : Y1 : Z1) + (X2 : Y2 : Z2)
 *    A = X1 * Z2^2
 *    B = X2 * Z1^2
 *    C = Y1 * Z2^3
 *    D = Y2 * Z1^3
 *    E = B - A
 *    F = D - C
 *    X3 = F^2 - E^3 - 2 * A * E^2
 *    Y3 = F * (A * E^2 - X3) - C * E^3
 *    Z3 = E * Z1 * Z2
 *
 * @param res  the result Jacobian point
 * @param a    a Jacobian point
 * @param b    a Jacobian point
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Add_Jac(P256PointExt *res, const P256PointExt *a, const P256PointExt *b, struct P256AddLocal *tmp) {
    if (a->inf) {
        lt_memcpy(res, b, sizeof(P256PointExt));
        return;
    }
    if (b->inf) {
        lt_memcpy(res, a, sizeof(P256PointExt));
        return;
    }
    res->inf = false;
    // A = (X1 * Z2 * Z2) % P
    LT_P256_Multiply_ModP(tmp->Zsq, b->Z, b->Z, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->A, a->X, tmp->Zsq, &tmp->ml);
    // C = (Y1 * Z2 * Z2 * Z2) % P
    LT_P256_Multiply_ModP(tmp->C, b->Z, tmp->Zsq, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->C, a->Y, tmp->C, &tmp->ml);
    // B = (X2 * Z1 * Z1) % P
    LT_P256_Multiply_ModP(tmp->Zsq, a->Z, a->Z, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->B, b->X, tmp->Zsq, &tmp->ml);
    // D = (Y2 * Z1 * Z1 * Z1) % P
    LT_P256_Multiply_ModP(tmp->D, a->Z, tmp->Zsq, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->D, b->Y, tmp->D, &tmp->ml);
    // if A == B:
    if (LT_BN_Compare_Unsigned(tmp->A, tmp->B, LTSYSTEMCRYPTO_BYTES_PER_U256) == 0) {
    //     if C != D: # Q1 == -Q2, so result is infinity
        if (LT_BN_Compare_Unsigned(tmp->C, tmp->D, LTSYSTEMCRYPTO_BYTES_PER_U256) != 0) {
            res->inf = true;
        } else {
    //     else:        # Q1 == Q2, so double
            LT_P256_Double_Jac(res, a, &tmp->dl);
        }
        return;
    }
    // E = B - A
    LT_BN_Subtract_Mod_Unsigned(tmp->E, tmp->B, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // F = D - C
    LT_BN_Subtract_Mod_Unsigned(tmp->F, tmp->D, tmp->C, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // X3 = (F * F - E * E * E - 2 * A * E * E) % P
    LT_P256_Multiply_ModP(tmp->Zsq, tmp->E, tmp->E, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->B, tmp->A, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Add_Mod_Unsigned(tmp->B, tmp->B, tmp->E, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(tmp->B, tmp->B, tmp->Zsq, &tmp->ml);
    LT_P256_Multiply_ModP(res->X, tmp->F, tmp->F, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->X, res->X, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // Y3 = (F * ( A * E * E - X3) - C * E * E * E) % P
    LT_P256_Multiply_ModP(tmp->B, tmp->A, tmp->Zsq, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->Y, tmp->B, res->X, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(res->Y, res->Y, tmp->F, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->B, tmp->E, tmp->Zsq, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->B, tmp->C, tmp->B, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->Y, res->Y, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // Z3 = E * Z1 * Z2
    LT_P256_Multiply_ModP(res->Z, a->Z, b->Z, &tmp->ml);
    LT_P256_Multiply_ModP(res->Z, res->Z, tmp->E, &tmp->ml);
}

/**
 * @brief res = a + b, add two points in Jacobian and affine coordinates, mixed
 *
 *    (X3 : Y3 : Z3) = (X1 : Y1 : Z1) + (X2 : Y2)
 *    A = X2 * Z1^2
 *    B = Y2 * Z1^3
 *    C = A - X1
 *    D = B - Y1
 *    X3 = D^2 - C^3 - 2 * X1 * C^2
 *    Y3 = D * (X1 * C^2 - X3) - Y1 * C^3
 *    Z3 = C * Z1
 *
 * @param res  the result Jacobian point
 * @param a    a Jacobian point
 * @param b    a affine point
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Add_Mixed(P256PointExt *res, const P256PointExt *a, const P256Point *b, struct P256AddLocal *tmp) {
    if (a->inf) {
        lt_memcpy(res->X, b->x, LTSYSTEMCRYPTO_BYTES_PER_U256);
        lt_memcpy(res->Y, b->y, LTSYSTEMCRYPTO_BYTES_PER_U256);
        lt_memset(res->Z, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
        res->Z[0] = 1;
        res->inf = b->inf;
        return;
    }
    if (b->inf) {
        lt_memcpy(res, a, sizeof(P256PointExt));
        return;
    }
    res->inf = false;
    // A = (X2 * Z1^2) % P
    LT_P256_Multiply_ModP(tmp->Zsq, a->Z, a->Z, &tmp->ml);     // Z1^2
    LT_P256_Multiply_ModP(tmp->A, b->x, tmp->Zsq, &tmp->ml);
    // B = (Y2 * Z1^3) % P
    LT_P256_Multiply_ModP(tmp->B, a->Z, tmp->Zsq, &tmp->ml);   // Z1^3
    LT_P256_Multiply_ModP(tmp->B, b->y, tmp->B, &tmp->ml);
    // if A == X1:
    if (LT_BN_Compare_Unsigned(tmp->A, a->X, LTSYSTEMCRYPTO_BYTES_PER_U256) == 0) {
    //     if B != Y1: # Q1 == -Q2, so result is infinity
        if (LT_BN_Compare_Unsigned(tmp->B, a->Y, LTSYSTEMCRYPTO_BYTES_PER_U256) != 0) {
            res->inf = true;
        } else {
    //     else:        # Q1 == Q2, so double
            LT_P256_Double_Jac(res, a, &tmp->dl);
        }
        return;
    }
    // C = (A - X1) % P
    LT_BN_Subtract_Mod_Unsigned(tmp->C, tmp->A, a->X, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // D = (B - Y1) % P
    LT_BN_Subtract_Mod_Unsigned(tmp->D, tmp->B, a->Y, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // X3 = (D^2 - C^3 - 2 * X1 * C^2) % P
    LT_P256_Multiply_ModP(tmp->Zsq, tmp->C, tmp->C, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->A, tmp->C, tmp->Zsq, &tmp->ml);  // C^3
    LT_P256_Multiply_ModP(tmp->B, a->X, tmp->Zsq, &tmp->ml);    // X1 * C^2
    LT_P256_Multiply_ModP(res->X, tmp->D, tmp->D, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->X, res->X, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(res->X, res->X, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_BN_Subtract_Mod_Unsigned(res->X, res->X, tmp->B, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // Y3 = (D * (X1 * C^2 - X3) - Y1 * C^3) % P
    LT_P256_Multiply_ModP(tmp->A, a->Y, tmp->A, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->Y, tmp->B, res->X, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(res->Y, res->Y, tmp->D, &tmp->ml);
    LT_BN_Subtract_Mod_Unsigned(res->Y, res->Y, tmp->A, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    // Z3 = (C * Z1) % P
    LT_P256_Multiply_ModP(res->Z, tmp->C, a->Z, &tmp->ml);
}

/**
 * @brief Inversion mod P using Montgomery reduction
 *        res = a^-1 = a^(P-2) % P, because Fermat's theorem 1 = a^(P-1) % P
 *        Only works for the constant P
 *        P-2 = 0xffffffff,00000001,00000000,00000000,00000000,ffffffff,ffffffff,fffffffD
 * @param res  the result
 * @param a    the operand to inverse
 * @param tmp  a temporary buffer for computation
 * @note  both a and res are normal u256
 */
static void LT_P256_Inverse_ModP_Mont(u256 res, const u256 a, struct P256InvLocal *tmp) {
    // map to mont space
    lt_memcpy(tmp->r0, kP256_MP1, LTSYSTEMCRYPTO_BYTES_PER_U256);              // r0 = 1 * R % P
    LT_BN_Multiply_Mont(tmp->r1, a, kP256_RP2, kP256_P, kP256_P1, &tmp->mml);  // r1 = a * R % P

    int m = 0;
    // bit[255...224] = 1
    for (m = 255; m >= 224; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, kP256_P, kP256_P1, &tmp->mml);
    }
    // bit[223...193] = 0
    for (m = 223; m >= 193; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
    }
    // bit[192] = 1
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, kP256_P, kP256_P1, &tmp->mml);
    // bit[191...96] = 0
    for (m = 191; m >= 96; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
    }
    // bit[95...2] = 1
    for (m = 95; m >= 2; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, kP256_P, kP256_P1, &tmp->mml);
    }
    // bit[1] = 0
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
    // bit[0] = 1
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, kP256_P, kP256_P1, &tmp->mml);
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, kP256_P, kP256_P1, &tmp->mml);

    // map back to normal space
    lt_memcpy(tmp->mml.t, tmp->r0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memset(tmp->mml.t + LTSYSTEMCRYPTO_U32_PER_U256, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Redux_Mont(res, tmp->mml.t, kP256_P, kP256_P1);
}

/**
 * @brief Convert a point from Jacobian coordinates to affine coordinates
 *
 * @param res  the result
 * @param a    the point to convert
 * @param tmp  a temporary buffer for computation
 */
static void LT_P256_Jac2Point(P256Point *res, const P256PointExt *a, struct P256E2PLocal *tmp) {
    res->inf = a->inf;
    if (res->inf) return;
    LT_P256_Inverse_ModP_Mont(tmp->Zinv, a->Z, &tmp->il);
    // x = X/Z^2, y = Y/Z^3
    LT_P256_Multiply_ModP(res->x, tmp->Zinv, tmp->Zinv, &tmp->ml);
    LT_P256_Multiply_ModP(res->y, res->x, tmp->Zinv, &tmp->ml);
    LT_P256_Multiply_ModP(res->x, res->x, a->X, &tmp->ml);
    LT_P256_Multiply_ModP(res->y, res->y, a->Y, &tmp->ml);
}

/**
 * @brief Convert a point from affine coordinates to Jacobian coordinates
 *
 * @param res  the result
 * @param a    the point to convert
 */
static void LT_P256_Point2Jac(P256PointExt *res, const P256Point *a) {
    res->inf = a->inf;
    if (res->inf) return;
    lt_memcpy(res->X, a->x, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memcpy(res->Y, a->y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memset(res->Z + 1, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    res->Z[0] = 1;
}

/**
 * @brief res = a + b
 *
 * @param res  the result
 * @param a    the first curve point
 * @param b    the second curve point
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Add(P256Point *res, const P256Point *a, const P256Point *b, struct P256ScaLocal *tmp) {
    LT_P256_Point2Jac(&tmp->r0, a);
    LT_P256_Point2Jac(&tmp->r1, b);
    LT_P256_Add_Jac(&tmp->r0, &tmp->r0, &tmp->r1, &tmp->al);
    LT_P256_Jac2Point(res, &tmp->r0, &tmp->el);
}

/**
 * @brief res = k*Q, constant time, Montgomery ladder
 *        Sign must use this scalar multiplication for security.
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Const(P256Point *res, const u256 k, const P256Point *q, struct P256ScaLocal *tmp) {
    res->inf = q->inf;
    if (res->inf) return;
    // prepare r0 = 0 and r1 = q
    tmp->r0.inf = true;
    LT_P256_Point2Jac(&tmp->r1, q);
    for (int m = 255; m >= 0; --m) {
        if (0 == LT_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            LT_P256_Add_Jac(&tmp->r1, &tmp->r0, &tmp->r1, &tmp->al);
            LT_P256_Double_Jac(&tmp->r0, &tmp->r0, &tmp->dl);
        } else {
            LT_P256_Add_Jac(&tmp->r0, &tmp->r0, &tmp->r1, &tmp->al);
            LT_P256_Double_Jac(&tmp->r1, &tmp->r1, &tmp->dl);
        }
    }
    tmp->r0.inf = LT_BN_IsZero(tmp->r0.Z, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Jac2Point(res, &tmp->r0, &tmp->el);
}

/**
 * @brief res = k*Q, double and add
 *        Faster than constant-time, but only use in public key generation
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Normal(P256Point *res, const u256 k, const P256Point *q, struct P256ScaLocal *tmp) {
    res->inf = q->inf;
    if (res->inf) return;
    // prepare r0 = 0 and r1 = q
    tmp->r0.inf = true;
    for (int m = 255; m >= 0; --m) {
        LT_P256_Double_Jac(&tmp->r0, &tmp->r0, &tmp->dl);
        if (1 == LT_BN_Get_Bit(k, m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            LT_P256_Add_Mixed(&tmp->r0, &tmp->r0, q, &tmp->al);
        }
    }
    tmp->r0.inf = LT_BN_IsZero(tmp->r0.Z, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Jac2Point(res, &tmp->r0, &tmp->el);
}

/**
 * @brief res = u * G + v * Q, Shamir's look up
 *        Only use in signature verification
 *
 * @param res  the result
 * @param k    the scalar
 * @param q    the point to multiply
 * @param tmp  a temporary buffer for computation
 */
void LT_P256_Multiply_Add(P256Point *res, const u256 u, const P256Point *g, const u256 v, const P256Point *q, struct P256ScaLocal *tmp) {
    if (g->inf && q->inf) {
        res->inf = true;
        return;
    } else if (g->inf && !q->inf) {
        return LT_P256_Multiply_Normal(res, v, q, tmp);
    } else if (!g->inf && q->inf) {
        return LT_P256_Multiply_Normal(res, u, g, tmp);
    }
    // gq00 = 0, gq01 = q, gq10 = g, gq11 = g + q
    // prepare r0 = 0 and r1 = gq11
    tmp->r0.inf = true;
    LT_P256_Point2Jac(&tmp->r1, g);
    LT_P256_Add_Mixed(&tmp->r1, &tmp->r1, q, &tmp->al);
    int ub, vb;
    for (int m = 255; m >= 0; --m) {
        LT_P256_Double_Jac(&tmp->r0, &tmp->r0, &tmp->dl);
        ub = LT_BN_Get_Bit(u, m, LTSYSTEMCRYPTO_U32_PER_U256);
        vb = LT_BN_Get_Bit(v, m, LTSYSTEMCRYPTO_U32_PER_U256);
        if (ub == 0 && vb == 1) LT_P256_Add_Mixed(&tmp->r0, &tmp->r0, q, &tmp->al);
        else if (ub == 1 && vb == 0) LT_P256_Add_Mixed(&tmp->r0, &tmp->r0, g, &tmp->al);
        else if (ub == 1 && vb == 1) LT_P256_Add_Jac(&tmp->r0, &tmp->r0, &tmp->r1, &tmp->al);
    }
    tmp->r0.inf = LT_BN_IsZero(tmp->r0.Z, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Jac2Point(res, &tmp->r0, &tmp->el);
}

/**
 * @brief Check if a is on curve, partial public key validation
 *        NIST SP 800-186, D.1.1.1
 *
 * @param a   the curve point to check
 * @param tmp a temporary buffer for computation
 * @return true
 * @return false
 */
bool LT_P256_Validate_Partial(const P256Point *a, struct P256ValPLocal *tmp) {
    if (a->inf) return false; // infinity point is always true
    if (LT_BN_Compare_Unsigned(kP256_P, a->x, LTSYSTEMCRYPTO_U32_PER_U256) <= 0) return false;
    if (LT_BN_Compare_Unsigned(kP256_P, a->y, LTSYSTEMCRYPTO_U32_PER_U256) <= 0) return false;
    // (y*y) % P == (x*x*x + a * x + b) % P
    // l = (y*y) % P
    LT_P256_Multiply_ModP(tmp->l, a->y, a->y, &tmp->ml);
    // r = (((x * x) + a) * x) + b) % P
    LT_P256_Multiply_ModP(tmp->r, a->x, a->x, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->r, tmp->r, kP256_a, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    LT_P256_Multiply_ModP(tmp->r, tmp->r, a->x, &tmp->ml);
    LT_BN_Add_Mod_Unsigned(tmp->r, tmp->r, kP256_b, kP256_P, LTSYSTEMCRYPTO_U32_PER_U256);
    return (LT_BN_Compare_Unsigned(tmp->l, tmp->r, LTSYSTEMCRYPTO_U32_PER_U256) == 0);
}

/**
 * @brief Check if a is on curve, partial public key validation
 *        NIST SP 800-186, D.1.1.2
 *
 * @param a   the curve point to check
 * @param tmp a temporary buffer for computation
 * @return true
 * @return false
 */
bool LT_P256_Validate_Full(const P256Point *a, struct P256ValFLocal *tmp) {
    if (!LT_P256_Validate_Partial(a, &tmp->pl)) return false;
    LT_P256_Multiply_Normal(&tmp->v, LT_GetP256_N(), a, &tmp->sl);
    return tmp->v.inf;
}

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
void LT_P256_Inverse_ModN_Mont(u256 res, const u256 a, struct P256InvLocal *tmp) {
    // map to mont space
    lt_memcpy(tmp->r0, kP256_MN1, LTSYSTEMCRYPTO_BYTES_PER_U256);              // r0 = 1 * R % N
    LT_BN_Multiply_Mont(tmp->r1, a, LT_GetP256_RN2(), LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);  // r1 = a * R % N

    int m = 0;
    // bit[255...224] = 1
    for (m = 255; m >= 224; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
    }
    // bit[223...192] = 0
    for (m = 223; m >= 192; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
    }
    // bit[191...128] = 1
    for (m = 191; m >= 128; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
    }
    // bit[127...5]
    for (m = 127; m >= 5; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
        if (1 == LT_BN_Get_Bit(LT_GetP256_N(), m, LTSYSTEMCRYPTO_U32_PER_U256)) {
            LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
        }
    }
    // bit[4] = 0
    LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
    // bit[3...0] = 1
    for (m = 3; m >= 0; --m) {
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r0, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
        LT_BN_Multiply_Mont(tmp->r0, tmp->r0, tmp->r1, LT_GetP256_N(), LT_GetP256_N1(), &tmp->mml);
    }

    // map back to normal space
    lt_memcpy(tmp->mml.t, tmp->r0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    lt_memset(tmp->mml.t + LTSYSTEMCRYPTO_U32_PER_U256, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Redux_Mont(res, tmp->mml.t, LT_GetP256_N(), LT_GetP256_N1());
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  03-Apr-23   gallienus   created
 */

#if 0
// This inversion algorithm is slower than Montgomery reduction.
void LT_P256_Inverse_ModP(u256 res, const u256 a, struct InvLocal *tmp) {
    // Montgomery ladder, multiplication
    int m = 0;
    // bit[255] = 1
    lt_memcpy(tmp->r0, a, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_P256_Multiply_ModP(tmp->r1, tmp->r0, tmp->r0, &tmp->ml);
    // bit[254...224] = 1
    for (m = 254; m >= 224; --m) {
        LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r1, &tmp->ml);
        LT_P256_Multiply_ModP(tmp->r1, tmp->r1, tmp->r1, &tmp->ml);
    }
    // bit[223...193] = 0
    for (m = 223; m >= 193; --m) {
        LT_P256_Multiply_ModP(tmp->r1, tmp->r0, tmp->r1, &tmp->ml);
        LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r0, &tmp->ml);
    }
    // bit[192] = 1
    LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r1, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->r1, tmp->r1, tmp->r1, &tmp->ml);
    // bit[191...96] = 0
    for (m = 191; m >= 96; --m) {
        LT_P256_Multiply_ModP(tmp->r1, tmp->r0, tmp->r1, &tmp->ml);
        LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r0, &tmp->ml);
    }
    // bit[95...2] = 1
    for (m = 95; m >= 2; --m) {
        LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r1, &tmp->ml);
        LT_P256_Multiply_ModP(tmp->r1, tmp->r1, tmp->r1, &tmp->ml);
    }
    // bit[1] = 0
    LT_P256_Multiply_ModP(tmp->r1, tmp->r0, tmp->r1, &tmp->ml);
    LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r0, &tmp->ml);
    // bit[0] = 1
    LT_P256_Multiply_ModP(tmp->r0, tmp->r0, tmp->r1, &tmp->ml);
    // LT_P256_Multiply_ModP(tmp->r1, tmp->r1, tmp->r1, &tmp->ml);
    lt_memcpy(res, tmp->r0, LTSYSTEMCRYPTO_BYTES_PER_U256);
}
#endif