/*******************************************************************************
 * source/lt/net/tls/LTNetTlsDefs.h
 * 
 * Macros for TLS.
 * Macros starting with MBEDTLS are ported from MbedTLS.
 * Macros starting with LT are added for LT.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * 
 * This file has been created to complete TLS operations for LT, reduce code
 * and data sizes, and be compatible with LT coding standard.
 * 
 * This file has modified the source code from https://github.com/Mbed-TLS/mbedtls
 * 
 * The modified source files are error.h, ssl.h, ssl_ciphersuites.h, ssl_misc.h,
 * and ssl_tls13_keys.h in Mbed TLS.
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
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_TLS_LTNETTLSDEFS_H
#define ROKU_LT_SOURCE_LT_NET_TLS_LTNETTLSDEFS_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

#define LTTLS_PADDINGLEN(len, padding)                       (((padding) + 1 - ((len) & (padding))) & (padding))

#define LTTLS_RECEIVED_SIG_ALGS_SIZE                         4    /**< Number of valid signature algorithms + 1 */

#define SSL_SERVER_HELLO_COORDINATE_HELLO                    0
#define SSL_SERVER_HELLO_COORDINATE_HRR                      1
#define SSL_SERVER_HELLO_COORDINATE_TLS1_2                   2

/* Various constants 
 * These are the high and low bytes of ProtocolVersion as defined by:
 * - RFC 5246: ProtocolVersion version = { 3, 3 };     // TLS v1.2
 * - RFC 8446: see section 4.2.1 */
#define MBEDTLS_SSL_MAJOR_VERSION_3                          3
#define MBEDTLS_SSL_MINOR_VERSION_1                          1    /**< TLS v1.0 */
#define MBEDTLS_SSL_MINOR_VERSION_2                          2    /**< TLS v1.1 */
#define MBEDTLS_SSL_MINOR_VERSION_3                          3    /**< TLS v1.2 */
#define MBEDTLS_SSL_MINOR_VERSION_4                          4    /**< TLS v1.3 */

#define MBEDTLS_SSL_TRANSPORT_STREAM                         0    /**< TLS */
#define MBEDTLS_SSL_MAX_HOST_NAME_LEN                      255    /**< Maximum host name defined in RFC 1035 */
#define MBEDTLS_SSL_MAX_ALPN_NAME_LEN                      255    /**< Maximum size in bytes of a protocol name in alpn ext., RFC 7301 */

#define MBEDTLS_SSL_MAX_ALPN_LIST_LEN                    65535    /**< Maximum size in bytes of list in alpn ext., RFC 7301 */

/* RFC 6066 section 4, see also mfl_code_to_length in ssl_tls.c
 * NONE must be zero so that memset()ing structure to zero works */
#define MBEDTLS_SSL_MAX_FRAG_LEN_NONE                        0    /**< don't use this extension   */
#define MBEDTLS_SSL_MAX_FRAG_LEN_512                         1    /**< MaxFragmentLength 2^9      */
#define MBEDTLS_SSL_MAX_FRAG_LEN_1024                        2    /**< MaxFragmentLength 2^10     */
#define MBEDTLS_SSL_MAX_FRAG_LEN_2048                        3    /**< MaxFragmentLength 2^11     */
#define MBEDTLS_SSL_MAX_FRAG_LEN_4096                        4    /**< MaxFragmentLength 2^12     */
#define MBEDTLS_SSL_MAX_FRAG_LEN_INVALID                     5    /**< first invalid value        */

#define MBEDTLS_SSL_IS_CLIENT                                0
#define MBEDTLS_SSL_IS_SERVER                                1

#define MBEDTLS_SSL_EXTENDED_MS_DISABLED                     0
#define MBEDTLS_SSL_EXTENDED_MS_ENABLED                      1

#define MBEDTLS_SSL_CID_DISABLED                             0
#define MBEDTLS_SSL_CID_ENABLED                              1

#define MBEDTLS_SSL_ETM_DISABLED                             0
#define MBEDTLS_SSL_ETM_ENABLED                              1

#define MBEDTLS_SSL_COMPRESS_NULL                            0

#define MBEDTLS_SSL_VERIFY_NONE                              0
#define MBEDTLS_SSL_VERIFY_OPTIONAL                          1
#define MBEDTLS_SSL_VERIFY_REQUIRED                          2
#define MBEDTLS_SSL_VERIFY_UNSET                             3    /* Used only for sni_authmode */

#define MBEDTLS_SSL_LEGACY_RENEGOTIATION                     0
#define MBEDTLS_SSL_SECURE_RENEGOTIATION                     1

#define MBEDTLS_SSL_RENEGOTIATION_DISABLED                   0
#define MBEDTLS_SSL_RENEGOTIATION_ENABLED                    1

#define MBEDTLS_SSL_ANTI_REPLAY_DISABLED                     0
#define MBEDTLS_SSL_ANTI_REPLAY_ENABLED                      1

#define MBEDTLS_SSL_RENEGOTIATION_NOT_ENFORCED              -1
#define MBEDTLS_SSL_RENEGO_MAX_RECORDS_DEFAULT              16

#define MBEDTLS_SSL_LEGACY_NO_RENEGOTIATION                  0
#define MBEDTLS_SSL_LEGACY_ALLOW_RENEGOTIATION               1
#define MBEDTLS_SSL_LEGACY_BREAK_HANDSHAKE                   2

#define MBEDTLS_SSL_TRUNC_HMAC_DISABLED                      0
#define MBEDTLS_SSL_TRUNC_HMAC_ENABLED                       1
#define MBEDTLS_SSL_TRUNCATED_HMAC_LEN                      10    /* 80 bits, rfc 6066 section 7 */

#define MBEDTLS_SSL_SESSION_TICKETS_DISABLED                 0
#define MBEDTLS_SSL_SESSION_TICKETS_ENABLED                  1

#define MBEDTLS_SSL_PRESET_DEFAULT                           0
#define MBEDTLS_SSL_PRESET_SUITEB                            2

#define MBEDTLS_SSL_CERT_REQ_CA_LIST_ENABLED                 1
#define MBEDTLS_SSL_CERT_REQ_CA_LIST_DISABLED                0

#define MBEDTLS_SSL_DTLS_SRTP_MKI_UNSUPPORTED                0
#define MBEDTLS_SSL_DTLS_SRTP_MKI_SUPPORTED                  1

#define MBEDTLS_SSL_SRV_CIPHERSUITE_ORDER_CLIENT             1
#define MBEDTLS_SSL_SRV_CIPHERSUITE_ORDER_SERVER             0

/* Length of the verify data for secure renegotiation */
#define MBEDTLS_SSL_VERIFY_DATA_MAX_LEN                      12

/* Message, alert and handshake types */
#define MBEDTLS_SSL_MSG_CHANGE_CIPHER_SPEC                   20
#define MBEDTLS_SSL_MSG_ALERT                                21
#define MBEDTLS_SSL_MSG_HANDSHAKE                            22
#define MBEDTLS_SSL_MSG_APPLICATION_DATA                     23
#define MBEDTLS_SSL_MSG_CID                                  25

#define MBEDTLS_SSL_ALERT_LEVEL_WARNING                       1
#define MBEDTLS_SSL_ALERT_LEVEL_FATAL                         2

#define MBEDTLS_SSL_ALERT_MSG_CLOSE_NOTIFY                    0    /* 0x00 */
#define MBEDTLS_SSL_ALERT_MSG_UNEXPECTED_MESSAGE             10    /* 0x0A */
#define MBEDTLS_SSL_ALERT_MSG_BAD_RECORD_MAC                 20    /* 0x14 */
#define MBEDTLS_SSL_ALERT_MSG_DECRYPTION_FAILED              21    /* 0x15 */
#define MBEDTLS_SSL_ALERT_MSG_RECORD_OVERFLOW                22    /* 0x16 */
#define MBEDTLS_SSL_ALERT_MSG_DECOMPRESSION_FAILURE          30    /* 0x1E */
#define MBEDTLS_SSL_ALERT_MSG_HANDSHAKE_FAILURE              40    /* 0x28 */
#define MBEDTLS_SSL_ALERT_MSG_NO_CERT                        41    /* 0x29 */
#define MBEDTLS_SSL_ALERT_MSG_BAD_CERT                       42    /* 0x2A */
#define MBEDTLS_SSL_ALERT_MSG_UNSUPPORTED_CERT               43    /* 0x2B */
#define MBEDTLS_SSL_ALERT_MSG_CERT_REVOKED                   44    /* 0x2C */
#define MBEDTLS_SSL_ALERT_MSG_CERT_EXPIRED                   45    /* 0x2D */
#define MBEDTLS_SSL_ALERT_MSG_CERT_UNKNOWN                   46    /* 0x2E */
#define MBEDTLS_SSL_ALERT_MSG_ILLEGAL_PARAMETER              47    /* 0x2F */
#define MBEDTLS_SSL_ALERT_MSG_UNKNOWN_CA                     48    /* 0x30 */
#define MBEDTLS_SSL_ALERT_MSG_ACCESS_DENIED                  49    /* 0x31 */
#define MBEDTLS_SSL_ALERT_MSG_DECODE_ERROR                   50    /* 0x32 */
#define MBEDTLS_SSL_ALERT_MSG_DECRYPT_ERROR                  51    /* 0x33 */
#define MBEDTLS_SSL_ALERT_MSG_EXPORT_RESTRICTION             60    /* 0x3C */
#define MBEDTLS_SSL_ALERT_MSG_PROTOCOL_VERSION               70    /* 0x46 */
#define MBEDTLS_SSL_ALERT_MSG_INSUFFICIENT_SECURITY          71    /* 0x47 */
#define MBEDTLS_SSL_ALERT_MSG_INTERNAL_ERROR                 80    /* 0x50 */
#define MBEDTLS_SSL_ALERT_MSG_INAPROPRIATE_FALLBACK          86    /* 0x56 */
#define MBEDTLS_SSL_ALERT_MSG_USER_CANCELED                  90    /* 0x5A */
#define MBEDTLS_SSL_ALERT_MSG_NO_RENEGOTIATION              100    /* 0x64 */
#define MBEDTLS_SSL_ALERT_MSG_UNSUPPORTED_EXT               110    /* 0x6E */
#define MBEDTLS_SSL_ALERT_MSG_UNRECOGNIZED_NAME             112    /* 0x70 */
#define MBEDTLS_SSL_ALERT_MSG_UNKNOWN_PSK_IDENTITY          115    /* 0x73 */
#define MBEDTLS_SSL_ALERT_MSG_NO_APPLICATION_PROTOCOL       120    /* 0x78 */

#define MBEDTLS_SSL_HS_HELLO_REQUEST                          0
#define MBEDTLS_SSL_HS_CLIENT_HELLO                           1    // 1.3
#define MBEDTLS_SSL_HS_SERVER_HELLO                           2    // 1.3
#define MBEDTLS_SSL_HS_HELLO_VERIFY_REQUEST                   3
#define MBEDTLS_SSL_HS_NEW_SESSION_TICKET                     4    // 1.3
#define MBEDTLS_SSL_HS_END_OF_EARLY_DATA                      5    // 1.3 new
#define MBEDTLS_SSL_HS_ENCRYPTED_EXTENSIONS                   8    // 1.3 new
#define MBEDTLS_SSL_HS_CERTIFICATE                           11    // 1.3
#define MBEDTLS_SSL_HS_SERVER_KEY_EXCHANGE                   12
#define MBEDTLS_SSL_HS_CERTIFICATE_REQUEST                   13    // 1.3
#define MBEDTLS_SSL_HS_SERVER_HELLO_DONE                     14
#define MBEDTLS_SSL_HS_CERTIFICATE_VERIFY                    15    // 1.3
#define MBEDTLS_SSL_HS_CLIENT_KEY_EXCHANGE                   16
#define MBEDTLS_SSL_HS_FINISHED                              20    // 1.3
#define MBEDTLS_SSL_HS_KEY_UPDATE                            24    // 1.3 new
#define MBEDTLS_SSL_HS_MESSAGE_HASH                         254

/* TLS extensions */
#define MBEDTLS_TLS_EXT_SERVERNAME                            0
#define MBEDTLS_TLS_EXT_SERVERNAME_HOSTNAME                   0
#define MBEDTLS_TLS_EXT_MAX_FRAGMENT_LENGTH                   1
#define MBEDTLS_TLS_EXT_TRUNCATED_HMAC                        4
#define MBEDTLS_TLS_EXT_STATUS_REQUEST                        5    /* RFC 6066 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_SUPPORTED_ELLIPTIC_CURVES            10
#define MBEDTLS_TLS_EXT_SUPPORTED_GROUPS                     10    /* RFC 8422,7919 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_SUPPORTED_POINT_FORMATS              11
#define MBEDTLS_TLS_EXT_SIG_ALG                              13    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_USE_SRTP                             14
#define MBEDTLS_TLS_EXT_HEARTBEAT                            15    /* RFC 6520 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_ALPN                                 16
#define MBEDTLS_TLS_EXT_SCT                                  18    /* RFC 6962 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_CLI_CERT_TYPE                        19    /* RFC 7250 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_SERV_CERT_TYPE                       20    /* RFC 7250 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_PADDING                              21    /* RFC 7685 TLS 1.2 and 1.3 */
#define MBEDTLS_TLS_EXT_ENCRYPT_THEN_MAC                     22    /* 0x16 */
#define MBEDTLS_TLS_EXT_EXTENDED_MASTER_SECRET               23    /* 23 */
#define MBEDTLS_TLS_EXT_SESSION_TICKET                       35
#define MBEDTLS_TLS_EXT_PRE_SHARED_KEY                       41    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_EARLY_DATA                           42    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_SUPPORTED_VERSIONS                   43    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_COOKIE                               44    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_PSK_KEY_EXCHANGE_MODES               45    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_CERT_AUTH                            47    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_OID_FILTERS                          48    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_POST_HANDSHAKE_AUTH                  49    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_SIG_ALG_CERT                         50    /* RFC 8446 TLS 1.3 */
#define MBEDTLS_TLS_EXT_KEY_SHARE                            51    /* RFC 8446 TLS 1.3 */

/*
 * TLS 1.3 NamedGroup values
 *
 * From RF 8446
 *    enum {
 *         // Elliptic Curve Groups (ECDHE)
 *         secp256r1(0x0017), secp384r1(0x0018), secp521r1(0x0019),
 *         x25519(0x001D), x448(0x001E),
 *         // Finite Field Groups (DHE)
 *         ffdhe2048(0x0100), ffdhe3072(0x0101), ffdhe4096(0x0102),
 *         ffdhe6144(0x0103), ffdhe8192(0x0104),
 *         // Reserved Code Points
 *         ffdhe_private_use(0x01FC..0x01FF),
 *         ecdhe_private_use(0xFE00..0xFEFF),
 *         (0xFFFF)
 *     } NamedGroup;
 */
/* Elliptic Curve Groups (ECDHE) */
#define MBEDTLS_SSL_IANA_TLS_GROUP_NONE                        0x0
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP192K1                   0x0012
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP192R1                   0x0013
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP224K1                   0x0014
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP224R1                   0x0015
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP256K1                   0x0016
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1                   0x0017
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1                   0x0018
#define MBEDTLS_SSL_IANA_TLS_GROUP_SECP521R1                   0x0019
#define MBEDTLS_SSL_IANA_TLS_GROUP_BP256R1                     0x001A
#define MBEDTLS_SSL_IANA_TLS_GROUP_BP384R1                     0x001B
#define MBEDTLS_SSL_IANA_TLS_GROUP_BP512R1                     0x001C
#define MBEDTLS_SSL_IANA_TLS_GROUP_X25519                      0x001D
#define MBEDTLS_SSL_IANA_TLS_GROUP_X448                        0x001E
/* Finite Field Groups (DHE) */
#define MBEDTLS_SSL_IANA_TLS_GROUP_FFDHE2048                   0x0100
#define MBEDTLS_SSL_IANA_TLS_GROUP_FFDHE3072                   0x0101
#define MBEDTLS_SSL_IANA_TLS_GROUP_FFDHE4096                   0x0102
#define MBEDTLS_SSL_IANA_TLS_GROUP_FFDHE6144                   0x0103
#define MBEDTLS_SSL_IANA_TLS_GROUP_FFDHE8192                   0x0104

/* TLS 1.3 Key Exchange Modes */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK            ( 1u << 0 ) /**< Pure-PSK TLS 1.3 key exchange, encompassing both externally agreed PSKs as well as resumption PSKs. */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL      ( 1u << 1 ) /**< Pure-Ephemeral TLS 1.3 key exchanges, including for example ECDHE and DHE key exchanges. */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL  ( 1u << 2 ) /**< PSK-Ephemeral TLS 1.3 key exchanges, using both a PSK and an ephemeral key exchange. */

/* Convenience macros for sets of key exchanges. */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_ALL                         \
    ( MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK              |            \
      MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL    |            \
      MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL )        /**< All TLS 1.3 key exchanges */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_ALL                     \
    ( MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK              |            \
      MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL    ) /**< All PSK-based TLS 1.3 key exchanges */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ALL               \
    ( MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL        |            \
      MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL    ) /**< All ephemeral TLS 1.3 key exchanges */

/* Signaling ciphersuite values (SCSV) */
#define MBEDTLS_SSL_EMPTY_RENEGOTIATION_INFO                   0xFF      /**< renegotiation info ext */

/* TLS 1.3 signature algorithms, RFC 8446, Section 4.2.2, NOT supported in LT */
#define MBEDTLS_TLS1_3_SIG_NONE                                0x0
/* RSASSA-PKCS1-v1_5 algorithms */
#define MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256                    0x0401
#define MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA384                    0x0501
#define MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA512                    0x0601
/* ECDSA algorithms */
#define MBEDTLS_TLS1_3_SIG_ECDSA_SECP384R1_SHA384              0x0503
#define MBEDTLS_TLS1_3_SIG_ECDSA_SECP521R1_SHA512              0x0603
/* RSASSA-PSS algorithms with public key OID rsaEncryption */
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA256                 0x0804
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA384                 0x0805
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA512                 0x0806
/* EdDSA algorithms */
#define LTTLS_SIG_ED448                                        0x0808
/* RSASSA-PSS algorithms with public key OID RSASSA-PSS  */
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_PSS_SHA256                  0x0809
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_PSS_SHA384                  0x080A
#define MBEDTLS_TLS1_3_SIG_RSA_PSS_PSS_SHA512                  0x080B

/* Supported ciphersuites (Official IANA names) */
#define MBEDTLS_TLS_RSA_WITH_NULL_MD5                          0x01   /**< Weak! */
#define MBEDTLS_TLS_RSA_WITH_NULL_SHA                          0x02   /**< Weak! */
#define MBEDTLS_TLS_PSK_WITH_NULL_SHA                          0x2C   /**< Weak! */
#define MBEDTLS_TLS_DHE_PSK_WITH_NULL_SHA                      0x2D   /**< Weak! */
#define MBEDTLS_TLS_RSA_PSK_WITH_NULL_SHA                      0x2E   /**< Weak! */
#define MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA                   0x2F
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA               0x33
#define MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA                   0x35
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CBC_SHA               0x39
#define MBEDTLS_TLS_RSA_WITH_NULL_SHA256                       0x3B   /**< Weak! */
#define MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256                0x3C   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256                0x3D   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_128_CBC_SHA              0x41
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA          0x45
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA256            0x67   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CBC_SHA256            0x6B   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_256_CBC_SHA              0x84
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA          0x88
#define MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA                   0x8C
#define MBEDTLS_TLS_PSK_WITH_AES_256_CBC_SHA                   0x8D
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CBC_SHA               0x90
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CBC_SHA               0x91
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_128_CBC_SHA               0x94
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_256_CBC_SHA               0x95
#define MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256                0x9C   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384                0x9D   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_128_GCM_SHA256            0x9E   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384            0x9F   /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256                0xA8   /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_256_GCM_SHA384                0xA9   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_128_GCM_SHA256            0xAA   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_256_GCM_SHA384            0xAB   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_128_GCM_SHA256            0xAC   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_256_GCM_SHA384            0xAD   /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA256                0xAE
#define MBEDTLS_TLS_PSK_WITH_AES_256_CBC_SHA384                0xAF
#define MBEDTLS_TLS_PSK_WITH_NULL_SHA256                       0xB0   /**< Weak! */
#define MBEDTLS_TLS_PSK_WITH_NULL_SHA384                       0xB1   /**< Weak! */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CBC_SHA256            0xB2
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CBC_SHA384            0xB3
#define MBEDTLS_TLS_DHE_PSK_WITH_NULL_SHA256                   0xB4   /**< Weak! */
#define MBEDTLS_TLS_DHE_PSK_WITH_NULL_SHA384                   0xB5   /**< Weak! */
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_128_CBC_SHA256            0xB6
#define MBEDTLS_TLS_RSA_PSK_WITH_AES_256_CBC_SHA384            0xB7
#define MBEDTLS_TLS_RSA_PSK_WITH_NULL_SHA256                   0xB8   /**< Weak! */
#define MBEDTLS_TLS_RSA_PSK_WITH_NULL_SHA384                   0xB9   /**< Weak! */
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_128_CBC_SHA256           0xBA   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA256       0xBE   /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_256_CBC_SHA256           0xC0   /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA256       0xC4   /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_NULL_SHA                   0xC001 /**< Weak! */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA            0xC004
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA            0xC005
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_NULL_SHA                  0xC006 /**< Weak! */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA           0xC009
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA           0xC00A
#define MBEDTLS_TLS_ECDH_RSA_WITH_NULL_SHA                     0xC00B /**< Weak! */
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA              0xC00E
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA              0xC00F
#define MBEDTLS_TLS_ECDHE_RSA_WITH_NULL_SHA                    0xC010 /**< Weak! */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA             0xC013
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA             0xC014
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256        0xC023 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384        0xC024 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256         0xC025 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384         0xC026 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256          0xC027 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384          0xC028 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256           0xC029 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384           0xC02A /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256        0xC02B /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384        0xC02C /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256         0xC02D /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384         0xC02E /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256          0xC02F /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384          0xC030 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256           0xC031 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384           0xC032 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA             0xC035
#define MBEDTLS_TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA             0xC036
#define MBEDTLS_TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256          0xC037
#define MBEDTLS_TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA384          0xC038
#define MBEDTLS_TLS_ECDHE_PSK_WITH_NULL_SHA                    0xC039
#define MBEDTLS_TLS_ECDHE_PSK_WITH_NULL_SHA256                 0xC03A
#define MBEDTLS_TLS_ECDHE_PSK_WITH_NULL_SHA384                 0xC03B
#define MBEDTLS_TLS_RSA_WITH_ARIA_128_CBC_SHA256               0xC03C /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_ARIA_256_CBC_SHA384               0xC03D /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_ARIA_128_CBC_SHA256           0xC044 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_ARIA_256_CBC_SHA384           0xC045 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_ARIA_128_CBC_SHA256       0xC048 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_ARIA_256_CBC_SHA384       0xC049 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_ARIA_128_CBC_SHA256        0xC04A /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_ARIA_256_CBC_SHA384        0xC04B /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_ARIA_128_CBC_SHA256         0xC04C /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_ARIA_256_CBC_SHA384         0xC04D /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_ARIA_128_CBC_SHA256          0xC04E /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_ARIA_256_CBC_SHA384          0xC04F /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_ARIA_128_GCM_SHA256               0xC050 /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_ARIA_256_GCM_SHA384               0xC051 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_ARIA_128_GCM_SHA256           0xC052 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_ARIA_256_GCM_SHA384           0xC053 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_ARIA_128_GCM_SHA256       0xC05C /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_ARIA_256_GCM_SHA384       0xC05D /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_ARIA_128_GCM_SHA256        0xC05E /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_ARIA_256_GCM_SHA384        0xC05F /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_ARIA_128_GCM_SHA256         0xC060 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_ARIA_256_GCM_SHA384         0xC061 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_ARIA_128_GCM_SHA256          0xC062 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_ARIA_256_GCM_SHA384          0xC063 /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_ARIA_128_CBC_SHA256               0xC064 /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_ARIA_256_CBC_SHA384               0xC065 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_ARIA_128_CBC_SHA256           0xC066 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_ARIA_256_CBC_SHA384           0xC067 /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_ARIA_128_CBC_SHA256           0xC068 /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_ARIA_256_CBC_SHA384           0xC069 /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_ARIA_128_GCM_SHA256               0xC06A /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_ARIA_256_GCM_SHA384               0xC06B /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_ARIA_128_GCM_SHA256           0xC06C /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_ARIA_256_GCM_SHA384           0xC06D /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_ARIA_128_GCM_SHA256           0xC06E /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_ARIA_256_GCM_SHA384           0xC06F /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_PSK_WITH_ARIA_128_CBC_SHA256         0xC070 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_PSK_WITH_ARIA_256_CBC_SHA384         0xC071 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CAMELLIA_128_CBC_SHA256   0xC072
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CAMELLIA_256_CBC_SHA384   0xC073
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_CAMELLIA_128_CBC_SHA256    0xC074
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_CAMELLIA_256_CBC_SHA384    0xC075
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CAMELLIA_128_CBC_SHA256     0xC076
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CAMELLIA_256_CBC_SHA384     0xC077
#define MBEDTLS_TLS_ECDH_RSA_WITH_CAMELLIA_128_CBC_SHA256      0xC078
#define MBEDTLS_TLS_ECDH_RSA_WITH_CAMELLIA_256_CBC_SHA384      0xC079
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_128_GCM_SHA256           0xC07A /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_CAMELLIA_256_GCM_SHA384           0xC07B /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_128_GCM_SHA256       0xC07C /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_GCM_SHA384       0xC07D /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CAMELLIA_128_GCM_SHA256   0xC086 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CAMELLIA_256_GCM_SHA384   0xC087 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_CAMELLIA_128_GCM_SHA256    0xC088 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_ECDSA_WITH_CAMELLIA_256_GCM_SHA384    0xC089 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CAMELLIA_128_GCM_SHA256     0xC08A /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CAMELLIA_256_GCM_SHA384     0xC08B /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_CAMELLIA_128_GCM_SHA256      0xC08C /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDH_RSA_WITH_CAMELLIA_256_GCM_SHA384      0xC08D /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_CAMELLIA_128_GCM_SHA256           0xC08E /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_CAMELLIA_256_GCM_SHA384           0xC08F /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_CAMELLIA_128_GCM_SHA256       0xC090 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_CAMELLIA_256_GCM_SHA384       0xC091 /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_CAMELLIA_128_GCM_SHA256       0xC092 /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_CAMELLIA_256_GCM_SHA384       0xC093 /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_CAMELLIA_128_CBC_SHA256           0xC094
#define MBEDTLS_TLS_PSK_WITH_CAMELLIA_256_CBC_SHA384           0xC095
#define MBEDTLS_TLS_DHE_PSK_WITH_CAMELLIA_128_CBC_SHA256       0xC096
#define MBEDTLS_TLS_DHE_PSK_WITH_CAMELLIA_256_CBC_SHA384       0xC097
#define MBEDTLS_TLS_RSA_PSK_WITH_CAMELLIA_128_CBC_SHA256       0xC098
#define MBEDTLS_TLS_RSA_PSK_WITH_CAMELLIA_256_CBC_SHA384       0xC099
#define MBEDTLS_TLS_ECDHE_PSK_WITH_CAMELLIA_128_CBC_SHA256     0xC09A
#define MBEDTLS_TLS_ECDHE_PSK_WITH_CAMELLIA_256_CBC_SHA384     0xC09B
#define MBEDTLS_TLS_RSA_WITH_AES_128_CCM                       0xC09C  /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_AES_256_CCM                       0xC09D  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM                   0xC09E  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CCM                   0xC09F  /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_AES_128_CCM_8                     0xC0A0  /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_WITH_AES_256_CCM_8                     0xC0A1  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM_8                 0xC0A2  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CCM_8                 0xC0A3  /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_128_CCM                       0xC0A4  /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_256_CCM                       0xC0A5  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CCM                   0xC0A6  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CCM                   0xC0A7  /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8                     0xC0A8  /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_AES_256_CCM_8                     0xC0A9  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CCM_8                 0xC0AA  /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CCM_8                 0xC0AB  /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM               0xC0AC  /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM               0xC0AD  /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8             0xC0AE  /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8             0xC0AF  /**< TLS 1.2 */
#define MBEDTLS_TLS_ECJPAKE_WITH_AES_128_CCM_8                 0xC0FF  /**< experimental */

/* RFC 7905 */
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256    0xCCA8 /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256  0xCCA9 /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256      0xCCAA /**< TLS 1.2 */
#define MBEDTLS_TLS_PSK_WITH_CHACHA20_POLY1305_SHA256          0xCCAB /**< TLS 1.2 */
#define MBEDTLS_TLS_ECDHE_PSK_WITH_CHACHA20_POLY1305_SHA256    0xCCAC /**< TLS 1.2 */
#define MBEDTLS_TLS_DHE_PSK_WITH_CHACHA20_POLY1305_SHA256      0xCCAD /**< TLS 1.2 */
#define MBEDTLS_TLS_RSA_PSK_WITH_CHACHA20_POLY1305_SHA256      0xCCAE /**< TLS 1.2 */

/* RFC 8446, Appendix B.4 */
#define MBEDTLS_TLS1_3_AES_128_GCM_SHA256                      0x1301 /**< TLS 1.3 */
#define MBEDTLS_TLS1_3_AES_256_GCM_SHA384                      0x1302 /**< TLS 1.3 */
#define MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256                0x1303 /**< TLS 1.3 */
#define MBEDTLS_TLS1_3_AES_128_CCM_SHA256                      0x1304 /**< TLS 1.3 */
#define MBEDTLS_TLS1_3_AES_128_CCM_8_SHA256                    0x1305 /**< TLS 1.3 */

LT_EXTERN_C_END
#endif  // ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLSDEFS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  21-May-22   gallienus   created
 */
