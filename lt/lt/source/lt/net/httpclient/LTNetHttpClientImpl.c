/*******************************************************************************
 * LTNetHttpClientImpl.c - Implementation of HTTP client interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("http");
#define PLOG(...) LTLOG(__VA_ARGS__)
#define P(...)
// #define P(...) do { if (pRequest->debugRequest) LTLOG(__VA_ARGS__); } while(0)

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTNetHttpClient, (LTUtilityByteOps));

typedef enum {
    kLTHttpResponseParserState_Connecting,
    kLTHttpResponseParserState_ProtocolVersion,
    kLTHttpResponseParserState_StatusCode,
    kLTHttpResponseParserState_Reason,
    kLTHttpResponseParserState_HeaderKey,
    kLTHttpResponseParserState_HeaderValue,
    kLTHttpResponseParserState_BodyChunkNewline,
    kLTHttpResponseParserState_BodyChunkLength,
    kLTHttpResponseParserState_Body,
    kLTHttpResponseParserState_Complete,
    kLTHttpResponseParserState_Error
} LTHttpResponseParserState;

typedef enum {
    kLTHttpRequestState_SendRequestString,
    kLTHttpRequestState_WaitingForNextPart,
    kLTHttpRequestState_SendPartHeader,
    kLTHttpRequestState_SendPartData,
    kLTHttpRequestState_SendPartEpilog,
    kLTHttpRequestState_SendBody,
    kLTHttpRequestState_Complete,
} LTHttpRequestState;

typedef enum {
    kParamEncoding_RFC3986,
    kParamEncoding_XForms1p1
} ParamEncoding;

typedef enum {
    kTransferEncoding_Plain,
    kTransferEncoding_Chunked,
} TransferEncoding;

enum {
    kMultipartBoundaryBytesSize = 16,
    kMultipartBoundaryStringSize = (kMultipartBoundaryBytesSize * 2) + 1,
};

typedef struct {
    LTHttpResponseParserState      state;
    LTTokenizer                   *pTokenizer;
    char                          *pProtocolVersion;
    u32                            nStatusCode;
    char                          *pReason;
    char                          *pCurrHeaderKey;
    LTAssociativeArray            *pHeaders;
    TransferEncoding               transferEncoding;
    u32                            nContentLength;
    u32                            nBodyBytesRemaining;
} LTHttpResponse;

typedef struct {
    LTHttpRequest        hHandle;
    LTHttpRequestState   state;
    LTEvent              hEvent;
    LTHttpRequestMethod  nMethod;
    bool                 bUseTLS;
    bool                 bUseChunkedTransfer;
    char                *pHost;
    u16                  nPort;
    char                *pResource;
    char                *pTlsSpec;
    LTString             pQueryStr;
    LTString             pHeaderStr;
    LTString             pContentType;
    char                 multipartBoundary[kMultipartBoundaryStringSize];
    LTHttpMultipartData *pMultipartData;
    u32                  nNumParts;
    u32                  nCurrentPart;
    const void          *pBodyData;
    u32                  nBodySize;
    LTSocket             hSocket;
    bool                 bSocketDisconnected;
    bool                 bWriteReady;
    bool                 multiPartIncomplete;
    bool                 debugRequest;
    LTTransport          hTransport;
    LTHttpResponse       response;
    u32                  nRequestDataLeft;
    LTString             outString;
    const u8            *outSegmentData;
    u32                  outSegmentLeft;
} LTHttpRequestContext;

static void         OnDataArrived(LTSocket hSocket, void *pClientData);
static void         OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *pClientData);
static void         PrepareRequest(LTHttpRequestContext *pRequest);
static void         DoSendRequestData(LTHttpRequestContext *pRequest);
static const char  *LTHttpRequest_GetResponseHeader(LTHttpRequest hRequest, const char *pName);
static void         LTHttpRequest_AddHeader(LTHttpRequest hRequest, const char *pName, LTArgType argType, ...);
static void         LTHttpRequest_DestroyRequest(LTHttpRequest hRequest);
static s32          LTHttpRequest_WritePartBoundary(LTHttpRequest hRequest);

static ILTHttpRequest s_ILTHttpRequest;

static LTCore                  *s_pCore         = NULL;
static LTNetCore               *s_pNetCore      = NULL;
static LTUtilityTokenizer      *s_pTokenizer    = NULL;
static ILTThread               *s_pThread       = NULL;
static ILTEvent                *s_pEvent        = NULL;

static const char s_multipartFormData[] = "multipart/form-data";

static const LTArgsDescriptor s_RequestEventArgs =
    {2, {kLTArgType_lthandle, kLTArgType_u32}};

static void
DispatchRequestEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);

    LTHttpClient_RequestEventProc *pCallback = (LTHttpClient_RequestEventProc *)pEventProc;
    LTHttpRequest hRequest  = LTArgs_lthandleAt(0, pEventArgs);
    LTHttpRequestEvent event = LTArgs_u32At(1, pEventArgs);

    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    u32 remain = pRequest->response.nBodyBytesRemaining;
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);

    if ((event == LTHttpRequestEvent_BodyDataAvailable) && (remain == 0)) return;
    pCallback(hRequest, event, pEventProcClientData);
}

static bool
ProcessHeadToken(LTTokenizer *pTokenizer, const char *pToken, char nMatchedSentinel, void *pClientData) {
    LT_UNUSED(pTokenizer);
    LT_UNUSED(nMatchedSentinel);
    LTHttpRequestContext *pRequest = pClientData;
    bool bContinue = true;
    switch (pRequest->response.state) {
        case kLTHttpResponseParserState_ProtocolVersion:
            P("head.parse.protocolversion", "Parsed protocol version: %s", pToken);
            if (pRequest->response.pProtocolVersion) lt_free(pRequest->response.pProtocolVersion);
            pRequest->response.pProtocolVersion = lt_strdup(pToken);
            pRequest->response.state = kLTHttpResponseParserState_StatusCode;
            s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, " \n");
            break;
        case kLTHttpResponseParserState_StatusCode:
            P("head.parse.statuscode", "Parsed status code: %s", pToken);
            pRequest->response.nStatusCode = lt_strtou32(pToken, NULL, 10);
            pRequest->response.state = kLTHttpResponseParserState_Reason;
            s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, "\n");
            break;
        case kLTHttpResponseParserState_Reason:
            P("head.parse.reason", "Parsed reason: %s", pToken);
            if (pRequest->response.pReason) lt_free(pRequest->response.pReason);
            pRequest->response.pReason = lt_strdup(pToken);
            pRequest->response.state = kLTHttpResponseParserState_HeaderKey;
            s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, ":\n");
            break;
        case kLTHttpResponseParserState_HeaderKey:
            if (lt_strlen(pToken) == 0) {
                P("head.end", "Reached end of head");
                bContinue = false;
                if (pRequest->nMethod != kLTHttpRequestMethod_HEAD) {
                    const char *pTransferEncoding = LTHttpRequest_GetResponseHeader(pRequest->hHandle, "Transfer-Encoding");
                    if (!pTransferEncoding) {
                        pRequest->response.transferEncoding = kTransferEncoding_Plain;
                        const char *pContentLength = LTHttpRequest_GetResponseHeader(pRequest->hHandle, "Content-Length");
                        if (pContentLength) {
                            pRequest->response.nContentLength = lt_strtou32(pContentLength, NULL, 10);
                            P("head.content.length", "content length = %lu", LT_Pu32(pRequest->response.nContentLength));
                        } else {
                            pRequest->response.nContentLength = 0;
                        }
                        pRequest->response.nBodyBytesRemaining = pRequest->response.nContentLength;
                        if (pRequest->response.nContentLength) {
                            pRequest->response.state = kLTHttpResponseParserState_Body;
                        } else { // no body after header, so the response is complete
                            P("head.no.body", "no body!");
                            pRequest->response.state = kLTHttpResponseParserState_Complete;
                        }
                    } else if (lt_strcasecmp(pTransferEncoding, "chunked") == 0) {
                        pRequest->response.transferEncoding = kTransferEncoding_Chunked;
                        pRequest->response.state = kLTHttpResponseParserState_BodyChunkLength;
                        s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, "\n");
                        bContinue = true;
                    } else {
                        LTLOG_YELLOWALERT("head.transenc.unsup", "Unsupported Transfer-Encoding: %s", pTransferEncoding);
                    }
                } else {
                    // TODO: header-only request should be complete on status code 200, but may be not complete on error status.
                    // pRequest->response.state = kLTHttpResponseParserState_Complete;  // only if status code 200
                }
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_HeadComplete);
                if (pRequest->response.state == kLTHttpResponseParserState_Complete) {
                    s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_RequestComplete);
                }

            } else {
                P("head.parse.headerkey", "Parsed header key: %s", pToken);
                if (pRequest->response.pCurrHeaderKey) lt_free(pRequest->response.pCurrHeaderKey);
                pRequest->response.pCurrHeaderKey = lt_strdup(pToken);
                pRequest->response.state = kLTHttpResponseParserState_HeaderValue;
                s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, "\n");
            }
            break;
        case kLTHttpResponseParserState_HeaderValue:
            P("head.parse.headervalue", "Parsed header value: %s", pToken);
            char *pHeaderValueStr = lt_strdup(pToken);
            /* Convert to lower-case since keys are case-insensitive */
            lt_strlower(pRequest->response.pCurrHeaderKey);
            LTCStringKeyedArray_Set(pRequest->response.pHeaders, pRequest->response.pCurrHeaderKey, pHeaderValueStr);
            lt_free(pRequest->response.pCurrHeaderKey);
            pRequest->response.pCurrHeaderKey = NULL;
            pRequest->response.state = kLTHttpResponseParserState_HeaderKey;
            s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, ":\n");
            break;
        case kLTHttpResponseParserState_BodyChunkNewline:
            if (lt_strlen(pToken) != 0) {
                LTLOG_YELLOWALERT("body.ck.nl.err", "Expected empty token before body chunk length. Got: '%s'", pToken);
            }
            pRequest->response.state = kLTHttpResponseParserState_BodyChunkLength;
            s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, "\n");
            break;
        case kLTHttpResponseParserState_BodyChunkLength:
            pRequest->response.nBodyBytesRemaining = lt_strtou32(pToken, NULL, 16);
            P("chunk.len", "Read chunk len: %lu", LT_Pu32(pRequest->response.nBodyBytesRemaining));
            if (pRequest->response.nBodyBytesRemaining > 0) {
                pRequest->response.state = kLTHttpResponseParserState_Body;
            } else {
                pRequest->response.state = kLTHttpResponseParserState_Complete;
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_RequestComplete);
            }
            break;
        case kLTHttpResponseParserState_Body:
        default:
            LTLOG_YELLOWALERT("head.token.badstate", "Invalid state for head token processing: %lu", LT_Pu32(pRequest->response.state));
            break;
    }
    return bContinue;
}

static void
DestroyRequestSocket(LTHttpRequestContext *pRequest) {
    // Make sure request data and socket exist
    if (!pRequest || !pRequest->hSocket) return;
    lt_destroyhandle(pRequest->hSocket);
    pRequest->hSocket = 0;
}

static void
OnDataArrived(LTSocket hSocket, void *pClientData) {
    LTHttpRequestContext *pRequest = pClientData;

    /* Filter out spurious read events */
    if (pRequest->response.state == kLTHttpResponseParserState_Complete) {
        return;
    }

    while (pRequest->response.state != kLTHttpResponseParserState_Complete) {
        if (pRequest->response.state == kLTHttpResponseParserState_Body) {
            s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_BodyDataAvailable);
            break;
        } else {
            u8 byte;
            ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);
            u32 nBytesRead = pSocket->ReadSocket(hSocket, &byte, sizeof(byte));
            if (!nBytesRead) break;
            s_pTokenizer->ProcessData(pRequest->response.pTokenizer, &byte, nBytesRead, true);
        }
    }
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LTHttpRequestContext *pRequest = (LTHttpRequestContext *)clientData;
    if (!pRequest || pRequest->hSocket != hSocket || !pRequest->hSocket) return;

    P("sock.event", "0x%02lx", LT_Pu32(event));
    switch (event) {
        case kLTSocket_Event_DnsResolved:
        case kLTSocket_Event_SocketReady:
            break;
        case kLTSocket_Event_Connected:
            pRequest->bSocketDisconnected = false;
            PrepareRequest(pRequest);
            break;
        case kLTSocket_Event_WriteReady:
            pRequest->bWriteReady = true;
            DoSendRequestData(pRequest);
            break;
        case kLTSocket_Event_ReadReady:
            OnDataArrived(hSocket, pRequest);
            break;
        case kLTSocket_Event_Disconnected:
            P("sock.discon", "state %d event 0x%02lx", pRequest->response.state, LT_Pu32(event));
            pRequest->bSocketDisconnected = true;
            if (pRequest->response.state == kLTHttpResponseParserState_Connecting) {
                // Socket disconnects when client is connecting, so it is an error.
                DestroyRequestSocket(pRequest);
                pRequest->response.state = kLTHttpResponseParserState_Error;
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
            } else if (pRequest->response.state == kLTHttpResponseParserState_Complete) {
                // Client already completed the http request. Server disconnects, so client should disconnect.
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Disconnected);
            } else {
                // Server disconnects right after sending the last data. But, client didn't complete data processing.
                // Client should decide what to do. May ignore, disconnect, or treat as error.
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle,  LTHttpRequestEvent_DisconnectPending);
            }
            break;
        case kLTSocket_Event_Error:
        case kLTSocket_Event_SocketError:
        case kLTSocket_Event_WriteError:
        case kLTSocket_Event_ReadError:
        case kLTSocket_Event_DnsError:
        case kLTSocket_Event_DnsTimeout:
        case kLTSocket_Event_ConnectError:
        case kLTSocket_Event_ConnectTimeout:
            PLOG("sock.error", "socket 0x%02lx event 0x%02lx", LT_Pu32(hSocket), LT_Pu32(event));
            pRequest->response.state = kLTHttpResponseParserState_Error;
            // Disconnect & Destroy socket on error
            DestroyRequestSocket(pRequest);
            pRequest->bSocketDisconnected = true;
            s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
            break;
        default:
            LTLOG_YELLOWALERT("sock.event.unhandled", "socket 0x%02lx event 0x%02x", LT_Pu32(hSocket), event);
            break;
    }
}

static void PrepareRequest(LTHttpRequestContext *pRequest) {
    if (!pRequest->response.pTokenizer) {
        pRequest->response.pTokenizer = s_pTokenizer->CreateTokenizer(" \n", ProcessHeadToken, pRequest);
    } else {
        s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, " \n");
    }
    pRequest->response.state = kLTHttpResponseParserState_ProtocolVersion;

    const char *pMethodStr;
    switch (pRequest->nMethod) {
        case kLTHttpRequestMethod_GET:  pMethodStr = "GET";  break;

        case kLTHttpRequestMethod_HEAD: pMethodStr = "HEAD"; break;

        case kLTHttpRequestMethod_POST:
        case kLTHttpRequestMethod_PUT:
            pMethodStr = pRequest->nMethod == kLTHttpRequestMethod_POST ? "POST" : "PUT";
            if (pRequest->pContentType) {
                LTHttpRequest_AddHeader(pRequest->hHandle, "Content-Type", kLTArgType_charstar, pRequest->pContentType);
            }

            // We can't include the content length for incomplete multipart requests as the total size is unknown
            pRequest->bUseChunkedTransfer = false;
            if (!pRequest->multiPartIncomplete) {
                LTHttpRequest_AddHeader(pRequest->hHandle, "Content-Length", kLTArgType_u32, pRequest->nBodySize);
            } else {
                LTHttpRequest_AddHeader(pRequest->hHandle, "Transfer-Encoding", kLTArgType_charstar, "chunked");
                pRequest->bUseChunkedTransfer = true;
                LTLOG("prep.req.chunked", "Using Transfer-Encoding: chunked for incomplete multipart request");
            }
            break;

        default: LTLOG_YELLOWALERT("sendrequest", "Invalid method: %lu", LT_Pu32(pRequest->nMethod)); return;
    }

    LTHttpRequest_AddHeader(pRequest->hHandle, "User-Agent", kLTArgType_charstar, "Roku/LT");

    ltstring_format(&pRequest->outString, "%s /%s%s HTTP/1.1\r\n%s\r\n",
        pMethodStr,
        pRequest->pResource,
        pRequest->pQueryStr,
        pRequest->pHeaderStr);

    pRequest->nRequestDataLeft = lt_strlen(pRequest->outString) + pRequest->nBodySize;
    P("prep.req", "nRequestDataLeft init to: %lu", LT_Pu32(pRequest->nRequestDataLeft));

    pRequest->state = kLTHttpRequestState_SendRequestString;
    pRequest->outSegmentData = (u8 *)pRequest->outString;
    pRequest->outSegmentLeft = lt_strlen(pRequest->outString);
    P("prep.req", "Request Header:\n%s", pRequest->outString);
}

static void
MakePartHeader(const LTHttpMultipartData *part, const char *boundary, LTString *partHeader) {
    ltstring_appendformat(partHeader, "\r\n--%s\r\n", boundary);
    ltstring_appendformat(partHeader, "Content-Type: %s\r\n", part->contentType);
    if (!part->filename) {
        ltstring_appendformat(partHeader, "Content-Disposition: form-data; name=\"%s\"\r\n", part->name);
    } else {
        ltstring_appendformat(partHeader, "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n", part->name, part->filename);
    }
    if (part->size) {
        // Only add the Content-Length field if the part has a non-zero size
        ltstring_appendformat(partHeader, "Content-Length: %lu\r\n", LT_Pu32(part->size));
    }
    if (part->partHeaders) {
        // Add part-specific headers
        ltstring_append(partHeader, part->partHeaders);
    }
    ltstring_append(partHeader, "\r\n");
}

static void
MakePartEpilog(const char *boundary, LTString *partEpilog) {
    ltstring_format(partEpilog, "\r\n--%s--\r\n", boundary);
}

static LTHttpRequestState
PreparePartHeader(LTHttpRequestContext *pRequest) {
    const LTHttpMultipartData *part = &pRequest->pMultipartData[pRequest->nCurrentPart];

    // Check if the next part is available yet
    if (part->contentType == NULL) {
        P("prep.part.hdr.wait", "Waiting for next part: %lu", LT_Pu32(pRequest->nCurrentPart));
        return kLTHttpRequestState_WaitingForNextPart;
    }

    MakePartHeader(part, pRequest->multipartBoundary, &pRequest->outString);

    if (pRequest->bUseChunkedTransfer) {
        // For simplicity we treat each form part as a separate chunk, even if additional chunks are already available
        u32 chunkSize = lt_strlen(pRequest->outString);
        chunkSize += pRequest->pMultipartData[pRequest->nCurrentPart].size;

        // Build the chunk header, using the size for this form part (including form header).
        // If it's not the first part then also include the end-marker for the previous chunk.
        char chunkHeader[16];
        lt_snprintf(chunkHeader, sizeof(chunkHeader),  "%s%lX\r\n", (pRequest->nCurrentPart > 0) ? "\r\n" : "", LT_Pu32(chunkSize));
        ltstring_insert(&pRequest->outString, 0, chunkHeader);

        // Increase the overall count of remaining bytes to send, to accurately track when the overall request will be finished
        pRequest->nRequestDataLeft += lt_strlen(chunkHeader);
        P("prep.part.hdr", "nRequestDataLeft up to: %lu", LT_Pu32(pRequest->nRequestDataLeft));
    }

    pRequest->outSegmentData = (u8 *)pRequest->outString;
    pRequest->outSegmentLeft = lt_strlen(pRequest->outString);
    P("prep.part.hdr", "HDR:\n%s", pRequest->outString);
    return kLTHttpRequestState_SendPartHeader;
}

static void
PreparePartData(LTHttpRequestContext *pRequest) {
    const LTHttpMultipartData *part = &pRequest->pMultipartData[pRequest->nCurrentPart];
    pRequest->outSegmentData = part->data;
    pRequest->outSegmentLeft = part->size;
    P("prep.part.data", "Len: %lu", LT_Pu32(part->size));
}

static void
PreparePartEpilog(LTHttpRequestContext *pRequest) {
    MakePartEpilog(pRequest->multipartBoundary, &pRequest->outString);

    if (pRequest->bUseChunkedTransfer) {
        // The last chunk is just the form epilog (final boundary)
        u32 chunkSize = lt_strlen(pRequest->outString);

        // Build the chunk header, using the size for this form epilog.
        char chunkHeader[16];
        lt_snprintf(chunkHeader, sizeof(chunkHeader),  "\r\n%lX\r\n", LT_Pu32(chunkSize));
        ltstring_insert(&pRequest->outString, 0, chunkHeader);

        // Terminate the chunk stream after the form epilog has been sent
        const char chunkTrailer[] = "\r\n0\r\n\r\n";
        ltstring_append(&pRequest->outString, chunkTrailer);

        // Increase the overall count of remaining bytes to send
        pRequest->nRequestDataLeft += lt_strlen(chunkHeader) + lt_strlen(chunkTrailer);
        P("prep.part.epi", "nRequestDataLeft up to: %lu", LT_Pu32(pRequest->nRequestDataLeft));
    }

    pRequest->outSegmentData = (u8 *)pRequest->outString;
    pRequest->outSegmentLeft = lt_strlen(pRequest->outString);
    P("prep.part.epi", "%s", pRequest->outString);
}

static void
PrepareBodyData(LTHttpRequestContext *pRequest) {
    pRequest->outSegmentData = pRequest->pBodyData;
    pRequest->outSegmentLeft = pRequest->nBodySize;
}

static void
PrepareSegmentCompletedForNextWrite(LTHttpRequestContext *pRequest) {
    ltstring_empty(pRequest->outString);
    switch (pRequest->state) {
        case kLTHttpRequestState_SendRequestString:
            if (lt_strncmp(pRequest->pContentType, s_multipartFormData, sizeof(s_multipartFormData) - 1) == 0) {
                pRequest->nCurrentPart = 0;
                pRequest->state = PreparePartHeader(pRequest);
            } else {
                PrepareBodyData(pRequest);
                pRequest->state = kLTHttpRequestState_SendBody;
            }
            break;
        case kLTHttpRequestState_WaitingForNextPart:
            if (pRequest->nCurrentPart < pRequest->nNumParts) {
                pRequest->state = PreparePartHeader(pRequest);
            } else {
                PreparePartEpilog(pRequest);
                pRequest->state = kLTHttpRequestState_SendPartEpilog;
            }
            break;
        case kLTHttpRequestState_SendPartHeader:
            PreparePartData(pRequest);
            pRequest->state = kLTHttpRequestState_SendPartData;
            break;
        case kLTHttpRequestState_SendPartData:
            if (++pRequest->nCurrentPart < pRequest->nNumParts) {
                pRequest->state = PreparePartHeader(pRequest);
            } else {
                PreparePartEpilog(pRequest);
                pRequest->state = kLTHttpRequestState_SendPartEpilog;
            }
            break;
        case kLTHttpRequestState_SendPartEpilog:
            pRequest->state = kLTHttpRequestState_Complete;
            break;
        case kLTHttpRequestState_SendBody:
            pRequest->state = kLTHttpRequestState_Complete;
            break;
        case kLTHttpRequestState_Complete:
            break;
    }
}

// TODO: merge this logic with LTHttpRequest_WriteBodyData for less code duplication
static void
DoSendRequestData(LTHttpRequestContext *pRequest) {
    if (pRequest->nRequestDataLeft > 0) {
        if (pRequest->nRequestDataLeft < 2000) { P("send.data", "data left: %lu/%lu", LT_Pu32(pRequest->nRequestDataLeft), LT_Pu32(pRequest->outSegmentLeft)); }
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pRequest->hSocket);
        if (pRequest->state == kLTHttpRequestState_WaitingForNextPart) {
            // Notify the user to supply the next form part(s) available for sending
            s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_BodyPartPending);
        } else if (pRequest->outSegmentData) {
            while (pRequest->outSegmentLeft > 0) { // Write until socket full or error
                s32 nWritten = pSocket->WriteSocket(pRequest->hSocket, pRequest->outSegmentData, pRequest->outSegmentLeft);
                /* For HTTPS/TLS, nWrite is 0 only if the input length is 0 or there is pending data in TLS to resend.
                 *     Otherwise, nWrite is guaranteed either positive on success or negative on failure.
                 * For HTTP/TCP, nWrite could be 0 if TCP asks for retransmission or the socket is full.
                 */
                if (nWritten < 0) {
                    LTLOG("send.wr.err", "error code %ld", LT_Ps32(nWritten));
                    s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
                    return;
                }
                pRequest->nRequestDataLeft -= nWritten;
                pRequest->outSegmentLeft   -= nWritten; // decrement amount of data left in buffer
                pRequest->outSegmentData   += nWritten; // increment location of data pointer in buffer

                if (pRequest->nRequestDataLeft & 0x80000000) {
                    LTLOG_YELLOWALERT("send.wr.overflow", "nRequestDataLeft overflow: 0x%08lx", LT_Pu32(pRequest->nRequestDataLeft));
                    return;
                }

                if (nWritten == 0) break;
            }

            P("send.wr", "nRequestDataLeft down to: %lu/%lu", LT_Pu32(pRequest->nRequestDataLeft), LT_Pu32(pRequest->outSegmentLeft));

            if (pRequest->outSegmentLeft == 0) {
                PrepareSegmentCompletedForNextWrite(pRequest);
            }
            // all data is sent out, so the current write ready is used.
            if (pRequest->nRequestDataLeft == 0) {
                pRequest->bWriteReady = false;
                P("send.wr.done", "All data sent out");
            }
        } else {
            // sanity check that the current output segment data can only be missing for body data or part data sections that are
            // to be streamed in by the user with WriteBodyData()
            LT_ASSERT((pRequest->state == kLTHttpRequestState_SendBody) || (pRequest->state == kLTHttpRequestState_SendPartData));

            // Notify the user that they may stream in data
            s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_BodyWriteReady);
        }
    }
}

/* Used just to validate IP address string */
static bool
StringToIPAddress(const char *str, u8 *ip) {
    if (!str) return false;
    int n = 0;
    int len = lt_strlen(str);
    bool octet_started = false;
    ip[0] = 0;
    ip[1] = 0;
    ip[2] = 0;
    ip[3] = 0;
    for (int i = 0; i<len && n < 4; i++) {
        char c = str[i];
        if (c>='0' && c<='9') {
            octet_started = true;
            ip[n] *= 10;
            ip[n] += (c-'0');
        } else if (c=='.') {
            if (!octet_started) {
                return false;
            }
            octet_started = false;
            n++;
        }
        else {
            return false;
        }
    }
    if (n==3 && octet_started) return true;
    return false;
}

static bool
ParseUrl(const char *pUrl, bool *bUseTLS, char **pHost, u16 *pPort, char **pResource, LTString *pQuery) {
    *pPort = 80;
    *bUseTLS = false;

    // get protocol type
    const char *pSep = lt_strstr(pUrl, "://");
    // no protocol found
    if (!pSep) return false;

    const char *pScheme = pUrl;
    int nSchemeLen = pSep - pScheme;
    if (nSchemeLen == 5 && lt_strncmp("https", pScheme, nSchemeLen) == 0) {
        // https
        *pPort = 443;
        *bUseTLS = true;
    } else if (nSchemeLen == 4 && lt_strncmp("http", pScheme, nSchemeLen) == 0) {
        // http
        *pPort = 80;
        *bUseTLS = false;
    } else {
        // no valid protocol
        return false;
    }
    pUrl += nSchemeLen + 3;

    // get port and host
    int nHostLen;
    const char *pTemp = pUrl;
    if ((pSep = lt_strstr(pUrl, ":"))) {
        // get port, same to the end of host
        nHostLen = pSep - pUrl + 1;
        pUrl += nHostLen;
        // get port and move url to after port
        *pPort = lt_strtou32(pUrl, (char **)&pUrl, 10);
    } else {
        // no port provided, so get the end of host
        if ((pSep = lt_strstr(pUrl, "/"))) {
            nHostLen = pSep - pUrl + 1;
        } else {
            nHostLen = lt_strlen(pUrl) + 1;
        }
        pUrl += nHostLen - 1;
    }

    if (*pPort == 0) return false;

    *pHost = lt_malloc(nHostLen);
    if (!*pHost) return false;
    lt_strncpyTerm(*pHost, pTemp, nHostLen);

    if ((pSep = lt_strstr(pUrl, "/"))) {
        pUrl += 1;
        bool bHasQuery = false;
        int nResourceLen;
        if ((pSep = lt_strstr(pUrl, "?"))) {
            nResourceLen = pSep - pUrl + 1;
            bHasQuery = true;
        } else {
            nResourceLen = lt_strlen(pUrl) + 1;
        }

        *pResource = lt_malloc(nResourceLen);
        if (!*pResource) return false;
        lt_strncpyTerm(*pResource, pUrl, nResourceLen);
        pUrl += nResourceLen - 1;
        if (bHasQuery) ltstring_set(pQuery, pUrl);
    } else {
        *pResource = lt_malloc(1);
        if (!*pResource) return false;
        **pResource = 0;
    }

    LTLOG_DEBUG("url.parse", "use_tls=%ld, host=%s, port=%ld, resource=%s, query=%s", LT_Pu32(*bUseTLS), *pHost, LT_Pu32(*pPort), *pResource, *pQuery);
    return true;
}

static bool
FreeTableItem(LTAssociativeArray *pArray, const void *pKey, u16 keySize, void *pValue, void *pClientData) {
    LT_UNUSED(pArray);
    LT_UNUSED(pKey);
    LT_UNUSED(keySize);
    LT_UNUSED(pClientData);
    lt_free(pValue);
    return true;
}

static void
ClearResponse(LTHttpResponse *response) {
    if (response->pHeaders) {
        response->pHeaders->API->Enumerate(response->pHeaders, FreeTableItem, NULL);
        lt_destroyobject(response->pHeaders);
    }
    s_pTokenizer->DestroyTokenizer(response->pTokenizer);
    lt_free(response->pProtocolVersion);
    lt_free(response->pReason);
    lt_memset(response, 0, sizeof(LTHttpResponse));
}

static void
LTHttpRequest_DestroyRequest(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    DestroyRequestSocket(pRequest);
    lt_destroyhandle(pRequest->hEvent);
    lt_free(pRequest->pTlsSpec);
    lt_free(pRequest->pHost);
    lt_free(pRequest->pResource);
    ltstring_destroy(pRequest->pContentType);
    ltstring_destroy(pRequest->pHeaderStr);
    ltstring_destroy(pRequest->pQueryStr);
    ltstring_destroy(pRequest->outString);
    ClearResponse(&pRequest->response);
    lt_free(pRequest->pMultipartData);
    *pRequest = (LTHttpRequestContext){};
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_OnRequestEvent(LTHttpRequest hRequest, LTHttpClient_RequestEventProc *pCallback, void *pClientData) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    s_pEvent->RegisterForEvent(pRequest->hEvent, pCallback, NULL, pClientData, false);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_NoRequestEvent(LTHttpRequest hRequest, LTHttpClient_RequestEventProc *pCallback) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    s_pEvent->UnregisterFromEvent(pRequest->hEvent, pCallback);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_EnableDebug(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    pRequest->debugRequest = true;
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static LTString
ArgToString(LTArgType argType, lt_va_list args) {
    LTString pValueStr = NULL;
    switch (argType) {
        case kLTArgType_ltsize:   ltstring_format(&pValueStr, "%llu", lt_va_arg(args, LT_SIZE));      break;
        case kLTArgType_charstar: ltstring_format(&pValueStr, "%s",   lt_va_arg(args, char *));       break;
        case kLTArgType_double:   ltstring_format(&pValueStr, "%f",   lt_va_arg(args, double));       break;
        case kLTArgType_u8:       ltstring_format(&pValueStr, "%u",   lt_va_arg(args, unsigned int)); break;
        case kLTArgType_u16:      ltstring_format(&pValueStr, "%u",   lt_va_arg(args, unsigned int)); break;
        case kLTArgType_u32:      ltstring_format(&pValueStr, "%lu",  lt_va_arg(args, u32));          break;
        case kLTArgType_u64:      ltstring_format(&pValueStr, "%llu", lt_va_arg(args, u64));          break;
        case kLTArgType_s8:       ltstring_format(&pValueStr, "%d",   lt_va_arg(args, int));          break;
        case kLTArgType_s16:      ltstring_format(&pValueStr, "%d",   lt_va_arg(args, int));          break;
        case kLTArgType_s32:      ltstring_format(&pValueStr, "%ld",  lt_va_arg(args, s32));          break;
        case kLTArgType_s64:      ltstring_format(&pValueStr, "%lld", lt_va_arg(args, s64));          break;
        default:                                                                                      break;
    }
    return pValueStr;
}

static void
LTHttpRequest_AddHeader(LTHttpRequest hRequest, const char *pName, LTArgType argType, ...) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;

    lt_va_list args;
    lt_va_start(args, argType);
    LTString pValueStr = ArgToString(argType, args);
    lt_va_end(args);

    if (pValueStr) {
        ltstring_append(&pRequest->pHeaderStr, pName);
        ltstring_append(&pRequest->pHeaderStr, ": ");
        ltstring_append(&pRequest->pHeaderStr, pValueStr);
        ltstring_append(&pRequest->pHeaderStr, "\r\n");
        ltstring_destroy(pValueStr);
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_ResetHeader(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;
    ltstring_empty(pRequest->pHeaderStr);
    if (pRequest->pHost) {
        ltstring_format(&pRequest->pHeaderStr, "Host: %s\r\n", pRequest->pHost); // add the same host name back
    }
    ltstring_empty(pRequest->outString);
    ClearResponse(&pRequest->response);
    pRequest->response.pHeaders = lt_createobject(LTAssociativeArray);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
EncodeParamComponent(LTString *pDestStr, const char *pSrcStr, ParamEncoding nEncoding) {
    char c;
    while ((c = *pSrcStr) != 0) {
        if (lt_isalnum(c) || (c == '-') || (c == '.') || (c == '_') || (c == '~') || /* Unreserved per RFC3986 and XForms 1.1 */
            ((nEncoding == kParamEncoding_RFC3986) &&                                /* Unreserved per RFC3986 only */
             ((c == '!') || (c == '$') || (c == '\'') || (c == '(') || (c == ')') ||
              (c == '*') || (c == '+') || (c == ',') || (c == ';') || (c == ':') ||
              (c == '@') || (c == '/') || (c == '?'))))
            ltstring_appendchar(pDestStr, c);
        else if ((nEncoding == kParamEncoding_XForms1p1) && c == ' ')                /* Replace ' ' with '+' for XForms 1.1 */
            ltstring_appendchar(pDestStr, '+');
        else {                                                                       /* Percent-encode everything else */
            static const char *hex = "0123456789ABCDEF";
            char encodedChar[4] = {'%', hex[(c >> 4) & 0xf], hex[c & 0xf], 0};
            ltstring_append(pDestStr, encodedChar);
        }
        ++pSrcStr;
    }
}

static void
LTHttpRequest_AddParam(LTHttpRequest hRequest, const char *pName, LTArgType argType, ...) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;

    lt_va_list args;
    lt_va_start(args, argType);
    LTString pValueStr = ArgToString(argType, args);
    lt_va_end(args);

    if (pValueStr) {
        LTString  pParamStr  = NULL;
        LTString *ppParamStr = &pParamStr;
        ParamEncoding nParamEncoding = kParamEncoding_RFC3986;
        if ((pRequest->nMethod == kLTHttpRequestMethod_GET) || (pRequest->nMethod == kLTHttpRequestMethod_HEAD)) {
            ppParamStr = &pRequest->pQueryStr;
        } else if (pRequest->nMethod == kLTHttpRequestMethod_POST || pRequest->nMethod == kLTHttpRequestMethod_PUT) {
            if (!pRequest->pContentType || lt_strcmp(pRequest->pContentType, "application/x-www-form-urlencoded") == 0) {
                if (!pRequest->pContentType) {
                    pRequest->pContentType = ltstring_create("application/x-www-form-urlencoded");
                }
                *ppParamStr = ltstring_create("");
                ltstring_appendbytes(ppParamStr, pRequest->pBodyData, pRequest->nBodySize);
                nParamEncoding = kParamEncoding_XForms1p1;
            } else {
                P("addparam.contenttype.err", "Invalid Content-Type already specified: %s", pRequest->pContentType);
                s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
                return;
            }
        }

        if (*ppParamStr) {
            if (!ltstring_isempty(*ppParamStr))
                ltstring_appendchar(ppParamStr, '&');
            else if (pRequest->nMethod != kLTHttpRequestMethod_POST && pRequest->nMethod != kLTHttpRequestMethod_PUT)
                ltstring_appendchar(ppParamStr, '?');
            EncodeParamComponent(ppParamStr, pName, nParamEncoding);
            ltstring_appendchar(ppParamStr, '=');
            EncodeParamComponent(ppParamStr, pValueStr, nParamEncoding);
        }
        if (pParamStr) ltstring_destroy(pParamStr);
        ltstring_destroy(pValueStr);
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_SetBody(LTHttpRequest hRequest, const char *pContentType, const void *pBodyData, u32 nBodyLength) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;

    // Clear the old body string, if it exists
    if (pRequest->pContentType) ltstring_destroy(pRequest->pContentType);
    pRequest->pContentType = ltstring_create(pContentType);

    if (lt_strcmp("multipart/form-data", pContentType) == 0) {
        LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(kMultipartBoundaryBytesSize, pRequest->multipartBoundary, kMultipartBoundaryStringSize);
        ltstring_appendformat(&pRequest->pContentType, "; boundary=%s", pRequest->multipartBoundary);
        pRequest->nNumParts = nBodyLength;
        P("multipart", "%lu parts, boundary: %s", LT_Pu32(pRequest->nNumParts), pRequest->multipartBoundary);
        pRequest->pMultipartData = lt_malloc(sizeof(LTHttpMultipartData) * pRequest->nNumParts);
        lt_memcpy(pRequest->pMultipartData, pBodyData, sizeof(LTHttpMultipartData) * pRequest->nNumParts);
        pRequest->nBodySize = 0;
        LTString partStr = ltstring_create("");
        for (u32 i = 0; i < pRequest->nNumParts; ++i) {
            const LTHttpMultipartData *part = &pRequest->pMultipartData[i];
            if (part->contentType == NULL) {
                // no more parts available at this time
                pRequest->multiPartIncomplete = true;
                break;
            }
            MakePartHeader(part, pRequest->multipartBoundary, &partStr);
            pRequest->nBodySize += lt_strlen(partStr) + part->size;
            ltstring_empty(partStr);
        }

        // Even if the parts are incomplete, we still need to add the epilog to the body size
        // This will ensure that nRequestDataLeft is greater than zero when a missing part if
        // encountered, allowing the state machine to suspend
        //
        MakePartEpilog(pRequest->multipartBoundary, &partStr);
        pRequest->nBodySize += lt_strlen(partStr);
        P("multipart.body", "%s body size: %lu", pRequest->multiPartIncomplete ? "Incomplete" : "Total", LT_Pu32(pRequest->nBodySize));
        ltstring_destroy(partStr);
    } else {
        pRequest->pBodyData = pBodyData;
        pRequest->nBodySize = nBodyLength;
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static void
LTHttpRequest_AddPendingParts(LTHttpRequest hRequest, const LTHttpMultipartData * pParts, u32 numAddedParts) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;

    // The Content-Type string now includes a boundary suffix, so only compare the type prefix
    if (lt_strncmp(pRequest->pContentType, s_multipartFormData, sizeof(s_multipartFormData) - 1) != 0) {
        LTLOG_YELLOWALERT("add.parts.wrong.type", "Cannot add form parts to request type %s", pRequest->pContentType);
        goto releaseHandlePrivData;
    }

    /* Find the first missing part */
    u32 nextPart;
    for (nextPart = 0; nextPart < pRequest->nNumParts; nextPart++) {
        if (pRequest->pMultipartData[nextPart].contentType == NULL) {
            break;
        }
    }

    if (!pParts && !numAddedParts) {
        // Caller has no more parts to send - end the request when the currently-available parts are done
        pRequest->nNumParts = nextPart;
        goto maybeRestartTransmission;
    }

    if (!pParts || !pParts->contentType) {
        LTLOG_YELLOWALERT("add.parts.missing", "Must supply at least one new form part");
        goto releaseHandlePrivData;
    }

    if (numAddedParts > pRequest->nNumParts - nextPart) {
        LTLOG_YELLOWALERT("add.parts.no.space", "No space to add %lu parts", LT_Pu32(numAddedParts));
        goto releaseHandlePrivData;
    }

    // Copy the new parts into the request and update the body size
    lt_memcpy(&pRequest->pMultipartData[nextPart], pParts, sizeof(LTHttpMultipartData) * numAddedParts);

    u32 extraBodySize = 0;
    LTString partStr = ltstring_create("");
    for (u32 i = 0; i < numAddedParts; ++i) {
        const LTHttpMultipartData *part = &pParts[i];
        MakePartHeader(part, pRequest->multipartBoundary, &partStr);
        extraBodySize += lt_strlen(partStr) + part->size;
        ltstring_empty(partStr);
    }
    pRequest->nBodySize += extraBodySize;
    pRequest->nRequestDataLeft += extraBodySize;
    P("add.parts", "added %lu to body size", LT_Pu32(extraBodySize));
    P("add.parts", "nRequestDataLeft up to: %lu", LT_Pu32(pRequest->nRequestDataLeft));
    ltstring_destroy(partStr);

maybeRestartTransmission:
    // Restart transmission if it was paused due to missing parts
    if (pRequest->state == kLTHttpRequestState_WaitingForNextPart) {
        PrepareSegmentCompletedForNextWrite(pRequest);
        if (pRequest->bWriteReady) DoSendRequestData(pRequest);
    }

releaseHandlePrivData:
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}


static void
LTHttpRequest_SetTlsSpec(LTHttpRequest hRequest, const char * pTlsSpec) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return;

    if (pRequest->pTlsSpec) lt_free(pRequest->pTlsSpec);
    pRequest->pTlsSpec = lt_strdup(pTlsSpec);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
}

static bool
LTHttpRequest_SendRequest(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return false;
    if (s_pCore->IsHandleValid(pRequest->hSocket)) {
        // a socket exists
        PrepareRequest(pRequest);
        if (pRequest->bWriteReady) DoSendRequestData(pRequest);
        s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
        return true;
    }
    pRequest->response.state = kLTHttpResponseParserState_Connecting;
    if (!pRequest->pHost) {
        pRequest->response.state = kLTHttpResponseParserState_Error;
        s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
        return false;
    }
    // TODO: only allocate what is necessary for SockSpec, with a max of 256bytes
    const int sockSpecLen = 256;
    char *sockSpec = lt_malloc(sockSpecLen);
    bool ret = false;
    if (sockSpec) {
        u32 ip_addr;
        const char *protocol = pRequest->bUseTLS ? "tls" : "tcp";
        const char *endpointType = StringToIPAddress(pRequest->pHost, (u8*)&ip_addr) ? "ip" : "host";

        const char *alpnStr;
        const char *tlsSpec;
        if (pRequest->bUseTLS) {
            alpnStr = "alpn: http/1.1";
            tlsSpec = pRequest->pTlsSpec ? pRequest->pTlsSpec : "";
        } else {
            alpnStr = "";
            tlsSpec = "";
        }

        lt_snprintf(sockSpec, sockSpecLen,  "%s %s: %s port: %d %s %s tcp-prio: 32", protocol, endpointType, pRequest->pHost, pRequest->nPort, alpnStr, tlsSpec);
        pRequest->hSocket = s_pNetCore->OpenSocket(pRequest->hTransport, sockSpec, OnSocketEvent, pRequest);
        lt_free(sockSpec);
        ret = (pRequest->hSocket != 0);
    }
    if (!ret) LTLOG_YELLOWALERT("createsocket.fail", NULL);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return ret;
}

static LTHttpRequest LTNetHttpClientImpl_CreateRequest(const char *pRequestUrl, LTHttpRequestMethod nMethod, LTHandle hTransport);

static u32
LTHttpRequest_GetResponseStatusCode(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    u32 code = pRequest->response.nStatusCode;
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return code;
}

static u32
LTHttpRequest_GetResponseContentLength(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    u32 len = pRequest->response.nContentLength;
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return len;
}

static const char *
LTHttpRequest_GetResponseHeader(LTHttpRequest hRequest, const char *pName) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return NULL;
    char *pNameLowerCase = lt_strlower(lt_strdup(pName));
    const char *pHeaderVal = LTCStringKeyedArray_Get(pRequest->response.pHeaders, pNameLowerCase, NULL);
    lt_free(pNameLowerCase);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return pHeaderVal;
}

static u32
LTHttpRequest_BodyDataAvailable(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    u32 nBodyBytesRemaining = (pRequest->response.state == kLTHttpResponseParserState_Body) ? pRequest->response.nBodyBytesRemaining : 0;
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return nBodyBytesRemaining;
}

static s32
LTHttpRequest_ReadBodyData(LTHttpRequest hRequest, void *pData, LT_SIZE nDataLen) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    if (pRequest->response.state != kLTHttpResponseParserState_Body) {
        s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
        return 0;
    }
    nDataLen = nDataLen < pRequest->response.nBodyBytesRemaining ? nDataLen : pRequest->response.nBodyBytesRemaining;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pRequest->hSocket);
    s32 nBytesRead = pSocket->ReadSocket(pRequest->hSocket, pData, nDataLen);
    if ((nBytesRead <= 0) &&
        (pRequest->response.nBodyBytesRemaining > 0) &&
        pRequest->bSocketDisconnected) {
        pRequest->response.state = kLTHttpResponseParserState_Error;
        s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
        s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
        return nBytesRead; // 0 or error code
    }

    pRequest->response.nBodyBytesRemaining -= nBytesRead;
    if (pRequest->response.nBodyBytesRemaining == 0) {
        switch (pRequest->response.transferEncoding) {
            default:
            case kTransferEncoding_Plain:
                pRequest->response.state = kLTHttpResponseParserState_Complete;
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_RequestComplete);
                break;
            case kTransferEncoding_Chunked:
                pRequest->response.state = kLTHttpResponseParserState_BodyChunkNewline;
                s_pTokenizer->SetSentinels(pRequest->response.pTokenizer, "\n");
                break;
        }
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return nBytesRead;
}

// TODO: merge this logic with DoSendRequestData() for less code duplication
static s32
LTHttpRequest_WriteBodyData(LTHttpRequest hRequest, const void *pData, LT_SIZE nDataLen) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    LT_ASSERT((pRequest->state == kLTHttpRequestState_SendBody) || (pRequest->state == kLTHttpRequestState_SendPartData));
    LT_ASSERT(pRequest->outSegmentData == NULL);
    s32 nWritten = 0;
    LT_SIZE nTotalWritten = 0;

    if (pRequest->outSegmentLeft > 0) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pRequest->hSocket);
        do { // Write until socket full or error
            LT_SIZE nToWrite = nDataLen - nTotalWritten;
            nWritten = pSocket->WriteSocket(pRequest->hSocket,(char *)pData + nTotalWritten, LT_MIN(pRequest->outSegmentLeft, (u32)nToWrite));
            /* For HTTPS/TLS, nWrite is 0 only if the input length is 0 or there is pending data in TLS to resend.
            *     Otherwise, nWrite is guaranteed either positive on success or negative on failure.
            * For HTTP/TCP, nWrite could be 0 if TCP asks for retransmission.
            */
            if (nWritten < 0) {
                LTLOG("body.wr.err", "err code %ld", LT_Ps32(nWritten));
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
                s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
                return -1; // error code
            }
            pRequest->nRequestDataLeft -= nWritten;
            pRequest->outSegmentLeft -= nWritten;
            nTotalWritten += nWritten;
        } while (nWritten > 0);

        if (pRequest->outSegmentLeft == 0) {
            PrepareSegmentCompletedForNextWrite(pRequest);
        }
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return nTotalWritten;
}

// TODO: merge this logic with DoSendRequestData() for less code duplication
static s32
LTHttpRequest_StreamBodyData(LTHttpRequest hRequest, void *pData, LT_SIZE nDataLen) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return 0;
    LT_ASSERT((pRequest->state == kLTHttpRequestState_SendBody) || (pRequest->state == kLTHttpRequestState_SendPartData));
    LT_ASSERT(pRequest->outSegmentData == NULL);
    s32 nWritten = 0;
    LT_SIZE nTotalWritten = 0;

    if (pRequest->outSegmentLeft == 0) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pRequest->hSocket); // initialize socket
        do { // Write until socket full or error
            LT_SIZE nToWrite = nDataLen - nTotalWritten;

            nWritten = pSocket->WriteSocket(pRequest->hSocket,(char *)pData + nTotalWritten, (u32)nToWrite);

            if (nWritten < 0) {
                LTLOG("body.wr.err.stream", "err code %ld", LT_Ps32(nWritten));
                s_pEvent->NotifyEvent(pRequest->hEvent, pRequest->hHandle, LTHttpRequestEvent_Error);
                s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
                return LTHttpStreamError; // error code
            }
            nTotalWritten += nWritten;
        } while (nWritten > 0);
    }
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return nTotalWritten;
}

static s32
LTHttpRequest_WritePartBoundary(LTHttpRequest hRequest) {
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!pRequest) return LTHttpStreamError;
    LT_ASSERT((pRequest->state == kLTHttpRequestState_SendBody) || (pRequest->state == kLTHttpRequestState_SendPartData));
    s32 streamState = LTHttpStreamError;
    ltstring_empty(pRequest->outString);
    if (++pRequest->nCurrentPart < pRequest->nNumParts) {
        PreparePartHeader(pRequest);
        pRequest->state = kLTHttpRequestState_SendPartHeader;
        streamState = LTHttpStreamPartDone;
    } else {
        PreparePartEpilog(pRequest);
        pRequest->state = kLTHttpRequestState_SendPartEpilog;
        streamState = LTHttpStreamEpilogDone;
    }
    DoSendRequestData(pRequest);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    return streamState;
}

static bool
LTHttpRequest_ResetRequest(LTHttpRequest hRequest) {
    LTHttpRequestContext *request = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!request) return false; // Cannot reset.
    // clean request data
    DestroyRequestSocket(request);
    lt_destroyhandle(request->hEvent);
    request->hEvent = 0;
    ltstring_empty(request->outString);
    ClearResponse(&request->response);
    // recreate
    bool ret = false;
    do {
        if (!(request->response.pHeaders = lt_createobject(LTAssociativeArray))) break;
        if (!(request->hEvent = s_pCore->CreateEvent(&s_RequestEventArgs, DispatchRequestEventCB, NULL, NULL, NULL))) break;
        ret = true;
    } while (false);
    s_pCore->ReleaseHandlePrivateData(hRequest, request);
    return ret;
}

static void
LTHttpRequest_DestroyRequestSocket(LTHttpRequest hRequest) {
    LTHttpRequestContext *request = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!request) return;
    DestroyRequestSocket(request);
    s_pCore->ReleaseHandlePrivateData(hRequest, request);
}

static LTHttpRequest
LTNetHttpClientImpl_CreateRequest(const char *pRequestUrl, LTHttpRequestMethod nMethod, LTHandle hTransport) {
    LTHttpRequest hRequest = s_pCore->CreateHandle((LTInterface*)&s_ILTHttpRequest, sizeof(LTHttpRequestContext));
    if (!hRequest) return 0;
    LTHttpRequestContext *pRequest = s_pCore->ReserveHandlePrivateData(hRequest);
    lt_memset(pRequest, 0, sizeof(LTHttpRequestContext));
    pRequest->hHandle = hRequest;
    pRequest->nMethod = nMethod;
    bool bCreated = false;
    do {
        if (!(pRequest->outString  = ltstring_create(""))) break;
        if (!(pRequest->pHeaderStr = ltstring_create(""))) break;
        if (!(pRequest->pQueryStr  = ltstring_create(""))) break;
        pRequest->pTlsSpec = NULL;
        pRequest->hTransport = hTransport;
        pRequest->hEvent = s_pCore->CreateEvent(&s_RequestEventArgs, DispatchRequestEventCB, NULL, NULL, NULL);
        if (!ParseUrl(pRequestUrl, &pRequest->bUseTLS, &pRequest->pHost, &pRequest->nPort, &pRequest->pResource, &pRequest->pQueryStr)) break;
        if (!(pRequest->response.pHeaders = lt_createobject(LTAssociativeArray))) break;
        ltstring_format(&pRequest->pHeaderStr, "Host: %s\r\n", pRequest->pHost);
        bCreated = true;
    } while (0);
    s_pCore->ReleaseHandlePrivateData(hRequest, pRequest);
    if (!bCreated) {
        LTHttpRequest_DestroyRequest(hRequest);
        hRequest = 0;
        LTLOG_YELLOWALERT("req.fail", "Fail to create request %s", pRequestUrl);
    }
    return hRequest;
}

static void
LTHttpRequest_SetEndpoint(LTHttpRequest hRequest, const char * pEndpoint) {
    LTHttpRequestContext *request = s_pCore->ReserveHandlePrivateData(hRequest);
    if (!request) return;
    lt_free(request->pResource);
    u32 len = lt_strlen(pEndpoint) + 1;
    request->pResource = lt_malloc(len);
    if (request->pResource) lt_strncpyTerm(request->pResource, pEndpoint, len);
    s_pCore->ReleaseHandlePrivateData(hRequest, request);
    return;
}

static void
ArgsToRequestParams(LTHttpRequest hRequest, u32 nNumParams, lt_va_list args) {
    for (u32 i = 0; i < nNumParams; ++i) {
        const char *pName = lt_va_arg(args, char *);
        const char *pValue = lt_va_arg(args, char *);
        LTHttpRequest_AddParam(hRequest, pName, kLTArgType_charstar, pValue);
    }
}

static LTHttpRequest
LTNetHttpClientImpl_Get(const char *pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc *pCallback, void *pClientData) {
    LTHttpRequest hRequest = LTNetHttpClientImpl_CreateRequest(pRequestUrl, kLTHttpRequestMethod_GET, hTransport);
    LTHttpRequest_OnRequestEvent(hRequest, pCallback, pClientData);
    LTHttpRequest_SendRequest(hRequest);
    return hRequest;
}

static LTHttpRequest
LTNetHttpClientImpl_GetWithParams(const char *pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc *pCallback, void *pClientData, u32 nNumParams, ...) {
    LTHttpRequest hRequest = LTNetHttpClientImpl_CreateRequest(pRequestUrl, kLTHttpRequestMethod_GET, hTransport);
    LTHttpRequest_OnRequestEvent(hRequest, pCallback, pClientData);

    lt_va_list args;
    lt_va_start(args, nNumParams);
    ArgsToRequestParams(hRequest, nNumParams, args);
    lt_va_end(args);

    LTHttpRequest_SendRequest(hRequest);
    return hRequest;
}

static LTHttpRequest
LTNetHttpClientImpl_Post(const char *pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc *pCallback, void *pClientData, u32 nNumParams, ...) {
    LTHttpRequest hRequest = LTNetHttpClientImpl_CreateRequest(pRequestUrl, kLTHttpRequestMethod_POST, hTransport);
    LTHttpRequest_OnRequestEvent(hRequest, pCallback, pClientData);

    lt_va_list args;
    lt_va_start(args, nNumParams);
    ArgsToRequestParams(hRequest, nNumParams, args);
    lt_va_end(args);

    LTHttpRequest_SendRequest(hRequest);
    return hRequest;
}

static LTHttpRequest
LTNetHttpClientImpl_Put(const char *pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc *pCallback, void *pClientData, u32 nNumParams, ...) {
    LTHttpRequest hRequest = LTNetHttpClientImpl_CreateRequest(pRequestUrl, kLTHttpRequestMethod_PUT, hTransport);
    LTHttpRequest_OnRequestEvent(hRequest, pCallback, pClientData);

    lt_va_list args;
    lt_va_start(args, nNumParams);
    ArgsToRequestParams(hRequest, nNumParams, args);
    lt_va_end(args);

    LTHttpRequest_SendRequest(hRequest);
    return hRequest;
}

/*___________________________________________
 / LTSystemHttpClient library initialization */

// only called by LibInit and LibFini
void ShutdownHttpClient(void) {
    /* Cleanup */
    /* Close libraries */
    lt_closelibrary(s_pNetCore);
    s_pNetCore      = NULL;
    lt_closelibrary(s_pTokenizer);
    s_pTokenizer    = NULL;

    s_pThread       = NULL;
    s_pEvent        = NULL;
    s_pCore         = NULL;
}

static bool
LTNetHttpClientImpl_LibInit(void) {
    // Core and core interfaces
    s_pCore         = LT_GetCore();
    s_pThread       = lt_getlibraryinterface(ILTThread, s_pCore);
    s_pEvent        = lt_getlibraryinterface(ILTEvent, s_pCore);

    // Open the libraries
    // Get the necessary interfaces
    do {
        s_pTokenizer = lt_openlibrary(LTUtilityTokenizer);
        if (!s_pTokenizer) break;
        s_pNetCore = lt_openlibrary(LTNetCore);
        if (!s_pNetCore) break;
        return true;
    } while (false);

    ShutdownHttpClient();
    return false;
}

static void
LTNetHttpClientImpl_LibFini(void) {
    ShutdownHttpClient();
}

/*___________________________________________________
 / LTSystemHttpClient library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTNetHttpClient,) {
    .CreateRequest             = &LTNetHttpClientImpl_CreateRequest,
    .Get                       = &LTNetHttpClientImpl_Get,
    .GetWithParams             = &LTNetHttpClientImpl_GetWithParams,
    .Post                      = &LTNetHttpClientImpl_Post,
    .Put                       = &LTNetHttpClientImpl_Put,
} LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTHttpRequest, LTHttpRequest_DestroyRequest) {
    /* Events */
    .OnRequestEvent            = &LTHttpRequest_OnRequestEvent,
    .NoRequestEvent            = &LTHttpRequest_NoRequestEvent,
    .EnableDebug               = &LTHttpRequest_EnableDebug,

    /* Request building */
    .SetEndpoint               = &LTHttpRequest_SetEndpoint,
    .AddHeader                 = &LTHttpRequest_AddHeader,
    .ResetHeader               = &LTHttpRequest_ResetHeader,
    .AddParam                  = &LTHttpRequest_AddParam,
    .SetBody                   = &LTHttpRequest_SetBody,
    .AddPendingParts           = &LTHttpRequest_AddPendingParts,
    .SetTlsSpec                = &LTHttpRequest_SetTlsSpec,
    .SendRequest               = &LTHttpRequest_SendRequest,
    .WriteBodyData             = &LTHttpRequest_WriteBodyData,
    .StreamBodyData            = &LTHttpRequest_StreamBodyData,
    .WritePartBoundary         = &LTHttpRequest_WritePartBoundary,
    .ResetRequest              = &LTHttpRequest_ResetRequest,
    .DestroyRequestSocket      = &LTHttpRequest_DestroyRequestSocket,

    /* Response handling */
    .GetResponseStatusCode     = &LTHttpRequest_GetResponseStatusCode,
    .GetResponseContentLength  = &LTHttpRequest_GetResponseContentLength,
    .GetResponseHeader         = &LTHttpRequest_GetResponseHeader,
    .BodyDataAvailable         = &LTHttpRequest_BodyDataAvailable,
    .ReadBodyData              = &LTHttpRequest_ReadBodyData,
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Feb-22   trajan      created
 *  25-Jan-23   titus       ported to LTNetCore
 *  29-Sep-23   aurelian    added http queue system
 *  22-Jul-24   decius      added streaming function
 */
