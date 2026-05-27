/******************************************************************************
 * source/lt/net/srtp/RtpStream.h - RTP Media Stream
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPSTREAM_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTPSTREAM_H

#include <lt/net/srtp/LTNetSrtp.h>
#include "RtpPacket.h"

typedef struct LTSrtpSessionImpl LTSrtpSessionImpl;

typedef struct {
    u8 *data;
    u32 size;
} PayloadSegment;

typedef struct {
    bool         (* SupportsFormat)(LTMediaFormat * pFormat);
    void         (* OnMediaEvent)(LTMediaEvent event, void *eventData, void * pClientData);
} RtpStreamFormatter;

typedef_LTObjectImpl(LTRtpStream, LTRtpStreamImpl) {
    LTMutex                        *mutex;
    const RtpStreamFormatter       *pFormatter;
    LTSrtpSessionImpl              *session;
    bool                            active;
    LTHandle                        hMediaEndpoint;
    LTRtpExtensionMap               extMap;
    u32                             nSyncSource;
    u32                             nPayloadType;
    bool                            bPacketIndexInitialized;
    u64                             packetIndex;          /* lower 16-bits: sequence number, upper 48-bits: rollover counter */
    u32                             nLastTimestamp;
    u32                             nTimestampOffset;
    u32                             transmittedFrameFlags;
    LTTime                          lastRefreshTime;
    LTTime                          lastActiveChangeTime;
    struct {
        LTTime                      activeDuration;
        LTTime                      pausedDuration;
        union {
            struct {
                u32                 framesSent;                /* Frames sent to peer */
                u32                 frameProcessingOverruns;   /* Frames whose processing was longer than intra-frame period */
                u32                 packetsSent;               /* Packets sent to peer */
                LTTime              firstFrameOutTime;         /* Time when first I-Frame is sent */
                u32                 sendErrors;
            } send;
            struct {
                u32                 packetsReceived;
                u32                 mediaSinkBackups;
            } recv;
        };
    } metrics;
} LTOBJECT_API;

LTRtpStreamImpl *
LTRtpStreamImpl_CreateSender(LTSrtpSessionImpl *session, LTMediaSource hMediaSource, const RtpStreamFormatter * pFormatter, u32 nSyncSource, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat);

LTRtpStreamImpl *
LTRtpStreamImpl_CreateReceiver(LTSrtpSessionImpl *session, LTMediaSink hMediaSink, const RtpStreamFormatter * pFormatter, u32 nSyncSource, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat);

void
LTRtpStreamImpl_Pause(LTRtpStreamImpl *stream);

void
LTRtpStreamImpl_Resume(LTRtpStreamImpl *stream);

void
LTRtpStreamImpl_HandleRtpPayload(LTRtpStreamImpl *stream, u8 *payload, u32 nPayloadLen, u32 nTimestamp);

void
LTRtpStreamImpl_Refresh(LTRtpStreamImpl *stream);

void
LTRtpStreamImpl_SetTargetBitrate(LTRtpStreamImpl *stream, u32 nTargetBitrate);

void
LTRtpStreamImpl_SendPacket(LTRtpStreamImpl *stream, PayloadSegment *payloadSegments, u32 numPayloadSegments, u32 timestamp, bool mark, u8 dscp);

RtpPacket *
LTRtpStreamImpl_GeneratePaddingPacket(LTRtpStreamImpl *stream, u8 paddingLen, u8 dscp);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPSTREAM_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Jun-22   trajan      created
 */
