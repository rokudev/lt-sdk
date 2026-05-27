/*******************************************************************************
 * source/lt/net/srtp/RtpStreamFormatH264.c - RTP packet packing/parsing for H.264
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "RtpStream.h"
#include "RtpPacket.h"

enum {
    kNalType_FragmentationUnitA        = 28,
    kFragmentationUnitHeader_StartFlag = 0x80,
    kFragmentationUnitHeader_EndFlag   = 0x40,

    kFragmentationUnit_HeaderSize     = 2,
    kFragmentationUnit_MaxPayloadSize = kRtpPacket_MaxPayloadLen - kFragmentationUnit_HeaderSize,
};

// DEFINE_LTLOG_SECTION("h264");

static bool
CanTransmitNalu(LTRtpStreamImpl *stream, u8 nalType) {
    u32 prereqFlags;
    switch (nalType) {
        case kLTMediaNalTypePps:
        case kLTMediaNalTypeSps:
            // Always send PPS and SPS
            prereqFlags = 0;
            break;

        case kLTMediaNalTypeIFrame:
            // Don't send IDR frames until PPS and SPS have been sent
            prereqFlags = (1 << kLTMediaNalTypePps) | (1 << kLTMediaNalTypeSps);
            break;

        case kLTMediaNalTypePFrame:
            // Don't send non-IDR frames until an IDR-frame has been sent
            prereqFlags = (1 << kLTMediaNalTypeIFrame);
            break;

        case kLTMediaNalTypeSei:
            return false; // Drop SEI NAL units from the live stream

        default:
            return false; // Drop unsupported NAL types
    }
    if ((stream->transmittedFrameFlags & prereqFlags) != prereqFlags) return false;
    stream->transmittedFrameFlags |= (1 << nalType);
    return true;
}

// in Webrtc thread
void
LTRtpStreamH264_OnMediaEvent(LTMediaEvent event, void *eventData, void * pClientData) {
    if ((event != kLTMediaEvent_Data) || !eventData || !pClientData) return;
    LTRtpStreamImpl *stream = pClientData;
    LTMediaData *pMediaData = eventData;

    u8 * pPacketData = pMediaData->pData;
    u32 nPacketLen   = pMediaData->nDataLen;

    // Skip start code if it's present
    if (pPacketData[0] == 0 && pPacketData[1] == 0 && pPacketData[2] == 0 && pPacketData[3] == 1) {
        pPacketData += 4;
        nPacketLen  -= 4;
    }

    u8 nalType = pPacketData[0] & 0x1F;
    if (!CanTransmitNalu(stream, nalType)) return;

    u8 dscp      = (nalType == kLTMediaNalTypePFrame) ? kLTNetDscp_AF42 : kLTNetDscp_AF41;

    // Timestamp first I-frame after connection setup
    if (LTTime_IsZero(stream->metrics.send.firstFrameOutTime) && (nalType == kLTMediaNalTypeIFrame)) {
        stream->metrics.send.firstFrameOutTime = LT_GetCore()->GetKernelTime();
    }

    // Count I-frames and P-frames for fps calculation
    if ((nalType == kLTMediaNalTypeIFrame) || (nalType == kLTMediaNalTypePFrame))
        ++stream->metrics.send.framesSent;

    LTTime start = LT_GetCore()->GetKernelTime();
    // Convert from microseconds to 90 KHz clock ticks
    u32 nRtpTimestamp = (pMediaData->nTimestamp * 9) / 100;

    stream->mutex->API->Lock(stream->mutex);
    if (nPacketLen < kRtpPacket_MaxPayloadLen) {
        PayloadSegment payload = { .data = pPacketData, .size = nPacketLen };
        LTRtpStreamImpl_SendPacket(stream, &payload, 1, nRtpTimestamp, pMediaData->bEndOfFrame, dscp);
    } else {
        // This packet is too big to send in one RTP packet. Generate
        // Fragmentation Units (FUs) per RFC 6184 section-5.8 to split
        // it across several RTP packets.
        u8 naluHdr    = pPacketData[0];
        u8 naluUpper  = naluHdr & 0xE0;
        u8 naluType   = naluHdr & 0x1F;

        // Skip the NAL header byte - only the NAL data is included in the FU.
        // The information from the NAL header will be encoded in the FU indicator
        // and header bytes.
        pPacketData += 1;
        nPacketLen  -= 1;

        u8 fuIndicator = naluUpper | kNalType_FragmentationUnitA; // only need FU_A's for non-interleaved mode
        u32 nNumFragments = (nPacketLen + kFragmentationUnit_MaxPayloadSize - 1) / kFragmentationUnit_MaxPayloadSize;
        for (u32 nFragmentIdx = 0; nFragmentIdx < nNumFragments; ++nFragmentIdx) {
            bool bLastFragment = (nFragmentIdx == nNumFragments-1);
            u8 fuHeader = naluType;
            if (nFragmentIdx == 0) fuHeader |= kFragmentationUnitHeader_StartFlag;
            if (bLastFragment)     fuHeader |= kFragmentationUnitHeader_EndFlag;

            u32 nFragPayloadSize = (nPacketLen < kFragmentationUnit_MaxPayloadSize) ? nPacketLen : kFragmentationUnit_MaxPayloadSize;
            u8 fragHeader[kFragmentationUnit_HeaderSize] = { fuIndicator, fuHeader };
            PayloadSegment payload[2] = {
                { .data = fragHeader,  .size = kFragmentationUnit_HeaderSize },
                { .data = pPacketData, .size = nFragPayloadSize },
            };
            LTRtpStreamImpl_SendPacket(stream, payload, 2, nRtpTimestamp, pMediaData->bEndOfFrame && bLastFragment, dscp);

            pPacketData += nFragPayloadSize;
            nPacketLen  -= nFragPayloadSize;
        }
    }
    stream->mutex->API->Unlock(stream->mutex);

    // Based on 20fps if we are taking more than 50ms to process a frame then it can
    // potentially drop the next frame.
    if (LTTime_GetMilliseconds(LTTime_Subtract(LT_GetCore()->GetKernelTime(), start)) > 50)
        ++stream->metrics.send.frameProcessingOverruns;
}

bool
LTRtpStreamH264_SupportsFormat(LTMediaFormat * pFormat) {
    if (!pFormat) return false;
    return (pFormat->nEncoding == kLTMediaEncoding_H264) &&
           (pFormat->params.h264.nClockRate == 90000)    &&
           (pFormat->params.h264.nPacketizationMode == 1);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  01-Jun-22   trajan      created
 */
