/*******************************************************************************
 * source/lt/net/tls/LTNetTlsImpl.c     Implementation of TLS 1.3 client interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/LTTypes.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>

#include <lt/net/x509/LTNetX509.h>
#include <lt/net/tls/LTNetTls.h>
#include <lt/net/tls/LTNetTlsErrors.h>

#include "LTNetTls13.h"

/***************************** helper functions ******************************/
DEFINE_LTLOG_SECTION("nettls");
#define P(...)
#define PLOG(...) LTLOG(__VA_ARGS__)

typedef enum {
    kSocketSpec_TlsParamType_Servername,
    kSocketSpec_TlsParamType_CAKeys,
    kSocketSpec_TlsParamType_ClientCert,
    kSocketSpec_TlsParamType_Alpn,
    kSocketSpec_TlsParamType_Unknown,
} SocketSpec_TlsParamType;

typedef enum {
    kSocketSpecParser_Protocol,
    kSocketSpecParser_Key,
    kSocketSpecParser_Value,
    kSocketSpecParser_Error,
} SocketSpecParser_State;

typedef struct {
    SocketSpecParser_State  state;
    SocketSpec_TlsParamType paramType;
    LTTlsOptions           *options;
} SocketSpecParserContext;

// Private TLS socket data
typedef struct TlsSocketData {
    LTTlsSessionContext *sessionCtx;        // Extra data (pSessionCtx later)
    char                *spec;
    LTSocket             hSocket;           // TLS socket
    LTSocket             hTransportSocket;  // TCP socket
    LTEvent              event;
    LTAtomic            *socketWriteCount;
    LTAtomic            *socketWriteFailCount;
} TlsSocketData;

static ILTSocket s_ILTSocket;

static struct {
    LTCore              *core;
    LTSystemCrypto      *crypto;
    LTUtilityTokenizer  *tokenizer;
    ILTThread           *iThread;
    ILTEvent            *iEvent;
    LTMutex             *mutex;
    LTNetX509           *x509;
} S;

/** Event Dispatch Procs ******************************************************/

static const LTArgsDescriptor SocketEventArgs = {2, { kLTArgType_lthandle, kLTArgType_u32 }}; // socket, event

static void DispatchOnSocketEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc || !args) return;
    LTSocket hSocket = LTArgs_lthandleAt(0, args);
    u32 sockEvent    = LTArgs_u32At(1, args);
    (*(LTSocket_EventProc *)proc)(hSocket, sockEvent, data);
}

/** TLS ************************************************************************
 * We don't destroy session here on error or disconnect in this TLS driver.
 * We pass such event to the application and let the application decide and take actions. */

/**
 * @brief  Callback when TLS timeout, defined as LTTLS_TIMEOUT
 *
 * @param data  The session context
 */
static void OnTlsTimeout(void *data) {
    if (!data) return;
    LTSocket hSocket = VOIDPTR_TO_LTHANDLE(data);
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    if (sockData->sessionCtx) {
        S.iThread->KillTimer(sockData->sessionCtx->hThread, OnTlsTimeout, LTHANDLE_TO_VOIDPTR(hSocket));
        S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_ConnectTimeout);
    }
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
    // LTLOG("", "Tls time out state %d", sockData->sessionCtx->tlsCtx.eState);
}

/**
 * @brief  Restart the timer upon a valid network activity
 *
 * @param data  The session context
 */
static void ContinueTlsTimer(LTSocket hSocket) {
    // P("Tls timer continue\n");
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    S.iThread->RestartTimer(sockData->sessionCtx->hThread, OnTlsTimeout, LTHANDLE_TO_VOIDPTR(hSocket));
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief Create a TLS session context
 *
 * @return The context
 */
static LTTlsSessionContext *CreateSession(LTSocket hTransportSocket, LTTlsOptions *pOptions) {
    // Create session handle and context
    LTTlsSessionContext *sessionCtx = lt_malloc(sizeof(LTTlsSessionContext));
    P("create context %p\n", sessionCtx);
    if (!sessionCtx) {
        return NULL;
    }
    lt_memset(sessionCtx, 0, sizeof(LTTlsSessionContext));
    S.mutex->API->Lock(S.mutex);
    LTList_Init(&sessionCtx->tlsAppRxBuf);
    S.mutex->API->Unlock(S.mutex);

    // Copy onSocketCBData to tlsOptions
    sessionCtx->tlsOptions = *pOptions;

    // Init TLS context
    sessionCtx->tlsConfig.crypto  = S.crypto;
    sessionCtx->tlsConfig.x509    = S.x509;
    sessionCtx->tlsConfig.hSocket = hTransportSocket;
    LTTlsContext *tlsCtx = &sessionCtx->tlsCtx;
    sessionCtx->hThread = S.iThread->GetCurrentThread();
    if ((InitTls(tlsCtx, &sessionCtx->tlsConfig, &sessionCtx->tlsOptions, true)) < 0) {
        lt_free(sessionCtx);
        return NULL;
    }

    return sessionCtx;
}

static void FreeOptions(LTTlsOptions *pOptions) {
    if (pOptions->serverName)        lt_free(pOptions->serverName);
    if (pOptions->clientCertificate) lt_free(pOptions->clientCertificate);
    LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
    if (keyManager) {
        if (pOptions->caKeys) {
            for (u32 i = 0; i < pOptions->caKeyCount; ++i) {
                keyManager->API->FreePublicKey(&pOptions->caKeys[i]);
            }
            lt_free(pOptions->caKeys);
        }
        lt_destroyobject(keyManager);
    }
    if (pOptions->alpnExt) {
        if (pOptions->alpnExt->protocolNames) lt_free(pOptions->alpnExt->protocolNames);
        lt_free(pOptions->alpnExt);
    }
}

/**
 * @brief Clear TLS session context.
 *        The session handle shall be destroyed by application via NetCore.
 *
 * @param sessionCtx  The session context
 */
static void DestroySession(LTTlsSessionContext *sessionCtx) {
    P("DestroySession %p\n", sessionCtx);
    if (!sessionCtx) {
        return;
    }

    FreeTls(&sessionCtx->tlsCtx);
    FreeOptions(&sessionCtx->tlsOptions);

    LTTlsAppRxData *data = NULL;
    S.mutex->API->Lock(S.mutex);
    while (!LTList_IsEmpty(&sessionCtx->tlsAppRxBuf)) {
        data = LT_CONTAINER_OF(sessionCtx->tlsAppRxBuf.pNext, LTTlsAppRxData, node);
        LTList_Remove(&data->node);
        lt_free(data->data);
        lt_free(data);
    }
    S.mutex->API->Unlock(S.mutex);

    lt_memset(sessionCtx, 0, sizeof(LTTlsSessionContext));
    lt_free(sessionCtx);
}

/**
 * @brief  Process socket data.
 *
 * @param sockData     The socket data
 * @return   Error code
 * @note     Only after a complete record is received, the record will be processed.
 */
static int ProcessData(TlsSocketData *sockData) {
    if (!sockData) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (!S.core->IsHandleValid(sockData->hTransportSocket)) {
        sockData->hTransportSocket = 0;
        return MBEDTLS_ERR_SSL_INVALID_SOCKET;
    }

    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    s32 readCount = 0;
    LTTlsSessionContext *sessionCtx = sockData->sessionCtx;
    LTTlsContext *tlsCtx = &sessionCtx->tlsCtx;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);

    // Each iteration will read one record. The loop will read until no complete record is left.
    while (true) {
        // A new record to read
        if (tlsCtx->toRead == 0) {
            if (tlsCtx->inBufLen >= LTTLS_RECORD_HEADER_LEN) {
                LTLOG_YELLOWALERT("procdata.corrupt", "%d unknown bytes already in a new record, -%04x", tlsCtx->inBufLen, -MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED);
                return MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
            }

            // Read record header, 5 B
            readCount = pSocket->ReadSocket(sockData->hTransportSocket, tlsCtx->inBuf + tlsCtx->inBufLen, LTTLS_RECORD_HEADER_LEN - tlsCtx->inBufLen);
            P("rd.head", "read header %ld bytes rec %u\n", LT_Ps32(readCount), tlsCtx->inBuf[0]);

            if (readCount == 0) {
                // No data available
                break;
            }

            if (readCount < 0) {
                LTLOG_YELLOWALERT("error", "socket down, -%04x", -MBEDTLS_ERR_SSL_INVALID_SOCKET);
                return MBEDTLS_ERR_SSL_INVALID_SOCKET;
            }

            tlsCtx->inBufLen += readCount;

            // Not enough for a header
            if (tlsCtx->inBufLen < LTTLS_RECORD_HEADER_LEN) {
                break;
            }

            // Larger than a header
            if (tlsCtx->inBufLen > LTTLS_RECORD_HEADER_LEN) {
                LTLOG_YELLOWALERT("procdata.headertoobig", "%d bytes in a new record header (should be 5 bytes), -%04x", tlsCtx->inBufLen, -MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED);
                return MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
            }

            // Exactly one header, so get the length of body
            tlsCtx->toRead = ((u16)tlsCtx->inBuf[3] << 8) + tlsCtx->inBuf[4];
            P("read expect body %ld bytes\n", LT_Ps32(tlsCtx->toRead));

            // Body + header larger than the TLS read buffer
            if ((tlsCtx->toRead + LTTLS_RECORD_HEADER_LEN) > LTTLS_IN_BUFFER_LEN) {
                LTLOG_YELLOWALERT("procdata.recordtoobig", "record is too large, %d bytes, -%04x", tlsCtx->toRead + LTTLS_RECORD_HEADER_LEN, -MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL);
                return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
            }
        }

        // Now a good header is read and parsed, so read the body
        readCount = pSocket->ReadSocket(sockData->hTransportSocket, tlsCtx->inBuf + tlsCtx->inBufLen, tlsCtx->toRead);
        P("rd.body", "read body %ld / %d\n", LT_Ps32(readCount), tlsCtx->toRead);

        if (readCount == 0) {
            break;
        }

        if (readCount < 0) {
            LTLOG_YELLOWALERT("error", "socket down, -%04x", -MBEDTLS_ERR_SSL_INVALID_SOCKET);
            return MBEDTLS_ERR_SSL_INVALID_SOCKET;
        }

        if (tlsCtx->toRead < readCount) {
            LTLOG_YELLOWALERT("procdata.toomany", "read more bytes than needed, -%04x", -MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED);
            return MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
        }
        tlsCtx->inBufLen += readCount;
        tlsCtx->toRead   -= readCount;

        if (tlsCtx->toRead > 0) {
            // A partial record is read. More to read on next ReadReady event.
            continue;
        }

        // Now, one complete record is in buf.

        /* In TLS 1.3, always treat ChangeCipherSpec record as unencrypted.
         * The only thing we do with ChangeCipherSpec is check the length and content and then ignore. */
        if (tlsCtx->inBuf[0] == MBEDTLS_SSL_MSG_CHANGE_CIPHER_SPEC) {
            // check ChangeCipherSpec, check ignore.
            if (tlsCtx->minorVer != MBEDTLS_SSL_MINOR_VERSION_4 || tlsCtx->inBufLen != LTTLS_RECORD_HEADER_LEN + 1 || tlsCtx->inBuf[5] != 1) {
                LTLOG_YELLOWALERT("ccs.error", "CCS decode, -%04x", -MBEDTLS_ERR_SSL_DECODE_ERROR);
                return MBEDTLS_ERR_SSL_DECODE_ERROR;
            }
            tlsCtx->inRecPayloadLen = 0; // this record is processed, so no more data left.
            tlsCtx->inBufLen = 0;
            continue;
        }

        if (sessionCtx->tlsCtx.eState != kLTTLSState_APPLICATION) {
            // Handle handshake
            ret = Handshake(&sessionCtx->tlsCtx);
            tlsCtx->inRecPayloadLen = 0; // this record is processed, so no more data left.
            tlsCtx->inBufLen = 0;

            // State is changed to APPLICATION, so tls session is good to go. Notify application.
            if (0 == ret && sessionCtx->tlsCtx.eState == kLTTLSState_APPLICATION) {
                // Event notify that TLS connection is ready.
                S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_Connected);
                P("App start\n");
            }

        } else {
            // Handle session
            ret = Session(&sessionCtx->tlsCtx);
            if (0 == ret && tlsCtx->inRecType == MBEDTLS_SSL_MSG_APPLICATION_DATA) {
                // A new packet, append to app rx buf
                LTTlsAppRxData *data = lt_malloc(sizeof(LTTlsAppRxData));
                if (!data) {
                    LTLOG_REDALERT("oom.data", "oom for app data, -%04x", -MBEDTLS_ERR_SSL_ALLOC_FAILED);
                    return MBEDTLS_ERR_SSL_ALLOC_FAILED;
                }
                data->data = lt_malloc(tlsCtx->inRecPayloadLen);
                if (!data->data) {
                    lt_free(data);
                    LTLOG_REDALERT("oom.data.data", "oom for app data, -%04x", -MBEDTLS_ERR_SSL_ALLOC_FAILED);
                    return MBEDTLS_ERR_SSL_ALLOC_FAILED;
                }

                // add decrypted app data record to tlsAppRxBuf.
                P("data InRecLen %d\n", tlsCtx->inRecPayloadLen);
                lt_memcpy(data->data, tlsCtx->inRecPayload, tlsCtx->inRecPayloadLen);
                data->len = tlsCtx->inRecPayloadLen;
                data->alreadyRead = 0;
                S.mutex->API->Lock(S.mutex);
                LTList_AddTail(&sessionCtx->tlsAppRxBuf, &data->node);
                S.mutex->API->Unlock(S.mutex);
                tlsCtx->inRecPayloadLen = 0;  // this record is processed, so no more data left.
                tlsCtx->inBufLen = 0;

                // Notify that app can read app data.
                S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_ReadReady);

            } else {
                // post handshake record
                tlsCtx->inRecPayloadLen = 0; // this record is processed, so no more data left.
                tlsCtx->inBufLen = 0;
            }

        }

        if (ret != 0 && ((-ret) & 0xFF80) != (-MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
            LTLOG_YELLOWALERT("proc.fail", "process record -%08lX", LT_Ps32(-ret));
            return ret;
        }

    }

    return 0;
}

/**
 * @brief  OnSocketEvent callback to process socket events
 *
 * @param hSocket       The socket handle, not session handle
 * @param event         The network event
 * @param data          The event data
 */
static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *data) {
    TlsSocketData *sockData = data;
    if (!sockData || sockData->hTransportSocket != hSocket || !sockData->hTransportSocket) return;
    LTTlsSessionContext *sessionCtx = sockData->sessionCtx;
    ContinueTlsTimer(sockData->hSocket);

    P("sck.evt", "%lx %lx", LT_PLT_HANDLE(hSocket), LT_PLT_HANDLE(event));
    switch (event) {
        case kLTSocket_Event_SocketReady:
        case kLTSocket_Event_Connected:
            // Socket and link are connected, but not TLS. So, ignore this event.
            break;

        case kLTSocket_Event_WriteReady:
            // Buffered data to resend
            if (sessionCtx->tlsCtx.resendLen) {
                // Data buffered to resend
                const u8 *buf = sessionCtx->tlsCtx.resendData;
                u16 len = sessionCtx->tlsCtx.resendLen;
                int ret = FlushBuffer(hSocket, &buf, &len);
                P("onsck.left", "%d %u / %u", ret, len, sessionCtx->tlsCtx.resendLen);
                sessionCtx->tlsCtx.resendLen = len;
                sessionCtx->tlsCtx.resendData = buf;
                // On success, another kLTSocket_Event_WriteReady is sent by net driver socket.
                if (ret == 0) { P("onsck.resend.done", "all flushed"); }
                // On write error, net driver socket will notify a kLTSocket_Event_WriteError event. So, we don't re-notify here.
                break;
            }

            // Normal situation, no buffered data to resend
            if (sessionCtx->tlsCtx.eState == kLTTLSState_HS_CLIENT_HELLO) {
                P("start tls\n");
                Handshake(&sessionCtx->tlsCtx);

            } else {
                if (sessionCtx->tlsCtx.eState == kLTTLSState_APPLICATION) {
                    S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_WriteReady);
                }
            }
            break;

        case kLTSocket_Event_ReadReady:
            {
                int ret = ProcessData(sockData);
                if (((-ret) & 0xFF80) == (-MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
                    S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_Disconnected);

                } else if (ret < 0) {
                    S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, kLTSocket_Event_ReadError);
                }
            }
            break;

        default:
            S.iEvent->NotifyEvent(sockData->event, sockData->hSocket, event);
    }
}

/** Interface used for Socket handles *****************************************/
/**
 * @brief Destroy lower driver's socket
 *
 * @param hSocket
 */
static void LTSocket_DestroySocket(LTHandle hSocket) {
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket); // zero handle returns NULL
    if (!sockData) return;
    if (sockData->sessionCtx) {
        S.iThread->KillTimer(sockData->sessionCtx->hThread, OnTlsTimeout, LTHANDLE_TO_VOIDPTR(hSocket));
        lt_destroyhandle(sockData->event);
        lt_destroyhandle(sockData->hTransportSocket);
        lt_free(sockData->spec);
        DestroySession(sockData->sessionCtx);
        *sockData = (TlsSocketData){};
    }
    // On return, socket handle data is freed.
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
}

/** LTSocket functions **********************************************/

/**
 * @brief Use CreateSocket for TLS. This function does nothing for TLS.
 */
static void LTSocket_ConnectSocket(LTSocket hSocket) {
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
    if (pSocket) pSocket->ConnectSocket(sockData->hTransportSocket);
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief TCP socket is disconnected, but not closed here.
 */
static void LTSocket_DisconnectSocket(LTSocket hSocket) {
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockData->hTransportSocket);
    if (pSocket) pSocket->DisconnectSocket(sockData->hTransportSocket);
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
}

/**
 * @brief  Send application data of session to network, not for handshake
 *
 * @param socketData The TLS socket data
 * @param buffer     The application buffer
 * @param len        The length of data to write, in bytes
 * @return    The number of Bytes actually sent, or negative error code.
 *            0 only if no data to write.
 */
static s32 LTSocket_WriteSocket(LTSocket hSocket, const void *buffer, u32 len) {
    if (!buffer || !len) return 0; // no data to write
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);

    if (!sockData) return -0x10;
    if (!S.core->IsHandleValid(sockData->hTransportSocket)) {
        sockData->hTransportSocket = 0;
        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        return -0x20;
    }
    LTTlsSessionContext *sessionCtx = sockData->sessionCtx;
    if (!sessionCtx) {
        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        return -0x30;
    }
    if (sessionCtx->tlsCtx.resendLen) {
        // Still have data in outBuf to resend. So, nothing will be sent.
        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        P("sck.wr.resend", "has data pending to resend");
        return 0;
    }
    s32 wlen = WriteApplicationData(&sessionCtx->tlsCtx, (const u8 *)buffer, len);
    ContinueTlsTimer(sockData->hSocket); // reset timer since data is being written
    S.core->ReleaseHandlePrivateData(hSocket, sockData);
    // wlen < 0 on errors. wlen cannot be 0.
    if (wlen <= 0) {
        LTAtomic_FetchAdd(sockData->socketWriteFailCount, 1);
        return (-0x50 + wlen);
    }
    LTAtomic_FetchAdd(sockData->socketWriteCount, 1);
    return wlen;
}

/**
 * @brief  Copy application data of session from the session buffer to an application buffer, not for handshake
 *
 * @param socketData The TLS socket data
 * @param buffer     The application buffer to copy data to
 * @param maxLen     The max length of data to read, in bytes
 * @return  The number of Bytes actually read, or negative error code.
 *          0 if no buffer to read data, or no data in AppRxBuf.
 */
static s32 LTSocket_ReadSocket(LTSocket hSocket, void *buffer, u32 maxLen) {
    if (!buffer || !maxLen) return 0; // no buffer to read
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    if (!sockData) return -0x10;
    if (!S.core->IsHandleValid(sockData->hTransportSocket)) {
        sockData->hTransportSocket = 0;
        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        return -0x20;
    }
    LTTlsSessionContext *sessionCtx = sockData->sessionCtx;
    if (!sessionCtx) {
        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        return -0x30;
    }

    u8 *p = buffer;
    LTTlsAppRxData *data = NULL;
    u32 copyLen = 0;
    u32 leftLen = 0;
    S.mutex->API->Lock(S.mutex);
    while (!LTList_IsEmpty(&sessionCtx->tlsAppRxBuf) && maxLen > 0) {
        // get first record in list
        data = LT_CONTAINER_OF(sessionCtx->tlsAppRxBuf.pNext, LTTlsAppRxData, node);
        leftLen = data->len - data->alreadyRead;
        if (leftLen > maxLen) leftLen = maxLen;
        lt_memcpy(p, data->data + data->alreadyRead, leftLen);
        maxLen -= leftLen;
        p += leftLen;
        data->alreadyRead += leftLen;
        copyLen += leftLen;

        if (data->alreadyRead == data->len) {
            LTList_Remove(&data->node);
            lt_free(data->data);
            lt_free(data);
        }
    }
    S.mutex->API->Unlock(S.mutex);

    S.core->ReleaseHandlePrivateData(hSocket, sockData);
    return copyLen;
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
    TlsSocketData *pSocketData = S.core->ReserveHandlePrivateData(hSocket);
    lt_strncpyTerm(spec, pSocketData->spec, len);
    S.core->ReleaseHandlePrivateData(hSocket, pSocketData);
}

/** End of LTSocket interface functions ************************************/

static bool AppendCaKey(const char *caName, LTTlsOptions *options) {
    LTPublicKey newKey = {};
    do {
        LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
        bool bResult = keyManager && keyManager->API->GetCaPublicKey(caName, &newKey);
        lt_destroyobject(keyManager);
        if (!bResult) break;

        LTPublicKey *newCaKeys = lt_realloc((LTPublicKey *)options->caKeys, sizeof(LTPublicKey) * (options->caKeyCount + 1));
        if (!newCaKeys) break;

        newCaKeys[options->caKeyCount] = newKey;
        options->caKeys = newCaKeys;
        options->caKeyCount += 1;

        return true;
    } while(0);

    if (newKey.key)   lt_free(newKey.key);
    if (newKey.keyId) lt_free(newKey.keyId);

    return false;
}

static bool LoadCert(const char *certName, u8 **certData, u16 *certLen) {
    LTDriverCryptoCertManager *certManager = lt_createobject(LTDriverCryptoCertManager);
    bool bRet = false;
    const char *errMsg = NULL;
    do {
        if (!certManager) { errMsg = "fail cert manager"; break; }
        if (!certManager->API->GetCertificate(certName, NULL, certLen)) { errMsg = "fail cert size"; break; }
        if (*certLen == 0) { errMsg = "zero cert len"; break; }
        *certData = lt_malloc(*certLen);
        if (!*certData) { errMsg = "oom cert data"; break; }
        if (!certManager->API->GetCertificate(certName, *certData, certLen)) { errMsg = "fail cert data"; break; }
        bRet = true;
    } while (false);
    lt_destroyobject(certManager);
    if (!bRet) {
        if (*certData) {
            lt_free(*certData);
            *certData = NULL;
        }
        LTLOG_YELLOWALERT("cert.load.error", "%s :%s", certName, errMsg);
    }
    return bRet;
}

static bool LoadPrivateKey(const char *keyName, LTDriverCrypto_KeyReference *privateKeyReference) {
    LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
    bool bRet = false;
    const char *errMsg = NULL;
    do {
        if (!keyManager) { errMsg = "fail key manager"; break; }
        LTDriverCrypto_KeyReference *keyReference = keyManager->API->GetPrivateKeyReference(keyName);
        if (!keyReference) { errMsg = "fail key reference"; break; }
        lt_memcpy(privateKeyReference, keyReference, sizeof(LTDriverCrypto_KeyReference));
        keyManager->API->FreeKeyReference(keyReference);
        bRet = true;
    } while (false);
    lt_destroyobject(keyManager);
    if (!bRet) LTLOG_YELLOWALERT("key.load.error", "%s :%s", keyName, errMsg);
    return bRet;
}

static bool FormatAlpn(const char *alpnStr, LTTlsAlpnExt **ppAlpn) {
    LT_SIZE alpnStrLen = lt_strlen(alpnStr);
    LTTlsAlpnExt *alpn = NULL;
    u8 *protocolNames  = NULL;
    do {
        alpn = lt_malloc(sizeof(LTTlsAlpnExt));
        if (!alpn) break;

        protocolNames  = lt_malloc(alpnStrLen + 1);
        if (!protocolNames) break;

        protocolNames[0] = alpnStrLen;
        lt_memcpy(&protocolNames[1], alpnStr, alpnStrLen);
        *alpn = (LTTlsAlpnExt) {
            .alpnLen = alpnStrLen + 1,
            .protocolNames = protocolNames,
        };

        *ppAlpn = alpn;

        return true;
    } while(0);

    if (alpn)          lt_free(alpn);
    if (protocolNames) lt_free(protocolNames);

    return false;
}

static bool OnSocketSpecToken(LTTokenizer *tokenizer, const char *token, char matchedSentinel, void *clientData) {
    SocketSpecParserContext *context = clientData;
    switch (context->state) {
        case kSocketSpecParser_Protocol:
            if (lt_strcmp(token, "tls") == 0) {
                context->state = kSocketSpecParser_Key;
                S.tokenizer->SetSentinels(tokenizer, ":");
            } else {
                context->state = kSocketSpecParser_Error;
                return false;
            }
            break;

        case kSocketSpecParser_Key:
            if (lt_strcmp(token, "servername") == 0) {
                context->paramType = kSocketSpec_TlsParamType_Servername;
            } else if (lt_strcmp(token, "cakeys") == 0) {
                context->paramType = kSocketSpec_TlsParamType_CAKeys;
            } else if (lt_strcmp(token, "clientcert") == 0) {
                context->paramType = kSocketSpec_TlsParamType_ClientCert;
            } else if (lt_strcmp(token, "alpn") == 0) {
                context->paramType = kSocketSpec_TlsParamType_Alpn;
            } else {
                context->paramType = kSocketSpec_TlsParamType_Unknown;
            }
            context->state = kSocketSpecParser_Value;
            S.tokenizer->SetSentinels(tokenizer, " ,");
            break;

        case kSocketSpecParser_Value:
            if (token[0] != '\0') {
                bool bResult = false;
                switch (context->paramType) {
                    case kSocketSpec_TlsParamType_Servername:
                        context->options->serverName = lt_strdup(token);
                        bResult = (context->options->serverName != NULL);
                        if (!bResult) PLOG("sck.spec.srv", "no server name");
                        break;

                    case kSocketSpec_TlsParamType_CAKeys:
                        bResult = AppendCaKey(token, context->options);
                        if (!bResult) PLOG("sck.spec.ca", "no ca key");
                        break;

                    case kSocketSpec_TlsParamType_ClientCert:
                        bResult = LoadCert(token, &context->options->clientCertificate, &context->options->clientCertificateLen) &&
                                  LoadPrivateKey(token, &context->options->clientPrivateKeyReference);
                        if (!bResult) PLOG("sck.spec.crt", "no client cert");
                        break;

                    case kSocketSpec_TlsParamType_Alpn:
                        bResult = FormatAlpn(token, &context->options->alpnExt);
                        if (!bResult) PLOG("sck.spec.crt", "no alpn");
                        break;

                    case kSocketSpec_TlsParamType_Unknown:
                        bResult = true;
                        break;

                    default:
                        PLOG("sck.spec.unknown", "unknown type %u", (u8)context->paramType);
                        bResult = false;
                        break;

                }
                if (!bResult) {
                    context->state = kSocketSpecParser_Error;
                    return false;
                }
                if (matchedSentinel == ' ') {
                    context->state = kSocketSpecParser_Key;
                    S.tokenizer->SetSentinels(tokenizer, ":");
                }
            }
            break;

        case kSocketSpecParser_Error:
        default:
            return false;
    }
    return true;
}

static bool ParseSocketSpec(const char *socketSpec, LTTlsOptions *tlsOptions) {
    lt_memset(tlsOptions, 0, sizeof(*tlsOptions));
    SocketSpecParserContext pcontext = {
        .state   = kSocketSpecParser_Protocol,
        .options = tlsOptions,
    };
    LTTokenizer *pTokenizer = S.tokenizer->CreateTokenizer(" ", OnSocketSpecToken, &pcontext);
    S.tokenizer->ProcessData(pTokenizer, socketSpec, lt_strlen(socketSpec), false);
    S.tokenizer->DestroyTokenizer(pTokenizer);

    if (pcontext.state == kSocketSpecParser_Error) {
        PLOG("sspec.prs.err", "Socket spec parsing failed");
        return false;
    }
    if (!tlsOptions->serverName) {
        PLOG("sspec.prs.no.sn", "Socket spec is missing servername");
        return false;
    }
    if (!tlsOptions->caKeys || !tlsOptions->caKeyCount) {
        PLOG("sspec.prs.no.cak", "Socket spec is missing cakeys");
        return false;
    }
    return true;
}

static LTSocket LTNetTls_WrapSocket(LTSocket hTransportSocket, const char *socketSpec, LTEvent transportSocketEvent, LTSocket_EventProc procFunc, void *procData, LTAtomic *socketWriteCount, LTAtomic *socketWriteFailCount) {
    if (!hTransportSocket || !socketSpec) return 0;
    LT_UNUSED(socketSpec);

    LTTlsOptions options;
    if (!ParseSocketSpec(socketSpec, &options)) {
        PLOG("parse.sspec.err", "Failed to parse socket spec");
        return 0;
    }

    LTSocket hSocket = S.core->CreateHandle((LTInterface *)&s_ILTSocket, sizeof(TlsSocketData));
    if (!hSocket) return 0;
    TlsSocketData *sockData = S.core->ReserveHandlePrivateData(hSocket);
    do {
        lt_memset(sockData, 0, sizeof(TlsSocketData));

        sockData->socketWriteCount = socketWriteCount;
        sockData->socketWriteFailCount = socketWriteFailCount;
        sockData->hSocket = hSocket;
        sockData->hTransportSocket = hTransportSocket;
        sockData->spec = lt_strdup(socketSpec);
        sockData->event = S.core->CreateEvent(&SocketEventArgs, DispatchOnSocketEvent, NULL, NULL, NULL);

        // create TLS session context
        sockData->sessionCtx = CreateSession(hTransportSocket, &options);
        if (!sockData->sessionCtx) {
            LTLOG_REDALERT("ctx.create.fail", "Session context create fails");
            break;
        }

        // Start TLS timer.
        S.iThread->SetTimer(sockData->sessionCtx->hThread, LTTime_Milliseconds(LTTLS_TIMEOUT), OnTlsTimeout, NULL, LTHANDLE_TO_VOIDPTR(hSocket));

        S.iEvent->RegisterForEvent(transportSocketEvent, OnSocketEvent, NULL, sockData, false);
        S.iEvent->RegisterForEvent(sockData->event, procFunc, NULL, procData, false);

        S.core->ReleaseHandlePrivateData(hSocket, sockData);
        return hSocket;
    } while (0);

    S.core->ReleaseHandlePrivateData(hSocket, sockData);
    lt_destroyhandle(hSocket);
    return 0;
}

/**
 * @brief Check if required cryptos are available
 *
 * @return true if required cryptos are available
 */
static bool CheckCrypto(void) {
    if (!S.crypto) return false;
    LTSystemCryptoOptions o;
    S.crypto->GetOptions(&o);
    return o.enabled    &&
           o.random     &&
           o.aes128Gcm  &&
           o.sha256     &&
           o.hmacSha256 &&
           o.seqSha256  &&
           o.ecdsa      &&
           o.ecdhe;
}

static void ShutdownNetTls(void) {
    lt_closelibrary(S.x509);
    lt_closelibrary(S.crypto);
    lt_closelibrary(S.tokenizer);
    lt_destroyobject(S.mutex);
    lt_memset(&S, 0, sizeof(S));
}

/* LTNetTls library initialization */
static bool LTNetTlsImpl_LibInit(void) {
    lt_memset(&S, 0, sizeof(S));
    // Get the necessary interfaces
    S.core   = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, S.core);
    S.iEvent  = lt_getlibraryinterface(ILTEvent, S.core);

    do {
        if (!(S.mutex = lt_createobject(LTMutex))) break;
        if (!(S.crypto = lt_openlibrary(LTSystemCrypto))) break;
        if (!(S.tokenizer = lt_openlibrary(LTUtilityTokenizer))) break;
        if (!(S.x509 = lt_openlibrary(LTNetX509))) break;
        if (!CheckCrypto()) break;
        return true;
    } while (0);

    ShutdownNetTls();
    return false;
}

static void LTNetTlsImpl_LibFini(void) {
    ShutdownNetTls();
}

/* LTNetTls library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTNetTls) {
    .WrapSocket            = &LTNetTls_WrapSocket,
} LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTSocket, LTSocket_DestroySocket)
    .ConnectSocket    = &LTSocket_ConnectSocket,
    .DisconnectSocket = &LTSocket_DisconnectSocket,
    .WriteSocket      = &LTSocket_WriteSocket,
    .ReadSocket       = &LTSocket_ReadSocket,
    .GetSocketSpec    = &LTSocket_GetSocketSpec,
LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTNetTls, (ILTSocket));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-22   gallienus   created
 */
