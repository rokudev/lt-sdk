/*******************************************************************************
 *
 * LTNetDtls.c - Implementation of DTLS protocol interface
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/core/LTCore.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "mbedtls/gcm_alt.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256_alt.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"

static LTSystemCrypto     *s_Crypto = NULL;

DEFINE_LTLOG_SECTION("netdtls");

#if defined(MBEDTLS_GCM_ALT)

void mbedtls_gcm_init(mbedtls_gcm_context *ctx) {
    if (!ctx) return;
    ctx->key = NULL;
}

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits) {
    u32 keyBytes = keybits / 8;
    if (!ctx || (keyBytes != AES128_KEY_LENGTH)) {
        LTLOG_YELLOWALERT("gcm.set.key.error", "key length %d bits", keybits);
        return -1;
    }
    if (cipher == MBEDTLS_CIPHER_ID_AES) {
        ctx->key = lt_memdup(key, keyBytes);
    }
    return 0;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                              int mode,
                              size_t length,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *add,
                              size_t add_len,
                              const unsigned char *input,
                              unsigned char *output,
                              size_t tag_len,
                              unsigned char *tag) {
    if (!ctx || !ctx->key || !input || !output || iv_len != AES128_GCM_IV_LENGTH) {
        LTLOG_YELLOWALERT("gcm.encrypt", "not ready");
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    int ret = 0;
    if (mode == MBEDTLS_GCM_ENCRYPT) {
        ret = s_Crypto->EncryptAES128GCM((u8 *)ctx->key, iv, add, add_len, input, length, output, tag, tag_len);
    }
    if (ret) {
        LTLOG_YELLOWALERT("gcm.encrypt", "ret %d", ret);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx,
                             size_t length,
                             const unsigned char *iv,
                             size_t iv_len,
                             const unsigned char *add,
                             size_t add_len,
                             const unsigned char *tag,
                             size_t tag_len,
                             const unsigned char *input,
                             unsigned char *output) {
    if (!ctx || !ctx->key || !input || !output || iv_len != AES128_GCM_IV_LENGTH) {
        LTLOG_YELLOWALERT("gcm.decrypt", "not ready");
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    int ret = s_Crypto->DecryptAES128GCM((u8 *)ctx->key, iv, add, add_len, input, length, tag, tag_len, output);
    if (ret) {
        LTLOG_YELLOWALERT("gcm.decrypt", "failed ret %d", ret);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len) {
    LT_UNUSED(ctx);
    LT_UNUSED(mode);
    LT_UNUSED(iv);
    LT_UNUSED(iv_len);
    return 0;
}

int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx,
                          const unsigned char *add,
                          size_t add_len) {
    LT_UNUSED(ctx);
    LT_UNUSED(add);
    LT_UNUSED(add_len);
    return 0;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input, size_t input_length,
                       unsigned char *output, size_t output_size,
                       size_t *output_length) {
    LT_UNUSED(ctx);
    LT_UNUSED(input);
    LT_UNUSED(input_length);
    LT_UNUSED(output);
    LT_UNUSED(output_size);
    LT_UNUSED(output_length);
    return 0;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output, size_t output_size,
                       size_t *output_length,
                       unsigned char *tag, size_t tag_len) {
    LT_UNUSED(ctx);
    LT_UNUSED(tag);
    LT_UNUSED(tag_len);
    LT_UNUSED(output);
    LT_UNUSED(output_size);
    LT_UNUSED(output_length);
    return 0;
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx) {
    if (ctx && ctx->key)
        lt_free(ctx->key);
}

#endif

#if defined(MBEDTLS_SHA256_ALT)

void mbedtls_sha256_init(mbedtls_sha256_context *ctx)
{
    lt_memset(ctx, 0, sizeof(mbedtls_sha256_context));
}

void mbedtls_sha256_free(mbedtls_sha256_context *ctx)
{
    if (ctx == NULL) {
        return;
    }
    s_Crypto->DestroySeqSHA256(ctx->lt_ctx);

    mbedtls_platform_zeroize(ctx, sizeof(mbedtls_sha256_context));
}

void mbedtls_sha256_clone(mbedtls_sha256_context *dst,
                          const mbedtls_sha256_context *src)
{
    s_Crypto->DestroySeqSHA256(dst->lt_ctx);
    dst->lt_ctx = s_Crypto->CloneSeqSHA256(src->lt_ctx);
}

/*
 * SHA-256 context setup
 */
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{
    if (!ctx || is224) {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }
    s_Crypto->DestroySeqSHA256(ctx->lt_ctx);
    ctx->lt_ctx = s_Crypto->CreateSeqSHA256();
    if (!ctx->lt_ctx) {
        LTLOG_YELLOWALERT("sha256.start", "CreateSeqSHA256 failed");
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

/*
 * SHA-256 process buffer
 */
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                          const unsigned char *input,
                          size_t ilen)
{
    if (!ctx || !input || ilen == 0) {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }
    int ret = s_Crypto->UpdateSeqSHA256((LT_SHA256_CTX*)ctx->lt_ctx, input, ilen);
    if (ret) {
        LTLOG_YELLOWALERT("sha256.update", "failed %p ret %d", ctx->lt_ctx, ret);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

/*
 * SHA-256 final digest
 */
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,
                          unsigned char *output)
{
    if (!ctx || !output) {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }
    int ret = s_Crypto->FinishSeqSHA256((LT_SHA256_CTX*)ctx->lt_ctx, output);
    if (ret) {
        LTLOG_YELLOWALERT("sha256.finish", "failed %p ret %d", ctx->lt_ctx, ret);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

/*
 * output = SHA-256( input buffer )
 */
int mbedtls_sha256(const unsigned char *input,
                   size_t ilen,
                   unsigned char *output,
                   int is224)
{
    if (!input || !output || !ilen || is224) {
       return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }
    int ret = s_Crypto->GenDigestSHA256(input, ilen, output);
    if (ret) {
        LTLOG_YELLOWALERT("sha256.digest", "failed ret %d", ret);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    else
        return 0;
}

#endif

void LTNetDtlsCrypto_Fini(void) {
    if (s_Crypto) lt_closelibrary(s_Crypto);
    s_Crypto = NULL;
}

bool
LTNetDtlsCrypto_Init(void) {
    s_Crypto  = lt_openlibrary(LTSystemCrypto);
    if (s_Crypto) return true;
    LTNetDtlsCrypto_Fini();
    return false;
}
