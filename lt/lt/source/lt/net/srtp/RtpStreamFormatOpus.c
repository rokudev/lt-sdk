/*******************************************************************************
 * source/lt/net/srtp/RtpStreamFormatOpus.c - RTP packet packing/parsing for Opus
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include "RtpStream.h"
#include "SrtpSession.h"

// DEFINE_LTLOG_SECTION("Opus");

void
LTRtpStreamOpus_OnMediaEvent(LTMediaEvent event, void *eventData, void *clientData) {
    if ((event != kLTMediaEvent_Data) || !eventData || !clientData) return;
    LTRtpStreamImpl *stream = clientData;
    LTMediaData *pMediaData = eventData;

    u8 *pPacketData = pMediaData->pData;
    u32 nPacketLen   = pMediaData->nDataLen;
    u32 nRtpTimestamp = (pMediaData->nTimestamp * 48) / 1000;
    PayloadSegment payload = { .data = pPacketData, .size = nPacketLen };
    LTRtpStreamImpl_SendPacket(stream, &payload, 1, nRtpTimestamp, false, kLTNetDscp_EF);
}

bool
LTRtpStreamOpus_SupportsFormat(LTMediaFormat * pFormat) {
    if (!pFormat) return false;
    return (pFormat->nEncoding == kLTMediaEncoding_Opus);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  01-Jun-22   trajan      created
 */
