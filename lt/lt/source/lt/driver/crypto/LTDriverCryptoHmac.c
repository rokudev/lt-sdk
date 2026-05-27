/*******************************************************************************
 * lt/source/lt/driver/crypto/LTDriverCryptoHmac.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

// NIST FIPS 198-1, https://csrc.nist.gov/publications/detail/fips/198/1/final

#include <lt/LT.h>
#include "LTDriverCrypto.h"

/**
 * @brief  Check all pointers in context.
 *
 * @param  hmacCtx  the context
 * @return true if all pointers are valid, NOT null
 * @return false if any pointer is null
 */
static bool HmacCtxValid(LT_HMAC_CTX_Impl *hmacCtx) {
    return hmacCtx && hmacCtx->pad && hmacCtx->hashCtx && hmacCtx->HashInit &&
           hmacCtx->HashUpdate && hmacCtx->HashFinish && hmacCtx->HashDigest;
}

/**
 * @brief  Initialize HMAC context
 * @param  hmacCtx  the context
 * @param  key      the key
 * @param  keyLen   the length of key
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Init(LT_HMAC_CTX_Impl *hmacCtx, const u8 *key, LT_SIZE keyLen) {
    // sanity check
    if (!HmacCtxValid(hmacCtx) || !key) {
        return kLTSystemCrypto_Result_Null;
    }

    // 2.3, ipad = 0x36, opad = 0x5C. opad not set here
    lt_memset(hmacCtx->pad, 0x36, hmacCtx->blockLen);

    // step 1-3
    u32 *hmacKey = lt_malloc(hmacCtx->blockLen);
    if (!hmacKey) return kLTSystemCrypto_Result_OOM;

    lt_memset(hmacKey, 0, hmacCtx->blockLen);
    if (keyLen > hmacCtx->blockLen) {
        hmacCtx->HashDigest(key, keyLen, (u8 *)hmacKey);
    } else {
        lt_memcpy(hmacKey, key, keyLen);
    }

    // step 4, key ^ ipad, cast to u32 to xor
    for (LT_SIZE i = 0; i < (hmacCtx->blockLen >> 2); ++i) {
        hmacCtx->pad[i] ^= hmacKey[i];
    }

    // step 5
    hmacCtx->HashInit(hmacCtx->hashCtx);
    hmacCtx->HashUpdate(hmacCtx->hashCtx, (u8 *)hmacCtx->pad, hmacCtx->blockLen);

    lt_memset(hmacKey, 0, hmacCtx->blockLen);
    lt_free(hmacKey);
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hmac input data and update internal state
 * @param  hmacCtx  the context
 * @param  data     the input data
 * @param  len      the length of data
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Update(LT_HMAC_CTX_Impl *hmacCtx, const u8 *data, LT_SIZE len) {
    // sanity check
    if(!HmacCtxValid(hmacCtx) || !data) {
        return kLTSystemCrypto_Result_Null;
    }

    // step 6
    return hmacCtx->HashUpdate(hmacCtx->hashCtx, data, len);
}

/**
 * @brief  Finish hash and get the hmac
 * @param  hmacCtx the context
 * @param  hmac    the output hmac
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Finish(LT_HMAC_CTX_Impl *hmacCtx, u8 *hmac) {
    // sanity check
    if (!HmacCtxValid(hmacCtx) || !hmac) return kLTSystemCrypto_Result_Null;

    u8 *digest = lt_malloc(hmacCtx->digestLen);
    if (!digest) return kLTSystemCrypto_Result_OOM;

    // step 7, opad ^ key, opad = 0x5C
    for (LT_SIZE i = 0; i < (hmacCtx->blockLen >> 2); ++i) {
        hmacCtx->pad[i] ^= (0x36363636 ^ 0x5C5C5C5C);
    }

    // step 8
    hmacCtx->HashFinish(hmacCtx->hashCtx, digest);

    // step 9
    hmacCtx->HashInit(hmacCtx->hashCtx);
    hmacCtx->HashUpdate(hmacCtx->hashCtx, (u8 *)hmacCtx->pad, hmacCtx->blockLen);
    hmacCtx->HashUpdate(hmacCtx->hashCtx, digest, hmacCtx->digestLen);
    hmacCtx->HashFinish(hmacCtx->hashCtx, hmac);

    lt_memset(digest, 0, hmacCtx->digestLen);
    lt_free(digest);
    lt_memset(hmacCtx->pad, 0, hmacCtx->blockLen);
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief  Hmac data and get the hmac
 * @param  hmacCtx the context
 * @param  key     the key
 * @param  keyLen  the length of key
 * @param  data    the input data block
 * @param  len     the length of data
 * @param  hmac    the output hmac
 * @return result code
 */
LTSystemCryptoResult LT_HMAC_Digest(LT_HMAC_CTX_Impl *hmacCtx, const u8 *key, LT_SIZE keyLen, const u8 *data, LT_SIZE len, u8 *hmac) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;

    if (kLTSystemCrypto_Result_Ok == (ret = LT_HMAC_Init(hmacCtx, key, keyLen)) &&
        kLTSystemCrypto_Result_Ok == (ret = LT_HMAC_Update(hmacCtx, data, len)) &&
        kLTSystemCrypto_Result_Ok == (ret = LT_HMAC_Finish(hmacCtx, hmac))) {
        ;
    }

    lt_memset(hmacCtx->pad, 0, hmacCtx->blockLen);
    return ret;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Feb-22   gallienus   created
 */
