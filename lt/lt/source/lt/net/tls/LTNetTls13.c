/*******************************************************************************
 * source/lt/net/tls/LTNetTls13.c         Implementation of TLS 1.3 client functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 *******************************************************************************
 *
 * This file has been created to complete TLS operations for LT, reduce code
 * and data sizes, and be compatible with LT coding standard.
 *
 * This file has modified the source code from https://github.com/Mbed-TLS/mbedtls
 *
 * The modified source files are ssl_client.c, ssl_msg.c, ssl_tls.c,
 * ssl_tls13_client.c, and ssl_tls13_generic.c in Mbed TLS.
 *
 * The copyright and license in the headers of the original source files are as follows.
 *
 *******************************************************************************
 *
 * Copyright The Mbed TLS Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 ( the "License" ); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *******************************************************************************/

/********************************************************************************
IETF RFC 8446, https://datatracker.ietf.org/doc/html/rfc8446

page 62, 4.4.1, transcript hash
This value is computed by hashing the concatenation
of each included handshake message, including the handshake message
header carrying the handshake message type and length fields, but not
including record layer headers.  I.e.,

Transcript-Hash(M1, M2, ... Mn) = Hash(M1 || M2 || ... || Mn)

For concreteness, the transcript hash is always taken from the
following sequence of handshake messages, starting at the first
ClientHello and including only those messages that were sent:
ClientHello, HelloRetryRequest, ClientHello, ServerHello,
EncryptedExtensions, server CertificateRequest, server Certificate,
server CertificateVerify, server Finished, EndOfEarlyData, client
Certificate, client CertificateVerify, client Finished.

As an exception to this general rule, when the server responds to a
ClientHello with a HelloRetryRequest, the value of ClientHello1 is
replaced with a special synthetic handshake message of handshake type
"message_hash" containing Hash(ClientHello1).  I.e.,

Transcript-Hash(ClientHello1, HelloRetryRequest, ... Mn) =
    Hash(message_hash       ||              // Handshake type
         00 00 Hash.length  ||              // Handshake message length (bytes)
         Hash(ClientHello1) ||              // Hash of ClientHello1
         HelloRetryRequest  || ... || Mn)

page 92, 7.1, key schedule
             0
             |
             v
   PSK ->  HKDF-Extract = Early Secret
             |
             +-----> Derive-Secret(., "ext binder" | "res binder", "")
             |                     = binder_key
             |
             +-----> Derive-Secret(., "c e traffic", ClientHello)
             |                     = client_early_traffic_secret
             |
             +-----> Derive-Secret(., "e exp master", ClientHello)
             |                     = early_exporter_master_secret
             v
       Derive-Secret(., "derived", "")
             |
             v
   (EC)DHE -> HKDF-Extract = Handshake Secret
             |
             +-----> Derive-Secret(., "c hs traffic",
             |                     ClientHello...ServerHello)
             |                     = client_handshake_traffic_secret
             |
             +-----> Derive-Secret(., "s hs traffic",
             |                     ClientHello...ServerHello)
             |                     = server_handshake_traffic_secret
             v
       Derive-Secret(., "derived", "")
             |
             v
   0 -> HKDF-Extract = Master Secret
             |
             +-----> Derive-Secret(., "c ap traffic",
             |                     ClientHello...server Finished)
             |                     = client_application_traffic_secret_0
             |
             +-----> Derive-Secret(., "s ap traffic",
             |                     ClientHello...server Finished)
             |                     = server_application_traffic_secret_0
             |
             +-----> Derive-Secret(., "exp master",
             |                     ClientHello...server Finished)
             |                     = exporter_master_secret
             |
             +-----> Derive-Secret(., "res master",
                                   ClientHello...client Finished)
                                   = resumption_master_secret

page 61, 4.4, handshake context
   +-----------+-------------------------+-----------------------------+
   | Mode      | Handshake Context       | Base Key                    |
   +-----------+-------------------------+-----------------------------+
   | Server    | ClientHello ... later   | server_handshake_traffic_   |
   |           | of EncryptedExtensions/ | secret                      |
   |           | CertificateRequest      |                             |
   |           |                         |                             |
   | Client    | ClientHello ... later   | client_handshake_traffic_   |
   |           | of server               | secret                      |
   |           | Finished/EndOfEarlyData |                             |
   |           |                         |                             |
   | Post-     | ClientHello ... client  | client_application_traffic_ |
   | Handshake | Finished +              | secret_N                    |
   |           | CertificateRequest      |                             |
   +-----------+-------------------------+-----------------------------+
For concreteness, the transcript hash is always taken from the
following sequence of handshake messages, starting at the first
ClientHello and including only those messages that were sent:
ClientHello, HelloRetryRequest, ClientHello, ServerHello,
EncryptedExtensions, server CertificateRequest, server Certificate,
server CertificateVerify, server Finished, EndOfEarlyData, client
Certificate, client CertificateVerify, client Finished.

page 96, 7.5, Exporters

   TLS-Exporter(label, context_value, key_length) =
       HKDF-Expand-Label(Derive-Secret(Secret, label, ""),
                         "exporter", Hash(context_value), key_length)

   Where Secret is either the early_exporter_master_secret or the
   exporter_master_secret.  Implementations MUST use the
   exporter_master_secret unless explicitly specified by the
   application.  The early_exporter_master_secret is defined for use in
   settings where an exporter is needed for 0-RTT data.

*/

/* A function returns the error code. The error code should be immediately passed
 * to handshake or session handler. Then, a fatal alert will be sent.
 *
 * Each function has an error id. The error id is combined with Mbed TLS error code to make the LT's TLS error code.
 * If a function will replace an error code, it must log the error code before replacing.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTTime.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/tls/LTNetTlsErrors.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include "LTNetTlsDefs.h"
#include "LTNetTls13.h"

/***************************** helper functions ******************************/
DEFINE_LTLOG_SECTION("tls13");
#define P(...)
#define PLOG(...) LTLOG(__VA_ARGS__)

// enable or disable trace error code
#if 1
static void TraceError(int error) {
    LTLOG_YELLOWALERT("tr.tls", "-%08lX", LT_Ps32(-error));
}
#else
#define TraceError(err) LT_UNUSED(err)
#endif

static void LogTlsError(LTTlsState currState, LTTlsState nextState, int error) {
    // Most errors are transient and thus use yellow alert. Critical errors immediately generate red alerts at the origin.
    LTLOG_YELLOWALERT("error", "curr state %d, next state %d, error code -%08lX", currState, nextState, LT_Ps32(-error));
}

// TODO add more supporeted ciphers here if needed
static const u16 s_CiphersuiteList[] = {MBEDTLS_TLS1_3_AES_128_GCM_SHA256, 0x0000};
static const u16 s_ECGroupList[]     = {MBEDTLS_SSL_IANA_TLS_GROUP_X25519, 0x0000};

// constants
static const u8 s_MagicHrrString[LTTLS_SERVER_HELLO_RANDOM_LEN] = {
    0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
    0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C};

#define LTTLS_LABEL(name, string)            .name = string,
struct LTTLSLabelsStruct const tlsLabels = { LTTLS_LABEL_LIST };
#undef LTTLS_LABEL

/******************************************************************************
 * This part is for TLS debug (in either debug build or release build).
 *
 * To debug TLS, we need to dump some secrets to a key file in Linux
 * Then, we can decrypt TLS packets in Wireshark.
 *
 * Undefine TLSFILE to disable TLS secret dump.
 *
 * TODO create a mechanism to save the secrets in device and dump TLS packets
 *      for TLS diagnosis.
 */
#ifdef TLSKEY
#if TLSKEY == 0
#define P(...) LT_GetCore()->ConsoleStomp(__VA_ARGS__)
static void PrintBytes(const u8 *d, u16 len, const char *tag) {
    if (tag) P("%s : ", tag);
    for (u32 i = 0; i < len; ++i) {
        P("%02x", d[i]);
    }
}
static void DumpSecret(const char *label, const u8 *rand, u8 randLen, const u8 *secret, u8 secLen) {
    P("%s ", label);
    PrintBytes(rand, randLen, NULL);
    P(" ");
    PrintBytes(secret, secLen, NULL);
    P("\n");
}
#endif

#if TLSKEY == 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
static int  s_hTlsKeyFile = 0;
static char s_tlsKeyFile[] = "/tmp/tls-key.log";

// #define P(...) LTLOG(__VA_ARGS__)
// static void PrintBytes(const u8 *d, u16 len, const char *tag) {
//     if (tag) P("%s : ", tag);
//     for (u32 i = 0; i < len; ++i) {
//         P("%02x", d[i]);
//     }
// }

static void ToHex(u8 *hex, const u8 *data, LT_SIZE len) {
    if (!hex || !data) return;
    char h[] = "0123456789abcdef";
    for (LT_SIZE i = 0; i < len; i++) {
        hex[i * 2]     = h[data[i] >> 4];
        hex[i * 2 + 1] = h[data[i] & 0xF];
    }
}

static void DumpSecret(const char *label, const u8 *rand, u8 randLen, const u8 *secret, u8 secLen) {
    if (!s_hTlsKeyFile) s_hTlsKeyFile = open(s_tlsKeyFile, O_WRONLY|O_CREAT|O_APPEND);  // O_TRUNC

    u8 secbuf[SHA256_HASH_LENGTH * 2];
    ssize_t w;
    w = write(s_hTlsKeyFile, label, strlen(label));
    w = write(s_hTlsKeyFile, " ", 1);
    ToHex(secbuf, (u8 *)rand, SHA256_HASH_LENGTH);
    w = write(s_hTlsKeyFile, secbuf, SHA256_HASH_LENGTH * 2);
    w = write(s_hTlsKeyFile, " ", 1);
    ToHex(secbuf, (u8 *)secret, SHA256_HASH_LENGTH);
    w = write(s_hTlsKeyFile, secbuf, SHA256_HASH_LENGTH * 2);
    w = write(s_hTlsKeyFile, "\n", 1);
    fsync(s_hTlsKeyFile);

    LT_UNUSED(w);
    LT_UNUSED(randLen);
    LT_UNUSED(secLen);
}
#endif
#endif
/* End of TLS debug **********************************************************/

/**********************************  Cryptos *********************************/

/**
 * @brief  Extract secret from salt and IKM using HMAC_SHA256
 * IETF RFC 5869, https://datatracker.ietf.org/doc/html/rfc5869
 * IETF RFC 8446, 7.1
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[out] res     The new secret, always 32 bytes
 * @param[in]  salt    The salt (old secret, from top)
 * @param[in]  saltLen The length of salt
 * @param[in]  ikm     The input key material (key, from left)
 * @param[in]  ikmLen  The length of IKM
 * @return   Crypto rror code
 */
static LTSystemCryptoResult HKDFExtract(const LTTlsContext *tlsCtx, u8 res[SHA256_HASH_LENGTH], const u8 *salt, LT_SIZE saltLen, const u8 *ikm, LT_SIZE ikmLen) {
    return tlsCtx->config->crypto->GenHMACSHA256(salt, saltLen, ikm, ikmLen, res);
}

/**
 * @brief  Create info to derive secret
 *
 *      page 90, section 7.1, Key Schedule
 *      Make the HkdfLabel as specified below:
 *
 *      struct {
 *          uint16 length = Length;
 *          opaque label<7..255> = "tls13 " + Label;
 *          opaque context<0..255> = Context;
 *      } HkdfLabel;
 *
 * @param[out] info       The result info
 * @param[in]  length     The length of the derived secret (not info)
 * @param[in]  label      The label
 * @param[in]  labelLen   The length of label
 * @param[in]  context    The context (hash)
 * @param[in]  contextLen The length of context
 * @return  The length of info
 */
static LT_SIZE MakeInfo(u8 info[64], LT_SIZE length, const char *label, LT_SIZE labelLen, const u8 *context, LT_SIZE contextLen) {
    // [0...1] must be nLength
    info[0] = (length >> 8);
    info[1] = length;
    // [2] must be the length of 6 + nLabelLen
    info[2] = 6 + labelLen;
    // [3...8] must be "tls13 "
    lt_memcpy(info + 3, "tls13 ", 6);
    // [9...L] must be the label
    lt_memcpy(info + 9, label, labelLen);
    // [L+1] must be nContextLen
    info[3 + info[2]] = contextLen;
    // [L+2...] must be the context
    lt_memcpy(info + 3 + info[2] + 1, context, contextLen);
    return 10 + labelLen + contextLen;
}

/**
 * @brief Hmac-based key derivation function with expansion
 *
 *        page 90, section 7.1, Key Schedule
 *        HKDF-Expand-Label(Secret, Label, Context, Length)
 *
 * @param[in]  tlsCtx     The TLS context
 * @param[out] res        The result key
 * @param[in]  resLen     The length of key
 * @param[in]  prk        The pseudorandom key    (Secret  in HKDF-Expand-Label)
 * @param[in]  label      The label               (Label   in HKDF-Expand-Label)
 * @param[in]  labelLen   The length of label
 * @param[in]  context    The context             (Context in HKDF-Expand-Label, hash of a message)
 * @param[in]  contextLen The length of context
 * @return   Crypto error code
 */
static LTSystemCryptoResult HKDFExpand(const LTTlsContext *tlsCtx, u8 *res, LT_SIZE resLen, const u8 prk[SHA256_HASH_LENGTH], const char *label, LT_SIZE labelLen, const u8 *context, LT_SIZE contextLen) {
    u8 info[64];
    lt_memset(info, 0, 64);
    LT_SIZE infoLen = MakeInfo(info, resLen, label, labelLen, context, contextLen);
    info[infoLen] = 1;

    LTSystemCryptoResult ret = 0;
    if (resLen == SHA256_HASH_LENGTH) {
        ret = tlsCtx->config->crypto->GenHMACSHA256(prk, SHA256_HASH_LENGTH, info, infoLen + 1, res);

    } else {
        ret = tlsCtx->config->crypto->GenHMACSHA256(prk, SHA256_HASH_LENGTH, info, infoLen + 1, info);
        lt_memcpy(res, info, resLen);
    }

    return ret;
}

/**
 * @brief  Derive secret with key, label, and contextual message
 *
 * @param[in]  tlsCtx     The TLS context
 * @param[out] res        The secret to derive
 * @param[in]  resLen     The length of secret
 * @param[in]  prk        The key to derive the secret, must be 32 bytes
 * @param[in]  label      The label
 * @param[in]  labelLen   The length of label
 * @param[in]  message    The message
 * @param[in]  messageLen The length of message. 32 if the message is hashed
 * @param[in]  bHashed    True if the message is already hashed. Otherwise, false, i.e. the message is not hashed yet.
 * @return   Crypto error code
 */
static LTSystemCryptoResult DeriveSecret(const LTTlsContext *tlsCtx, u8 *res, LT_SIZE resLen, const u8 prk[SHA256_HASH_LENGTH], const char *label, LT_SIZE labelLen, const u8 *message, LT_SIZE messageLen, bool bHashed) {
    LTSystemCryptoResult ret = 0;
    if (!bHashed) {
        u8 h[SHA256_HASH_LENGTH];
        if (0 != (ret = tlsCtx->config->crypto->GenDigestSHA256(message, messageLen, h))) return ret;
        ret = HKDFExpand(tlsCtx, res, resLen, prk, label, labelLen, h, SHA256_HASH_LENGTH);

    } else {
        if (messageLen != SHA256_HASH_LENGTH) return kLTSystemCrypto_Result_WrongLength;
        ret = HKDFExpand(tlsCtx, res, resLen, prk, label, labelLen, message, SHA256_HASH_LENGTH);
    }
    return ret;
}

/**
 * @brief  Initialize the transcript
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Crypto error code
 */
static LTSystemCryptoResult InitTranscript(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (!handshake) return kLTSystemCrypto_Result_Null;
    LTSystemCrypto *crypto = tlsCtx->config->crypto;
    crypto->DestroySeqSHA256(handshake->hashCtx);
    return ((handshake->hashCtx = crypto->CreateSeqSHA256()) != NULL) ?
           kLTSystemCrypto_Result_Ok : kLTSystemCrypto_Result_Error;
}

/**
 * @brief  Update the transcript (hash, or context)
 *
 * @param[in,out]  tlsCtx      The TLS context
 * @param[in]      message     The message
 * @param[in]      messageLen  The length of message
 * @return  Crypto error code
 */
static LTSystemCryptoResult UpdateTranscript(LTTlsContext *tlsCtx, const u8 *message, LT_SIZE messageLen) {
    if (!tlsCtx->handshake) return kLTSystemCrypto_Result_Null;
    return tlsCtx->config->crypto->UpdateSeqSHA256(tlsCtx->handshake->hashCtx, message, messageLen);
}

/**
 * @brief Get the Transcript Hash
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[out] h       The output hash
 * @return  Crypto error code
 */
static LTSystemCryptoResult GetTranscriptHash(const LTTlsContext *tlsCtx, u8 h[SHA256_HASH_LENGTH]) {
    LTSystemCryptoResult result = kLTSystemCrypto_Result_Null;
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (handshake) {
        LTSystemCrypto *crypto = tlsCtx->config->crypto;
        LT_SHA256_CTX  *ctx = crypto->CloneSeqSHA256(handshake->hashCtx);
        if (ctx) {
            result = crypto->FinishSeqSHA256(ctx, h);
            crypto->DestroySeqSHA256(ctx);
        } else {
            result = kLTSystemCrypto_Result_OOM;
        }
    }
    return result;
}

/**
 * @brief Get the Hmac
 *
 * @param[in]  tlsCtx      The TLS context
 * @param[in]  key         The hmac key
 * @param[in]  keyLen      The length of key
 * @param[in]  message     The message to produce hmac
 * @param[in]  messageLen  The length of message
 * @param[out] hmac        The output hmac
 * @return  Crypto error code
 */
static LTSystemCryptoResult GetHmac(const LTTlsContext *tlsCtx, const u8 *key, LT_SIZE keyLen, const u8 *message, LT_SIZE messageLen, u8 hmac[SHA256_HASH_LENGTH]) {
    return tlsCtx->config->crypto->GenHMACSHA256(key, keyLen, message, messageLen, hmac);
}

/**
 * @brief Get a ECDHE key
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[in]  inKey   The peer's public key
 * @param[out] outKey  The output key
 * @return  Crypto error code
 */
static LTSystemCryptoResult GenKeyECDHE(const LTTlsContext *tlsCtx, const u8 *inKey, u8 *outKey) {
    if (!tlsCtx->handshake) return kLTSystemCrypto_Result_Null;
    return tlsCtx->config->crypto->GenKeyEcdhe((u8 *)tlsCtx->handshake->ecdhePrivKey, inKey, outKey);
}

/**
 * @brief  add content type to the end of content and pad 0s to the end of content
 *
 * @param[in,out] content     The content, plaintext
 * @param[out]    contentLen  The length of content
 * @param[in]     recType     The content type
 * @param[in]     padLen      The length of padding 0s
 */
static void BuildInnerPlaintext(u8 *content, u16 *contentLen, u8 recType, u16 padLen) {
    LT_SIZE nLen = *contentLen;
    content[nLen] = recType;
    ++nLen;
    lt_memset(content + nLen, 0, padLen);
    *contentLen = nLen + padLen;
}

/**
 * @brief Remove the padding 0s from the end of the content and restore record type of the content
 *
 * @param[in]      content     The content, plaintext
 * @param[out]     contentLen  The pointer to the length of content. The length will be updated after removing the padding 0s.
 * @param[in,out]  recType     The pointer to the content type. The type will be updated with the last byte in the content.
 */
static void StripInnerPlaintext(u8 const *content, u16 *contentLen, u8 *recType) {
    LT_SIZE nRemaining = *contentLen;

    /* Determine length of padding by skipping zeroes from the back. */
    do {
        if (nRemaining == 0) {
            return;
        }
        --nRemaining;
    } while (content[nRemaining] == 0);

    *contentLen = nRemaining;
    *recType = content[nRemaining];
}

/**
 * @brief  Produce the additional authentication data
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] aad    The output additional authentication data
 */
static void ExtractAadData(const LTTlsContext *tlsCtx, u8 *aad) {
    aad[0] = tlsCtx->outMsgType;
    aad[1] = MBEDTLS_SSL_MAJOR_VERSION_3;
    aad[2] = MBEDTLS_SSL_MINOR_VERSION_3;
    u16 aadLen = tlsCtx->outMsgLen + AES128_GCM_MAX_TAG_LENGTH;
    aad[3] = (aadLen >> 8);
    aad[4] = aadLen;
}

// Error ID 0x0080
/**
 * @brief  Encrypt a record pointed by pOutHdr and update nOutMsgLen and nOutCtr
 *
 * @param[in,out]  tlsCtx  The TLS context
 * @return   Error code
 */
static int EncryptRecord(LTTlsContext *tlsCtx) {
    if (!tlsCtx->transformOut || tlsCtx->outMsg - tlsCtx->outHdr != LTTLS_RECORD_HEADER_LEN) {
        return LTTLS_ERROR(0x0080 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    if (tlsCtx->outMsgLen > LTTLS_OUT_BUFFER_LEN) {
        return LTTLS_ERROR(0x0080, MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
    }

    LT_SIZE paddingLen = LTTLS_PADDINGLEN(tlsCtx->outMsgLen, LTTLS_PADDING);
    LTTLS_CHK_BUF_PTR(0x0080, tlsCtx->outHdr, tlsCtx->outBufEnd,
        (LTTLS_RECORD_HEADER_LEN + tlsCtx->outMsgLen + 1 + paddingLen + AES128_GCM_MAX_TAG_LENGTH));

    /* After build, out_msglen = out_msglen + 1 + padding_len */
    BuildInnerPlaintext(tlsCtx->outMsg, &tlsCtx->outMsgLen, tlsCtx->outMsgType, paddingLen);
    // now, set type to application. The original type is already saved inside the inner text.
    tlsCtx->outMsgType = MBEDTLS_SSL_MSG_APPLICATION_DATA;

    /* Build nonce for AEAD encryption: IV = fixed_iv XOR ( 0 || counter_big_endian ) */
    LTTLSNonce iv;
    lt_memcpy(iv.bytes, tlsCtx->transformOut->clientWriteIV, AES128_GCM_IV_LENGTH);
    u64 nCtr = LT_BE64(tlsCtx->transformOut->outCounter);
    iv.bytes[1] ^= nCtr;
    iv.bytes[2] ^= nCtr >> 32;

    /* Build additional authentication data for AEAD encryption. */
    ExtractAadData(tlsCtx, tlsCtx->outHdr);

    /* Encrypt and authenticate */
    int ret = 0;
    if (0 != (ret = tlsCtx->config->crypto->EncryptAES128GCM((u8 *)tlsCtx->transformOut->clientWriteKey,
        (u8 *)iv.bytes, tlsCtx->outHdr, LTTLS_RECORD_HEADER_LEN, tlsCtx->outMsg, tlsCtx->outMsgLen,
        tlsCtx->outMsg, tlsCtx->outMsg + tlsCtx->outMsgLen, AES128_GCM_MAX_TAG_LENGTH))) {
        return LTTLS_ERROR(0x0080 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    tlsCtx->outMsgLen += AES128_GCM_MAX_TAG_LENGTH;
    ++tlsCtx->transformOut->outCounter;
    return 0;
}

// Error ID 0x0090
/**
 * @brief  Decrypt the record in input with AES128_GCM
 *         IV is pTransformIn->serverWriteIV || nInCtr
 *         AAD is pInHdr, AAD length is always 5
 *         Cipher text is pInMsg
 *         Key is pTransformIn->serverWriteKey
 *         nInRecPayloadLen and nInRecType are updated after decryption
 *         Then nInCtr++
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return int
 */
static int DecryptRecord(LTTlsContext *tlsCtx) {
    if (!tlsCtx->transformIn) {
        return LTTLS_ERROR(0x0090, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    /* Build nonce for AEAD decryption: IV = fixed_iv XOR ( 0 || counter_big_endian ) */
    LTTLSNonce iv;
    lt_memcpy(iv.bytes, tlsCtx->transformIn->serverWriteIV, AES128_GCM_IV_LENGTH);
    u64 counter = LT_BE64(tlsCtx->transformIn->inCounter);
    iv.bytes[1] ^= counter;
    iv.bytes[2] ^= counter >> 32;

    /* Check that there's space for the authentication tag. */
    if (tlsCtx->inRecPayloadLen < AES128_GCM_MAX_TAG_LENGTH + 1) {
        return LTTLS_ERROR(0x0090 + 1, MBEDTLS_ERR_SSL_INVALID_MAC);
    }

    /* Decrypt and authenticate */
    tlsCtx->inRecPayloadLen -= AES128_GCM_MAX_TAG_LENGTH;

    int ret = tlsCtx->config->crypto->DecryptAES128GCM((u8 *)tlsCtx->transformIn->serverWriteKey,
        (u8 *)iv.bytes, tlsCtx->inRecHdr, LTTLS_RECORD_HEADER_LEN, tlsCtx->inRecPayload, tlsCtx->inRecPayloadLen,
        tlsCtx->inRecPayload + tlsCtx->inRecPayloadLen, AES128_GCM_MAX_TAG_LENGTH, tlsCtx->inRecPayload);
    if (0 != ret) {
        return LTTLS_ERROR(0x0090 + 2, MBEDTLS_ERR_SSL_INVALID_MAC + ret);
    }

    if (tlsCtx->minorVer == MBEDTLS_SSL_MINOR_VERSION_4) {
        /* Remove inner padding and infer true content type. */
        StripInnerPlaintext(tlsCtx->inRecPayload, &tlsCtx->inRecPayloadLen, &tlsCtx->inRecType);
    }

    ++tlsCtx->transformIn->inCounter;
    return 0;
}

// Error ID 0x00A0
/**
 * @brief  Evolve derivation of secret and key
 *
 * @param[in]  tlsCtx     The TLS context
 * @param[in]  secretOld  The old secret, top most
 * @param[in]  ikm        The input key material, left
 * @param[in]  ikmLen     The length of IKM
 * @param[out] secretNew  The new secret, right, going down
 * @return  Error code
 *
 *        old secret
 *            |
 *            v
 *      Derive-Secret(., "derived", "")
 *            |
 *            v
 *   IKM -> HKDF-Extract = new secret
 *            |
 */
static int EvolveSecret(const LTTlsContext *tlsCtx, const u8 *secretOld, const u8 *ikm, LT_SIZE ikmLen, u8 *secretNew) {
    u8 tmpSecret[SHA256_HASH_LENGTH];
    lt_memset(tmpSecret, 0, SHA256_HASH_LENGTH);

    /* For non-initial runs, call Derive-Secret( ., "derived", "")
     * on the old secret. */
    int ret = 0;
    if (secretOld != NULL) {
        if (0 != (ret = DeriveSecret(tlsCtx, tmpSecret, SHA256_HASH_LENGTH, secretOld, LTTLS_LBL_WITH_LEN(derived), (u8 *)"", 0, false))) {
            return LTTLS_ERROR(0x00A0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
        }
    }

    u8 tmpIKM[SHA256_HASH_LENGTH];
    lt_memset(tmpIKM, 0, SHA256_HASH_LENGTH);
    if (ikm == NULL || ikmLen == 0) {
        ikm = tmpIKM;
        ikmLen = SHA256_HASH_LENGTH;
    }

    /* HKDF-Extract takes a salt and input key material.
     * The salt is the old secret, and the input key material
     * is the input secret (PSK / ECDHE). */
    if (0 != (ret = HKDFExtract(tlsCtx, secretNew, tmpSecret, SHA256_HASH_LENGTH, ikm, ikmLen))) {
        return LTTLS_ERROR(0x00A0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    lt_memset(tmpSecret, 0, SHA256_HASH_LENGTH);
    return 0;
}

// Error ID 0x00B0
/**
 * @brief  Derive keys for early data
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 *
 *            0
 *            |
 *            v
 *  PSK ->  HKDF-Extract = Early Secret
 *            |
 *            +-----> Derive-Secret(., "ext binder" | "res binder", "")
 *            |                     = binder_key
 *            |
 *            +-----> Derive-Secret(., "c e traffic", ClientHello)
 *            |                     = client_early_traffic_secret
 *            |
 *            +-----> Derive-Secret(., "e exp master", ClientHello)
 *            |                     = early_exporter_master_secret
 *            v
 */
static int DeriveEarlySecrets(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (!handshake) {
        return LTTLS_ERROR(0x00B0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    int ret = 0;
    if (0 != (ret = EvolveSecret(tlsCtx, NULL, (u8 *)handshake->psk, SHA256_HASH_LENGTH, (u8 *)handshake->earlySecret))) {
        TraceError(ret);
        return LTTLS_ERROR(0x00B0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    // hash the client hello
    u8 h[SHA256_HASH_LENGTH];
    if (0 != (ret = GetTranscriptHash(tlsCtx, h))) {
        return LTTLS_ERROR(0x00B0 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // create client_early_traffic_secret/
    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)handshake->clientEarlyTrafficSecret, SHA256_HASH_LENGTH,
        (u8 *)handshake->earlySecret, LTTLS_LBL_WITH_LEN(c_e_traffic), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00B0 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // create early_exporter_master_secret/
    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)handshake->earlyExporterMasterSecret, SHA256_HASH_LENGTH,
        (u8 *)handshake->earlySecret, LTTLS_LBL_WITH_LEN(e_exp_master), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00B0 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    return 0;
}

// Error ID 0x00C0
/**
 * @brief  Calculate verify_data for client finished or server finished
 *
 * @param[in]  tlsCtx The TLS context
 * @param[in]  from  For server or client
 * @param[out] hmac   The output HMAC
 * @return   Error code
 *
 *    finished_key =
 *        HKDF-Expand-Label(BaseKey, "finished", "", Hash.length)
 *
 *    Structure of this message:
 *
 *       struct {
 *           opaque verify_data[Hash.length];
 *        } Finished;
 *
 *    The verify_data value is computed as follows:
 *
 *       verify_data =
 *           HMAC(finished_key, Transcript-Hash(Handshake Context, Certificate*, CertificateVerify*))
 */
static int CalculateVerifyData(const LTTlsContext *tlsCtx, int from, u8 hmac[SHA256_HASH_LENGTH]) {
    // get hash
    int ret = 0;
    u8 h[SHA256_HASH_LENGTH];
    if (0 != (ret = GetTranscriptHash(tlsCtx, h))) {
        return LTTLS_ERROR(0x00C0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // get finished key
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (!handshake) {
        return LTTLS_ERROR(0x00C0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    const u8 *baseKey = (MBEDTLS_SSL_IS_CLIENT == from) ? (u8 *)handshake->clientHandshakeTrafficSecret : (u8 *)handshake->serverHandshakeTrafficSecret;
    u32 finishedKey[SHA256_HASH_LENGTH / 4]; // aligned with u32
    u8 *finKey = (u8 *)finishedKey;
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)finKey, SHA256_HASH_LENGTH, (u8 *)baseKey, LTTLS_LBL_WITH_LEN(finished), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00C0 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // get hmac
    if (0 != (ret = GetHmac(tlsCtx, finKey, SHA256_HASH_LENGTH, h, SHA256_HASH_LENGTH, hmac))) {
        return LTTLS_ERROR(0x00C0 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    /* Erase handshake secrets */
    lt_memset(finKey, 0, SHA256_HASH_LENGTH);
    lt_memset(h, 0, SHA256_HASH_LENGTH);
    return 0;
}

// Error ID 0x00D0
/**
 * @brief  Derive keys for handshake
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 *
 *               |
 *               v
 *       Derive-Secret(., "derived", "")
 *               |
 *               v
 * (EC)DHE -> HKDF-Extract = Handshake Secret
 *               |
 *               +-----> Derive-Secret(., "c hs traffic",
 *               |                     ClientHello...ServerHello)
 *               |                     = client_handshake_traffic_secret
 *               |
 *               +-----> Derive-Secret(., "s hs traffic",
 *               |                     ClientHello...ServerHello)
 *               |                     = server_handshake_traffic_secret
 *               v
 *
 *   [sender]_write_key = HKDF-Expand-Label( Secret, "key", "", key_length )
 *   [sender]_write_iv  = HKDF-Expand-Label( Secret, "iv" , "", iv_length )
 */
static int KeyScheduleHandshake(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    LTTlsTransform *transform = tlsCtx->transformHandshake;
    if (!handshake || !transform) {
        return LTTLS_ERROR(0x00D0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    int ret = 0;
    if (handshake->offeredGroupId == MBEDTLS_SSL_IANA_TLS_GROUP_X25519) {
        // compute ECDHE shared secret
        if (0 != (ret = GenKeyECDHE(tlsCtx, (u8 *)handshake->ecdhePeerKey, (u8 *)handshake->ecdhePeerKey))) {
            return LTTLS_ERROR(0x00D0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
        }
        lt_memset(handshake->ecdhePrivKey, 0, ECDHE_KEY_LENGTH);
        // compute the handshake secret
        if (0 != (ret = EvolveSecret(tlsCtx, (u8 *)handshake->earlySecret, (u8 *)handshake->ecdheKey, ECDHE_KEY_LENGTH, (u8 *)handshake->handshakeSecret))) {
            TraceError(ret);
            return LTTLS_ERROR(0x00D0 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
        }
        lt_memset(handshake->ecdheKey, 0, ECDHE_KEY_LENGTH);

    } else {
        return LTTLS_ERROR(0x00D0 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    u8 h[SHA256_HASH_LENGTH];
    // compute server hello hash (already update, just need to finish)
    if (0 != (ret = GetTranscriptHash(tlsCtx, h))) {
        return LTTLS_ERROR(0x00D0 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // compute client_handshake_traffic_secret
    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)handshake->clientHandshakeTrafficSecret,
        SHA256_HASH_LENGTH, (u8 *)handshake->handshakeSecret, LTTLS_LBL_WITH_LEN(c_hs_traffic), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00D0 + 6, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    // compute server_handshake_traffic_secret
    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)handshake->serverHandshakeTrafficSecret,
        SHA256_HASH_LENGTH, (u8 *)handshake->handshakeSecret, LTTLS_LBL_WITH_LEN(s_hs_traffic), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00D0 + 7, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    lt_memset(h, 0, sizeof(SHA256_HASH_LENGTH));

    // TODO export tranffic secret

    // make transform
    // server handshake traffic key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->serverWriteKey, AES128_KEY_LENGTH,
        (u8 *)handshake->serverHandshakeTrafficSecret, LTTLS_LBL_WITH_LEN(key), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00D0 + 8, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->serverWriteIV, AES128_GCM_IV_LENGTH,
        (u8 *)handshake->serverHandshakeTrafficSecret, LTTLS_LBL_WITH_LEN(iv), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00D0 + 9, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // client handshake traffic key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->clientWriteKey, AES128_KEY_LENGTH,
        (u8 *)handshake->clientHandshakeTrafficSecret, LTTLS_LBL_WITH_LEN(key), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00D0 + 0xA, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->clientWriteIV, AES128_GCM_IV_LENGTH,
        (u8 *)handshake->clientHandshakeTrafficSecret, LTTLS_LBL_WITH_LEN(iv), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00D0 + 0xB, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    transform->inCounter  = 0;
    transform->outCounter = 0;

#ifdef TLSKEY
    // dump tls secrets
    DumpSecret("SERVER_HANDSHAKE_TRAFFIC_SECRET", (u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN, (u8 *)handshake->serverHandshakeTrafficSecret, SHA256_HASH_LENGTH);
    DumpSecret("CLIENT_HANDSHAKE_TRAFFIC_SECRET", (u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN, (u8 *)handshake->clientHandshakeTrafficSecret, SHA256_HASH_LENGTH);
#endif

    return 0;
}

// Error ID 0x00E0
/**
 * @brief  Derive keys for application
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 *
 *          |
 *          v
 *   Derive-Secret(., "derived", "")
 *          |
 *          v
 * 0 -> HKDF-Extract = Master Secret
 *          |
 *          +-----> Derive-Secret( ., "c ap traffic",
 *          |                      ClientHello...server Finished )
 *          |                      = client_application_traffic_secret_0
 *          |
 *          +-----> Derive-Secret( ., "s ap traffic",
 *          |                      ClientHello...Server Finished )
 *          |                      = server_application_traffic_secret_0
 *          |
 *          +-----> Derive-Secret( ., "exp master",
 *          |                      ClientHello...server Finished)
 *          |                      = exporter_master_secret
 *
 *   [sender]_write_key = HKDF-Expand-Label( Secret, "key", "", key_length )
 *   [sender]_write_iv  = HKDF-Expand-Label( Secret, "iv" , "", iv_length )
 */
static int KeyScheduleApplication(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    LTTlsSession   *session   = tlsCtx->sessionNegotiate;
    LTTlsTransform *transform = tlsCtx->transformApplication;
    if (!handshake || !session || !transform) {
        return LTTLS_ERROR(0x00E0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    int ret = 0;
    /* Compute MasterSecret */
    if (0 != (ret = EvolveSecret(tlsCtx, (u8 *)handshake->handshakeSecret, NULL, 0, (u8 *)session->masterAppSecret))) {
        TraceError(ret);
        return LTTLS_ERROR(0x00E0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    /* Compute current handshake transcript after the ServerFinished. */
    u8 h[SHA256_HASH_LENGTH];
    if (0 != (ret = GetTranscriptHash(tlsCtx, h))) {
        return LTTLS_ERROR(0x00E0 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)session->clientApplicationTrafficSecret, SHA256_HASH_LENGTH,
        (u8 *)session->masterAppSecret, LTTLS_LBL_WITH_LEN(c_ap_traffic), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00E0 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)session->serverApplicationTrafficSecret, SHA256_HASH_LENGTH,
        (u8 *)session->masterAppSecret, LTTLS_LBL_WITH_LEN(s_ap_traffic), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00E0 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)session->exporterMasterSecret, SHA256_HASH_LENGTH,
        (u8 *)session->masterAppSecret, LTTLS_LBL_WITH_LEN(exp_master), h, SHA256_HASH_LENGTH, true))) {
        return LTTLS_ERROR(0x00E0 + 6, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    lt_memset(h, 0, SHA256_HASH_LENGTH);

    /* Derive first epoch of IV + Key for application traffic. */
    // server application traffic key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->serverWriteKey, AES128_KEY_LENGTH,
        (u8 *)session->serverApplicationTrafficSecret, LTTLS_LBL_WITH_LEN(key), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00E0 + 7, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->serverWriteIV, AES128_GCM_IV_LENGTH,
        (u8 *)session->serverApplicationTrafficSecret, LTTLS_LBL_WITH_LEN(iv), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00E0 + 8, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // client application traffic key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->clientWriteKey, AES128_KEY_LENGTH,
        (u8 *)session->clientApplicationTrafficSecret, LTTLS_LBL_WITH_LEN(key), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00E0 + 9, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)transform->clientWriteIV, AES128_GCM_IV_LENGTH,
        (u8 *)session->clientApplicationTrafficSecret, LTTLS_LBL_WITH_LEN(iv), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x00E0 + 0xA, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    transform->inCounter = 0;
    transform->outCounter = 0;

#ifdef TLSKEY
    // dump tls secrets
    DumpSecret("SERVER_TRAFFIC_SECRET_0", (u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN, (u8 *)session->serverApplicationTrafficSecret, SHA256_HASH_LENGTH);
    DumpSecret("CLIENT_TRAFFIC_SECRET_0", (u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN, (u8 *)session->clientApplicationTrafficSecret, SHA256_HASH_LENGTH);
    DumpSecret("EXPORTER_SECRET",         (u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN, (u8 *)session->exporterMasterSecret,           SHA256_HASH_LENGTH);
#endif

    lt_memset(&handshake->handshakeSecret, 0, sizeof(handshake->handshakeSecret));
    return 0;
}

// Error ID 0x00F0
/**
 * @brief  Derive resumption_master_secret
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 *
 *          |
 *          +-----> Derive-Secret(., "res master",
 *                                ClientHello...client Finished)
 *                                = resumption_master_secret
 */
static int KeyScheduleAfterClientFinished(LTTlsContext *tlsCtx) {
    LTTlsSession *session = tlsCtx->sessionNegotiate;
    if (!session) {
        return LTTLS_ERROR(0x00F0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }
    int ret = 0;
    u8 h[SHA256_HASH_LENGTH];
    if (0 != (ret = GetTranscriptHash(tlsCtx, h))) {
        return LTTLS_ERROR(0x00F0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    ret = DeriveSecret(tlsCtx, (u8 *)session->resumptionMasterSecret, SHA256_HASH_LENGTH,
        (u8 *)session->masterAppSecret, LTTLS_LBL_WITH_LEN(res_master), h, SHA256_HASH_LENGTH, true);
    lt_memset(&session->masterAppSecret, 0, sizeof(session->masterAppSecret));
    lt_memset(h, 0, SHA256_HASH_LENGTH);

    return 0 == ret ? 0 : LTTLS_ERROR(0x00F0 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
}

// Error ID 0x0100
/**
 * @brief  Derive PSK binder using a ticket for session resumption
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[in]  data    The client hello prefix data
 * @param[in]  len     The length of data
 * @param[in]  ticket  The ticket
 * @param[out] binder  The output binder
 * @return    Error code
 *
 *  The PSK associated with the ticket is computed as:
 *  PSK = HKDF-Expand-Label(resumption_master_secret, "resumption", ticket_nonce, Hash.length)
 *
 *            0
 *            |
 *            v
 *  PSK ->  HKDF-Extract = Early Secret
 *            |
 *            +-----> Derive-Secret(., "ext binder" | "res binder", "")
 *            |                     = binder_key
 *
 *  The PskBinderEntry is computed in the same way as the Finished
 *  message (Section 4.4.4) but with the BaseKey being the binder_key
 *  derived via the key schedule from the corresponding PSK which is
 *  being offered (see Section 7.1).
 */
static int DerivePskBinder(const LTTlsContext *tlsCtx, const u8 *data, u16 len, const LTTlsSessionTicket *ticket, u8 binder[SHA256_HASH_LENGTH]) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    LTTlsSession   *session   = tlsCtx->sessionNegotiate;
    if (!handshake || !session) {
        return LTTLS_ERROR(0x0100 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    u32 key[SHA256_HASH_LENGTH / 4];
    u8 nonce[ticket->nonceLen];
    lt_memcpy(nonce, ticket->nonceTicket, sizeof(nonce));
    int ret = 0;
    // derive PSK, early secret, binder key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)handshake->psk, SHA256_HASH_LENGTH,
        (u8 *)session->resumptionMasterSecret, LTTLS_LBL_WITH_LEN(resumption), nonce, ticket->nonceLen))) {
        return LTTLS_ERROR(0x0100 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = HKDFExtract(tlsCtx, (u8 *)handshake->earlySecret, (u8 *)"", 0, (u8 *)handshake->psk, SHA256_HASH_LENGTH))) {
        return LTTLS_ERROR(0x0100 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = DeriveSecret(tlsCtx, (u8 *)key, SHA256_HASH_LENGTH,
        (u8 *)handshake->earlySecret, LTTLS_LBL_WITH_LEN(res_binder), (u8 *)"", 0, false))) {
        return LTTLS_ERROR(0x0100 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // derive finished key
    if (0 != (ret = HKDFExpand(tlsCtx, (u8 *)key, SHA256_HASH_LENGTH, (u8 *)key, LTTLS_LBL_WITH_LEN(finished), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x0100 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    u8 h[SHA256_HASH_LENGTH];
    if (0 != (ret = tlsCtx->config->crypto->GenDigestSHA256(data, len, h))) {
        return LTTLS_ERROR(0x0100 + 6, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    // derive PSK binder
    if (0 != (ret = GetHmac(tlsCtx, (u8 *)key, SHA256_HASH_LENGTH, h, SHA256_HASH_LENGTH, binder))) {
        return LTTLS_ERROR(0x0100 + 7, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    return 0;
}

// Error ID 0x0110
/**
 * @brief  Update session write secret, key and IV
 *
 * @param[in]  tlsCtx   The TLS context
 * @param[in]  bClient  If update client write serect and key
 * @return   Error code
 *
 *    The next-generation application_traffic_secret is computed as:
 *    application_traffic_secret_N+1 =
 *        HKDF-Expand-Label(application_traffic_secret_N, "traffic upd", "", Hash.length)
 */
static int UpdateSessionWriteKey(const LTTlsContext *tlsCtx, bool bClient) {
    LTTlsTransform *transform = tlsCtx->transformApplication;
    LTTlsSession   *session   = tlsCtx->sessionData;
    if (!transform || !session) {
        return LTTLS_ERROR(0x0110 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    u8 *secret = NULL;
    u8 *key    = NULL;
    u8 *iv     = NULL;
    if (bClient) {
        secret = (u8 *)session->clientApplicationTrafficSecret;
        key    = (u8 *)transform->clientWriteKey;
        iv     = (u8 *)transform->clientWriteIV;
        transform->outCounter = 0;

    } else {
        secret = (u8 *)session->serverApplicationTrafficSecret;
        key    = (u8 *)transform->serverWriteKey;
        iv     = (u8 *)transform->serverWriteIV;
        transform->inCounter = 0;
    }

    int ret = 0;
    if (0 != (ret = HKDFExpand(tlsCtx, secret, SHA256_HASH_LENGTH, secret, LTTLS_LBL_WITH_LEN(traffic_upd), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x0110 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = HKDFExpand(tlsCtx, key, AES128_KEY_LENGTH, secret, LTTLS_LBL_WITH_LEN(key), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x0110 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (0 != (ret = HKDFExpand(tlsCtx, iv, AES128_GCM_IV_LENGTH, secret, LTTLS_LBL_WITH_LEN(iv), (u8 *)"", 0))) {
        return LTTLS_ERROR(0x0110 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    return 0;
}

/******************************* End of cryptos ******************************/

/********************************** Common ************************************
 * Errors in these calls shall not trigger fatal alerts, but return error code.
 * Handshake and session control will use the error code to determine to send fatal alerts. */

/**
 * @brief Flush data in buffer to socket
 *
 * @param [in] hSocket  The handle of a net driver socket, not TLS socket
 * @param [in,out] buf  The pointer to the buffer of data to send
 * @param [in,out] len  The pointer to the length of data to send
 * @return  Error code
 *          On return, if data is partially flushed, buf and len point to the remaining bytes.
 */
int FlushBuffer(LTSocket hSocket, const u8 **buf, u16 *len) {
    int ret = 0;
    s32 writeLen = 0;
    const u8 *p = *buf;
    u16 left = *len;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
    while (left > 0) {
        writeLen = pSocket->WriteSocket(hSocket, p, left);

        // Success
        if (writeLen > 0) {
            left -= writeLen;
            p    += writeLen;
            continue;
        }

        // Obvious send error: wrong.
        if (writeLen < 0) {
            ret = -2;
            break;
        }

        // Unexplanable error: how and why.
        if (writeLen > left) {
            ret = -3;
            break;
        }

        // Write 0 because send buffer is full or not available temporarily.
        if (writeLen == 0) {
            ret = -4;
            break;
        }
    }
    *buf = p;
    *len = left;
    return ret;
}

// Error ID 0x0200
/**
 * @brief  Flush out records in buffer.
 *         It's guaranteed tlsCtx->resendLen is 0 here, by TLS WriteSocket and OnSocketEvent.
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int FlushOutput(LTTlsContext *tlsCtx) {
    /* Avoid incrementing counter if data is flushed */
    if (tlsCtx->outLeft == 0) return 0;

    /* outHdr points to the current record (not ready to flush out)
       In this function, outHdr points to the end of left bytes
       outLeft bytes before outHdr need to be flushed out.
       outBuf                           outHdr
        [         outLeft                 |                ]
     */
    if ((tlsCtx->outLeft > tlsCtx->outBufLen) || (tlsCtx->outHdr < tlsCtx->outBuf) ||
        (tlsCtx->outHdr < tlsCtx->outBuf + tlsCtx->outLeft)) {
        return LTTLS_ERROR(0x0200 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    const u8 *buf = tlsCtx->outHdr - tlsCtx->outLeft;
    u16 len = tlsCtx->outLeft;
    int ret = FlushBuffer(tlsCtx->config->hSocket, &buf, &len);
    if (ret == -4) {
        P("flush.left", "%d %u / %u", ret, len, tlsCtx->outLeft);
        tlsCtx->resendLen = len;
        tlsCtx->resendData = buf;
        ret = 0;
    }
    if (ret) ret = LTTLS_ERROR(0x0200 + (-ret), MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    // Regardless error or success, set outHdr to outBuf and outLen to 0 as if all data is flushed.
    tlsCtx->outHdr = tlsCtx->outBuf;
    tlsCtx->outLeft = 0;
    return ret;
}

/**
 * @brief  Free memory allocated to pTicket.
 *         NEED to set pTicket to NULL after returning to caller
 *
 * @param[in,out] ticket  The ticket to free
 */
static void FreeTicket(LTTlsSessionTicket *ticket) {
    if (!ticket) {
        return;
    }
    lt_memset(ticket->nonceTicket, 0, ticket->nonceLen + ticket->ticketLen);
    lt_free(ticket->nonceTicket);
    lt_memset(ticket, 0, sizeof(LTTlsSessionTicket));
}

/**
 * @brief  Free memory allocated to pSession.
 *         NEED to set pSession to NULL after returning to caller
 *
 * @param[in,out] session  The session to free
 */
static void FreeSession(LTTlsContext *tlsCtx, LTTlsSession *session) {
    if (!tlsCtx || !session) {
        return;
    }
    LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
    if (keyManager) {
        keyManager->API->FreePublicKey(&session->peerPubKey);
        lt_destroyobject(keyManager);
    }
    FreeTicket(&session->ticket);
    lt_memset(session, 0, sizeof(LTTlsSession));
    lt_free(session);
}

/**
 * @brief  Free memory allocated to pHandshake.
 *         NEED to set pHandshake to NULL after returning to caller
 *
 * @param[in,out] handshake  The handshake to free
 */
static void FreeHandshake(LTTlsHandshake *handshake, LTSystemCrypto *crypto) {
    if (!handshake) {
        return;
    }
    crypto->DestroySeqSHA256(handshake->hashCtx);
    lt_free(handshake->cookie);
    lt_memset(handshake, 0, sizeof(LTTlsHandshake));
    lt_free(handshake);
}

/**
 * @brief  Free memory allocated to pTransform.
 *         NEED to set pTransform to NULL after returning to caller
 *
 * @param[in,out] transform  The transform to free
 */
static void FreeTransform(LTTlsTransform *transform) {
    if (!transform) {
        return;
    }
    lt_memset(transform, 0, sizeof(LTTlsTransform));
    lt_free(transform);
}

/**
 * @brief  Free memory allocated to certificate request
 *
 * @param[in,out] tlsCtx
 */
static void FreeCertRequest(LTTlsContext *tlsCtx) {
    lt_free(tlsCtx->certReqContext);
    tlsCtx->certReqContext = NULL;
}

// Error ID 0x0210
/** TODO need to see if disconnect and reconnect the TCP connection
 * @brief  Reset a TLS session, but not disconnect TCP
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return Error code
 */
static int ResetTls(LTTlsContext *tlsCtx) {
    FreeTls(tlsCtx);
    return InitTls(tlsCtx, NULL, NULL, true);
}

// Error ID 0x0220
/**
 * @brief  Write current record.
 *
 * @param[in,out] tlsCtx       The TLS context
 * @param[in]     bForceFlush  Force to flash
 * @return   Error code
 *
 * Uses:
 *  - tlsCtx->outMsgType: type of the message (AppData, Handshake, Alert, CCS)
 *  - tlsCtx->outMsgLen: length of the record payload (excl headers)
 *  - tlsCtx->outMsg: record payload
 *  - tlsCtx->outHdr: record header
 */
static int WriteRecord(LTTlsContext *tlsCtx, bool bForceFlush) {
    int ret = 0;
    P("write", "rec %u type %u len %u", tlsCtx->outMsgType, tlsCtx->outMsg[0], tlsCtx->outMsgLen);

    /* TLS 1.3 still uses the TLS 1.2 version identifier * for backwards compatibility.
     * Otherwise, use the minor version in ssl context.
     */
    u8 minorVersion = (tlsCtx->minorVer == MBEDTLS_SSL_MINOR_VERSION_4) ? MBEDTLS_SSL_MINOR_VERSION_3 : tlsCtx->minorVer;
    tlsCtx->outHdr[1] = tlsCtx->majorVer;
    tlsCtx->outHdr[2] = minorVersion;

    // if output need to be transformed to an encrypted record, but never encrypt CHANGE_CIPHER_SPEC.
    if (tlsCtx->transformOut && tlsCtx->outMsgType != MBEDTLS_SSL_MSG_CHANGE_CIPHER_SPEC) {
        // will change head version, out_msglen, out_msgtype, add tag
        if (0 != (ret = EncryptRecord(tlsCtx))) {
            return ret;
        }
    }

    // Now write the potentially updated record content in header (type and length). */
    tlsCtx->outHdr[0] = tlsCtx->outMsgType;
    tlsCtx->outHdr[3] = tlsCtx->outMsgLen >> 8;
    tlsCtx->outHdr[4] = tlsCtx->outMsgLen;

    // Update outbuf info
    LT_SIZE protectedRecordSize = tlsCtx->outMsgLen + LTTLS_RECORD_HEADER_LEN;
    tlsCtx->outLeft += protectedRecordSize;
    tlsCtx->outHdr  += protectedRecordSize;
    tlsCtx->outMsg   = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;

    if (bForceFlush && 0 != (ret = FlushOutput(tlsCtx))) {
        return ret;
    }

    return 0;
}

// Error ID 0x0230
/**
 * @brief  Send alert
 *
 * @param[in,out] tlsCtx      The TLS context
 * @param[in]     level       The alert level, fatal or warning
 * @param[in]     description The alert des
 * @return int
 */
static int SendAlertMessage(LTTlsContext *tlsCtx, u8 level, u8 descryption) {
    LTTLS_CHK_BUF_PTR(0x0230, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + 2);
    tlsCtx->outMsg     = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    tlsCtx->outMsgType = MBEDTLS_SSL_MSG_ALERT;
    tlsCtx->outMsgLen  = 2;
    tlsCtx->outMsg[0]  = level;
    tlsCtx->outMsg[1]  = descryption;

    return WriteRecord(tlsCtx, true);
}

/**
 * @brief  Send a fatal alert based on an error code
 *
 * @param[in,out]  tlsCtx    The TLS context
 * @param[in]      error  The error code that triggers an alert.
 */
static void SendFatalAlert(LTTlsContext *tlsCtx, int error) {
    u8 alert = 0;
    switch ((-error) & 0xFF80) {
        case -MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE :
        case -MBEDTLS_ERR_SSL_UNEXPECTED_RECORD :
            alert = MBEDTLS_SSL_ALERT_MSG_UNEXPECTED_MESSAGE;
            break;

        case -MBEDTLS_ERR_SSL_INVALID_MAC :
            alert = MBEDTLS_SSL_ALERT_MSG_BAD_RECORD_MAC;
            break;

        case -MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE :
            alert = MBEDTLS_SSL_ALERT_MSG_HANDSHAKE_FAILURE;
            break;

        case -MBEDTLS_ERR_SSL_BAD_CERTIFICATE :
            alert = MBEDTLS_SSL_ALERT_MSG_BAD_CERT;
            break;

        case -MBEDTLS_ERR_SSL_CERTIFICATE_REVOKED :
            alert = MBEDTLS_SSL_ALERT_MSG_CERT_REVOKED;
            break;

        case -MBEDTLS_ERR_SSL_CERTIFICATE_EXPIRED :
            alert = MBEDTLS_SSL_ALERT_MSG_CERT_EXPIRED;
            break;

        case -MBEDTLS_ERR_SSL_CERTIFICATE_UNKNOWN :
            alert = MBEDTLS_SSL_ALERT_MSG_CERT_UNKNOWN;
            break;

        case -MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER :
            alert = MBEDTLS_SSL_ALERT_MSG_ILLEGAL_PARAMETER;
            break;

        case -MBEDTLS_ERR_SSL_UNKNOWN_CA :
            alert = MBEDTLS_SSL_ALERT_MSG_UNKNOWN_CA;
            break;

        case -MBEDTLS_ERR_SSL_ACCESS_DENIED :
            alert = MBEDTLS_SSL_ALERT_MSG_ACCESS_DENIED;
            break;

        case -MBEDTLS_ERR_SSL_DECODE_ERROR :
            alert = MBEDTLS_SSL_ALERT_MSG_DECODE_ERROR;
            break;

        case -MBEDTLS_ERR_SSL_DECRYPT_ERROR :
            alert = MBEDTLS_SSL_ALERT_MSG_DECRYPT_ERROR;
            break;

        case -MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION :
            alert = MBEDTLS_SSL_ALERT_MSG_PROTOCOL_VERSION;
            break;

        case -MBEDTLS_ERR_SSL_INTERNAL_ERROR :
        case -MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL :
        case -MBEDTLS_ERR_SSL_BAD_INPUT_DATA :
        case -MBEDTLS_ERR_SSL_INVALID_RECORD :
        case -MBEDTLS_ERR_SSL_ALLOC_FAILED :
            alert = MBEDTLS_SSL_ALERT_MSG_INTERNAL_ERROR;
            break;

        case -MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION :
            alert = MBEDTLS_SSL_ALERT_MSG_UNSUPPORTED_EXT;
            break;

        default :
            // ignore all other errors
            ;

    }

    if (!alert) {
        SendAlertMessage(tlsCtx, MBEDTLS_SSL_ALERT_LEVEL_FATAL, alert);
    }
}

// Error ID 0x0240
/**
 * @brief  Fetch next record from either
 *         (1) recving new data in buffer when no more old data in buffer, or
 *         (2) moving to the next record in buffer.
 *         Ignore cipher change specs and handle alerts.
 *         Then, set nInRecType, pInRecPayload, nInRecPayloadLen, nInRecDataLeft, pInRecEnd,
 *
 * @param[in,out] tlsCtx
 * @return   Error code
 */
static int FetchNextRecord(LTTlsContext *tlsCtx) {
    int ret = 0;

    // When reaching this function, buffer is already filled with one complete record and nInBufLen is set.
    tlsCtx->inBufDataLeft = tlsCtx->inBufLen;
    tlsCtx->inBufEnd = tlsCtx->inBuf + tlsCtx->inBufLen;
    tlsCtx->inRecHdr = tlsCtx->inBuf;

    LTTLS_CHK_BUF_READ_PTR(0x0240 + 1, tlsCtx->inRecHdr, tlsCtx->inBufEnd, LTTLS_RECORD_HEADER_LEN);

    // check record type
    tlsCtx->inRecType = tlsCtx->inRecHdr[0];
    if (tlsCtx->inRecType != MBEDTLS_SSL_MSG_HANDSHAKE &&
        tlsCtx->inRecType != MBEDTLS_SSL_MSG_ALERT &&
        tlsCtx->inRecType != MBEDTLS_SSL_MSG_CHANGE_CIPHER_SPEC &&
        tlsCtx->inRecType != MBEDTLS_SSL_MSG_APPLICATION_DATA) {
        return LTTLS_ERROR(0x0240 + 1, MBEDTLS_ERR_SSL_UNEXPECTED_RECORD);
    }

    // check record version
    if (tlsCtx->inRecHdr[1] != tlsCtx->majorVer || tlsCtx->inRecHdr[2] > tlsCtx->minorVer) {
        return LTTLS_ERROR(0x0240 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    // check record length
    tlsCtx->inRecPayload = tlsCtx->inRecHdr + LTTLS_RECORD_HEADER_LEN;
    tlsCtx->inRecPayloadLen = (((u16)tlsCtx->inRecHdr[3]) << 8) + tlsCtx->inRecHdr[4];

    // record payload must be in InBuf, and cannot be longer than InBuf.
    if (tlsCtx->inRecPayload > tlsCtx->inBufEnd || tlsCtx->inRecPayloadLen > LTTLS_IN_BUFFER_LEN - LTTLS_RECORD_HEADER_LEN) {
        return LTTLS_ERROR(0x0240 + 3, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    // set inRecNextHdr and inBufDataLeft, because inRecLen may be reduced after decryption
    tlsCtx->inRecNextHdr = tlsCtx->inRecPayload + tlsCtx->inRecPayloadLen;
    tlsCtx->inBufDataLeft -= tlsCtx->inRecPayloadLen + LTTLS_RECORD_HEADER_LEN;

    // if needed, decrypt the record (update record type and payload length)
    if (tlsCtx->transformIn) {
        if (0 != (ret = DecryptRecord(tlsCtx))) {
            return ret;
        }

        if (tlsCtx->inRecPayloadLen == 0) {
            ++tlsCtx->inZero;
            /* Three or more empty messages may be a DoS attack
             * (excessive CPU consumption). */
            if (tlsCtx->inZero > 3) {
                return LTTLS_ERROR(0x0240, MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE);
            }

        } else {
            tlsCtx->inZero = 0;
        }
    }

    // update actual record end
    tlsCtx->inRecDataLeft = tlsCtx->inRecPayloadLen;
    tlsCtx->inRecEnd = tlsCtx->inRecPayload + tlsCtx->inRecPayloadLen;

    // handle particular types of records
    switch (tlsCtx->inRecType) {
        case MBEDTLS_SSL_MSG_HANDSHAKE:
        case MBEDTLS_SSL_MSG_APPLICATION_DATA:
            break;

        case MBEDTLS_SSL_MSG_ALERT:
            /* Only support one 2-byte alert */
            if (tlsCtx->inRecPayloadLen != 2) {
                return LTTLS_ERROR(0x0240 + 4, MBEDTLS_ERR_SSL_DECODE_ERROR);
            }
            /* Fatal and critical alerts, except close_notify and no_renegotiation */
            if (tlsCtx->inRecPayload[0] == MBEDTLS_SSL_ALERT_LEVEL_FATAL) {
                tlsCtx->nRecvAlertDescryption = tlsCtx->inRecPayload[1];
                LTLOG_REDALERT("alert", "fatal alert %u", tlsCtx->nRecvAlertDescryption);
                return LTTLS_ERROR(0x0240, MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE);
            }
            if (tlsCtx->inRecPayload[0] == MBEDTLS_SSL_ALERT_LEVEL_WARNING &&
                (tlsCtx->inRecPayload[1] == MBEDTLS_SSL_ALERT_MSG_CLOSE_NOTIFY ||
                 tlsCtx->inRecPayload[1] == MBEDTLS_SSL_ALERT_MSG_USER_CANCELED)) {
                /* close_notify:  This alert notifies the recipient that the sender will
                 * not send any more messages on this connection.  Any data received
                 * after a closure alert has been received MUST be ignored.
                 *
                 * user_canceled:  This alert notifies the recipient that the sender is
                 * canceling the handshake for some reason unrelated to a protocol
                 * failure.  If a user cancels an operation after the handshake is
                 * complete, just closing the connection by sending a "close_notify"
                 * is more appropriate.  This alert SHOULD be followed by a
                 * "close_notify".  This alert generally has AlertLevel=warning.
                 */
                return LTTLS_ERROR(0x0240, MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);
            }
            // Ignore non-fatal alerts
            return LTTLS_ERROR(0x0240, MBEDTLS_ERR_SSL_NON_FATAL);
            break;

        default:
            return LTTLS_ERROR(0x0240 + 2, MBEDTLS_ERR_SSL_UNEXPECTED_RECORD);
    }

    return 0;
}

/******************************* End of common ******************************/

/**************************** Handshake Sending ******************************
 * Errors in these calls shall not trigger fatal alerts, but return error code.
 * Handshake and session control will use the error code to determine to send fatal alerts. */

/**
 * @brief  Fill in the OutMsg fields for the handshake packet before writing record
 *         The OutMsg is the handshake header.
 *
 * @param[in,out] tlsCtx  The TLS context
 * @param[in]     msgLen  The length of handshake payload
 * @param[in]     hsType  The type of handshake
 */
static void FinishHandshakeMsg(LTTlsContext *tlsCtx, u16 msgLen, u8 hsType) {
    tlsCtx->outMsg[0]  = hsType;
    tlsCtx->outMsg[1]  = 0;
    tlsCtx->outMsg[2]  = msgLen >> 8;
    tlsCtx->outMsg[3]  = msgLen;
    tlsCtx->outMsgType = MBEDTLS_SSL_MSG_HANDSHAKE;
    tlsCtx->outMsgLen  = msgLen + LTTLS_HANDSHAKE_HEADER_LEN;
}

// Error ID 0x0300
/**
 * @brief Write cipher suite
 *
 * @param[out] buf     The buffer to write to
 * @param[out] end     The end of buffer
 * @param[out] outLen  The resulting length of written
 * @return   Error code
 */
static int WriteCipherSuites(u8 *buf, u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    /* Check there is space for the cipher suite list length (2 bytes). */
    LTTLS_CHK_BUF_PTR(0x0300 + 1, p, end, 2);
    p += 2;

    /* Write cipher_suites */
    u16 cipherSuite;
    u16 cipherSuitesLen = 0;
    for (LT_SIZE i = 0; s_CiphersuiteList[i] != 0; ++i) {
        cipherSuite = s_CiphersuiteList[i];
        LTTLS_CHK_BUF_PTR(0x0300 + 2, p, end, 2);
        p[0] = cipherSuite >> 8;
        p[1] = cipherSuite;
        p += 2;
        cipherSuitesLen += 2;
    }

    /* Write the cipher_suites length in number of bytes */
    buf[0] = cipherSuitesLen >> 8;
    buf[1] = cipherSuitesLen;

    /* Output the total length of cipher_suites field. */
    *outLen = p - buf;
    return 0;
}

// Error ID 0x0308
/**
 * @brief Write supported group extension
 *
 * @param[in]  tlsCtx
 * @param[out] buf
 * @param[out] end
 * @param[out] outLen
 * @return   Error code
 *
 * pSsl_tls13_write_supported_versions_ext():
 *
 * struct {
 *      ProtocolVersion versions<2..254>;
 * } SupportedVersions;
 */
static int WriteSupportedVersionsExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    /* Check if we have space to write the extension:
     * - extension_type         (2 bytes)
     * - extension_data_length  (2 bytes)
     * - versions_length        (1 byte )
     * - versions               (2 bytes)
     */
    LTTLS_CHK_BUF_PTR(0x0308, p, end, 7);
    p[0] = 0;               // extension_type,      0x002B
    p[1] = MBEDTLS_TLS_EXT_SUPPORTED_VERSIONS;
    p[2] = 0;               // length of extension, 0x0003
    p[3] = 3;
    p[4] = 2;               // length of versions,  0x02
    p[5] = tlsCtx->majorVer;  // supported versions,  0x0304
    p[6] = tlsCtx->minorVer;

    *outLen = 7;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_SUPPORTED_VERSIONS;
    return 0;
}

// Error ID 0x0310
/**
 * @brief  Write max fragment length extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 *   RFC 8066
 *   enum{
 *       2^9(1), 2^10(2), 2^11(3), 2^12(4), (255)
 *   } MaxFragmentLength;
 */
static int WriteMaxFragLenExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    LTTLS_CHK_BUF_PTR(0x0310, p, end, 5);
    p[0] = 0;                // extension_type,      0x0001
    p[1] = MBEDTLS_TLS_EXT_MAX_FRAGMENT_LENGTH;
    p[2] = 0;                // length of extension, 0x0001
    p[3] = 1;
    p[4] = 3;                // 2048 Bytes

    *outLen = 5;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_MAX_FRAGMENT_LENGTH;
    return 0;
}

// Error ID 0x0318
/**
 * @brief  Write ALPN extension, Application-Layer Protocol Negotiation
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 * RFC7301
 *   enum {
 *       application_layer_protocol_negotiation(16), (65535)
 *   } ExtensionType;
 *
 *   The "extension_data" field of the
 *   ("application_layer_protocol_negotiation(16)") extension SHALL
 *   contain a "ProtocolNameList" value.
 *
 *   opaque ProtocolName<1..2^8-1>;
 *
 *   struct {
 *       ProtocolName protocol_name_list<2..2^16-1>
 *   } ProtocolNameList;
 *
 *   ext type (2B) is always 0x10.
 *   ext length (2B) is 2 + nAlpnLen.
 *   each protocol name is {nNameLen (1B), Name[nNameLen]}.
 *   nAlpnLen (2B) = sum(1 + nNameLen)
 */
static int WriteAlpnExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    if (!tlsCtx->options->alpnExt) {
        // We can ignore this extension if no ALPN extension data available
        *outLen = 0;
        return 0;
    }
    u8 *p = buf;
    u16 extLen = 2 + tlsCtx->options->alpnExt->alpnLen;    // exclude the extension header (4B) : type (2B) + len (2B)
    LTTLS_CHK_BUF_PTR(0x0318, p, end, 4 + extLen);
    p[0] = 0;                                               // extension_type, 0x0010
    p[1] = MBEDTLS_TLS_EXT_ALPN;
    p[2] = extLen >> 8;                                    // length of extension body
    p[3] = extLen & 0xFF;
    p[4] = tlsCtx->options->alpnExt->alpnLen >> 8;          // length of alpn names
    p[5] = tlsCtx->options->alpnExt->alpnLen & 0xFF;
    p += 6;
    lt_memcpy(p, tlsCtx->options->alpnExt->protocolNames, tlsCtx->options->alpnExt->alpnLen);
    *outLen = 4 + extLen;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_ALPN;
    return 0;
}

// Error ID 0x0320
/**
 * @brief  Write server name indication extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 * Sect. 3, RFC 6066 (TLS Extensions Definitions)
 *
 * In order to provide any of the server names, clients MAY include an
 * extension of type "server_name" in the (extended) client hello. The
 * "extension_data" field of this extension SHALL contain
 * "ServerNameList" where:
 *
 * struct {
 *     NameType name_type;
 *     select (name_type) {
 *         case host_name: HostName;
 *     } name;
 * } ServerName;
 *
 * enum {
 *     host_name(0), (255)
 * } NameType;
 *
 * opaque HostName<1..2^16-1>;
 *
 * struct {
 *     ServerName server_name_list<1..2^16-1>
 * } ServerNameList;
 */
static int WriteServerNameIndicationExt(const LTTlsContext *tlsCtx, u8 *buf, const u8 *end, LT_SIZE *outLen) {
    if (!tlsCtx->options->serverName) {
        // We can ignore this extension if no server name available
        *outLen = 0;
        return 0;
    }

    u8 *p = buf;
    LT_SIZE nHostnameLen = lt_strlen(tlsCtx->options->serverName);

    LTTLS_CHK_BUF_PTR(0x0320, p, end, nHostnameLen + 9);
    p[0] = 0;
    p[1] = MBEDTLS_TLS_EXT_SERVERNAME_HOSTNAME; // extension type, 0x0000
    p[2] = (nHostnameLen + 5) >> 8;             // extension length
    p[3] = (nHostnameLen + 5);
    p[4] = (nHostnameLen + 3) >> 8;             // length of first entry in the list
    p[5] = (nHostnameLen + 3);
    p[6] = 0;                                   // server name type, 0
    p[7] = nHostnameLen >> 8;                   // length of server name
    p[8] = nHostnameLen;
    lt_memcpy(p + 9, tlsCtx->options->serverName, nHostnameLen);

    *outLen = nHostnameLen + 9;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_SERVERNAME;
    return 0;
}

// Error ID 0x0328
/** TODO need to validate
 * @brief Write cookie extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 *   struct {
 *       opaque cookie<1..2^16-1>;
 *   } Cookie;
 */
static int WriteCookieExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (handshake->cookie == NULL) {
        return 0;
    }
    u8 *p = buf;
    *outLen = 0;
    LTTLS_CHK_BUF_PTR(0x0328, p, end, handshake->hrrCookieLen + 6);
    p[0] = MBEDTLS_TLS_EXT_COOKIE >> 8;
    p[1] = MBEDTLS_TLS_EXT_COOKIE;
    p[2] = (handshake->hrrCookieLen + 2) >> 8;
    p[3] = (handshake->hrrCookieLen + 2);
    p[4] = handshake->hrrCookieLen >> 8;
    p[5] = handshake->hrrCookieLen;
    /* Cookie */
    lt_memcpy(p + 6, handshake->cookie, handshake->hrrCookieLen);
    *outLen = handshake->hrrCookieLen + 6;
    handshake->extensionsPresent |= LTTLS_EXT_COOKIE;
    return 0;
}

// Error ID 0x0330
/**
 * @brief  Write key share extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 * Structure of key_share extension in ClientHello:
 *
 *  struct {
 *          NamedGroup group;
 *          opaque key_exchange<1..2^16-1>;
 *      } KeyShareEntry;
 *  struct {
 *          KeyShareEntry client_shares<0..2^16-1>;
 *      } KeyShareClientHello;
 *
 *   We only support ECDHE_X25519
 *   .keyShare = {
 *       {
 *           .eExtensionType    = LT_BE16(kLTTLSExtensionType_KeyShare),
 *           .nExtensionDataLen = LT_BE16(38),
 *       },
 *       .nKeyShareLen = LT_BE16(36),
 *       .eNamedGroup  = LT_BE16(kLTTLSNamedGroupType_X25519),
 *       .nKeyLen      = LT_BE16(32),
 *   },
 */
static int WriteKeyShareExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    LTTLS_CHK_BUF_PTR(0x0330, p, end, ECDHE_KEY_LENGTH + 10);

    int ret = 0;
    if (0 != (ret = GenKeyECDHE(tlsCtx, (const u8 *)tlsCtx->config->crypto->GetCryptoConsts()->kX25519_BP, p + 10))) {
        return LTTLS_ERROR(0x0330, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    p[0] = 0;
    p[1] = MBEDTLS_TLS_EXT_KEY_SHARE;
    p[2] = 0;
    p[3] = ECDHE_KEY_LENGTH + 6;
    p[4] = 0;
    p[5] = ECDHE_KEY_LENGTH + 4;
    p[6] = 0;
    p[7] = MBEDTLS_SSL_IANA_TLS_GROUP_X25519;
    p[8] = 0;
    p[9] = ECDHE_KEY_LENGTH;
    tlsCtx->handshake->offeredGroupId = MBEDTLS_SSL_IANA_TLS_GROUP_X25519;

    *outLen = ECDHE_KEY_LENGTH + 10;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_KEY_SHARE;
    return 0;
}

// Error ID 0x0338
/**
 * @brief  Write supported group extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 * Function for writing a supported groups (TLS 1.3) or supported elliptic
 * curves (TLS 1.2) extension.
 *
 * The "extension_data" field of a supported groups extension contains a
 * "NamedGroupList" value (TLS 1.3 RFC8446):
 *      enum {
 *          secp256r1(0x0017), secp384r1(0x0018), secp521r1(0x0019),
 *          x25519(0x001D), x448(0x001E),
 *          ffdhe2048(0x0100), ffdhe3072(0x0101), ffdhe4096(0x0102),
 *          ffdhe6144(0x0103), ffdhe8192(0x0104),
 *          ffdhe_private_use(0x01FC..0x01FF),
 *          ecdhe_private_use(0xFE00..0xFEFF),
 *          (0xFFFF)
 *      } NamedGroup;
 *      struct {
 *          NamedGroup named_group_list<2..2^16-1>;
 *      } NamedGroupList;
 *
 * The "extension_data" field of a supported elliptic curves extension contains
 * a "NamedCurveList" value (TLS 1.2 RFC 8422):
 * enum {
 *      deprecated(1..22),
 *      secp256r1 (23), secp384r1 (24), secp521r1 (25),
 *      x25519(29), x448(30),
 *      reserved (0xFE00..0xFEFF),
 *      deprecated(0xFF01..0xFF02),
 *      (0xFFFF)
 *  } NamedCurve;
 * struct {
 *      NamedCurve named_curve_list<2..2^16-1>
 *  } NamedCurveList;
 *
 *   We only support X25519
 *   .namedGroup = {
 *       {
 *           .eExtensionType    = LT_BE16(kLTTLSExtensionType_SupportedGroups),
 *           .nExtensionDataLen = LT_BE16(4),
 *       },
 *       .nNamedGroupLen = LT_BE16(2),
 *       .eNamedGroup    = LT_BE16(kLTTLSNamedGroupType_X25519)
 *   },
 */
static int WriteSupportedGroupsExt(const LTTlsContext *tlsCtx, u8 *buf, const u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    /* Check if we have space for header and length fields:
     * - extension_type            (2 bytes)
     * - extension_data_length     (2 bytes)
     * - named_group_list_length   (2 bytes)
     */
    LTTLS_CHK_BUF_PTR(0x0338, p, end, 8);

    p[0] = 0;
    p[1] = MBEDTLS_TLS_EXT_SUPPORTED_GROUPS;
    p[2] = 0;
    p[3] = 4;
    p[4] = 0;
    p[5] = 2;
    p[6] = 0;
    p[7] = MBEDTLS_SSL_IANA_TLS_GROUP_X25519;

    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_SUPPORTED_GROUPS;
    *outLen = 8;
    return 0;
}

// Error ID 0x0340
/**
 * @brief  Write signature algorithm extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 * Function for writing a signature algorithm extension.
 *
 * The `extension_data` field of signature algorithm contains  a `SignatureSchemeList`
 * value (TLS 1.3 RFC8446):
 *      enum {
 *         ....
 *        ecdsa_secp256r1_sha256( 0x0403 ),
 *        ecdsa_secp384r1_sha384( 0x0503 ),
 *        ecdsa_secp521r1_sha512( 0x0603 ),
 *         ....
 *      } SignatureScheme;
 *
 *      struct {
 *         SignatureScheme supported_signature_algorithms<2..2^16-2>;
 *      } SignatureSchemeList;
 *
 * The TLS 1.3 signature algorithm extension was defined to be a compatible
 * generalization of the TLS 1.2 signature algorithm extension.
 * `SignatureAndHashAlgorithm` field of TLS 1.2 can be represented by
 * `SignatureScheme` field of TLS 1.3
 *
 *   We support ECDSA-P256-SHA256 and ED25519
 *   .signatureScheme = {
 *   {
 *       .eExtensionType    = LT_BE16(MBEDTLS_TLS_EXT_SIG_ALG),
 *       .nExtensionDataLen = LT_BE16(4),
 *   },
 *   .nSignatureAlgorithmLen = LT_BE16(2),
 *   .eSignatureAlgorithm    = LT_BE16(SIGNATURE_ECDSA_SECP256R1_SHA256), LT_BE16(SIGNATURE_ED25519),
 *   },
 */
static int WriteSigAlgExt(const LTTlsContext *tlsCtx, u8 *buf, const u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    /* Check if we have space for header and length field:
     * - extension_type         (2 bytes)
     * - extension_data_length  (2 bytes)
     * - supported_signature_algorithms_length   (2 bytes)
     */
    LTTLS_CHK_BUF_PTR(0x0340, p, end, 10);

    p[0] = 0;
    p[1] = MBEDTLS_TLS_EXT_SIG_ALG;
    p[2] = 0;
    p[3] = 6;
    p[4] = 0;
    p[5] = 4;
    p[6] = SIGNATURE_ECDSA_SECP256R1_SHA256 >> 8;
    p[7] = SIGNATURE_ECDSA_SECP256R1_SHA256 & 0xFF;
    p[8] = SIGNATURE_ED25519 >> 8;
    p[9] = SIGNATURE_ED25519 & 0xFF;

    *outLen = 10;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_SIG_ALG;
    return 0;
}

// Error ID 0x0348
/**
 * @brief  Write ec point format extension, RFC8422, https://www.rfc-editor.org/rfc/rfc8422.html
 *
 *       enum {
 *           elliptic_curves(10),
 *           ec_point_formats(11)
 *       } ExtensionType;
 *
 *       enum {
 *           uncompressed (0),
 *           deprecated (1..2),
 *           reserved (248..255)
 *       } ECPointFormat;
 *       struct {
 *           ECPointFormat ec_point_format_list<1..2^8-1>
 *       } ECPointFormatList;
 *
 *       00 0B 00 02 01 00
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 */
static int WriteEcPointFormatExt(const LTTlsContext *tlsCtx, u8 *buf, const u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    /* Check if we have space for header and length field:
     * - extension_type         (2 bytes)
     * - extension_data_length  (2 bytes)
     * - format_length          (1 byte)
     * - formats
     */
    LTTLS_CHK_BUF_PTR(0x0348, p, end, 6);

    p[0] = 0;
    p[1] = MBEDTLS_TLS_EXT_SUPPORTED_POINT_FORMATS;
    p[2] = 0;
    p[3] = 2;
    p[4] = 1;
    p[5] = 0;

    *outLen = 6;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_POINT_FORMAT;
    return 0;
}

// Error ID 0x0350
/**
 * @brief  Write PSK key exchange mode extension
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The buffer to write
 * @param[out] end    The end of buffer
 * @param[out] outLen The resulting length of written data
 * @return  Error code
 *
 *   enum { psk_ke(0), psk_dhe_ke(1), (255) } PskKeyExchangeMode;
 *
 *   struct {
 *       PskKeyExchangeMode ke_modes<1..255>;
 *   } PskKeyExchangeModes;
 */
static int WritePskKexModeExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen) {
    u8 *p = buf;
    *outLen = 0;

    LTTLS_CHK_BUF_PTR(0x0350, p, end, 6);
    p[0] = 0;                // extension_type,      0x002D
    p[1] = MBEDTLS_TLS_EXT_PSK_KEY_EXCHANGE_MODES;
    p[2] = 0;                // length of extension, 0x0002
    p[3] = 2;
    p[4] = 1;                // length of psk key exchange mode,  0x01
    p[5] = 1;                // psk_dhe_ke

    *outLen = 6;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_PSK_KEY_EXCHANGE_MODES;
    return 0;
}

/**
 * @brief  Test if a ticket is valid
 *
 * @param[in]  ticket    The ticket
 * @param[out] ticketAge The ticket age
 * @retval true (valid)
 * @retval false (invalid)
 */
static bool TicketIsValid(const LTTlsSessionTicket *ticket, u32 *ticketAge) {
    if (!ticket->bValid || !ticket->nonceTicket) {
        return false;
    }

    LTTime age = LTTime_Subtract(LT_GetCore()->GetKernelTime(), ticket->receivedTime);
    if (LTTime_IsGreaterThan(age, LTTime_Seconds(ticket->lifeTime))) {
        return false;
    }

    *ticketAge = LTTime_GetMilliseconds(age);
    return true;
}

// Error ID 0x0358
/**
 * @brief  Write preshared key extension
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[out] buf     The buffer to write
 * @param[out] end     The end of buffer
 * @param[out] outLen  The resulting length of written data
 * @param[out] hello   The buffer holding the client hello so far, including handshake header and payload
 * @param[out] bodyLen The length of client hello body so far, excluding this PSK extension
 * @return  Error code
 *
 *   struct {
 *       opaque identity<1..2^16-1>;
 *       uint32 obfuscated_ticket_age;
 *   } PskIdentity;
 *
 *   opaque PskBinderEntry<32..255>;
 *
 *   struct {
 *       PskIdentity identities<7..2^16-1>;
 *       PskBinderEntry binders<33..2^16-1>;
 *   } OfferedPsks;
 *
 *   struct {
 *       select (Handshake.msg_type) {
 *           case client_hello: OfferedPsks;
 *           case server_hello: uint16 selected_identity;
 *       };
 *   } PreSharedKeyExtension;
 */
static int WritePreSharedKeyExt(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, LT_SIZE *outLen, u8 *hello, u16 bodyLen, u8 *extensionsLen) {
    *outLen = 0;
    if (!tlsCtx->sessionNegotiate) {
        return 0;
    }

    // find a valid ticket
    LTTlsSessionTicket *ticket = NULL;
    bool bValid = false;
    u32 ticketAge;

    ticket = &tlsCtx->sessionNegotiate->ticket;
    bValid = TicketIsValid(ticket, &ticketAge);
    if (!bValid) {
        return 0;
    }

    // now, we have a valid ticket
    u8 *p = buf;

    LTTLS_CHK_BUF_PTR(0x0358 + 1, p, end, 4);
    p[0] = 0;                // extension_type,      0x0029
    p[1] = MBEDTLS_TLS_EXT_PRE_SHARED_KEY;
    u16 ticketLen = ticket->ticketLen;
    u16 idLen = 2 + ticketLen + 4;        // ticket_len (2), ticket, ticket_age (4)
    u16 extLen = 2 + idLen + 3 + SHA256_HASH_LENGTH; // id_len (2), id, binder_len(2), hash_len(1), hash
    p[2] = extLen >> 8;
    p[3] = extLen;
    p += 4;

    // ticket
    LTTLS_CHK_BUF_PTR(0x0358 + 2, p, end, 2 + idLen);
    p[0] = idLen >> 8;      // identity length
    p[1] = idLen;
    p[2] = ticketLen >> 8;  // ticket length
    p[3] = ticketLen;
    p += 4;
    lt_memcpy(p, ticket->nonceTicket + ticket->nonceLen, ticketLen); // ticket
    p += ticketLen;
    ticketAge += ticket->ageAdd; // ticket age
    p[0] = ticketAge >> 24;
    p[1] = ticketAge >> 16;
    p[2] = ticketAge >> 8;
    p[3] = ticketAge;
    p += 4;

    // complete the handshake header first
    hello[0] = MBEDTLS_SSL_HS_CLIENT_HELLO;
    hello[1] = 0;
    bodyLen += 4 + extLen;   // ext_type (2), ext_len (2), ext
    hello[2] = bodyLen >> 8;
    hello[3] = bodyLen;

    // complete the extension length
    extLen = p - extensionsLen - 2 + 3 + SHA256_HASH_LENGTH;
    if (extensionsLen == 0) {
        return LTTLS_ERROR(0x0358 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }
    extensionsLen[0] = extLen >> 8;
    extensionsLen[1] = extLen;

    // calculate binder
    u8 binder[SHA256_HASH_LENGTH];
    if (0 != DerivePskBinder(tlsCtx, hello, p - hello, ticket, binder)) {
        return LTTLS_ERROR(0x0358 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    // write binder
    LTTLS_CHK_BUF_PTR(0x0358 + 3, p, end, 2 + 1 + SHA256_HASH_LENGTH);
    p[0] = 0;
    p[1] = 1 + SHA256_HASH_LENGTH;
    p[2] = SHA256_HASH_LENGTH;
    p += 3;
    lt_memcpy(p, binder, SHA256_HASH_LENGTH);
    p += SHA256_HASH_LENGTH;

    *outLen = p - buf;
    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_PRE_SHARED_KEY;
    return 0;
}

// Error ID 0x0360
/**
 * @brief  Write ClientHello handshake body
 *
 * @param[in]  tlsCtx The TLS context
 * @param[out] buf    The output buffer
 * @param[out] end    The end of buffer
 * @param[out] outLen The output body length
 * @return   Error code
 *
 * Structure of ClientHello message:
 *
 *    struct {
 *        ProtocolVersion legacy_version = 0x0303;    // TLS v1.2
 *        Random random;
 *        opaque legacy_session_id<0..32>;
 *        CipherSuite cipher_suites<2..2^16-2>;
 *        opaque legacy_compression_methods<1..2^8-1>;
 *        Extension extensions<8..2^16-1>;
 *    } ClientHello;
 */
static int WriteClientHelloBody(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, u16 *outLen) {
    int ret = 0;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /* Buffer management */
    u8 *p = buf;
    *outLen = 0;
    // check buffer has enough space.
    LTTLS_CHK_BUF_PTR(0x0360 + 1, p, end, 2 + LTTLS_CLIENT_HELLO_RANDOM_LEN + 1 + LTTLS_CLIENT_HELLO_RANDOM_LEN);

    /* Write legacy_version = 0x0303;    TLS 1.2 for TLS 1.3 */
    p[0] = p[1] = 0x03;
    p += 2;

    /* Write the random bytes ( random ).*/
    lt_memcpy(p, tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN);
    p += LTTLS_CLIENT_HELLO_RANDOM_LEN;

    /*
     * Write legacy_session_id
     * opaque legacy_session_id<0..32>;
     */
    *p = tlsCtx->sessionNegotiate->idLen;
    if (*p != 0) {
        lt_memcpy(p + 1, tlsCtx->sessionNegotiate->id, tlsCtx->sessionNegotiate->idLen);
    }
    p += 1 + LTTLS_CLIENT_HELLO_RANDOM_LEN;

    /* Write cipher_suites */
    LT_SIZE outputLen;
    if (0 != (ret = WriteCipherSuites(p, end, &outputLen))) return ret;
    p += outputLen;

    /* Write legacy_compression_methods
     *
     * For every TLS 1.3 ClientHello, this vector MUST contain exactly
     * one byte set to zero, which corresponds to the 'null' compression
     * method in prior versions of TLS.
     */
    LTTLS_CHK_BUF_PTR(0x0360 + 2, p, end, 2);
    p[0] = 1;
    p[1] = MBEDTLS_SSL_COMPRESS_NULL;
    p += 2;

    /* Write extensions */

    /* Keeping track of the included extensions */
    handshake->extensionsPresent = LTTLS_EXT_NONE;

    /* First write extensions, then the total length */
    LTTLS_CHK_BUF_PTR(0x0360 + 3, p, end, 2);
    u8 *extensionsLen = p;
    p += 2;

    /* Write supported_versions extension, mandatory with TLS 1.3. */
    if (0 != (ret = WriteSupportedVersionsExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    if (0 != (ret = WriteServerNameIndicationExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    if (0 != (ret = WriteMaxFragLenExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    /* Add ALPN extension if available */
    if (0 != (ret = WriteAlpnExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    /* Add the extensions related to signature */
    if (0 != (ret = WriteSigAlgExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    /* Add the extensions related to ECDSA */
    if (0 != (ret = WriteEcPointFormatExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    /* Add the extensions related to (EC)DHE ephemeral key establishment. */
    if (0 != (ret = WriteSupportedGroupsExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    if (0 != (ret = WriteKeyShareExt(tlsCtx, p, end, &outputLen))) {
        return ret;
    }
    p += outputLen;

    // on session resumption, we use a session ticket
    if (tlsCtx->eState == kLTTLSState_SR_CLIENT_HELLO) {
        if (0 != (ret = WritePskKexModeExt(tlsCtx, p, end, &outputLen))) {
            return ret;
        }
        p += outputLen;

        // The "pre_shared_key" extension MUST be the last extension in the ClientHello
        if (0 != (ret = WritePreSharedKeyExt(tlsCtx, p, end, &outputLen, buf - LTTLS_HANDSHAKE_HEADER_LEN, p - buf, extensionsLen))) {
            return ret;
        }
        p += outputLen;
    }

    // TODO Echo the cookie if the server provided one in its preceding HelloRetryRequest message.
    if (tlsCtx->bHrr) {
        if (0 != (ret = WriteCookieExt(tlsCtx, p, end, &outputLen))) {
            return ret;
        }
        p += outputLen;
    }

    /* Write the length of the list of extensions. */
    u16 extLen = p - extensionsLen - 2;

    if (extLen == 0) {
        return LTTLS_ERROR(0x0360, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    extensionsLen[0] = extLen >> 8;
    extensionsLen[1] = extLen;
    *outLen = p - buf;

    return 0;
}

// Error ID 0x0370
/**
 * @brief  Write client hello
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int WriteClientHello(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (!handshake) {
        return LTTLS_ERROR(0x0370 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }
    int ret = 0;
    u16 msgLen;

    // generate random numbers
    if (!tlsCtx->config->crypto->GenRandomBytes((u8 *)tlsCtx->randBytes, LTTLS_CLIENT_HELLO_RANDOM_LEN)) {
        return LTTLS_ERROR(0x0370 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    if (!tlsCtx->config->crypto->GenRandomBytes((u8 *)tlsCtx->sessionNegotiate->id, LTTLS_CLIENT_HELLO_RANDOM_LEN)) {
        return LTTLS_ERROR(0x0370 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }
    tlsCtx->sessionNegotiate->idLen = LTTLS_CLIENT_HELLO_RANDOM_LEN;

    // generate ephemeral private key
    if (!tlsCtx->config->crypto->GenRandomBytes((u8 *)handshake->ecdhePrivKey, ECDHE_KEY_LENGTH)) {
        return LTTLS_ERROR(0x0370 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    LTTLS_CHK_BUF_PTR(0x0370, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + LTTLS_HANDSHAKE_HEADER_LEN);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    if (0 != (ret = WriteClientHelloBody(tlsCtx, tlsCtx->outMsg + LTTLS_HANDSHAKE_HEADER_LEN,
        tlsCtx->outBufEnd, &msgLen))) {
        return ret;
    }
    FinishHandshakeMsg(tlsCtx, msgLen, MBEDTLS_SSL_HS_CLIENT_HELLO);

    // hash client hello. hash ctx shall be init already.
    if (0 != (ret = UpdateTranscript(tlsCtx, tlsCtx->outMsg, tlsCtx->outMsgLen))) {
        return LTTLS_ERROR(0x0370 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    // derive early secret
    if (0 != (ret = DeriveEarlySecrets(tlsCtx))) {
        return ret;
    }

    // set to TLS 1.0 first to write client hello record
    tlsCtx->minorVer = MBEDTLS_SSL_MINOR_VERSION_1;
    if (0 != (ret = WriteRecord(tlsCtx, true))) {
        return ret;
    }
    // then set back to TLS 1.3
    tlsCtx->minorVer = MBEDTLS_SSL_MINOR_VERSION_4;

    return 0;
}

// Error ID 0x0378
/**
 * @brief  Write change cipher spec, for compatibility purpose.
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 * {0x14,0x03,0x03,0x00,0x01,0x01};
 */
static int WriteChangeCipherSpec(LTTlsContext *tlsCtx) {
    // write change cipher spec body
    LTTLS_CHK_BUF_PTR(0x0378, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + 1);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    tlsCtx->outMsg[0] = 1;
    tlsCtx->outMsgLen = 1;
    tlsCtx->outMsgType = MBEDTLS_SSL_MSG_CHANGE_CIPHER_SPEC;

    /* Dispatch message */
    return WriteRecord(tlsCtx, false);
}

// Error ID 0x0380
/**
 * @brief Write certificate body (client certificate chain)
 *
 * @param[in]  tlsCtx  The TLS context
 * @param[out] buf     The output buffer
 * @param[out] end     The end of buffer
 * @param[out] outLen  The output body length
 * @return   Error code
 *
 *  enum {
 *        X509(0),
 *        RawPublicKey(2),
 *        (255)
 *    } CertificateType;
 *
 *    struct {
 *        select (certificate_type) {
 *            case RawPublicKey:
 *              // From RFC 7250 ASN.1_subjectPublicKeyInfo
 *              opaque ASN1_subjectPublicKeyInfo<1..2^24-1>;
 *
 *            case X509:
 *              opaque cert_data<1..2^24-1>;
 *        };
 *        Extension extensions<0..2^16-1>;
 *    } CertificateEntry;
 *
 *    struct {
 *        opaque certificate_request_context<0..2^8-1>;
 *        CertificateEntry certificate_list<0..2^24-1>;
 *    } Certificate;
 */
static int WriteCertificateBody(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, u16 *outLen) {
    u8 *p = buf;
    u8 *certReqContext = tlsCtx->certReqContext;
    u8 certReqContextLen = tlsCtx->certReqContextLen;

    /* ...
     * opaque certificate_request_context<0..2^8-1>;
     * ...
     */
    LTTLS_CHK_BUF_PTR(0x0380 + 1, p, end, certReqContextLen + 1);
    *p = certReqContextLen;
    ++p;
    if (certReqContextLen > 0) {
        lt_memcpy(p, certReqContext, certReqContextLen);
        p += certReqContextLen;
    }

    /* ...
     * CertificateEntry certificate_list<0..2^24-1>;
     * ...
     */
    u16 certDataLen = tlsCtx->options->clientCertificateLen;
    LTTLS_CHK_BUF_PTR(0x0380 + 2, p, end, certDataLen + 3);
    // only support total cert length < 2^16
    p[0] = 0;
    p[1] = certDataLen >> 8;
    p[2] = certDataLen;
    lt_memcpy(p + 3, tlsCtx->options->clientCertificate, certDataLen);
    *outLen = certDataLen + 4;
    return 0;
}

// Error ID 0x0388
/**
 * @brief  Put a client certificate in pOutBuf after receiving certificate request.
 *         Could happen in both handshake and session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
static int MakeClientCertificate(LTTlsContext *tlsCtx) {
    int ret = 0;
    u16 msgLen = 0;
    LTTLS_CHK_BUF_PTR(0x0388, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + LTTLS_HANDSHAKE_HEADER_LEN);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    if (0 != (ret = WriteCertificateBody(tlsCtx, tlsCtx->outMsg + LTTLS_HANDSHAKE_HEADER_LEN, tlsCtx->outBufEnd, &msgLen))) {
        return ret;
    }
    FinishHandshakeMsg(tlsCtx, msgLen, MBEDTLS_SSL_HS_CERTIFICATE);
    return 0;
}

// Error ID 0x0390
/**
 * @brief  Write certificate in handshake
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
static int WriteClientCertificate(LTTlsContext *tlsCtx) {
    int ret = 0;
    // received certificate request, so send client certificate
    if (0 != (ret = MakeClientCertificate(tlsCtx))) {
        return ret;
    }
    if (0 != (ret = UpdateTranscript(tlsCtx, tlsCtx->outMsg, tlsCtx->outMsgLen))) {
        return LTTLS_ERROR(0x0390, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    return WriteRecord(tlsCtx, false);
}

// Error ID 0x0398
/**
 * @brief Write Certificate Verify
 *
 * @param[in]  tlsCtx
 * @param[out] buf
 * @param[out] end
 * @param[out] outLen
 * @return   Error code
 *    struct {
 *        SignatureScheme algorithm;
 *        opaque signature<0..2^16-1>;
 *    } CertificateVerify;
 */
static int WriteCertificateVerifyBody(const LTTlsContext *tlsCtx, u8 *buf, u8 *end, u16 *outLen) {
    struct CltCertVerify {
        u8 toVerify[130];
        union {
            u8 eddsaSigVal[EdDSA_SIGNATURE_LENGTH];
            u8 ecdsaSigVal[ECDSA_P256_SIGNATURE_LENGTH];
        };
        u8 eddsaPubKey[EdDSA_KEY_LENGTH];
    };
    struct CltCertVerify *tmp = lt_malloc(sizeof(struct CltCertVerify));
    if (!tmp) return LTTLS_ERROR(0x0398, MBEDTLS_ERR_SSL_ALLOC_FAILED);

    int ret = 0;
    do {
        u8 *p = buf;
        u16 type = tlsCtx->options->clientPrivateKeyReference.keyType;

        /* Check there is space for the algorithm identifier (2 bytes) and the
        * signature length (2 bytes). */
        LTTLS_CHK_BUF_PTR_BREAK(0x0398 + 1, p, end, 4);
        p[0] = type >> 8;
        p[1] = type & 0xFF;
        u8 *sigLen = p + 2;
        p += 4;

        lt_memset(tmp->toVerify, 0x20, 64);
        lt_memcpy(tmp->toVerify + 64, LTTLS_LBL_WITH_LEN(client_cv));
        lt_memset(tmp->toVerify + 97, 0x00, 1);
        if (0 != (ret = GetTranscriptHash(tlsCtx, tmp->toVerify + 98))) {
            ret = LTTLS_ERROR(0x0398 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
            break;
        }

        if (type == SIGNATURE_ED25519) {
            LTSecureCryptoEd25519 *secEd25519 = lt_createobject(LTSecureCryptoEd25519);
            if (!secEd25519) {
                ret =  LTTLS_ERROR(0x0398 + 5, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
                break;
            }
            if (0 != (ret = secEd25519->API->Sign(&tlsCtx->options->clientPrivateKeyReference, tmp->toVerify, 130, tmp->eddsaSigVal, tmp->eddsaPubKey))) {
                ret = LTTLS_ERROR(0x0398 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
            }
            lt_destroyobject(secEd25519);
            if (0 != ret) break;

            LTTLS_CHK_BUF_PTR_BREAK(0x0398 + 2, p, end, EdDSA_SIGNATURE_LENGTH);

            sigLen[0] = EdDSA_SIGNATURE_LENGTH >> 8;
            sigLen[1] = EdDSA_SIGNATURE_LENGTH;

            lt_memcpy(p, tmp->eddsaSigVal, EdDSA_SIGNATURE_LENGTH);
            p += EdDSA_SIGNATURE_LENGTH;

        } else if (type == SIGNATURE_ECDSA_SECP256R1_SHA256) {
            LTSecureCryptoEcdsaP256 *secEcdsa = lt_createobject(LTSecureCryptoEcdsaP256);
            if (!secEcdsa) {
                ret =  LTTLS_ERROR(0x0398 + 6, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
                break;
            }
            if (0 != (ret = secEcdsa->API->Sign(&tlsCtx->options->clientPrivateKeyReference, tmp->toVerify, 130, (u8 *)tmp->ecdsaSigVal, NULL))) {
                ret = LTTLS_ERROR(0x0398 + 3, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
            }
            lt_destroyobject(secEcdsa);
            if (0 != ret) break;

            LTTLS_CHK_BUF_PTR_BREAK(0x0398 + 3, p, end, ECDSA_P256_SIGNATURE_LENGTH + 6 + 2);

            u16 slen = 0;
            // won't fail because buf end is checked already.
            LTSystemCryptoEncoder *cryptoEncoder = lt_createobject(LTSystemCryptoEncoder);
            if (!cryptoEncoder) {
                ret = LTTLS_ERROR(0x0398 + 4, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
                break;
            }
            cryptoEncoder->API->EncodeEcdsaSignature(tmp->ecdsaSigVal, ECDSA_P256_SIGNATURE_LENGTH, p, end - p, &slen);
            lt_destroyobject(cryptoEncoder);
            p += slen;

            sigLen[0] = slen >> 8;
            sigLen[1] = slen;
        }

        *outLen = p - buf;
        ret = 0;
    } while (0);

    lt_free(tmp);
    return ret;
}

// Error ID 0x03A0
/**
 * @brief  Make certificate verify
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int MakeCertificateVerify(LTTlsContext *tlsCtx) {
    int ret = 0;
    u16 msgLen = 0;
    LTTLS_CHK_BUF_PTR(0x03A0, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + LTTLS_HANDSHAKE_HEADER_LEN);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    if (0 != (ret = WriteCertificateVerifyBody(tlsCtx, tlsCtx->outMsg + LTTLS_HANDSHAKE_HEADER_LEN, tlsCtx->outBufEnd, &msgLen))) {
        return ret;
    }
    FinishHandshakeMsg(tlsCtx, msgLen, MBEDTLS_SSL_HS_CERTIFICATE_VERIFY);
    return 0;
}

// Error ID 0x03A8
/**
 * @brief  Write client certificate verify
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int WriteClientCertificateVerify(LTTlsContext *tlsCtx) {
    int ret = 0;
    if (0 != (ret = MakeCertificateVerify(tlsCtx))) {
        return ret;
    }
    if (0 != (ret = UpdateTranscript(tlsCtx, tlsCtx->outMsg, tlsCtx->outMsgLen))) {
        return LTTLS_ERROR(0x03A8, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    return WriteRecord(tlsCtx, false);
}

// Error ID 0x03B0
/**
 * @brief  Write client finished
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 *     struct {
 *         opaque verify_data[Hash.length];
 *     } Finished;
 */
static int WriteClientFinished(LTTlsContext *tlsCtx) {
    int ret = 0;

    LTTLS_CHK_BUF_PTR(0x03B0, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + LTTLS_HANDSHAKE_HEADER_LEN + SHA256_HASH_LENGTH);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    u8 *buf = tlsCtx->outMsg + LTTLS_HANDSHAKE_HEADER_LEN;

    u8 hmac[SHA256_HASH_LENGTH];
    if (0 != (ret = CalculateVerifyData(tlsCtx, MBEDTLS_SSL_IS_CLIENT, hmac))) {
        return ret;
    }
    lt_memcpy(buf, hmac, SHA256_HASH_LENGTH);
    FinishHandshakeMsg(tlsCtx, SHA256_HASH_LENGTH, MBEDTLS_SSL_HS_FINISHED);
    if (0 != (ret = UpdateTranscript(tlsCtx, tlsCtx->outMsg, tlsCtx->outMsgLen))) {
        return LTTLS_ERROR(0x03B0, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    if (0 != (ret = KeyScheduleAfterClientFinished(tlsCtx))) {
        return ret;
    }

    return WriteRecord(tlsCtx, true);
}

/************************* End of handshake sending **************************/

/**************************** Handshake recving *******************************
 * Errors in these calls shall not trigger fatal alerts, but return error code.
 * Handshake and session control will use the error code to determine to send fatal alerts. */

// Error ID 0x0400
/**
 * @brief  Fetch the next handshake packet. Shall only be called by PeekNextHandshakeMsg or ReadNextHandshakeMsg
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
static int FetchNextHandshakeMsg(LTTlsContext *tlsCtx) {
    int ret = 0;
    LTTlsHandshake *pHandshake = tlsCtx->handshake;
    if (tlsCtx->inRecDataLeft == 0) {
        do {
            ret = FetchNextRecord(tlsCtx);
        } while ((-MBEDTLS_ERR_SSL_NON_FATAL) == ((-ret) & 0xFF80));
        if (0 != ret) {
            return ret;
        }

        pHandshake->inHsHdr = tlsCtx->inRecHdr + LTTLS_RECORD_HEADER_LEN;

    } else {
        pHandshake->inHsHdr = pHandshake->inHsNextHdr;
    }

    /* Jump handshake header (4 bytes, see Section 4 of RFC 8446).
     *    ...
     *    HandshakeType msg_type;
     *    uint24 length;
     *    ... */
    LTTLS_CHK_BUF_READ_PTR(0x0400 + 1, pHandshake->inHsHdr, tlsCtx->inRecEnd, LTTLS_HANDSHAKE_HEADER_LEN);
    // we only support < 2^16 bytes of handshake data.
    if (pHandshake->inHsHdr[1] != 0) {
        return LTTLS_ERROR(0x0400 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }
    pHandshake->inHsLen = LTTLS_HANDSHAKE_HEADER_LEN +
        (((LT_SIZE)pHandshake->inHsHdr[2]) << 8) + pHandshake->inHsHdr[3];
    LTTLS_CHK_BUF_READ_PTR(0x0400 + 3, pHandshake->inHsHdr, tlsCtx->inRecEnd, pHandshake->inHsLen);

    P("read.hs", "rec %u msg %u len %u", tlsCtx->inRecType, pHandshake->inHsHdr[0], pHandshake->inHsLen);

    pHandshake->inHsNextHdr = pHandshake->inHsHdr + pHandshake->inHsLen;
    tlsCtx->inRecDataLeft -= pHandshake->inHsLen;
    return 0;
}

/**
 * @brief  Look into the next handshake packet without a specified packet type.
 *         The packet is already read, but marked to be kept in buffer.
 *         After PeekNextHandshakeMsg, Call ReadNextHandshakeMsg to process the peeked packet (kept in buffer).
 *         Consecutive calls of PeekNextHandshakeMsg will simply pass through packets in buffer without processing.
 *         Only the last peeked packet is kept in buffer, and all earlier peeked packets are wasted.

 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
static int PeekNextHandshakeMsg(LTTlsContext *tlsCtx) {
    int ret = 0;
    if (0 != (ret = FetchNextHandshakeMsg(tlsCtx))) {
        return ret;
    }

    tlsCtx->bKeepInMsg = true;
    return 0;
}

// Error ID 0x0408
/**
 * @brief  Read the next handshake packet with a specified handshake type.
 *         If a packet is marked in buffer, then just clear the mark and read the marked packet.
 *         Set pInHsHdr to the handshake header
 *         Set nInHsLen to the length of handshake header and payload
 *
 * @param[in,out] tlsCtx  The TLS context
 * @param[in]     hsType  The handshake type
 * @param[out]    buf     The pointer to the pointer of handshake payload
 * @param[out]    bufLen  The pointer to the length of handshake payload
 * @return   Error code
 */
static int ReadNextHandshakeMsg(LTTlsContext *tlsCtx, u8 hsType, u8 **buf, LT_SIZE *bufLen) {
    int ret = 0;
    LTTlsHandshake *handshake = tlsCtx->handshake;
    if (!tlsCtx->bKeepInMsg) {
        // fetch a new packet
        if (0 != (ret = FetchNextHandshakeMsg(tlsCtx))) {
            return ret;
        }

    } else {
        // Msg is already fetched, so do not fetch again.
        tlsCtx->bKeepInMsg = false;
    }

    if (tlsCtx->inRecType != MBEDTLS_SSL_MSG_HANDSHAKE || handshake->inHsHdr[0] != hsType) {
        return LTTLS_ERROR(0x0408, MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE);
    }

    *buf = handshake->inHsHdr + LTTLS_HANDSHAKE_HEADER_LEN;
    *bufLen = handshake->inHsLen - LTTLS_HANDSHAKE_HEADER_LEN;
    return 0;
}

// Error ID 0x0410
/**
 * @brief Detect if the ServerHello contains a supported_versions extension.
 *
 * @param[in] buf  The buffer containing the ServerHello message
 * @param[in] end  The end of buffer containing the ServerHello message
 *
 * @retval 0 if the ServerHello does not contain a supported_versions extension
 * @retval 1 if the ServerHello contains a supported_versions extension
 * @retval A negative value if an error occurred while parsing the ServerHello.
 */
static int SupportedVersionsExtPresent(const u8 *buf, const u8 *end) {
    const u8 *p = buf;

    /*
     * Check there is enough data to access the legacy_session_id_echo vector
     * length:
     * - legacy_version                 2 bytes
     * - random                         LTTLS_SERVER_HELLO_RANDOM_LEN bytes
     * - legacy_session_id_echo length  1 byte
     */
    LTTLS_CHK_BUF_READ_PTR(0x0410 + 1, p, end, LTTLS_SERVER_HELLO_RANDOM_LEN + 3);
    p += LTTLS_SERVER_HELLO_RANDOM_LEN + 2;
    u8 legacySessionIdEchoLen = *p;

    /*
     * Jump to the extensions, jumping over:
     * - legacy_session_id_echo     (legacy_session_id_echo_len + 1) bytes
     * - cipher_suite               2 bytes
     * - legacy_compression_method  1 byte
     */
    LTTLS_CHK_BUF_READ_PTR(0x0410 + 2, p, end, legacySessionIdEchoLen + 4);
    p += legacySessionIdEchoLen + 4;

    /* Case of no extension */
    if (p == end) {
        return 0;
    }

    /* ...
     * Extension extensions<6..2^16-1>;
     * ...
     * struct {
     *      ExtensionType extension_type; (2 bytes)
     *      opaque extension_data<0..2^16-1>;
     * } Extension;
     */
    LTTLS_CHK_BUF_READ_PTR(0x0410 + 3, p, end, 2);
    u16 extensionsLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    /* Check extensions do not go beyond the buffer of data. */
    LTTLS_CHK_BUF_READ_PTR(0x0410 + 4, p, end, extensionsLen);
    const u8 *extensionsEnd = p + extensionsLen;

    u16 extensionType, extensionDataLen;
    while (p < extensionsEnd) {
        LTTLS_CHK_BUF_READ_PTR(0x0410 + 5, p, extensionsEnd, LTTLS_EXTENSION_HEADER_LEN);
        extensionType = (((u16)p[0]) << 8) + p[1];
        extensionDataLen = (((u16)p[2]) << 8) + p[3];
        p += 4;

        if (extensionType == MBEDTLS_TLS_EXT_SUPPORTED_VERSIONS) {
            return 1;
        }

        LTTLS_CHK_BUF_READ_PTR(0x0410 + 6, p, extensionsEnd, extensionDataLen);
        p += extensionDataLen;
    }

    return 0;
}

// Error ID 0x0418
/**
 * @brief  Check if Server Hello is Hello Retry Request
 *
 * @param[in] buf  The buffer holding Server Hello
 * @param[in] end  The end of buffer
 * @return Returns a negative value on failure, and otherwise
 *         - SSL_SERVER_HELLO_COORDINATE_HELLO or
 *         - SSL_SERVER_HELLO_COORDINATE_HRR
 *
 * Server Hello and HRR are only distinguished by Random set to the
 * special value of the SHA-256 of "HelloRetryRequest".
 *
 * struct {
 *    ProtocolVersion legacy_version = 0x0303;
 *    Random random;
 *    opaque legacy_session_id_echo<0..32>;
 *    CipherSuite cipher_suite;
 *    uint8 legacy_compression_method = 0;
 *    Extension extensions<6..2^16-1>;
 * } ServerHello;
 */
static int ServerHelloIsHrr(const u8 *buf, const u8 *end) {
    LTTLS_CHK_BUF_PTR(0x0418, buf, end, 2 + sizeof(s_MagicHrrString));

    if (lt_memcmp(buf + 2, s_MagicHrrString, sizeof(s_MagicHrrString)) == 0) {
        return SSL_SERVER_HELLO_COORDINATE_HRR;
    }

    return SSL_SERVER_HELLO_COORDINATE_HELLO;
}

// Error ID 0x0420
/**
 * @brief Precheck if server hello or server retry request
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The pointer to the buffer holding handshake body
 * @param[out]    bufLen The pointer to the length of handshake body
 * @return Returns a negative value on failure, and otherwise
 *         - SSL_SERVER_HELLO_COORDINATE_HELLO or
 *         - SSL_SERVER_HELLO_COORDINATE_HRR or
 *         - SSL_SERVER_HELLO_COORDINATE_TLS1_2
 */
static int ServerHelloCoordinate(LTTlsContext *tlsCtx, u8 **ppBuf, LT_SIZE *bufLen) {
    int ret = 0;
    LTTlsHandshake *pHandshake = tlsCtx->handshake;

    if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_SERVER_HELLO, ppBuf, bufLen))) {
        return ret;
    }

    // 1 if present, 0 if not, -x if error
    if ((ret = SupportedVersionsExtPresent(*ppBuf, *ppBuf + *bufLen)) < 0) {
        return ret;
    }

    if (0 == ret) { // For now, we don't support TLS1.2 at all.
        /* For TLS 1.3, the supported versions extension must be present. Abort, if not present.
         * If the "supported_versions" extension in the ServerHello contains a
         * version not offered by the client or contains a version prior to TLS 1.3,
         * the client MUST abort the handshake with an "illegal_parameter" alert. */
        if (tlsCtx->minorVer == MBEDTLS_SSL_MINOR_VERSION_4) {
            return LTTLS_ERROR(0x0420 + 1, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
        }

        return SSL_SERVER_HELLO_COORDINATE_TLS1_2;
    }

    ret = ServerHelloIsHrr(*ppBuf, *ppBuf + *bufLen);
    switch (ret) {
        case SSL_SERVER_HELLO_COORDINATE_HELLO:
            break;
        case SSL_SERVER_HELLO_COORDINATE_HRR:  // TODO need to validate
             /* If a client receives a second HelloRetryRequest in the same connection (i.e., where the ClientHello
              * was itself in response to a HelloRetryRequest), it MUST abort the handshake with an "unexpected_message" alert. */
            if (pHandshake->helloRetryRequestCount > 0) {
                return LTTLS_ERROR(0x0420, MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE);
            }
            /*
             * Clients must abort the handshake with an "illegal_parameter"
             * alert if the HelloRetryRequest would not result in any change
             * in the ClientHello.
             * In a PSK only key exchange that what we expect.
             */
            if ((pHandshake->kexModes & MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ALL) ==0) {
                return LTTLS_ERROR(0x0420 + 2, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
            }

            ++pHandshake->helloRetryRequestCount;
            break;
    }

    return ret;
}

// Error ID 0x0428
/**
 * @brief  Check if the session ID in Server Hello echos the session ID in Client Hello
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The pointer to the buffer holding the session ID in Server Hello
 * @param[out]    end    The end of buffer
 * @return   Error code
 */
static int ServerHelloSessionIdEcho(LTTlsContext *tlsCtx, const u8 **buf, const u8 *end) {
    const u8 *p = *buf;

    LTTLS_CHK_BUF_READ_PTR(0x0428 + 1, p, end, 1);
    u8 legacySessionIdEchoLen = *p;
    ++p;

    LTTLS_CHK_BUF_READ_PTR(0x0428 + 2, p, end, legacySessionIdEchoLen);

    /* legacy_session_id_echo
     * A client which receives a legacy_session_id_echo field that does not match what
     * it sent in the ClientHello MUST abort the handshake with an "illegal_parameter" alert. */
    if (tlsCtx->sessionNegotiate->idLen != legacySessionIdEchoLen ||
        lt_memcmp(tlsCtx->sessionNegotiate->id, p, legacySessionIdEchoLen) != 0) {
        return LTTLS_ERROR(0x0428, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
    }

    *buf = p + legacySessionIdEchoLen;
    return 0;
}

// Error ID 0x0430
/** TODO need to validate
 * @brief  Parse cookie extension in Hello Retry Request
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The data buffer
 * @param[out]    end    The end of buffer
 * @return   Error code
 *
 * struct {
 *        opaque cookie<1..2^16-1>;
 * } Cookie;
 *
 * When sending a HelloRetryRequest, the server MAY provide a "cookie"
 * extension to the client (this is an exception to the usual rule that
 * the only extensions that may be sent are those that appear in the
 * ClientHello).  When sending the new ClientHello, the client MUST copy
 * the contents of the extension received in the HelloRetryRequest into
 * a "cookie" extension in the new ClientHello.  Clients MUST NOT use
 * cookies in their initial ClientHello in subsequent connections.
 */
static int ParseCookieExt(const LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    u16 cookieLen;
    const u8 *p = buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /* Retrieve length field of cookie */
    LTTLS_CHK_BUF_READ_PTR(0x0430 + 1, p, end, 2);
    cookieLen = (((u16)p[0]) << 8) + p[1];
    p += 2;
    // if cookie is present, it must at least have one byte.
    if (cookieLen == 0) {
        return LTTLS_ERROR(0x0430 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    LTTLS_CHK_BUF_READ_PTR(0x0430 + 3, p, end, cookieLen);

    lt_free(handshake->cookie);
    handshake->cookie = lt_malloc(cookieLen);
    if (!handshake->cookie) {
        return LTTLS_ERROR(0x0430, MBEDTLS_ERR_SSL_ALLOC_FAILED);
    }

    lt_memcpy(handshake->cookie, p, cookieLen);
    handshake->hrrCookieLen = cookieLen;
    handshake->extensionsPresent |= LTTLS_EXT_COOKIE;
    return 0;
}

// Error ID 0x0438
/** TODO need to validate
 * @brief  Parse key_share extension in Hello Retry Request
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The data buffer
 * @param[out]    end    The end of buffer
 * @return   Error code
 *
 * struct {
 *        NamedGroup selected_group;
 * } KeyShareHelloRetryRequest;
 */
static int ParseHrrKeyShareExt(const LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    const u8 *p = buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    // Read selected_group
    LTTLS_CHK_BUF_READ_PTR(0x0438, p, end, 2);
    u16 selectedGroup = (((u16)p[0]) << 8) + p[1];

    /* Upon receipt of this extension in a HelloRetryRequest, the client
     * MUST first verify that the selected_group field corresponds to a
     * group which was provided in the "supported_groups" extension in the
     * original ClientHello.
     * The supported_group was based on the info in pTls->conf->group_list.
     *
     * If the server provided a key share that was not sent in the ClientHello
     * then the client MUST abort the handshake with an "illegal_parameter" alert.
     */
    int found = 0;
    for (int i = 0; s_ECGroupList[i] != 0; ++i) {
        if (selectedGroup == s_ECGroupList[i]) {
            // Found a match
            found = 1;
            break;
        }
    }

    /* Client MUST verify that the selected_group field does not
     * correspond to a group which was provided in the "key_share"
     * extension in the original ClientHello. If the server sent an
     * HRR message with a key share already provided in the
     * ClientHello then the client MUST abort the handshake with
     * an "illegal_parameter" alert.
     */
    if (found == 0 || selectedGroup == handshake->offeredGroupId) {
        return LTTLS_ERROR(0x0438, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
    }

    // Remember server's preference for next ClientHello
    handshake->offeredGroupId = selectedGroup;

    return 0;
}

// Error ID 0x0440
/**
 * @brief  Read the peer public key for ECDHE
 *
 * @param[in,out] tlsCtx  The TLS context
 * @param[out]    buf     The buffer holding the key share extension
 * @param[out]    bufLen  The length of buffer
 * @return  Error code
 */
static int ReadPublicEcdheShare(LTTlsContext *tlsCtx, const u8 *buf, LT_SIZE bufLen) {
    const u8 *p = (u8 *)buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /* Get size of the TLS opaque key_exchange field of the KeyShareEntry struct. */
    u16 peerkeyLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    /* Check if key size is consistent with given buffer length. */
    if (peerkeyLen > (bufLen - 2)) {
        return LTTLS_ERROR(0x0440, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }
    // TODO, only support ECDHE_X25519
    if (peerkeyLen != ECDHE_KEY_LENGTH) {
        return LTTLS_ERROR(0x0440, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
    }

    /* Store peer's ECDH public key. */
    lt_memcpy(handshake->ecdhePeerKey, p, peerkeyLen);

    return 0;
}

// Error ID 0x0448
/**
 * @brief  Parse the key share extension
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the extension
 * @param[out]    end    The end of buffer
 * @return  Error code
 *
 * struct {
 *        KeyShareEntry server_share;
 * } KeyShareServerHello;
 *
 * struct {
 *        NamedGroup group;
 *        opaque key_exchange<1..2^16-1>;
 * } KeyShareEntry;
 */
static int ParseKeyShareExt(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    int ret = 0;
    const u8 *p = buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /* ...
     * NamedGroup group; (2 bytes)
     * ...
     */
    LTTLS_CHK_BUF_READ_PTR(0x0448, p, end, 2);
    u16 group = (((u16)p[0]) << 8) + p[1];
    p += 2;

    /* Check that the chosen group matches the one we offered. */
    if (handshake->offeredGroupId != group) {
        return LTTLS_ERROR(0x0448 + 1, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
    }

    if (group == MBEDTLS_SSL_IANA_TLS_GROUP_X25519) {
        if (0 != (ret = ReadPublicEcdheShare(tlsCtx, p, end - p))) {
            return ret;
        }

    } else {
        return LTTLS_ERROR(0x0448 + 2, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
    }

    handshake->extensionsPresent |= LTTLS_EXT_KEY_SHARE;
    return 0;
}

// Error ID 0x0450
/**
 * @brief  Parse the supported version extension (selected_version)
 *         It must be 0304 (TLS 1.3)
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the extension
 * @param[out]    end    The end of buffer
 * @return  Error code
 *    struct {
 *        select (Handshake.msg_type) {
 *            case client_hello:
 *                ProtocolVersion versions<2..254>;
 *
 *            case server_hello: // and HelloRetryRequest
 *                ProtocolVersion selected_version;
 *        };
 *    } SupportedVersions;
 */
static int ParseSupportedVersionsExt(LTTlsContext *tlsCtx, u8 const *buf, u8 const *end) {
    LTTLS_CHK_BUF_READ_PTR(0x0450, buf, end, 2);
    if (buf[0] != MBEDTLS_SSL_MAJOR_VERSION_3 || buf[1] != MBEDTLS_SSL_MINOR_VERSION_4) {
        return LTTLS_ERROR(0x0450, MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION);
    }

    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_SUPPORTED_VERSIONS;
    return 0;
}

// Error ID 0x0458
/**
 * @brief  Parse pre shared key extension (selected_identity only)
 *         The extension is received from the server when the client resumes a session.
 *         Duiring resumption, the client first sends a pre shared key (using a ticket) to the server,
 *         the server replies with this extension to pick which PSK to use.
 *         Because only one ticket is saved in client, the selection is always 0 (the key identity).
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the extension
 * @param[out]    end    The end of buffer
 * @return  Error code
 *
 *   struct {
 *       select (Handshake.msg_type) {
 *           case client_hello: OfferedPsks;
 *           case server_hello: uint16 selected_identity;
 *       };
 *   } PreSharedKeyExtension;
 */
static int ParsePreSharedKeyIdExt(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    LTTLS_CHK_BUF_READ_PTR(0x0458, buf, end, 2);
    if (buf[0] != 0 || buf[1] != 0) {
        return LTTLS_ERROR(0x0458, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
    }

    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_PRE_SHARED_KEY;
    return 0;
}

// Error ID 0x0460
/**
 * @brief  Parse server hello
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the extension
 * @param[out]    end    The end of buffer
 * @param[in]     bIsHrr True if the hello is HRR
 * @return  Error code
 *
 * struct {
 *    ProtocolVersion legacy_version = 0x0303; // TLS 1.2
 *    Random random;
 *    opaque legacy_session_id_echo<0..32>;
 *    CipherSuite cipher_suite;
 *    uint8 legacy_compression_method = 0;
 *    Extension extensions<6..2^16-1>;
 * } ServerHello;
 */
static int ParseServerHello(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end, bool bIsHrr) {
    int ret = 0;
    const u8 *p = buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /*
     * Check there is space for minimal fields
     *
     * - legacy_version             ( 2 bytes)
     * - random                     (MBEDTLS_SERVER_HELLO_RANDOM_LEN bytes)
     * - legacy_session_id_echo     ( 1 byte ), minimum size
     * - cipher_suite               ( 2 bytes)
     * - legacy_compression_method  ( 1 byte )
     */
    LTTLS_CHK_BUF_READ_PTR(0x0460 + 1, p, end, LTTLS_SERVER_HELLO_RANDOM_LEN + 6);

    /* ...
     * ProtocolVersion legacy_version = 0x0303; // TLS 1.2
     * ...
     * with ProtocolVersion defined as:
     * uint16 ProtocolVersion;
     */
    if (!(p[0] == MBEDTLS_SSL_MAJOR_VERSION_3 && p[1] == MBEDTLS_SSL_MINOR_VERSION_3)) {
        return LTTLS_ERROR(0x0460, MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION);
    }
    p += 2;

    /* ...
     * Random random;
     * ...
     * with Random defined as:
     * opaque Random[MBEDTLS_SERVER_HELLO_RANDOM_LEN];
     *
     * We don't use or keep server random bytes, so ignore.
     */
    // if (!bIsHrr) lt_memcpy(pHandshake->serverRandBytes, p, MBEDTLS_SERVER_HELLO_RANDOM_LEN);
    p += LTTLS_SERVER_HELLO_RANDOM_LEN;

    /* ...
     * opaque legacy_session_id_echo<0..32>;
     * ...
     */
    if (0 != (ret = ServerHelloSessionIdEcho(tlsCtx, &p, end))) {
        return ret;
    }

    /* ...
     * CipherSuite cipher_suite;
     * ...
     * with CipherSuite defined as:
     * uint8 CipherSuite[2];
     */
    LTTLS_CHK_BUF_READ_PTR(0x0460 + 2, p, end, 2);
    u16 cipherSuite = (((u16)p[0]) << 8) + p[1];
    p += 2;

    if ((cipherSuite != MBEDTLS_TLS1_3_AES_128_GCM_SHA256)) {
        return LTTLS_ERROR(0x0460 + 1, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);

    } else if ((!bIsHrr) && (handshake->helloRetryRequestCount > 0) &&
               (cipherSuite != tlsCtx->sessionNegotiate->cipherSuite)) {
        /*
         * If we received an HRR before and that the proposed selected
         * ciphersuite in this server hello is not the same as the one
         * proposed in the HRR, we abort the handshake and send an
         * "illegal_parameter" alert.
         */
        return LTTLS_ERROR(0x0460 + 1, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
    }

    tlsCtx->sessionNegotiate->cipherSuite = cipherSuite;
    tlsCtx->sessionNegotiate->start = LT_GetCore()->GetKernelTime();

    /* ...
     * uint8 legacy_compression_method = 0;
     * ...
     */
    LTTLS_CHK_BUF_READ_PTR(0x0460 + 3, p, end, 1);
    if (p[0] != 0) {
        return LTTLS_ERROR(0x0460 + 2, MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER);
    }
    ++p;

    /* ...
     * Extension extensions<6..2^16-1>;
     * ...
     * struct {
     *      ExtensionType extension_type; (2 bytes)
     *      opaque extension_data<0..2^16-1>;
     * } Extension;
     */
    LTTLS_CHK_BUF_READ_PTR(0x0460 + 4, p, end, 2);
    u16 extensionsLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    /* Check extensions do not go beyond the buffer of data. */
    LTTLS_CHK_BUF_READ_PTR(0x0460 + 5, p, end, extensionsLen);
    const u8 *extensionsEnd = p + extensionsLen;

    // parse extensions
    u16 extensionType, extensionDataLen;
    const u8 *extensionDataEnd;
    while (p < extensionsEnd) {
        LTTLS_CHK_BUF_READ_PTR(0x0460 + 6, p, extensionsEnd, LTTLS_EXTENSION_HEADER_LEN);
        extensionType = (((u16)p[0]) << 8) + p[1];
        extensionDataLen = (((u16)p[2]) << 8) + p[3];
        p += LTTLS_EXTENSION_HEADER_LEN;

        LTTLS_CHK_BUF_READ_PTR(0x0460 + 7, p, extensionsEnd, extensionDataLen);
        extensionDataEnd = p + extensionDataLen;

        switch (extensionType) {
            case MBEDTLS_TLS_EXT_COOKIE:
                if (!bIsHrr) {
                    return LTTLS_ERROR(0x0460 + 1, MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION);
                }
                if (0 != (ret = ParseCookieExt(tlsCtx, p, extensionDataEnd))) {
                    return ret;
                }
                break;

            case MBEDTLS_TLS_EXT_SUPPORTED_VERSIONS:
                if (0 != (ret = ParseSupportedVersionsExt(tlsCtx, p, extensionDataEnd))) {
                    return ret;
                }
                break;

            case MBEDTLS_TLS_EXT_KEY_SHARE:
                if ((handshake->kexModes & MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ALL) == 0) {
                    return LTTLS_ERROR(0x0460 + 2, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
                }

                if (bIsHrr) {
                    ret = ParseHrrKeyShareExt(tlsCtx, p, extensionDataEnd);

                } else {
                    ret = ParseKeyShareExt(tlsCtx, p, extensionDataEnd);
                }

                if (0 != ret) {
                    return ret;
                }
                break;

            case MBEDTLS_TLS_EXT_PRE_SHARED_KEY:
                if (0 != (ret = ParsePreSharedKeyIdExt(tlsCtx, p, extensionDataEnd))) {
                    return ret;
                }
                break;

            default:
                return LTTLS_ERROR(0x0460 + 2, MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION);
        }

        p += extensionDataLen;
    }

    return 0;
}

/** TODO need to validate with HRR
 * @brief  Rest transcript hash after receiving HRR
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Crypto error code
 *
 * Replace Transcript-Hash(X) by
 * Transcript-Hash( message_hash || 00 00 || Hash.length || X )
 */
static LTSystemCryptoResult ResetTranscriptForHrr(LTTlsContext *tlsCtx) {
    LTSystemCryptoResult ret = 0;
    u8 h[SHA256_HASH_LENGTH + 4];
    if (0 != (ret = GetTranscriptHash(tlsCtx, h + 4))) return ret;

    h[0] = MBEDTLS_SSL_HS_MESSAGE_HASH;
    h[1] = 0;
    h[2] = 0;
    h[3] = SHA256_HASH_LENGTH;

    if (0 != (ret = InitTranscript(tlsCtx))) return ret;
    if (0 != (ret = UpdateTranscript(tlsCtx, h, SHA256_HASH_LENGTH + 4))) return ret;

    return 0;
}

/** TODO need to validate with HRR
 * @brief  Operations after processing HRR
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int  PostProcessHrr(LTTlsContext *tlsCtx) {
    /* TODO need to keep info from retry request to next client hello
     * A few states of the handshake are preserved, including:
     *   - session ID
     *   - session ticket
     *   - negotiated ciphersuite
     */
    int ret = 0;
    if (0 != (ret = ResetTls(tlsCtx))) return ret;
    tlsCtx->bHrr = true;
    return 0;
}

// Error ID 0x0470
/**
 * @brief Operations after processing server hello
 *        Determine key exchange mode and schedule handshake keys
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int PostProcessServerHello(LTTlsContext *tlsCtx) {
    LTTlsHandshake *handshake = tlsCtx->handshake;

    switch (handshake->extensionsPresent & (LTTLS_EXT_PRE_SHARED_KEY | LTTLS_EXT_KEY_SHARE)) {
        /* Only the pre_shared_key extension was received */
        case LTTLS_EXT_PRE_SHARED_KEY:
            handshake->kexModes = MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK;
            break;

        /* Only the key_share extension was received */
        case LTTLS_EXT_KEY_SHARE:
            handshake->kexModes = MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL;
            break;

        /* Both the pre_shared_key and key_share extensions were received */
        case (LTTLS_EXT_PRE_SHARED_KEY | LTTLS_EXT_KEY_SHARE):
            handshake->kexModes = MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL;
            break;

        /* Neither pre_shared_key nor key_share extension was received */
        default:
            return LTTLS_ERROR(0x0470, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
    }

    /* Compute handshake secret and traffic secret */
    int ret = 0;
    if (0 != (ret = KeyScheduleHandshake(tlsCtx))) return ret;

    tlsCtx->transformOut = tlsCtx->transformHandshake;
    tlsCtx->transformIn = tlsCtx->transformHandshake;
    return 0;
}

// Error ID 0x0478
/**
 * @brief  Process server hello, determine if it is a hello retry request or a server hello.
 *         Set tlsCtx->bHrr to true if received a hello retry request
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int ProcessServerHello(LTTlsContext *tlsCtx) {
    int ret = 0;
    u8 *buf = NULL;
    LT_SIZE bufLen = 0;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    handshake->extensionsPresent = LTTLS_EXT_NONE;

    // check the hello to determine if it is HRR
    if ((ret = ServerHelloCoordinate(tlsCtx, &buf, &bufLen)) < 0) {
        return ret;
    }

    // We don't support downgrade to TLS 1.2 here.
    if (ret == SSL_SERVER_HELLO_COORDINATE_TLS1_2) {
        return LTTLS_ERROR(0x0478, MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION);
    }

    tlsCtx->bHrr = (ret == SSL_SERVER_HELLO_COORDINATE_HRR);

    // buf points to the hello payload
    if (0 != (ret = ParseServerHello(tlsCtx, buf, buf + bufLen, tlsCtx->bHrr))) {
        return ret;
    }

    if (tlsCtx->bHrr) {
        if (0 != (ret = ResetTranscriptForHrr(tlsCtx))) {
            return LTTLS_ERROR(0x0478 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
        }
    }

    if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
        return LTTLS_ERROR(0x0478 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    if (tlsCtx->bHrr) {
        if (0 != (ret = PostProcessHrr(tlsCtx))) {
            return ret;
        }

    } else {
        if (0 != (ret = PostProcessServerHello(tlsCtx))) {
            return ret;
        }
    }

    return 0;
}

/**
 * @brief Find a name is in the name list
 *
 * @param nameList  The name list
 * @param name      The name: len (1B) | name (len B)
 * @return true   if the name is in the list
 * @return false  otherwise
 * @note   Assume the name list is correctly formatted, no security check on this.
 */
static bool FindAlpnName(const LTTlsAlpnExt *nameList, const u8 *name) {
    u16 listLen = nameList->alpnLen;
    const u8 *p = nameList->protocolNames;
    while (listLen > 0) {
        if ((p[0] == name[0]) && (lt_memcmp(p, name, p[0]) == 0)) {
            return true;
        }
        listLen -= p[0] + 1;
        p        += p[0] + 1;
    }
    return false;
}

// Error ID 0x0480
/**
 * @brief  Parse the ALPN extension
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the extension
 * @param[out]    end    The end of buffer
 * @return  Error code
 *
 * RFC7301
 *   enum {
 *       application_layer_protocol_negotiation(16), (65535)
 *   } ExtensionType;
 *
 *   The "extension_data" field of the
 *   ("application_layer_protocol_negotiation(16)") extension SHALL
 *   contain a "ProtocolNameList" value.
 *
 *   opaque ProtocolName<1..2^8-1>;
 *
 *   struct {
 *       ProtocolName protocol_name_list<2..2^16-1>
 *   } ProtocolNameList;
 *
 *   ext type (2B) is always 0x10.
 *   ext length (2B) is 2 + nAlpnLen.
 *   each protocol name is {nNameLen (1B), Name[nNameLen]}.
 *   nAlpnLen (2B) = sum(1 + nNameLen)
 */
static int ParseAlpnExt(const LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    if (!tlsCtx->options->alpnExt) {
        // No ALPN extension, so ignore
        return 0;
    }

    const u8 *p = buf;
    LTTLS_CHK_BUF_READ_PTR(0x0480 + 1, p, end, 2);
    u16 listLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    LTTLS_CHK_BUF_READ_PTR(0x0480 + 2, p, end, listLen);
    u8 nameLen;
    // Each name has to be in the Alpha list.
    while (listLen > 0) {
        nameLen = p[0] + 1;
        LTTLS_CHK_BUF_READ_PTR(0x0480 + 3, p, end, nameLen);
        if (!FindAlpnName(tlsCtx->options->alpnExt, p)) {
            return LTTLS_ERROR(0x0480, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
        }
        listLen -= nameLen;
        p       += nameLen;
    }

    tlsCtx->handshake->extensionsPresent |= LTTLS_EXT_ALPN;
    return 0;
}

// Error ID 0x0488
/**
 * @brief  Parse the encrypted extensions
 *
 * @param[in] tlsCtx The TLS context
 * @param[in] buf    The buffer holding the extension
 * @param[in] end    The end of buffer
 * @return  Error code
 *
 * Parse EncryptedExtensions message
 * struct {
 *     Extension extensions<0..2^16-1>;
 * } EncryptedExtensions;
 */
static int ParseEncryptedExtensions(const LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    const u8 *p = buf;

    LTTLS_CHK_BUF_READ_PTR(0x0488 + 1, p, end, 2);
    u16 extensionsLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    const u8 *extensionsEnd = p + extensionsLen;
    LTTLS_CHK_BUF_READ_PTR(0x0488 + 2, p, end, extensionsLen);

    int ret = 0;
    u16 extensionType;
    u16 extensionDataLen;
    while (p < extensionsEnd) {
        /*
         * struct {
         *     ExtensionType extension_type; (2 bytes)
         *     opaque extension_data<0..2^16-1>;
         * } Extension;
         */
        LTTLS_CHK_BUF_READ_PTR(0x0488 + 3, p, extensionsEnd, 4);
        extensionType = (((u16)p[0]) << 8) + p[1];
        extensionDataLen = (((u16)p[2]) << 8) + p[3];
        p += 4;

        LTTLS_CHK_BUF_READ_PTR(0x0488 + 4, p, extensionsEnd, extensionDataLen);

        /* In IETF RFC 8446, 4.3.1, The client MUST check EncryptedExtensions for the
         * presence of any forbidden extensions and if any are found MUST abort
         * the handshake with an "unsupported_extension" alert.
         * HOWEVER, at https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml
         * there are many additional TLS extensions.
         * So, we will only process must-need extenssion and ignore all other extensions.
         */
        switch (extensionType) {
            case MBEDTLS_TLS_EXT_SUPPORTED_GROUPS:
                break;

            case MBEDTLS_TLS_EXT_ALPN:
                if (0 != (ret = ParseAlpnExt(tlsCtx, p, extensionsEnd))) return ret;
                break;

            default:
                ; // we just ignore all here.
                #if 0
                return MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION;
                #endif
        }

        p += extensionDataLen;
    }

    /* Check that we consumed all the message. */
    if (p != end) {
        return LTTLS_ERROR(0x0488 + 5, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    return 0;
}

// Error ID 0x04D0
/**
 * @brief  Process encrypted extension
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int ProcessEncryptedExtensions(LTTlsContext *tlsCtx) {
    int ret = 0;
    u8 *buf = NULL;
    LT_SIZE bufLen = 0;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_ENCRYPTED_EXTENSIONS, &buf, &bufLen))) {
        return ret;
    }

    if (0 != (ret = ParseEncryptedExtensions(tlsCtx, buf, buf + bufLen))) {
        return ret;
    }

    if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
        return LTTLS_ERROR(0x04D0, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    return 0;
}

/**
 * @brief Coordination with the ambiguity of not knowing if a CertificateRequest will be sent.
 *
 * @param[in,out] tlsCtx  The TLS context
 * @retval  Negative code on failure
 * @retval  SSL_CERTIFICATE_REQUEST_EXPECT_REQUEST if a Certificate Request is expected
 * @retval  SSL_CERTIFICATE_REQUEST_SKIP if a Certificate Request is not expected
 */
static int CertificateRequestCoordinate(LTTlsContext *tlsCtx) {
    int ret = 0;

    if (0 != (ret = PeekNextHandshakeMsg(tlsCtx))) {
        return ret;
    }

    if (tlsCtx->handshake->inHsHdr[0] == MBEDTLS_SSL_HS_CERTIFICATE_REQUEST) {
        return SSL_CERTIFICATE_REQUEST_EXPECT_REQUEST;
    }

    return SSL_CERTIFICATE_REQUEST_SKIP;
}

// Error ID 0x0490
/**
 * @brief  Parse signature algorithm extension
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[in]     buf    The buffer holding the extension
 * @param[in]     end    The end of buffer
 * @return  Error code
 *
 * enum {
 *    ....
 *   ecdsa_secp256r1_sha256( 0x0403 ),
 *   ecdsa_secp384r1_sha384( 0x0503 ),
 *   ecdsa_secp521r1_sha512( 0x0603 ),
 *    ....
 * } SignatureScheme;
 *
 * struct {
 *    SignatureScheme supported_signature_algorithms<2..2^16-2>;
 * } SignatureSchemeList;
 */
static int ParseSigAlgExt(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    const u8 *p = buf;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    LTTLS_CHK_BUF_READ_PTR(0x0490 + 1, p, end, 2);
    u16 supportedSigAlgsLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    lt_memset(handshake->receivedSigAlgs, 0, sizeof(handshake->receivedSigAlgs));

    LTTLS_CHK_BUF_READ_PTR(0x0490 + 2, p, end, supportedSigAlgsLen);
    const u8 *supportedSigAlgsEnd = p + supportedSigAlgsLen;
    u16 sigAlg;
    u8 idx = 0;
    while (p < supportedSigAlgsEnd) {
        LTTLS_CHK_BUF_READ_PTR(0x0490 + 3, p, supportedSigAlgsEnd, 2);
        sigAlg = (((u16)p[0]) << 8) + p[1];
        p += 2;

        if (sigAlg != SIGNATURE_ECDSA_SECP256R1_SHA256 && sigAlg != SIGNATURE_ED25519) {
            continue;
        }

        if (idx + 1 < LTTLS_RECEIVED_SIG_ALGS_SIZE) {
            handshake->receivedSigAlgs[idx] = sigAlg;
            ++idx;

        } else {
            // received too many signature algorithms, at most 3 expected
            return LTTLS_ERROR(0x0490 + 1, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
        }

    }

    /* Check that we consumed all the message. */
    if (p != end) {
        return LTTLS_ERROR(0x0490 + 4, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    // didn't receive any supported signature algorithm
    if (idx == 0) {
        return LTTLS_ERROR(0x0490 + 2, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
    }

    handshake->receivedSigAlgs[idx] = MBEDTLS_TLS1_3_SIG_NONE;
    return 0;
}

// Error ID 0x0498
/**
 * @brief  Parse certificate request
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[in]     buf    The buffer holding the extension
 * @param[in]     end    The end of buffer
 * @return  Error code
 *
 * struct {
 *   opaque certificate_request_context<0..2^8-1>;
 *   Extension extensions<2..2^16-1>;
 * } CertificateRequest;
 */
static int ParseCertificateRequest(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end ) {
    int ret = 0;
    const u8 *p = buf;

    /* ...
     * opaque certificate_request_context<0..2^8-1>
     * ...
     */
    LTTLS_CHK_BUF_READ_PTR(0x0498 + 1, p, end, 1);
    u8 certReqContextLen = p[0];
    p += 1;

    if (certReqContextLen > 0) {
        LTTLS_CHK_BUF_READ_PTR(0x0498 + 2, p, end, certReqContextLen);
        lt_free(tlsCtx->certReqContext);
        tlsCtx->certReqContext = lt_malloc(certReqContextLen);
        if (!tlsCtx->certReqContext) {
            return LTTLS_ERROR(0x0498, MBEDTLS_ERR_SSL_ALLOC_FAILED);
        }
        lt_memcpy(tlsCtx->certReqContext, p, certReqContextLen);
        tlsCtx->certReqContextLen = certReqContextLen;
        p += certReqContextLen;
    }

    /* ...
     * Extension extensions<2..2^16-1>;
     * ...
     */
    LTTLS_CHK_BUF_READ_PTR(0x0498 + 3, p, end, 2);
    u8 extensionsLen = (((u16)p[0]) << 8) + p[1];
    p += 2;

    LTTLS_CHK_BUF_READ_PTR(0x0498 + 4, p, end, extensionsLen);
    const u8 *pExtensionsEnd = p + extensionsLen;

    bool bSigAlgExtFound = false;
    u16 extensionType;
    u16 extensionDataLen;

    while (p < pExtensionsEnd) {
        LTTLS_CHK_BUF_READ_PTR(0x0498 + 5, p, pExtensionsEnd, 4);
        extensionType = (((u16)p[0]) << 8) + p[1];
        extensionDataLen = (((u16)p[2]) << 8) + p[3];
        p += 4;

        LTTLS_CHK_BUF_READ_PTR(0x0498 + 6, p, pExtensionsEnd, extensionDataLen);

        switch (extensionType) {
            case MBEDTLS_TLS_EXT_SIG_ALG:
                if (0 != (ret = ParseSigAlgExt(tlsCtx, p, p + extensionDataLen))) {
                    return ret;
                }
                if (!bSigAlgExtFound) {
                    bSigAlgExtFound = true;
                } else {
                    return LTTLS_ERROR(0x0498, MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION); // only one sig alg extension, so no second.
                }
                break;

            default:
                // TODO ignore unknown extensions
                break;
        }
        p += extensionDataLen;
    }

    /* Check that we consumed all the message and found valid signature algorithms. */
    if (p != end || !bSigAlgExtFound) {
        return LTTLS_ERROR(0x0498 + 7, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    tlsCtx->bClientAuth = true;
    return 0;
}

// Error ID 0x04A0
/**
 * @brief  Process certificate request
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int ProcessCertificateRequest(LTTlsContext *tlsCtx) {
    int ret = 0;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    if ((ret = CertificateRequestCoordinate(tlsCtx)) < 0) {
        return ret;
    }

    if (ret == SSL_CERTIFICATE_REQUEST_EXPECT_REQUEST) {
        if (!tlsCtx->options->clientCertificate) {
            return LTTLS_ERROR(0x04A0, MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE);
        }
        if (kLTDriverCrypto_ProvisionId_CommPrivate != tlsCtx->options->clientPrivateKeyReference.provisionId) {
            return LTTLS_ERROR(0x04A0, MBEDTLS_ERR_SSL_PRIVATE_KEY_REQUIRED);
        }
        u8 *pBuf;
        LT_SIZE nBufLen;
        if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_CERTIFICATE_REQUEST, &pBuf, &nBufLen))) {
            return ret;
        }
        if (0 != (ret = ParseCertificateRequest(tlsCtx, pBuf, pBuf + nBufLen))) {
            return ret;
        }
        if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
            return LTTLS_ERROR(0x04A0, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
        }

    } else if (ret == SSL_CERTIFICATE_REQUEST_SKIP) {
        ret = 0;
    }

    return ret;
}

// Error ID 0x04A8
/**
 * @brief  Process server finished
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int ProcessServerFinished(LTTlsContext *tlsCtx) {
    int ret = 0;
    u8 *buf;
    LT_SIZE bufLen;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    /* Preprocessing step: Compute handshake digest */
    u8 hmac[SHA256_HASH_LENGTH];
    if (0 != (ret = CalculateVerifyData(tlsCtx, MBEDTLS_SSL_IS_SERVER, hmac))) {
        return ret;
    }

    if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_FINISHED, &buf, &bufLen))) {
        return ret;
    }

    /* Validation */
    if (bufLen != SHA256_HASH_LENGTH) {
        return LTTLS_ERROR(0x04A8, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    if (lt_memcmp(buf, hmac, SHA256_HASH_LENGTH) != 0) {
        return LTTLS_ERROR(0x04A8, MBEDTLS_ERR_SSL_DECRYPT_ERROR);
    }

    if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
        return LTTLS_ERROR(0x04A8, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    if (0 != (ret = KeyScheduleApplication(tlsCtx))) {
        return ret;
    }
    tlsCtx->transformIn = tlsCtx->transformApplication;

    return 0;
}

// Error ID 0x04B0
/**
 * @brief  Parse and validate certificate chain.
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[in]     buf    The data buffer
 * @param[in]     end    The end of buffer
 * @return   Error code
 * @note    The certificates must be ordered from end to CA and validated from CA to end.
 *          The CA's key must be in client's CA key pool (caKeys).
 *
 * Structure of Certificate message:
 *
 * enum {
 *     X509(0),
 *     RawPublicKey(2),
 *     (255)
 * } CertificateType;
 *
 * struct {
 *     select (certificate_type) {
 *         case RawPublicKey:
 *           * From RFC 7250 ASN.1_subjectPublicKeyInfo *
 *           opaque ASN1_subjectPublicKeyInfo<1..2^24-1>;
 *         case X509:
 *           opaque cert_data<1..2^24-1>;
 *     };
 *     Extension extensions<0..2^16-1>;
 * } CertificateEntry;
 *
 * struct {
 *     opaque certificate_request_context<0..2^8-1>;
 *     CertificateEntry certificate_list<0..2^24-1>;
 * } Certificate;
 */
static int ParseServerCertificate(LTTlsContext *tlsCtx, const u8 *buf, const u8 *end) {
    const u8 *p = buf;

    LTTLS_CHK_BUF_READ_PTR(0x04B0 + 1, p, end, 4);
    LT_SIZE certReqContextLen = p[0];
    u16 certListLen = (((u16)p[2]) << 8) + p[3];
    /* In theory, the certificate list can be up to 2^24 Bytes,
     * but we don't support anything beyond 2^16 = 64K.
     * We don't support request context in server certificate either. */
    if ((certReqContextLen != 0) || (p[1] != 0)) {
        return LTTLS_ERROR(0x04B0 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    p += 4;
    LTTLS_CHK_BUF_READ_PTR(0x04B0 + 3, p, end, certListLen);

    int ret = tlsCtx->config->x509->ParseValidateCertChain(p, certListLen, tlsCtx->options->caKeys, tlsCtx->options->caKeyCount, tlsCtx->options->serverName, &tlsCtx->sessionNegotiate->peerPubKey);
    return ret;
}

// Error ID 0x04C8
/**
 * @brief  Process server certificate
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
static int ProcessServerCertificate(LTTlsContext *tlsCtx) {
    int ret = 0;
    u8 *buf;
    LT_SIZE bufLen;
    LTTlsHandshake *handshake = tlsCtx->handshake;

    if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_CERTIFICATE, &buf, &bufLen))) {
        return ret;
    }

    /* Parse and validate the certificate chain sent by the peer. */
    if (0 != (ret = ParseServerCertificate(tlsCtx, buf, buf + bufLen))) {
        return ret;
    }

    if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
        return LTTLS_ERROR(0x04C8, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }

    return 0;
}

// Error ID 0x04C0
/**
 * @brief Process server certificate verify
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 *
 *     struct {
 *         SignatureScheme algorithm;
 *         opaque signature<0..2^16-1>;
 *     } CertificateVerify;
 */
static int ProcessServerCertificateVerify(LTTlsContext *tlsCtx) {
    struct SrvCertVerify {
        u8 toVerify[130];
        u32 sigVal[ECDSA_P256_SIGNATURE_LENGTH / 4];  // can contain Ed25519 signature
        u32 pkVal[ECDSA_P256_PUBLICKEY_LENGTH / 4];  // can contain Ed25519 public key
    };
    struct SrvCertVerify *tmp = lt_malloc(sizeof(struct SrvCertVerify));
    if (!tmp) return LTTLS_ERROR(0x04C0, MBEDTLS_ERR_SSL_ALLOC_FAILED);

    int ret = 0;
    do {
        u8 *p;
        LT_SIZE len;
        LTTlsHandshake *handshake = tlsCtx->handshake;

        if (0 != (ret = ReadNextHandshakeMsg(tlsCtx, MBEDTLS_SSL_HS_CERTIFICATE_VERIFY, &p, &len))) {
            break;
        }

        if (len < 2) {
            ret = LTTLS_ERROR(0x04C0 + 1, MBEDTLS_ERR_SSL_DECODE_ERROR);
            break;
        }
        u16 algorithm = (((u16)p[0]) << 8) + p[1];
        p += 2;

        // support ECDSA_SECP256R1_SHA256 and ED25519
        if (algorithm != SIGNATURE_ECDSA_SECP256R1_SHA256 && algorithm != SIGNATURE_ED25519) {
            ret = LTTLS_ERROR(0x04C0, MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE);
            break;
        }

        lt_memset(tmp->toVerify, 0x20, 64);
        lt_memcpy(tmp->toVerify + 64, LTTLS_LBL_WITH_LEN(server_cv));
        lt_memset(tmp->toVerify + 97, 0x00, 1);
        if (0 != (ret = GetTranscriptHash(tlsCtx, tmp->toVerify + 98))) {
            ret = LTTLS_ERROR(0x04C0 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
            break;
        }

        lt_memcpy(tmp->pkVal, tlsCtx->sessionNegotiate->peerPubKey.key, tlsCtx->sessionNegotiate->peerPubKey.keyLen);

        len = (((u16)p[0]) << 8) + p[1];
        p += 2;

        if (algorithm == SIGNATURE_ED25519) {
            if (len != EdDSA_SIGNATURE_LENGTH) {
                ret = LTTLS_ERROR(0x04C0 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
                break;
            }

            lt_memcpy(tmp->sigVal, p, EdDSA_SIGNATURE_LENGTH);
            if (0 != (ret = tlsCtx->config->crypto->VerifyEddsa(tmp->toVerify, 130, (u8 *)tmp->sigVal, (u8 *)tmp->pkVal))) {
                ret = LTTLS_ERROR(0x04C0 + 1, MBEDTLS_ERR_SSL_DECRYPT_ERROR + ret);
                break;
            }

        } else if (algorithm == SIGNATURE_ECDSA_SECP256R1_SHA256) {
            if (len < ECDSA_P256_SIGNATURE_LENGTH) {
                ret = LTTLS_ERROR(0x04C0 + 3, MBEDTLS_ERR_SSL_DECODE_ERROR);
                break;
            }

            LTSystemCryptoEncoder *cryptoEncoder = lt_createobject(LTSystemCryptoEncoder);
            if (!cryptoEncoder) {
                ret = LTTLS_ERROR(0x04C0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
                break;
            }
            bool bDecoded = cryptoEncoder->API->DecodeEcdsaSignature(p, len, (u8 *)tmp->sigVal, ECDSA_P256_SIGNATURE_LENGTH);
            lt_destroyobject(cryptoEncoder);
            if (!bDecoded) {
                ret = LTTLS_ERROR(0x04C0, MBEDTLS_ERR_X509_INVALID_SIGNATURE);
                break;
            }
            if (0 != (ret = tlsCtx->config->crypto->VerifyEcdsa(tmp->toVerify, 130, (u8 *)tmp->sigVal, (u8 *)tmp->pkVal))) {
                ret = LTTLS_ERROR(0x04C0 + 2, MBEDTLS_ERR_SSL_DECRYPT_ERROR + ret);
                break;
            }
        }

        if (0 != (ret = UpdateTranscript(tlsCtx, handshake->inHsHdr, handshake->inHsLen))) {
            ret = LTTLS_ERROR(0x04C0 + 2, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
            break;
        }

        ret = 0;
    } while (0);

    lt_free(tmp);
    return ret;
}

/**
 * @brief  Complete handshake
 *
 * @param[in,out] tlsCtx  The TLS context
 */
static void WrapupHandshake(LTTlsContext *tlsCtx) {
    FreeTransform(tlsCtx->transformHandshake);
    tlsCtx->transformHandshake = NULL;
    tlsCtx->transformOut = tlsCtx->transformApplication;
    tlsCtx->transformIn  = tlsCtx->transformApplication;

    /* Free the previous session and switch to the current one. */
    FreeSession(tlsCtx, tlsCtx->sessionData);
    tlsCtx->sessionData = tlsCtx->sessionNegotiate;
    tlsCtx->sessionNegotiate = NULL;

    /* Free handshake */
    FreeHandshake(tlsCtx->handshake, tlsCtx->config->crypto);
    tlsCtx->handshake = NULL;
}

/************************ End of handshake recving ***************************/

/*********************************** Session **********************************
 * Errors in these calls shall not trigger fatal alerts, but return error code.
 * Handshake and session control will use the error code to determine to send fatal alerts. */

// Error ID 0x0500
/**
 * @brief  Write key update handshake
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[in]     val    0 or 1
 * @return  Error code
 *
 *     enum {
 *         update_not_requested(0), update_requested(1), (255)
 *     } KeyUpdateRequest;
 *
 *     struct {
 *         KeyUpdateRequest request_update;
 *     } KeyUpdate;
 */
static int WriteKeyUpdate(LTTlsContext *tlsCtx, u8 val) {
    int ret = 0;

    LTTLS_CHK_BUF_PTR(0x0500, tlsCtx->outHdr, tlsCtx->outBufEnd, LTTLS_RECORD_HEADER_LEN + LTTLS_HANDSHAKE_HEADER_LEN + 1);
    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    u8 *p = tlsCtx->outMsg + LTTLS_HANDSHAKE_HEADER_LEN;
    p[0] = val;
    FinishHandshakeMsg(tlsCtx, 1, MBEDTLS_SSL_HS_KEY_UPDATE);
    // first send record with old key
    if (0 != (ret = WriteRecord(tlsCtx, true))) {
        return ret;
    }
    // then update key
    if (0 != (ret = UpdateSessionWriteKey(tlsCtx, true))) {
        return ret;
    }

    return 0;
}

// Error ID 0x0508
/**
 * @brief  Read the next application packet.
 *         We only support one application/alert/post-handshake packet per record.
 *         We don't support multiples packets in one record or one packet split over multiple records.
 *
 * @param[in,out] tlsCtx     The TLS context
 * @return   Error code
 */
static int ReadNextApplicationMsg(LTTlsContext *tlsCtx) {
    int ret = 0;
    if (tlsCtx->inRecDataLeft == 0) {
        do {
            ret = FetchNextRecord(tlsCtx);
        } while ((-MBEDTLS_ERR_SSL_NON_FATAL) == ((-ret) & 0xFF80));
        if (0 != ret) {
            return ret;
        }

    } else {
        // we assume one packet per record, so this branch shall not happen.
        // if it happens, return error and need to diagnose later.
        return LTTLS_ERROR(0x0508, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    P("read.app", "rec %u len %u", tlsCtx->inRecType, tlsCtx->inRecPayloadLen);

    tlsCtx->inRecDataLeft -= tlsCtx->inRecPayloadLen;
    return 0;
}

// Error ID 0x0510
/**
 * @brief  Process a received key updata
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding key update request (payload)
 * @param[out]    end    The end of buffer
 * @return  Error code
 */
static int ProcessKeyUpdate(LTTlsContext *tlsCtx, u8 *buf, u8 *end) {
    LTTLS_CHK_BUF_READ_PTR(0x0510, buf, end, 1);
    int ret = 0;

    // received KeyUpdate from server, so update server write secret and key
    if (0 != (ret = UpdateSessionWriteKey(tlsCtx, false))) {
        return ret;
    }

    // received update_requested(1), so send update_not_requested(0)
    if (buf[0] == 1) {
        if (0 != (ret = WriteKeyUpdate(tlsCtx, 0))) {
            return ret;
        }
    }

    return 0;
}

/** TODO need to validate with certificate request
 * @brief  Process certificate request in post handshake
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding certificate request (only payload)
 * @param[out]    end    The end of buffer
 * @return  Error code
 */
static int ProcessPostHsCertificateRequest(LTTlsContext *tlsCtx, u8 *buf, u8 *end) {
    int ret = 0;
    if ((0 == (ret = ParseCertificateRequest(tlsCtx, buf, end))) &&
        (0 == (ret = MakeClientCertificate(tlsCtx))) &&
        (0 == (ret = WriteRecord(tlsCtx, false))) &&
        (0 == (ret = MakeCertificateVerify(tlsCtx))) &&
        (0 == (ret = WriteRecord(tlsCtx, true))))
        {;}

    return ret;
}

// Error ID 0x0520
/**
 * @brief  Process a new session ticket
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[out]    buf    The buffer holding the ticket
 * @param[out]    end    The end of buffer
 * @return   Error code
 *
 *  struct {
 *     uint32 ticket_lifetime;
 *     uint32 ticket_age_add;
 *     opaque ticket_nonce<0..255>;
 *     opaque ticket<1..2^16-1>;
 *     Extension extensions<0..2^16-2>;
 * } NewSessionTicket;
 *
 * struct {
 *     select (Handshake.msg_type) {
 *         case new_session_ticket:   uint32 max_early_data_size;
 *         case client_hello:         Empty;
 *         case encrypted_extensions: Empty;
 *     };
 * } EarlyDataIndication;
 */
static int ProcessNewTicket(LTTlsContext *tlsCtx, u8 *buf, u8 *end) {
    u8 *p = buf;
    LTTlsSession *session = tlsCtx->sessionData;
    LTTlsSessionTicket *ticket = &session->ticket;
    ticket->receivedTime = LT_GetCore()->GetKernelTime();

    LTTLS_CHK_BUF_READ_PTR(0x0520 + 1, p, end, 4 + 4 + 1);
    ticket->lifeTime = ((u32)p[0] << 24) + ((u32)p[1] << 16) + ((u32)p[2] << 8) + p[3];
    p += 4;
    ticket->ageAdd = ((u32)p[0] << 24) + ((u32)p[1] << 16) + ((u32)p[2] << 8) + p[3];
    p += 4;
    u8 nonceLen = *p;
    ++p;

    LTTLS_CHK_BUF_READ_PTR(0x0520 + 2, p, end, nonceLen);
    u8 *nonceData = p;
    p += nonceLen;

    LTTLS_CHK_BUF_READ_PTR(0x0520 + 3, p, end, 2);
    u16 ticketLen = ((u16)p[0] << 8) + p[1];
    if (ticketLen < 1) {
        return LTTLS_ERROR(0x0520 + 4, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }
    p += 2;

    LTTLS_CHK_BUF_READ_PTR(0x0520 + 5, p, end, ticketLen);
    if (ticket->nonceTicket) {
        // an old ticket exist, so clear and free it.
        lt_memset(ticket->nonceTicket, 0, ticket->nonceLen + ticket->ticketLen);
        lt_free(ticket->nonceTicket);
    }
    ticket->nonceTicket = lt_malloc(nonceLen + ticketLen);
    if (!ticket->nonceTicket) {
        return LTTLS_ERROR(0x0520, MBEDTLS_ERR_SSL_ALLOC_FAILED);
    }
    lt_memcpy(ticket->nonceTicket, nonceData, nonceLen);
    lt_memcpy(ticket->nonceTicket + nonceLen, p, ticketLen);
    ticket->nonceLen = nonceLen;
    ticket->ticketLen = ticketLen;
    p += ticketLen;

    LTTLS_CHK_BUF_READ_PTR(0x0520 + 6, p, end, 2);
    ticket->extensionLen = ((u16)p[0] << 8) + p[1];
    p += 2;

    if (ticket->extensionLen != 0) {
        // TODO parsing extension
        LTTLS_CHK_BUF_READ_PTR(0x0520 + 7, p, end, ticket->extensionLen);
        u8 *extensionsEnd = p + ticket->extensionLen;
        u16 extensionType;
        u16 extensionDataLen;
        while (p < extensionsEnd) {
            /*
             * struct {
             *     ExtensionType extension_type; (2 bytes)
             *     opaque extension_data<0..2^16-1>;
             * } Extension;
             */
            LTTLS_CHK_BUF_READ_PTR(0x0520 + 7, p, extensionsEnd, 4);
            extensionType = (((u16)p[0]) << 8) + p[1];
            extensionDataLen = (((u16)p[2]) << 8) + p[3];
            p += 4;

            LTTLS_CHK_BUF_READ_PTR(0x0520 + 8, p, extensionsEnd, extensionDataLen);
            // only support early_data_indication, ignore all other extensions
            // suppose to send alert on unrecognized extensions
            switch (extensionType) {
                case MBEDTLS_TLS_EXT_EARLY_DATA:
                    if (extensionDataLen != 4) {
                        return LTTLS_ERROR(0x0520 + 1, MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION);
                    }
                    ticket->maxEarlyDataSize = ((u32)p[0] << 24) + ((u32)p[1] << 16) + ((u32)p[2] << 8) + p[3];
                    break;

                default:
                    ;
                    #if 0
                    return MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION;
                    #endif
            }

            p += extensionDataLen;
        }
    }

    if (p != end) {
        return LTTLS_ERROR(0x0520 + 9, MBEDTLS_ERR_SSL_DECODE_ERROR);
    }

    ticket->bValid = true;
    return 0;
}

// Error ID 0x0518
/**
 * @brief Process post handshake messages (handshake only)
 *
 * @param[in,out] tlsCtx  The TLS context
 * @param[in]     hsType  The handshake type
 * @param[out]    buf     The data buffer
 * @param[out]    end     The end of buffer
 * @return  Error code
 */
static int HandlePostHandshake(LTTlsContext *tlsCtx, u8 hsType, u8 *buf, u8 *end) {
    int ret = 0;

    switch (hsType) {
        case MBEDTLS_SSL_HS_NEW_SESSION_TICKET:
            ret = ProcessNewTicket(tlsCtx, buf, end);
            break;

        case MBEDTLS_SSL_HS_CERTIFICATE_REQUEST:  // TODO need to validate
            ret = ProcessPostHsCertificateRequest(tlsCtx, buf, end);
            break;

        case MBEDTLS_SSL_HS_KEY_UPDATE:
            ret = ProcessKeyUpdate(tlsCtx, buf, end);
            break;

        default:
            ret = LTTLS_ERROR(0x0518, MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE);
    }

    return ret;
}

/******************************** End of session ******************************/

/**************************** TLS public functions *****************************
 * Only these handshake and session control functions use the error code to send alerts. */

// Error ID 0x0600
/**
 * @brief  Initialize TLS context. This must be called before calling any TLS function.
 *
 * @param[in,out] tlsCtx     The TLS context
 * @param[in]     config  The TLS config. If NULL, keep current config.
 * @param[in]     options The TLS options. If NULL, keep current config.
 * @param[in]     bNew     True if init session, False if resume session
 * @return    Error code
 *
 * When resuming a session, the session data shall be saved before calling InitTLS.
 * Refer to ResumeSession as an example
 */
int InitTls(LTTlsContext *tlsCtx, LTTlsConfig *config, LTTlsOptions *options, bool bNew) {
    if (!tlsCtx) {
        return LTTLS_ERROR(0x0600 + 1, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
    }

    // NULL, so keep current config and options.
    if (!config) {
        config = tlsCtx->config;
    }
    if (!options) {
        options = tlsCtx->options;
    }

    lt_memset(tlsCtx, 0, sizeof(LTTlsContext));
    tlsCtx->config  = config;
    tlsCtx->options = options;

    // Now allocate missing structures
    tlsCtx->outBuf    = tlsCtx->outDataBuf;
    tlsCtx->outBufLen = LTTLS_OUT_BUFFER_LEN;
    tlsCtx->outHdr    = tlsCtx->outBuf;
    tlsCtx->outBufEnd = tlsCtx->outBuf + tlsCtx->outBufLen;
    tlsCtx->inBuf     = tlsCtx->inDataBuf;

    tlsCtx->transformHandshake = lt_malloc(sizeof(LTTlsTransform));
    tlsCtx->transformApplication = lt_malloc(sizeof(LTTlsTransform));
    tlsCtx->handshake = lt_malloc(sizeof(LTTlsHandshake));

    // All pointers should exist and can be directly freed without issue */
    if (!tlsCtx->outBuf || !tlsCtx->inBuf || !tlsCtx->handshake ||
        !tlsCtx->transformHandshake || !tlsCtx->transformApplication) {

        lt_free(tlsCtx->transformHandshake);
        tlsCtx->transformHandshake = NULL;
        lt_free(tlsCtx->transformApplication);
        tlsCtx->transformApplication = NULL;
        lt_free(tlsCtx->handshake);
        tlsCtx->handshake = NULL;
        return LTTLS_ERROR(0x0600 + 1, MBEDTLS_ERR_SSL_ALLOC_FAILED);
    }

    // Initialize structures
    lt_memset(tlsCtx->outBuf, 0, LTTLS_OUT_BUFFER_LEN);
    lt_memset(tlsCtx->inBuf, 0, LTTLS_IN_BUFFER_LEN);
    lt_memset(tlsCtx->transformHandshake, 0, sizeof(LTTlsTransform));
    lt_memset(tlsCtx->transformApplication, 0, sizeof(LTTlsTransform));

    // Initialize handshake
    int ret = 0;
    lt_memset(tlsCtx->handshake, 0, sizeof(LTTlsHandshake));
    if (0 != (ret = InitTranscript(tlsCtx))) {
        return LTTLS_ERROR(0x0600, MBEDTLS_ERR_SSL_INTERNAL_ERROR + ret);
    }
    // kex mode will be updated after parsing server hello's key share extension
    tlsCtx->handshake->kexModes = MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_ALL;

    // Init a new session
    if (bNew) {
        tlsCtx->sessionNegotiate = lt_malloc(sizeof(LTTlsSession));
        if (!tlsCtx->sessionNegotiate) {
            return LTTLS_ERROR(0x0600 + 2, MBEDTLS_ERR_SSL_ALLOC_FAILED);
        }
        lt_memset(tlsCtx->sessionNegotiate, 0, sizeof(LTTlsSession));
    }

    // Other inits
    tlsCtx->eState    = kLTTLSState_HS_CLIENT_HELLO;
    tlsCtx->majorVer  = MBEDTLS_SSL_MAJOR_VERSION_3;
    tlsCtx->minorVer  = MBEDTLS_SSL_MINOR_VERSION_4;
    tlsCtx->resendLen = 0;
    return 0;
}

/**
 * @brief Free TLS context and all enclosed resources. This is called when closing TLS.
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return    Error code
 */
void FreeTls(LTTlsContext *tlsCtx) {
    if (!tlsCtx) return;

    FreeTransform(tlsCtx->transformHandshake);
    FreeTransform(tlsCtx->transformApplication);
    FreeHandshake(tlsCtx->handshake, tlsCtx->config->crypto);
    FreeSession(tlsCtx, tlsCtx->sessionNegotiate);
    FreeSession(tlsCtx, tlsCtx->sessionData);
    FreeCertRequest(tlsCtx);

    lt_memset(tlsCtx->outBuf, 0, LTTLS_OUT_BUFFER_LEN);
    lt_memset(tlsCtx->inBuf, 0, LTTLS_IN_BUFFER_LEN);
    *tlsCtx = (LTTlsContext){};
}

/**
 * @brief Write application data.
 *        For a large data block, write only one max TLS record, and return the size of one TLS record.
 *        Upper stack needs to write the rest of data in a later call on write-ready event.
 *
 * @param[in,out] tlsCtx The TLS context
 * @param[in]     data   The data
 * @param[in]     len    The length of data
 * @return  The length of written data. Negative number means error code. Guarantee not to return 0.
 */
s32 WriteApplicationData(LTTlsContext *tlsCtx, const u8 *data, LT_SIZE len) {
    if (tlsCtx->outHdr > tlsCtx->outBufEnd ||
        (LTTLS_RECORD_HEADER_LEN + 1 > (LT_SIZE)tlsCtx->outBufEnd - (LT_SIZE)tlsCtx->outHdr)) {
        return -1;
    }

    tlsCtx->outMsg = tlsCtx->outHdr + LTTLS_RECORD_HEADER_LEN;
    tlsCtx->outMsgType = MBEDTLS_SSL_MSG_APPLICATION_DATA;
    u32 bytes = (len < LTTLS_OUT_RECORD_MAXLEN) ? len : LTTLS_OUT_RECORD_MAXLEN;
    lt_memcpy(tlsCtx->outMsg, data, bytes);
    tlsCtx->outMsgLen = bytes;
    // After write (success or fail), tslCtx->outHdr is set to tlsCtx->outBuf.
    int err = WriteRecord(tlsCtx, true);
    if (0 != err) LogTlsError(tlsCtx->eState, tlsCtx->eState, err);
    // All bytes must be sent out. No zero length.
    return (err == 0) ? (s32)bytes : -2;
}

// Error ID 0x0610
/**
 * @brief  Handshake handling function
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int Handshake(LTTlsContext *tlsCtx) {
    /* Sanity checks */
    if (!tlsCtx || !tlsCtx->handshake) {
        return LTTLS_ERROR(0x0610 + 1, MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
    }

    int ret = 0;

    /* bLoop indicates if the main handshake needs to continue loop after processing one write or one read.
     *
     * True, in one of the following cases:
     * (1) multiple messages in one record, i.e. tlsCtx->nInRecDataLeft > 0,
     * (2) a record is peeked in the loop (the record is already removed from the socket buffer), i.e. pTls->bKeepInMsg == true,
     * (3) write multiple records in a row.
     *
     * False in one the following cases:
     * (1) no more message in the current record, i.e. pTls->nInRecDataLeft == 0,
     * (2) a record is read in the loop and complete processed, i.e. pTls->bKeepInMsg == false,
     * (3) only write one record.
     *
     * Start with default, True */
    bool bLoop = true;
    LTTlsState eCurState;
    /* Main handshake loop */
    while (bLoop) {

        eCurState = tlsCtx->eState;
        switch (tlsCtx->eState) {
            /* normal handshake */
            case kLTTLSState_HS_CLIENT_HELLO:
                ret = WriteClientHello(tlsCtx);
                tlsCtx->bHrr = false;  // clear bHrr flag
                tlsCtx->eState = kLTTLSState_HS_SERVER_HELLO;
                bLoop = false;
                break;

            case kLTTLSState_HS_SERVER_HELLO:
                ret = ProcessServerHello(tlsCtx);
                if (tlsCtx->bHrr) {
                    // Hello retry request, so resend client hello
                    tlsCtx->eState = kLTTLSState_HS_CLIENT_HELLO;
                    if (tlsCtx->inRecDataLeft != 0) {
                        TraceError(ret);
                        ret = LTTLS_ERROR(0x0610 + 2, MBEDTLS_ERR_SSL_BAD_INPUT_DATA); // no more message after HRR
                    }

                } else {
                    tlsCtx->eState = kLTTLSState_HS_SERVER_ENCRYPTED_EXTENSIONS;
                    if (tlsCtx->inRecDataLeft == 0) {
                        bLoop = false;
                    }
                }
                break;

            case kLTTLSState_HS_SERVER_ENCRYPTED_EXTENSIONS:
                ret = ProcessEncryptedExtensions(tlsCtx);
                tlsCtx->eState = kLTTLSState_HS_SERVER_CERTIFICATE_REQUEST;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_HS_SERVER_CERTIFICATE_REQUEST:
                ret = ProcessCertificateRequest(tlsCtx);
                tlsCtx->eState = kLTTLSState_HS_SERVER_CERTIFICATE;
                // Received certificate request, so bLoop = false to break the loop to read SERVER_CERTIFICATE.
                // Otherwise, bLoop = true to continue the loop because SERVER_CERTIFICATE is already peeked.
                if (!tlsCtx->bKeepInMsg && tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_HS_SERVER_CERTIFICATE:
                ret = ProcessServerCertificate(tlsCtx);
                tlsCtx->eState = kLTTLSState_HS_SERVER_CERTIFICATE_VERIFY;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_HS_SERVER_CERTIFICATE_VERIFY:
                ret = ProcessServerCertificateVerify(tlsCtx);
                tlsCtx->eState = kLTTLSState_HS_SERVER_FINISHED;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_HS_SERVER_FINISHED:
                ret = ProcessServerFinished(tlsCtx);
                if (tlsCtx->inRecDataLeft != 0) {
                    TraceError(ret);
                    ret = LTTLS_ERROR(0x0610 + 3, MBEDTLS_ERR_SSL_BAD_INPUT_DATA); // no more message after SERVER_FINISHED
                }
                /* In compatibility mode, suppose to send Change Cipher Specs after processing Server Finished. */
                tlsCtx->eState = kLTTLSState_HS_CLIENT_CHANGE_CIPHER_SPEC;
                break;

            case kLTTLSState_HS_CLIENT_CHANGE_CIPHER_SPEC:
                bLoop = false;
                // write CLIENT_CHANGE_CIPHER_SPEC
                if (0 != (ret = WriteChangeCipherSpec(tlsCtx))) break;

                // mTLS required
                if (tlsCtx->bClientAuth) {
                    // write CLIENT_CERTIFICATE
                    tlsCtx->eState = kLTTLSState_HS_CLIENT_CERTIFICATE;
                    if (0 != (ret = WriteClientCertificate(tlsCtx))) break;
                    // write CLIENT_CERTIFICATE_VERIFY
                    tlsCtx->eState = kLTTLSState_HS_CLIENT_CERTIFICATE_VERIFY;
                    if (0 != (ret = WriteClientCertificateVerify(tlsCtx))) break;
                    tlsCtx->bClientAuth = false; // clear the flag of client needs to authenticate with certificate.
                }

                // write CLIENT_FINISHED
                tlsCtx->eState = kLTTLSState_HS_CLIENT_FINISHED;
                if (0 != (ret = WriteClientFinished(tlsCtx))) break;

                // wrap up handshake
                tlsCtx->eState = kLTTLSState_HS_HANDSHAKE_WRAPUP;
                WrapupHandshake(tlsCtx);
                tlsCtx->eState = kLTTLSState_APPLICATION;
                break;

            /* session resume */
            case kLTTLSState_SR_CLIENT_HELLO:
                ret = WriteClientHello(tlsCtx);
                tlsCtx->eState = kLTTLSState_SR_SERVER_HELLO;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_SR_SERVER_HELLO:
                ret = ProcessServerHello(tlsCtx);
                tlsCtx->eState = kLTTLSState_SR_SERVER_ENCRYPTED_EXTENSIONS;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            case kLTTLSState_SR_SERVER_ENCRYPTED_EXTENSIONS:
                ret = ProcessEncryptedExtensions(tlsCtx);
                tlsCtx->eState = kLTTLSState_HS_SERVER_FINISHED;
                if (tlsCtx->inRecDataLeft == 0) {
                    bLoop = false;
                }
                break;

            default:
                ret = LTTLS_ERROR(0x0610 + 4, MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
        }

        if (0 != ret) {
            LogTlsError(eCurState, tlsCtx->eState, ret);
            SendFatalAlert(tlsCtx, ret);
            return ret;
        }

    }

    return ret;
}

// Error ID 0x0620
/**
 * @brief  Session handling function
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int Session(LTTlsContext *tlsCtx) {
    if (!tlsCtx || !tlsCtx->sessionData || tlsCtx->eState != kLTTLSState_APPLICATION) {
        return LTTLS_ERROR(0x0620, MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
    }

    int ret = 0;

    if (0 == (ret = ReadNextApplicationMsg(tlsCtx))) {
        /* Alerts and Cipher Change Spec are handled or ignored by FetchNextRecord.
        * So, only post handshake and application packets can reach here*/
        switch (tlsCtx->inRecType) {
            case MBEDTLS_SSL_MSG_HANDSHAKE:
            {
                u8 *inHsHdr = tlsCtx->inRecPayload;
                LTTLS_CHK_BUF_READ_PTR(0x0620 + 1, inHsHdr, tlsCtx->inRecEnd, LTTLS_HANDSHAKE_HEADER_LEN);
                // we only support < 2^16 bytes of handshake data.
                if (inHsHdr[1] != 0) {
                    return LTTLS_ERROR(0x0620 + 2, MBEDTLS_ERR_SSL_DECODE_ERROR);
                }
                u16 inHsLen = LTTLS_HANDSHAKE_HEADER_LEN + (((LT_SIZE)inHsHdr[2]) << 8) + inHsHdr[3];
                LTTLS_CHK_BUF_READ_PTR(0x0620 + 3, inHsHdr, tlsCtx->inRecEnd, inHsLen);
                u8 *buf = inHsHdr + LTTLS_HANDSHAKE_HEADER_LEN;
                u8 *end = inHsHdr + inHsLen;
                ret = HandlePostHandshake(tlsCtx, inHsHdr[0], buf, end);
                break;
            }

            case MBEDTLS_SSL_MSG_APPLICATION_DATA:
                ret = 0;
                break;

            default:
                ret = LTTLS_ERROR(0x0620, MBEDTLS_ERR_SSL_INVALID_RECORD);
        }
    }

    if (0 != ret && ((-ret) & 0xFF80) != (-MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
        LogTlsError(tlsCtx->eState, tlsCtx->eState, ret);
        SendFatalAlert(tlsCtx, ret);
    }

    return ret;
}

/**
 * @brief  Restart a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int RestartSession(LTTlsContext *tlsCtx) {
    ResetTls(tlsCtx);
    Handshake(tlsCtx);
    return 0;
}

/**
 * @brief  Resume a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int ResumeSession(LTTlsContext *tlsCtx) {
    // save the session
    LTTlsSession *session = tlsCtx->sessionData;
    tlsCtx->sessionData = NULL;

    // refer to FreeTLS to see what to free and what to keep for resuming session
    FreeTransform(tlsCtx->transformApplication);
    tlsCtx->transformApplication = NULL;
    lt_free(tlsCtx->certReqContext);
    tlsCtx->certReqContext = NULL;

    tlsCtx->resendLen = 0;
    InitTls(tlsCtx, NULL, NULL, false);

    // switch to session negotiate
    tlsCtx->sessionNegotiate = session;

    tlsCtx->eState = kLTTLSState_SR_CLIENT_HELLO;
    Handshake(tlsCtx);
    tlsCtx->eState = kLTTLSState_APPLICATION;
    return 0;
}

/**
 * @brief  Update a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int UpdateSession(LTTlsContext *tlsCtx) {
    return WriteKeyUpdate(tlsCtx, 1);
}

/**
 * @brief  Send a CloseNotify to close the session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
int CloseSession(LTTlsContext *tlsCtx) {
    return SendAlertMessage(tlsCtx, MBEDTLS_SSL_ALERT_LEVEL_WARNING, MBEDTLS_SSL_ALERT_MSG_CLOSE_NOTIFY);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  21-May-22   gallienus   created
 */
