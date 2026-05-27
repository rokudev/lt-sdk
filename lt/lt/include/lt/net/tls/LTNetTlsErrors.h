/*******************************************************************************
 * include/lt/net/tls/LTNetTlsErrors.h
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
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLSERRORS_H
#define ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLSERRORS_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/* Error id is assigned to each function, positive.
 * Error code is Mbed TLS error code, negative. 
 *
 *  0                   1                   2                   3  
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |-|       Error ID      |?| Cnt |   Error Code    |    Extra    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * Bit 12 is shared by Error ID and Cnt.
 * Bits 25 - 31 are used for crypto error code, or oid/asn1 error code, or count.
 */
// enable or disable LTTLS error encoding
#if 1
#define LTTLS_ERROR(errId, errCode)    ((-((errId) << 16)) + (errCode))
#else
#define LTTLS_ERROR(errId, errCode)    (errCode)
#endif

/* All error codes are used internally in TLS */
#define MBEDTLS_ERR_ERROR_GENERIC_ERROR                -0x0010    /**< Generic error */
#define MBEDTLS_ERR_ERROR_NO_DATA                      -0x0018    /**< No data error */
#define MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED          -0x0020    /**< This is a bug in the library */

/* OID Error codes. These error codes are OR'ed to X509 error codes for higher error granularity. */
#define MBEDTLS_ERR_OID_NOT_FOUND                      -0x0030    /** OID is not found. */
#define MBEDTLS_ERR_OID_BUF_TOO_SMALL                  -0x0038    /** output buffer is too small */

/* ASN1 Error codes. These error codes are OR'ed to X509 error codes for higher error granularity. */
// These errors may occur at multiple places in a function.
#define MBEDTLS_ERR_ASN1_OUT_OF_DATA                   -0x0040    /**< ** Out of data when parsing an ASN1 data structure. */
#define MBEDTLS_ERR_ASN1_INVALID_LENGTH                -0x0048    /**< ** Error when trying to determine the length or invalid length. */
#define MBEDTLS_ERR_ASN1_INVALID_DATA                  -0x0050    /**< ** Data is invalid. */
#define MBEDTLS_ERR_ASN1_LENGTH_MISMATCH               -0x0058    /**< Actual length differs from expected length. */
// These errors only occur once in a function.
#define MBEDTLS_ERR_ASN1_UNEXPECTED_TAG                -0x0060    /**< ASN1 tag was of an unexpected value. */
#define MBEDTLS_ERR_ASN1_ALLOC_FAILED                  -0x0062    /**< Memory allocation failed */
#define MBEDTLS_ERR_ASN1_BUF_TOO_SMALL                 -0x0063    /**< Buffer too small when writing ASN.1 data structure. */

/* X509 error codes */
#define MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE           -0x2080    /**< Unavailable feature, e.g. RSA hashing/encryption combination. */
#define MBEDTLS_ERR_X509_UNKNOWN_OID                   -0x2100    /**< Requested OID is unknown. */
#define MBEDTLS_ERR_X509_INVALID_FORMAT                -0x2180    /**< The CRT/CRL/CSR format is invalid, e.g. different type expected. */
#define MBEDTLS_ERR_X509_INVALID_VERSION               -0x2200    /**< The CRT/CRL/CSR version element is invalid. */
#define MBEDTLS_ERR_X509_INVALID_SERIAL                -0x2280    /**< The serial tag or value is invalid. */
#define MBEDTLS_ERR_X509_INVALID_ALG                   -0x2300    /**< The algorithm tag or value is invalid. */
#define MBEDTLS_ERR_X509_INVALID_NAME                  -0x2380    /**< ** The name tag or value is invalid. */
#define MBEDTLS_ERR_X509_INVALID_DATE                  -0x2400    /**< ** The date tag or value is invalid. */
#define MBEDTLS_ERR_X509_INVALID_SIGNATURE             -0x2480    /**< The signature tag or value invalid. */
#define MBEDTLS_ERR_X509_INVALID_EXTENSIONS            -0x2500    /**< ** The extension tag or value is invalid. */
#define MBEDTLS_ERR_X509_UNKNOWN_VERSION               -0x2580    /**< CRT/CRL/CSR has an unsupported version number. */
#define MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG               -0x2600    /**< Signature algorithm (oid) is unsupported. */
#define MBEDTLS_ERR_X509_SIG_MISMATCH                  -0x2680    /**< Signature algorithms do not match. */
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED            -0x2700    /**< Certificate verification failed, e.g. CRL, CA or signature check failed. */
#define MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT           -0x2780    /**< Format not recognized as DER or PEM. */
#define MBEDTLS_ERR_X509_BAD_INPUT_DATA                -0x2800    /**< Input invalid. */
#define MBEDTLS_ERR_X509_ALLOC_FAILED                  -0x2880    /**< ** Allocation of memory failed. */
#define MBEDTLS_ERR_X509_FILE_IO_ERROR                 -0x2900    /**< Read/write of file failed. */
#define MBEDTLS_ERR_X509_BUFFER_TOO_SMALL              -0x2980    /**< Destination buffer is too small. */
#define MBEDTLS_ERR_X509_FATAL_ERROR                   -0x3000    /**< A fatal error occurred, eg the chain is too long or the vrfy callback failed. */

/* Public Key error codes */
#define MBEDTLS_ERR_PK_BUFFER_TOO_SMALL                -0x3880    /**< The output buffer is too small. */
#define MBEDTLS_ERR_PK_SIG_LEN_MISMATCH                -0x3900    /**< The buffer contains a valid signature followed by more data. */
#define MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE             -0x3980    /**< Unavailable feature, e.g. RSA disabled for RSA key. */
#define MBEDTLS_ERR_PK_UNKNOWN_NAMED_CURVE             -0x3A00    /**< Elliptic curve is unsupported (only NIST curves are supported). */
#define MBEDTLS_ERR_PK_INVALID_ALG                     -0x3A80    /**< ** The algorithm tag or value is invalid. */
#define MBEDTLS_ERR_PK_INVALID_PUBKEY                  -0x3B00    /**< The pubkey tag or value is invalid (only RSA and EC are supported). */
#define MBEDTLS_ERR_PK_PASSWORD_MISMATCH               -0x3B80    /**< Given private key password does not allow for correct decryption. */
#define MBEDTLS_ERR_PK_PASSWORD_REQUIRED               -0x3C00    /**< Private key password can't be empty. */
#define MBEDTLS_ERR_PK_UNKNOWN_PK_ALG                  -0x3C80    /**< Key algorithm is unsupported (only RSA and EC are supported). */
#define MBEDTLS_ERR_PK_KEY_INVALID_FORMAT              -0x3D00    /**< Invalid key tag or value. */
#define MBEDTLS_ERR_PK_KEY_INVALID_VERSION             -0x3D80    /**< Unsupported key version */
#define MBEDTLS_ERR_PK_FILE_IO_ERROR                   -0x3E00    /**< Read/write of file failed. */
#define MBEDTLS_ERR_PK_BAD_INPUT_DATA                  -0x3E80    /**< Bad input parameters to function. */
#define MBEDTLS_ERR_PK_TYPE_MISMATCH                   -0x3F00    /**< Type mismatch, eg attempt to encrypt with an ECDSA key */
#define MBEDTLS_ERR_PK_ALLOC_FAILED                    -0x3F80    /**< Memory allocation failed. */

/* TLS error codes */
#define MBEDTLS_ERR_SSL_BAD_CONFIG                     -0x5E80    /**< Invalid value in SSL config */
#define MBEDTLS_ERR_SSL_VERSION_MISMATCH               -0x5F00    /**< An operation failed due to an unexpected version or configuration. */
#define MBEDTLS_ERR_SSL_UNEXPECTED_CID                 -0x6000    /**< An encrypted DTLS-frame with an unexpected CID was received. */
#define MBEDTLS_ERR_SSL_EARLY_MESSAGE                  -0x6480    /**< Internal-only message signaling that a message arrived early. */
#define MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS              -0x6500    /**< The asynchronous operation is not completed yet. */
#define MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER              -0x6600    /**< ** A field in a message was incorrect or inconsistent with other fields. */
#define MBEDTLS_ERR_SSL_NON_FATAL                      -0x6680    /**< The alert message received indicates a non-fatal error. */
#define MBEDTLS_ERR_SSL_UNEXPECTED_RECORD              -0x6700    /**< ** Record header looks valid but is not expected. */
#define MBEDTLS_ERR_SSL_CLIENT_RECONNECT               -0x6780    /**< The client initiated a reconnect from the same port. */
#define MBEDTLS_ERR_SSL_TIMEOUT                        -0x6800    /**< The operation timed out. */
#define MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL               -0x6A00    /**< ** A buffer is too small to receive or write a message */
#define MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED          -0x6A80    /**< DTLS client must retry for hello verification */
#define MBEDTLS_ERR_SSL_WAITING_SERVER_HELLO_RENEGO    -0x6B00    /**< Unexpected message at ServerHello in renegotiation. */
#define MBEDTLS_ERR_SSL_COUNTER_WRAPPING               -0x6B80    /**< A counter would wrap (eg, too many messages exchanged). */
#define MBEDTLS_ERR_SSL_INTERNAL_ERROR                 -0x6C00    /**< ** Internal error (eg, unexpected failure in lower-level module) */
#define MBEDTLS_ERR_SSL_UNKNOWN_IDENTITY               -0x6C80    /**< Unknown identity received (eg, PSK identity) */
#define MBEDTLS_ERR_SSL_PK_TYPE_MISMATCH               -0x6D00    /**< Public key type mismatch (eg, asked for RSA key exchange and presented EC key) */
#define MBEDTLS_ERR_SSL_SESSION_TICKET_EXPIRED         -0x6D80    /**< Session ticket has expired. */
#define MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE              -0x6E00    /**< ** The handshake negotiation failed. */
#define MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION           -0x6E80    /**< Handshake protocol not within min/max boundaries */
#define MBEDTLS_ERR_SSL_HW_ACCEL_FALLTHROUGH           -0x6F80    /**< Hardware acceleration function skipped / left alone data */
#define MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS             -0x7000    /**< A cryptographic operation is in progress. Try again later. */
#define MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE            -0x7080    /**< The requested feature is not available. */
#define MBEDTLS_ERR_SSL_BAD_INPUT_DATA                 -0x7100    /**< Bad input parameters to function. */
#define MBEDTLS_ERR_SSL_INVALID_MAC                    -0x7180    /**< Verification of the message MAC failed. */
#define MBEDTLS_ERR_SSL_INVALID_RECORD                 -0x7200    /**< An invalid SSL record was received. */
#define MBEDTLS_ERR_SSL_CONN_EOF                       -0x7280    /**< The connection indicated an EOF. */
#define MBEDTLS_ERR_SSL_DECODE_ERROR                   -0x7300    /**< ** A message could not be parsed due to a syntactic error. */
#define MBEDTLS_ERR_SSL_DECRYPT_ERROR                  -0x7380    /**< LTTLS, A message cannot be decrypted */
#define MBEDTLS_ERR_SSL_NO_RNG                         -0x7400    /**< No RNG was provided to the SSL module. */
#define MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE          -0x7480    /**< No client certification received from the client, but required by the authentication mode. */
#define MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION          -0x7500    /**< Client received an extended server hello containing an unsupported extension */
#define MBEDTLS_ERR_SSL_NO_APPLICATION_PROTOCOL        -0x7580    /**< No ALPN protocols supported that the client advertises */
#define MBEDTLS_ERR_SSL_PRIVATE_KEY_REQUIRED           -0x7600    /**< The own private key or pre-shared key is not set, but needed. */
#define MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED              -0x7680    /**< No CA Chain is set, but required to operate. */
#define MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE             -0x7700    /**< An unexpected message was received from our peer. */
#define MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE            -0x7780    /**< A fatal alert message was received from our peer. */
#define MBEDTLS_ERR_SSL_UNRECOGNIZED_NAME              -0x7800    /**< No server could be identified matching the client's SNI. */
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY              -0x7880    /**< The peer notified us that the connection is going to be closed. */
#define MBEDTLS_ERR_SSL_BAD_CERTIFICATE                -0x7A00    /**< Processing of the Certificate handshake message failed. */
#define MBEDTLS_ERR_SSL_UNSUPPORTED_CERTIFICATE        -0x7A80    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_CERTIFICATE_REVOKED            -0x7B00    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_CERTIFICATE_EXPIRED            -0x7B80    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_CERTIFICATE_UNKNOWN            -0x7C00    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_UNKNOWN_CA                     -0x7C80    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_ACCESS_DENIED                  -0x7D00    /**< LTTLS, Unsupported certificate */
#define MBEDTLS_ERR_SSL_INVALID_SOCKET                 -0x7D80    /**< LTTLS, socket down */
#define MBEDTLS_ERR_SSL_ALLOC_FAILED                   -0x7F00    /**< ** Memory allocation failed */
#define MBEDTLS_ERR_SSL_HW_ACCEL_FAILED                -0x7F80    /**< Hardware acceleration function returned with error */

#define LTTLS_CHK_BUF_PTR(errId, cur, end, need)                    if (cur > end || ((LT_SIZE)need > (LT_SIZE)end - (LT_SIZE)cur)) return LTTLS_ERROR(errId, MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL)
#define LTTLS_CHK_BUF_PTR_BREAK(errId, cur, end, need)              if (cur > end || ((LT_SIZE)need > (LT_SIZE)end - (LT_SIZE)cur)) { ret = LTTLS_ERROR(errId, MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL); break; }
#define LTTLS_CHK_BUF_READ_PTR(errId, cur, end, need)               if (cur > end || ((LT_SIZE)need > (LT_SIZE)end - (LT_SIZE)cur)) return LTTLS_ERROR(errId, MBEDTLS_ERR_SSL_DECODE_ERROR)
#define LTTLS_CHK_BUF_READ_PTR_GOTO(errId, cur, end, need, finish)  if (cur > end || ((LT_SIZE)need > (LT_SIZE)end - (LT_SIZE)cur)) { ret = LTTLS_ERROR(errId, MBEDTLS_ERR_SSL_DECODE_ERROR); goto finish; }
#define LTTLS_CHK_BUF_READ_PTR_BREAK(errId, cur, end, need)         if (cur > end || ((LT_SIZE)need > (LT_SIZE)end - (LT_SIZE)cur)) { ret = LTTLS_ERROR(errId, MBEDTLS_ERR_SSL_DECODE_ERROR); break; }

LT_EXTERN_C_END
#endif  // ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLSERRORS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Sep-23   gallienus   created
 */
