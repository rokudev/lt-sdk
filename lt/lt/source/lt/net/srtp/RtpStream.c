/*******************************************************************************
 * source/lt/net/srtp/RtpStream.c - RTP Media Stream
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "RtpStream.h"
#include "SrtpSession.h"
#include "RtpPacketPool.h"

DEFINE_LTLOG_SECTION("rtp.stream");

enum {
    kMaxRefreshIntervalMs = 1000, // Only allow a refresh every second
    kDefaultGopLength     = 40,   // I-frame interval every 2 second based on 20 fps
};

/*__________________________________
/ LTRtpStream implementation */
void
LTRtpStreamImpl_Resume(LTRtpStreamImpl *stream) {
    if (stream->active) return;
    ILTMediaSource * pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
    if (pMediaSource) {
        pMediaSource->Start(stream->hMediaEndpoint);
        pMediaSource->OnMediaEvent(stream->hMediaEndpoint, stream->pFormatter->OnMediaEvent, stream);
        pMediaSource->SetGopLength(stream->hMediaEndpoint, kDefaultGopLength);
    }
    else {
        ILTMediaSink * pMediaSink = lt_gethandleinterface(ILTMediaSink, stream->hMediaEndpoint);
        if (pMediaSink)
            pMediaSink->Start(stream->hMediaEndpoint);
    }
    stream->active = true;
    LTTime now = LT_GetCore()->GetKernelTime();
    LTTime_AddTo(stream->metrics.pausedDuration, LTTime_Subtract(now, stream->lastActiveChangeTime));
    stream->lastActiveChangeTime = now;
}

void
LTRtpStreamImpl_Pause(LTRtpStreamImpl *stream) {
    if (!stream->active) return;
    ILTMediaSource * pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
    if (pMediaSource) {
        pMediaSource->NoMediaEvent(stream->hMediaEndpoint, stream->pFormatter->OnMediaEvent);
        pMediaSource->Stop(stream->hMediaEndpoint);
    }
    else {
        ILTMediaSink * pMediaSink = lt_gethandleinterface(ILTMediaSink, stream->hMediaEndpoint);
        if (pMediaSink)
            pMediaSink->Stop(stream->hMediaEndpoint, true);
    }
    stream->active = false;
    LTTime now = LT_GetCore()->GetKernelTime();
    LTTime_AddTo(stream->metrics.activeDuration, LTTime_Subtract(now, stream->lastActiveChangeTime));
    stream->lastActiveChangeTime = now;
}

void
LTRtpStreamImpl_HandleRtpPayload(LTRtpStreamImpl *stream, u8 * pPayload, u32 nPayloadLen, u32 nTimestamp) {
    if (!stream || !stream->hMediaEndpoint) return;

    ILTMediaSink * pMediaSink = lt_gethandleinterface(ILTMediaSink, stream->hMediaEndpoint);
    if (!pMediaSink->GetAvailableFrameBuffers(stream->hMediaEndpoint)) {
        // Dropping audio payload
        stream->metrics.recv.mediaSinkBackups++;
        return;
    }

    LTMediaData * pMediaData = lt_malloc(sizeof(LTMediaData) + nPayloadLen);
    u8 * pData = (u8 *)(pMediaData + 1);
    *pMediaData = (LTMediaData) {
        .pData       = pData,
        .nDataLen    = nPayloadLen,
        .nTimestamp  = nTimestamp,
        .bEndOfFrame = false
    };
    lt_memcpy(pData, pPayload, nPayloadLen);

    pMediaSink->HandleMediaData(stream->hMediaEndpoint, pMediaData);
}

/* This logic needs further exploration. Possible improvements:
 *  - Purge pacer queue of packets for this SSRC to avoid bandwidth hit of sending a
 *    bunch of useless P-frames when a new I-frame will be coming soon. A naive first
 *    attempt at this cleared the entire queue however this causes a jump in RTP sequence
 *    number which generates further NACKs which can result in a positive feedback loop. Could
 *    possibly roll back the sequence number to eliminate this jump.
 *  - Only refresh if the NACK'd packet was inside of the last I-frame since it could result in
 *    extended encoder/decoder desynchronization. We don't necessarily want to refresh if a P-frame
 *    was lost. This means keeping track of last I-frame sequence numbers.
 *  - Explore the possibility of _actually_ buffering and re-transmitting packets. What would
 *    the memory impact actually be?
 */
void
LTRtpStreamImpl_Refresh(LTRtpStreamImpl *stream) {
    if (!stream || !stream->hMediaEndpoint) return;
    LTTime now              = LT_GetCore()->GetKernelTime();
    LTTime lastRefreshDelta = LTTime_Subtract(now, stream->lastRefreshTime);
    if (LTTime_IsGreaterThan(lastRefreshDelta, LTTime_Milliseconds(kMaxRefreshIntervalMs))) {
        LTLOG_SERVER("refresh", NULL);
        stream->lastRefreshTime = now;
        stream->transmittedFrameFlags = 0;
        ILTMediaSource *pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
        pMediaSource->Refresh(stream->hMediaEndpoint);
    }
}

static u32
LTRtpStreamImpl_GetSyncSource(LTRtpStreamImpl *stream) {
    return stream->nSyncSource;
}

void
LTRtpStreamImpl_SetTargetBitrate(LTRtpStreamImpl *stream, u32 nTargetBitrate) {
    ILTMediaSource *pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
    if (pMediaSource) pMediaSource->SetTargetBitrate(stream->hMediaEndpoint, nTargetBitrate);
}

void
LTRtpStreamImpl_SendPacket(LTRtpStreamImpl *stream, PayloadSegment *payloadSegments, u32 numPayloadSegments, u32 timestamp, bool mark, u8 dscp) {
    RtpPacket *packet = RtpPacketPool_GetPacket();
    if (!packet) {
        LTLOG_YELLOWALERT("pkt.oom", NULL);
        return;
    }

    packet->packetType              = kRtpPacketType_Media;
    packet->extMap                  = stream->extMap;
    packet->syncSource              = stream->nSyncSource;
    packet->payloadType             = stream->nPayloadType;
    packet->timestamp               = stream->nTimestampOffset + timestamp;
    packet->packetIndex             = stream->packetIndex++;
    packet->transportSequenceNumber = 0;
    packet->mark                    = mark;
    packet->payloadLen              = 0;
    packet->paddingLen              = 0;
    packet->dscp                    = dscp;
    for (u32 i = 0; i < numPayloadSegments; ++i) {
        PayloadSegment *segment = &payloadSegments[i];
        lt_memcpy(packet->payload + packet->payloadLen, segment->data, segment->size);
        packet->payloadLen += segment->size;
    }

    if (LTSrtpSession_Send(stream->session, packet)) {
        ++stream->metrics.send.packetsSent;
    } else {
        RtpPacketPool_ReleasePacket(packet);
        ++stream->metrics.send.sendErrors;
        // Log every 64 sending errors
        if ((stream->metrics.send.sendErrors & 0x3F) == 0x10) {
            LTLOG("send.fail", "(0x%lX) len %lu", LT_Pu32(stream->metrics.send.sendErrors), LT_Pu32(packet->payloadLen));
        }
    }
    stream->nLastTimestamp = packet->timestamp;
}

RtpPacket *
LTRtpStreamImpl_GeneratePaddingPacket(LTRtpStreamImpl *stream, u8 paddingLen, u8 dscp) {
    RtpPacket *packet = RtpPacketPool_GetPacket();
    if (!packet) {
        LTLOG_YELLOWALERT("pad.oom", NULL);
        return NULL;
    }

    packet->packetType              = kRtpPacketType_Padding;
    packet->extMap                  = stream->extMap;
    packet->syncSource              = stream->nSyncSource;
    packet->payloadType             = stream->nPayloadType;
    packet->timestamp               = stream->nLastTimestamp;
    packet->packetIndex             = stream->packetIndex++;
    packet->transportSequenceNumber = 0;
    packet->mark                    = false;
    packet->payloadLen              = 0;
    packet->paddingLen              = paddingLen;
    packet->dscp                    = dscp;

    return packet;
}

static bool
LTRtpStreamImpl_ConstructObject(LTRtpStreamImpl *stream) {
    LT_UNUSED(stream);
    return true;
}

static void
LogSessionSummary(LTRtpStreamImpl *stream) {
    ILTMediaSource *pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
    ILTMediaSink   *pMediaSink   = lt_gethandleinterface(ILTMediaSink,   stream->hMediaEndpoint);
    if (pMediaSource) {
        LTMediaEncoding nMediaEncoding = pMediaSource->GetMediaEncoding(stream->hMediaEndpoint);
        float fps = 1000.0f * stream->metrics.send.framesSent / LTTime_GetMilliseconds(stream->metrics.activeDuration);
        LTLOG_SERVER("sum", "sid=%s dir=send enc=%s active_dur(ms)=%lld paused_dur(ms)=%lld frms_sent=%lu fps=%0.1f frm_proc_ovr=%lu pkts_sent=%lu",
                    stream->session->sessionId,
                    LTMediaEncodingToString(nMediaEncoding),
                    LT_Ps64(LTTime_GetMilliseconds(stream->metrics.activeDuration)),
                    LT_Ps64(LTTime_GetMilliseconds(stream->metrics.pausedDuration)),
                    LT_Pu32(stream->metrics.send.framesSent),
                    fps,
                    LT_Pu32(stream->metrics.send.frameProcessingOverruns),
                    LT_Pu32(stream->metrics.send.packetsSent));
    } else if (pMediaSink) {
        LTMediaEncoding nMediaEncoding = pMediaSink->GetMediaEncoding(stream->hMediaEndpoint);
        LTLOG_SERVER("sum", "sid=%s dir=recv enc=%s active_dur(ms)=%lld paused_dur(ms)=%lld pkts_recv=%lu sink_bkup=%lu",
                    stream->session->sessionId,
                    LTMediaEncodingToString(nMediaEncoding),
                    LT_Ps64(LTTime_GetMilliseconds(stream->metrics.activeDuration)),
                    LT_Ps64(LTTime_GetMilliseconds(stream->metrics.pausedDuration)),
                    LT_Pu32(stream->metrics.recv.packetsReceived),
                    LT_Pu32(stream->metrics.recv.mediaSinkBackups));
    }
}

static void
LTRtpStreamImpl_DestructObject(LTRtpStreamImpl *stream) {
    LogSessionSummary(stream);
    ILTMediaSource *pMediaSource = lt_gethandleinterface(ILTMediaSource, stream->hMediaEndpoint);
    if (pMediaSource) {
        pMediaSource->NoMediaEvent(stream->hMediaEndpoint, stream->pFormatter->OnMediaEvent);
    }
    ILTMediaSink *pMediaSink = lt_gethandleinterface(ILTMediaSink, stream->hMediaEndpoint);
    if (pMediaSink) {
        pMediaSink->Stop(stream->hMediaEndpoint, true);
    }
    lt_destroyhandle(stream->hMediaEndpoint);
    lt_destroyobject(stream->mutex);
}

static LTRtpStreamImpl *
LTRtpStreamImpl_Create(LTSrtpSessionImpl *session, LTHandle hMediaEndpoint, const RtpStreamFormatter * pFormatter, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat) {
    if (!pFormatter || !pFormatter->SupportsFormat(&pPayloadFormat->mediaFormat)) return NULL;
    LTRtpStreamImpl *stream = (LTRtpStreamImpl *)lt_createobject(LTRtpStream);
    stream->mutex                = lt_createobject(LTMutex);
    stream->active               = false;
    stream->lastActiveChangeTime = LT_GetCore()->GetKernelTime();
    stream->pFormatter           = pFormatter;
    stream->session              = session;
    stream->hMediaEndpoint       = hMediaEndpoint;
    stream->extMap               = *pExtMap;
    stream->nPayloadType         = pPayloadFormat->nPayloadType;
    return stream;
}

LTRtpStreamImpl *
LTRtpStreamImpl_CreateSender(LTSrtpSessionImpl *session, LTMediaSource hMediaSource, const RtpStreamFormatter * pFormatter, u32 nSyncSource, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat) {
    LTRtpStreamImpl *stream = LTRtpStreamImpl_Create(session, hMediaSource, pFormatter, pExtMap, pPayloadFormat);
    if (!stream) return NULL;

    /* Random sequence number and timestamp per RFC3550, section 5.1 */
    LTUtilityByteOps * pByteOps = lt_openlibrary(LTUtilityByteOps);
    u16 sequenceNumber;
    pByteOps->GenRandomBytes((u8*)&sequenceNumber, sizeof(u16));
    pByteOps->GenRandomBytes((u8*)&stream->nTimestampOffset, sizeof(u32));
    stream->nSyncSource = nSyncSource;
    stream->packetIndex = sequenceNumber;
    lt_closelibrary(pByteOps);

    ILTMediaSource * pMediaSource = lt_gethandleinterface(ILTMediaSource, hMediaSource);
    pMediaSource->OnMediaEvent(hMediaSource, stream->pFormatter->OnMediaEvent, stream);

    return stream;
}

LTRtpStreamImpl *
LTRtpStreamImpl_CreateReceiver(LTSrtpSessionImpl *session, LTMediaSink hMediaSink, const RtpStreamFormatter * pFormatter, u32 nSyncSource, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat) {
    LTRtpStreamImpl * stream = LTRtpStreamImpl_Create(session, hMediaSink, pFormatter, pExtMap, pPayloadFormat);
    if (!stream) return NULL;
    stream->bPacketIndexInitialized = false;
    stream->nSyncSource = nSyncSource;
    return stream;
}

/*_______________________________________
  LTSrtpSession LTObjectApi definition */
define_LTObjectImplPublic(LTRtpStream, LTRtpStreamImpl,
    GetSyncSource,
    Pause,
    Resume,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  01-Jun-22   trajan      created
 */
