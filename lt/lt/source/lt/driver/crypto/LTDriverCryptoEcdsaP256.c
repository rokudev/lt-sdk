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
 NIST FIPS 186-5, https://csrc.nist.gov/publications/detail/fips/186/5/final
 IETF RFC 6979,   https://www.rfc-editor.org/rfc/rfc6979
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include "LTDriverCryptoP256.h"

/* kP256_G, P256 generator */
static const P256Point kP256_G = {{0xd898c296,0xf4a13945,0x2deb33a0,0x77037d81,0x63a440f2,0xf8bce6e5,0xe12c4247,0x6b17d1f2},
                                  {0x37bf51f5,0xcbb64068,0x6b315ece,0x2bce3357,0x7c0f9e16,0x8ee7eb4a,0xfe1a7f9b,0x4fe342e2},
                                  false};

// big endian numbers
typedef struct LT_Ecdsa_Ctx {
    u32 priKey[ECDSA_P256_PRIVATEKEY_LENGTH / 4];
    bool bInited;
} LT_Ecdsa_Ctx;

static void GenMsgRandom(u256 res, const u32 priKey[ECDSA_P256_PRIVATEKEY_LENGTH / 4], const u32 H[SHA256_HASH_LENGTH / 4], struct RndLocal *tmp) {
    tmp->hmacCtx = (LT_HMAC_CTX_Impl){
        .pad        = tmp->hmacHashCtx.pad,
        .hashCtx    = &tmp->hmacHashCtx.hashCtx,
        .blockLen   = SHA256_BLOCK_LENGTH,
        .digestLen  = SHA256_HASH_LENGTH,
        .HashInit   = (LT_Hash_Init)LT_SHA256_Init,
        .HashUpdate = (LT_Hash_Update)LT_SHA256_Update,
        .HashFinish = (LT_Hash_Finish)LT_SHA256_Finish,
        .HashDigest = (LT_Hash_Digest)LT_SHA256_Digest,
    };

    // Generally follow the process in A.3.3
    // But, convert integer to little-endian bytes and simplify 4
    // 1.1, nothing to do
    // 1.2
    LT_BN_Mod_Unsigned(tmp->H, H, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    // 1.3, nothing to do
    // 1.4
    lt_memset(tmp->K, 0, SHA256_HASH_LENGTH);
    // 1.5, nothing to do
    lt_memset(tmp->V, 1, SHA256_HASH_LENGTH);
    // 1.6, K = HMAC(K, V || 0x00 || d || H)
    LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
    u8 o = 0;
    LT_HMAC_Update(&tmp->hmacCtx, &o, 1);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)priKey, ECDSA_P256_PRIVATEKEY_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->H, SHA256_HASH_LENGTH);
    LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->K);
    // 1.7, V = HMAC(K, V)
    LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
    LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->V);
    // 1.8, K = HMAC(K, V || 0x01 || d || H)
    LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
    o = 1;
    LT_HMAC_Update(&tmp->hmacCtx, &o, 1);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)priKey, ECDSA_P256_PRIVATEKEY_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->H, SHA256_HASH_LENGTH);
    LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->K);
    // 1.9, V = HMAC(K, V)
    LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
    LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
    LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->V);
    // 2, 3, nothing to do
    // 4, simplified
    do {
        // 4.5, K = HMAC(K, V || 0x02).
        LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
        LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
        ++o;
        LT_HMAC_Update(&tmp->hmacCtx, &o, 1);
        LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->K);
        // 4.6, V = HMAC(K, V)
        LT_HMAC_Init(&tmp->hmacCtx, (u8 *)tmp->K, SHA256_HASH_LENGTH);
        LT_HMAC_Update(&tmp->hmacCtx, (u8 *)tmp->V, SHA256_HASH_LENGTH);
        LT_HMAC_Finish(&tmp->hmacCtx, (u8 *)tmp->V);
        // 4.4, k = V % N
        LT_BN_Mod_Unsigned(res, tmp->V, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256);
    } while (LT_BN_IsZero(res, LTSYSTEMCRYPTO_U32_PER_U256));
}

/**
 * @brief
 *
 * @param priKey  the private key, a big endian number
 * @param pubKey  the public key, a curve point with x, y coordinates as big endian numbers
 * @param tmp
 * @return true   if the resulting curve point of the pubKey is a valid point, not inf.
 * @return false
 */
static bool GenPublicKey(const u8 priKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 pubKey[ECDSA_P256_PUBLICKEY_LENGTH], struct PubLocal *tmp) {
    LT_BN_Copy_B2L((u8 *)tmp->d, ECDSA_P256_PRIVATEKEY_LENGTH, priKey, ECDSA_P256_PRIVATEKEY_LENGTH);
    if (LT_BN_IsZero(tmp->d, LTSYSTEMCRYPTO_U32_PER_U256)) return false;
    LT_P256_Multiply_Normal(&tmp->u, tmp->d, &kP256_G, &tmp->sl);
    LT_BN_Copy_L2B(pubKey, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->u.x, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Copy_L2B(pubKey + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->u.y, LTSYSTEMCRYPTO_BYTES_PER_U256);
    return !tmp->u.inf;
}

/**
 * @brief  Generate public key from private key
 * @param  privateKey    the private key, 32 Bytes
 * @param  pubKey        the output public key, 64 Bytes
 * @return result code
 */
LTSystemCryptoResult LT_Ecdsa_GenPublicKey(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    if (!privateKey || !publicKey) return kLTSystemCrypto_Result_Null;
    struct PubLocal *pl = lt_malloc(sizeof(struct PubLocal));
    if (!pl) return kLTSystemCrypto_Result_OOM;
    LTSystemCryptoResult ret = GenPublicKey(privateKey, publicKey, pl) ? kLTSystemCrypto_Result_Ok : kLTSystemCrypto_Result_InvalidPoint;
    lt_memset(pl, 0, sizeof(struct PubLocal));
    lt_free(pl);
    return ret;
}

/**
 * @brief  Initialize the ECDSA context
 * @param  ctx           the context
 * @param  publicKey     the output public key, 64 Bytes
 * @param  privateKey    the private key, 32 Bytes
 * @return result code
 * @note    publicKey could be NULL if no need to compute publicKey.
 */
LTSystemCryptoResult LT_Ecdsa_Init(LT_Ecdsa_Ctx *ctx, u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH], const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH]) {
    if (!ctx || !privateKey) return kLTSystemCrypto_Result_Null;
    // compute public key only if asked. The public key is not required to generate signature.
    if (publicKey) {
        LTSystemCryptoResult ret = LT_Ecdsa_GenPublicKey(privateKey, publicKey);
        if (kLTSystemCrypto_Result_Ok != ret) return ret;
    }
    lt_memcpy(ctx->priKey, privateKey, ECDSA_P256_PRIVATEKEY_LENGTH);
    ctx->bInited = true;
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief ECDSA signature generation, FIPS 186-5 Section 6.4.1
 *        Use deterministic per-message random number.
 *
 * @param  ctx           the context
 * @param  hash          the hash of data to sign, only support SHA256
 * @param  signature     the output signature
 * @return result code
 */
LTSystemCryptoResult LT_Ecdsa_SignHash(LT_Ecdsa_Ctx *ctx, const u8 hash[SHA256_HASH_LENGTH], u8 signature[ECDSA_P256_SIGNATURE_LENGTH]) {
    if (!ctx || !ctx->bInited || !hash || !signature) {
        return kLTSystemCrypto_Result_Null;
    }

    struct SignLocal {
        u32 H[SHA256_HASH_LENGTH / 4];
        u256 e;                     // little endian hash of message
        P256Point R;
        u256 k;
        u256 d;                     // little endian private key
        union {
            struct RndLocal rl;
            struct P256ScaLocal sl;
            struct P256InvLocal il;
            struct MtMulLocal ml;
            struct P256ValPLocal vpl;
        };
    };
    struct SignLocal *tmp = lt_malloc(sizeof(struct SignLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;
    lt_memcpy(tmp->H, hash, SHA256_HASH_LENGTH);

    // step 2, e = H(m)
    LT_BN_Copy_B2L((u8 *)tmp->e, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->H, LTSYSTEMCRYPTO_BYTES_PER_U256);
    while (true) { // loop ends when signature is not 0.
        // step 3
        GenMsgRandom(tmp->k, ctx->priKey, tmp->H, &tmp->rl);
        // step 5, R = k * G
        LT_P256_Multiply_Const(&tmp->R, tmp->k, &kP256_G, &tmp->sl);
        // step 4, k^-1 % N
        LT_P256_Inverse_ModN_Mont(tmp->k, tmp->k, &tmp->il);
        // step 6, 7, 8, r = Rx % N
        LT_BN_Mod_Unsigned(tmp->R.x, tmp->R.x, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        // step 9, s = k^-1 * (e + r * d) % n
        LT_BN_Copy_B2L((u8 *)tmp->d, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)ctx->priKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_BN_Multiply_Mont(tmp->d, tmp->d, tmp->R.x, LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Multiply_Mont(tmp->d, tmp->d, LT_GetP256_RN2(), LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Add_Mod_Unsigned(tmp->d, tmp->d, tmp->e, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        LT_BN_Multiply_Mont(tmp->d, tmp->d, tmp->k, LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Multiply_Mont(tmp->d, tmp->d, LT_GetP256_RN2(), LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        // step 11, if r==0 or s == 0, generate another k in a deterministic way by incrementing H
        if (LT_BN_IsZero(tmp->R.x, LTSYSTEMCRYPTO_BYTES_PER_U256) || LT_BN_IsZero(tmp->d, LTSYSTEMCRYPTO_BYTES_PER_U256)) {
            LT_BN_Increment_Unsigned(tmp->H, LTSYSTEMCRYPTO_U32_PER_U256);
        } else {
            break;
        }
    }
    // step 12, return r, s
    LT_BN_Copy_L2B(signature, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->R.x, LTSYSTEMCRYPTO_BYTES_PER_U256);
    LT_BN_Copy_L2B(signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->d, LTSYSTEMCRYPTO_BYTES_PER_U256);

    lt_memset(tmp, 0, sizeof(struct SignLocal));
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Verify a signature
 *
 * @param  hash          the hash of data to verify, only support SHA256
 * @param  signature     the signature to verify
 * @param  publicKey     the public key for verifying signature
 * @return result code
 */
LTSystemCryptoResult LT_Ecdsa_VerifyHash(const u8 hash[SHA256_HASH_LENGTH], const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    // sanity check
    if (!publicKey || !signature || !hash) {
        return kLTSystemCrypto_Result_Null;
    }

    struct VerifyLocal {
        u32 H[SHA256_HASH_LENGTH / 4];
        u256 e;                     // little endian hash of message
        P256Point R;
        P256Point Q;                // curve point of public key
        u256 r;
        u256 s;                     // little endian private key
        union {
            struct P256ScaLocal sl;
            struct P256InvLocal il;
            struct MtMulLocal ml;
            struct P256ValPLocal vpl;
        };
    };
    struct VerifyLocal *tmp = lt_malloc(sizeof(struct VerifyLocal));
    if (!tmp) return kLTSystemCrypto_Result_OOM;
    lt_memcpy(tmp->H, hash, SHA256_HASH_LENGTH);

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        // 0 partial key validation
        LT_BN_Copy_B2L((u8 *)tmp->Q.x, LTSYSTEMCRYPTO_BYTES_PER_U256, publicKey, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_BN_Copy_B2L((u8 *)tmp->Q.y, LTSYSTEMCRYPTO_BYTES_PER_U256, publicKey + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        tmp->Q.inf = false;
        if (!LT_P256_Validate_Partial(&tmp->Q, &tmp->vpl)) {
            ret = kLTSystemCrypto_Result_InvalidPoint;
            break;
        }
        // 1
        LT_BN_Copy_B2L((u8 *)tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256, signature, LTSYSTEMCRYPTO_BYTES_PER_U256);
        LT_BN_Copy_B2L((u8 *)tmp->s, LTSYSTEMCRYPTO_BYTES_PER_U256, signature + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256);
        if (LT_BN_IsZero(tmp->r, LTSYSTEMCRYPTO_BYTES_PER_U256) ||
            LT_BN_Compare_Unsigned(tmp->r, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256) >= 0 ||
            LT_BN_IsZero(tmp->s, LTSYSTEMCRYPTO_BYTES_PER_U256) ||
            LT_BN_Compare_Unsigned(tmp->s, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256) >= 0) {
            return kLTSystemCrypto_Result_WrongVerification;
        }
        // 3, e = H(m)
        LT_BN_Copy_B2L((u8 *)tmp->e, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)tmp->H, LTSYSTEMCRYPTO_BYTES_PER_U256);
        // 4, s^-1 % N
        LT_P256_Inverse_ModN_Mont(tmp->s, tmp->s, &tmp->il);
        // 5, u = e * s^-1 % N, v = r * s^-1 % N
        LT_BN_Multiply_Mont(tmp->e, tmp->e, tmp->s, LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Multiply_Mont(tmp->e, tmp->e, LT_GetP256_RN2(), LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Multiply_Mont(tmp->s, tmp->r, tmp->s, LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        LT_BN_Multiply_Mont(tmp->s, tmp->s, LT_GetP256_RN2(), LT_GetP256_N(), LT_GetP256_N1(), &tmp->ml);
        // 6, R1 = uG + vQ
        LT_P256_Multiply_Add(&tmp->R, tmp->e, &kP256_G, tmp->s, &tmp->Q, &tmp->sl);
        if (tmp->R.inf) {
            ret = kLTSystemCrypto_Result_InvalidPoint;
            break;
        }
        // 7, 8, 9, r == r1
        LT_BN_Mod_Unsigned(tmp->R.x, tmp->R.x, LT_GetP256_N(), LTSYSTEMCRYPTO_U32_PER_U256);
        ret = (LT_BN_Compare_Unsigned(tmp->r, tmp->R.x, LTSYSTEMCRYPTO_U32_PER_U256) == 0) ? kLTSystemCrypto_Result_Ok : kLTSystemCrypto_Result_WrongVerification;
    } while (0);

    lt_memset(tmp, 0, sizeof(struct VerifyLocal));
    lt_free(tmp);
    return ret;
}

typedef_LTObjectImpl(LTDriverCryptoEcdsaP256, LTSoftwareCryptoEcdsaP256) {
} LTOBJECT_API;

static LTSystemCryptoResult LTSoftwareCryptoEcdsaP256_GenPublicKey(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    return LT_Ecdsa_GenPublicKey(privateKey, publicKey);
}

static LTSystemCryptoResult LTSoftwareCryptoEcdsaP256_SignHash(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 hash[SHA256_HASH_LENGTH], u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    LT_Ecdsa_Ctx *ecdsaCtx = lt_malloc(sizeof(LT_Ecdsa_Ctx));
    if (!ecdsaCtx) return kLTSystemCrypto_Result_OOM;

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    do {
        ecdsaCtx->bInited = false;
        if (kLTSystemCrypto_Result_Ok != (ret = LT_Ecdsa_Init(ecdsaCtx, publicKey, privateKey))) {
            break;
        }
        ret = LT_Ecdsa_SignHash(ecdsaCtx, hash, signature);
    } while (false);

    lt_memset(ecdsaCtx, 0, sizeof(LT_Ecdsa_Ctx));
    lt_free(ecdsaCtx);
    return ret;
}

static LTSystemCryptoResult LTSoftwareCryptoEcdsaP256_Sign(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], const u8 * data, LT_SIZE dataLen, u8 signature[ECDSA_P256_SIGNATURE_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    u32 hash[SHA256_HASH_LENGTH / 4];
    if (kLTSystemCrypto_Result_Ok != LT_SHA256_Digest(data, dataLen, (u8 *)hash)) return kLTSystemCrypto_Result_Error;
    return LTSoftwareCryptoEcdsaP256_SignHash(privateKey, (u8 *)hash, signature, publicKey);
}

static LTSystemCryptoResult LTSoftwareCryptoEcdsaP256_VerifyHash(const u8 hash[SHA256_HASH_LENGTH], const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    return LT_Ecdsa_VerifyHash(hash, signature, publicKey);
}

static LTSystemCryptoResult LTSoftwareCryptoEcdsaP256_Verify(const u8 *data, LT_SIZE dataLen, const u8 signature[ECDSA_P256_SIGNATURE_LENGTH], const u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]) {
    u32 hash[SHA256_HASH_LENGTH / 4];
    if (kLTSystemCrypto_Result_Ok != LT_SHA256_Digest(data, dataLen, (u8 *)hash)) return kLTSystemCrypto_Result_Error;
    return LTSoftwareCryptoEcdsaP256_VerifyHash((u8 *)hash, signature, publicKey);
}

static void LTSoftwareCryptoEcdsaP256_DestructObject(LTSoftwareCryptoEcdsaP256 *instance) {
    LT_UNUSED(instance);
}

static bool LTSoftwareCryptoEcdsaP256_ConstructObject(LTSoftwareCryptoEcdsaP256 *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEcdsaP256, LTSoftwareCryptoEcdsaP256,
    GenPublicKey,
    SignHash,
    Sign,
    VerifyHash,
    Verify,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  07-Apr-23   gallienus   created
 */
