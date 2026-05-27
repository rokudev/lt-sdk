/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/Esp32DriverCryptoECDHE.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

/********************************************************************************
IETF RFC 7748, https://datatracker.ietf.org/doc/html/rfc7748

Section 4.1.  Curve25519

v^2 = u^3 + A*u^2 + u % p
p  2^255 - 19
A  486662
order  2^252 + 0x14def9dea2f79cd65812631a5cf5d3ed
cofactor  8
The base point is u = 9, v = 14781619447589544791020593568409986887264606134616475288964881837755586237401

Section 5.
   def decodeScalar25519(k):
       k_list = [ord(b) for b in k]
       k_list[0] &= 248
       k_list[31] &= 127
       k_list[31] |= 64
       return decodeLittleEndian(k_list, 255)

All calculations are performed in GF(p), i.e., they are performed modulo p.
The constant a24 is (486662 - 2) / 4 = 121665 for curve25519/X25519

X25519(k, u) :
    x_1 = u
    x_2 = 1
    z_2 = 0
    x_3 = u
    z_3 = 1
    swap = 0

    For t = bits-1 down to 0:
        k_t = (k >> t) & 1
        swap ^= k_t
        // Conditional swap; see text below.
        (x_2, x_3) = cswap(swap, x_2, x_3)
        (z_2, z_3) = cswap(swap, z_2, z_3)
        swap = k_t

        A = x_2 + z_2
        AA = A^2
        B = x_2 - z_2
        BB = B^2
        E = AA - BB
        C = x_3 + z_3
        D = x_3 - z_3
        DA = D * A
        CB = C * B
        x_3 = (DA + CB)^2
        z_3 = x_1 * (DA - CB)^2
        x_2 = AA * BB
        z_2 = E * (AA + a24 * E)

    // Conditional swap; see text below.
    (x_2, x_3) = cswap(swap, x_2, x_3)
    (z_2, z_3) = cswap(swap, z_2, z_3)
    Return x_2 * (z_2^(p - 2))

cswap(swap, x_2, x_3):
    dummy = mask(swap) AND (x_2 XOR x_3)
    x_2 = x_2 XOR dummy
    x_3 = x_3 XOR dummy
    Return (x_2, x_3)

Where mask(swap) is the all-1 or all-0 word of the same length as x_2
and x_3, computed, e.g., as mask(swap) = 0 - swap.
*/

#include <lt/core/LTStdlib.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "Esp32DriverCrypto.h"
#include "Esp32DriverCryptoBigNum.h"
#include "Esp32DriverCrypto25519.h"

/* X25519 base point (generator), needed in key exchange protocols (TLS and certificate provision) */
static const u32 kX25519_BP[LTSYSTEMCRYPTO_U32_PER_U256] = {0x00000009,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
/* X25519_A24 = 121665 = 0x1DB41 */
static const u32 kX25519_A24[LTSYSTEMCRYPTO_U32_PER_U256] = {0x0001DB41,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};

/**
 * @brief  Multiple a scalar and a X25519 point
 *
 * @param res  the resulting X25519 point
 * @param k    the scalar, must already be modular over L, i.e. k < L
 * @param u    the X25519 point
 * @param tmp  a temporary buffer for computation
 */
static void ESP32_X_Multiply(const ESP32_RSA_CTX *ctx, u256 res, const u256 k, const u256 u, struct Esp32C25519XMulLocal *tmp) {
    // x_1 = u
    lt_memcpy(tmp->x1, u, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // x_2 = 1
    tmp->X2 = tmp->x2;
    lt_memset(tmp->X2, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->X2[0] = 1;
    // z_2 = 0
    tmp->Z2 = tmp->z2;
    lt_memset(tmp->Z2, 0, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // x_3 = u
    tmp->X3 = tmp->x3;
    lt_memcpy(tmp->X3, tmp->x1, LTSYSTEMCRYPTO_BYTES_PER_U256);
    // z_3 = 1
    tmp->Z3 = tmp->z3;
    lt_memcpy(tmp->Z3, tmp->X2, LTSYSTEMCRYPTO_BYTES_PER_U256);
    //swap = 0
    u8 swap = 0;

    int kt = 0;
    tmp->Tmp2 = NULL;  // for constant time conditional swap
    tmp->Tmp3 = NULL;
    // for t in range(254, -1, -1):
    //     k_t = (k >> t) & 1
    for (int i = 254; i >= 0 ; --i) {
        kt = ESP32_BN_Get_Bit(k, i, LTSYSTEMCRYPTO_U32_PER_U256);
        // swap ^= k_t
        swap ^= kt;
        // (x_2, x_3) = cswap(swap, x_2, x_3)
        if (swap) {
            tmp->Tmp2 = tmp->X3;
            tmp->Tmp3 = tmp->X2;
        } else {
            tmp->Tmp2 = tmp->X2;
            tmp->Tmp3 = tmp->X3;
        }
        tmp->X2 = tmp->Tmp2;
        tmp->X3 = tmp->Tmp3;
        // (z_2, z_3) = cswap(swap, z_2, z_3)
        if (swap) {
            tmp->Tmp2 = tmp->Z3;
            tmp->Tmp3 = tmp->Z2;
        } else {
            tmp->Tmp2 = tmp->Z2;
            tmp->Tmp3 = tmp->Z3;
        }
        tmp->Z2 = tmp->Tmp2;
        tmp->Z3 = tmp->Tmp3;
        // swap = k_t
        swap = kt;

        // A = (x_2 + z_2) % p
        ESP32_BN_Add_Mod_Unsigned(tmp->A, tmp->X2, tmp->Z2, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // B = (x_2 - z_2) % p
        ESP32_BN_Subtract_Mod_Unsigned(tmp->B, tmp->X2, tmp->Z2, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // C = (x_3 + z_3) % p
        ESP32_BN_Add_Mod_Unsigned(tmp->C, tmp->X3, tmp->Z3, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // D = (x_3 - z_3) % p
        ESP32_BN_Subtract_Mod_Unsigned(tmp->D, tmp->X3, tmp->Z3, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // DA = (D * A) % p
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->D, tmp->D, tmp->A);
        // CB = (C * B) % p
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->C, tmp->C, tmp->B);
        // x_3 = ((DA + CB) * (DA + CB)) % p
        ESP32_BN_Add_Mod_Unsigned(tmp->X3, tmp->D, tmp->C, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->X3, tmp->X3, tmp->X3);
        // z_3 = (x_1 * (DA - CB) * (DA - CB)) % p
        ESP32_BN_Subtract_Mod_Unsigned(tmp->Z3, tmp->D, tmp->C, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->Z3, tmp->Z3, tmp->Z3);
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->Z3, tmp->Z3, tmp->x1);

        // AA = (A * A) % p         (A = (A * A) % p )
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->A, tmp->A, tmp->A);
        // BB = (B * B) % p         (B = (B * B) % p)
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->B, tmp->B, tmp->B);
        // x_2 = (AA * BB) % p      (x_2 = (A * B) % p)
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->X2, tmp->A, tmp->B);
        // E = (AA - BB) % p        (C = (A - B) % p)
        ESP32_BN_Subtract_Mod_Unsigned(tmp->C, tmp->A, tmp->B, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // z_2 = (E * (AA + a24 * E)) % p       (z_2 = (C * (A + a24 * C)) % p)
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->Z2, kX25519_A24, tmp->C);
        ESP32_BN_Add_Mod_Unsigned(tmp->Z2, tmp->A, tmp->Z2, Esp32_GetC25519_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        ESP32_BN_Multiply_Mod_Preset(ctx, tmp->Z2, tmp->C, tmp->Z2);
    }

    // (x_2, x_3) = cswap(swap, x_2, x_3)
    tmp->X2 = swap ? tmp->X3 : tmp->X2;
    // (z_2, z_3) = cswap(swap, z_2, z_3)
    tmp->Z2 = swap ? tmp->Z3 : tmp->Z2;

    // return (x_2 * pow(z_2, (p - 2), p)) % p
    ESP32_BN_Pow_Mod_Preset(ctx, res, tmp->Z2, Esp32_GetC25519_N2());
    ESP32_BN_Multiply_Mod_Preset(ctx, res, tmp->X2, res);

    lt_memset(&tmp, 0, sizeof(tmp));
}

/**
 * @brief  Generate a ECDHE_X25519 key
 *
 * @param k    the scalar
 * @param u    the X25519 point
 * @param key  the resulting key or the resulting X25519 point
 * @return LTSystemCryptoResult
 */
static LTSystemCryptoResult ESP32_Ecdhe(const u8 k[ECDHE_KEY_LENGTH], const u8 u[ECDHE_KEY_LENGTH], u8 key[ECDHE_KEY_LENGTH]) {
    if (!k || !u || !key) {
        return kLTSystemCrypto_Result_Null;
    }
    struct EcdheLocal {
        u256 scalar;
        u256 result;
        struct Esp32C25519XMulLocal xb;
        ESP32_RSA_CTX ctx;
    };
    struct EcdheLocal *tmp = lt_malloc(sizeof(struct EcdheLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;

    lt_memcpy(tmp->scalar, k, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->scalar[0] &= 0xFFFFFFF8;
    tmp->scalar[7] &= 0x7FFFFFFF;
    tmp->scalar[7] |= 0x40000000;

    lt_memcpy(tmp->result, u, LTSYSTEMCRYPTO_BYTES_PER_U256);
    tmp->result[7] &= 0x7FFFFFFF;

    ESP32_BN_Set_Mod(&tmp->ctx, Esp32_GetC25519_N(), Esp32_GetC25519_R2(), LTSYSTEMCRYPTO_U32_PER_U256, Esp32_GetC25519_N1(), LTSYSTEMCRYPTO_RSA512LEN);
    ESP32_X_Multiply(&tmp->ctx, tmp->result, tmp->scalar, tmp->result, &tmp->xb);
    ESP32_BN_ClearRSA(LTSYSTEMCRYPTO_RSA512LEN);
    lt_memcpy(key, tmp->result, LTSYSTEMCRYPTO_BYTES_PER_U256);

    lt_memset(tmp, 0, sizeof(struct EcdheLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

typedef_LTObjectImpl(LTDriverCryptoX25519, LTHardwareCryptoX25519) {
} LTOBJECT_API;

static LTSystemCryptoResult LTHardwareCryptoX25519_GenKey(const u8 k[ECDHE_KEY_LENGTH], const u8 u[ECDHE_KEY_LENGTH], u8 key[ECDHE_KEY_LENGTH]) {
    Esp32DriverCrypto_LockShaMutex();
    ESP32_RSA_Enable();
    LTSystemCryptoResult ret = ESP32_Ecdhe(k, u, key);
    ESP32_RSA_Disable();
    Esp32DriverCrypto_UnlockShaMutex();
    return ret;
}

static const u32* LTHardwareCryptoX25519_GetBasePoint(void) {
    return kX25519_BP;
}

static void LTHardwareCryptoX25519_DestructObject(LTHardwareCryptoX25519 *instance) {
    LT_UNUSED(instance);
}

static bool LTHardwareCryptoX25519_ConstructObject(LTHardwareCryptoX25519 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoX25519, LTHardwareCryptoX25519,
    GenKey,
    GetBasePoint,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   gallienus   created
 */
