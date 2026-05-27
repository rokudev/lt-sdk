/*******************************************************************************
 * source/lt/net/srtpproxy/server/LTNetSrtpProxyServer.c - LTSrtp proxy server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/net/socketproxy/client/LTSocketProxyClient.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include "../LTNetSrtpProxy.h"

DEFINE_LTLOG_SECTION("srtp.prx.srv");

typedef struct {
    LTList_Node    node;
    u32            id;
    LTSrtpSession *session;
    LTSocket       dtlsSocket;
    LTSocket       transportSocket;
    LTString       sessionId;
    LTThread       hThread;
} ProxiedSession;

typedef struct {
    LTList_Node          node;
    u32                  id;
    u32                  nSyncSource;
    LTRtpStream         *stream;
    LTRtpStreamDirection direction;
    LTRtpExtensionMap    extMap;
    LTRtpPayloadFormat   payloadFormat;
    LTThread             hThread;
} ProxiedStream;

static LTCore               *s_pCore;
static LTAtomic              s_NextId;
static LTDeviceSdioPort     *s_SdioPort;
static LTOThread            *s_thread;
static LTOThread            *s_rxThread;
static LTUtilityMessagePack *s_mpack;
static LTSocketProxyClient  *s_socketProxyClient;
static LTList                s_Sessions;
static LTList                s_Streams;
static ILTThread            *s_pThread;

static ProxiedSession *ProxiedSession_FindById(u32 id) {
    LTList_ForEach(pNode, &s_Sessions) {
        ProxiedSession *prxSession = LTList_GetNodeDataOfType(ProxiedSession, pNode);
        if (prxSession->id == id) return prxSession;
    } LTList_EndForEach;
    return NULL;
}

static ProxiedStream *ProxiedStream_FindById(u32 id) {
    LTList_ForEach(pNode, &s_Streams) {
        ProxiedStream *prxStream = LTList_GetNodeDataOfType(ProxiedStream, pNode);
        if (prxStream->id == id) return prxStream;
    } LTList_EndForEach;
    return NULL;
}

static void HandleSession_Init(u32 id, LTBuffer *packet) {
    const char *sessionId = s_mpack->CopyString(packet);
    u32 dtlsSocketId, transportSocketId;
    s_mpack->ReadIntU32(packet, &dtlsSocketId);
    s_mpack->ReadIntU32(packet, &transportSocketId);

    ProxiedSession* prxSession = lt_malloc(sizeof(ProxiedSession));
    if (!prxSession) {
        LTLOG_YELLOWALERT("sess.init.oom", NULL);
        goto done;
    }
    prxSession->id = id;
    prxSession->dtlsSocket = s_socketProxyClient->GetSocket(dtlsSocketId);
    prxSession->transportSocket = s_socketProxyClient->GetSocket(transportSocketId);
    prxSession->sessionId = ltstring_create("");
    ltstring_set(&prxSession->sessionId, sessionId);

    char threadName[20] = {};
    lt_snprintf(threadName, sizeof(threadName), "Webrtc-%5s", sessionId);
    prxSession->hThread = s_pCore->CreateThread(threadName);
    s_pThread->SetStackSize(prxSession->hThread, 2048);
    s_pThread->SetThreadSpecificClientData(prxSession->hThread, "session", NULL, prxSession);
    s_pThread->Start(prxSession->hThread, NULL, NULL);
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            prxSession->session = lt_createobject(LTSrtpSession);
            prxSession->session->API->Init(prxSession->session,
                                            prxSession->sessionId,
                                            prxSession->transportSocket,
                                            prxSession->dtlsSocket);
        }
        &Foo;
    }), NULL, prxSession);

    LTList_AddTail(&s_Sessions, &prxSession->node);
done:
    s_mpack->FreeString(packet, sessionId);
}

static void OnSessionEvent(LTSrtpSession *session, LTSrtpSession_Event event, void * pClientData) {
    LT_UNUSED(session);
    ProxiedSession* prxSession = pClientData;

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_Event);
    s_mpack->WriteIntU32(message, prxSession->id);
    s_mpack->WriteIntU32(message, event);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("ose.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void SendDestroyDone(void *clientData) {
    u32 id = (u32)clientData;
    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Destroy_Done);
    s_mpack->WriteIntU32(message, id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("dtor.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void HandleSession_Destroy(u32 id) {
    ProxiedSession* prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("rev.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            LTList_Remove(&prxSession->node);
            lt_destroyobject(prxSession->session);
            ltstring_destroy(prxSession->sessionId);
            s_thread->API->QueueTaskProc(s_thread, SendDestroyDone, NULL, (void*)prxSession->id);
            lt_destroyhandle(prxSession->hThread);
            lt_free(prxSession);
            prxSession = NULL;
        }
        &Foo;
    }), NULL, prxSession);
}

static void HandleSession_RegisterForEvents(u32 id) {
    ProxiedSession* prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("rev.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            prxSession->session->API->OnSessionEvent(prxSession->session, OnSessionEvent, NULL, prxSession);
        }
        &Foo;
    }), NULL, prxSession);
}

static void HandleSession_UnregisterFromEvents(u32 id) {
    ProxiedSession* prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("uev.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            prxSession->session->API->NoSessionEvent(prxSession->session, OnSessionEvent);
        }
        &Foo;
    }), NULL, prxSession);
}

static void HandleSession_OpenStream(u32 id, LTBuffer *packet) {
    ProxiedStream* prxStream = lt_malloc(sizeof(ProxiedStream));
    if (!prxStream) {
        LTLOG_YELLOWALERT("sess.openstream.oom", NULL);
        return;
    }

    LTRtpExtensionMap  *pExtMap = &prxStream->extMap;
    LTRtpPayloadFormat *pPayloadFormat = &prxStream->payloadFormat;

    s_mpack->ReadIntU32(packet, &prxStream->id);
    s_mpack->ReadIntU32(packet, &prxStream->direction);
    s_mpack->ReadIntU32(packet, &prxStream->nSyncSource);
    s_mpack->ReadIntU8(packet, &pExtMap->nTransportWideCC);
    s_mpack->ReadIntU32(packet, &pPayloadFormat->mediaFormat.nKind);
    s_mpack->ReadIntU32(packet, &pPayloadFormat->mediaFormat.nEncoding);
    switch (pPayloadFormat->mediaFormat.nEncoding) {
        case kLTMediaEncoding_H264:
            s_mpack->ReadIntU32(packet, &pPayloadFormat->mediaFormat.params.h264.nClockRate);
            s_mpack->ReadIntU8(packet, &pPayloadFormat->mediaFormat.params.h264.nProfile);
            s_mpack->ReadIntU8(packet, &pPayloadFormat->mediaFormat.params.h264.nConstraints);
            s_mpack->ReadIntU8(packet, &pPayloadFormat->mediaFormat.params.h264.nLevel);
            s_mpack->ReadIntU8(packet, &pPayloadFormat->mediaFormat.params.h264.nPacketizationMode);
            s_mpack->ReadIntU16(packet, &pPayloadFormat->mediaFormat.params.h264.nWidth);
            s_mpack->ReadIntU16(packet, &pPayloadFormat->mediaFormat.params.h264.nHeight);
            break;
        case kLTMediaEncoding_PCMU:
        case kLTMediaEncoding_PCMA:
            s_mpack->ReadIntU32(packet, &pPayloadFormat->mediaFormat.params.g711.nSampleRate);
            break;

        default:
            break;
    }
    s_mpack->ReadIntU32(packet, &pPayloadFormat->nPayloadType);
    s_mpack->ReadIntU32(packet, &pPayloadFormat->nRtcpFeedbackFlags);

    ProxiedSession *prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("prx.sess.nf", "Failed to find proxied session: %lu", LT_Pu32(id));
        return;
    }
    prxStream->hThread = prxSession->hThread;
    LTList_AddTail(&s_Streams, &prxStream->node);

    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = s_pThread->GetThreadSpecificClientData(s_pThread->GetCurrentThread(), "session");
            ProxiedStream* prxStream = clientData;
            LTRtpStream *stream = prxSession->session->API->OpenStream(
                    prxSession->session,
                    prxStream->direction,
                    prxStream->nSyncSource,
                    &prxStream->extMap,
                    &prxStream->payloadFormat);
            prxStream->stream = stream;
        }
        &Foo;
    }), NULL, prxStream);
}

static void HandleSession_Pause(u32 id) {
    ProxiedSession* prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("pause.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            prxSession->session->API->Pause(prxSession->session);
        }
        &Foo;
    }), NULL, prxSession);
}

static void HandleSession_Resume(u32 id) {
    ProxiedSession* prxSession = ProxiedSession_FindById(id);
    if (!prxSession) {
        LTLOG_YELLOWALERT("resume.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxSession->hThread, ({
        void Foo(void *clientData) {
            ProxiedSession* prxSession = clientData;
            prxSession->session->API->Resume(prxSession->session);
        }
        &Foo;
    }), NULL, prxSession);
}

static void HandleStream_Pause(u32 id) {
    ProxiedStream* prxStream = ProxiedStream_FindById(id);
    if (!prxStream) {
        LTLOG_YELLOWALERT("pause.stm.nf", "Failed to find stream with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxStream->hThread, ({
        void Foo(void *clientData) {
            ProxiedStream* prxStream = clientData;
            prxStream->stream->API->Pause(prxStream->stream);
        }
        &Foo;
    }), NULL, prxStream);
}

static void HandleStream_Resume(u32 id) {
    ProxiedStream* prxStream = ProxiedStream_FindById(id);
    if (!prxStream || !prxStream->stream) {
        LTLOG_YELLOWALERT("resume.stm.nf", "Failed to find stream with id: %lu", LT_Pu32(id));
        return;
    }
    s_pThread->QueueTaskProc(prxStream->hThread, ({
        void Foo(void *clientData) {
            ProxiedStream* prxStream = clientData;
            prxStream->stream->API->Resume(prxStream->stream);
        }
        &Foo;
    }), NULL, prxStream);
}

static void HandleStream_Destroy(u32 id) {
    ProxiedStream* prxStream = ProxiedStream_FindById(id);
    if (!prxStream) {
        LTLOG("dtor.stm.nf", "Failed to find stream with id: %lu", LT_Pu32(id));
        return;
    }
    // no need to destroy prxStream->stream as it is destroyed in SrtpSession
    LTList_Remove(&prxStream->node);
    lt_free(prxStream);
}

static void SdioReadCallback(LTDeviceSdioPort *port, u32 recvLen, void *clientData) {
    LT_UNUSED(port); LT_UNUSED(clientData);

    LTBuffer *packet = lt_createobject(LTBuffer);
    packet->API->Reserve(packet, recvLen);
    LTBufferAccess write = packet->API->StartWrite(packet, recvLen);

    if (!s_SdioPort->API->Read(s_SdioPort, write.span.data)) {
        LTLOG_YELLOWALERT("rdcb.rd.err", "SDIO read failed");
        goto done;
    }
    packet->API->FinishWrite(packet, write);

    u32 packetType, id;
    if (!s_mpack->ReadIntU32(packet, &packetType)) {
        LTLOG_YELLOWALERT("rdcb.pt.err", "Failed to read packet type from message");
        goto done;
    }
    if (!s_mpack->ReadIntU32(packet, &id)) {
        LTLOG_YELLOWALERT("rdcb.id.err", "Failed to read session id from message");
        goto done;
    }
    switch (packetType) {
        case kLTSrtpProxy_PacketType_Session_Init:
            HandleSession_Init(id, packet);
            break;

        case kLTSrtpProxy_PacketType_Session_Destroy:
            HandleSession_Destroy(id);
            break;

        case kLTSrtpProxy_PacketType_Session_RegisterForEvents:
            HandleSession_RegisterForEvents(id);
            break;

        case kLTSrtpProxy_PacketType_Session_UnregisterFromEvents:
            HandleSession_UnregisterFromEvents(id);
            break;

        case kLTSrtpProxy_PacketType_Session_OpenStream:
            HandleSession_OpenStream(id, packet);
            break;

        case kLTSrtpProxy_PacketType_Session_Pause:
            HandleSession_Pause(id);
            break;

        case kLTSrtpProxy_PacketType_Session_Resume:
            HandleSession_Resume(id);
            break;

        case kLTSrtpProxy_PacketType_Stream_Pause:
            HandleStream_Pause(id);
            break;

        case kLTSrtpProxy_PacketType_Stream_Resume:
            HandleStream_Resume(id);
            break;

        case kLTSrtpProxy_PacketType_Stream_Destroy:
            HandleStream_Destroy(id);
            break;

        default:
            LTLOG_YELLOWALERT("pk.unhandled", "packetType %lu", LT_Pu32(packetType));
            break;
    }
done:
    lt_destroyobject(packet);
}

static bool Thread_Init(void) {
    if (!(s_socketProxyClient = lt_openlibrary(LTSocketProxyClient))) return false;
    if (!(s_mpack = lt_openlibrary(LTUtilityMessagePack))) return false;
    if (!(s_SdioPort = lt_createobject(LTDeviceSdioPort))) return false;
    if (!s_SdioPort->API->Bind(s_SdioPort, kLTDeviceSdioPort_Srtp, SdioReadCallback, NULL)) return false;
    return true;
}

static void Thread_Exit(void) {
    lt_destroyobject(s_SdioPort);           s_SdioPort = NULL;
    lt_closelibrary(s_mpack);               s_mpack = NULL;
    lt_closelibrary(s_socketProxyClient);   s_socketProxyClient = NULL;
}

static void LTNetSrtpProxyServerImpl_LibFini(void) {
    lt_destroyobject(s_thread);
    lt_destroyobject(s_rxThread);
}

static bool LTNetSrtpProxyServerImpl_LibInit(void) {
    LTList_Init(&s_Sessions);
    LTList_Init(&s_Streams);
    if (!(s_pCore   = LT_GetCore())) return false;
    if (!(s_pThread = lt_getlibraryinterface(ILTThread, s_pCore))) return false;

    if (!(s_thread = lt_createobject(LTOThread))) return false;
    s_thread->API->SetStackSize(s_thread, 1024);
    s_thread->API->Start(s_thread, "srtp.prx.srv", NULL, NULL);

    if (!(s_rxThread = lt_createobject(LTOThread))) return false;
    s_rxThread->API->SetStackSize(s_rxThread, 2048);
    s_rxThread->API->Start(s_rxThread, "srtp.prx.srv.rx", Thread_Init, Thread_Exit);
    return true;
}

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetSrtpProxyServer, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetSrtpProxyServer, ) LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Oct-24   trajan      created
 */
