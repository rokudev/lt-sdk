/*******************************************************************************
 * source/lt/net/srtp/RtpCongestionControl.c - RTP Congestion Control
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "RtpCongestionControl.h"

// #define LOG_CC 1

DEFINE_LTLOG_SECTION("rtp.cc");

enum {
    // must be a power of two to ensure that packets are placed into the correct
    // history slot based on sequence number (which will eventually roll over)
    kNumPacketsTracked = 0x80,
    kSequenceNumberToHistorySlotMask = kNumPacketsTracked - 1,

    kInitialTargetBitrateBitsPerSec = 500000,  // bits/s
    kTargetBitrateMaxBitsPerSec     = 1500000, // bits/s
    kTargetBitrateMinBitsPerSec     = 200000,  // bits/s
    kBitrateUpdateIntervalMs        = 500,
    kHistogramBuckets               = 6,
};

static const float kBitrateStableError    = 0.1;
static const float kRollingAverageSamples = 40.0;

typedef enum {
    kPacketStatus_Sent,
    kPacketStatus_Received,
    kPacketStatus_Lost,
} PacketStatus;

typedef struct {
    u16          nSequenceNumber;
    LTTime       tSendTime;
    LTTime       tReceiveDelta;
    u32          payloadLen;
    PacketStatus nStatus;
} PacketStats;

struct RtpCongestionControlContext {
    char       *sessionId;
    LTEvent     hEvent;
    u32         nLossBasedTargetBitrate;
    u8          nNextFeedbackSequenceNumber;
    LTTime      tLastPacketReceiveTimeAbs;
    u32         totalDropped;
    float       avgLossRatio;
    float       avgBitrate;
    PacketStats sendHistory[kNumPacketsTracked];
    struct {
        u32 packetsSent;
        u32 packetsReceived;
        u32 feedbackPackets;
        u32 bitrateIncreases;
        u32 bitrateDecreases;
        u32 minTargetBitrate;
        u32 maxTargetBitrate;
        u32 targetBitrateHistogram[kHistogramBuckets];
        u32 minReceivedBitrate;
        u32 maxReceivedBitrate;
        u32 receiveBitrateHistogram[kHistogramBuckets];
    } metrics;
};

static const LTArgsDescriptor s_TargetBitrateEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_u32}};


static ILTEvent             *s_pEvent       = NULL;

static void
DispatchTargetBitrateEventCB(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTRtpCongestionControl_TargetBitrateChangedProc * pCallback = (LTRtpCongestionControl_TargetBitrateChangedProc *)pEventProc;
    RtpCongestionControlContext * pControl = LTArgs_pointerAt(0, pEventArgs);
    u32 nTargetBitrate = LTArgs_u32At(1, pEventArgs);
    pCallback(pControl, nTargetBitrate, pEventProcClientData);
}

void
RtpCongestionControl_OnTargetBitrateChanged(RtpCongestionControlContext * pControl, LTRtpCongestionControl_TargetBitrateChangedProc * pCallback, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    if (!pControl) return;
    s_pEvent->RegisterForEvent(pControl->hEvent, (void *)pCallback, pClientDataReleaseProc, pClientData, false);
}

void
RtpCongestionControl_NoTargetBitrateChanged(RtpCongestionControlContext * pControl, LTRtpCongestionControl_TargetBitrateChangedProc * pCallback) {
    if (!pControl) return;
    s_pEvent->UnregisterFromEvent(pControl->hEvent, (void *)pCallback);
}

RtpCongestionControlContext *
RtpCongestionControl_Create(const char *sessionId) {
    if (!s_pEvent)       s_pEvent       = lt_getlibraryinterface(ILTEvent, LT_GetCore());

    RtpCongestionControlContext * pControl = lt_malloc(sizeof(RtpCongestionControlContext));
    lt_memset(pControl, 0, sizeof(RtpCongestionControlContext));
    pControl->sessionId = lt_strdup(sessionId);
    pControl->hEvent = LT_GetCore()->CreateEvent(&s_TargetBitrateEventArgs, DispatchTargetBitrateEventCB, NULL, NULL, NULL);
    pControl->nLossBasedTargetBitrate = kInitialTargetBitrateBitsPerSec;
    pControl->avgBitrate = kInitialTargetBitrateBitsPerSec;
    pControl->avgLossRatio = 0.0;
    pControl->metrics.minReceivedBitrate = LT_U32_MAX;
    pControl->metrics.maxReceivedBitrate = 0;
    pControl->metrics.minTargetBitrate = LT_U32_MAX;
    pControl->metrics.maxTargetBitrate = 0;
    return pControl;
}

static void
LogSessionSummary(RtpCongestionControlContext *pControl) {
    LTLOG_SERVER("sum", "sid=%s pkts_s=%lu pkts_r=%lu fb_pkts=%lu br_inc=%lu br_dec=%lu "
                        "tgt_br_min=%lu tgt_br_max=%lu tgt_br_hist=[%lu %lu %lu %lu %lu %lu] "
                        "rcv_br_min=%lu rcv_br_max=%lu rcv_br_hist=[%lu %lu %lu %lu %lu %lu]",
        pControl->sessionId,
        LT_Pu32(pControl->metrics.packetsSent),
        LT_Pu32(pControl->metrics.packetsReceived),
        LT_Pu32(pControl->metrics.feedbackPackets),
        LT_Pu32(pControl->metrics.bitrateIncreases),
        LT_Pu32(pControl->metrics.bitrateDecreases),
        LT_Pu32(pControl->metrics.minTargetBitrate),
        LT_Pu32(pControl->metrics.maxTargetBitrate),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[0]),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[1]),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[2]),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[3]),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[4]),
        LT_Pu32(pControl->metrics.targetBitrateHistogram[5]),
        LT_Pu32(pControl->metrics.minReceivedBitrate),
        LT_Pu32(pControl->metrics.maxReceivedBitrate),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[0]),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[1]),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[2]),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[3]),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[4]),
        LT_Pu32(pControl->metrics.receiveBitrateHistogram[5]));
}

void
RtpCongestionControl_Destroy(RtpCongestionControlContext *pControl) {
    if (!pControl) return;
    LogSessionSummary(pControl);
    lt_destroyhandle(pControl->hEvent);
    lt_free(pControl->sessionId);
    lt_free(pControl);
}

u32
RtpCongestionControl_GetTargetBitrate(RtpCongestionControlContext * pControl) {
    return pControl->nLossBasedTargetBitrate;
}

void
RtpCongestionControl_OnPacketSent(RtpCongestionControlContext * pControl, u16 nTransportSequenceNumber, u32 payloadLen) {
    if (!pControl) return;

    ++pControl->metrics.packetsSent;

    u8 nHistorySlot = nTransportSequenceNumber & kSequenceNumberToHistorySlotMask;
    pControl->sendHistory[nHistorySlot] = (PacketStats) {
        .nSequenceNumber = nTransportSequenceNumber, // do we need to save sequence number?
        .tSendTime       = LT_GetCore()->GetKernelTime(),
        .payloadLen      = payloadLen,
        .nStatus         = kPacketStatus_Sent,
    };
}

static void
UpdateLossBasedTargetBitrate(RtpCongestionControlContext * pControl, u32 receiveBitrate, float fLossRatio) {
    u32 newBitrate = pControl->nLossBasedTargetBitrate;
    if (fLossRatio > 0.1) {
        newBitrate *= (1.0 - (fLossRatio / 2));
    } else if (fLossRatio < 0.02) {
        // TODO send filler data to achieve requested target bitrate more quickly so we don't need to
        // wait so long for encoder bitrate to catch up which necessitates this bitrate stability check
        float curBitrateErr = lt_fabs(receiveBitrate - pControl->nLossBasedTargetBitrate) / pControl->nLossBasedTargetBitrate;
        if (curBitrateErr < kBitrateStableError) {
            newBitrate *= 1.05;
        }
    } /* else 2-10% loss ratio - no change to bandwidth estimate */

    /* Clamp to min/max bitrate */
    newBitrate = LT_MIN((u32)kTargetBitrateMaxBitsPerSec, newBitrate);
    newBitrate = LT_MAX((u32)kTargetBitrateMinBitsPerSec, newBitrate);

    if (pControl->nLossBasedTargetBitrate != newBitrate) {
        if (newBitrate > pControl->nLossBasedTargetBitrate) {
            ++pControl->metrics.bitrateIncreases;
        } else if (newBitrate < pControl->nLossBasedTargetBitrate) {
            ++pControl->metrics.bitrateDecreases;
        }
        pControl->nLossBasedTargetBitrate = newBitrate;
        if (pControl->nLossBasedTargetBitrate > pControl->metrics.maxTargetBitrate) {
            pControl->metrics.maxTargetBitrate = pControl->nLossBasedTargetBitrate;
        }
        if (pControl->nLossBasedTargetBitrate < pControl->metrics.minTargetBitrate) {
            pControl->metrics.minTargetBitrate = pControl->nLossBasedTargetBitrate;
        }

        s_pEvent->NotifyEvent(pControl->hEvent, pControl, pControl->nLossBasedTargetBitrate);
    }
    s32 histBucket = kHistogramBuckets * (pControl->nLossBasedTargetBitrate - kTargetBitrateMinBitsPerSec) / (kTargetBitrateMaxBitsPerSec - kTargetBitrateMinBitsPerSec);
    histBucket = LT_MAX(histBucket, (s32)0);
    histBucket = LT_MIN(histBucket, (s32)(kHistogramBuckets - 1));
    ++pControl->metrics.targetBitrateHistogram[histBucket];
}

#ifdef LOG_CC
static void
PrintReport(float avgBitrate, u32 nLossBasedTargetBitrate, float fLossRatio, u32 totalDropped) {
    static LTTime nextReport;
    LTTime now = LT_GetCore()->GetKernelTime();
    if (LTTime_IsGreaterThan(now, nextReport)) {
        LTLOG("", "Actual bitrate: %lu Kbit/s, target bitrate: %lu Kbit/s, loss pct: %0.2f, dropped: %lu",
            LT_Pu32(avgBitrate / 1000),
            LT_Pu32(nLossBasedTargetBitrate / 1000),
            fLossRatio * 100,
            LT_Pu32(totalDropped));
        nextReport = LTTime_Add(now, LTTime_Milliseconds(500));
    }
}
#endif

void
RtpCongestionControl_HandleFeedback(RtpCongestionControlContext * pControl, TransportFeedback * pFeedback) {
    if (!pControl || !pFeedback || !pFeedback->pPacketFeedback) return;

    ++pControl->metrics.feedbackPackets;
    LTTime timespan = {0};
    u32 nNumDropped = 0;
    u32 nBytesReceived = 0;
    LTTime tReceiveTimeAbs = pFeedback->tReferenceTime;
    u16 nNextExpectedSequenceNumber = pFeedback->nBaseSequenceNumber;
    for (u32 i = 0; i < pFeedback->nNumPacketsReceived; ++i) {
        PacketFeedback * pPacketFeedback = &pFeedback->pPacketFeedback[i];
        while (pPacketFeedback->nSequenceNumber != nNextExpectedSequenceNumber) {
            u8 nHistorySlot = nNextExpectedSequenceNumber++ & kSequenceNumberToHistorySlotMask;
            pControl->sendHistory[nHistorySlot].nStatus = kPacketStatus_Lost;
            ++nNumDropped;
        }

        u8 nHistorySlot = pPacketFeedback->nSequenceNumber & kSequenceNumberToHistorySlotMask;
        if (pPacketFeedback->nStatus == kPacketFeedbackStatus_NotReceived) {
            pControl->sendHistory[nHistorySlot].nStatus = kPacketStatus_Lost;
            ++nNumDropped;
        } else {
            pControl->sendHistory[nHistorySlot].nStatus = kPacketStatus_Received;
            ++pControl->metrics.packetsReceived;
        }
        pControl->sendHistory[nHistorySlot].tReceiveDelta = pPacketFeedback->tReceiveDelta;
        ++nNextExpectedSequenceNumber;
        nBytesReceived += pControl->sendHistory[nHistorySlot].payloadLen;

        LTTime_AddTo(tReceiveTimeAbs, pPacketFeedback->tReceiveDelta);
        if (i != 0) {
            LTTime_AddTo(timespan, pPacketFeedback->tReceiveDelta);
        } else if (pFeedback->nFeedbackSequenceNumber == pControl->nNextFeedbackSequenceNumber) {
            LTTime_AddTo(timespan, LTTime_Subtract(tReceiveTimeAbs, pControl->tLastPacketReceiveTimeAbs));
        }
    }
    pControl->nNextFeedbackSequenceNumber = pFeedback->nFeedbackSequenceNumber + 1;
    pControl->tLastPacketReceiveTimeAbs = tReceiveTimeAbs;

    if (LTTime_GetNanoseconds(timespan) > 0) {
        float timespan_ns = LTTime_GetNanoseconds(timespan);
        u32 nActualBitrateBps = (8e9 * nBytesReceived / timespan_ns);

        pControl->totalDropped += nNumDropped;

        pControl->avgBitrate -= pControl->avgBitrate / kRollingAverageSamples;
        pControl->avgBitrate += nActualBitrateBps / kRollingAverageSamples;

        float fLossRatio = (float)nNumDropped / pFeedback->nPacketStatusCount;
        pControl->avgLossRatio -= pControl->avgLossRatio / kRollingAverageSamples;
        pControl->avgLossRatio += fLossRatio / kRollingAverageSamples;

        if (pControl->avgBitrate < pControl->metrics.minReceivedBitrate) {
            pControl->metrics.minReceivedBitrate = pControl->avgBitrate;
        }
        if (pControl->avgBitrate > pControl->metrics.maxReceivedBitrate) {
            pControl->metrics.maxReceivedBitrate = pControl->avgBitrate;
        }

        s32 histBucket = kHistogramBuckets * (pControl->avgBitrate - kTargetBitrateMinBitsPerSec) / (kTargetBitrateMaxBitsPerSec - kTargetBitrateMinBitsPerSec);
        histBucket = LT_MAX(histBucket, (s32)0);
        histBucket = LT_MIN(histBucket, (s32)(kHistogramBuckets - 1));
        ++pControl->metrics.receiveBitrateHistogram[histBucket];

        UpdateLossBasedTargetBitrate(pControl, nActualBitrateBps, fLossRatio);

#ifdef LOG_CC
        PrintReport(pControl->avgBitrate, pControl->nLossBasedTargetBitrate, pControl->avgLossRatio, pControl->totalDropped);
#endif
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jun-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
