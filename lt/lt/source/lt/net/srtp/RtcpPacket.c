/*******************************************************************************
 * source/lt/net/srtp/RtcpPacket.c - Real-time Transport Control Protocol (RTCP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include "RtcpPacket.h"
#include "RtpCongestionControl.h"

DEFINE_LTLOG_SECTION("rtcp.pack");

enum {
    kRtcpVersionValue      = 2,

    kRtcpHeader_Byte0_VersionMask      = 0xC0,
    kRtcpHeader_Byte0_VersionShift     = 6,
    kRtcpHeader_Byte0_PaddingMask      = 0x20,
    kRtcpHeader_Byte0_PaddingShift     = 5,
    kRtcpHeader_Byte0_ReportCountMask  = 0x1F,
    kRtcpHeader_Byte0_ReportCountShift = 0,

    kRtcpFooter_Byte0_EncryptedMask    = 0x80,
    kRtcpFooter_Byte0_EncryptedShift   = 7,
    kRtcpFooter_Word0_IndexMask        = 0x7FFFFFFF,
};
typedef struct {
    u8  nByte0;   // 2-bits: Version, 1-bit: Padding, 5-bits: Report Count
    u8  nPayloadType;
    u16 nLength;
    u32 nSyncSource;
} __attribute__((packed)) RtcpPacketHeader;
/* ISSUE: NOT ALLOWED TO USE __attribute__ or any other compiler specific keyword... */

typedef struct {
    u32 nSyncSource;
    u32 nWord1; // 8-bits: fraction lost, 24-bits: cumulative lost
    u32 nHighestSeqReceived;
    u32 nInterarrivalJitter;
    u32 nLastSR;
    u32 nDelaySinceLastSR;
} __attribute__((packed)) ReceptionReportBlock;

typedef enum {
    kPSFB_MsgType_PictureLossIndication               = 1,
    kPSFB_MsgType_SliceLossIndication                 = 2,
    kPSFB_MsgType_ReferencePictureSelectionIndication = 3,
    kPSFB_MsgType_FullIntraRequest                    = 4,
    kPSFB_MsgType_ApplicationLayerFB                  = 15,
} PSFB_MsgType;

typedef enum {
    kTLFB_MsgType_GenericNack           = 1,
    kTLFB_MsgType_TransportWideFeedback = 15,
} TLFB_MsgType;

typedef enum {
    /* Standard RTCP packet types per RFC 3550 */
    kPayloadType_RTCP_SenderReport            = 200,
    kPayloadType_RTCP_ReceiverReport          = 201,
    kPayloadType_RTCP_SourceDescription       = 202,
    kPayloadType_RTCP_Goodbye                 = 203,
    kPayloadType_RTCP_Application             = 204,

    /* Extended RTP/AVPF RTCP packet types per RFC 4585 */
    kPayloadType_RTCP_TransportLayerFeedback  = 205,
    kPayloadType_RTCP_PayloadSpecificFeedback = 206,
} RtcpPacketType;

typedef struct {
    u16 nBaseSequenceNumber;
    u16 nPacketStatusCount;
    u16 nReferenceTimeUpper;
    u8  nReferenceTimeLower;
    u8  nFeedbackPacketCount;
} __attribute__((packed)) TransportWideFeedbackHeader;

typedef enum {
    kTWCC_ChunkType_RunLength    = 0,
    kTWCC_ChunkType_StatusVector = 1,
} TWCC_ChunkType;

static void
HandleReceiverReport(LTSrtpSessionImpl *session, u8 nReportCount, u8 * pPayload, u32 nPayloadLen) {
    LT_UNUSED(session);
    if (nPayloadLen < nReportCount * sizeof(ReceptionReportBlock)) {
        LTLOG_YELLOWALERT("bad.rr.len", "Invalid RR len: %lu", LT_Pu32(nPayloadLen));
        return;
    }

    for (u32 i = 0; i < nReportCount; ++i) {
        ReceptionReportBlock * pReports = (ReceptionReportBlock *)pPayload;
        ReceptionReportBlock * pReport = &pReports[i];
        u32 nSyncSource = LT_NTOHL(pReport->nSyncSource);
        LTRtpStreamImpl *stream = LTSrtpSession_FindSendStreamBySyncSource(session, nSyncSource);
        if (!stream) {
            LTLOG("unk.ssrc", "Unknown SSRC: %lu - Ignoring packet", LT_Pu32(nSyncSource));
            return;
        }
        LTLOG_DEBUG("rrpt.h", "Receiver report for SSRC %lu", LT_Pu32(nSyncSource));
        LTLOG_DEBUG("rrpt.f", "  fraction lost: %lu",         LT_Pu32(LT_NTOHL(pReport->nWord1) >> 24));
        LTLOG_DEBUG("rrpt.c", "  cumulative lost: %lu",       LT_Pu32(LT_NTOHL(pReport->nWord1) & 0x00FFFFFF));
        LTLOG_DEBUG("rrpt.s", "  highest seq: %lu",           LT_Pu32(LT_NTOHL(pReport->nHighestSeqReceived)));
        LTLOG_DEBUG("rrpt.j", "  jitter: %lu",                LT_Pu32(LT_NTOHL(pReport->nInterarrivalJitter)));
        LTLOG_DEBUG("rrpt.l", "  last SR: %lu",               LT_Pu32(LT_NTOHL(pReport->nLastSR)));
        LTLOG_DEBUG("rrpt.d", "  delay since last SR: %lu",   LT_Pu32(LT_NTOHL(pReport->nDelaySinceLastSR)));
    }
}

static bool
ParseStatusChunks(u8 * pPayload, u32 nPayloadLen, TransportFeedback * pFeedback, u32 * pStatusSectionLen, u32 * pTimingSectionLen) {
    if (!pStatusSectionLen || !pTimingSectionLen || !pFeedback) return false;

    u32 nMaxNumPacketFeedbacks = pFeedback->nNumPacketsReceived;
    pFeedback->nNumPacketsReceived = 0;
    *pStatusSectionLen = 0;
    *pTimingSectionLen = 0;
    u16 nPacketIdx = 0;
    while (nPacketIdx < pFeedback->nPacketStatusCount) {
        if (nPayloadLen < sizeof(u16)) {
            LTLOG_YELLOWALERT("bad.twcc.status.len", "Ran out of message data before all packet statuses were parsed");
            return false;
        }
        u16 nPacketChunk = LT_NTOHS(*(u16*)pPayload);
        *pStatusSectionLen += sizeof(u16);
        pPayload    += sizeof(u16);
        nPayloadLen -= sizeof(u16);
        u8 nChunkType = (nPacketChunk & 0x8000) >> 15;
        if (nChunkType == kTWCC_ChunkType_StatusVector) {
            u16 nSymbolSize = ((nPacketChunk & 0x4000) >> 14) + 1;
            u16 nSymbolMask = (1 << nSymbolSize) - 1;
            u16 nNumSymbols = 14 / nSymbolSize;
            for (u8 i = 0; i < nNumSymbols; ++i) {
                u16 nStatusSymbol = (nPacketChunk >> (14 - (nSymbolSize * (i + 1)))) & nSymbolMask;
                *pTimingSectionLen += nStatusSymbol;
                if (nStatusSymbol != 0) {
                    if (pFeedback->pPacketFeedback) {
                        if (pFeedback->nNumPacketsReceived >= nMaxNumPacketFeedbacks) return false;
                        pFeedback->pPacketFeedback[pFeedback->nNumPacketsReceived] = (PacketFeedback) {
                            .nSequenceNumber = pFeedback->nBaseSequenceNumber + nPacketIdx,
                            .nStatus         = nStatusSymbol,
                        };
                    }
                    ++pFeedback->nNumPacketsReceived;
                }
                if (++nPacketIdx == pFeedback->nPacketStatusCount) break;
            }
        } else { // Run Length Chunk
            u8 nStatusSymbol = (nPacketChunk & 0x6000) >> 13;
            u16 nRunLength   = (nPacketChunk & 0x1FFF);
            for (u32 i = 0; i < nRunLength; ++i) {
                *pTimingSectionLen += nStatusSymbol;
                if (nStatusSymbol != 0) {
                    if (pFeedback->pPacketFeedback) {
                        if (pFeedback->nNumPacketsReceived >= nMaxNumPacketFeedbacks) return false;
                        pFeedback->pPacketFeedback[pFeedback->nNumPacketsReceived] = (PacketFeedback) {
                            .nSequenceNumber = pFeedback->nBaseSequenceNumber + nPacketIdx,
                            .nStatus         = nStatusSymbol,
                        };
                    }
                    ++pFeedback->nNumPacketsReceived;
                }
                if (++nPacketIdx == pFeedback->nPacketStatusCount) break;
            }
        }
    }

    return true;
}

static void
RequestStreamRefresh(LTSrtpSessionImpl *session, u32 nSyncSource) {
    LTRtpStreamImpl *stream = LTSrtpSession_FindSendStreamBySyncSource(session, nSyncSource);
    if (!stream) {
        LTLOG_YELLOWALERT("ssrc.nf", "Stream for SSRC %08lX not found", LT_Pu32(nSyncSource));
        return;
    }
    LTRtpStreamImpl_Refresh(stream);
}

static void
HandleTransportWideFeedback(LTSrtpSessionImpl *session, u8 * pPayload, u32 nPayloadLen) {
    if (nPayloadLen < sizeof(TransportWideFeedbackHeader)) {
        LTLOG_YELLOWALERT("bad.twcc.len", "Invalid TWCC feedback message len: %lu", LT_Pu32(nPayloadLen));
        return;
    }

    TransportWideFeedbackHeader * pTWCC = (TransportWideFeedbackHeader *)pPayload;
    TransportFeedback feedback = {
        .nFeedbackSequenceNumber = pTWCC->nFeedbackPacketCount,
        .nBaseSequenceNumber     = LT_NTOHS(pTWCC->nBaseSequenceNumber),
        .nPacketStatusCount      = LT_NTOHS(pTWCC->nPacketStatusCount),
        .tReferenceTime          = LTTime_Milliseconds(64 * ((LT_NTOHS(pTWCC->nReferenceTimeUpper) << 8) + pTWCC->nReferenceTimeLower)),
        .pPacketFeedback         = NULL
    };

    pPayload    += sizeof(TransportWideFeedbackHeader);
    nPayloadLen -= sizeof(TransportWideFeedbackHeader);

    u32 nStatusSectionLen, nTimingSectionLen;
    bool bStatusChunksValid = ParseStatusChunks(pPayload, nPayloadLen, &feedback, &nStatusSectionLen, &nTimingSectionLen);
    if (!bStatusChunksValid) return;
    if (nPayloadLen < (nStatusSectionLen + nTimingSectionLen)) {
        LTLOG_DEBUG("bad.timing.len", "Insufficient space for status and timing data: left=%lu, status=%lu, timing=%lu", LT_Pu32(nPayloadLen), LT_Pu32(nStatusSectionLen), LT_Pu32(nTimingSectionLen));
        return;
    }
    feedback.pPacketFeedback = lt_malloc(feedback.nNumPacketsReceived * sizeof(PacketFeedback));
    if (!feedback.pPacketFeedback) return;
    ParseStatusChunks(pPayload, nPayloadLen, &feedback, &nStatusSectionLen, &nTimingSectionLen);

    pPayload    += nStatusSectionLen;
    nPayloadLen -= nStatusSectionLen;

    for (u32 i = 0; i < feedback.nNumPacketsReceived; ++i) {
        u16 nStatus = feedback.pPacketFeedback[i].nStatus;
        s16 nReceiveDelta;
        if (nStatus == 1) {
            if (nPayloadLen < sizeof(u8)) {
                LTLOG_YELLOWALERT("bad.twcc.dly1.len", "Insufficient space for 1-byte packet delay");
                break;
            }
            nReceiveDelta = *pPayload;
            pPayload     += sizeof(u8);
            nPayloadLen  -= sizeof(u8);
        } else {
            if (nPayloadLen < sizeof(s16)) {
                LTLOG_YELLOWALERT("bad.twcc.dly2.len", "Insufficient space for 2-byte packet delay");
                break;
            }
            lt_memcpy(&nReceiveDelta, pPayload, sizeof(s16));
            nReceiveDelta = LT_NTOHS(nReceiveDelta);
            pPayload     += sizeof(s16);
            nPayloadLen  -= sizeof(s16);
        }
        feedback.pPacketFeedback[i].tReceiveDelta = LTTime_Microseconds(250 * nReceiveDelta);
    }
    RtpCongestionControl_HandleFeedback(session->pCongestionControl, &feedback);
    lt_free(feedback.pPacketFeedback);
}

static void
HandleRtcpTransportLayerFeedback(LTSrtpSessionImpl *session, u8 nFeedbackMessageType, u8 * pPayload, u32 nPayloadLen) {
    if (nPayloadLen < sizeof(u32)) {
        LTLOG_YELLOWALERT("bad.tlfb.len", "Invalid TLFB len: %lu", LT_Pu32(nPayloadLen));
        return;
    }
    pPayload    += sizeof(u32);
    nPayloadLen -= sizeof(u32);

    switch (nFeedbackMessageType) {
        case kTLFB_MsgType_TransportWideFeedback:
            HandleTransportWideFeedback(session, pPayload, nPayloadLen);
            break;
        case kTLFB_MsgType_GenericNack:
            session->rtcpMetrics.nGenericNackCount++;
            break;
        default:
            LTLOG_DEBUG("unk.tlfb", "Unsupported TLFB message type: %lu", LT_Pu32(nFeedbackMessageType));
            break;
    }
}

static void
HandleRtcpPayloadSpecificFeedback(LTSrtpSessionImpl *session, u8 nFeedbackMessageType, u8 * pPayload, u32 nPayloadLen) {
    LT_UNUSED(session);
    LT_UNUSED(pPayload);
    if (nPayloadLen < sizeof(u32)) {
        LTLOG_YELLOWALERT("bad.psfb.len", "Invalid PSFB len: %lu", LT_Pu32(nPayloadLen));
        return;
    }
    u32 nSyncSource = LT_NTOHL(*(u32 *)pPayload);

    switch (nFeedbackMessageType) {
        case kPSFB_MsgType_PictureLossIndication:
            session->rtcpMetrics.nPliCount++;
            break;
        case kPSFB_MsgType_SliceLossIndication:
            session->rtcpMetrics.nSliCount++;
            break;
        case kPSFB_MsgType_ReferencePictureSelectionIndication:
            session->rtcpMetrics.nRpsiCount++;
            break;
        case kPSFB_MsgType_FullIntraRequest:
            session->rtcpMetrics.nFirCount++;
            break;
        case kPSFB_MsgType_ApplicationLayerFB:
        default:
            LTLOG_DEBUG("unk.psfb", "Unsupported PSFB message type: %lu", LT_Pu32(nFeedbackMessageType));
            return;
    }
    RequestStreamRefresh(session, nSyncSource);
}

static u32
DecryptCompoundPacket(SrtpAuthContext *authCtx, u8 * pCompoundPacketData, u32 nCompoundPacketSize) {
    u32 nFooterLen = sizeof(u32) + authCtx->profile.nRtcpAuthTagLength;
    if (nCompoundPacketSize < sizeof(RtcpPacketHeader) + nFooterLen) {
        LTLOG_DEBUG("bad.rtcp.len", "Invalid RTCP packet length: %lu", LT_Pu32(nCompoundPacketSize));
        return 0;
    }

    RtcpPacketHeader * pHeader = (RtcpPacketHeader *)pCompoundPacketData;
    u8 * pFooter               = pCompoundPacketData + nCompoundPacketSize - nFooterLen;
    u8 * pAuthTag              = pFooter + sizeof(u32);
    u8 * pPayload              = pCompoundPacketData + sizeof(RtcpPacketHeader);

    u32 nEncryptedPortionLen     = nCompoundPacketSize - sizeof(RtcpPacketHeader) - nFooterLen;
    u32 nAuthenticatedPortionLen = nCompoundPacketSize - authCtx->profile.nRtcpAuthTagLength;

    if (!SrtpCrypto_VerifyAuthTag(&authCtx->profile, &authCtx->keys.rtcpReceive, pCompoundPacketData, nAuthenticatedPortionLen, pAuthTag, authCtx->profile.nRtcpAuthTagLength)) {
        LTLOG_DEBUG("rtcp.auth.fail", "RTCP packet authentication failed. Ignoring.");
        return 0;
    }

    bool bIsEncrypted = (pFooter[0] & kRtcpFooter_Byte0_EncryptedMask) != 0;
    if (bIsEncrypted) {
        u32 nSyncSource   = LT_NTOHL(pHeader->nSyncSource);
        u32 nPacketIdx    = LT_NTOHL(*(u32 *)pFooter) & kRtcpFooter_Word0_IndexMask;
        SrtpCrypto_CryptPayload(kCryptDirection_Decrypt, &authCtx->profile, &authCtx->keys.rtcpReceive, nPacketIdx, nSyncSource, pPayload, nEncryptedPortionLen, pPayload);
    }

    return nCompoundPacketSize - nFooterLen;
}

static u32
HandleRtcpPacket(LTSrtpSessionImpl *session, u8 *pCompoundPacketData, u32 nCompoundPacketDataLeft) {
    if (nCompoundPacketDataLeft < sizeof(RtcpPacketHeader)) {
        LTLOG_DEBUG("bad.rtcp.hdr.len", "Not enough data to parse next header: remaining=%lu", LT_Pu32(nCompoundPacketDataLeft));
        return 0;
    }
    RtcpPacketHeader *pHeader = (RtcpPacketHeader *)pCompoundPacketData;

    u8 nVersion = (pHeader->nByte0 & kRtcpHeader_Byte0_VersionMask) >> kRtcpHeader_Byte0_VersionShift;
    if (nVersion != kRtcpVersionValue) {
        LTLOG_DEBUG("bad.rtcp.ver", "Unsupported RTCP version: %lu", LT_Pu32(nVersion));
        return 0;
    }

    u16 nPacketLen  = ((LT_NTOHS(pHeader->nLength) + 1) * sizeof(u32));
    u16 nPayloadLen = nPacketLen - sizeof(RtcpPacketHeader);
    if (nCompoundPacketDataLeft < nPacketLen) {
        LTLOG_DEBUG("bad.rtcp.pld.len", "Not enough data to parse next payload: remaining=%lu, need=%lu", LT_Pu32(nCompoundPacketDataLeft), LT_Pu32(nPayloadLen));
        return 0;
    }
    u8 *pPayload = pCompoundPacketData + sizeof(RtcpPacketHeader);

    bool bIsPadded  = (pHeader->nByte0 & kRtcpHeader_Byte0_PaddingMask) >> kRtcpHeader_Byte0_PaddingShift;
    if (bIsPadded) {
        u8 nPaddingLen = pPayload[nPayloadLen-1];
        if (nPayloadLen < nPaddingLen) {
            LTLOG_DEBUG("bad.rtcp.pad.len", "Not enough data for padding: remaining=%lu, need=%lu", LT_Pu32(nPayloadLen), LT_Pu32(nPaddingLen));
            return 0;
        }
        nPayloadLen -= nPaddingLen;
    }
    u8 nCountField  = pHeader->nByte0 & 0x1F;

    switch (pHeader->nPayloadType) {
        case kPayloadType_RTCP_ReceiverReport:
            HandleReceiverReport(session, nCountField, pPayload, nPayloadLen);
            break;

        case kPayloadType_RTCP_TransportLayerFeedback:
            HandleRtcpTransportLayerFeedback(session, nCountField, pPayload, nPayloadLen);
            break;

        case kPayloadType_RTCP_PayloadSpecificFeedback:
            HandleRtcpPayloadSpecificFeedback(session, nCountField, pPayload, nPayloadLen);
            break;

        case kPayloadType_RTCP_SenderReport:
        case kPayloadType_RTCP_SourceDescription:
        case kPayloadType_RTCP_Goodbye:
        case kPayloadType_RTCP_Application:
        default:
            LTLOG_DEBUG("unk.rtcp", "Unsupported RTCP payload type: %lu", LT_Pu32(pHeader->nPayloadType));
            break;
    }
    return nPacketLen;
}

void
RtcpPacket_Handle(LTSrtpSessionImpl *session, SrtpAuthContext *authCtx, u8 *pCompoundPacketData, u32 nCompoundPacketSize) {
    u32 nCompoundPacketDataLeft = DecryptCompoundPacket(authCtx, pCompoundPacketData, nCompoundPacketSize);

    while (nCompoundPacketDataLeft > 0) {
        u32 nPacketLen = HandleRtcpPacket(session, pCompoundPacketData, nCompoundPacketDataLeft);
        if (nPacketLen == 0) return;
        nCompoundPacketDataLeft -= nPacketLen;
        pCompoundPacketData     += nPacketLen;
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jun-22   trajan      created
 */
