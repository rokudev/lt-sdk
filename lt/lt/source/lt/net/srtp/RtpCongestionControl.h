/******************************************************************************
 * source/lt/net/srtp/RtpCongestionControl.h - RTP Congestion Control
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPCONGESTIONCONTROL_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTPCONGESTIONCONTROL_H

typedef struct RtpCongestionControlContext RtpCongestionControlContext;

typedef enum {
    kPacketFeedbackStatus_NotReceived        = 0,
    kPacketFeedbackStatus_ReceivedSmallDelta = 1,
    kPacketFeedbackStatus_ReceivedLargeDelta = 2,
} PacketFeedbackStatus;

typedef struct {
    u16                  nSequenceNumber;
    PacketFeedbackStatus nStatus;
    LTTime               tReceiveDelta;
} PacketFeedback;

typedef struct {
    u8               nFeedbackSequenceNumber;
    u16              nBaseSequenceNumber;
    u16              nPacketStatusCount;
    LTTime           tReferenceTime;
    u16              nNumPacketsReceived;
    PacketFeedback * pPacketFeedback;
} TransportFeedback;

typedef void (LTRtpCongestionControl_TargetBitrateChangedProc)(RtpCongestionControlContext * pControl, u32 nTargetBitrate, void * pClientData);

RtpCongestionControlContext *
RtpCongestionControl_Create(const char *sessionId);

void
RtpCongestionControl_Destroy(RtpCongestionControlContext *pControl);

u32
RtpCongestionControl_GetTargetBitrate(RtpCongestionControlContext * pControl);

void
RtpCongestionControl_OnPacketSent(RtpCongestionControlContext * pControl, u16 nTransportSequenceNumber, u32 nPacketLen);

void
RtpCongestionControl_HandleFeedback(RtpCongestionControlContext * pControl, TransportFeedback * pFeedback);

void
RtpCongestionControl_OnTargetBitrateChanged(RtpCongestionControlContext * pControl, LTRtpCongestionControl_TargetBitrateChangedProc * pCallback, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);

void
RtpCongestionControl_NoTargetBitrateChanged(RtpCongestionControlContext * pControl, LTRtpCongestionControl_TargetBitrateChangedProc * pCallback);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPCONGESTIONCONTROL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Jun-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
