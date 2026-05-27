/******************************************************************************
 * source/lt/net/x509/LTNetX509.c         Implementation of X509 parser
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *
 * This file has been created to complete X509 operations for LT, reduce code
 * and data sizes, and be compatible with LT coding standard.
 *
 * This file has modified the source code from https://github.com/Mbed-TLS/mbedtls
 *
 * The modified source files are asn1parse.c, oid.c, pkparse.c, x509.c,
 * x509_crl.c, x509_crt.c, x509_csr.c in Mbed TLS.
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
 *****************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/net/tls/LTNetTlsErrors.h>
#include <lt/net/x509/LTNetX509.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/timezone/LTSystemTimeZone.h>

#include "LTNetX509Defs.h"
#include "LTNetX509Types.h"

DEFINE_LTLOG_SECTION("x509");
#define P(...)

static LTCore *s_core = NULL;

#define MBEDTLS_ERROR_ADD(x, y)   ((x) + (y))

// Signature algorithms. TODO add more signatures if needed.
static const LTOidSigAlg s_oidSigAlg[] = {
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA256,     "ecdsa-with-SHA256",        "1.2.840.10045.4.3.2"),
        MBEDTLS_MD_SHA256,   MBEDTLS_PK_ECDSA,
    },
    {
        OID_DESCRIPTOR(LTTLS_OID_ED25519,            "ED25519",             "ED25519"),
        MBEDTLS_MD_NONE,     LTTLS_PK_ED25519,
    },
#if 0
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA256,     "sha256WithRSAEncryption",  "RSA with SHA-256"),
        MBEDTLS_MD_SHA256,   MBEDTLS_PK_RSA,
    },
#endif
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_MD_NONE,     MBEDTLS_PK_NONE,
    },
};

// Supported X509 extensions. TODO add more extension if needed.
static const LTOidX509Ext s_oidX509Ext[] = {
    {
        OID_DESCRIPTOR(MBEDTLS_OID_SUBJECT_KEY_IDENTIFIER,   "id-ce-subjectKeyIdentifier",   "Subject Key Identifier"),
        MBEDTLS_OID_X509_EXT_SUBJECT_KEY_IDENTIFIER,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AUTHORITY_KEY_IDENTIFIER, "id-ce-authorityKeyIdentifier", "Authority Key Identifier"),
        MBEDTLS_OID_X509_EXT_AUTHORITY_KEY_IDENTIFIER,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_BASIC_CONSTRAINTS,    "id-ce-basicConstraints",    "Basic Constraints"),
        MBEDTLS_OID_X509_EXT_BASIC_CONSTRAINTS,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_KEY_USAGE,            "id-ce-keyUsage",            "Key Usage"),
        MBEDTLS_OID_X509_EXT_KEY_USAGE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EXTENDED_KEY_USAGE,   "id-ce-extKeyUsage",         "Extended Key Usage"),
        MBEDTLS_OID_X509_EXT_EXTENDED_KEY_USAGE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_SUBJECT_ALT_NAME,     "id-ce-subjectAltName",      "Subject Alt Name"),
        MBEDTLS_OID_X509_EXT_SUBJECT_ALT_NAME,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_NS_CERT_TYPE,         "id-netscape-certtype",      "Netscape Certificate Type"),
        MBEDTLS_OID_X509_EXT_NS_CERT_TYPE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_CERTIFICATE_POLICIES, "id-ce-certificatePolicies", "Certificate Policies"),
        MBEDTLS_OID_X509_EXT_CERTIFICATE_POLICIES,
    },
    {
        NULL_OID_DESCRIPTOR,
        0,
    },
};

// Public key algorithm OIDs. TODO add more public key OIDs if needed.
static const LTOidPkAlg s_oidPkAlg[] = {
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_ALG_UNRESTRICTED,    "id-ecPublicKey",        "1.2.840.10045.2.1"),
        MBEDTLS_PK_ECDSA,
    },
    {
        OID_DESCRIPTOR(LTTLS_OID_ED25519,           "ED25519",                    "EDDSA"),
        LTTLS_PK_ED25519,
    },
#if 0
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA256,    "sha256WithRSAEncryption",    "RSA SHA256"),
        MBEDTLS_PK_RSA,
    },
#endif
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_PK_NONE,
    },
};

/*************************** OID and ASN1 functions ***************************
 * These functions only return 1-byte error code. So, they don't need error id.
 */

/**
 * @brief  Get signature algorithm
 *
 * @param[in]  oid    The pointer to the OID ASN1 buffer
 * @param[out] mdAlg  The pointer to the hash algorithm ID
 * @param[out] pkAlg  The pointer to the public key algorithm ID
 * @return  Error code
 */
static int LTOidGetSigAlg(const LTAsn1Buf *oid, u8 *mdAlg, u16 *pkAlg) {
    const LTOidSigAlg *p = s_oidSigAlg;
    const LTOidDescriptor *curr = (const LTOidDescriptor *)p;

    while (curr->asn1 != NULL) {
        if (curr->asn1Len == oid->len && 0 == lt_memcmp(curr->asn1, oid->p, oid->len)) {
            break;
        }
        ++p;
        curr = (const LTOidDescriptor *)p;
    }

    if (!curr->asn1) {
        return MBEDTLS_ERR_OID_NOT_FOUND;
    }

    *mdAlg = p->mdAlg;
    *pkAlg = p->pkAlg;

    return 0;
}

/**
 * @brief  Get X509 extension type
 *
 * @param[in]  oid      The pointer to the OID ASN1 buffer
 * @param[out] extType  The pointer to the buffer to save the extension type
 * @return  Error code
 */
static int LTOidGetX509ExtType(const LTAsn1Buf *oid, int *extType) {
    const LTOidX509Ext *p = s_oidX509Ext;
    const LTOidDescriptor *curr = (const LTOidDescriptor *)p;

    while (curr->asn1 != NULL) {
        if (curr->asn1Len == oid->len && 0 == lt_memcmp(curr->asn1, oid->p, oid->len)) {
            break;
        }
        ++p;
        curr = (const LTOidDescriptor *)p;
    }

    if (!curr->asn1) {
        return MBEDTLS_ERR_OID_NOT_FOUND;
    }

    *extType = p->extType;

    return 0;
}

/**
 * @brief  Get public key algorithm
 *
 * @param[in]  oid    The pointer to the OID ASN1 buffer
 * @param[out] pkAlg  The pointer to the public key algorithm ID
 * @return  Error code
 */
static int LTOidGetPkAlg(const LTAsn1Buf *oid, u16 *pkAlg) {
    const LTOidPkAlg *p = s_oidPkAlg;
    const LTOidDescriptor *curr = (const LTOidDescriptor *)p;

    while (curr->asn1 != NULL) {
        if (curr->asn1Len == oid->len && 0 == lt_memcmp(curr->asn1, oid->p, oid->len)) {
            break;
        }
        ++p;
        curr = (const LTOidDescriptor *)p;
    }

    if (!curr->asn1) {
        return MBEDTLS_ERR_OID_NOT_FOUND;
    }

    *pkAlg = p->pkAlg;

    return 0;
}

/** Only support 2^16 bytes
 * @brief  Get the ASN1 length.
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The end of ASN1 buf
 * @param[out]    len  The length of the ASN1
 * @return  Error code
 * @note    (*pp) will be moved to point to the byte right after the length field.
 */
static int LTAsn1GetLen(u8 **pp, const u8 *end, u16 *len) {
    if ((end - *pp) < 1) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 1;
    }

    if (0 == (**pp & 0x80)) {
        // a short form length, <= 127
        *len = *(*pp);
        ++(*pp);

    } else {
        // a long form length
        switch (**pp & 0x7F) {

            case 1: // 1-byte length
                if ((end - *pp) < 2) {
                    return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 2;
                }

                *len = (*pp)[1];
                (*pp) += 2;
                break;

            case 2: // 2-byte length
                if ((end - *pp) < 3) {
                    return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 3;
                }

                *len = ((u16)(*pp)[1] << 8 ) | (*pp)[2];
                (*pp) += 3;
                break;

            case 3: // 3-byte length
                if ((end - *pp) < 4) {
                    return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 4;
                }
                if (0 != (*pp)[1]) {
                    return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 1;
                }
                *len = ((u16)(*pp)[2] << 8) | (*pp)[3];
                (*pp) += 4;
                break;

            case 4: // 4-byte length
                if ((end - *pp) < 5) {
                    return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 5;
                }
                if (0 != (*pp)[1] || 0 != (*pp)[2]) {
                    return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 2;
                }
                *len = ((u16)(*pp)[3] << 8) | (*pp)[4];
                (*pp) += 5;
                break;

            default:
                return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 3;
        }
    }

    if (*len > (LT_SIZE)(end - *pp)) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA - 6;
    }

    return 0;
}

/**
 * @brief  Get and check the ASN1 tag and get the ASN1 length
 *
 * @param[in,out] pp    The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The end of ASN1 buf
 * @param[out]    len  The length of the ASN1
 * @param[in]     tag  The expected tag
 * @return  Error code
 * @note    (*pp) will be moved to point to the byte right after the tag and the length field.
 */
static int LTAsn1GetTag(u8 **pp, const u8 *end, u16 *len, u8 tag) {
    if ((end - *pp) < 1) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA;
    }
    if (**pp != tag) {
        return MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
    }

    ++(*pp);  // now length
    return LTAsn1GetLen(pp, end, len);
}

/**
 * @brief  Get integer
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The end of ASN1 buf
 * @param[out]    val  The pointer to the buffer to save the integer
 * @return  Error code
 */
static int LTAsn1GetInt(u8 **pp, const u8 *end, int *val) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_INTEGER)))
        return ret;

    // 0 == len is malformed (0 must be represented as 020100 for INTEGER, or 0A0100 for ENUMERATED tags
    if (0 == len) {
        return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 1;
    }

    //This is a cryptography library. Reject negative integers.
    if (0 != (**pp & 0x80)) {
        return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 2;
    }

    // Skip leading zeros.
    while (len > 0 && 0 == **pp) {
        ++(*pp);
        --len;
    }

    // Reject integers that don't fit in an int. This code assumes that the int type has no padding bit. */
    if (len > sizeof(int) || (len == sizeof(int) && 0 != (**pp & 0x80))) {
        return MBEDTLS_ERR_ASN1_INVALID_LENGTH - 3;
    }

    *val = 0;
    while (len > 0) {
        *val = ((*val) << 8) | (**pp);
        ++(*pp);
        --len;
    }

    return 0;
}

/**
 * @brief  Get bit string
 *
 * @param[in,out] pp     The pointer to the pointer of ASN1 buffer
 * @param[in]     end    The end of ASN1 buf
 * @param[out]    bitstr The pointer to the buffer to save the bit string
 * @return  Error code
 */
static int LTAsn1GetBitString(u8 **pp, const u8 *end, LTAsn1BitString *bitstr) {
    int ret = 0;

    /* Certificate type is a single byte bitstring */
    if (0 != (ret = LTAsn1GetTag(pp, end, &bitstr->len, MBEDTLS_ASN1_BIT_STRING))) {
        return ret;
    }

    /* Check length, subtract one for actual bit string length */
    if (bitstr->len < 1) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA;
    }
    bitstr->len -= 1;

    /* Get number of unused bits, ensure unused bits <= 7 */
    bitstr->unusedBits = **pp;
    if (bitstr->unusedBits > 7) {
        return MBEDTLS_ERR_ASN1_INVALID_LENGTH;
    }
    ++(*pp);

    /* Get actual bitstring */
    bitstr->p = *pp;
    *pp += bitstr->len;

    if (*pp != end) {
        return MBEDTLS_ERR_ASN1_LENGTH_MISMATCH;
    }

    return 0;
}

/**
 * @brief  Skip an empty bit string
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The end of ASN1 buf
 * @param[out]    len  The pointer to the length of ASN1 buffer
 * @return  Error code
 */
static int LTAsn1GetBitStringNull(u8 **pp, const u8 *end, u16 *len) {
    int ret = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, len, MBEDTLS_ASN1_BIT_STRING))) {
        return ret;
    }

    if (0 == *len) {
        return MBEDTLS_ERR_ASN1_INVALID_DATA - 1;
    }

    --(*len);

    if (0 != **pp) {
        return MBEDTLS_ERR_ASN1_INVALID_DATA - 2;
    }

    ++(*pp);

    return 0;
}

/**
 * @brief  Get boolean
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The end of ASN1 buf
 * @param[out]    val  The pointer to the buffer to save the boolean
 * @return  Error code
 */
static int LTAsn1GetBool(u8 **pp, const u8 *end, int *val) {
    int ret = 0;
    u16 len;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_BOOLEAN))) {
        return ret;
    }

    if (len != 1) {
        return MBEDTLS_ERR_ASN1_INVALID_LENGTH;
    }

    *val = (0 != **pp) ? 1 : 0;
    ++(*pp);

    return 0;
}

/**
 * @brief  Get algorithm and parameters. If no param, param will be cleared.
 *
 * @param[in,out] pp      The pointer to the pointer of ASN1 buffer
 * @param[in]     end     The end of ASN1 buf
 * @param[out]    alg     The pointer to the buffer to save the algorithm
 * @param[out]    params  The pointer to the buffer to save the algorithm parameters
 * @return  Error code
 */
static int LTAsn1GetAlg(u8 **pp, const u8 *end, LTAsn1Buf *alg, LTAsn1Buf *params) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return ret;
    }

    if ((end - *pp) < 1) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA;
    }

    alg->tag = **pp;
    end = *pp + len;

    if (0 != (ret = LTAsn1GetTag(pp, end, &alg->len, MBEDTLS_ASN1_OID))) {
        return ret;
    }

    alg->p = *pp;
    *pp += alg->len;

    if (*pp == end) {
        lt_memset(params, 0, sizeof(LTAsn1Buf));
        return 0;
    }

    params->tag = **pp;
    ++(*pp);

    if (0 != (ret = LTAsn1GetLen(pp, end, &params->len))) {
        return ret;
    }

    params->p = *pp;
    *pp += params->len;

    if (*pp != end) {
        return MBEDTLS_ERR_ASN1_LENGTH_MISMATCH;
    }

    return 0;
}

/*********************** Ebd of OID and ASN1 functions ***********************/

/*************************** X509 parsing functions **************************/

// Error ID 0x0808
/**
 * @brief  Check if date is valid
 *
 * @param[in] t  The pointer to the date
 * @return  Error code
 */
static int LTX509DateIsValid(const LTCalendarTime *t) {
    int monthLen = 0;

    if ((t->nYear > 9999) ||
        (t->nHour > 23) ||
        (t->nMinute > 59) ||
        (t->nSecond > 59)) {
        return LTTLS_ERROR(0x0808 + 1, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    switch (t->nMonth) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            monthLen = 31;
            break;

        case 4:
        case 6:
        case 9:
        case 11:
            monthLen = 30;
            break;

        case 2:
            if ((!(t->nYear % 4 ) && t->nYear % 100) || !(t->nYear % 400)) {
                monthLen = 29;
            } else {
                monthLen = 28;
            }
            break;

        default:
            return LTTLS_ERROR(0x0808 + 2, MBEDTLS_ERR_X509_INVALID_DATE);;
    }

    if (t->nDay < 1 || t->nDay > monthLen) {
        return LTTLS_ERROR(0x0808 + 3, MBEDTLS_ERR_X509_INVALID_DATE);;
    }

    return 0;
}

// Error ID 0x0810
/**
 * @brief  Get version
 *
 * @param[in,out] pp       The pointer to the pointer of ASN1 buffer
 * @param[in]     end      The pointer to the end of ASN1 buffer
 * @param[out]    version  The pointer to the buffer to save the version
 * @return  Error code
 */
static int LTX509GetVersion(u8 **pp, const u8 *end, int *version) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED))) {
        if (ret == MBEDTLS_ERR_ASN1_UNEXPECTED_TAG) {
            // version 1 is not included in cert, so not tag here
            *version = 0;
            return 0;
        }

        return LTTLS_ERROR(0x0810, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
    }

    end = *pp + len;

    if (0 != (ret = LTAsn1GetInt(pp, end, version))) {
        return LTTLS_ERROR(0x0810 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_VERSION, ret));
    }

    if (*pp != end) {
        return LTTLS_ERROR(0x0810 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_VERSION, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x0818
/**
 * @brief  Get serial number
 *
 * @param[in,out] pp      The pointer to the pointer of ASN1 buffer
 * @param[in]     end     The pointer to the end of ASN1 buffer
 * @param[out]    serial  The pointer to the buffer to save the serial number
 * @return  Error code
 */
static int LTX509GetSerial(u8 **pp, const u8 *end, LTAsn1Buf *serial) {
    int ret = 0;

    if ((end - *pp) < 1) {
        return LTTLS_ERROR(0x0818 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_SERIAL, MBEDTLS_ERR_ASN1_OUT_OF_DATA));
    }

    if (**pp != (MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_INTEGER) && **pp != MBEDTLS_ASN1_INTEGER) {
        return LTTLS_ERROR(0x0818 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_SERIAL, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG));
    }

    serial->tag = **pp;

    ++(*pp);
    if (0 != (ret = LTAsn1GetLen(pp, end, &serial->len))) {
        return LTTLS_ERROR(0x0818 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_SERIAL, ret));
    }

    serial->p = *pp;
    *pp += serial->len;

    return 0;
}

// Error ID 0x0820
/**
 * @brief  Get algorithm
 *
 * @param[in,out] pp      The pointer to the pointer of ASN1 buffer
 * @param[in]     end     The pointer to the end of ASN1 buffer
 * @param[out]    alg     The pointer to the buffer to save the algorithm
 * @param[out]    params  The pointer to the buffer to save the algorithm parameters
 * @return  Error code
 */
static int LTX509GetAlg(u8 **pp, const u8 *end, LTAsn1Buf *alg, LTAsn1Buf *params) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0820 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_ALG, ret));
    }

    if ((end - *pp) < 1) {
        return LTTLS_ERROR(0x0820 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_ALG, MBEDTLS_ERR_ASN1_OUT_OF_DATA));
    }

    // algorithm
    alg->tag = **pp;
    end = (*pp) + len;

    if (0 != (ret = LTAsn1GetTag(pp, end, &alg->len, MBEDTLS_ASN1_OID))) {
        return LTTLS_ERROR(0x0820 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_ALG, ret));
    }

    // algorithm OID
    alg->p = *pp;
    *pp += alg->len;

    if (*pp == end) {
        // no params
        lt_memset(params, 0, sizeof(LTAsn1Buf));
        return 0;
    }

    // params
    params->tag = **pp;
    ++(*pp);

    if (0 != (ret = LTAsn1GetLen(pp, end, &params->len))) {
        return LTTLS_ERROR(0x0820 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_ALG, ret));
    }

    params->p = *pp;
    *pp += params->len;

    if (*pp != end) {
        return LTTLS_ERROR(0x0820 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_ALG, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x0828
/**
 * @brief  Get signature algorithm
 *
 * @param[in]  sigOid     The pointer to the signature OID
 * @param[in]  sigParams  The pointer to the signature parameters
 * @param[out] mdAlg      The pointer to the buffer to save the hash algorithm
 * @param[out] pkAlg      The pointer to the buffer to save the public key algorithm
 * @param[out] sigOpts    The pointer to the buffer to save the optional signature parameters
 * @return  Error code
 */
static int LTX509GetSigAlg(const LTAsn1Buf *sigOid, const LTAsn1Buf *sigParams, u8 *mdAlg, u16 *pkAlg, void **sigOpts) {
    int ret = 0;

    if (*sigOpts != NULL) {
        return LTTLS_ERROR(0x0828, MBEDTLS_ERR_X509_BAD_INPUT_DATA);
    }

    if (0 != (ret = LTOidGetSigAlg(sigOid, mdAlg, pkAlg))) {
        return LTTLS_ERROR(0x0828, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG, ret));
    }

    if (*pkAlg == MBEDTLS_PK_ECDSA || *pkAlg == LTTLS_PK_ED25519) {
        // ECDSA or ED25519
        LT_UNUSED(sigParams);
        ret = 0;
    } else {
        // TODO if needed to support other public key algorithms
        ret = LTTLS_ERROR(0x0828, MBEDTLS_ERR_X509_INVALID_ALG);
    }

    return ret;
}

// Error ID 0x0830
/**
 * @brief  Get attribute type
 *
 * @param[in,out] pp    The pointer to the pointer of ASN1 buffer
 * @param[in]     end   The pointer to the end of ASN1 buffer
 * @param[out]    curr  The pointer to the buffer to save the attribute type
 * @return  Error code
 */
static int LTX509GetAttrTypeValue(u8 **pp, const u8 *end, LTAsn1NamedData *curr) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0830 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, ret));
    }

    end = (*pp) + len;

    if ((end - (*pp)) < 1) {
        return LTTLS_ERROR(0x0830 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, MBEDTLS_ERR_ASN1_OUT_OF_DATA));
    }

    LTAsn1Buf *oid = &curr->oid;
    oid->tag = **pp;

    if (0 != (ret = LTAsn1GetTag(pp, end, &oid->len, MBEDTLS_ASN1_OID))) {
        return LTTLS_ERROR(0x0830 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, ret));
    }

    oid->p = *pp;
    *pp += oid->len;

    if ((end - (*pp)) < 1) {
        return LTTLS_ERROR(0x0830 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, MBEDTLS_ERR_ASN1_OUT_OF_DATA));
    }

    if (**pp != MBEDTLS_ASN1_BMP_STRING && **pp != MBEDTLS_ASN1_UTF8_STRING      &&
        **pp != MBEDTLS_ASN1_T61_STRING && **pp != MBEDTLS_ASN1_PRINTABLE_STRING &&
        **pp != MBEDTLS_ASN1_IA5_STRING && **pp != MBEDTLS_ASN1_UNIVERSAL_STRING &&
        **pp != MBEDTLS_ASN1_BIT_STRING) {
        return LTTLS_ERROR(0x0830 + 5, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG));
    }

    LTAsn1Buf *val = &curr->val;
    val->tag = **pp;
    ++(*pp);

    if (0 != (ret = LTAsn1GetLen(pp, end, &val->len))) {
        return LTTLS_ERROR(0x0830 + 6, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, ret));
    }

    val->p = *pp;
    *pp += val->len;

    if (*pp != end) {
        return LTTLS_ERROR(0x0830 + 7, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    curr->next = NULL;

    return 0;
}

// Error ID 0x0838
/**
 * @brief  Get name
 *
 * @param[in,out] pp    The pointer to the pointer of ASN1 buffer
 * @param[in]     end   The pointer to the end of ASN1 buffer
 * @param[out]    curr  The pointer to the buffer to save the attribute type
 * @return  Error code
 */
static int LTX509GetName(u8 **pp, const u8 *end, LTAsn1NamedData *curr) {
    int ret = 0;
    u16 setLen = 0;
    const u8 *endSet = NULL;

    while (true) {
        // parse SET
        if (0 != (ret = LTAsn1GetTag(pp, end, &setLen, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SET))) {
            return LTTLS_ERROR(0x0838, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME, ret));
        }

        endSet  = (*pp) + setLen;

        while (true) {

            if (0 != (ret = LTX509GetAttrTypeValue(pp, endSet, curr))) {
                return ret;
            }

            if ((*pp) == endSet) {
                break;
            }

            curr->next = lt_malloc(sizeof(LTAsn1NamedData));
            if (!curr->next) {
                return LTTLS_ERROR(0x0838 + 1, MBEDTLS_ERR_X509_ALLOC_FAILED);
            }

            lt_memset(curr->next, 0, sizeof(LTAsn1NamedData));
            curr = curr->next;
        }

        /*
         * continue until end of SEQUENCE is reached
         */
        if ((*pp) == end) {
            return 0;
        }

        curr->next = lt_malloc(sizeof(LTAsn1NamedData));
        if (!curr->next) {
            return LTTLS_ERROR(0x0838 + 2, MBEDTLS_ERR_X509_ALLOC_FAILED);
        }

        lt_memset(curr->next, 0, sizeof(LTAsn1NamedData));
        curr = curr->next;
    }
}

/**
 * @brief  Parse U16 value only
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     n    The length of the buffer
 * @param[out]    res  The pointer to the buffer to save the integer
 * @return  true on success; false otherwise.
 */
static bool LTX509ParseU16(u8 **pp, LT_SIZE n, u16 *res) {
    u32 v = 0;
    *res = 0;

    for (; n > 0; --n) {
        if ((**pp < '0') || (**pp > '9')) {
            return false;
        }
        v *= 10;
        v += (**pp) - '0';
	    // must be a reasonable u16
	    if (v >= 10000) return false;
        ++(*pp);
    }

    *res = v;
    return true;
}

// Error ID 0x0840
/**
 * @brief  Parse time
 *
 * @param[in,out] pp       The pointer to the pointer of ASN1 buffer
 * @param[in]     len      The length of buffer
 * @param[in]     yearLen  The length of year
 * @param[out]    timeVal  The pointer to the time
 * @return  Error code
 */
static int LTX509ParseTime(u8 **pp, LT_SIZE len, LT_SIZE yearLen, LTCalendarTime *timeVal) {
    // Minimum length is 10 or 12 depending on yearlen
    if (len < yearLen + 8) {
        return LTTLS_ERROR(0x0840 + 1, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    len -= yearLen + 8;

    // Parse year, month, day, hour, minute
    if (!LTX509ParseU16(pp, yearLen, &timeVal->nYear)) {
        return LTTLS_ERROR(0x0840 + 2, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    if (2 == yearLen) {
        if (timeVal->nYear < 50) {
            timeVal->nYear += 100;
        }
        timeVal->nYear += 1900;
    }

    if (!LTX509ParseU16(pp, 2, &timeVal->nMonth)) {
        return LTTLS_ERROR(0x0840 + 3, MBEDTLS_ERR_X509_INVALID_DATE);
    }
    if (!LTX509ParseU16(pp, 2, &timeVal->nDay)) {
        return LTTLS_ERROR(0x0840 + 4, MBEDTLS_ERR_X509_INVALID_DATE);
    }
    if (!LTX509ParseU16(pp, 2, &timeVal->nHour)) {
        return LTTLS_ERROR(0x0840 + 5, MBEDTLS_ERR_X509_INVALID_DATE);
    }
    if (!LTX509ParseU16(pp, 2, &timeVal->nMinute)) {
        return LTTLS_ERROR(0x0840 + 6, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    // Parse seconds if present
    if (len >= 2) {
        if (!LTX509ParseU16(pp, 2, &timeVal->nSecond)) {
            return LTTLS_ERROR(0x0840 + 7, MBEDTLS_ERR_X509_INVALID_DATE);
        }
        len -= 2;
    } else {
        return LTTLS_ERROR(0x0840 + 8, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    // Parse trailing 'Z' if present
    if (1 == len && 'Z' == **pp) {
        ++(*pp);
        --len;
    }

    // set the rest to 0
    timeVal->nMillisecond = 0; // ok here.
    timeVal->nWeekday = 0;     // not correct, but ok here.

    // We should have parsed all characters at this point
    if (0 != len) {
        return LTTLS_ERROR(0x0840 + 9, MBEDTLS_ERR_X509_INVALID_DATE);
    }

    return LTX509DateIsValid(timeVal);
}

// Error ID 0x0850
/**
 * @brief  Get time
 *
 * @param[in,out] pp      The pointer to the pointer of ASN1 buffer
 * @param[in]     end     The pointer to the end of ASN1 buffer
 * @param[out]    timeVal The pointer to the buffer to save the time
 * @return  Error code
 */
static int LTX509GetTime(u8 **pp, const u8 *end, LTCalendarTime *timeVal) {
    int ret = 0;
    u16 len = 0, yearLen = 0;
    u8 tag = 0;

    if ((end - (*pp)) < 1) {
        return LTTLS_ERROR(0x0850 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_DATE, MBEDTLS_ERR_ASN1_OUT_OF_DATA));
    }

    tag = **pp;

    if (tag == MBEDTLS_ASN1_UTC_TIME) {
        yearLen = 2;
    } else if (tag == MBEDTLS_ASN1_GENERALIZED_TIME) {
        yearLen = 4;
    } else {
        return LTTLS_ERROR(0x0850 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_DATE, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG));
    }

    ++(*pp);

    if (0 != (ret = LTAsn1GetLen(pp, end, &len))) {
        return LTTLS_ERROR(0x0850 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_DATE, ret));
    }

    return LTX509ParseTime(pp, len, yearLen, timeVal);
}

// Error ID 0x0858
/**
 * @brief  Get dates
 *
 * @param[in,out] pp    The pointer to the pointer of ASN1 buffer
 * @param[in]     end   The pointer to the end of ASN1 buffer
 * @param[out]    from  The pointer to the buffer to save the start date
 * @param[out]    to    The pointer to the buffer to save the end date
 * @return  Error code
 */
static int LTX509GetDates(u8 **pp, const u8 *end, LTCalendarTime *from, LTCalendarTime *to) {
    int ret = 0;
    u16 len = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0858 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_DATE, ret));
    }

    end = (*pp) + len;

    if (0 != (ret = LTX509GetTime(pp, end, from))) {
        return ret;
    }
    if (0 != (ret = LTX509GetTime(pp, end, to))) {
        return ret;
    }

    if (*pp != end) {
        return LTTLS_ERROR(0x0858 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_DATE, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x0860
/**
 * @brief  Get Uid, TODO X.509 v2/v3 unique identifier (not parsed)
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The pointer to the end of ASN1 buffer
 * @param[out]    uid  The pointer to the buffer to save the Uid
 * @param[in]     n    The X.509 version number
 * @return  Error code
 */
static int LTX509GetUid(u8 **pp, const u8 *end, LTAsn1Buf *uid, int n) {
    int ret = 0;

    if (*pp == end) {
        return 0;
    }

    uid->tag = **pp;

    if (0 != (ret = LTAsn1GetTag(pp, end, &uid->len, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | n))) {
        if (ret == MBEDTLS_ERR_ASN1_UNEXPECTED_TAG) {
            return 0;
        }
        return LTTLS_ERROR(0x0860, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
    }

    uid->p = *pp;
    *pp += uid->len;

    return 0;
}

// Error ID 0x0868
/**
 * @brief  Get signature
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The pointer to the end of ASN1 buffer
 * @param[out]    sig  The pointer to the buffer to save the signature
 * @return  Error code
 */
static int LTX509GetSig(u8 **pp, const u8 *end, LTAsn1Buf *sig) {
    int ret = 0;

    sig->tag = **pp;
    if (0 != (ret = LTAsn1GetBitStringNull(pp, end, &sig->len))) {
        return LTTLS_ERROR(0x0868 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_SIGNATURE, ret));
    }
    sig->p = *pp;

    *pp += sig->len;

    if (*pp != end) {
        return LTTLS_ERROR(0x0868 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_SIGNATURE, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x0870
/**
 * @brief  Get extension
 *
 * @param[in,out] pp    The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The pointer to the end of ASN1 buffer
 * @param[out]    ext  The pointer to the buffer to save the extension
 * @param[in]     tag  The extension tag
 * @return  Error code
 */
static int LTX509GetExt(u8 **pp, const u8 *end, LTAsn1Buf *ext, int tag) {
    int ret = 0;
    u16 len = 0;

    /* Extension structure use EXPLICIT tagging. That is, the actual
     * `Extensions` structure is wrapped by a tag-length pair using
     * the respective context-specific tag. */
    if (0 != (ret = LTAsn1GetTag(pp, end, &ext->len, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | tag))) {
        return LTTLS_ERROR(0x0870 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    ext->tag = MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | tag;
    ext->p    = *pp;
    end       = *pp + ext->len;

    /*
     * Extensions  ::=  SEQUENCE SIZE (1..MAX) OF Extension
     */
    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0870 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (end != (*pp) + len) {
        return LTTLS_ERROR(0x0870 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x0878
/**
 * @brief  Get subject key ID
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The pointer to the end of ASN1 buffer
 * @param[out]    ext  The pointer to the buffer to save the extension
 * @return  Error code
 */
static int LTX509GetSubjectKeyId(u8 **pp, const u8 *end, LTAsn1Buf *ext) {
    int ret = 0;
    if (0 != (ret = LTAsn1GetTag(pp, end, &ext->len, MBEDTLS_ASN1_OCTET_STRING))) {
        return LTTLS_ERROR(0x0878 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }
    ext->tag = MBEDTLS_ASN1_OCTET_STRING;
    ext->p   = *pp;
    *pp += ext->len;
    if (end != (*pp)) {
        return LTTLS_ERROR(0x0878 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }
    return 0;
}

// Error ID 0x0880
/**
 * @brief  Get authority key ID
 *
 * @param[in,out] pp   The pointer to the pointer of ASN1 buffer
 * @param[in]     end  The pointer to the end of ASN1 buffer
 * @param[out]    ext  The pointer to the buffer to save the extension
 * @return  Error code
 */
static int LTX509GetAuthorityKeyId(u8 **pp, const u8 *end, LTAsn1Buf *ext) {
    int ret = 0;
    if (0 != (ret = LTAsn1GetTag(pp, end, &ext->len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0880 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }
    if (0 != (ret = LTAsn1GetTag(pp, end, &ext->len, MBEDTLS_ASN1_CONTEXT_SPECIFIC))) {
        return LTTLS_ERROR(0x0880 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }
    ext->tag = MBEDTLS_ASN1_CONTEXT_SPECIFIC;
    ext->p   = *pp;
    *pp += ext->len;
    if (end < (*pp)) {
        return LTTLS_ERROR(0x0880 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }
    // ignore the rest data in this extension
    *pp = (u8 *)end;
    return 0;
}

// Error ID 0x0888
/**
 * @brief  Get CA constraints
 *
 * @param[in,out] pp          The pointer to the pointer of ASN1 buffer
 * @param[in]     end         The pointer to the end of ASN1 buffer
 * @param[out]    caIsTrue    The pointer to the buffer to save the indicator if it is a true CA
 * @param[in]     maxPathLen  The pointer to the buffer to save the max path length
 * @return  Error code
 */
static int LTX509GetBasicConstraints(u8 **pp, const u8 *end, int *caIsTrue, int *maxPathLen) {
    int ret = 0;
    u16 len = 0;

    /*
     * BasicConstraints ::= SEQUENCE {
     *      cA                      BOOLEAN DEFAULT FALSE,
     *      pathLenConstraint       INTEGER (0..MAX) OPTIONAL }
     */
    *caIsTrue = 0; /* DEFAULT FALSE */
    *maxPathLen = 0; /* endless */

    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0888 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (*pp == end) {
        return 0;
    }

    if (0 != (ret = LTAsn1GetBool(pp, end, caIsTrue))) {
        if (ret == MBEDTLS_ERR_ASN1_UNEXPECTED_TAG) {
            ret = LTAsn1GetInt(pp, end, caIsTrue);
        }

        if (0 != ret) {
            return LTTLS_ERROR(0x0888 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        if (0 != *caIsTrue) {
            *caIsTrue = 1;
        }
    }

    if (*pp == end) {
        return 0;
    }

    if (0 != (ret = LTAsn1GetInt(pp, end, maxPathLen))) {
        return LTTLS_ERROR(0x0888 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (*pp != end) {
        return LTTLS_ERROR(0x0888 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    /* Do not accept max_pathlen equal to INT_MAX to avoid a signed integer
     * overflow, which is an undefined behavior. */
    if (*maxPathLen == LT_S32_MAX) {
        return LTTLS_ERROR(0x0888 + 5, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_INVALID_LENGTH));
    }

    ++(*maxPathLen);

    return 0;
}

// Error ID 0x0890
/**
 * @brief  Get key usage
 *
 * @param[in,out] pp        The pointer to the pointer of ASN1 buffer
 * @param[in]     end       The pointer to the end of ASN1 buffer
 * @param[out]    keyUsage  The pointer to the buffer to save the key usage
 * @return  Error code
 */
static int LTX509GetKeyUsage(u8 **pp, const u8 *end, u32 *keyUsage) {
    int ret = 0;
    LT_SIZE i = 0;
    LTAsn1BitString bs = {0, 0, NULL};

    if (0 != (ret = LTAsn1GetBitString(pp, end, &bs))) {
        return LTTLS_ERROR(0x0890 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (bs.len < 1) {
        return LTTLS_ERROR(0x0890 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_INVALID_LENGTH));
    }

    /* Get actual bitstring */
    *keyUsage = 0;
    for (i = 0; i < bs.len && i < sizeof(u32); ++i) {
        *keyUsage |= (u32)bs.p[i] << (8 * i);
    }

    return 0;
}

// Error ID 0x0898
/**
 * @brief  Get extended key usage
 *
 * @param[in,out] pp           The pointer to the pointer of ASN1 buffer
 * @param[in]     end          The pointer to the end of ASN1 buffer
 * @param[out]    extKeyUsage  The pointer to the buffer to save the extended key usage
 * @return  Error code
 */
static int LTX509GetExtKeyUsage(u8 **pp, const u8 *end, LTAsn1Sequence *extKeyUsage) {
    int ret = 0;
    u16 len = 0;
    LTAsn1Sequence *curr = extKeyUsage;

    /* Get main sequence tag */
    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x0898 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (*pp + len != end) {
        return LTTLS_ERROR(0x0898 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    u8 tag = 0;
    while (*pp < end) {

        if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_OID))) {
            return LTTLS_ERROR(0x0898 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        if (curr->buf.p != NULL) {
            curr->next = (LTAsn1Sequence *)lt_malloc(sizeof(LTAsn1Sequence));
            if (!curr->next) {
                return LTTLS_ERROR(0x0898, MBEDTLS_ERR_ASN1_ALLOC_FAILED);
            }

            lt_memset(curr->next, 0, sizeof(LTAsn1Sequence));
            curr = curr->next;
        }

        curr->buf.p = *pp;
        curr->buf.len = len;
        curr->buf.tag = tag;
        *pp += len;
    }

    if (0 != ret) {
        return LTTLS_ERROR(0x0898 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    /* Sequence length must be >= 1 */
    if (extKeyUsage->buf.p == NULL) {
        return LTTLS_ERROR(0x0898 + 5, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_INVALID_LENGTH));
    }

    return 0;
}

// Error ID 0x08A0
/**
 * @brief  Get subject alternative name
 *
 * @param[in,out] pp              The pointer to the pointer of ASN1 buffer
 * @param[in]     end             The pointer to the end of ASN1 buffer
 * @param[out]    subjectAltName  The pointer to the buffer to save the subject alternative name
 * @return  Error code
 */
static int LTX509GetSubjectAltName(u8 **pp, const u8 *end, LTAsn1Sequence *subjectAltName) {
    int ret = 0;
    u16 len = 0, tagLen = 0;
    u8 tag = 0;
    LTAsn1Sequence *curr = subjectAltName;

    /* Get main sequence tag */
    if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x08A0 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (*pp + len != end) {
        return LTTLS_ERROR(0x08A0 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    LTX509SubjectAlternativeName dummySanBuf;
    while (*pp < end) {
        lt_memset(&dummySanBuf, 0, sizeof(dummySanBuf));

        tag = **pp;
        ++(*pp);

        if (0 != (ret = LTAsn1GetLen(pp, end, &tagLen))) {
            return LTTLS_ERROR(0x08A0 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        if ((tag & MBEDTLS_ASN1_TAG_CLASS_MASK) != MBEDTLS_ASN1_CONTEXT_SPECIFIC) {
            return LTTLS_ERROR(0x08A0 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG));
        }

        /* Allocate and assign next pointer */
        if (curr->buf.p != NULL) {
            if (curr->next != NULL) {
                return LTTLS_ERROR(0x08A0 + 5, MBEDTLS_ERR_X509_INVALID_EXTENSIONS);
            }

            curr->next = (LTAsn1Sequence *)lt_malloc(sizeof(LTAsn1Sequence));
            if (!curr->next) {
                return LTTLS_ERROR(0x08A0 + 6, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_ALLOC_FAILED));
            }

            lt_memset(curr->next, 0, sizeof(LTAsn1Sequence));
            curr = curr->next;
        }

        curr->buf.tag = tag;
        curr->buf.len = tagLen;
        curr->buf.p = *pp;
        *pp += tagLen;
    }

    /* Set final sequence entry's next pointer to NULL */
    curr->next = NULL;

    if (*pp != end) {
        return LTTLS_ERROR(0x08A0 + 7, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

// Error ID 0x08A8
/**
 * @brief  Get netscape certificate type
 *
 * @param[in,out] pp          The pointer to the pointer of ASN1 buffer
 * @param[in]     end         The pointer to the end of ASN1 buffer
 * @param[out]    nsCertType  The pointer to the buffer to save the netscape certificate type
 * @return  Error code
 */
static int LTX509GetNsCertType(u8 **pp, const u8 *end, u8 *nsCertType) {
    int ret = 0;
    LTAsn1BitString bs = {0, 0, NULL};

    if (0 != (ret = LTAsn1GetBitString(pp, end, &bs))) {
        return LTTLS_ERROR(0x08A8 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
    }

    if (bs.len != 1) {
        return LTTLS_ERROR(0x08A8 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_INVALID_LENGTH));
    }

    /* Get actual bitstring */
    *nsCertType = *bs.p;

    return 0;
}

// Error ID 0x08B0
/** TODO need to validate
 * @brief  Get certificate extension
 *
 * @param[in,out] pp          The pointer to the pointer of ASN1 buffer
 * @param[in]     end         The pointer to the end of ASN1 buffer
 * @param[out]    certificate The pointer to the buffer to save the certificate
 * @return  Error code
 */
static int LTX509GetCertExt(u8 **pp, const u8 *end, LTX509Cert *certificate) {
    // X.509 v3 extensions
    int ret = 0;
    u16 len = 0;
    u8 *endExtData = NULL;
    // u8 *pStartExtOctet = NULL;
    u8 *endExtOctet = NULL;

    if ((*pp) == end) {
        return 0;
    }

    if (0 != (ret = LTX509GetExt(pp, end, &certificate->v3Ext, MBEDTLS_ASN1_BIT_STRING))) {
        return ret;
    }

    LTAsn1Buf extnOid = {0, 0, NULL};
    int isCritical = 0; /* DEFAULT FALSE */
    int extType = 0;
    end = certificate->v3Ext.p + certificate->v3Ext.len;
    while (*pp < end) {
        /*
         * Extension  ::=  SEQUENCE  {
         *      extnID      OBJECT IDENTIFIER,
         *      critical    BOOLEAN DEFAULT FALSE,
         *      extnValue   OCTET STRING  }
         */
        if (0 != (ret = LTAsn1GetTag(pp, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
            return LTTLS_ERROR(0x08B0 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        endExtData = *pp + len;

        /* Get extension ID */
        if (0 != (ret = LTAsn1GetTag(pp, endExtData, &extnOid.len, MBEDTLS_ASN1_OID))) {
            return LTTLS_ERROR(0x08B0 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        extnOid.tag = MBEDTLS_ASN1_OID;
        extnOid.p = *pp;
        *pp += extnOid.len;

        /* Get optional critical */
        if (0 != (ret = LTAsn1GetBool(pp, endExtData, &isCritical)) && (ret != MBEDTLS_ERR_ASN1_UNEXPECTED_TAG)) {
            return LTTLS_ERROR(0x08B0 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        /* Data should be octet string type */
        if (0 != (ret = LTAsn1GetTag(pp, endExtData, &len, MBEDTLS_ASN1_OCTET_STRING))) {
            return LTTLS_ERROR(0x08B0 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, ret));
        }

        // pStartExtOctet = *pp;
        endExtOctet = *pp + len;

        if (endExtOctet != endExtData) {
            return LTTLS_ERROR(0x08B0 + 5, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH - 1));
        }

        /* Detect supported extensions */
        ret = LTOidGetX509ExtType(&extnOid, &extType);

        if (0 != ret) {
            /* No parser found, skip extension */
            *pp = endExtOctet;

            /* Data is marked as critical: fail */
            if (isCritical) {
                return LTTLS_ERROR(0x08B0 + 6, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG));
            }

            continue;
        }

        /* Forbid repeated extensions */
        if (0 != (certificate->extTypes & extType)) {
            return LTTLS_ERROR(0x08B0 + 7, MBEDTLS_ERR_X509_INVALID_EXTENSIONS);
        }

        certificate->extTypes |= extType;

        switch (extType) {
            case MBEDTLS_OID_X509_EXT_SUBJECT_KEY_IDENTIFIER:
                /* Parse subject key id */
                if (0 != (ret = LTX509GetSubjectKeyId(pp, endExtOctet, &certificate->subjectKeyId))) {
                    return ret;
                }

                break;

            case MBEDTLS_OID_X509_EXT_AUTHORITY_KEY_IDENTIFIER:
                /* Parse authority key id */
                if (0 != (ret = LTX509GetAuthorityKeyId(pp, endExtOctet, &certificate->authorityKeyId))) {
                    return ret;
                }

                break;

            case MBEDTLS_X509_EXT_BASIC_CONSTRAINTS:
                /* Parse basic constraints */
                if (0 != (ret = LTX509GetBasicConstraints(pp, endExtOctet, &certificate->caIsTrue, &certificate->maxPathLen))) {
                    return ret;
                }

                break;

            case MBEDTLS_X509_EXT_KEY_USAGE:
                /* Parse key usage */
                if (0 != (ret = LTX509GetKeyUsage(pp, endExtOctet, &certificate->keyUsage))) {
                    return ret;
                }

                break;

            case MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE:
                /* Parse extended key usage */
                if (0 != (ret = LTX509GetExtKeyUsage(pp, endExtOctet, &certificate->extKeyUsage))) {
                    return ret;
                }

                break;

            case MBEDTLS_X509_EXT_SUBJECT_ALT_NAME:
                /* Parse subject alt name */
                if (0 != (ret = LTX509GetSubjectAltName(pp, endExtOctet, &certificate->subjectAltNames))) {
                    return ret;
                }

                break;

            case MBEDTLS_X509_EXT_NS_CERT_TYPE:
                /* Parse netscape certificate type */
                if (0 != (ret = LTX509GetNsCertType(pp, endExtOctet, &certificate->nsCertType))) {
                    return ret;
                }

                break;

            default:
                /*
                * If this is a non-critical extension, which the oid layer
                * supports, but there isn't an x509 parser for it,
                * skip the extension.
                */
                if (isCritical) {
                    return LTTLS_ERROR(0x08B0, MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE);
                } else {
                    *pp = endExtOctet;
                }
        }
    }

    if (*pp != end) {
        return LTTLS_ERROR(0x08B0 + 8, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH - 2));
    }

    return 0;
}

// Error ID 0x08C0
/**
 * @brief  Get public key algorithm, only used in parsing subject public key
 *
 * @param[in,out] pp      The pointer to the pointer of ASN1 buffer
 * @param[in]     end     The pointer to the end of ASN1 buffer
 * @param[out]    pkAlg   The pointer to the buffer to save the public key algorithm
 * @param[out]    params  The pointer to the buffer to save the algorithm parameters
 * @return  Error code
 */
static int LTX509GetPkAlg(u8 **pp, const u8 *end, u16 *pkAlg, LTAsn1Buf *params) {
    int ret = 0;
    LTAsn1Buf algOid;

    lt_memset(params, 0, sizeof(LTAsn1Buf));

    if (0 != (ret = LTAsn1GetAlg(pp, end, &algOid, params))) {
        return LTTLS_ERROR(0x08C0 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_INVALID_ALG, ret));
    }

    if (0 != (ret = LTOidGetPkAlg(&algOid, pkAlg))) {
        return LTTLS_ERROR(0x08C0, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_UNKNOWN_PK_ALG, ret));
    }

    // No parameters with RSA or EDDSA (only for EC), return.
    if ((*pkAlg == LTTLS_PK_ED25519 || *pkAlg == MBEDTLS_PK_RSA)) {
        if ((params->tag != MBEDTLS_ASN1_NULL && 0 != params->tag) || 0 != params->len) {
            return LTTLS_ERROR(0x08C0 + 2, MBEDTLS_ERR_PK_INVALID_ALG);
        }
    }

    // Map public key algorithm to TLS 1.3 signature algorithm
    // Ed25519
    if (*pkAlg == LTTLS_PK_ED25519) *pkAlg = SIGNATURE_ED25519;
    // P256/secp256r1, 1.2.840.10045.3.1.7
    if (*pkAlg == MBEDTLS_PK_ECDSA) {
        if (params->tag != MBEDTLS_ASN1_OID || params->len != (sizeof(MBEDTLS_OID_EC_GRP_SECP256R1) - 1) ||
            lt_memcmp(params->p, MBEDTLS_OID_EC_GRP_SECP256R1, params->len) != 0) {
            return LTTLS_ERROR(0x08C0 + 3, MBEDTLS_ERR_PK_INVALID_ALG);
        }
        // Map ECDSA public key algorithm to TLS 1.3 signature algorithm
        *pkAlg = SIGNATURE_ECDSA_SECP256R1_SHA256;
    }

    return 0;
}

// Error ID 0x08C8
/**
 * @brief Parse subject public key
 *
 * @param[in,out] pp        The pointer to the pointer of ASN1 buffer
 * @param[in]     end       The pointer to the end of ASN1 buffer
 * @param[out]    pkAlg     The pointer to the buffer to save the public key algorithm
 * @param[out]    pkParams  The pointer to the buffer to save public key parameters
 * @param[out]    pubKey    The pointer to the buffer to save public key
 * @return  Error code
 */
static int LTX509ParseSubPubKey(u8 **pp, const u8 *end, u16 *pkAlg, LTAsn1Buf *pkParams, LTAsn1Buf *pubKey) {
    int ret = 0;

    if (0 != (ret = LTAsn1GetTag(pp, end, &pubKey->len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
        return LTTLS_ERROR(0x08C8, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_KEY_INVALID_FORMAT, ret));
    }

    end = (*pp) + pubKey->len;

    if (0 != (ret = LTX509GetPkAlg(pp, end, pkAlg, pkParams))) {
        return ret;
    }

    if (*pkAlg != SIGNATURE_ECDSA_SECP256R1_SHA256 && *pkAlg != SIGNATURE_ED25519) {
        ret = LTTLS_ERROR(0x08C8, MBEDTLS_ERR_PK_UNKNOWN_PK_ALG);
    }

    pubKey->tag = **pp;
    if (0 != (ret = LTAsn1GetBitStringNull(pp, end, &pubKey->len))) {
        return LTTLS_ERROR(0x08C8 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_INVALID_PUBKEY, ret));
    }

    if (*pkAlg == SIGNATURE_ECDSA_SECP256R1_SHA256) {
        // P256
        --pubKey->len; // the first byte is an uncompressed flag
        // only support uncompressed point
        if (pubKey->len != ECDSA_P256_PUBLICKEY_LENGTH || **pp != LT_EC_POINT_UNCOMPRESSED) {
            return LTTLS_ERROR(0x08C8 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_INVALID_PUBKEY, ret));
        }
        ++(*pp);

    } else if (*pkAlg == SIGNATURE_ED25519) {
        // ED25519
        if (pubKey->len != EdDSA_KEY_LENGTH) {
            return LTTLS_ERROR(0x08C8 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_INVALID_PUBKEY, ret));
        }
    }
    pubKey->p = *pp;

    *pp += pubKey->len;

    if (*pp != end) {
        ret = LTTLS_ERROR(0x08C8 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_PK_INVALID_PUBKEY, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
    }

    return 0;
}

/**
 * @brief  Free and clear the content of a certificate
 *
 * @param[in,out] certificate  The certificate
 */
static void LTX509FreeCert(LTX509Cert *certificate) {
    if (!certificate) return;

    LTAsn1NamedData *nameCurr = NULL;
    LTAsn1NamedData *namePrev = NULL;
    LTAsn1Sequence  *seqCurr = NULL;
    LTAsn1Sequence  *seqPrev = NULL;

    lt_free(certificate->sigOpts);

    nameCurr = certificate->issuer.next;
    while (nameCurr != NULL) {
        namePrev = nameCurr;
        nameCurr = nameCurr->next;
        lt_memset(namePrev, 0, sizeof(LTAsn1NamedData));
        lt_free(namePrev);
    }

    nameCurr = certificate->subject.next;
    while (nameCurr != NULL) {
        namePrev = nameCurr;
        nameCurr = nameCurr->next;
        lt_memset(namePrev, 0, sizeof(LTAsn1NamedData));
        lt_free(namePrev);
    }

    seqCurr = certificate->extKeyUsage.next;
    while (seqCurr != NULL) {
        seqPrev = seqCurr;
        seqCurr = seqCurr->next;
        lt_memset(seqPrev, 0, sizeof(LTAsn1Sequence));
        lt_free(seqPrev);
    }

    seqCurr = certificate->subjectAltNames.next;
    while (seqCurr != NULL) {
        seqPrev = seqCurr;
        seqCurr = seqCurr->next;
        lt_memset(seqPrev, 0, sizeof(LTAsn1Sequence));
        lt_free( seqPrev );
    }

    lt_memset(certificate, 0, sizeof(LTX509Cert));
}

// Error ID 0x0920
/**
 * @brief  Parse one DER certificate.
 *         The function assumes the cert is initially cleared.
 *         On passing error, it clears the cert, frees any heap, and returns an error.
 *
 * @param[in] certificate The pointer to the result certificate
 * @param[in] buf         The pointer to the buffer of certificate DER data
 * @param[in] bufLen      The length of buffer, in Bytes
 * @return  Error code
 */
static int LTX509ParseCertDer(LTX509Cert *certificate, const u8 *buf, u16 bufLen) {
    // Check for valid input
    if (certificate == NULL || buf == NULL) {
        return LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_BAD_INPUT_DATA);
    }

    int ret= 0;
    u16 len;
    u8 *p, *end, *certEnd;
    LTAsn1Buf sigParams1, sigParams2, sigOid2;

    lt_memset(&sigParams1, 0, sizeof(LTAsn1Buf));
    lt_memset(&sigParams2, 0, sizeof(LTAsn1Buf));
    lt_memset(&sigOid2,    0, sizeof(LTAsn1Buf));

    // Use the original buffer until we figure out actual length. */
    p = (u8 *)buf;
    len = bufLen;
    end = p + len;

    do {
        /*
         * Certificate  ::=  SEQUENCE  {
         *      tbsCertificate       TBSCertificate,
         *      signatureAlgorithm   AlgorithmIdentifier,
         *      signatureValue       BIT STRING  }
         */
        if (0 != (ret= LTAsn1GetTag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
            ret = LTTLS_ERROR(0x0920 + 1, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
            break;
        }

        // now the actual cert length is figured out, so update end.
        end = certEnd = p + len;
        certificate->raw.len = certEnd - buf;
        certificate->raw.p = (u8 *)buf;

        /***** Begin of signedCertificate (tbs) ******
        TBSCertificate  ::=  SEQUENCE  {
            version         [0]  EXPLICIT Version DEFAULT v1,
            serialNumber         CertificateSerialNumber,
            signature            AlgorithmIdentifier,
            issuer               Name,
            validity             Validity,
            subject              Name,
            subjectPublicKeyInfo SubjectPublicKeyInfo,
            issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL, If present, version MUST be v2 or v3
            subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL, If present, version MUST be v2 or v3
            extensions      [3]  EXPLICIT Extensions OPTIONAL If present, version MUST be v3
            }
        */
        certificate->tbs.p = p;

        if(0 != (ret= LTAsn1GetTag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
            ret = LTTLS_ERROR(0x0920 + 2, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
            break;
        }

        // update end again to exclude padding
        end = p + len;
        certificate->tbs.len = end - certificate->tbs.p;

        /*
         * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
         * Example:
         * v1: no tag at all, or a0 03 02 01 00
         * v2: a0 03 02 01 01
         * v3: a0 03 02 01 01
         *
         * CertificateSerialNumber  ::=  INTEGER
         *
         * signature            AlgorithmIdentifier
         */
        if(0 != (ret = LTX509GetVersion(&p, end, &certificate->version)) ||
           0 != (ret = LTX509GetSerial(&p, end, &certificate->serial)) ||
           0 != (ret = LTX509GetAlg(&p, end, &certificate->sigOid, &sigParams1))) {
            break;
        }

        if (certificate->version < 0 || certificate->version > 2) {
            ret = LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_UNKNOWN_VERSION);
            break;
        }

        // increment version value to match version.
        ++certificate->version;

        // signature algorithm
        if (0 != (ret = LTX509GetSigAlg(&certificate->sigOid, &sigParams1, &certificate->sigMd, &certificate->sigPk, &certificate->sigOpts))) {
            break;
        }

        // issuer    Name
        certificate->issuerRaw.p = p;
        if (0 != (ret = LTAsn1GetTag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
            ret = LTTLS_ERROR(0x0920 + 3, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
            break;
        }
        if (0 != (ret = LTX509GetName(&p, p + len, &certificate->issuer))) {
            break;
        }
        certificate->issuerRaw.len = p - certificate->issuerRaw.p;

        /*
         * Validity ::= SEQUENCE {
         *      notBefore      Time,
         *      notAfter       Time }
         *
         */
        if (0 != (ret = LTX509GetDates(&p, end, &certificate->validFrom, &certificate->validTo))) {
            break;
        }

        // validate time
        LTSystemTimeZone *timezone = lt_openlibrary(LTSystemTimeZone);
        if (!timezone) {
            ret = LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_ALLOC_FAILED);
            break;
        }
        ret = LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_INVALID_DATE);
        LTTime timeValidFrom = (LTTime){};
        LTTime timeValidTo = (LTTime){};
        if (timezone->CalendarTimeToClockTime(&certificate->validFrom, &timeValidFrom) &&
            timezone->CalendarTimeToClockTime(&certificate->validTo, &timeValidTo)) {
            LTTime timeNow = LT_GetCore()->GetClockTimeUTC();
            if (LTTime_IsZero(timeNow)) timeNow = LT_GetCore()->GetApproximateClockTimeUTC();
            if (LTTime_IsLessThan(timeNow, timeValidTo)) ret = 0;
            // TODO maybe validate against timeValidFrom, LTTime_IsLessThan(timeValidFrom, timeNow)
        }
        lt_closelibrary(timezone);
        if (0 != ret) break;

        // subject      Name
        certificate->subjectRaw.p = p;
        if (0 != (ret = LTAsn1GetTag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
            ret = LTTLS_ERROR(0x0920 + 4, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, ret));
            break;
        }
        if (len && 0 != (ret = LTX509GetName(&p, p + len, &certificate->subject))) {
            break;
        }

        certificate->subjectRaw.len = p - certificate->subjectRaw.p;

        // SubjectPublicKeyInfo
        if (0 != (ret = LTX509ParseSubPubKey(&p, end, &certificate->pkAlg, &certificate->pkParams, &certificate->pubKey))) {
            break;
        }

        /*
         *  issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
         *                       -- If present, version shall be v2 or v3
         *  subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
         *                       -- If present, version shall be v2 or v3
         *  extensions      [3]  EXPLICIT Extensions OPTIONAL
         *                       -- If present, version shall be v3
         */
        if (certificate->version == 2 || certificate->version == 3) {
            if (0 != (ret = LTX509GetUid(&p, end, &certificate->issuerId, 1)) &&
                0 != (ret = LTX509GetUid(&p, end, &certificate->subjectId, 2))) {
                break;
            }
        }

        if (certificate->version == 3) {
            if (0 != (ret = LTX509GetCertExt(&p, end, certificate))) {
                break;
            }
        }

        if (p != end) {
            ret = LTTLS_ERROR(0x0920 + 5, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
            break;
        }
        /****** End of signedCertificate (tbs) ******/

        end = certEnd;

        /****** Begin of algorithmIdentifier ******
         *  signatureAlgorithm   AlgorithmIdentifier,
         *  signatureValue       BIT STRING
         */
        if (0 != (ret = LTX509GetAlg(&p, certEnd, &sigOid2, &sigParams2))) {
            break;
        }

        if (certificate->sigOid.len != sigOid2.len ||
            0 != lt_memcmp(certificate->sigOid.p, sigOid2.p, certificate->sigOid.len) ||
            sigParams1.tag != sigParams2.tag ||
            sigParams1.len != sigParams2.len ||
            (0 != sigParams1.len && 0 != lt_memcmp(sigParams1.p, sigParams2.p, sigParams1.len))) {
            ret = LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_SIG_MISMATCH);
            break;
        }
        /****** End of algorithmIdentifier ******/

        // signature
        if (0 != (ret = LTX509GetSig(&p, certEnd, &certificate->sig))) break;
        if ((sigOid2.len == sizeof(MBEDTLS_OID_ECDSA_SHA256) - 1) && lt_memcmp(sigOid2.p, MBEDTLS_OID_ECDSA_SHA256, sizeof(MBEDTLS_OID_ECDSA_SHA256) - 1) == 0) {
            // ecdsa-p256
            LTSystemCryptoEncoder *cryptoEncoder = lt_createobject(LTSystemCryptoEncoder);
            if (!cryptoEncoder) {
                ret = LTTLS_ERROR(0x0920 + 1, MBEDTLS_ERR_X509_ALLOC_FAILED);
                break;
            }
            bool bDecoded = cryptoEncoder->API->DecodeEcdsaSignature(certificate->sig.p, certificate->sig.len, certificate->EcdsaSig, ECDSA_P256_SIGNATURE_LENGTH);
            lt_destroyobject(cryptoEncoder);
            if (!bDecoded) {
                ret = LTTLS_ERROR(0x0920, MBEDTLS_ERR_X509_INVALID_SIGNATURE);
                break;
            }

        } else if ((sigOid2.len == sizeof(LTTLS_OID_ED25519) - 1) && lt_memcmp(sigOid2.p, LTTLS_OID_ED25519, sizeof(LTTLS_OID_ED25519) - 1) == 0) {
            // ed25519
            lt_memcpy(certificate->EddsaSig, certificate->sig.p, certificate->sig.len);
        }

        if (p != certEnd) {
            ret = LTTLS_ERROR(0x0920 + 6, MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH));
            break;
        }
    } while (0);

    if (0 != ret) {
        LTX509FreeCert(certificate);
        return ret;
    }
    return 0;
}

/**
 * @brief Find a valid CA key for cert.
 *
 * @param[in]  caKeys       The array of CA keys
 * @param[in]  caKeyCount   The count of CA keys
 * @param[out] certificate  The CA cert
 * @return The index of the CA key. If no key found, return 0xFF.
 */
static u8 LTX509FindValidCAKey(const LTPublicKey *caKeys, u8 caKeyCount, LTX509Cert *certificate) {
    u8 i;
    if (certificate->authorityKeyId.p && certificate->authorityKeyId.len != 0) {
        // The CA cert has a authority key ID, so the cert is certified by the key of the authority.
        for (i = 0; i < caKeyCount; ++i) {
            if (certificate->authorityKeyId.len == caKeys[i].keyIdLen && caKeys[i].keyId &&
                0 == lt_memcmp(certificate->authorityKeyId.p, caKeys[i].keyId, certificate->authorityKeyId.len)) {
                return i;
            }
        }

    } else if (certificate->pubKey.p && certificate->pubKey.len != 0) {
        // The CA cert is self-signed, so the cert key is one of the CA keys.
        for (i = 0; i < caKeyCount; ++i) {
            if (certificate->pubKey.len == caKeys[i].keyLen && caKeys[i].key &&
                0 == lt_memcmp(certificate->pubKey.p, caKeys[i].key, certificate->pubKey.len)) {
                return i;
            }
        }
    }

    return 0xFF;
}

/**
 * @brief   Validate a certificate
 *
 * @param[in] certificate  The certificate to validate
 * @param[in] publicKey    The public key to validate the certificate
 * @return Crypto error code
 */
static LTSystemCryptoResult LTX509ValidateCertificate(const LTX509Cert *certificate, const LTPublicKey *publicKey) {
    LTSystemCryptoResult ret = 0;
    // TODO validate time

    LTSystemCrypto *crypto = lt_openlibrary(LTSystemCrypto);
    if (!crypto) return kLTSystemCrypto_Result_Error;

    // validate signature
    if (publicKey->type == SIGNATURE_ECDSA_SECP256R1_SHA256) {
        // ECDSA-P256-SHA256
        ret = crypto->VerifyEcdsa(certificate->tbs.p, certificate->tbs.len, certificate->EcdsaSig, publicKey->key);

    } else if (publicKey->type == SIGNATURE_ED25519) {
        // ED25519
        ret = crypto->VerifyEddsa(certificate->tbs.p, certificate->tbs.len, certificate->EddsaSig, publicKey->key);
    }

    lt_closelibrary(crypto);
    return ret;
}

/*********************** End of X509 parsing functions ***********************/

/**************************** X509 check functions ***************************/
#if 0
// Error ID 0x0900
/**
 * @brief  Check key usage
 *
 * @param[in] certificate  The pointer to the certificate
 * @param[in] usage        Key usage type
 * @return  Error code
 */
static int LTX509CertCheckKeyUsage(const LTX509Cert *certificate, u16 usage) {

    if (0 == (certificate->extTypes & MBEDTLS_X509_EXT_KEY_USAGE)) {
        return 0;
    }

    u16 mayMask = MBEDTLS_X509_KU_ENCIPHER_ONLY | MBEDTLS_X509_KU_DECIPHER_ONLY;
    u16 usageMusk = usage & ~mayMask;
    if (((certificate->keyUsage & ~mayMask) & usageMusk) != usageMusk) {
        return LTTLS_ERROR(0x0900 + 1, MBEDTLS_ERR_X509_BAD_INPUT_DATA);
    }

    u16 usageMay = usage & mayMask;
    if (((certificate->keyUsage & mayMask) | usageMay) != usageMay) {
        return LTTLS_ERROR(0x0900 + 2, MBEDTLS_ERR_X509_BAD_INPUT_DATA);
    }

    return 0;
}

// Error ID 0x0908
/**
 * @brief  Check extended key usage
 *
 * @param[in] certificate  The pointer to the certificate
 * @param[in] usageOid     The usage OID
 * @param[in] usageLen     The usage length
 * @return  Error code
 */
static int LTX509CertCheckExtendedKeyUsage(const LTX509Cert *certificate, const char *usageOid, LT_SIZE usageLen) {
    const LTAsn1Sequence *curr;

    /* Extension is not mandatory, absent means no restriction */
    if (0 == (certificate->extTypes & MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE)) {
        return 0;
    }

    /* Look for the requested usage (or wildcard ANY) in our list */
    const LTAsn1Buf *currOid;
    for (curr = &certificate->extKeyUsage; curr != NULL; curr = curr->next) {
        currOid = &curr->buf;
        if (currOid->len == usageLen && 0 == lt_memcmp(currOid->p, usageOid, usageLen)) {
            return 0;
        }
        if (0 == MBEDTLS_OID_CMP(MBEDTLS_OID_ANY_EXTENDED_KEY_USAGE, currOid)) {
            return 0;
        }
    }

    return LTTLS_ERROR(0x0908, MBEDTLS_ERR_X509_BAD_INPUT_DATA);
}

// Error ID 0x0910
/**
 * @brief  Check certificate usage
 *
 * @param[in]  certificate The pointer to the certificate
 * @param[out] flags       The pointer to the flags
 * @return  Error code
 */
static int LTX509CheckCertUsage(const LTX509Cert *certificate, u32 *flags) {
    int ret = 0;
    const char *extOid;
    LT_SIZE extLen;

    if (0 != (ret = LTX509CertCheckKeyUsage(certificate, MBEDTLS_X509_KU_DIGITAL_SIGNATURE))) {
        *flags |= MBEDTLS_X509_BADCERT_KEY_USAGE;
        return ret;
    }

        extOid = MBEDTLS_OID_CLIENT_AUTH;
        extLen = MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH);

    if (0 != (ret = LTX509CertCheckExtendedKeyUsage(certificate, extOid, extLen))) {
        *flags |= MBEDTLS_X509_BADCERT_EXT_KEY_USAGE;
        return ret;
    }

    // TODO if needed, LTX509CertCheckEELocallyTrusted

    return 0;
}
#endif
/************************ End of X509 check functions ************************/

/*************************** X509 APIs ***************************/
static void FreePublicKey(LTPublicKey *key) {
    if (!key) return;
    lt_free(key->key);
    lt_free(key->keyId);
    lt_memset(key, 0, sizeof(LTPublicKey));
}

static bool CopyPublicKey(LTPublicKey *output, LTDriverCrypto_KeyType type, const u8 *publicKey, u16 keyLen, const u8 *keyId, u16 keyIdLen) {
    bool ret = false;
    lt_memset(output, 0, sizeof(LTPublicKey));
    do {
        // copy type
        output->type = type;

        // copy public key
        if (!publicKey || !keyLen) break;
        output->key = lt_malloc(keyLen);
        if (!output->key) break;
        output->keyLen = keyLen;
        lt_memcpy(output->key, publicKey, keyLen);

        // copy keyId
        if (keyId && keyIdLen) {
            output->keyId = lt_malloc(keyIdLen);
            if (!output->keyId) break;
            output->keyIdLen = keyIdLen;
            lt_memcpy(output->keyId, keyId, keyIdLen);
        }

        ret = true;
    } while (0);
    if (!ret) FreePublicKey(output);
    return ret;
}

// Error ID 0x0910
/**
 * @brief   Extract ecdsa public key from x.509 certificate
 *
 * @param[in]  certData    The certificate data
 * @param[in]  certLen     The certificate length
 * @param[out] publicKey   The public key inside the certificate
 * @param[out] signature   The signature of the certificate
 * @param[out] certTbs     The certificate TBS section start
 * @param[out] certTbsLen  The certificate TBS section length
 *
 * @return Error code
 */
static int LTNetX509_ExtractDataFromCert(const u8 *certData, u16 certLen, LTPublicKey *publicKey, u8 *signature, u8 **certTbs, u16 *certTbsLen) {
    if (!certData || !certLen) {
        return LTTLS_ERROR(0x0910, MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
    }
    int ret = 0;
    LTX509Cert *cert = lt_malloc(sizeof(LTX509Cert));
    do {
        if (!cert) {
            ret = LTTLS_ERROR(0x0910 + 1, MBEDTLS_ERR_SSL_ALLOC_FAILED);
            break;
        }
        lt_memset(cert, 0, sizeof(LTX509Cert));
        ret = LTX509ParseCertDer(cert, certData, certLen);
        if (0 != ret) {
            LTLOG_REDALERT("cert.key", "parse cert -%08lX", LT_Ps32(-ret));
            ret = LTTLS_ERROR(0x0910 + 2, MBEDTLS_ERR_SSL_UNSUPPORTED_CERTIFICATE);
            break;
        }
        if (publicKey) {
            if (!CopyPublicKey(publicKey, cert->pkAlg, cert->pubKey.p, cert->pubKey.len, cert->subjectKeyId.p, cert->subjectKeyId.len)) {
                ret = LTTLS_ERROR(0x0910 + 4, MBEDTLS_ERR_SSL_ALLOC_FAILED);
            }
        }
        if (signature) lt_memcpy(signature, cert->EcdsaSig, ECDSA_P256_SIGNATURE_LENGTH);
        if (certTbs && certTbsLen) {
            *certTbs    = cert->tbs.p;
            *certTbsLen = cert->tbs.len;
        }
    } while(0);

    if (cert) {
        LTX509FreeCert(cert);
        lt_free(cert);
    }
    return ret;
}

// Error ID 0x0930
int LTNetX509_ParseValidateCertChain(const u8 *certChain, u16 certChainLen, const LTPublicKey *caKeys, u8 caKeyCount, const char *serverName, LTPublicKey *publicKey) {
    const u8 *p = certChain;
    const u8 *certListEnd = p + certChainLen;
    struct CertLocal {
        LTX509Chain chain;
        LTX509Cert cert;
        LTPublicKey pubKey;
    };
    struct CertLocal *tmp = lt_malloc(sizeof(struct CertLocal));
    if (!tmp) return LTTLS_ERROR(0x0930 + 1, MBEDTLS_ERR_SSL_ALLOC_FAILED);
    lt_memset(tmp, 0, sizeof(struct CertLocal));

    /* First, we go through the chain to find out how many certificates. */
    int ret = 0;
    tmp->chain.count = 0;
    u16 itemLen;
    while (p < certListEnd) {
        if (tmp->chain.count >= kLTNetX509_MaxVerifyChainSize) {
            /* The chain is too long */
            ret = LTTLS_ERROR(0x0930 + 1, MBEDTLS_ERR_SSL_BAD_CERTIFICATE);
            goto _finish;
        }

        LTTLS_CHK_BUF_READ_PTR_GOTO(0x0930 + 4, p, certListEnd, 3, _finish);
        /* Don't support any certificate larger than 65535 bytes */
        if (p[0] != 0) {
            ret = LTTLS_ERROR(0x0930 + 5, MBEDTLS_ERR_SSL_DECODE_ERROR);
            goto _finish;
        }
        itemLen = (((u16)p[1]) << 8) + p[2];
        p += 3;
        LTTLS_CHK_BUF_READ_PTR_GOTO(0x0930 + 6, p, certListEnd, itemLen, _finish);
        tmp->chain.certs[tmp->chain.count].certLen = itemLen;
        tmp->chain.certs[tmp->chain.count].cert    = p;
        p += itemLen;
        ++tmp->chain.count;

        /* Ignore extension outside a certificate */
        LTTLS_CHK_BUF_READ_PTR_GOTO(0x0930 + 7, p, certListEnd, 2, _finish);
        itemLen = (((u16)p[0]) << 8) + p[1];
        p += 2;
        LTTLS_CHK_BUF_READ_PTR_GOTO(0x0930 + 8, p, certListEnd, itemLen, _finish);
        p += itemLen;
    }

    if (tmp->chain.count == 0) {
        ret = LTTLS_ERROR(0x0930, MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED);
        goto _finish;
    }

    /* Second, we parse and validate the root cert */
    // On error, the cert is cleared, and any heap of the cert is freed.
    ret = LTX509ParseCertDer(&tmp->cert, tmp->chain.certs[tmp->chain.count - 1].cert, tmp->chain.certs[tmp->chain.count - 1].certLen);
    if (0 != ret) {
        LTLOG_REDALERT("cert.root", "parse root ca -%08lX", LT_Ps32(-ret));
        ret = LTTLS_ERROR(0x0930 + 1, MBEDTLS_ERR_SSL_UNSUPPORTED_CERTIFICATE);
        goto _finish;
    }

    // Search CA root key in client
    u8 k = LTX509FindValidCAKey(caKeys, caKeyCount, &tmp->cert);
    if (k == 0xFF) {
        ret = LTTLS_ERROR(0x0930, MBEDTLS_ERR_SSL_UNKNOWN_CA);
        goto _finish;
    }

    ret = LTX509ValidateCertificate(&tmp->cert, &caKeys[k]);
    if (0 != ret) {
        ret = LTTLS_ERROR(0x0930 + 2, MBEDTLS_ERR_SSL_BAD_CERTIFICATE + ret);
        goto _finish;
    }

    // Parse and validate intermediate and end certs
    --tmp->chain.count;
    while (tmp->chain.count > 0) {
        // Keep previous cert's public key to validate the next cert
        if (!CopyPublicKey(&tmp->pubKey, tmp->cert.pkAlg, tmp->cert.pubKey.p, tmp->cert.pubKey.len, NULL, 0)) {
            ret = LTTLS_ERROR(0x0930 + 2, MBEDTLS_ERR_SSL_ALLOC_FAILED);
            goto _finish;
        }
        LTX509FreeCert(&tmp->cert);
        ret = LTX509ParseCertDer(&tmp->cert, tmp->chain.certs[tmp->chain.count - 1].cert, tmp->chain.certs[tmp->chain.count - 1].certLen);
        if (0 != ret) {
            LTLOG_REDALERT("cert", "parse cert %d -%08lX", tmp->chain.count - 1, LT_Ps32(-ret));
            ret = LTTLS_ERROR(0x0930 + 2, MBEDTLS_ERR_SSL_UNSUPPORTED_CERTIFICATE);
            goto _finish;
        }
        ret = LTX509ValidateCertificate(&tmp->cert, &tmp->pubKey);
        if (0 != ret) {
            ret = LTTLS_ERROR(0x0930 + 3, MBEDTLS_ERR_SSL_BAD_CERTIFICATE + ret);
            goto _finish;
        }
        --tmp->chain.count;
        FreePublicKey(&tmp->pubKey);
    }

    // Copy end cert's public key to peer public key
    if (!CopyPublicKey(publicKey, tmp->cert.pkAlg, tmp->cert.pubKey.p, tmp->cert.pubKey.len, tmp->cert.subjectKeyId.p, tmp->cert.subjectKeyId.len)) {
        ret = LTTLS_ERROR(0x0930 + 3, MBEDTLS_ERR_SSL_ALLOC_FAILED);
        goto _finish;
    }

    // Verify server name with common name and subject alt names of end cert.
    u16 nameLen = lt_strlen(serverName);

    bool bFound = false;
    // compare common name
    LTAsn1NamedData *curName = &tmp->cert.subject;
    while (curName && !bFound) {
        if (curName->oid.len == 3 && lt_memcmp(curName->oid.p, MBEDTLS_OID_AT_CN, 3) == 0) {
            if (nameLen == curName->val.len && lt_memcmp(serverName, curName->val.p, nameLen) == 0) {
                bFound = true;
                break;
            }
        }
        curName = curName->next;
    }
    // compare subject alt names
    LTAsn1Sequence *altName = &tmp->cert.subjectAltNames;
    while (altName && !bFound) {
        if (nameLen == altName->buf.len && lt_memcmp(serverName, altName->buf.p, nameLen) == 0) {
            bFound = true;
            break;
        }
        altName = altName->next;
    }

    ret = bFound ? 0 : LTTLS_ERROR(0x0930 + 3, MBEDTLS_ERR_SSL_UNSUPPORTED_CERTIFICATE);

_finish:
    LTX509FreeCert(&tmp->cert);
    FreePublicKey(&tmp->pubKey);
    lt_free(tmp);
    return ret;
}

/** LTNetX509 library init and fini *******************************************/
static void LTNetX509Impl_LibFini(void) {
}

static bool LTNetX509Impl_LibInit(void) {
    s_core = LT_GetCore();
    return true;
}

/* LTNetX509 library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTNetX509) {
    .ExtractDataFromCert     = &LTNetX509_ExtractDataFromCert,
    .ParseValidateCertChain  = &LTNetX509_ParseValidateCertChain,
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-May-22   gallienus   created
 */
