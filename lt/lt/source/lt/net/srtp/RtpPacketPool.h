/******************************************************************************
 * source/lt/net/srtp/RtpPacketPool.h - RTP Packet Pool
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKETPOOL_H
#define ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKETPOOL_H

#include "RtpPacket.h"

bool
RtpPacketPool_LibInit(void);

void
RtpPacketPool_LibFini(void);

RtpPacket *
RtpPacketPool_GetPacket(void);

void
RtpPacketPool_ReleasePacket(RtpPacket *packet);

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTP_RTPPACKETPOOL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  19-Sep-24   trajan      created
 */
