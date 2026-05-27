/******************************************************************************
 * source/lt/net/srtp/SrtpTransport.h - SRTP Transport
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPTRANSPORT_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_SRTPTRANSPORT_H

#include "RtpPacket.h"
#include "SrtpCrypto.h"

typedef struct {
    LTSrtpSessionImpl     *session;
    LTOThread             *thread;
    u16                    transportSequenceNumber;
    LTList                 packetQueue;
    u32                    numPacketsQueued;
    LTMutex               *mutex;
    LTSocket               hDtlsSocket;
    LTSocket               hTransportSocket;
    SrtpAuthContext       *authContext;
    u8                     packetBuf[kRtpPacket_MaxLen];
} SrtpTransport;

SrtpTransport *
SrtpTransport_Create(LTSrtpSessionImpl *session, LTSocket hTransportSocket, LTSocket hDtlsSocket);

void
SrtpTransport_Destroy(SrtpTransport *transport);

bool
SrtpTransport_Send(SrtpTransport *transport, RtpPacket *packet);

void
SrtpTransport_SetTransportSocket(SrtpTransport *transport, LTSocket hSocket);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_SRTPTRANSPORT_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Sep-24   trajan      created
 */
