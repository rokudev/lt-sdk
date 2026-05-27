/*******************************************************************************
 * source/lt/net/srtp/LTNetSrtp.c - Secure Real-time Transport Protocol (SRTP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "SrtpSession.h"
#include "RtpStream.h"
#include "RtpPacket.h"
#include "RtcpPacket.h"
#include "RtpSendPacer.h"
#include "RtpPacketPool.h"

DEFINE_LTLOG_SECTION("srtp");

typedef struct {
    LTMediaEncoding            nMediaEncoding;
    const RtpStreamFormatter *pFormatter;
} MediaStreamInfo;

void LTRtpStreamH264_OnMediaEvent(LTMediaEvent event, void *eventData, void *pClientData);
bool LTRtpStreamH264_SupportsFormat(LTMediaFormat *pFormat);
static const RtpStreamFormatter s_RtpStreamFormatter_H264 = {
    .SupportsFormat  = &LTRtpStreamH264_SupportsFormat,
    .OnMediaEvent    = &LTRtpStreamH264_OnMediaEvent,
};

void LTRtpStreamG711_OnMediaEvent(LTMediaEvent event, void *eventData, void *pClientData);
bool LTRtpStreamG711_SupportsFormat(LTMediaFormat *pFormat);
static const RtpStreamFormatter s_RtpStreamFormatter_G711 = {
    .SupportsFormat  = &LTRtpStreamG711_SupportsFormat,
    .OnMediaEvent    = &LTRtpStreamG711_OnMediaEvent,
};

void LTRtpStreamOpus_OnMediaEvent(LTMediaEvent event, void *eventData, void * pClientData);
bool LTRtpStreamOpus_SupportsFormat(LTMediaFormat * pFormat);
static const RtpStreamFormatter s_RtpStreamFormatter_Opus = {
    .SupportsFormat  = &LTRtpStreamOpus_SupportsFormat,
    .OnMediaEvent    = &LTRtpStreamOpus_OnMediaEvent,
};

static const MediaStreamInfo s_MediaStreamInfo[] = {
    {
        .nMediaEncoding = kLTMediaEncoding_H264,
        .pFormatter     = &s_RtpStreamFormatter_H264,
    },
    {
        .nMediaEncoding = kLTMediaEncoding_PCMU,
        .pFormatter     = &s_RtpStreamFormatter_G711,
    },
    {
        .nMediaEncoding = kLTMediaEncoding_PCMA,
        .pFormatter     = &s_RtpStreamFormatter_G711,
    },
    {
        .nMediaEncoding = kLTMediaEncoding_Opus,
        .pFormatter     = &s_RtpStreamFormatter_Opus,
    },
};
enum { kNumSupportedMediaFormats = sizeof(s_MediaStreamInfo) / sizeof(s_MediaStreamInfo[0]) };

static LTCore               *s_pCore        = NULL;
static ILTEvent             *s_pEvent       = NULL;
static LTDeviceMedia        *s_pDeviceMedia = NULL;

static const LTArgsDescriptor s_SessionEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_u32}};

static void LTNetSrtpImpl_LibFini(void);
static void DispatchSessionEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData);

void
LTSrtpSession_OnTransportConnected(LTSrtpSessionImpl *session) {
    session->nSessionState = kSrtpSessionState_Established;

    LTLOG_DEBUG("sess.estab", "SRTP session established");
    s_pEvent->NotifyEvent(session->hEvent, session, kLTSrtpSession_Event_Established);

    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        LTRtpStreamImpl_Resume(stream);
    }
    for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
        LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
        LTRtpStreamImpl_Resume(stream);
    }
}

void
LTSrtpSession_OnTransportDisconnected(LTSrtpSessionImpl *session) {
    LTLOG_DEBUG("sess.term", "SRTP session terminated");
    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        LTRtpStreamImpl_Pause(stream);
    }
    for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
        LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
        LTRtpStreamImpl_Pause(stream);
    }
    session->nSessionState = kSrtpSessionState_Terminated;
    s_pEvent->NotifyEvent(session->hEvent, session, kLTSrtpSession_Event_Terminated);
}

void
LTSrtpSession_OnTransportSentPacket(LTSrtpSessionImpl *session, RtpPacket *packet) {
    if (packet->extMap.nTransportWideCC) {
        RtpCongestionControl_OnPacketSent(session->pCongestionControl, packet->transportSequenceNumber, packet->payloadLen + packet->paddingLen);
    }
    RtpPacketPool_ReleasePacket(packet);
}

// in Webrtc thread
bool
LTSrtpSession_Send(LTSrtpSessionImpl *session, RtpPacket *packet) {
    if (!session || !packet || (session->nSessionState != kSrtpSessionState_Established)) return false;

    if (packet->extMap.nTransportWideCC) {
        // protected by pPacer->sendQueueMutex to avoid race on pPacer->pSendQueue in
        // RtpSendPacer_AddPacket in Webrtc thread and
        // OnSendBurst in RtpSendP thread
        RtpSendPacer_AddPacket(session->pPacer, packet);
        return true;
    } else {
        return SrtpTransport_Send(session->transport, packet);
    }
}

static LTRtpStreamImpl *
FindPaddableStream(LTSrtpSessionImpl *session) {
    if (!session) return NULL;
    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        if (stream->extMap.nTransportWideCC && (stream->nLastTimestamp != 0)) {
            return stream;
        }
    }
    return NULL;
}

// in RtpSendP thread
RtpPacket *
LTSrtpSession_GeneratePaddingPacket(LTSrtpSessionImpl *session, u8 paddingLen, u8 dscp) {
    LTRtpStreamImpl *stream = FindPaddableStream(session);
    if (!session || !stream || (session->nSessionState != kSrtpSessionState_Established)) {
        return NULL;
    }

    // Use TryLock here as a check for collision with the media packet generator.
    // If this fails then media packets are being generated and so padding packet
    // generation should cease.
    if (!stream->mutex->API->TryLock(stream->mutex)) return NULL;

    RtpPacket *packet = LTRtpStreamImpl_GeneratePaddingPacket(stream, paddingLen, dscp);

    stream->mutex->API->Unlock(stream->mutex);

    return packet;
}

LTRtpStreamImpl *
LTSrtpSession_FindRecvStreamBySyncSource(LTSrtpSessionImpl *session, u32 nSyncSource) {
    for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
        LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
        if (stream->nSyncSource == nSyncSource) return stream;
    }
    return NULL;
}

LTRtpStreamImpl *
LTSrtpSession_FindSendStreamBySyncSource(LTSrtpSessionImpl *session, u32 nSyncSource) {
    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        if (stream->nSyncSource == nSyncSource) return stream;
    }
    return NULL;
}

static void
OnTargetBitrateChanged(RtpCongestionControlContext *pControl, u32 nTargetBitrate, void *pClientData) {
    LT_UNUSED(pControl);
    LTSrtpSessionImpl *session = pClientData;

    // TODO - split total bandwidth between sources
    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        LTRtpStreamImpl_SetTargetBitrate(stream, nTargetBitrate);
    }
}

static LTRtpStream *
LTSrtpSessionImpl_OpenStream(LTSrtpSessionImpl *session, LTRtpStreamDirection direction, u32 nSyncSource, LTRtpExtensionMap *pExtMap, LTRtpPayloadFormat *pPayloadFormat) {
    LTHandle hMediaEndpoint = (direction == kLTRtpStreamDirection_Send) ?
        s_pDeviceMedia->OpenSource(&pPayloadFormat->mediaFormat) :
        s_pDeviceMedia->OpenSink(&pPayloadFormat->mediaFormat);
    if (!hMediaEndpoint) {
        LTLOG_YELLOWALERT("open.media.ep.err", "Failed to open media endpoint %lu", LT_Pu32(pPayloadFormat->mediaFormat.nEncoding));
        return NULL;
    }

    LTMediaEncoding nEndpointMediaEncoding;
    ILTMediaSource *pSource = lt_gethandleinterface(ILTMediaSource, hMediaEndpoint);
    ILTMediaSink   *pSink = lt_gethandleinterface(ILTMediaSink, hMediaEndpoint);
    if (pSource)
        nEndpointMediaEncoding = pSource->GetMediaEncoding(hMediaEndpoint);
    else if (pSink)
        nEndpointMediaEncoding = pSink->GetMediaEncoding(hMediaEndpoint);
    else
        return NULL;

    for (u32 i = 0; i < kNumSupportedMediaFormats; ++i) {
        const MediaStreamInfo *pStreamInfo = &s_MediaStreamInfo[i];
        if (pStreamInfo->nMediaEncoding == nEndpointMediaEncoding) {
            LTRtpStreamImpl *stream;
            if (pSource) {
                stream = LTRtpStreamImpl_CreateSender(session, hMediaEndpoint, pStreamInfo->pFormatter, nSyncSource, pExtMap, pPayloadFormat);
                if (stream) {
                    LTRtpStreamImpl_SetTargetBitrate(stream, RtpCongestionControl_GetTargetBitrate(session->pCongestionControl));
                    session->pSenders->API->Append(session->pSenders, stream);
                }
            } else {
                stream = LTRtpStreamImpl_CreateReceiver(session, hMediaEndpoint, pStreamInfo->pFormatter, nSyncSource, pExtMap, pPayloadFormat);
                if (stream) session->pReceivers->API->Append(session->pReceivers, stream);
            }
            if (session->nSessionState == kSrtpSessionState_Established) LTRtpStreamImpl_Resume(stream);
            return (LTRtpStream *)stream;
        }
    }
    return NULL;
}

static void
LTSrtpSessionImpl_Init(LTSrtpSessionImpl *session, const char *sessionId, LTSocket hTransportSocket, LTSocket hDtlsSocket) {
    session->sessionId          = lt_strdup(sessionId);
    session->transport          = SrtpTransport_Create(session, hTransportSocket, hDtlsSocket);
    session->pCongestionControl = RtpCongestionControl_Create(sessionId);
    session->pPacer             = RtpSendPacer_Create(sessionId, session, session->transport, session->pCongestionControl);
    RtpCongestionControl_OnTargetBitrateChanged(session->pCongestionControl, OnTargetBitrateChanged, NULL, session);
}

static void
LogSessionSummary(LTSrtpSessionImpl *session) {
    LTLOG_SERVER("sum.rtcp", "sid=%s pli=%lu fir=%lu sli=%lu rpsi=%lu nack=%lu",
        session->sessionId,
        LT_Pu32(session->rtcpMetrics.nPliCount),
        LT_Pu32(session->rtcpMetrics.nFirCount),
        LT_Pu32(session->rtcpMetrics.nSliCount),
        LT_Pu32(session->rtcpMetrics.nRpsiCount),
        LT_Pu32(session->rtcpMetrics.nGenericNackCount));
}

static bool
LTSrtpSessionImpl_ConstructObject(LTSrtpSessionImpl *session) {
    session->nSessionState = kSrtpSessionState_New;
    session->hEvent        = s_pCore->CreateEvent(&s_SessionEventArgs, DispatchSessionEventCB, NULL, NULL, NULL);
    session->pSenders      = lt_createobject_typed(LTArray, List);
    session->pReceivers    = lt_createobject_typed(LTArray, List);
    return true;
}

static void
LTSrtpSessionImpl_DestructObject(LTSrtpSessionImpl *session) {
    LogSessionSummary(session);

    if (session->pPacer) RtpSendPacer_Stop(session->pPacer);
    if (session->pSenders) {
        for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
            LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
            lt_destroyobject(stream);
        }
        lt_destroyobject(session->pSenders);
        session->pSenders = NULL;
    }

    if (session->pReceivers) {
        for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
            LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
            lt_destroyobject(stream);
        }
        lt_destroyobject(session->pReceivers);
        session->pReceivers = NULL;
    }

    if (session->transport) {
        SrtpTransport_Destroy(session->transport);
        session->transport = NULL;
    }

    if (session->pPacer) {
        RtpSendPacer_Destroy(session->pPacer);
        session->pPacer = NULL;
    }

    if (session->pCongestionControl) {
        RtpCongestionControl_Destroy(session->pCongestionControl);
        session->pCongestionControl = NULL;
    }

    if (session->hEvent) {
        lt_destroyhandle(session->hEvent);
        session->hEvent = 0;
    }
    if (session->sessionId) {
        lt_free(session->sessionId);
        session->sessionId = NULL;
    }
}

static void
DispatchSessionEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTSrtpSession_SessionEventProc *pCallback = (LTSrtpSession_SessionEventProc *)pEventProc;
    LTSrtpSession *session    = LTArgs_pointerAt(0, pEventArgs);
    LTSrtpSession_Event event = LTArgs_u32At(1, pEventArgs);
    pCallback(session, event, pEventProcClientData);
}

static void
LTSrtpSessionImpl_OnSessionEvent(LTSrtpSessionImpl *session, LTSrtpSession_SessionEventProc pCallback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    s_pEvent->RegisterForEvent(session->hEvent, (void *)pCallback, pClientDataReleaseProc, pClientData, false);
}

static void
LTSrtpSessionImpl_NoSessionEvent(LTSrtpSessionImpl *session, LTSrtpSession_SessionEventProc pCallback) {
    s_pEvent->UnregisterFromEvent(session->hEvent, (void *)pCallback);
}

static void
LTSrtpSessionImpl_SetTransportSocket(LTSrtpSessionImpl *session, LTSocket hSocket) {
    SrtpTransport_SetTransportSocket(session->transport, hSocket);
}

static void
LTSrtpSessionImpl_Pause(LTSrtpSessionImpl *session) {
    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        LTRtpStreamImpl_Pause(stream);
    }
    for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
        LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
        LTRtpStreamImpl_Pause(stream);
    }
    RtpSendPacer_Destroy(session->pPacer);
    session->pPacer = NULL;

    RtpCongestionControl_Destroy(session->pCongestionControl);
    session->pCongestionControl = NULL;
}

static void
LTSrtpSessionImpl_Resume(LTSrtpSessionImpl *session) {
    session->pCongestionControl = RtpCongestionControl_Create(session->sessionId);
    session->pPacer             = RtpSendPacer_Create(session->sessionId, session, session->transport, session->pCongestionControl);
    RtpCongestionControl_OnTargetBitrateChanged(session->pCongestionControl, OnTargetBitrateChanged, NULL, session);

    for (u32 i = 0; i < session->pSenders->API->GetCount(session->pSenders); ++i) {
        LTRtpStreamImpl *stream = session->pSenders->API->Get(session->pSenders, i, NULL);
        LTRtpStreamImpl_SetTargetBitrate(stream, RtpCongestionControl_GetTargetBitrate(session->pCongestionControl));
        LTRtpStreamImpl_Resume(stream);
    }
    for (u32 i = 0; i < session->pReceivers->API->GetCount(session->pReceivers); ++i) {
        LTRtpStreamImpl *stream = session->pReceivers->API->Get(session->pReceivers, i, NULL);
        LTRtpStreamImpl_Resume(stream);
    }
}

static bool
LTSrtpSessionImpl_SupportsFormat(LTMediaFormat *pFormat) {
    if (!s_pDeviceMedia->SupportsFormat(pFormat)) return false;
    bool bFormatSupported = false;
    for (u32 i = 0; i < kNumSupportedMediaFormats; ++i) {
        const MediaStreamInfo *streamInfo = &s_MediaStreamInfo[i];
        if ((streamInfo->nMediaEncoding == pFormat->nEncoding) &&
            streamInfo->pFormatter->SupportsFormat(pFormat)) {
            bFormatSupported = true;
            break;
        }
    }
    return bFormatSupported;
}

/*_______________________________________
  LTSrtpSession LTObjectApi definition */
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

/*____________________________________
 / LTNetSrtp library initialization */

static bool
LTNetSrtpImpl_LibInit(void) {
    s_pCore             = LT_GetCore();
    s_pEvent            = lt_getlibraryinterface(ILTEvent, s_pCore);
    s_pDeviceMedia      = lt_openlibrary(LTDeviceMedia);
    if (!s_pCore || !s_pEvent || !RtpPacketPool_LibInit() || !SrtpCrypto_LibInit() || !RtpSendPacer_LibInit()) {
        LTNetSrtpImpl_LibFini();
        return false;
    }
    return true;
}

static void
LTNetSrtpImpl_LibFini(void) {
    lt_closelibrary(s_pDeviceMedia);
    SrtpCrypto_LibFini();
    RtpSendPacer_LibFini();
    RtpPacketPool_LibFini();
}

/*____________________________________________
 / LTNetSrtp library root interface binding */

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetSrtp, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetSrtp) LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTNetSrtp, (LTSrtpSessionImpl) (LTRtpStreamImpl));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
