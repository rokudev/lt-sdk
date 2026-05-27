/*******************************************************************************
 * source/lt/system/crypto/LTSystemCryptoEncoder.c
 *
 * This library provides APIs to encode and decode crypto data.
 * This library does not provide cryptos.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("crypto.encoder")
#define P(...)

static const char *s_kPreCert        = "-----BEGIN CERTIFICATE-----\n";
static const char *s_kPostCert       = "\n-----END CERTIFICATE-----\n";
static const char *s_kPrePrivateKey  = "-----BEGIN EC PRIVATE KEY-----\n";
static const char *s_kPostPrivateKey = "\n-----END EC PRIVATE KEY-----\n";

LTSystemCryptoResult LTSystemCrypto_GenEcdsaPublicKey(const u8 privateKey[ECDSA_P256_PRIVATEKEY_LENGTH], u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH]);

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTSystemCryptoEncoder, LTSystemCryptoEncoderImpl) {
} LTOBJECT_API;

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTSystemCryptoEncoderImpl_DestructObject(LTSystemCryptoEncoderImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTSystemCryptoEncoderImpl_ConstructObject(LTSystemCryptoEncoderImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

/**
 * @brief Encode Ecdsa signature to ANSI X9.62, https://datatracker.ietf.org/doc/html/rfc3279, Section 2.2.3
 *      Ecdsa-Sig-Value  ::=  SEQUENCE  {
 *          r     INTEGER,
 *          s     INTEGER  }
 *      ANSI X9.62 of ECDSA-SECP256R1-SHA256 signature
 * @param[in]  x962Signature  The signature in X9.62 format, cannot be NULL.
 * @param[in]  sigLen         The length of signature, in bytes, must be a even number, but cannot be 0.
 * @param[in]  x962MaxLen     The max length of x962Signature, in bytes, i.e. the x962Signature buffer size.
 * @param[out] x962Len        The output length of x962Signature, in bytes, cannot be NULL.
 * @return true if encode succeeds.
 * @return false on failure.
 */
bool LTSystemCryptoEncoderImpl_EncodeEcdsaSignature(const u8 *signature, u16 sigLen, u8 *x962Signature, u16 x962MaxLen, u16 *x962Len) {
    if (!x962Signature || !x962Len || !sigLen || (sigLen & 1) || !signature || x962MaxLen < 6) return false;

    u8 *p = x962Signature;
    u8 *end = x962Signature + x962MaxLen;
    // 0x30,0xseqLen
    if (p + 5 > end) return false;
    *p = 0x30; // ASN1_CONSTRUCTED | ASN1_SEQUENCE
    ++p;
    u8 *seqLen = p;
    ++p;

    // R: 0x02,0x??,R
    *p = 0x02; // ASN1_INTEGER;
    ++p;
    u8 i = 0;
    for (; i < sigLen / 2 && signature[i] == 0; ++i) ;
    u8 len = sigLen / 2 - i;
    if (len == 0 || signature[i] >= 0x80) { // all zero, or the first byte is negative.
        *p = len + 1;
        ++p;
        *p = 0;
    } else {
        *p = len;
    }
    ++p;
    if (p + len > end) return false;
    lt_memcpy(p, signature + i, len);
    p += len;

    // S: 0x02,0x2?,S
    if (p + 3 > end) return false;
    *p = 0x02; // ASN1_INTEGER;
    ++p;
    i = sigLen / 2;
    for (; i < sigLen && signature[i] == 0; ++i) ;
    len = sigLen - i;
    if (len == 0 || signature[i] >= 0x80) { // all zero, or the first byte is negative.
        *p = len + 1;
        ++p;
        *p = 0;
    } else {
        *p = len;
    }
    ++p;
    if (p + len > end) return false;
    lt_memcpy(p, signature + i, len);
    p += len;

    *x962Len = p - x962Signature;
    *seqLen = (*x962Len) - 2;
    return true;
}

/**
 * @brief Decode Ecdsa signature, https://datatracker.ietf.org/doc/html/rfc3279, Section 2.2.3
 *      Ecdsa-Sig-Value  ::=  SEQUENCE  {
 *          r     INTEGER,
 *          s     INTEGER  }
 *      ANSI X9.62 of ECDSA-SECP256R1-SHA256 signature
 *      If signature is NULL, only check if the x962Signature is valid.
 * @param[in]  x962Signature  The signature in X9.62 format, cannot be NULL.
 * @param[in]  x962Len        The length of x962Signature, in bytes, cannot be 0.
 * @param[out] signature      The signature of R || S, could be NULL.
 * @param[in]  sigLen         The length of signature, in bytes, must be a even number, but cannot be 0.
 * @return true if signature is valid.
 * @return false on failure.
 */
bool LTSystemCryptoEncoderImpl_DecodeEcdsaSignature(const u8 *x962Signature, u16 x962Len, u8 *signature, u16 sigLen) {
    if (!x962Signature || !x962Len || !sigLen || (sigLen & 1)) return false;

    if (signature) lt_memset(signature, 0, sigLen);

    const u8 *p = x962Signature;
    const u8 *end = x962Signature + x962Len;
    // check header, ASN1_CONSTRUCTED | ASN1_SEQUENCE
    if ((*p) != 0x30) return false;
    ++p;
    if ((*p) != (end - p - 1)) return false;
    ++p;

    // parse R, ASN1_INTEGER
    if ((*p) != 0x02) return false;
    ++p;
    u8 len = (*p);
    ++p;
    if (len > (sigLen / 2 + 1)) return false;
    // TODO: shall we check if the first byte < 0x80?
    if (len == (sigLen / 2 + 1)) {
        // len = 0x21
        --len;
        ++p;
    }
    // now len <= 0x20, p points to the start of R.
    if (signature) lt_memcpy(signature + sigLen / 2 - len, p, len);
    p += len;

    // parse S, ASN1_INTEGER
    if ((*p) != 0x02) return false;
    ++p;
    len = (*p);
    ++p;
    if (len > (sigLen / 2 + 1)) return false;
    // TODO: shall we check if the first byte < 0x80?
    if (len == (sigLen / 2 + 1)) {
        // len = 0x21
        --len;
        ++p;
    }
    // now len <= 0x20, p points to the start of S.
    if (signature) lt_memcpy(signature + sigLen - len, p, len);
    p += len;

    // end check
    if (p != end) return false;

    return true;
}

// Encode one DER data to PEM, not a chain.
static u32 EncodePem(const u8 *derData, u32 derLength, char *pemData, u32 pemLength, const char *prePem, const char *postPem) {
    LTUtilityByteOps *byteOps = lt_openlibrary(LTUtilityByteOps);
    if (!byteOps) return 0;

    if (!pemData) {
        // Calculate the required output buffer length. One or two chars larger than actual length, but will terminate with '\0'.
        u32 len = lt_strlen(prePem) + byteOps->GetBase64EncodeBufferRequirement(derLength) + lt_strlen(postPem) + 1;
        lt_closelibrary(byteOps);
        return len;
    }

    u32 offset = lt_strlen(prePem);
    char *p = pemData;
    lt_memcpy(p, prePem, offset);
    p += offset;
    offset = byteOps->Base64Encode(derData, derLength, p, pemLength);
    lt_closelibrary(byteOps);
    p += offset;
    offset = lt_strlen(postPem);
    lt_memcpy(p, postPem, offset);
    p += offset;
    *p = 0;  // Force '\0'.
    return p - pemData; // Same as lt_strlen().
}

static u32 LTSystemCryptoEncoderImpl_ConvertDerToPem(LTSystemCryptoEncoding type, const u8 *derData, u32 derLength, char *pemData, u32 pemLength) {
    if (!derData || !derLength) return false;
    switch (type) {
        case kLTSystemCryptoEncoding_Certificate_DER:
            return EncodePem(derData, derLength, pemData, pemLength, s_kPreCert, s_kPostCert);
            break;

        case kLTSystemCryptoEncoding_PrivateKey_DER:
            return EncodePem(derData, derLength, pemData, pemLength, s_kPrePrivateKey, s_kPostPrivateKey);
            break;

        default:
        P("unknown", "%d", type);
    }
    return 0;
}

static u32 EncodeEcdsaPrivateKeyToDer(const LTPrivateKey *privateKey, u8 **derData) {
    u8 pubKey[ECDSA_P256_PUBLICKEY_LENGTH];
    u32 derLen = 0;
    if (kLTSystemCrypto_Result_Ok == LTSystemCrypto_GenEcdsaPublicKey(privateKey->key, pubKey)) {
        /* Private key DER format
        = b"\x30\x77" + b"\x02\x01\x01" + b"\x04\x20" \
        + prikey \
        + b"\xa0\x0a" + b"\x06\x08" + b"\x2a\x86\x48\xce\x3d\x03\x01\x07" \
        + b"\xa1\x44" + b"\x03\x42" + b"\x00\x04" \
        + pubkeyx + pubkeyy
        */
        derLen = 7 + ECDSA_P256_PRIVATEKEY_LENGTH + 12 + 6 + ECDSA_P256_PUBLICKEY_LENGTH;
        u8 *der = lt_malloc(derLen);
        if (der) {
            u8 *p = der;
            lt_memcpy(p, "\x30\x77\x02\x01\x01\x04\x20", 7);
            p += 7;
            lt_memcpy(p, privateKey->key, ECDSA_P256_PRIVATEKEY_LENGTH);
            p += ECDSA_P256_PRIVATEKEY_LENGTH;
            lt_memcpy(p, "\xa0\x0a\x06\x08\x2a\x86\x48\xce\x3d\x03\x01\x07\xa1\x44\x03\x42\x00\x04", 18);
            p += 18;
            lt_memcpy(p, pubKey, ECDSA_P256_PUBLICKEY_LENGTH);
            *derData = der;
        } else {
            derLen = 0;
            LTLOG_YELLOWALERT("priv.key.der.oom", NULL);
        }
    } else {
        LTLOG_YELLOWALERT("gen.pub.key.fail", NULL);
    }
    return derLen;
}

static u32 LTSystemCryptoEncoderImpl_EncodePrivateKeyToPem(const LTPrivateKey *privateKey, char **pemData) {
    if (!privateKey || !pemData) return false;

    // Encode to DER
    u8 *derData = NULL;
    u32 derLen = 0;
    if (privateKey->type == SIGNATURE_ECDSA_SECP256R1_SHA256) {
        derLen = EncodeEcdsaPrivateKeyToDer(privateKey, &derData);
    } else {
        LTLOG_YELLOWALERT("not.support", "key type 0x%04X", privateKey->type);
        return 0;
    }

    // Convert DER to PEM
    u32 pemLen = 0;
    if (derLen > 0) {
        pemLen = LTSystemCryptoEncoderImpl_ConvertDerToPem(kLTSystemCryptoEncoding_PrivateKey_DER, derData, derLen, NULL, 0);
        char *pem = lt_malloc(pemLen);
        if (pem) {
            pemLen = LTSystemCryptoEncoderImpl_ConvertDerToPem(kLTSystemCryptoEncoding_PrivateKey_DER, derData, derLen, pem, pemLen);
            *pemData = pem;
            P("enc.key.pem", "len %lu", LT_Pu32(pemLen));
        } else {
            pemLen = 0;
            LTLOG_YELLOWALERT("priv.key.pem.oom", NULL);
        }
        lt_free(derData);

    } else {
        LTLOG_YELLOWALERT("der.err", NULL);
    }

    return pemLen;
}

static u32 LTSystemCryptoEncoderImpl_EncodeCertChainToPem(const u8 *certChain, u32 certLength, char **pemData) {
    if (!certChain || !certLength || !pemData) return false;

    // First round, find out the total length of output buffer.
    const u8 *p = certChain;
    u32 totalLen = 0;
    u32 pemLen = 0;
    u16 certLen = 0;
    bool bValidCert = false;
    while (p < certChain + certLength) {
        bValidCert = false;
        // A cert has a cert length (3 Bytes big endian) + DER cert + "\x00\x00"
        if (*p != 0) break;
        ++p;
        certLen = (((u16)(*p)) << 8) + *(p + 1);
        p += 2;
        if (p + certLen > certChain + certLength) break;
        pemLen = LTSystemCryptoEncoderImpl_ConvertDerToPem(kLTSystemCryptoEncoding_Certificate_DER, p, certLen, NULL, 0);
        p += certLen;
        totalLen += pemLen;
        p += 2;
        P("enc.cert.pem.1", "len %lu/%lu", LT_Pu32(certLen), LT_Pu32(pemLen));
        bValidCert = true;
    }
    if (!bValidCert || totalLen == 0) {
        LTLOG_YELLOWALERT("cert.chain.err", NULL);
        return 0;
    }
    P("enc.cert.pem.1", "total %lu", LT_Pu32(totalLen));

    // Second round, encode each certificate in the chain.
    // Since the first round has validated the format of the chain, so no validation in the second round.
    char *pem = lt_malloc(totalLen);
    u32 bufLen = totalLen;
    totalLen = 0;
    if (pem) {
        p = certChain;
        char *m = pem;
        while (p < certChain + certLength) {
            // A cert has a cert length (3 Bytes big endian) + DER cert + "\x00\x00"
            ++p;
            certLen = (((u16)(*p)) << 8) + *(p + 1);
            p += 2;
            pemLen = LTSystemCryptoEncoderImpl_ConvertDerToPem(kLTSystemCryptoEncoding_Certificate_DER, p, certLen, m, bufLen);
            p += certLen;
            m += pemLen;
            totalLen += pemLen;
            bufLen -= pemLen;
            p += 2;
            P("enc.cert.pem.2", "len %lu/%lu", LT_Pu32(certLen), LT_Pu32(pemLen));
        }
        *pemData = pem;
    } else {
        LTLOG_YELLOWALERT("cert.pem.oom", NULL);
    }
    P("enc.cert.pem.2", "total %lu", LT_Pu32(totalLen));
    return totalLen;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTSystemCryptoEncoder, LTSystemCryptoEncoderImpl,
    EncodeEcdsaSignature,
    DecodeEcdsaSignature,
    ConvertDerToPem,
    EncodePrivateKeyToPem,
    EncodeCertChainToPem,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Dec-24   gallienus   created
 *  06-Jun-25   gallienus   refactored to crypto objects
 */
