/*******************************************************************************
 * source/lt/net/srtp/RtpPacket.c - RTP Packet packing/parsing
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "RtpPacket.h"
#include "SrtpSession.h"

DEFINE_LTLOG_SECTION("rtp.pack");

enum {
    kSrtpVersionValue    = 2,    // Always version 2
    kSrtpPaddingValue    = 0x20,
    kSrtpCsrcCountValue  = 0,    // Don't currently support CSRCs

    kRtpHeader_Byte0_VersionMask    = 0xC0,
    kRtpHeader_Byte0_VersionShift   = 6,
    kRtpHeader_Byte0_PaddingMask    = 0x20,
    kRtpHeader_Byte0_PaddingShift   = 5,
    kRtpHeader_Byte0_ExtensionMask  = 0x10,
    kRtpHeader_Byte0_ExtensionShift = 4,
    kRtpHeader_Byte0_CsrcCountMask  = 0x0F,
    kRtpHeader_Byte0_CsrcCountShift = 0,

    kRtpHeader_Byte1_MarkerMask       = 0x80,
    kRtpHeader_Byte1_MarkerShift      = 7,
    kRtpHeader_Byte1_PayloadTypeMask  = 0x7F,
};
typedef struct {
    u8  nByte0;   // 2-bits: Version, 1-bit: Padding, 1-bit: Extension, 4-bits: CSRC Count
    u8  nByte1;   // 1-bit: Marker, 7-bits: Payload Type
    u16 nSequenceNumber;
    u32 nTimestamp;
    u32 nSyncSource;
} __attribute__((packed)) RtpPacketHeader;

LT_STATIC_ASSERT_SIZE_32_64(RtpPacketHeader, kRtpPacket_HeaderSize, kRtpPacket_HeaderSize);

typedef struct {
    u16 nMagic;
    u16 nLength;
    u8  nHeaderByte;
    u16 nSequenceNumber;
    u8  nPadding;
} __attribute__((packed)) RtpHeaderExt_TransportWideCongestionControl;

LT_STATIC_ASSERT_SIZE_32_64(RtpHeaderExt_TransportWideCongestionControl, kRtpPacket_TWCC_ExtSize, kRtpPacket_TWCC_ExtSize);

enum {
    kMiddleSequenceNumber = 32768,
};

u32
RtpPacket_Pack(RtpPacket *packet, SrtpAuthContext *authCtx, u8 *packetBuffer, u32 packetBufferLen) {
    if (!packet || !packetBuffer) return 0;

    u32 nEncryptedPortionLen     = packet->payloadLen + packet->paddingLen;
    u32 nExtensionsLen           = packet->extMap.nTransportWideCC ? sizeof(RtpHeaderExt_TransportWideCongestionControl) : 0;
    u32 nAuthenticatedPortionLen = sizeof(RtpPacketHeader) + nExtensionsLen + nEncryptedPortionLen;
    u32 nPacketLen               = nAuthenticatedPortionLen + authCtx->profile.nAuthTagLength;

    if (packetBufferLen < nPacketLen) return 0;

    RtpPacketHeader * pHeader     = (RtpPacketHeader *)packetBuffer;
    u8              * pExtensions = packetBuffer + sizeof(RtpPacketHeader);
    u8              * pPayload    = pExtensions  + nExtensionsLen;
    u8              * pPadding    = pPayload     + packet->payloadLen;
    u8              * pAuthTag    = pPadding     + packet->paddingLen;

    pHeader->nByte0 = ((kSrtpVersionValue   << kRtpHeader_Byte0_VersionShift)     & kRtpHeader_Byte0_VersionMask) |
                      ((kSrtpCsrcCountValue << kRtpHeader_Byte0_CsrcCountShift)   & kRtpHeader_Byte0_CsrcCountMask);

    pHeader->nByte1 = packet->payloadType & kRtpHeader_Byte1_PayloadTypeMask;
    if (packet->mark) pHeader->nByte1 |= kRtpHeader_Byte1_MarkerMask;
    pHeader->nSequenceNumber = LT_HTONS(packet->packetIndex & 0xffff);
    pHeader->nTimestamp      = LT_HTONL(packet->timestamp);
    pHeader->nSyncSource     = LT_HTONL(packet->syncSource);

    if (packet->extMap.nTransportWideCC) {
        pHeader->nByte0 |= kRtpHeader_Byte0_ExtensionMask;
        RtpHeaderExt_TransportWideCongestionControl * pTWCCHeader = (RtpHeaderExt_TransportWideCongestionControl *)pExtensions;
        pTWCCHeader->nMagic          = LT_HTONS(0xBEDE);
        pTWCCHeader->nLength         = LT_HTONS(1);
        pTWCCHeader->nHeaderByte     = (packet->extMap.nTransportWideCC << 4) | 0x1;
        pTWCCHeader->nSequenceNumber = LT_HTONS(packet->transportSequenceNumber);
        pTWCCHeader->nPadding        = 0;
    }

    if (packet->payloadLen) {
        lt_memcpy(pPayload, packet->payload, packet->payloadLen);
    }
    if (packet->paddingLen) {
        pHeader->nByte0 |= kSrtpPaddingValue;
        lt_memset(pPadding, 0, packet->paddingLen);
        pPadding[packet->paddingLen - 1] = packet->paddingLen;
    }

    u32 nAuthROC = LT_HTONL(packet->packetIndex >> 16);
    lt_memcpy(pAuthTag, &nAuthROC, sizeof(nAuthROC));
    SrtpCrypto_CryptPayload(kCryptDirection_Encrypt, &authCtx->profile, &authCtx->keys.rtpSend, packet->packetIndex, packet->syncSource, pPayload, nEncryptedPortionLen, pPayload);
    SrtpCrypto_WriteAuthTag(&authCtx->profile, &authCtx->keys.rtpSend, packetBuffer, nAuthenticatedPortionLen + sizeof(nAuthROC), pAuthTag, authCtx->profile.nAuthTagLength);

    return nPacketLen;
}

/* Receiver-side packet index estimation per RFC3711 section 3.3.1 */
static u64
EstimatePacketIndex(u16 nPacketSequenceNumber, u64 nHighestPacketIndexSeen) {
    u64 nRollOverCounter           = nHighestPacketIndexSeen >> 16;
    u16 nHighestSequenceNumberSeen = nHighestPacketIndexSeen & 0xffff;
    if (nHighestSequenceNumberSeen < kMiddleSequenceNumber) {
        if (nPacketSequenceNumber - nHighestSequenceNumberSeen > kMiddleSequenceNumber) {
            nRollOverCounter -= 1;
        }
    } else {
        if (nHighestSequenceNumberSeen - kMiddleSequenceNumber > nPacketSequenceNumber) {
            nRollOverCounter += 1;
        }
    }
    return (nRollOverCounter << 16) + nPacketSequenceNumber;
}

void
RtpPacket_Handle(LTSrtpSessionImpl *session, SrtpAuthContext *authCtx, u8 * pPacketData, u32 nPacketSize) {
    if (!authCtx) {
        return;
    }
    if (nPacketSize < sizeof(RtpPacketHeader) + authCtx->profile.nAuthTagLength) {
        LTLOG("bad.rtp.len", "Invalid RTP packet length: %lu", LT_Pu32(nPacketSize));
        return;
    }

    RtpPacketHeader * pHeader = (RtpPacketHeader *)pPacketData;
    u8 * pAuthTag             = pPacketData + nPacketSize - authCtx->profile.nAuthTagLength;
    RtpHeaderExt_TransportWideCongestionControl * pTWCCHeader = (RtpHeaderExt_TransportWideCongestionControl *)(pPacketData + sizeof(RtpPacketHeader));

    u32 nAuthenticatedPortionLen = nPacketSize - authCtx->profile.nAuthTagLength;
    u32 nTWCCLen                 = (pHeader->nByte0 & kRtpHeader_Byte0_ExtensionMask) ? sizeof(RtpHeaderExt_TransportWideCongestionControl) : 0;
    u8 * pPayload                = pPacketData + sizeof(RtpPacketHeader) + nTWCCLen;
    u32 nPayloadLen              = nAuthenticatedPortionLen - sizeof(RtpPacketHeader) - nTWCCLen;

    u8 nVersion     = (pHeader->nByte0 & kRtpHeader_Byte0_VersionMask) >> kRtpHeader_Byte0_VersionShift;
    bool bIsPadded  = (pHeader->nByte0 & kRtpHeader_Byte0_PaddingMask) >> kRtpHeader_Byte0_PaddingShift;
    u8 nCsrcCount   = (pHeader->nByte0 & kRtpHeader_Byte0_CsrcCountMask) >> kRtpHeader_Byte0_CsrcCountShift;
    u16 nSequenceNumber = LT_NTOHS(pHeader->nSequenceNumber);
    u32 nTimestamp = LT_NTOHL(pHeader->nTimestamp);
    u32 nSyncSource = LT_NTOHL(pHeader->nSyncSource);
    if (nVersion != kSrtpVersionValue) {
        LTLOG("bad.rtp.ver", "Unsupported RTP version: %lu", LT_Pu32(nVersion));
        return;
    } else if (nCsrcCount != 0) {
        LTLOG("bad.rtp.cc", "Unsupported CSRC Count > 0");
        return;
    }

    if (nTWCCLen >= sizeof(RtpHeaderExt_TransportWideCongestionControl)) {
        u16 nTWCCMagic = LT_NTOHS(pTWCCHeader->nMagic);
        if (nTWCCMagic != 0xBEDE) {
            LTLOG("bad.rtp.twcc.magic", "Invalid TWCC magic: %04X", nTWCCMagic);
            return;
        }
        // TODO: Handle TWCC data and generate feedback
    }

    LTRtpStreamImpl *stream = LTSrtpSession_FindRecvStreamBySyncSource(session, nSyncSource);
    if (!stream) {
        LTLOG("unk.ssrc", "Unknown SSRC: %lu - Ignoring packet", LT_Pu32(nSyncSource));
        return;
    }

    if (!stream->bPacketIndexInitialized) {
        stream->packetIndex             = nSequenceNumber;
        stream->bPacketIndexInitialized = true;
    }

    u8 authTag[authCtx->profile.nAuthTagLength];
    lt_memcpy(authTag, pAuthTag, authCtx->profile.nAuthTagLength);
    u32 nEstimatedPacketIndex = EstimatePacketIndex(nSequenceNumber, stream->packetIndex);
    u32 nAuthROC = LT_HTONL(nEstimatedPacketIndex >> 16);
    lt_memcpy(pAuthTag, &nAuthROC, sizeof(nAuthROC));
    if (!SrtpCrypto_VerifyAuthTag(&authCtx->profile, &authCtx->keys.rtpReceive, pPacketData, nAuthenticatedPortionLen + sizeof(nAuthROC), authTag, authCtx->profile.nAuthTagLength)) {
        LTLOG("rtp.auth.fail", "RTP packet authentication failed. Ignoring.");
        return;
    }

    /* Now that the packet has been authenticated, we can update our local packet index
       with the now-confirmed packet index estimate */
    if (nEstimatedPacketIndex > stream->packetIndex) {
        stream->packetIndex = nEstimatedPacketIndex;
    }

    SrtpCrypto_CryptPayload(kCryptDirection_Decrypt, &authCtx->profile, &authCtx->keys.rtpReceive, nEstimatedPacketIndex, nSyncSource, pPayload, nPayloadLen, pPayload);

    if (bIsPadded) {
        u8 nPaddingLen = pPayload[nPayloadLen - 1];
        if (nPayloadLen < nPaddingLen) {
            LTLOG("bad.rtp.pad.len", "Not enough data for padding: remaining=%lu, need=%lu", LT_Pu32(nPayloadLen), LT_Pu32(nPaddingLen));
            return;
        }
        nPayloadLen -= nPaddingLen;
    }

    LTRtpStreamImpl_HandleRtpPayload(stream, pPayload, nPayloadLen, nTimestamp);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jun-22   trajan      created
 */
