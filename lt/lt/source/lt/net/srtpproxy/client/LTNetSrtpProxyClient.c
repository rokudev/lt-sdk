/*******************************************************************************
 * source/lt/net/srtpproxy/client/LTNetSrtpProxyClient.c - Secure Real-time Transport Protocol (SRTP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTMonitor.h>
#include <lt/core/LTArray.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/net/socketproxy/server/LTSocketProxyServer.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include "../LTNetSrtpProxy.h"

DEFINE_LTLOG_SECTION("srtp.prx.clt");

static LTDeviceSdioPort     *s_SdioPort;
static LTUtilityMessagePack *s_mpack;
static LTOThread            *s_thread;
static ILTThread            *s_iThread;
static ILTEvent             *s_event;
static LTAtomic              s_NextId;
static LTList                s_Sessions;
static LTList                s_Streams;

typedef_LTObjectImpl(LTSrtpSession, LTSrtpSessionImpl) {
    LTList_Node      node;
    u32              id;
    bool             bClosed;
    LTEvent          hEvent;
    LTArray         *pStreams;
    LTMonitor       *monitor;
    LTProxiedSocket *dtlsSocket;
    LTProxiedSocket *transportSocket;
} LTOBJECT_API;

typedef_LTObjectImpl(LTRtpStream, LTRtpStreamImpl) {
    LTList_Node      node;
    u32              id;
    u32              syncSource;
    bool             bClosed;
} LTOBJECT_API;

/*_______________________________
/ LTSrtpSession implementation */
static LTRtpStream *
LTSrtpSessionImpl_OpenStream(LTSrtpSessionImpl *session, LTRtpStreamDirection direction, u32 nSyncSource, LTRtpExtensionMap *pExtMap, LTRtpPayloadFormat *pPayloadFormat) {
    LTRtpStreamImpl *stream = (LTRtpStreamImpl *)lt_createobject(LTRtpStream);
    if (!stream) {
        LTLOG_YELLOWALERT("open.stream.err", "Failed to open rtp stream %ld", pPayloadFormat->mediaFormat.nEncoding);
        return NULL;
    }
    session->pStreams->API->Append(session->pStreams, stream);
    stream->syncSource = nSyncSource;

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_OpenStream);
    s_mpack->WriteIntU32(message, session->id);
    s_mpack->WriteIntU32(message, stream->id);
    s_mpack->WriteIntU32(message, direction);
    s_mpack->WriteIntU32(message, nSyncSource);
    s_mpack->WriteIntU32(message, pExtMap->nTransportWideCC);
    s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.nKind);
    s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.nEncoding);
    switch (pPayloadFormat->mediaFormat.nEncoding) {
        case kLTMediaEncoding_H264:
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nClockRate);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nProfile);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nConstraints);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nLevel);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nPacketizationMode);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nWidth);
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.h264.nHeight);
            break;
        case kLTMediaEncoding_PCMU:
        case kLTMediaEncoding_PCMA:
            s_mpack->WriteIntU32(message, pPayloadFormat->mediaFormat.params.g711.nSampleRate);
            break;

        default:
            break;
    }
    s_mpack->WriteIntU32(message, pPayloadFormat->nPayloadType);
    s_mpack->WriteIntU32(message, pPayloadFormat->nRtcpFeedbackFlags);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.os.write.err", NULL);
    }
    lt_destroyobject(message);
    return (LTRtpStream *)stream;
}

static void
OnTransportSocketInactivity(LTProxiedSocket_Event event, void * pClientData) {
    LTSrtpSessionImpl *session = pClientData;
    if (!session) return;
    LTLOG_YELLOWALERT("on.tport.skt.inact", "Notify media error on inactivity");
    if (event == LTProxiedSocket_Event_NoActivity) {
        session->bClosed = true;
        if (session->transportSocket) session->transportSocket->API->Close(session->transportSocket);
        if (session->dtlsSocket) session->dtlsSocket->API->Close(session->dtlsSocket);
        if (session->pStreams) {
            for (u32 i = 0; i < session->pStreams->API->GetCount(session->pStreams); ++i) {
                LTRtpStreamImpl *stream = session->pStreams->API->Get(session->pStreams, i, NULL);
                stream->bClosed = true;
            }
        }
        s_event->NotifyEvent(session->hEvent, session, kLTSrtpSession_Event_MediaError);
    }
}

static void
LTSrtpSessionImpl_Init(LTSrtpSessionImpl *session, const char *sessionId, LTSocket hTransportSocket, LTSocket hDtlsSocket) {
    session->dtlsSocket      = lt_createobject(LTProxiedSocket);
    session->dtlsSocket->API->Init(session->dtlsSocket, hDtlsSocket);
    session->transportSocket = lt_createobject(LTProxiedSocket);
    session->transportSocket->API->Init(session->transportSocket, hTransportSocket);
    session->transportSocket->API->OnEvent(session->transportSocket, OnTransportSocketInactivity, NULL, session);

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_Init);
    s_mpack->WriteIntU32(message, session->id);
    s_mpack->WriteCString(message, sessionId, false);
    s_mpack->WriteIntU32(message, session->dtlsSocket->API->GetId(session->dtlsSocket));
    s_mpack->WriteIntU32(message, session->transportSocket->API->GetId(session->transportSocket));

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.i.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void
DispatchSessionEvent(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTSrtpSession_SessionEventProc *pCallback = (LTSrtpSession_SessionEventProc *)pEventProc;
    LTSrtpSession *session    = LTArgs_pointerAt(0, pEventArgs);
    LTSrtpSession_Event event = LTArgs_u32At(1, pEventArgs);
    pCallback(session, event, pEventProcClientData);
}

static bool
LTSrtpSessionImpl_ConstructObject(LTSrtpSessionImpl *session) {
    session->id = LTAtomic_FetchAdd(&s_NextId, 1);
    static const LTArgsDescriptor argsDescriptor = {2, {kLTArgType_pointer, kLTArgType_u32}};
    session->hEvent = LT_GetCore()->CreateEvent(&argsDescriptor, DispatchSessionEvent, NULL, NULL, NULL);
    session->pStreams = lt_createobject_typed(LTArray, List);
    if (!(session->monitor = lt_createobject(LTMonitor))) return false;
    LTList_AddTail(&s_Sessions, &session->node);
    session->bClosed = false;
    return true;
}

static void
LTSrtpSessionImpl_DestructObject(LTSrtpSessionImpl *session) {
    if (session->pStreams) {
        for (u32 i = 0; i < session->pStreams->API->GetCount(session->pStreams); ++i) {
            LTRtpStreamImpl *stream = session->pStreams->API->Get(session->pStreams, i, NULL);
            lt_destroyobject(stream);
        }
        lt_destroyobject(session->pStreams);
        session->pStreams = NULL;
    }

    if (session->transportSocket && !session->bClosed) {
        LTBuffer *message = lt_createobject(LTBuffer);
        message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
        s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_Destroy);
        s_mpack->WriteIntU32(message, session->id);
        LTBufferAccess access = message->API->StartRead(message, 0);
        if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
            LTLOG_YELLOWALERT("sess.dtor.write.err", NULL);
        }
        lt_destroyobject(message);

        session->monitor->API->Enter(session->monitor);
        if (!session->monitor->API->Wait(session->monitor, LTTime_Milliseconds(1000))) {
            LTLOG_YELLOWALERT("dtor.nm.tout", "socket write timeout");
        }
        session->monitor->API->Exit(session->monitor);
    }

    if (session->transportSocket) session->transportSocket->API->NoEvent(session->transportSocket, OnTransportSocketInactivity);
    LTList_Remove(&session->node);
    lt_destroyhandle(session->hEvent);
    lt_destroyobject(session->dtlsSocket);
    lt_destroyobject(session->transportSocket);
    lt_destroyobject(session->monitor);
}

static void
LTSrtpSessionImpl_OnSessionEvent(LTSrtpSessionImpl *session, LTSrtpSession_SessionEventProc pCallback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    s_event->RegisterForEvent(session->hEvent, pCallback, pClientDataReleaseProc, pClientData, false);

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_RegisterForEvents);
    s_mpack->WriteIntU32(message, session->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.ose.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void
LTSrtpSessionImpl_NoSessionEvent(LTSrtpSessionImpl *session, LTSrtpSession_SessionEventProc pCallback) {
    s_event->UnregisterFromEvent(session->hEvent, pCallback);

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_UnregisterFromEvents);
    s_mpack->WriteIntU32(message, session->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.nse.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void
LTSrtpSessionImpl_SetTransportSocket(LTSrtpSessionImpl *session, LTSocket hSocket) {
    session->transportSocket->API->SetSocket(session->transportSocket, hSocket);
}

static void
LTSrtpSessionImpl_Pause(LTSrtpSessionImpl *session) {
    if (session->bClosed) return;
    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_Pause);
    s_mpack->WriteIntU32(message, session->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.p.write.err", NULL);
    }
    lt_destroyobject(message);
}

static void
LTSrtpSessionImpl_Resume(LTSrtpSessionImpl *session) {
    if (session->bClosed) return;
    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Session_Resume);
    s_mpack->WriteIntU32(message, session->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("sess.r.write.err", NULL);
    }
    lt_destroyobject(message);
}

static bool
LTSrtpSessionImpl_SupportsFormat(LTMediaFormat *pFormat) {
    switch (pFormat->nEncoding) {
        case kLTMediaEncoding_H264:
            return (pFormat->params.h264.nClockRate == 90000) && (pFormat->params.h264.nPacketizationMode == 1);

        case kLTMediaEncoding_PCMU:
        case kLTMediaEncoding_PCMA:
            return (pFormat->params.g711.nSampleRate == 8000);
        case kLTMediaEncoding_Opus:
            return true; // Pretend to have Opus support for Alexa (RANGER-1072)
        default:
            return false;
    }
}

define_LTObjectImplPublic(LTSrtpSession, LTSrtpSessionImpl,
    SupportsFormat,
    Init,
    OnSessionEvent,
    NoSessionEvent,
    OpenStream,
    SetTransportSocket,
    Pause,
    Resume
);

/*_____________________________
/ LTRtpStream implementation */
void
LTRtpStreamImpl_Resume(LTRtpStreamImpl *stream) {
    if (stream->bClosed) return;
    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Stream_Resume);
    s_mpack->WriteIntU32(message, stream->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("stm.r.write.err", NULL);
    }
    lt_destroyobject(message);
}

void
LTRtpStreamImpl_Pause(LTRtpStreamImpl *stream) {
    if (stream->bClosed) return;
    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Stream_Pause);
    s_mpack->WriteIntU32(message, stream->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("stm.p.write.err", NULL);
    }
    lt_destroyobject(message);
}

static u32
LTRtpStreamImpl_GetSyncSource(LTRtpStreamImpl *stream) {
    return stream->syncSource;
}

static bool
LTRtpStreamImpl_ConstructObject(LTRtpStreamImpl *stream) {
    stream->id = LTAtomic_FetchAdd(&s_NextId, 1);
    LTList_AddTail(&s_Streams, &stream->node);
    stream->bClosed = false;
    return true;
}

static void
LTRtpStreamImpl_DestructObject(LTRtpStreamImpl *stream) {
    LTList_Remove(&stream->node);
    if (stream->bClosed) return;

    LTBuffer *message = lt_createobject(LTBuffer);
    message->API->Reserve(message, kLTDeviceSdioPort_BlockSize);
    s_mpack->WriteIntU32(message, kLTSrtpProxy_PacketType_Stream_Destroy);
    s_mpack->WriteIntU32(message, stream->id);

    LTBufferAccess access = message->API->StartRead(message, 0);
    if (!s_SdioPort->API->Write(s_SdioPort, access.span.data, kLTDeviceSdioPort_BlockSize)) {
        LTLOG_YELLOWALERT("stm.d.write.err", NULL);
    }
    lt_destroyobject(message);
}

define_LTObjectImplPublic(LTRtpStream, LTRtpStreamImpl,
    GetSyncSource,
    Pause,
    Resume,
);

/*_______________________________________________
 / LTNetSrtpProxyClient library initialization */

static LTSrtpSessionImpl *Session_FindById(u32 id) {
    LTList_ForEach(pNode, &s_Sessions) {
        LTSrtpSessionImpl *session = LTList_GetNodeDataOfType(LTSrtpSessionImpl, pNode);
        if (session->id == id) return session;
    } LTList_EndForEach;
    return NULL;
}

static void Session_HandleEvent(u32 id, LTBuffer *packet) {
    LTSrtpSessionImpl* session = Session_FindById(id);
    if (!session) {
        LTLOG_YELLOWALERT("hev.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    u32 event;
    s_mpack->ReadIntU32(packet, &event);
    s_event->NotifyEvent(session->hEvent, session, event);
}

static void Session_HandleDestroyDone(u32 id) {
    LTSrtpSessionImpl* session = Session_FindById(id);
    if (!session) {
        LTLOG_YELLOWALERT("hdd.sess.nf", "Failed to find session with id: %lu", LT_Pu32(id));
        return;
    }
    session->monitor->API->Enter(session->monitor);
    session->monitor->API->Notify(session->monitor);
    session->monitor->API->Exit(session->monitor);
}

static void
SdioReadCallback(LTDeviceSdioPort *port, u32 recvLen, void *clientData) {
    LT_UNUSED(port); LT_UNUSED(clientData);

    LTBuffer *packet = lt_createobject(LTBuffer);
    packet->API->Reserve(packet, recvLen);
    LTBufferAccess write = packet->API->StartWrite(packet, recvLen);

    s_SdioPort->API->Read(s_SdioPort, write.span.data); // check
    packet->API->FinishWrite(packet, write); // check

    u32 packetType, id;
    s_mpack->ReadIntU32(packet, &packetType);
    s_mpack->ReadIntU32(packet, &id);

    switch (packetType) {
        case kLTSrtpProxy_PacketType_Session_Event:
            Session_HandleEvent(id, packet);
            break;
        case kLTSrtpProxy_PacketType_Destroy_Done:
            Session_HandleDestroyDone(id);
            break;

        default:
            LTLOG_YELLOWALERT("pk.unhandled", NULL);
            break;
    }
    lt_destroyobject(packet);
}

static bool
Thread_Init(void) {
    if (!(s_mpack = lt_openlibrary(LTUtilityMessagePack))) return false;
    if (!(s_SdioPort = lt_createobject(LTDeviceSdioPort))) return false;
    if (!s_SdioPort->API->Bind(s_SdioPort, kLTDeviceSdioPort_Srtp, SdioReadCallback, NULL)) return false;
    return true;
}

static void
Thread_Exit(void) {
    lt_closelibrary(s_mpack);       s_mpack = NULL;
    lt_destroyobject(s_SdioPort);   s_SdioPort = NULL;

    LTList_ForEach(pNode, &s_Streams) {
        LTRtpStreamImpl *stream = LTList_GetNodeDataOfType(LTRtpStreamImpl, pNode);
        lt_destroyobject(stream);
    } LTList_EndForEach;

    LTList_ForEach(pNode, &s_Sessions) {
        LTSrtpSessionImpl *session = LTList_GetNodeDataOfType(LTSrtpSessionImpl, pNode);
        lt_destroyobject(session);
    } LTList_EndForEach;
}

static void
LTNetSrtpProxyClientImpl_LibFini(void) {
    lt_destroyobject(s_thread);
}

static bool
LTNetSrtpProxyClientImpl_LibInit(void) {
    LTList_Init(&s_Sessions);
    LTList_Init(&s_Streams);
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_event = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    if (!(s_thread = lt_createobject(LTOThread))) return false;
    s_thread->API->SetStackSize(s_thread, 1024);
    s_thread->API->Start(s_thread, "srtp.prx.clt", Thread_Init, Thread_Exit);
    return true;
}

/*_______________________________________________________
 / LTNetSrtpProxyClient library root interface binding */

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetSrtpProxyClient, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetSrtpProxyClient) LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTNetSrtpProxyClient, (LTSrtpSessionImpl) (LTRtpStreamImpl));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Oct-24   trajan      created
 */
