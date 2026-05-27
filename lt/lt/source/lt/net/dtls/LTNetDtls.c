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

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include <lt/LT.h>
#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>

#include <lt/net/x509/LTNetX509.h>
#include <lt/net/dtls/LTNetDtls.h>
#include "LTNetDtls12.h"

#include "mbedtls/mbedtls_config.h"

#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/timing.h"
#include "mbedtls/pk_wrap.h"

DEFINE_LTLOG_SECTION("netdtls");
#if 1
#define P(...)
#else
#define P(...) LTLOG_DEBUG("dtls", __VA_ARGS__);
#endif

bool LTNetDtlsCrypto_Init(void);
void LTNetDtlsCrypto_Fini(void);

typedef enum {
    kSocketSpec_DtlsParamType_ClientCert,
    kSocketSpec_DtlsParamType_Unknown,
} SocketSpec_DtlsParamType;

typedef enum {
    kSocketSpecParser_Protocol,
    kSocketSpecParser_Key,
    kSocketSpecParser_Value,
    kSocketSpecParser_Error,
} SocketSpecParser_State;

typedef struct {
    SocketSpecParser_State   state;
    SocketSpec_DtlsParamType paramType;
    LTDtlsOptions           *options;
} SocketSpecParserContext;

// Private DTLS socket data
typedef struct DtlsSocketData {
    char                 *sessionId;
    LTDtlsSessionContext *sessionCtx;        ///< Extra data (pSessionCtx later)
    LTDtlsOptions         dtlsOptions;
    char                 *spec;
    LTSocket              hSocket;
    LTSocket              hTransportSocket;
    LTEvent               event;
    LTTime                timerIntermediate;
    LTTime                timerFinal;
    LTTime                connectionStartTime;
    LTTime                connectionDuration;
} DtlsSocketData;

static ILTSocket s_ILTSocket;

static LTCore             *s_Core      = NULL;
static LTUtilityTokenizer *s_Tokenizer = NULL;
static ILTThread          *s_Thread    = NULL;
static ILTEvent           *s_Event     = NULL;
static LTSystemCrypto     *s_Crypto    = NULL;

/** Helper Procs ******************************************************/
static const char *ConnectionStateToString(mbedtls_ssl_states state) {
    switch (state) {
        case MBEDTLS_SSL_HELLO_REQUEST:                        return "HelloRequest";
        case MBEDTLS_SSL_CLIENT_HELLO:                         return "ClientHello";
        case MBEDTLS_SSL_SERVER_HELLO:                         return "ServerHello";
        case MBEDTLS_SSL_SERVER_CERTIFICATE:                   return "ServerCert";
        case MBEDTLS_SSL_SERVER_KEY_EXCHANGE:                  return "ServerKeyExchange";
        case MBEDTLS_SSL_CERTIFICATE_REQUEST:                  return "CertRequest";
        case MBEDTLS_SSL_SERVER_HELLO_DONE:                    return "ServerHelloDone";
        case MBEDTLS_SSL_CLIENT_CERTIFICATE:                   return "ClientCert";
        case MBEDTLS_SSL_CLIENT_KEY_EXCHANGE:                  return "ClientKeyExchange";
        case MBEDTLS_SSL_CERTIFICATE_VERIFY:                   return "CertVerify";
        case MBEDTLS_SSL_CLIENT_CHANGE_CIPHER_SPEC:            return "ClientChangeCipherSpec";
        case MBEDTLS_SSL_CLIENT_FINISHED:                      return "ClientFinished";
        case MBEDTLS_SSL_SERVER_CHANGE_CIPHER_SPEC:            return "ServerChangeCipherSpec";
        case MBEDTLS_SSL_SERVER_FINISHED:                      return "ServerFinished";
        case MBEDTLS_SSL_FLUSH_BUFFERS:                        return "FlushBuffers";
        case MBEDTLS_SSL_HANDSHAKE_WRAPUP:                     return "HandshakeWrapup";
        case MBEDTLS_SSL_NEW_SESSION_TICKET:                   return "NewSessionTicket";
        case MBEDTLS_SSL_SERVER_HELLO_VERIFY_REQUEST_SENT:     return "ServerHelloVerifyRequestSent";
        case MBEDTLS_SSL_HELLO_RETRY_REQUEST:                  return "HelloRetryRequest";
        case MBEDTLS_SSL_ENCRYPTED_EXTENSIONS:                 return "EncryptedExtensions";
        case MBEDTLS_SSL_END_OF_EARLY_DATA:                    return "EndOfEarlyData";
        case MBEDTLS_SSL_CLIENT_CERTIFICATE_VERIFY:            return "ClientCertVerify";
        case MBEDTLS_SSL_CLIENT_CCS_AFTER_SERVER_FINISHED:     return "ClientCcsAfterServerFinished";
        case MBEDTLS_SSL_CLIENT_CCS_BEFORE_2ND_CLIENT_HELLO:   return "ClientCcsBefore2ndClientHello";
        case MBEDTLS_SSL_SERVER_CCS_AFTER_SERVER_HELLO:        return "ServerCcsAfterServerHello";
        case MBEDTLS_SSL_CLIENT_CCS_AFTER_CLIENT_HELLO:        return "ClientCcsAfterClientHello";
        case MBEDTLS_SSL_SERVER_CCS_AFTER_HELLO_RETRY_REQUEST: return "ServerCcsAfterHelloRetryRequest";
        case MBEDTLS_SSL_HANDSHAKE_OVER:                       return "HandshakeOver";
        case MBEDTLS_SSL_TLS1_3_NEW_SESSION_TICKET:            return "Tls13NewSessionTicket";
        case MBEDTLS_SSL_TLS1_3_NEW_SESSION_TICKET_FLUSH:      return "Tls13NewSessionTicketFlush";
        default:                                               return "Unknown";
    }
}

/** Event Dispatch Procs ******************************************************/

static const LTArgsDescriptor SocketEventArgs = {2, { kLTArgType_lthandle, kLTArgType_u32 }}; // socket, event

static void DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *clientData) {
    LT_UNUSED(event);
    if (!proc || !args) return;
    LTSocket hSocket = LTArgs_lthandleAt(0, args);
    u32 sockEvent    = LTArgs_u32At(1, args);
    (*(LTSocket_EventProc *)proc)(hSocket, sockEvent, clientData);
}

static void DispatchSocketEventImmediate(LTEvent hEvent, void *notifyImmediateEventStateClientData, void *eventProc, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTSocket hSocket = VOIDPTR_TO_LTHANDLE(notifyImmediateEventStateClientData);
    DtlsSocketData *sockData = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;

    if (sockData->sessionCtx->sslCtx.state == MBEDTLS_SSL_HANDSHAKE_OVER) {
        LTSocket_EventProc *callback = eventProc;
        callback(hSocket, kLTSocket_Event_Connected, eventProcClientData);
        callback(hSocket, kLTSocket_Event_WriteReady, eventProcClientData);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sockData);
}

static bool LoadCert(const char *certName, u8 **certData, u16 *certLen) {
    LTDriverCryptoCertManager *certManager = lt_createobject(LTDriverCryptoCertManager);
    if (!certManager) return false;
    bool bRet = false;
    do {
        certManager->API->GetCertificate(certName, NULL, certLen);
        if (*certLen == 0) break;
        *certData = lt_malloc(*certLen);
        if (!*certData) break;
        if (!certManager->API->GetCertificate(certName, *certData, certLen)) break;
        bRet = true;
    } while(0);
    lt_destroyobject(certManager);
    if (!bRet) {
        if (*certData) {
            lt_free(*certData);
            *certData = NULL;
        }
    }
    return bRet;
}

static bool LoadPublicKey(const char *keyName, LTPublicKey **key) {
    LTPublicKey *keyData = NULL;
    do {
        keyData = lt_malloc(sizeof(LTPublicKey));
        if (!keyData) break;
        LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
        bool bResult = keyManager && keyManager->API->GetPublicKey(keyName, keyData);
        lt_destroyobject(keyManager);
        if (!bResult) break;
        *key = keyData;
        return true;
    } while(0);
    if (keyData) lt_free(keyData);
    return false;
}

static bool LoadPrivateKey(const char *keyName, LTDriverCrypto_KeyReference *privateKeyReference) {
    LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
    if (!keyManager) return false;
    LTDriverCrypto_KeyReference *keyReference = keyManager->API->GetPrivateKeyReference(keyName);
    if (!keyReference) {
        lt_destroyobject(keyManager);
        return false;
    }
    lt_memcpy(privateKeyReference, keyReference, sizeof(LTDriverCrypto_KeyReference));
    keyManager->API->FreeKeyReference(keyReference);
    lt_destroyobject(keyManager);
    return true;
}

static bool OnSocketSpecToken(LTTokenizer *tokenizer, const char *token, char matchedSentinel, void *clientData) {
    SocketSpecParserContext *context = clientData;
    P("parse token %s", token);
    switch (context->state) {
        case kSocketSpecParser_Protocol:
            if (lt_strcmp(token, "dtls") == 0) {
                context->state = kSocketSpecParser_Key;
                s_Tokenizer->SetSentinels(tokenizer, ":");
            } else {
                context->state = kSocketSpecParser_Error;
                return false;
            }
            break;

        case kSocketSpecParser_Key:
            if (lt_strcmp(token, "clientcert") == 0) {
                context->paramType = kSocketSpec_DtlsParamType_ClientCert;
            } else {
                context->paramType = kSocketSpec_DtlsParamType_Unknown;
            }
            context->state = kSocketSpecParser_Value;
            s_Tokenizer->SetSentinels(tokenizer, " ,");
            break;

        case kSocketSpecParser_Value:
            if (token[0] != '\0') {
                bool bResult = false;
                switch (context->paramType) {
                    case kSocketSpec_DtlsParamType_ClientCert:
                        bResult = LoadCert(token, &context->options->clientCertificate, &context->options->clientCertificateLen) &&
                                  LoadPublicKey(token, &context->options->clientPublicKey) &&
                                  LoadPrivateKey(token, &context->options->clientPrivateKeyReference);
                        break;

                    case kSocketSpec_DtlsParamType_Unknown:
                        bResult = true;
                        break;

                    default:
                        bResult = false;
                        break;
                }
                if (!bResult) {
                    LTLOG_DEBUG("osst.err", "result failed");
                    context->state = kSocketSpecParser_Error;
                    return false;
                }
                if (matchedSentinel == ' ') {
                    context->state = kSocketSpecParser_Key;
                    s_Tokenizer->SetSentinels(tokenizer, ":");
                }
            }
            break;

        case kSocketSpecParser_Error:
        default:
            return false;
    }
    return true;
}

static bool ParseSocketSpec(const char *socketSpec, LTDtlsOptions *dtlsOptions) {
    lt_memset(dtlsOptions, 0, sizeof(*dtlsOptions));
    SocketSpecParserContext pcontext = {
        .state   = kSocketSpecParser_Protocol,
        .options = dtlsOptions,
    };
    LTTokenizer *pTokenizer = s_Tokenizer->CreateTokenizer(" ", OnSocketSpecToken, &pcontext);
    s_Tokenizer->ProcessData(pTokenizer, socketSpec, lt_strlen(socketSpec), false);
    s_Tokenizer->DestroyTokenizer(pTokenizer);

    if (pcontext.state == kSocketSpecParser_Error) {
        LTLOG_YELLOWALERT("sspec.prs.err", "Socket spec parsing failed");
        return false;
    }
    return true;
}

static void FreeOptions(LTDtlsOptions *pOptions) {
    if (pOptions->clientCertificate) {
        lt_free(pOptions->clientCertificate);
        pOptions->clientCertificate = NULL;
    }
    if (pOptions->clientPublicKey) {
        LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
        if (keyManager) keyManager->API->FreePublicKey(pOptions->clientPublicKey);
        lt_free(pOptions->clientPublicKey);
        pOptions->clientPublicKey = NULL;
    }
}

static void mbedtls_debug(void *ctx, int level, const char *file, int line, const char *str) {
    LT_UNUSED(ctx);
    LT_UNUSED(level);
    LTLOG_YELLOWALERT("mbedtls", "%s:%d: %s", file, line, str);
}

static void Handshake(DtlsSocketData *sockData) {
    LTDtlsSessionContext *sessionCtx = sockData->sessionCtx;
    int err = mbedtls_ssl_handshake(&sessionCtx->sslCtx);

    if (err != 0) {
        if ((err != MBEDTLS_ERR_SSL_INTERNAL_ERROR) && (err != MBEDTLS_ERR_SSL_WANT_READ)) {
            LTLOG_YELLOWALERT("hs.fail", " failed ! mbedtls_ssl_handshake returned -0x%x", (unsigned int) -err);
        }
    }
    else if (sessionCtx->sslCtx.state == MBEDTLS_SSL_HANDSHAKE_OVER) {
        // Event notify that DTLS connection is ready.
        sockData->connectionDuration = LTTime_Subtract(LT_GetCore()->GetKernelTime(), sockData->connectionStartTime);
        s_Event->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_Connected);
    }
}

/**
 * @brief  Callback in case of DTLS timeout
 *
 * @param data  The session context
 */
static void OnDtlsTimeout(void *data) {
    if (!data) return;
    LTLOG_DEBUG("timeout", "dtls timeout");
    DtlsSocketData *sockData = (DtlsSocketData *)data;
    s_Thread->KillTimer(s_Thread->GetCurrentThread(), OnDtlsTimeout, sockData);
    Handshake(sockData);
}

/**
 * @brief  Process socket data.
 *
 * @param sockData     The socket data
 * @return   Error code
 * @note     Only after a complete record is received, the record will be processed.
 */
static void ProcessData(DtlsSocketData *sockData) {
    if (!sockData) {
        return;
    }
    if (!s_Core->IsHandleValid(sockData->hTransportSocket)) {
        sockData->hTransportSocket = 0;
        return;
    }
    LTDtlsSessionContext *sessionCtx = sockData->sessionCtx;

    if (sessionCtx->sslCtx.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
        Handshake(sockData);
    } else {
        int ret = mbedtls_ssl_read(&sessionCtx->sslCtx, NULL, 0);
        if ((ret == 0) || (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
            s_Event->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_Disconnected);
        } else if ((ret < 0) && (ret != MBEDTLS_ERR_SSL_WANT_READ)) {
            s_Event->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_Error);
        }
    }
}

/**
 * @brief  OnSocketEvent callback to process socket events
 *
 * @param hSocket       The socket handle, not session handle
 * @param event         The network event
 * @param data          The event data
 */
static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *data) {
    DtlsSocketData *sockData = data;
    if (hSocket != sockData->hTransportSocket) return;
    if (!sockData) return;
    LTDtlsSessionContext *sessionCtx = sockData->sessionCtx;
    P("** Event hsocket %lx, event %x, pdata %p\n", LT_PLT_HANDLE(hSocket), event, data);

    switch (event) {
        case kLTSocket_Event_SocketReady:
        case kLTSocket_Event_Connected:
            // Socket and link are connected, but not DTLS. So, ignore this event.
            break;

        case kLTSocket_Event_WriteReady:
            if (sessionCtx->sslCtx.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
                Handshake(sockData);
            } else {
                s_Event->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_WriteReady);
            }
            break;

        case kLTSocket_Event_ReadReady:
            ProcessData(sockData);
            break;

        case kLTSocket_Event_Disconnected:
        default:
            s_Event->NotifyEvent(sockData->event, sockData->hSocket, event);
    }
}

void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms)
{
    P("set delay int_ms %d fin_ms %d", int_ms, fin_ms);
    DtlsSocketData *sockData = (DtlsSocketData *)data;
    s_Thread->KillTimer(s_Thread->GetCurrentThread(), OnDtlsTimeout, sockData);
    if (fin_ms == 0) {
        sockData->timerIntermediate = LTTime_Zero();
        sockData->timerFinal        = LTTime_Zero();
    } else {
        LTTime now = LT_GetCore()->GetKernelTime();
        sockData->timerIntermediate = LTTime_Add(now, LTTime_Milliseconds(int_ms));
        sockData->timerFinal        = LTTime_Add(now, LTTime_Milliseconds(fin_ms));
        s_Thread->SetTimer(sockData->sessionCtx->hThread, LTTime_Milliseconds(fin_ms), OnDtlsTimeout, NULL, sockData);
    }
}

int mbedtls_timing_get_delay(void *data)
{
    DtlsSocketData *sockData = (DtlsSocketData *)data;
    if (LTTime_IsZero(sockData->timerFinal)) return -1;
    LTTime now = LT_GetCore()->GetKernelTime();
    if(LTTime_IsGreaterThan(now, sockData->timerFinal))        return 2;
    if(LTTime_IsGreaterThan(now, sockData->timerIntermediate)) return 1;
    return 0;
}

int mbedtls_lt_random(void *data, unsigned char *output, LT_SIZE len, size_t *olen)
{
    LT_UNUSED(data);

    if (s_Crypto->GenRandomBytes(output, len)) {
        *olen = len;
        return 0;
    } else {
        *olen = 0;
        LTLOG_YELLOWALERT("random", "len %ld failed", LT_Pu32(len));
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
}

int mbedtls_lt_entropy(void *data,
                         unsigned char *output, LT_SIZE len)
{
    LT_UNUSED(data);

    if (s_Crypto->GenRandomBytes(output, len)) {
        return 0;
    } else {
        LTLOG_YELLOWALERT("entropy", "len %ld failed", LT_Pu32(len));
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
}

int mbedtls_bio_net_send(void *ctx, const unsigned char *buf, size_t len) {

    DtlsSocketData *sockData = (DtlsSocketData *)ctx;
    int ret = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
    if (pSocket) {
        ret = pSocket->WriteSocket(sockData->hTransportSocket, buf, len);
        if (ret <= 0) {
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
    }
    return ret;
}

int mbedtls_bio_net_recv(void *ctx, unsigned char *buf, size_t len) {

    DtlsSocketData *sockData = (DtlsSocketData *)ctx;
    int ret = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
    if (pSocket) {
        ret = pSocket->ReadSocket(sockData->hTransportSocket, buf, len);
        if (ret == 0) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        } else if (ret < 0) {
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
    }
    return ret;
}

void dtls_srtp_key_derivation(void *p_expkey,
                              mbedtls_ssl_key_export_type secret_type,
                              const unsigned char *secret,
                              size_t secret_len,
                              const unsigned char client_random[32],
                              const unsigned char server_random[32],
                              mbedtls_tls_prf_types tls_prf_type)
{
    dtls_srtp_keys *keys = (dtls_srtp_keys *) p_expkey;

    /* We're only interested in the DTLS 1.2 master secret */
    if (secret_type != MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET) {
        return;
    }
    if (secret_len != sizeof(keys->master_secret)) {
        return;
    }

    lt_memcpy(keys->master_secret, secret, sizeof(keys->master_secret));
    lt_memcpy(keys->randbytes, client_random, 32);
    lt_memcpy(keys->randbytes + 32, server_random, 32);
    keys->tls_prf_type = tls_prf_type;
}

/**
 * @brief Clear DTLS session context.
 *        The session handle shall be destroyed by application via NetCore.
 *
 * @param sessionCtx  The session context
 */
static void DestroySession(LTDtlsSessionContext *sessionCtx) {
    if (!sessionCtx) {
        return;
    }
    LTLOG_DEBUG("close", "connection");
    mbedtls_ssl_close_notify(&sessionCtx->sslCtx);

    mbedtls_x509_crt_free(&sessionCtx->clientCert);
    mbedtls_ssl_free(&sessionCtx->sslCtx);
    mbedtls_ssl_config_free(&sessionCtx->conf);
    mbedtls_entropy_free(&sessionCtx->entropy);
    mbedtls_pk_free(&sessionCtx->pkey);

    lt_memset(sessionCtx, 0, sizeof(LTDtlsSessionContext));
    lt_free(sessionCtx);
}


/**
 * @brief Create a DTLS session context
 *
 * @return The context
 */
static LTDtlsSessionContext *CreateSession(DtlsSocketData *sockData) {
    // Create session handle and context
    LTDtlsSessionContext *sessionCtx = lt_malloc(sizeof(LTDtlsSessionContext));
    P("create context %p\n", sessionCtx);
    if (!sessionCtx) {
        return NULL;
    }
    lt_memset(sessionCtx, 0, sizeof(LTDtlsSessionContext));

    // Init DTLS context
    sessionCtx->hThread = s_Thread->GetCurrentThread();

    mbedtls_ssl_init(&sessionCtx->sslCtx);
    mbedtls_ssl_config_init(&sessionCtx->conf);
    mbedtls_x509_crt_init(&sessionCtx->clientCert);
    mbedtls_pk_init(&sessionCtx->pkey);
    mbedtls_entropy_init(&sessionCtx->entropy);
#if defined(MBEDTLS_DEBUG_C)
	mbedtls_debug_set_threshold(2);
#endif
    int ret;
    if ((ret = mbedtls_entropy_add_source(&sessionCtx->entropy,
                               mbedtls_lt_random, NULL,
                               MBEDTLS_ENTROPY_BLOCK_SIZE,
                               MBEDTLS_ENTROPY_SOURCE_STRONG)) != 0) {
        LTLOG_YELLOWALERT("create.session.ent", "failed mbedtls_entropy_add_source returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    sessionCtx->sslCtx.clientPrivateKeyReference = &sockData->dtlsOptions.clientPrivateKeyReference;

    P("Loading certificate");

    unsigned char *certData = sockData->dtlsOptions.clientCertificate;
    size_t certLen = (certData[1] << 8) | (certData[2]);
    certData += 3;
    ret = mbedtls_x509_crt_parse(&sessionCtx->clientCert, certData, certLen);
    if (ret < 0) {
        LTLOG_YELLOWALERT("create.session.crt", "failed mbedtls_x509_crt_parse returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    mbedtls_pk_context *pk = &sessionCtx->pkey;
    const mbedtls_pk_info_t *pk_info = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);

    if ((ret = mbedtls_pk_setup(pk, pk_info)) != 0) {
        LTLOG_YELLOWALERT("create.session.pk", "failed mbedtls_pk_setup returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    mbedtls_ecp_keypair *key = pk->pk_ctx;
    if ((ret = mbedtls_ecp_group_load(&key->grp, MBEDTLS_ECP_DP_SECP256R1)) != 0) {
        LTLOG_YELLOWALERT("create.session.ecp.grp", "failed mbedtls_ecp_group_load returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    P("Setting up the DTLS structure");

    if ((ret = mbedtls_ssl_config_defaults(&sessionCtx->conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        LTLOG_YELLOWALERT("create.session.cfg", "failed mbedtls_ssl_config_defaults returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    mbedtls_ssl_conf_max_frag_len(&sessionCtx->conf, MBEDTLS_SSL_MAX_FRAG_LEN_1024);
    mbedtls_ssl_conf_rng(&sessionCtx->conf, mbedtls_lt_entropy, NULL);
    mbedtls_ssl_conf_dbg(&sessionCtx->conf, mbedtls_debug, NULL);

    if ((ret = mbedtls_ssl_setup(&sessionCtx->sslCtx, &sessionCtx->conf)) != 0) {
        LTLOG_YELLOWALERT("create.session.ssl", "failed mbedtls_ssl_setup returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    mbedtls_ssl_set_bio(&sessionCtx->sslCtx, sockData,
                        mbedtls_bio_net_send, mbedtls_bio_net_recv, NULL);

    mbedtls_ssl_set_timer_cb(&sessionCtx->sslCtx, sockData, mbedtls_timing_set_delay,
                             mbedtls_timing_get_delay);

    if ((ret = mbedtls_ssl_conf_own_cert(&sessionCtx->conf, &sessionCtx->clientCert, &sessionCtx->pkey)) != 0) {
        LTLOG_YELLOWALERT("create.session.own.cert", "failed mbedtls_ssl_conf_own_cert returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    sessionCtx->profile[0] = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80;
    sessionCtx->profile[1] = MBEDTLS_TLS_SRTP_UNSET;

    ret = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&sessionCtx->conf, sessionCtx->profile);
    if (ret != 0) {
        LTLOG_YELLOWALERT("create.session.srtp.prf", " failed dtls_srtp_protection_profiles returned -0x%x",
                    (unsigned int) -ret);
        goto exit;
    }

    mbedtls_ssl_set_export_keys_cb(&sessionCtx->sslCtx, dtls_srtp_key_derivation,
                                &sessionCtx->dtls_srtp_keying);

exit:
    return sessionCtx;
}

static void
LogSessionSummary(DtlsSocketData *sockData) {
    LTLOG_SERVER("sum", "sid=%s conn_dur(ms)=%lld state=%s",
        sockData->sessionId,
        LT_Ps64(LTTime_GetMilliseconds(sockData->connectionDuration)),
        ConnectionStateToString(sockData->sessionCtx->sslCtx.state));
}

/** Interface used for Socket handles *****************************************/
/**
 * @brief Destroy socket
 *
 * @param hSocket
 */
static void LTSocket_DestroySocket(LTHandle hSocket) {
    DtlsSocketData *sockData = s_Core->ReserveHandlePrivateData(hSocket); // zero handle returns NULL
    if (!sockData) return;

    LogSessionSummary(sockData);

    s_Core->DestroyHandle(sockData->event);
    lt_free(sockData->spec);
    sockData->spec = NULL;

    s_Thread->KillTimer(s_Thread->GetCurrentThread(), OnDtlsTimeout, sockData);
    DestroySession(sockData->sessionCtx);
    sockData->sessionCtx = NULL;
    FreeOptions(&sockData->dtlsOptions);
    sockData->hSocket = 0;
    if (sockData->sessionId) {
        lt_free(sockData->sessionId);
        sockData->sessionId = NULL;
    }
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
}

/** LTSocket functions **********************************************/

static void LTSocket_OnSocketEvent(LTSocket hSocket, LTSocket_EventProc proc, void *procData) {
    DtlsSocketData *sockData = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    s_Event->RegisterForEvent(sockData->event, proc, NULL, procData, true);
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
}

static void LTSocket_NoSocketEvent(LTSocket hSocket, LTSocket_EventProc proc) {
    DtlsSocketData *sockData = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    s_Event->UnregisterFromEvent(sockData->event, proc);
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief Connect socket
 */
static void LTSocket_ConnectSocket(LTSocket hSocket) {
    P("connect socket");
    DtlsSocketData *sockData = s_Core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    sockData->connectionStartTime = LT_GetCore()->GetKernelTime();
    Handshake(sockData);
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief Disconnect socket
 */
static void LTSocket_DisconnectSocket(LTSocket hSocket) {
    P("disconnect socket");
    DtlsSocketData *sockData = s_Core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
    if (pSocket) pSocket->DisconnectSocket(sockData->hTransportSocket);
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief  Get the LTSocketSpec.
 *
 * @param socketData  The socket
 * @param spec        The buffer to hold LTSocketSpec data
 * @param len         The length of spec buffer
 * @return LTNetworkResult
 */
static void LTSocket_GetSocketSpec(LTSocket hSocket, char *spec, int len) {
    DtlsSocketData *pSocketData = s_Core->ReserveHandlePrivateData(hSocket);
    lt_strncpyTerm(spec, pSocketData->spec, len);
    s_Core->ReleaseHandlePrivateData(hSocket, pSocketData);
}

static bool LTSocket_GetProperty(LTSocket hSocket, const char *name, void *value) {
    P("getproperty %s", name);
    DtlsSocketData *sockData = s_Core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return false;

    LTDtlsSessionContext *sessionCtx = sockData->sessionCtx;
    if (!sessionCtx || !name || !value) {
        s_Core->ReleaseHandlePrivateData(hSocket, sockData);
        return false;
    }

    if(0 == lt_strcmp(name, "srtp.protectionprofile")) {
        mbedtls_dtls_srtp_info info;
        mbedtls_ssl_get_dtls_srtp_negotiation_result(&sessionCtx->sslCtx, &info);
        if (info.chosen_dtls_srtp_profile == MBEDTLS_TLS_SRTP_UNSET) {
            LTLOG_YELLOWALERT("srtp.prof", "Error getting selected SRTP profile");
            lt_memcpy(value, &(LTDtlsSrtpProtectionProfileId) { kLTDtlsSrtpProtectionProfile_None }, sizeof(LTDtlsSrtpProtectionProfileId));
        }
        else {
            lt_memcpy(value, &(LTDtlsSrtpProtectionProfileId) { info.chosen_dtls_srtp_profile }, sizeof(LTDtlsSrtpProtectionProfileId));
            LTLOG_DEBUG("srtp.prof", "selected SRTP profile %d", info.chosen_dtls_srtp_profile);
            s_Core->ReleaseHandlePrivateData(hSocket, sockData);
            return true;
        }
    }
    else if(0 == lt_strcmp(name, "srtp.keymaterial")) {
        const char * dtls_srtp_label = "EXTRACTOR-dtls_srtp";
        unsigned char dtls_srtp_key_material[MBEDTLS_TLS_SRTP_MAX_KEY_MATERIAL_LENGTH];
        lt_memset(dtls_srtp_key_material, 0, MBEDTLS_TLS_SRTP_MAX_KEY_MATERIAL_LENGTH);

        dtls_srtp_keys* dtls_srtp_keying = &sessionCtx->dtls_srtp_keying;
        int ret = mbedtls_ssl_tls_prf(dtls_srtp_keying->tls_prf_type,
                                       dtls_srtp_keying->master_secret,
                                       sizeof(dtls_srtp_keying->master_secret),
                                       dtls_srtp_label,
                                       dtls_srtp_keying->randbytes,
                                       sizeof(dtls_srtp_keying->randbytes),
                                       dtls_srtp_key_material,
                                       sizeof(dtls_srtp_key_material));
        P("tls_prf_type %d", dtls_srtp_keying->tls_prf_type);
        if (ret) {
            LTLOG_YELLOWALERT("key.ext", "Error extracting SRTP key material: %d", ret);
        }
        else {
            LTLOG_DEBUG("key.ext", "Extracted SRTP key material");
            lt_memcpy(value, dtls_srtp_key_material, MBEDTLS_TLS_SRTP_MAX_KEY_MATERIAL_LENGTH);
            s_Core->ReleaseHandlePrivateData(hSocket, sockData);
            return true;
        }
    }
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
    return false;
}

static bool LTSocket_SetProperty(LTSocket hSocket, const char *name, const void *value) {
    P("setproperty %s", name);
    if (!name || !value) {
        LTLOG_DEBUG("sp.err", "invalid data");
        return false;
    }
    DtlsSocketData *sockData = s_Core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return false;
    LTDtlsSessionContext *sessionCtx = sockData->sessionCtx;
    if (0 == lt_strcmp(name, "transport.socket")) {
        LTSocket newSocket = *(LTSocket *)value;
        if (newSocket == sockData->hTransportSocket) return true;
        if (sockData->hTransportSocket) {
            ILTSocket *iOldSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
            iOldSocket->NoSocketEvent(sockData->hTransportSocket, OnSocketEvent);
        }
        sockData->hTransportSocket = newSocket;
        ILTSocket *iNewSocket = lt_gethandleinterface(ILTSocket, newSocket);
        iNewSocket->OnSocketEvent(newSocket, OnSocketEvent, sockData);
    } else if (0 == lt_strcmp(name, "peer.fingerprint.sha256")) {
        mbedtls_ssl_conf_peer_cert_digest(&sessionCtx->conf, MBEDTLS_MD_SHA256, value, SHA256_HASH_LENGTH);
        s_Core->ReleaseHandlePrivateData(hSocket, sockData);
        return true;
    } else if (0 == lt_strcmp(name, "sessionid")) {
        sockData->sessionId = lt_strdup(value);
        s_Core->ReleaseHandlePrivateData(hSocket, sockData);
        return true;
    } else {
        LTLOG_YELLOWALERT("setprop.unsup", "Unsupported property: %s", name);
    }
    s_Core->ReleaseHandlePrivateData(hSocket, sockData);
    return false;
}

/** End of LTSocket interface functions ************************************/

static LTSocket LTNetDtls_WrapSocket(LTSocket hTransportSocket, const char *socketSpec) {
    if (!hTransportSocket || !socketSpec) return 0;

    LTSocket hSocket = s_Core->CreateHandle((LTInterface *)&s_ILTSocket, sizeof(DtlsSocketData));
    if (!hSocket) return 0;
    DtlsSocketData *sockData = NULL;
    do {
        sockData = s_Core->ReserveHandlePrivateData(hSocket);
        if (!sockData) break;
        lt_memset(sockData, 0, sizeof(DtlsSocketData));

        sockData->hSocket = hSocket;
        sockData->hTransportSocket = hTransportSocket;
        sockData->spec = lt_strdup(socketSpec);
        sockData->event = s_Core->CreateEvent(&SocketEventArgs, DispatchSocketEvent, NULL, DispatchSocketEventImmediate, LTHANDLE_TO_VOIDPTR(hSocket));

        LTDtlsOptions options;
        if (!ParseSocketSpec(socketSpec, &options)) {
            LTLOG_YELLOWALERT("parse.spec.err", "Failed to parse socket spec");
            break;
        }
        sockData->dtlsOptions = options;

        // create DTLS session context
        sockData->sessionCtx = CreateSession(sockData);
        if (!sockData->sessionCtx) {
            LTLOG_YELLOWALERT("fail", "Session context create fails");
            break;
        }

        ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hTransportSocket);
        iSocket->OnSocketEvent(hTransportSocket, OnSocketEvent, sockData);
        s_Core->ReleaseHandlePrivateData(hSocket, sockData);
        return hSocket;
    } while (0);

    if (sockData) s_Core->ReleaseHandlePrivateData(hSocket, sockData);
    lt_destroyhandle(hSocket);
    return 0;
}

u16 LTNetDtls_SignCertVerify(void *clientPrivateKeyReference, const u8 hash[SHA256_HASH_LENGTH], u8 *x962Signature, u16 sigMaxSize) {
    LTSecureCryptoEcdsaP256 *secEcdsa = lt_createobject(LTSecureCryptoEcdsaP256);
    if (!secEcdsa) return 0;
    u8 signature[ECDSA_P256_SIGNATURE_LENGTH];
    LTSystemCryptoResult ret = secEcdsa->API->SignHash(clientPrivateKeyReference, hash, signature, NULL);
    lt_destroyobject(secEcdsa);
    if (kLTSystemCrypto_Result_Ok != ret) return 0;
    u16 sigLen = 0;
    LTSystemCryptoEncoder *cryptoEncoder = lt_createobject(LTSystemCryptoEncoder);
    if (cryptoEncoder) {
        cryptoEncoder->API->EncodeEcdsaSignature(signature, ECDSA_P256_SIGNATURE_LENGTH, x962Signature, sigMaxSize, &sigLen);
        lt_destroyobject(cryptoEncoder);
    }
    return sigLen;
}

static void CopyL2B(u8 *dst, u32 dstLen, const u8 *src, u32 srcLen) {
    if (srcLen > dstLen) return;
    u32 i;
    for (i=0, dst += (dstLen - 1); i < srcLen; ++i, --dst, ++src) {
        *dst = *src;
    }
}

bool LTNetDtls_VerifySigHash(mbedtls_pk_context *ctx, const u8 hash[SHA256_HASH_LENGTH], const u8 *x962Signature, u16 x962SigLen) {
    struct LOCAL {
        u8 publicKey[ECDSA_P256_PUBLICKEY_LENGTH];
        u8 signature[ECDSA_P256_SIGNATURE_LENGTH];
    };
    struct LOCAL *tmp = lt_malloc(sizeof(struct LOCAL));
    if (!tmp) return false;
    lt_memset(tmp, 0, sizeof(struct LOCAL));

    mbedtls_ecdsa_context *pk_ctx = (mbedtls_ecdsa_context *)ctx->pk_ctx;
    CopyL2B(tmp->publicKey, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)pk_ctx->Q.X.p, pk_ctx->Q.X.n * sizeof(mbedtls_mpi_uint));
    CopyL2B(tmp->publicKey + LTSYSTEMCRYPTO_BYTES_PER_U256, LTSYSTEMCRYPTO_BYTES_PER_U256, (u8 *)pk_ctx->Q.Y.p, pk_ctx->Q.Y.n * sizeof(mbedtls_mpi_uint));

    LTSystemCryptoResult ret = kLTSystemCrypto_Result_Error;
    LTSystemCryptoEncoder *cryptoEncoder = lt_createobject(LTSystemCryptoEncoder);
    if (cryptoEncoder) {
        if (cryptoEncoder->API->DecodeEcdsaSignature(x962Signature, x962SigLen, tmp->signature, ECDSA_P256_SIGNATURE_LENGTH)) {
            ret = s_Crypto->VerifyEcdsaHash(hash, tmp->signature, tmp->publicKey);
        }
        lt_destroyobject(cryptoEncoder);
    }
    lt_free(tmp);
    return kLTSystemCrypto_Result_Ok == ret;
}

/**
 * @brief Check if required cryptos are available
 *
 * @return true if required cryptos are available
 */
static bool CheckCrypto(void) {
    if (!s_Crypto) {
        return false;
    }
    LTSystemCryptoOptions o;
    s_Crypto->GetOptions(&o);
    return o.enabled    &&
           o.random     &&
           o.aes128Gcm  &&
           o.sha256     &&
           o.hmacSha256 &&
           o.seqSha256  &&
           o.ecdsa      &&
           o.ecdhe;
}

static void ShutdownNetDtls(void) {
    if (s_Tokenizer) lt_closelibrary(s_Tokenizer);
    s_Tokenizer = NULL;
    if (s_Crypto) lt_closelibrary(s_Crypto);
    s_Crypto = NULL;
    LTNetDtlsCrypto_Fini();

    s_Event  = NULL;
    s_Thread = NULL;
    s_Core   = NULL;
}

/*___________________________________________
 / LTNetDtls library initialization */
static bool
LTNetDtlsImpl_LibInit(void) {
    // Open the libraries
    s_Core    = LT_GetCore();
    s_Thread = lt_getlibraryinterface(ILTThread, s_Core);
    s_Event  = lt_getlibraryinterface(ILTEvent, s_Core);

    do {
        if (!(s_Crypto  = lt_openlibrary(LTSystemCrypto))) break;
        if (!(s_Tokenizer  = lt_openlibrary(LTUtilityTokenizer))) break;
        if (!CheckCrypto()) break;
        if (!LTNetDtlsCrypto_Init()) break;
        return true;
    } while (0);

    ShutdownNetDtls();
    return false;
}

static void
LTNetDtlsImpl_LibFini(void) {
    ShutdownNetDtls();
}

/* LTNetDtls library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTNetDtls) {
    .WrapSocket         = &LTNetDtls_WrapSocket,
} LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTSocket, LTSocket_DestroySocket)
    .OnSocketEvent    = &LTSocket_OnSocketEvent,
    .NoSocketEvent    = &LTSocket_NoSocketEvent,
    .ConnectSocket    = &LTSocket_ConnectSocket,
    .DisconnectSocket = &LTSocket_DisconnectSocket,
    .GetSocketSpec    = &LTSocket_GetSocketSpec,
    .GetProperty      = &LTSocket_GetProperty,
    .SetProperty      = &LTSocket_SetProperty,
LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTNetDtls, (ILTSocket));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 *  13-Aug-23   valerian    updated with mbedTls integration
 */
