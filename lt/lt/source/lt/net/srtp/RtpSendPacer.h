/******************************************************************************
 * source/lt/net/srtp/RtpSendPacer.h - RTP packet transmission pacer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPSENDPACER_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTPSENDPACER_H

#include "SrtpTransport.h"
#include "RtpCongestionControl.h"

typedef struct {
    LTMutex                      *mutex;
    LTList                        packetList;
    u32                           packetsQueued;
    u32                           bytesQueued;
} PacketQueue;

typedef struct RtpSendPacer {
    char                         *sessionId;
    LTThread                      hThread;
    LTMutex                      *socketMutex;
    LTSrtpSessionImpl            *srtpSession;
    SrtpTransport                *transport;
    RtpCongestionControlContext  *pCongestionControl;
    u32                           nTargetBitrate;
    bool                          bDrainQueue;
    s32                           nBurstBudget;
    LTTime                        lastBurstTime;
    bool                          bLastFrameComplete;
    bool                          bCanSendPadding;
    u8                            nDroppedPacketCount;
    PacketQueue                   sendQueue;
    bool                          stalled;
    LTTime                        stallStart;
    LTTime                        maxStallDuration;
    u32                           stallCount;
    u32                           sendErrors;
    LTTime                        nextStallReport;
    RtpPacket                    *currentPacket;
} RtpSendPacer;

bool
RtpSendPacer_LibInit(void);

void
RtpSendPacer_LibFini(void);

RtpSendPacer *
RtpSendPacer_Create(const char *sessionId, LTSrtpSessionImpl *srtpSession, SrtpTransport *transport, RtpCongestionControlContext * pCongestionContro);

void
RtpSendPacer_Destroy(RtpSendPacer *pacer);

void
RtpSendPacer_Stop(RtpSendPacer *pacer);

void
RtpSendPacer_AddPacket(RtpSendPacer * pPacer, RtpPacket *packet);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPSENDPACER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  13-Jun-22   trajan      created
 */
