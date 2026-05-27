/******************************************************************************
 * source/lt/net/srtp/SrtpSession.h - SRTP Session Private
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPSESSION_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_SRTPSESSION_H

#include <lt/core/LTArray.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include "RtpStream.h"
#include "RtpCongestionControl.h"
#include "SrtpCrypto.h"
#include "SrtpTransport.h"
#include "RtpSendPacer.h"

typedef enum {
    kSrtpSessionState_New,
    kSrtpSessionState_Established,
    kSrtpSessionState_Terminated,
} SrtpSessionState;

typedef struct LTRtcpMetrics {
    u32     nPliCount;
    u32     nFirCount;
    u32     nSliCount;
    u32     nRpsiCount;
    u32     nGenericNackCount;
} LTRtcpMetrics;

typedef_LTObjectImpl(LTSrtpSession, LTSrtpSessionImpl) {
    char                         *sessionId;
    LTEvent                       hEvent;
    SrtpSessionState              nSessionState;
    LTArray                      *pSenders;
    LTArray                      *pReceivers;
    RtpCongestionControlContext  *pCongestionControl;
    RtpSendPacer                 *pPacer;
    SrtpTransport                *transport;
    LTRtcpMetrics                 rtcpMetrics;
} LTOBJECT_API;

bool
LTSrtpSession_Send(LTSrtpSessionImpl *session, RtpPacket *packet);

RtpPacket *
LTSrtpSession_GeneratePaddingPacket(LTSrtpSessionImpl *session, u8 paddingLen, u8 dscp);

LTRtpStreamImpl *
LTSrtpSession_FindRecvStreamBySyncSource(LTSrtpSessionImpl *session, u32 nSyncSource);

LTRtpStreamImpl *
LTSrtpSession_FindSendStreamBySyncSource(LTSrtpSessionImpl *session, u32 nSyncSource);

void
LTSrtpSession_OnTransportConnected(LTSrtpSessionImpl *session);

void
LTSrtpSession_OnTransportDisconnected(LTSrtpSessionImpl *session);

void
LTSrtpSession_OnTransportSentPacket(LTSrtpSessionImpl *session, RtpPacket *packet);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPSESSION_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Jun-22   trajan      created
 */
