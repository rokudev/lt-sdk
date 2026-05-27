/*******************************************************************************
 * source/lt/net/srtp/RtpPacketPool.c - RTP Packet Pool
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "RtpPacketPool.h"

// DEFINE_LTLOG_SECTION("rtp.pkt.pool");

static LTList   s_FreeList;
static LTMutex *s_Mutex;

void
RtpPacketPool_LibFini(void) {
    while (!LTList_IsEmpty(&s_FreeList)) {
        RtpPacket *packet = LTList_GetNodeDataOfType(RtpPacket, s_FreeList.pNext);
        LTList_Remove(&packet->node);
        lt_free(packet);
    }
    lt_destroyobject(s_Mutex);
}

bool
RtpPacketPool_LibInit(void) {
    do {
        LTList_Init(&s_FreeList);
        if (!(s_Mutex = lt_createobject(LTMutex))) break;

        return true;
    } while(false);

    RtpPacketPool_LibFini();
    return false;
}

RtpPacket *
RtpPacketPool_GetPacket(void) {
    RtpPacket *packet = NULL;
    s_Mutex->API->Lock(s_Mutex);
    if (!LTList_IsEmpty(&s_FreeList)) {
        packet = LTList_GetNodeDataOfType(RtpPacket, s_FreeList.pNext);
        LTList_Remove(&packet->node);
    }
    s_Mutex->API->Unlock(s_Mutex);

    if (!packet) packet = lt_malloc(sizeof(RtpPacket));
    return packet;
}

void
RtpPacketPool_ReleasePacket(RtpPacket *packet) {
    s_Mutex->API->Lock(s_Mutex);
    LTList_AddTail(&s_FreeList, &packet->node);
    s_Mutex->API->Unlock(s_Mutex);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  19-Sep-24   trajan      created
 */
