/******************************************************************************
 * source/lt/net/srtp/RtpPacket.h - RTP Packet packing/parsing
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKET_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKET_H

#include <lt/net/srtp/LTNetSrtp.h>
#include "SrtpCrypto.h"

typedef struct LTSrtpSessionImpl LTSrtpSessionImpl;

enum {
    kRtpPacket_HeaderSize    = 12,
    kRtpPacket_TWCC_ExtSize  = 8,
    kRtpPacket_MaxPayloadLen = 1200,
    kRtpPacket_MaxAuthTagLen = 10,
    kRtpPacket_MaxLen        = kRtpPacket_HeaderSize + kRtpPacket_TWCC_ExtSize + kRtpPacket_MaxPayloadLen + kRtpPacket_MaxAuthTagLen,
};

typedef enum {
    kRtpPacketType_Media,
    kRtpPacketType_Padding,
} RtpPacketType;

typedef struct {
    LTList_Node       node;
    RtpPacketType     packetType;
    LTRtpExtensionMap extMap;
    u16               transportSequenceNumber;
    u64               packetIndex;
    u32               syncSource;
    bool              mark;
    u32               timestamp;
    u8                payloadType;
    u8                payload[kRtpPacket_MaxPayloadLen];
    u32               payloadLen;
    u8                paddingLen;
    u8                dscp;
} RtpPacket;

void
RtpPacket_Handle(LTSrtpSessionImpl *session, SrtpAuthContext *authCtx, u8 * pPacketData, u32 nPacketSize);

u32
RtpPacket_Pack(RtpPacket *packet, SrtpAuthContext *authCtx, u8 *packetBuffer, u32 packetBufferLen);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKET_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Jun-22   trajan      created
 */
