/*******************************************************************************
 * source/lt/net/stun/LTNetStun.c - Implementation of STUN agent interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/stun/LTNetStun.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "md5.h"

DEFINE_LTLOG_SECTION("stun");

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTNetStun, (LTSystemCrypto) (LTUtilityByteOps));

enum {
    kStunMagicCookieValue           = 0x2112A442,
    kStunMethod_Mask                = 0x3EEF,
    kStunClass_Mask                 = 0x0110,
    kHMAC_SHA1_Length               = 20,
    kHMAC_SHA256_LengthMin          = 16,
    kHMAC_SHA256_LengthMax          = 32,
    kErrorCode_LengthMax            = 763,
    kFingerprintXorValue            = 0x5354554e,
    kDefaultRetransmissionTimeoutMs = 250,
    kMaxRetransmissionTimeoutMs     = 8000,
    kMaxRetransmissionCount         = 7,
    kLTStunPasswordAlgorithmAttrLen = 4,
    kTransactionHistoryTimeoutSec   = 40, // transaction history timeout per RFC-8489 section 6.2.1
    kTransactionPurgeIntervalSec    = 5,
};

typedef enum {
    kMessageIntegrityAlgo_SHA1,
    kMessageIntegrityAlgo_SHA256,
} MessageIntegrityAlgo;

typedef struct LTStunAgentImpl LTStunAgentImpl;
typedef struct LTStunTransportImpl LTStunTransportImpl;

typedef struct {
    u16 nMessageType;
    u16 nMessageLen;
    u32 nMagicCookie;
    u8  transactionId[kLTStunTransactionIdLen];
} __attribute__((packed)) StunPacketHdr;

typedef struct {
    u16 nType;
    u16 nLength;
} __attribute__((packed)) StunAttrHdr;

typedef struct {
    u8  nPadding;
    u8  nFamily;
    u16 nXPort;
    u32 nXAddr;
} __attribute__((packed)) StunAttrXorAddressV4;

typedef enum {
    kLTBindingState_Idle,
    kLTBindingState_WaitingForResponse,
} LTBindingState;

typedef struct {
    u16 nType;
    u16 nLength;
    u8  data[0];
} StunAttribute;

typedef struct {
    LTStunPasswordAlgorithm passwordAlgorithm;
    const u8               *realm;
    u32                     realmLen;
    const u8               *username;
    u32                     usernameLen;
    const u8               *password;
    u32                     passwordLen;
} StunAuthContext;

typedef struct {
    LTStunTransportImpl *transport;
    LTStunTransactionId  transactionId;
    void                *clientData;
    LTNetIpv4Endpoint    endpoint;
    void                *packetData;
    u32                  packetSize;
    u32                  curRetransmissionTimeout;
    u32                  numRetransmissions;
} StunTransaction;

typedef struct {
    LTStunTransactionId  transactionId;
    LTTime               purgeTime;
} CompletedStunTransaction;

typedef struct {
    LTStunTransportImpl *transport;
    LTNetIpv4Endpoint    senderEndpoint;
    u32                  packetSize;
    u8                   packetData[0];
} ReceivedPacketData;

/*______________________________________________
  LTStunTransport object private data members */
typedef_LTObjectImpl(LTStunTransport, LTStunTransportImpl) {
    LTStunAgentImpl    *agent;
    LTSocket            hSocket;
    LTNetIpv4Endpoint   endpoint;
    LTEvent             hMessageEvent;
    void               *pClientData;
    LTStunTransport_UnhandledDatagramProc *pCallback;
} LTOBJECT_API;

/*__________________________________________
  LTStunAgent object private data members */
typedef_LTObjectImpl(LTStunAgent, LTStunAgentImpl) {
    LTSocket            hSocket;
    LTThread            hThread;
    LTBindingState      nBindingState;
    LTArray            *pPendingTransactions;
    LTArray            *pCompletedTransactions;
    LTArray            *credentials;
    LTArray            *transports;
} LTOBJECT_API;

/*____________________________________________
  LTStunMessage object private data members */
typedef_LTObjectImpl(LTStunMessage, LTStunMessageImpl) {
    LTStunAgentImpl           *agent;
    LTStunTransportImpl       *transport;             /**< The STUN transport over which the message was sent/received */
    LTStunMethod               nMethod;               /**< The STUN request/response method */
    LTStunClass                nClass;                /**< The STUN message class */
    LTStunTransactionId        transactionId;         /**< A unique identifier for the message */
    u32                        flags;
    bool                       bHasErrorCode;         /**< True if the message contains an error-code attribute */
    u32                        nErrorCode;            /**< The error numeric error code */
    char                      *pErrorReason;          /**< The error reason string */
    LTNetIpv4Endpoint          endpoint;              /**< The remote endpoint to/from which the message was sent/received */
    void                      *clientData;            /**< Client data which will be passed to the status callback when the request state changes */
    LTArray                   *pAttributes;           /**< STUN message attributes for the message */
    u32                        retransmissionTimeout; /**< Message retransmission timeout in milliseconds */
} LTOBJECT_API;

typedef struct {
    LTNetIpv4Endpoint             endpoint;
    char                         *username;
    char                         *password;
    LTStunCredentialsUsageContext usageContext;
} StunCredentials;

static LTCore              *s_pCore      = NULL;
static ILTEvent            *s_pEvent     = NULL;
static ILTThread           *s_pThread    = NULL;

static const LTArgsDescriptor s_MsgReceivedEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_pointer}};

static void OnRetransmissionTimerExpired(void *clientData);

/*____________________________________
  LTStunMessage object constructors */
static bool
LTStunMessageImpl_ConstructObject(LTStunMessageImpl *message) {
    message->retransmissionTimeout = kDefaultRetransmissionTimeoutMs;
    message->pAttributes = lt_createobject_typed(LTArray, List);
    return true;
}

static void
LTStunMessageImpl_DestructObject(LTStunMessageImpl *message) {
    if (message->pErrorReason) lt_free(message->pErrorReason);
    for (u32 i = 0; i < message->pAttributes->API->GetCount(message->pAttributes); ++i) {
        StunAttribute *attr = message->pAttributes->API->Get(message->pAttributes, i, NULL);
        lt_free(attr);
    }
    lt_destroyobject(message->pAttributes);
}

/*_____________________________________
  LTStunMessage public api functions */
static void
LTStunMessageImpl_InitRequest(LTStunMessageImpl *message, LTNetIpv4Endpoint endpoint, u16 method, LTStunMessageFlags flags, void *clientData) {
    message->nClass      = kLTStunClass_Request;
    message->nMethod     = method;
    message->endpoint    = endpoint;
    message->flags       = flags;
    message->clientData = clientData;
    LT_GetLTUtilityByteOps()->GenRandomBytes(message->transactionId.value, kLTStunTransactionIdLen);
}

static void
LTStunMessageImpl_InitResponse(LTStunMessageImpl *message, LTStunMessageImpl *request) {
    message->nClass        = kLTStunClass_Response;
    message->nMethod       = request->nMethod;
    message->flags         = request->flags & ~(kLTStunMessageFlags_FingerprintValid | kLTStunMessageFlags_MacValid);
    message->endpoint      = request->endpoint;
    message->transport     = request->transport;
    message->transactionId = request->transactionId;
}

static void
LTStunMessageImpl_SetRetransmissionTimeout(LTStunMessageImpl *message, u32 retransmissionTimeout) {
    message->retransmissionTimeout = retransmissionTimeout;
}

static u32
LTStunMessageImpl_GetClass(LTStunMessageImpl *message) {
    return message->nClass;
}

static u32
LTStunMessageImpl_GetMethod(LTStunMessageImpl *message) {
    return message->nMethod;
}

static u32
LTStunMessageImpl_GetErrorCode(LTStunMessageImpl *message) {
    return message->nErrorCode;
}

static const char *
LTStunMessageImpl_GetErrorReason(LTStunMessageImpl *message) {
    return message->pErrorReason;
}

static LTNetIpv4Endpoint
LTStunMessageImpl_GetEndpoint(LTStunMessageImpl *message) {
    return message->endpoint;
}

static u32
LTStunMessageImpl_GetFlags(LTStunMessageImpl *message) {
    return message->flags;
}

static void
LTStunMessageImpl_SetAttribute(LTStunMessageImpl *message, u32 attrType, LTStunAttrFormat format, ...) {
    u32 size;
    lt_va_list args;
    lt_va_start(args, format);
    switch (format) {
        case kStunAttrFormat_void:          size = 0;                                            break;
        case kStunAttrFormat_u32:           size = 4;                                            break;
        case kStunAttrFormat_u64:           size = 8;                                            break;
        case kStunAttrFormat_bytes:         lt_va_arg(args, void*); size = lt_va_arg(args, u32); break;
        case kStunAttrFormat_XorEndpointV4: size = sizeof(StunAttrXorAddressV4);                 break;
        default: LTLOG_YELLOWALERT("bad.fmt", "Invalid attribute format: %lu", LT_Pu32(format)); return;
    }
    lt_va_end(args);

    StunAttribute *attr = lt_malloc(sizeof(StunAttribute) + size);
    if (attr == NULL) return;
    attr->nType = attrType;
    attr->nLength = size;

    lt_va_start(args, format);
    switch (format) {
        case kStunAttrFormat_void:                                                       break;
        case kStunAttrFormat_u32:           *((u32*)&attr->data) = lt_va_arg(args, u32); break;
        case kStunAttrFormat_u64:           *((u64*)&attr->data) = lt_va_arg(args, u64); break;
        case kStunAttrFormat_bytes: {
            void *buf = lt_va_arg(args, void*);
            lt_memcpy(attr->data, buf, size);
            break;
        }
        case kStunAttrFormat_XorEndpointV4: {
            LTNetIpv4Endpoint endpoint = lt_va_arg(args, LTNetIpv4Endpoint);
            StunAttrXorAddressV4 *xaddr = (void*)&attr->data;
            *xaddr = (StunAttrXorAddressV4) {
                .nPadding = 0,
                .nFamily = kLTStunAddressFamily_IPv4,
                .nXPort = LT_HTONS(endpoint.port ^ (kStunMagicCookieValue >> 16)),
                .nXAddr = LT_HTONL(LT_NTOHL(endpoint.address) ^ kStunMagicCookieValue),
            };
            break;
        }
    }
    lt_va_end(args);

    message->pAttributes->API->Append(message->pAttributes, attr);
}

static bool
LTStunMessageImpl_GetAttribute(LTStunMessageImpl *message, u32 attrType, LTStunAttrFormat format, ...) {
    for (u32 i = 0; i < message->pAttributes->API->GetCount(message->pAttributes); ++i) {
        StunAttribute *attr = message->pAttributes->API->Get(message->pAttributes, i, NULL);
        if (attr->nType == attrType) {
            lt_va_list args;
            lt_va_start(args, format);
            switch (format) {
                case kStunAttrFormat_void:
                    break;
                case kStunAttrFormat_u32: {
                    if (attr->nLength != sizeof(u32)) return false;
                    u32 *out = lt_va_arg(args, u32*);
                    if (out) *out = LT_NTOHL(*((u32*)attr->data));
                    break;
                }
                case kStunAttrFormat_u64: {
                    if (attr->nLength != sizeof(u64)) return false;
                    u64 *out = lt_va_arg(args, u64*);
                    if (out) *out = LT_NTOHLL(*((u64*)attr->data));
                    break;
                }
                case kStunAttrFormat_bytes: {
                    u8 **buf = lt_va_arg(args, u8 **);
                    u32 *size = lt_va_arg(args, u32 *);
                    if (buf && size) {
                        *buf  = attr->data;
                        *size = attr->nLength;
                    }
                    break;
                }
                case kStunAttrFormat_XorEndpointV4: {
                    if (attr->nLength != sizeof(StunAttrXorAddressV4)) return false;
                    LTNetIpv4Endpoint *endpoint = lt_va_arg(args, LTNetIpv4Endpoint*);
                    if (endpoint) {
                        StunAttrXorAddressV4 *xaddr = (StunAttrXorAddressV4 *)&attr->data;
                        /* Leave this in network byte order since it's what LT networking APIs expect */
                        *endpoint = (LTNetIpv4Endpoint) {
                            .address = xaddr->nXAddr ^ LT_HTONL(kStunMagicCookieValue),
                            .port    = LT_NTOHS(xaddr->nXPort) ^ (kStunMagicCookieValue >> 16),
                        };
                    }
                    break;
                }
            }
            lt_va_end(args);
            return true;
        }
    }

    return false;
}

static void *
LTStunMessageImpl_GetClientData(LTStunMessageImpl *message) {
    return message->clientData;
}

/*_______________________________________
  LTStunMessage LTObjectApi definition */
define_LTObjectImplPublic(LTStunMessage, LTStunMessageImpl,
    InitRequest,
    InitResponse,
    SetRetransmissionTimeout,
    GetClass,
    GetMethod,
    GetErrorCode,
    GetErrorReason,
    GetEndpoint,
    GetFlags,
    SetAttribute,
    GetAttribute,
    GetClientData,
);

/*_______________________________________
  LTStunAgent private helper functions */
static void
DispatchReceivedMessageCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTStunTransport_MessageReceivedProc *pCallback = (LTStunTransport_MessageReceivedProc *)pEventProc;
    LTStunTransport *transport = LTArgs_pointerAt(0, pEventArgs);
    LTStunMessage *message     = LTArgs_pointerAt(1, pEventArgs);
    pCallback(transport, message, pEventProcClientData);
}

static void
DispatchReceivedMessageCompleteCB(LTEvent hEvent, LTArgs *pEventArgs) {
    LT_UNUSED(hEvent);
    LTStunMessage *message = LTArgs_pointerAt(1, pEventArgs);
    lt_destroyobject(message);
}

static bool
CalculateMessageIntegrity(StunAuthContext *authContext, MessageIntegrityAlgo algo, StunPacketHdr *pMsg, StunAttrHdr *pMsgIntegrityAttr, u32 nAttrLen, u8 *pMacOut, u32 nMacOutLen) {
    if (!pMsg || !pMsgIntegrityAttr || !pMacOut) return false;

    u8 *key;
    u32 keyLen;
    if (authContext->realm) {
        switch (authContext->passwordAlgorithm) {
            case kLTStunPasswordAlgorithm_SHA256: {
                // Long-term HMAC key generation per RFC 8489 section 18.5.1.2
                keyLen = SHA256_HASH_LENGTH;
                key = lt_malloc(keyLen);
                if (!key) {
                    LTLOG_YELLOWALERT("lthk.alloc.fail", "Failed to allocate buffer for long-term HMAC key");
                    return false;
                }
                LT_SHA256_CTX *keyHashCtx =  LT_GetLTSystemCrypto()->CreateSeqSHA256();
                if (!keyHashCtx) {
                    lt_free(key);
                    LTLOG_YELLOWALERT("lthk.create.fail", "Failed to create SHA256 context");
                    return false;
                }
                LT_GetLTSystemCrypto()->UpdateSeqSHA256(keyHashCtx, authContext->username, authContext->usernameLen);
                LT_GetLTSystemCrypto()->UpdateSeqSHA256(keyHashCtx, (u8 *)":", 1);
                LT_GetLTSystemCrypto()->UpdateSeqSHA256(keyHashCtx, authContext->realm, authContext->realmLen);
                LT_GetLTSystemCrypto()->UpdateSeqSHA256(keyHashCtx, (u8 *)":", 1);
                LT_GetLTSystemCrypto()->UpdateSeqSHA256(keyHashCtx, authContext->password, authContext->passwordLen);
                LT_GetLTSystemCrypto()->FinishSeqSHA256(keyHashCtx, key);
                LT_GetLTSystemCrypto()->DestroySeqSHA256(keyHashCtx);
                break;
            }

            case kLTStunPasswordAlgorithm_MD5:
                // Long-term HMAC key generation per RFC 8489 section 18.5.1.2
                keyLen = 16;
                key = lt_malloc(keyLen);
                if (!key) {
                    LTLOG_YELLOWALERT("lthk.alloc.fail", "Failed to allocate buffer for long-term HMAC key");
                    return false;
                }
                MD5Context *keyHashCtx = lt_malloc(sizeof(MD5Context));
                md5Init(keyHashCtx);
                md5Update(keyHashCtx, authContext->username, authContext->usernameLen);
                md5Update(keyHashCtx, (u8 *)":", 1);
                md5Update(keyHashCtx, authContext->realm, authContext->realmLen);
                md5Update(keyHashCtx, (u8 *)":", 1);
                md5Update(keyHashCtx, authContext->password, authContext->passwordLen);
                md5Finalize(keyHashCtx);
                lt_memcpy(key, keyHashCtx->digest, 16);
                lt_free(keyHashCtx);
                break;

            default:
                LTLOG_YELLOWALERT("bad.psw.algo", "Unsupported password algorithm: %lu", LT_Pu32(authContext->passwordAlgorithm));
                return false;
        }
    } else {
        // Short-term HMAC key generation per RFC 8489 section 9.1.1
        keyLen = authContext->passwordLen;
        key = lt_malloc(keyLen);
        if (!key) {
            LTLOG_YELLOWALERT("sthk.alloc.fail", "Failed to allocate buffer for short-term HMAC key");
            return false;
        }
        lt_memcpy(key, authContext->password, keyLen);
    }

    u16 nTextLen = (u8 *)pMsgIntegrityAttr - (u8 *)pMsg;
    u16 nOldMsgLen = pMsg->nMessageLen;
    pMsg->nMessageLen = LT_HTONS(nTextLen - sizeof(StunPacketHdr) + sizeof(StunAttrHdr) + nAttrLen);

    LTSystemCryptoResult result = kLTSystemCrypto_Result_Error;
    if ((algo == kMessageIntegrityAlgo_SHA1) && (nMacOutLen >= SHA1_HASH_LENGTH)) {
        result = LT_GetLTSystemCrypto()->GenHMACSHA1(key, keyLen, (u8 *)pMsg, nTextLen, pMacOut);
    } else if ((algo == kMessageIntegrityAlgo_SHA256) && (nMacOutLen >= SHA256_HASH_LENGTH)) {
        result = LT_GetLTSystemCrypto()->GenHMACSHA256(key, keyLen, (u8 *)pMsg, nTextLen, pMacOut);
    }
    lt_free(key);

    /* Restore the old message length */
    pMsg->nMessageLen = nOldMsgLen;

    return (result == kLTSystemCrypto_Result_Ok);
}

static bool
VerifyMessageIntegrity(StunAuthContext *authContext, MessageIntegrityAlgo algo, StunPacketHdr *pMsg, StunAttrHdr *pMsgIntegrityAttr, u8 *pRecvdMac, u32 nRecvdMacLen) {
    u8 calcMac[kHMAC_SHA256_LengthMax];
    if (CalculateMessageIntegrity(authContext, algo, pMsg, pMsgIntegrityAttr, nRecvdMacLen, calcMac, sizeof(calcMac))) {
        return lt_memcmp(pRecvdMac, calcMac, nRecvdMacLen) == 0;
    }
    return false;
}

static StunTransaction *
PopPendingTransaction(LTStunAgentImpl *agent, u8 *pTransactionId) {
    LTArray *pPendingTransactions = agent->pPendingTransactions;
    for (u32 i = 0; i < pPendingTransactions->API->GetCount(pPendingTransactions); ++i) {
        StunTransaction *transaction = pPendingTransactions->API->Get(pPendingTransactions, i, NULL);
        if (lt_memcmp(transaction->transactionId.value, pTransactionId, kLTStunTransactionIdLen) == 0) {
            pPendingTransactions->API->Remove(pPendingTransactions, i);
            return transaction;
        }
    }
    return NULL;
}

static void
DestroyTransaction(LTStunAgentImpl *agent, StunTransaction *transaction) {
    CompletedStunTransaction completedTransaction = {
        .purgeTime     = LTTime_Add(LT_GetCore()->GetKernelTime(), LTTime_Seconds(kTransactionHistoryTimeoutSec)),
        .transactionId = transaction->transactionId,
    };
    agent->pCompletedTransactions->API->Append(agent->pCompletedTransactions, &completedTransaction);

    s_pThread->KillTimer(agent->hThread, OnRetransmissionTimerExpired, transaction);
    lt_free(transaction->packetData);
    lt_free(transaction);
}

static bool
IsCompletedTransaction(LTStunAgentImpl *agent, LTStunTransactionId transactionId) {
    LTArray *pCompletedTransactions = agent->pCompletedTransactions;
    for (u32 i = 0; i < pCompletedTransactions->API->GetCount(pCompletedTransactions); ++i) {
        CompletedStunTransaction *completedTransaction = pCompletedTransactions->API->Get(pCompletedTransactions, i, NULL);
        if (lt_memcmp(&completedTransaction->transactionId, &transactionId, sizeof(LTStunTransactionId)) == 0) {
            return true;
        }
    }
    return false;
}

static void
PurgeCompletedTransactionsTimerProc(void *clientData) {
    LTStunAgentImpl *agent = clientData;
    LTArray *pCompletedTransactions = agent->pCompletedTransactions;
    while (pCompletedTransactions->API->GetCount(pCompletedTransactions)) {
        CompletedStunTransaction *completedTransaction = pCompletedTransactions->API->Get(pCompletedTransactions, 0, NULL);

        // completed transactions are inherently in chronological order in the list, so bail when the first
        // non-purgeable record is encountered.
        if (LTTime_IsLessThan(LT_GetCore()->GetKernelTime(), completedTransaction->purgeTime)) return;

        pCompletedTransactions->API->Remove(pCompletedTransactions, 0);
    }
}

static StunCredentials *
FindCredentials(LTStunAgentImpl *agent, LTStunCredentialsUsageContext usageContext, LTNetIpv4Endpoint endpoint, const u8 *username, u32 usernameLen) {
    StunCredentials *matchedCredentials = NULL;
    u32 maxMatchScore = 0;
    for (u32 i = 0; i < agent->credentials->API->GetCount(agent->credentials); ++i) {
        StunCredentials *credentials = agent->credentials->API->Get(agent->credentials, i, NULL);
        if (credentials->usageContext & usageContext) {
            u32 matchScore = 0;
            if (username == NULL)                                                           matchScore += 1;
            else if (lt_strncmp(credentials->username, (char *)username, usernameLen) == 0) matchScore += 2;
            else continue;

            if (LTNetIpv4Endpoint_IsAny(credentials->endpoint))                             matchScore += 1;
            else if (LTNetIpv4Endpoint_IsEqual(credentials->endpoint, endpoint))            matchScore += 2;
            else continue;

            if (matchScore > maxMatchScore) {
                maxMatchScore = matchScore;
                matchedCredentials = credentials;
            }
        }
    }
    return matchedCredentials;
}

static void
PrintCredentials(LTStunAgentImpl *agent) {
    u32 count = agent->credentials->API->GetCount(agent->credentials);
    LTLOG_SERVER("crdl.size", "%lu", LT_Pu32(count));
    for (u32 i = 0; i < count; ++i) {
        StunCredentials *credentials = agent->credentials->API->Get(agent->credentials, i, NULL);
        LTLOG_SERVER("crdl", "username %s uc %x ep %lu.%lu.%lu.%lu:%lu",
                    credentials->username,
                    credentials->usageContext,
                    LT_Pu32((credentials->endpoint.address >> 0) & 0xFF),
                    LT_Pu32((credentials->endpoint.address >> 8) & 0xFF),
                    LT_Pu32((credentials->endpoint.address >> 16) & 0xFF),
                    LT_Pu32((credentials->endpoint.address >> 24) & 0xFF),
                    LT_Pu32(credentials->endpoint.port));
    }
}

static void
HandleReceivedPacketProc(void *clientData) {
    ReceivedPacketData *rxPacket = clientData;
    LTStunAgentImpl *agent = s_pThread->GetThreadSpecificClientData(s_pThread->GetCurrentThread(), "agent");

    u8 *pPacketData = rxPacket->packetData;
    u32 nPacketSize = rxPacket->packetSize;
    if (nPacketSize < sizeof(StunPacketHdr)) return;

    StunPacketHdr *pStunHeader = (StunPacketHdr *)pPacketData;
    pPacketData += sizeof(StunPacketHdr);
    nPacketSize -= sizeof(StunPacketHdr);

    u16 nMessageType = LT_NTOHS(pStunHeader->nMessageType);
    u16 nMessageLen  = LT_NTOHS(pStunHeader->nMessageLen);
    u32 nMagicCookie = LT_NTOHL(pStunHeader->nMagicCookie);

    u16 nMessageMSBits = nMessageType & 0xC000;
    u16 nMessageMethod = nMessageType & kStunMethod_Mask;
    u16 nMessageClass  = nMessageType & kStunClass_Mask;

    if (nMessageMSBits != 0) {
        LTLOG_YELLOWALERT("bad.msg.msb", "Invalid top two bits: 0x%04X", nMessageMSBits);
        return;
    } else if (nMagicCookie != kStunMagicCookieValue) {
        LTLOG_YELLOWALERT("bad.cookie", "Invalid magic cookie: 0x%08lX", LT_Pu32(nMagicCookie));
        return;
    } else if (nMessageLen != nPacketSize) {
        LTLOG_YELLOWALERT("bad.length", "Invalid message length: %lu", LT_Pu32(nMessageLen));
        return;
    }

    LTStunMessageImpl *message = (LTStunMessageImpl*)lt_createobject(LTStunMessage);
    if (!message) {
        LTLOG_YELLOWALERT("hdlrx.alloc.err", "Failed to allocate message");
        return;
    }
    message->transport = rxPacket->transport;
    message->endpoint  = rxPacket->senderEndpoint;
    message->nMethod   = nMessageMethod;
    message->nClass    = nMessageClass;
    lt_memcpy(message->transactionId.value, pStunHeader->transactionId, kLTStunTransactionIdLen);

    if ((nMessageClass == kLTStunClass_Response) || (nMessageClass == kLTStunClass_Error)) {
        StunTransaction *transaction = PopPendingTransaction(agent, pStunHeader->transactionId);
        if (transaction) {
            if (transaction->transport != message->transport) {
                LTLOG_YELLOWALERT("txn.xport.mm", NULL);
                message->transport = transaction->transport;
            }
            message->clientData = transaction->clientData;
            DestroyTransaction(agent, transaction);
        } else if (IsCompletedTransaction(agent, message->transactionId)) {
            LTLOG_DEBUG("comp.txn.retrans", "Received retransmitted response for completed transaction. Ignoring.");
            goto err;
        } else {
            LTLOG_YELLOWALERT("no.pend.txn", "Unable to match STUN response to pending transaction. Ignoring.");
            goto err;
        }
    }

    LTStunCredentialsUsageContext usageContext = (message->nClass == kLTStunClass_Response) ?
        kLTStunCredentialsUsageContext_Local :
        kLTStunCredentialsUsageContext_Remote;
    StunCredentials *credentials = FindCredentials(agent, usageContext, rxPacket->senderEndpoint, NULL, 0);
    StunAuthContext authContext = {
        .passwordAlgorithm = kLTStunPasswordAlgorithm_MD5,
        .password          = credentials ? (u8 *)credentials->password : NULL,
        .passwordLen       = credentials ? lt_strlen(credentials->password) : 0,
    };

    while (nPacketSize >= sizeof(StunAttrHdr)) {
        StunAttrHdr *pAttrHeader = (StunAttrHdr *)pPacketData;
        pPacketData += sizeof(StunAttrHdr);
        nPacketSize -= sizeof(StunAttrHdr);

        u16 nAttrType   = LT_NTOHS(pAttrHeader->nType);
        u16 nAttrLength = LT_NTOHS(pAttrHeader->nLength);

        if (nPacketSize < nAttrLength) {
            LTLOG_YELLOWALERT("bad.len", "Packet size doesn't match attr length");
            goto err;
        }

        switch (nAttrType) {
            case kLTStunAttrType_Realm:
                if (nAttrLength >= kLTStunRealmAttrLenMax) {
                    LTLOG_YELLOWALERT("bad.realm.size", "Realm attribute is too long: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                authContext.realm = pPacketData;
                authContext.realmLen = nAttrLength;
                break;

            case kLTStunAttrType_Username: {
                if (nAttrLength >= kLTStunUsernameAttrLenMax) {
                    LTLOG_YELLOWALERT("bad.uname.size", "Username attribute is too long: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                authContext.username = pPacketData;
                authContext.usernameLen = nAttrLength;
                credentials = FindCredentials(agent, usageContext, rxPacket->senderEndpoint, pPacketData, nAttrLength);
                if (!credentials) {
                    LTLOG_YELLOWALERT("no.msg.auth", "Unknown username: %.*s uc %x ep %lu.%lu.%lu.%lu:%lu",
                                    nAttrLength, pPacketData, usageContext,
                                    LT_Pu32((rxPacket->senderEndpoint.address >> 0) & 0xFF),
                                    LT_Pu32((rxPacket->senderEndpoint.address >> 8) & 0xFF),
                                    LT_Pu32((rxPacket->senderEndpoint.address >> 16) & 0xFF),
                                    LT_Pu32((rxPacket->senderEndpoint.address >> 24) & 0xFF),
                                    LT_Pu32(rxPacket->senderEndpoint.port));
                    PrintCredentials(agent);
                    goto err;
                }
                authContext.password = (u8 *)credentials->password;
                authContext.passwordLen = lt_strlen(credentials->password);
                break;
            }

            case kLTStunAttrType_PasswordAlgorithm:
                if (nAttrLength != kLTStunPasswordAlgorithmAttrLen) {
                    LTLOG_YELLOWALERT("bad.pwalg.size", "Wrong size for password algorithm attribute: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                authContext.passwordAlgorithm = LT_NTOHS(*(u16 *)pPacketData);
                break;

            case kLTStunAttrType_MessageIntegrity: {
                if (nAttrLength != kHMAC_SHA1_Length) {
                    LTLOG_YELLOWALERT("bad.sha1.size", "Wrong size for message-integrity attribute: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                message->flags |= kLTStunMessageFlags_MacSHA1;
                if (VerifyMessageIntegrity(&authContext, kMessageIntegrityAlgo_SHA1, pStunHeader, pAttrHeader, pPacketData, nAttrLength)) {
                    message->flags |= kLTStunMessageFlags_MacValid;
                }
                break;
            }

            case kLTStunAttrType_MessageIntegritySHA256: {
                if ((nAttrLength < kHMAC_SHA256_LengthMin) || (nAttrLength > kHMAC_SHA256_LengthMax)) {
                    LTLOG_YELLOWALERT("bad.sha256.size", "Wrong size for message-integrity-sha256 attribute: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                message->flags |= kLTStunMessageFlags_MacSHA256;
                if (VerifyMessageIntegrity(&authContext, kMessageIntegrityAlgo_SHA256, pStunHeader, pAttrHeader, pPacketData, nAttrLength)) {
                    message->flags |= kLTStunMessageFlags_MacValid;
                }
                break;
            }

            case kLTStunAttrType_Fingerprint: {
                if (nAttrLength != sizeof(u32)) {
                    LTLOG_YELLOWALERT("bad.fprint.size", "Wrong size for fingerprint attribute: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                u32 nReceivedFingerprint = LT_NTOHL(*(u32*)pPacketData) ^ kFingerprintXorValue;
                u32 nCalculatedFingerprint = 0;
                LT_GetLTUtilityByteOps()->Crc32(pStunHeader, (u8*)pAttrHeader - (u8*)pStunHeader, &nCalculatedFingerprint);
                message->flags |= kLTStunMessageFlags_Fingerprint;
                if (nReceivedFingerprint == nCalculatedFingerprint) message->flags |= kLTStunMessageFlags_FingerprintValid;
                break;
            }

            case kLTStunAttrType_ErrorCode: {
                if ((nAttrLength < sizeof(u32)) || (nAttrLength > kErrorCode_LengthMax)) {
                    LTLOG_YELLOWALERT("bad.errcode.size", "Wrong size for error code attribute: %lu", LT_Pu32(nAttrLength));
                    goto err;
                }
                message->bHasErrorCode = true;
                message->nErrorCode = (100 * (pPacketData[2] & 0x7)) + pPacketData[3];
                if (nAttrLength > sizeof(u32)) {
                    u32 reasonLen = nAttrLength - sizeof(u32);
                    message->pErrorReason = lt_memdup(pPacketData + 4, reasonLen + 1);
                    if (message->pErrorReason) message->pErrorReason[reasonLen] = 0;
                }
                break;
            }

            default:
                break;
        }

        StunAttribute *attr = lt_malloc(sizeof(StunAttribute) + nAttrLength);
        attr->nType = nAttrType;
        attr->nLength = nAttrLength;
        lt_memcpy(attr->data, pPacketData, nAttrLength);
        message->pAttributes->API->Append(message->pAttributes, attr);

        /* The next attribute starts on the first 4-byte boundary after the current attribute */
        u32 nNextAttrOffset = (nAttrLength + sizeof(u32) - 1) & ~(sizeof(u32)-1);
        pPacketData += nNextAttrOffset;
        nPacketSize -= nNextAttrOffset;
    }

    s_pEvent->NotifyEvent(message->transport->hMessageEvent, message->transport, message);
    return;

err:
    lt_destroyobject(message);
}

static void
HandleReceivedPacketCompleteProc(LTThread_ReleaseReason releaseReason, void *clientData) {
    LT_UNUSED(releaseReason);
    ReceivedPacketData *packet = clientData;
    lt_free(packet);
}

static LTStunTransportImpl *
FindTransport(LTStunAgentImpl *agent, LTSocket hSocket, LTNetIpv4Endpoint endpoint) {
    LTStunTransportImpl *partialMatch = NULL;
    for (u32 i = 0; i < agent->transports->API->GetCount(agent->transports); ++i) {
        LTStunTransportImpl *transport = agent->transports->API->Get(agent->transports, i, NULL);
        if (transport->hSocket == hSocket) {
            if (LTNetIpv4Endpoint_IsEqual(transport->endpoint, endpoint)) {
                return transport;
            } else if (LTNetIpv4Endpoint_IsAny(transport->endpoint)) {
                if (partialMatch) {
                    LTLOG_YELLOWALERT("ambig.endpt", "Sender %lu.%lu.%lu.%lu:%lu matches multiple transports",
                        LT_Pu32((endpoint.address >> 0) & 0xFF),
                        LT_Pu32((endpoint.address >> 8) & 0xFF),
                        LT_Pu32((endpoint.address >> 16) & 0xFF),
                        LT_Pu32((endpoint.address >> 24) & 0xFF),
                        LT_Pu32(endpoint.port));
                }
                partialMatch = transport;
            }
        }
    }
    return partialMatch;
}

static void
OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    if (!clientData) return;
    LTStunAgentImpl *agent = clientData;
    if (event == kLTSocket_Event_ReadReady) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
        // keep reading until no more packets are available
        while (true) {
            s32 packetSize = pSocket->ReadSocket(hSocket, NULL, 0);
            if (packetSize == 0) return;
            if (packetSize < 0) {
                LTLOG_YELLOWALERT("sock.read.err", "Socket read failed: %ld", LT_Ps32(packetSize));
                return;
            }
            ReceivedPacketData *rxPacket = lt_malloc(sizeof(ReceivedPacketData) + packetSize);
            if (!rxPacket) {
                LTLOG_YELLOWALERT("rx.alloc.err", "Failed to allocate receive buffer");
                return;
            }
            rxPacket->packetSize = packetSize;
            s32 bytesRead = pSocket->ReadSocket(hSocket, rxPacket->packetData, packetSize);
            LT_ASSERT(bytesRead == packetSize);

            pSocket->GetProperty(hSocket, "recv.endpoint.v4", &rxPacket->senderEndpoint);

            rxPacket->transport = FindTransport(agent, hSocket, rxPacket->senderEndpoint);
            if (!rxPacket->transport) {
                LTLOG_YELLOWALERT("no.xprt", "No transport found for sender endpoint %lu.%lu.%lu.%lu:%lu - Ignoring.",
                    LT_Pu32((rxPacket->senderEndpoint.address >> 0) & 0xFF),
                    LT_Pu32((rxPacket->senderEndpoint.address >> 8) & 0xFF),
                    LT_Pu32((rxPacket->senderEndpoint.address >> 16) & 0xFF),
                    LT_Pu32((rxPacket->senderEndpoint.address >> 24) & 0xFF),
                    LT_Pu32(rxPacket->senderEndpoint.port));
                lt_free(rxPacket);
                return;
            }

            if (rxPacket->packetData[0] <= 3) {
                s_pThread->QueueTaskProc(agent->hThread, HandleReceivedPacketProc, HandleReceivedPacketCompleteProc, rxPacket);
            } else {
                if (rxPacket->transport->pCallback) {
                    rxPacket->transport->pCallback(rxPacket->packetData, packetSize, rxPacket->transport->pClientData);
                }
                lt_free(rxPacket);
            }
        }
    }
}

// Attributes must be 4-byte aligned
#define ATTR_SIZE(payload_size) ((sizeof(StunAttrHdr) + (payload_size) + sizeof(u32) - 1) & ~(sizeof(u32) - 1))

static u32
CalculatePacketSize(LTStunMessageImpl *message, u32 usernameLen) {
    u32 nPacketSize = sizeof(StunPacketHdr);
    if (message->flags & (kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_MacSHA256))
        nPacketSize += ATTR_SIZE(usernameLen);
    if (message->flags & kLTStunMessageFlags_MacSHA1)
        nPacketSize += ATTR_SIZE(kHMAC_SHA1_Length);
    if (message->flags & kLTStunMessageFlags_MacSHA256)
        nPacketSize += ATTR_SIZE(kHMAC_SHA256_LengthMax);
    if (message->flags & kLTStunMessageFlags_Fingerprint)
        nPacketSize += ATTR_SIZE(sizeof(u32));
    for (u32 i = 0; i < message->pAttributes->API->GetCount(message->pAttributes); ++i) {
        StunAttribute *attr = message->pAttributes->API->Get(message->pAttributes, i, NULL);
        nPacketSize += ATTR_SIZE(attr->nLength);
    }
    return nPacketSize;
}

static u8 *
WriteAttribute(u8 *pPacketData, LTStunAttrType attrType, const void *pPayload, u32 nPayloadLen) {
    StunAttrHdr *pAttrHdr = (StunAttrHdr *)pPacketData;
    pAttrHdr->nType = LT_HTONS(attrType);
    pAttrHdr->nLength = LT_HTONS(nPayloadLen);
    if (pPayload && nPayloadLen) lt_memcpy(pPacketData + sizeof(StunAttrHdr), pPayload, nPayloadLen);
    return pPacketData + ATTR_SIZE(nPayloadLen);
}

static void
OnRetransmissionTimerExpired(void *clientData) {
    s_pThread->KillTimer(s_pThread->GetCurrentThread(), OnRetransmissionTimerExpired, clientData);

    LTStunAgentImpl *agent = s_pThread->GetThreadSpecificClientData(s_pThread->GetCurrentThread(), "agent");
    StunTransaction *transaction = clientData;
    LTStunTransportImpl *transport = transaction->transport;
    if (!agent || !transaction) return;

    if (transaction->numRetransmissions >= kMaxRetransmissionCount) {
        LTLOG_DEBUG("rtx.limit", "No response to STUN request after %lu attempts", LT_Pu32(transaction->numRetransmissions));
        PopPendingTransaction(agent, transaction->transactionId.value);
        DestroyTransaction(agent, transaction);
    } else {
        transaction->numRetransmissions += 1;
        LTLOG_DEBUG("rtx", "No response to STUN request after %lu ms. Retransmitting attempt #%lu.", LT_Pu32(transaction->curRetransmissionTimeout), LT_Pu32(transaction->numRetransmissions));
        transaction->curRetransmissionTimeout *= 2;
        if (transaction->curRetransmissionTimeout > kMaxRetransmissionTimeoutMs) {
            transaction->curRetransmissionTimeout = kMaxRetransmissionTimeoutMs;
        }
        s_pThread->SetTimer(agent->hThread, LTTime_Milliseconds(transaction->curRetransmissionTimeout), OnRetransmissionTimerExpired, NULL, transaction);

        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hSocket);
        pSocket->SetProperty(transport->hSocket, "send.endpoint.v4", &transaction->endpoint);
        s32 err = pSocket->WriteSocket(transport->hSocket, transaction->packetData, transaction->packetSize);
        if (err < 0) {
            LTLOG("retx.send.fail", "err %ld len %lu", LT_Ps32(err), LT_Pu32(transaction->packetSize));
        }
    }
}

static void
SendMessageTaskProc(void *clientData) {
    LTStunMessageImpl *message = clientData;
    LTStunAgentImpl *agent = s_pThread->GetThreadSpecificClientData(s_pThread->GetCurrentThread(), "agent");
    if (!message || !agent) return;
    message->agent = agent;

    StunAuthContext authContext = {
        .passwordAlgorithm = kLTStunPasswordAlgorithm_MD5,
    };
    if (message->flags & (kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_MacSHA256)) {
        LTStunCredentialsUsageContext usageContext = (message->nClass == kLTStunClass_Request) ?
                                                        kLTStunCredentialsUsageContext_Local :
                                                        kLTStunCredentialsUsageContext_Remote;
        StunCredentials *credentials = FindCredentials(message->agent, usageContext, message->endpoint, NULL, 0);
        if (!credentials) {
            LTLOG_YELLOWALERT("no.send.cred", "Failed to find credentials for message integrity generation");
            return;
        }
        authContext.username    = (u8 *)credentials->username;
        authContext.usernameLen = lt_strlen(credentials->username);
        authContext.password    = (u8 *)credentials->password;
        authContext.passwordLen = lt_strlen(credentials->password);
    }

    u32 packetSize = CalculatePacketSize(message, authContext.usernameLen);
    u8 *pPacketData = lt_malloc(packetSize);
    if (!pPacketData) {
        LTLOG_YELLOWALERT("tx.alloc.err", "Failed to allocate TX packet");
        return;
    }
    lt_memset(pPacketData, 0, packetSize);

    StunPacketHdr *header = (StunPacketHdr *)pPacketData;
    header->nMessageType = LT_HTONS(message->nMethod | message->nClass);
    header->nMessageLen = LT_HTONS(packetSize - sizeof(StunPacketHdr));
    header->nMagicCookie = LT_HTONL(kStunMagicCookieValue);
    lt_memcpy(header->transactionId, message->transactionId.value, kLTStunTransactionIdLen);
    pPacketData += sizeof(StunPacketHdr);
    for (u32 i = 0; i < message->pAttributes->API->GetCount(message->pAttributes); ++i) {
        StunAttribute *attr = message->pAttributes->API->Get(message->pAttributes, i, NULL);
        if (attr->nType == kLTStunAttrType_Realm) {
            authContext.realm    = attr->data;
            authContext.realmLen = attr->nLength;
        } else if (attr->nType == kLTStunAttrType_PasswordAlgorithm) {
            authContext.passwordAlgorithm = LT_NTOHS(*(u16*)attr->data);
        }
        pPacketData = WriteAttribute(pPacketData, attr->nType, attr->data, attr->nLength);
    }
    if (message->flags & (kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_MacSHA256)) {
        LT_ASSERT(authContext.username);
        pPacketData = WriteAttribute(pPacketData, kLTStunAttrType_Username, authContext.username, authContext.usernameLen);
    }
    if (message->flags & kLTStunMessageFlags_MacSHA1) {
        LT_ASSERT(authContext.password);
        u8 msgMac[kHMAC_SHA1_Length];
        CalculateMessageIntegrity(&authContext, kMessageIntegrityAlgo_SHA1, header, (StunAttrHdr *)pPacketData, sizeof(msgMac), msgMac, sizeof(msgMac));
        pPacketData = WriteAttribute(pPacketData, kLTStunAttrType_MessageIntegrity, msgMac, sizeof(msgMac));
    }
    if (message->flags & kLTStunMessageFlags_MacSHA256) {
        LT_ASSERT(authContext.password);
        u8 msgMac[kHMAC_SHA256_LengthMax];
        CalculateMessageIntegrity(&authContext, kMessageIntegrityAlgo_SHA256, header, (StunAttrHdr *)pPacketData, sizeof(msgMac), msgMac, sizeof(msgMac));
        pPacketData = WriteAttribute(pPacketData, kLTStunAttrType_MessageIntegritySHA256, msgMac, sizeof(msgMac));
    }
    if (message->flags & kLTStunMessageFlags_Fingerprint) {
        u32 nFingerprint = 0;
        LT_GetLTUtilityByteOps()->Crc32(header, (u8*)pPacketData - (u8*)header, &nFingerprint);
        nFingerprint = LT_HTONL(nFingerprint ^ kFingerprintXorValue);
        pPacketData = WriteAttribute(pPacketData, kLTStunAttrType_Fingerprint, &nFingerprint, sizeof(u32));
    }

    LT_ASSERT((u32)(pPacketData - (u8*)header) == packetSize);

    // Append to the `pending transactions` list
    if (message->nClass == kLTStunClass_Request) {
        StunTransaction *transaction = lt_malloc(sizeof(StunTransaction));
        if (!transaction) {
            LTLOG_YELLOWALERT("txn.alloc.err", "Failed to allocate transaction");
            return;
        }
        *transaction = (StunTransaction) {
            .transport                = message->transport,
            .clientData               = message->clientData,
            .endpoint                 = message->endpoint,
            .transactionId            = message->transactionId,
            .curRetransmissionTimeout = message->retransmissionTimeout,
            .packetData               = header,
            .packetSize               = packetSize,
            .numRetransmissions       = 0,
        };
        agent->pPendingTransactions->API->Append(agent->pPendingTransactions, transaction);
        s_pThread->SetTimer(agent->hThread, LTTime_Milliseconds(transaction->curRetransmissionTimeout), OnRetransmissionTimerExpired, NULL, transaction);
    }

    LTStunTransportImpl *transport = message->transport;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hSocket);
    pSocket->SetProperty(transport->hSocket, "send.endpoint.v4", &message->endpoint);
    s32 err = pSocket->WriteSocket(transport->hSocket, header, packetSize);
    if (err < 0) {
        LTLOG("send.msg.fail", "err %ld len %lu", LT_Ps32(err), LT_Pu32(packetSize));
    }

    if (message->nClass != kLTStunClass_Request) {
        lt_free(header);
    }
}

static void
SendMessageTaskProcComplete(LTThread_ReleaseReason releaseReason, void *clientData) {
    LT_UNUSED(releaseReason);
    LTStunMessage *message = clientData;
    lt_destroyobject(message);
}

static void
CancelTransactionTaskProc(void *clientData) {
    LTStunTransactionId *transactionId = clientData;
    LTStunAgentImpl *agent = s_pThread->GetThreadSpecificClientData(s_pThread->GetCurrentThread(), "agent");
    StunTransaction *transaction = PopPendingTransaction(agent, transactionId->value);
    if (transaction) DestroyTransaction(agent, transaction);
}

static void
CancelTransactionTaskProcComplete(LTThread_ReleaseReason releaseReason, void *clientData) {
    LT_UNUSED(releaseReason);
    lt_free(clientData);
}

/*__________________________________
  LTStunTransport object constructors */
static bool
LTStunTransportImpl_ConstructObject(LTStunTransportImpl *transport) {
    transport->hMessageEvent = s_pCore->CreateEvent(&s_MsgReceivedEventArgs, DispatchReceivedMessageCB, DispatchReceivedMessageCompleteCB, NULL, NULL);
    return true;
}

static void
LTStunTransportImpl_DestructObject(LTStunTransportImpl *transport) {
    lt_destroyhandle(transport->hMessageEvent);
}

/*___________________________________
  LTStunAgent public api functions */
static void
LTStunAgentImpl_Init(LTStunAgentImpl *agent, const char *sessionId, LTSocket hSocket) {
    char threadName[20] = {};
    lt_snprintf(threadName, sizeof(threadName), "Stun-%10s", sessionId);
    agent->hThread = LT_GetCore()->CreateThread(threadName);
    s_pThread->SetThreadSpecificClientData(agent->hThread, "agent", NULL, agent);
    s_pThread->SetStackSize(agent->hThread, 2048);
    s_pThread->Start(agent->hThread, NULL, NULL);
    s_pThread->SetTimer(agent->hThread, LTTime_Seconds(kTransactionPurgeIntervalSec), PurgeCompletedTransactionsTimerProc, NULL, agent);
    agent->hSocket = hSocket;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
    pSocket->OnSocketEvent(hSocket, OnSocketEvent, agent);
}

static LTStunTransport *
LTStunAgentImpl_OpenTransport(LTStunAgentImpl *agent, LTSocket hSocket, LTNetIpv4Endpoint endpoint, LTStunTransport_UnhandledDatagramProc *pCallback, void *pClientData) {
    LTStunTransportImpl *transport = (LTStunTransportImpl *)lt_createobject(LTStunTransport);
    transport->agent        = agent;
    transport->endpoint     = endpoint;
    transport->pCallback    = pCallback;
    transport->pClientData  = pClientData;
    if (hSocket && (hSocket != agent->hSocket)) {
        transport->hSocket = hSocket;
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
        pSocket->OnSocketEvent(hSocket, OnSocketEvent, agent);
    } else {
        transport->hSocket = agent->hSocket;
    }
    agent->transports->API->Append(agent->transports, transport);
    return (LTStunTransport *)transport;
}

static void
LTStunAgentImpl_AddCredentials(LTStunAgentImpl *agent, LTNetIpv4Endpoint endpoint, const char *username, const char *password, LTStunCredentialsUsageContext usageContext) {
    StunCredentials *credentials = lt_malloc(sizeof(StunCredentials));
    *credentials = (StunCredentials) {
        .endpoint     = endpoint,
        .username     = lt_strdup(username),
        .password     = lt_strdup(password),
        .usageContext = usageContext,
    };
    agent->credentials->API->Append(agent->credentials, credentials);
}

/*___________________________________
  LTStunTransport public api functions */
static void
LTStunTransportImpl_OnMessageReceived(LTStunTransportImpl *transport, LTStunTransport_MessageReceivedProc *pCallback, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData) {
    s_pEvent->RegisterForEvent(transport->hMessageEvent, (void *)pCallback, clientDataReleaseProc, clientData, false);
}

static void
LTStunTransportImpl_NoMessageReceived(LTStunTransportImpl *transport, LTStunTransport_MessageReceivedProc *pCallback) {
    s_pEvent->UnregisterFromEvent(transport->hMessageEvent, (void *)pCallback);
}

static LTStunTransactionId
LTStunTransportImpl_Send(LTStunTransportImpl *transport, LTStunMessageImpl *message) {
    message->transport = transport;
    LTStunTransactionId transactionId = message->transactionId;
    s_pThread->QueueTaskProc(transport->agent->hThread, SendMessageTaskProc, SendMessageTaskProcComplete, message);
    return transactionId;
}

static void
LTStunTransportImpl_Cancel(LTStunTransportImpl *transport, LTStunTransactionId transactionId) {
    LTStunTransactionId *transactionIdCopy = lt_memdup(&transactionId, sizeof(LTStunTransactionId));
    if (!transactionIdCopy) {
        LTLOG_YELLOWALERT("txn.cancel.oom", NULL);
        return;
    }
    s_pThread->QueueTaskProc(transport->agent->hThread, CancelTransactionTaskProc, CancelTransactionTaskProcComplete, transactionIdCopy);
}

static s32
LTStunTransportImpl_SendDatagram(LTStunTransportImpl *transport, LTNetIpv4Endpoint endpoint, const u8 *pDatagram, u32 nDatagramSize, u8 dscp) {
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hSocket);
    pSocket->SetProperty(transport->hSocket, "send.endpoint.v4", &endpoint);
    pSocket->SetProperty(transport->hSocket, "dscp",             &dscp);
    return pSocket->WriteSocket(transport->hSocket, pDatagram, nDatagramSize);
}

/*__________________________________
  LTStunAgent object constructors */
static bool
LTStunAgentImpl_ConstructObject(LTStunAgentImpl *agent) {
    agent->credentials            = lt_createobject_typed(LTArray, List);
    agent->pPendingTransactions   = lt_createobject_typed(LTArray, List);
    agent->pCompletedTransactions = LTArray_CreateStructArray(sizeof(CompletedStunTransaction));
    agent->transports             = lt_createobject_typed(LTArray, List);
    return true;
}

static void
LTStunAgentImpl_DestructObject(LTStunAgentImpl *agent) {
    s_pThread->KillTimer(agent->hThread, PurgeCompletedTransactionsTimerProc, agent);
    lt_destroyhandle(agent->hThread);
    if (agent->hSocket) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, agent->hSocket);
        pSocket->NoSocketEvent(agent->hSocket, OnSocketEvent);
    }
    for (u32 i = 0; i < agent->transports->API->GetCount(agent->transports); ++i) {
        LTStunTransportImpl *transport = agent->transports->API->Get(agent->transports, i, NULL);
        if (transport->hSocket && (transport->hSocket != agent->hSocket)) {
            ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hSocket);
            pSocket->NoSocketEvent(transport->hSocket, OnSocketEvent);
        }
        lt_destroyobject(transport);
    }
    lt_destroyobject(agent->transports);

    for (u32 i = 0; i < agent->credentials->API->GetCount(agent->credentials); ++i) {
        StunCredentials *credentials = agent->credentials->API->Get(agent->credentials, i, NULL);
        lt_free(credentials->username);
        lt_free(credentials->password);
        lt_free(credentials);
    }
    lt_destroyobject(agent->credentials);

    for (u32 i = 0; i < agent->pPendingTransactions->API->GetCount(agent->pPendingTransactions); ++i) {
        StunTransaction *transaction = agent->pPendingTransactions->API->Get(agent->pPendingTransactions, i, NULL);
	if (transaction) {
		if (transaction->packetData) lt_free(transaction->packetData);
		lt_free(transaction);
	}
    }
    lt_destroyobject(agent->pPendingTransactions);

    lt_destroyobject(agent->pCompletedTransactions);
}

/*_____________________________________
  LTStunTransport LTObjectApi definition */
define_LTObjectImplPublic(LTStunTransport, LTStunTransportImpl,
    OnMessageReceived,
    NoMessageReceived,
    Send,
    Cancel,
    SendDatagram,
);

/*_____________________________________
  LTStunAgent LTObjectApi definition */
define_LTObjectImplPublic(LTStunAgent, LTStunAgentImpl,\
    Init,
    AddCredentials,
    OpenTransport,
);

/*____________________________________
 / LTNetStun library initialization */
static bool
LTNetStunImpl_LibInit(void) {
    // Open the libraries
    s_pCore   = LT_GetCore();
    s_pThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_pEvent  = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    return true;
}

static void
LTNetStunImpl_LibFini(void) {
}

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetStun, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetStun, ) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTNetStun,
    (LTStunMessageImpl)
    (LTStunAgentImpl)
    (LTStunTransportImpl)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
