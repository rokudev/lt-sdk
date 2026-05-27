/******************************************************************************
 * source/lt/net/srtp/RtcpPacket.h - Real-time Transport Control Protocol (RTCP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTCPPACKET_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTCPPACKET_H

#include "SrtpSession.h"

void
RtcpPacket_Handle(LTSrtpSessionImpl *session, SrtpAuthContext *authCtx, u8 * pCompoundPacketData, u32 pCompoundPacketSize);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTCPPACKET_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Jun-22   trajan      created
 */
