/*******************************************************************************
 * source/lt/net/tls/LTNetTls13.h
 *
 * For TLS 1.3 only
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
 * The modified source files are ssl.h, and ssl_tls13_keys.h in Mbed TLS.
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

#ifndef ROKU_LT_SOURCE_LT_NET_TLS_LTNETTLS13_H
#define ROKU_LT_SOURCE_LT_NET_TLS_LTNETTLS13_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include <lt/core/LTTime.h>
#include <lt/core/LTThread.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/tls/LTNetTls.h>
#include <lt/net/x509/LTNetX509.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include "LTNetTlsDefs.h"

#define LTTLS_TIMEOUT          15000     /**< TLS network timeout, in millisecond, default for most applications */

/* Maximum fragment length in bytes, determines the size of each of the two internal I/O buffers.
 * We use the fragment size extension to ask server to limit fragment size to 2048 Bytes.
 * Do not change these macros unless TLS doesn't work in device or with the server. */
#define LTTLS_IN_RECORD_MAXLEN   2048    /**< The max inner plaintext length of input record.
                                              If chhange this macro, must change the max fragment extension in Client Hello */
#define LTTLS_IN_BUFFER_LEN      2072    /**< We use the max_fragment_length extension for max 2048 Bytes record plaintext payload.
                                              The buffer needs additional 5 + 17 bytes to hold record head + AEAD extra. */
#define LTTLS_OUT_RECORD_MAXLEN  2048    /**< The max inner plaintext length of output record.
                                              Large data will be split to fit this max limit and sent over multiple records. */
#define LTTLS_OUT_BUFFER_LEN     2072    /**< We need to make sure all outgoing record size (head and payload and AEAD extra) smaller than 2048. */

typedef struct LTTLSNonce {
    u32 bytes[3];
} LTTLSNonce;

/* Size defines */
#define LTTLS_PSK_MAX_LEN                       32    // 256 bits
#define LTTLS_PREMASTER_SIZE                    32
#define LTTLS_RECORD_HEADER_LEN                  5    // sizeof TLS 1.3 Record Header
#define LTTLS_HANDSHAKE_HEADER_LEN               4    // sizeof TLS 1.3 Handshake Header
#define LTTLS_EXTENSION_HEADER_LEN               4    // sizeof TLS 1.3 Extension Header
#define LTTLS_CLIENT_HELLO_RANDOM_LEN           32
#define LTTLS_SERVER_HELLO_RANDOM_LEN           32
#define LTTLS_PADDING                          0xF    // padding must be 2^x - 1, 0 means no padding

/* Mask of TLS 1.3 handshake extensions used in extensions_present of LTTLS_Handshake */
#define LTTLS_EXT_NONE                        ( 0       )
#define LTTLS_EXT_SERVERNAME                  ( 1 <<  0 )
#define LTTLS_EXT_MAX_FRAGMENT_LENGTH         ( 1 <<  1 )
#define LTTLS_EXT_STATUS_REQUEST              ( 1 <<  2 )
#define LTTLS_EXT_SUPPORTED_GROUPS            ( 1 <<  3 )
#define LTTLS_EXT_SIG_ALG                     ( 1 <<  4 )
#define LTTLS_EXT_USE_SRTP                    ( 1 <<  5 )
#define LTTLS_EXT_HEARTBEAT                   ( 1 <<  6 )
#define LTTLS_EXT_ALPN                        ( 1 <<  7 )
#define LTTLS_EXT_SCT                         ( 1 <<  8 )
#define LTTLS_EXT_CLI_CERT_TYPE               ( 1 <<  9 )
#define LTTLS_EXT_SERV_CERT_TYPE              ( 1 << 10 )
#define LTTLS_EXT_PADDING                     ( 1 << 11 )
#define LTTLS_EXT_PRE_SHARED_KEY              ( 1 << 12 )
#define LTTLS_EXT_EARLY_DATA                  ( 1 << 13 )
#define LTTLS_EXT_SUPPORTED_VERSIONS          ( 1 << 14 )
#define LTTLS_EXT_COOKIE                      ( 1 << 15 )
#define LTTLS_EXT_PSK_KEY_EXCHANGE_MODES      ( 1 << 16 )
#define LTTLS_EXT_CERT_AUTH                   ( 1 << 17 )
#define LTTLS_EXT_OID_FILTERS                 ( 1 << 18 )
#define LTTLS_EXT_POST_HANDSHAKE_AUTH         ( 1 << 19 )
#define LTTLS_EXT_SIG_ALG_CERT                ( 1 << 20 )
#define LTTLS_EXT_KEY_SHARE                   ( 1 << 21 )
#define LTTLS_EXT_POINT_FORMAT                ( 1 << 22 )

/* This requires LTTLS_LABEL( idx, name, string ) to be defined at the point of use. */
#define LTTLS_LABEL_LIST                                             \
    LTTLS_LABEL( finished    , "finished"                          ) \
    LTTLS_LABEL( resumption  , "resumption"                        ) \
    LTTLS_LABEL( traffic_upd , "traffic upd"                       ) \
    LTTLS_LABEL( exporter    , "exporter"                          ) \
    LTTLS_LABEL( key         , "key"                               ) \
    LTTLS_LABEL( iv          , "iv"                                ) \
    LTTLS_LABEL( c_hs_traffic, "c hs traffic"                      ) \
    LTTLS_LABEL( c_ap_traffic, "c ap traffic"                      ) \
    LTTLS_LABEL( c_e_traffic , "c e traffic"                       ) \
    LTTLS_LABEL( s_hs_traffic, "s hs traffic"                      ) \
    LTTLS_LABEL( s_ap_traffic, "s ap traffic"                      ) \
    LTTLS_LABEL( s_e_traffic , "s e traffic"                       ) \
    LTTLS_LABEL( e_exp_master, "e exp master"                      ) \
    LTTLS_LABEL( res_master  , "res master"                        ) \
    LTTLS_LABEL( exp_master  , "exp master"                        ) \
    LTTLS_LABEL( ext_binder  , "ext binder"                        ) \
    LTTLS_LABEL( res_binder  , "res binder"                        ) \
    LTTLS_LABEL( derived     , "derived"                           ) \
    LTTLS_LABEL( client_cv   , "TLS 1.3, client CertificateVerify" ) \
    LTTLS_LABEL( server_cv   , "TLS 1.3, server CertificateVerify" )

#define LTTLS_LABEL( name, string )     const char name [ sizeof(string) - 1 ];
struct LTTLSLabelsStruct { LTTLS_LABEL_LIST };
#undef LTTLS_LABEL

#define LTTLS_LBL_LEN( LABEL )                  sizeof(tlsLabels.LABEL)
#define LTTLS_LBL_WITH_LEN( LABEL )             tlsLabels.LABEL, LTTLS_LBL_LEN( LABEL )

#define LTTLS_PSK_EXTERNAL                      0
#define LTTLS_PSK_RESUMPTION                    1

/* STATE HANDLING: CertificateRequest */
#define SSL_CERTIFICATE_REQUEST_EXPECT_REQUEST  0
#define SSL_CERTIFICATE_REQUEST_SKIP            1

/* The maximum length of HKDF contexts used in the TLS 1.3 standard.
 * Since contexts are always hashes of message transcripts, this can
 * be approximated from above by the maximum hash size. */
#define LTTLS_KEY_SCHEDULE_MAX_CONTEXT_LEN     32

/* Maximum desired length for expanded key material generated
 * by HKDF-Expand-Label.
 *
 * Warning: If this ever needs to be increased, the implementation
 * ssl_tls13_hkdf_encode_label() in ssl_tls13_keys.c needs to be
 * adjusted since it currently assumes that HKDF key expansion
 * is never used with more than 255 Bytes of output. */
#define LTTLS_KEY_SCHEDULE_MAX_EXPANSION_LEN  255

/* SSL state machine
 * Handshake states must be defined before APPLICATION
 * Post-handshake states must be defined after APPLICATION
 * We use their values to determine if state is before or at or after application.
 */
typedef enum {
    kLTTLSState_NONE                           = 0,  // not used
    // normal handshake
    kLTTLSState_HS_CLIENT_HELLO                = 1,
    kLTTLSState_HS_SERVER_HELLO                = 2,
    kLTTLSState_HS_SERVER_ENCRYPTED_EXTENSIONS = 3,
    kLTTLSState_HS_SERVER_CERTIFICATE_REQUEST  = 4,  // optional
    kLTTLSState_HS_SERVER_CERTIFICATE          = 5,
    kLTTLSState_HS_SERVER_CERTIFICATE_VERIFY   = 6,
    kLTTLSState_HS_SERVER_CHANGE_CIPHER_SPEC   = 7,
    kLTTLSState_HS_SERVER_FINISHED             = 8,
    kLTTLSState_HS_CLIENT_CERTIFICATE          = 9,  // optional
    kLTTLSState_HS_CLIENT_CERTIFICATE_VERIFY   = 10, // optional
    kLTTLSState_HS_CLIENT_FINISHED             = 11,
    kLTTLSState_HS_CLIENT_CHANGE_CIPHER_SPEC   = 12,
    kLTTLSState_HS_HANDSHAKE_WRAPUP            = 13,
    kLTTLSState_HS_HANDSHAKE_OVER              = 14,
    // session resume
    kLTTLSState_SR_CLIENT_HELLO                = 15,
    kLTTLSState_SR_SERVER_HELLO                = 16,
    kLTTLSState_SR_SERVER_ENCRYPTED_EXTENSIONS = 17,
    // application
    kLTTLSState_APPLICATION                    = 18,
} LTTlsState;

typedef struct LTTlsSessionTicket {
    LTTime                   receivedTime;          /**< the time when the ticket is received.
                                                          Clients MUST NOT attempt to use tickets which have ages greater than
                                                          the "ticket_lifetime" value which was provided with the ticket. */
    u32                      lifeTime;              /**< session ticket lifetime */
    u32                      ageAdd;                /**< session ticket added age */
    u8                       nonceLen;              /**< session ticket nonce length */
    u16                      ticketLen;             /**< session ticket length */
    u8                     * nonceTicket;           /**< buffer holding both nonce and ticket */
    // Extension, 0 or only EarlyDataIndication supported
    u16                      extensionLen;          /**< session ticket extension length */
    u32                      maxEarlyDataSize;      /**< max early data size */
    bool                     bValid;                /**< true if the ticket is valid to use. If the ticket is used, it is then invalid. */
} LTTlsSessionTicket;

/* This structure is used for storing current session data. */
typedef struct LTTlsSession {
    u32                      masterAppSecret[SHA256_HASH_LENGTH / 4];
    u32                      clientApplicationTrafficSecret[SHA256_HASH_LENGTH / 4];
    u32                      serverApplicationTrafficSecret[SHA256_HASH_LENGTH / 4];
    u32                      exporterMasterSecret[SHA256_HASH_LENGTH / 4];
    u32                      resumptionMasterSecret[SHA256_HASH_LENGTH / 4];
    u8                       clientSecretCount;
    u8                       serverSecretCount;

    u16                      idLen;                  /**< session id length  */
    u32                      id[LTTLS_CLIENT_HELLO_RANDOM_LEN / 4];   /**< session identifier */
    u16                      cipherSuite;            /**< chosen ciphersuite */
    LTTime                   start;                  /**< starting time      */

    LTPublicKey              peerPubKey;             /**< in heap, peer public key, obtained after parsing peer's cert chain */
    LTTlsSessionTicket       ticket;                 /**< in heap, session ticket */

    u8                     * inApHdr;                /**< always points to the current application header in parsing */
    u8                     * inApNextHdr;            /**< always points to the next application header not in parsing */
    u16                      inApLen;                /**< current application message length, including header and payload */

} LTTlsSession;

/* This structure contains the parameters only needed during handshake. */
typedef struct LTTlsHandshake {
    /* Serects and keys, need to be aligned for cryptos.
     * We use union to save some space because these secrets will
     * be updated at different handshake and session steps. */
    u32                      psk[SHA256_HASH_LENGTH / 4];
    union {
        struct {
            u32              earlySecret[SHA256_HASH_LENGTH / 4];
            u32              clientEarlyTrafficSecret [SHA256_HASH_LENGTH / 4];
            u32              earlyExporterMasterSecret[SHA256_HASH_LENGTH / 4];
        };
        struct {
            u32              handshakeSecret[SHA256_HASH_LENGTH / 4];
            u32              clientHandshakeTrafficSecret[SHA256_HASH_LENGTH / 4];
            u32              serverHandshakeTrafficSecret[SHA256_HASH_LENGTH / 4];
        };
    };
    u32                      ecdhePrivKey[ECDHE_KEY_LENGTH / 4];   /**< Ephemeral private key, need to be cleared after key exchange */
    union {
        u32                  ecdhePeerKey[ECDHE_KEY_LENGTH / 4];   /**< Peer's ephemeral public key */
        u32                  ecdheKey[ECDHE_KEY_LENGTH / 4];       /**< ECDHE key, ECDHE_X25519 */
    };

    LT_SHA256_CTX            *hashCtx;              /**< Hash context for sequential hash */
    u8                     * cookie;                /**< in heap, HelloRetryRequest cookie for TLS 1.3 */
    u16                      hrrCookieLen;          /**< HelloRetryRequest cookie length */
    u8                       kexModes;              /**< Key exchange modes for TLS 1.3 */
    u8                       helloRetryRequestCount;/**< Count of hello retry request, to prevent DoS attack */
    u16                      offeredGroupId;        /**< The NamedGroup value for ephemeral key exchange, GROUP_X25519 (0x001D) */
    u16                      cipherSuite;           /**< The cipher suite, TLS_AES_128_GCM_SHA256 (0x1301) */
    u16                      signature;             /**< The signature algorithm, Ed25519 (0x0807) */
    u8                       pskType;               /**< External PSK (0) or resumption PSK (1) */
    u16                      receivedSigAlgs[LTTLS_RECEIVED_SIG_ALGS_SIZE]; /**< Signature algorithm received in certificate request */
    u32                      extensionsPresent;     /**< Extension presence; Each bitfield represents an extension and defined as \c LTTLS_EXT_XXX */

    u8                     * inHsHdr;               /**< always points to the current handshake header in parsing */
    u8                     * inHsNextHdr;           /**< always points to the next handshake header not in parsing */
    u16                      inHsLen;               /**< current handshake message length, including header and payload */

} LTTlsHandshake;

/* Key and iv for encrypt and decrypt record, in either handshake or session */
typedef struct LTTlsTransform {
    u32                      clientWriteKey[AES128_KEY_LENGTH / 4];   /**< Key for client->server records. */
    u32                      clientWriteIV[AES128_GCM_IV_LENGTH / 4]; /**< IV  for client->server records. */
    u32                      serverWriteKey[AES128_KEY_LENGTH / 4];   /**< Key for server->client records. */
    u32                      serverWriteIV[AES128_GCM_IV_LENGTH / 4]; /**< IV  for server->client records. */
    u64                      outCounter;                              /**< outgoing record sequence number, client write, little endian */
    u64                      inCounter;                               /**< incoming record sequence number, server write, little endian */
} LTTlsTransform;

/* Configuration parameters to use by TLS */
typedef struct LTTlsConfig {
    LTSystemCrypto          *crypto;                /**< crypto library */
    LTNetX509               *x509;                  /**< x509 library */
    LTSocket                 hSocket;
} LTTlsConfig;

/* Application-Layer Protocol Negotiation Extension.
 * Note: The application that uses this extension must produce this extension
 *       according to RFC7301 https://www.rfc-editor.org/rfc/rfc7301.html
 *
 * Each protocol name is {nNameLen (1B), Name[nNameLen]}.
 * nAlpnLen (2B) = sum(1 + nNameLen)
 *
 * Example: for NTS,
 *     const u8           ntsAlpnName[] = {0x07,0x6E,0x74,0x73,0x6B,0x65,0x2F,0x31};   // "ntske/1"
 *     const LTTlsAlpnExt AlpnExt       = {8, ntsAlpnName};
 */
typedef struct LTTlsAlpnExt {
    u16  alpnLen;           ///< The length of the buffer pointed by pProtocolNames
    u8 * protocolNames;     ///< The buffer holding a list of protocol names.
} LTTlsAlpnExt;

/* All options are created by applications, and passed to socket spec.
 * TLS stack will copy the options upon socket creation.
 * Example:
 *     LTTlsOptions tlsOps;
 *     lt_memset(&tlsOps, 0, sizeof(LTTlsOptions))
 */
typedef struct LTTlsOptions {
    char         *serverName;            ///< Required, must match CN or alts for server certificate verification.
    LTPublicKey  *caKeys;                ///< Required, CA public keys
    u8            caKeyCount;            ///< Required, count of CA public keys
    u8           *clientCertificate;     ///< Optional, client certificate (whole chain).
    u16           clientCertificateLen;  ///< Optional, length of client certificate, in bytes.
    LTDriverCrypto_KeyReference clientPrivateKeyReference;    ///< Optional, client private key reference.
    LTTlsAlpnExt *alpnExt;               ///< Optional, must set to NULL if the application does not need this extension.
} LTTlsOptions;

/* TLS context */
typedef struct LTTlsContext {
    /* Generic */
    LTTlsState               eState;                /**< SSL handshake: current state */
    u8                       majorVer;              /**< equal to LTTLS_MAJOR_VERSION_3 */
    u8                       minorVer;              /**< equal to LTTLS_MINOR_VERSION_4 */
    u32                      randBytes[LTTLS_CLIENT_HELLO_RANDOM_LEN / 4];   /**< Client random bytes */
    u8                       inDataBuf[LTTLS_IN_BUFFER_LEN];                 /**< input data buffer (to read) */
    u8                       outDataBuf[LTTLS_OUT_BUFFER_LEN];               /**< output data buffer (to write) */
    LTTlsConfig             *config;                /**< config is init once, shall not reset during any other operations (such as reset).*/
    LTTlsOptions            *options;               /**< option is init once, shall not reset during any other operations (such as reset).*/

    struct {
        u8                   bKeepInMsg  : 1;       /**< 1 (true) to keep messages in buffer, used only for coordination, such as HRR and Cert Request. */
        u8                   bClientAuth : 1;       /**< 1 (true) if CertificateRequest is received from server. Could happen in either handshake or session.
                                                          If received, Certificate and CertificateVerify should be sent to server */
        u8                   bHrr        : 1;       /**< 1 (true) if a hello retry request is received */
        u8                   inZero      : 3;       /**< count of 0-length encrypted messages, used for detecting possible DoS attack */
    };

    u8                       nRecvAlertDescryption; /**< Fatal alert descryption received */

    /* Record layer (outgoing data) */
    u8                      *outBuf;                /**< output buffer, may hold multiple records */
    u8                      *outBufEnd;             /**< the end of output buffer */
    u16                      outBufLen;             /**< length of output buffer */
    u16                      outLeft;               /**< amount of data not yet written */
    u8                      *outHdr;                /**< a record header of the next header. The data before outHdr is pending for flush */
    u8                      *outMsg;                /**< a record payload, out_hdr + sizeof(record header) */
    u8                       outMsgType;            /**< record header: message type */
    u16                      outMsgLen;             /**< record header: message length */
    const u8                *resendData;            /**< point to data in outBuf to resend */
    u16                      resendLen;             /**< length of data to resend */

    /* Record layer (incoming data) */
    u8                      *inBuf;                 /**< input buffer, may hold multiple records */
    u8                      *inBufEnd;              /**< the end of input buffer, may hold multiple records */
    u16                      inBufLen;              /**< the length of received data in buffer, including record header and payload */
    u16                      inBufDataLeft;         /**< amount of data not processed so far in input buffer */
    u16                      toRead;                /**< bytes of record payload to read from socket in order to get a complete record in InBuf */

    u8                      *inRecHdr;              /**< always points to the current record header in parsing */
    u8                      *inRecPayload;          /**< always points to the current record payload in parsing */
    u8                      *inRecEnd;              /**< always points to the end of current record in parsing */
    u8                      *inRecNextHdr;          /**< always points to the current record header in parsing
                                                          pInRecEnd is not pInRecNextHdr because of padding in record. */
    u8                       inRecType;             /**< record type */
    u16                      inRecPayloadLen;       /**< record payload length, excluding record header */
    u16                      inRecDataLeft;         /**< amount of data not processed so far in record */

    LTTlsTransform          *transformHandshake;    /**< in heap, keys and ivs for handshake */
    LTTlsTransform          *transformApplication;  /**< in heap, keys and ivs for application */
    LTTlsTransform          *transformOut;          /**< point to the transform in use from client to server */
    LTTlsTransform          *transformIn;           /**< point to the transform in use from server to client */

    /* Session layer */
    LTTlsHandshake          *handshake;             /**< in heap, params required only during the handshake process */
    LTTlsSession            *sessionNegotiate;      /**< in heap, session data in handshake */
    LTTlsSession            *sessionData;           /**< in heap, session data in data */

    u8                      *certReqContext;        /**< in heap, The certificate request context */
    u8                       certReqContextLen;     /**< The length of certificate request context, in bytes */
} LTTlsContext;

/* TLS session context */
typedef struct LTTlsSessionContext {
    LTThread                 hThread;               /**< Thread handle of the session, shall not be destroyed by session.   \
                                                         But need to kill the handle's TLS timer when session is destroyed. */

    // config, options, and context for TLS
    LTTlsConfig              tlsConfig;
    LTTlsOptions             tlsOptions;
    LTTlsContext             tlsCtx;
    LTList                   tlsAppRxBuf;           /**< Decrypted TLS app data record is appended to this list. ReadSocket reads from this list */
} LTTlsSessionContext;

typedef struct LTTlsAppRxData {
    LTList_Node  node;    // A list of unencypted app data record
    u8 *data;             // data of one record
    u16 len;              // length of data
    u16 alreadyRead;      // the position to read
} LTTlsAppRxData;

/**
 * @brief  Initialize TLS context. This must be called before calling any TLS function.
 *
 * @param[in,out] tlsCtx   The TLS context
 * @param[in]     config   The TLS config. If NULL, keep current config.
 * @param[in]     options  The TLS options. If NULL, keep current options.
 * @param[in]     bNew     True if init session, False if resume session
 * @return    Error code
 *
 * When resuming a session, the session data shall be saved before calling InitTLS.
 * Refer to ResumeSession as an example
 */
int InitTls(LTTlsContext *tlsCtx, LTTlsConfig *config, LTTlsOptions *options, bool bNew);

/**
 * @brief Free TLS context and all enclosed resources. This is call when closing TLS.
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return    Error code
 */
void FreeTls(LTTlsContext *tlsCtx);

/**
 * @brief Write application data.
 *        For a large data block, write only one max TLS record, and return the size of one TLS record.
 *        Upper stack needs to write the rest of data in a later call on write-ready event.
 *
 * @param[in,out] tlsCtx   The TLS context
 * @param[in]     data  The data
 * @param[in]     len   The length of data
 * @return  The length of written data. Negative number means error code. Guarantee not to return 0.
 */
s32 WriteApplicationData(LTTlsContext *tlsCtx, const u8 *data, LT_SIZE len);

/**
 * @brief  Handshake handling function
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int Handshake(LTTlsContext *tlsCtx);

/**
 * @brief  Session handling function
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int Session(LTTlsContext *tlsCtx);

/**
 * @brief  Restart a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int RestartSession(LTTlsContext *tlsCtx);

/**
 * @brief  Resume a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int ResumeSession(LTTlsContext *tlsCtx);

/**
 * @brief  Update a session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return  Error code
 */
int UpdateSession(LTTlsContext *tlsCtx);

/**
 * @brief  Send a CloseNotify to close the session
 *
 * @param[in,out] tlsCtx  The TLS context
 * @return   Error code
 */
int CloseSession(LTTlsContext *tlsCtx);

/**
 * @brief Flush data in buffer to socket
 *
 * @param [in] hSocket  The handle of a net driver socket, not TLS socket
 * @param [in,out] buf  The pointer to the buffer of data to send
 * @param [in,out] len  The pointer to the length of data to send
 * @return  Error code
 *          On return, if data is partially flushed, buf and len point to the remaining bytes.
 */
int FlushBuffer(LTSocket hSocket, const u8 **buf, u16 *len);

LT_EXTERN_C_END
#endif  // ROKU_LT_SOURCE_LT_NET_TLS_LTNETTLS13_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  21-May-22   gallienus   created
 */
