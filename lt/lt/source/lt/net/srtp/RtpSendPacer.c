/*******************************************************************************
 * source/lt/net/srtp/RtpSendPacer.c - RTP packet transmission pacer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/net/core/LTNetCore.h>
#include "RtpStream.h"
#include "RtpSendPacer.h"
#include "SrtpSession.h"
#include "RtpPacketPool.h"

DEFINE_LTLOG_SECTION("rtp.pacer");

enum {
    kPacerThreadPriority       = 5,       // Should be higher than default to maintain timer rate, but lower or same as network threads priority to prevent packets piling up
    kBurstIntervalMs           = 5,       // Interval between packet bursts
    kMaxQueueWaitUs            = 50000,   // Desired maximum queueing delay
    kPaddingBitrateThreshold   = 650000,  // Only send padding below this bitrate
    kMaxQueuedPackets          = 128,     // Maximum size of the send queue before packets start being dropped
    kConsecutiveDroppedPackets = 100,     // How many consecutive packets need to be dropped before alert is logged
    kStallReportMinIntervalSec = 1,       // Interval between stall reports being logged
    kMaxPaddingPacketSize      = 255,     // Maximum payload size of a padding packet
};

static ILTThread            *s_pThread    = NULL;

/* Initialize PacketQueue list */ 
static void
PacketQueue_Init(PacketQueue *queue) {
    LTList_Init(&queue->packetList);
    queue->mutex = lt_createobject(LTMutex);
    queue->packetsQueued = 0;
    queue->bytesQueued = 0;
}

/* Removes all packets in the queue */
static void
PacketQueue_Destroy(PacketQueue *queue) {
    while(!LTList_IsEmpty(&queue->packetList)) {
        RtpPacket *packet = LTList_GetNodeDataOfType(RtpPacket, queue->packetList.pNext);
        LTList_Remove(&packet->node);
        RtpPacketPool_ReleasePacket(packet);
    }
    lt_destroyobject(queue->mutex);
}

/* Return true if no packets queued. */ 
static bool
PacketQueue_IsEmpty(PacketQueue *queue) {
    return queue->packetsQueued == 0;
}

/* Return total queued bytes (payload + padding).*/ 
static u32
PacketQueue_GetBytesQueued(PacketQueue *queue) {
    return queue->bytesQueued;
}

/*  Removes the first packet from the queue and updates queue counters.
    PRECONDITION: queue->mutex lock should be acquired before calling PacketQueue_Pop function*/
static RtpPacket *
PacketQueue_Pop(PacketQueue *queue) {
    if(LTList_IsEmpty(&queue->packetList)) {
        return NULL;
    }

    RtpPacket *packet = LTList_GetNodeDataOfType(RtpPacket, queue->packetList.pNext);
    LTList_Remove(&packet->node);
    queue->packetsQueued -= 1;
    queue->bytesQueued -= packet->payloadLen + packet->paddingLen;

    return packet;
}

/*  Pushes a packet to the queue, removing oldest packets if capacity is exceeded
    Locks the queue during the operation and updates queue counters*/ 
static u32
PacketQueue_Push(PacketQueue *queue, RtpPacket *packet) {
    queue->mutex->API->Lock(queue->mutex);

    u32 packetsDropped = 0;
    while (queue->packetsQueued >= kMaxQueuedPackets) {
        // Drop the oldest packet to make room for new packet
        RtpPacket *dropPacket = PacketQueue_Pop(queue);
        RtpPacketPool_ReleasePacket(dropPacket);
        ++packetsDropped;
    }

    // Always add the newest packet
    LTList_AddTail(&queue->packetList, &packet->node);
    queue->packetsQueued += 1;
    queue->bytesQueued += packet->payloadLen + packet->paddingLen;

    queue->mutex->API->Unlock(queue->mutex);

    return packetsDropped;
}

/*  Generates an SRTP padding packet if allowed by pacing and bitrate limits. 
    return Pointer to the generated RtpPacket, or NULL if padding cannot be sent. */ 
static RtpPacket *
GeneratePaddingPacket(RtpSendPacer *pPacer) {
    u32 nTargetBitrateBitsPerSec = RtpCongestionControl_GetTargetBitrate(pPacer->pCongestionControl);
    if (!pPacer->bCanSendPadding    ||
        (pPacer->nBurstBudget <= 0) ||
        (nTargetBitrateBitsPerSec > kPaddingBitrateThreshold)) return false;

    u8 paddingLen = LT_MIN(pPacer->nBurstBudget, (s32)kMaxPaddingPacketSize);
    return LTSrtpSession_GeneratePaddingPacket(pPacer->srtpSession, paddingLen, kLTNetDscp_AF42);
}

/*  Get next packet to send: uses currentPacket if present, else pops from queue with proper locking;
    may fall back to padding. No external lock required by caller. */ 
static RtpPacket *
GetNextPacket(RtpSendPacer *pPacer) {
    RtpPacket *packet = pPacer->currentPacket;
    PacketQueue *queue = &pPacer->sendQueue;
    if (!packet) {
        queue->mutex->API->Lock(queue->mutex);
        packet = PacketQueue_Pop(queue);   
        queue->mutex->API->Unlock(queue->mutex);
    }
    if (!packet) packet = GeneratePaddingPacket(pPacer);
    return packet;
}

/*  Update stall detector on send result; tracks stall durations, logs/reporting, and 
    transport metrics reset/show. No external lock required. Called from pacer thread context. */ 
static void
StallDetector_Update(RtpSendPacer *pPacer, bool bSendResult) {
    LTTime now = LT_GetCore()->GetKernelTime();
    if (!bSendResult) {
        if (!pPacer->stalled) {
            pPacer->stalled = true;
            pPacer->stallStart = now;

            // Reset transport metrics
            if (pPacer->transport) {
                ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pPacer->transport->hTransportSocket);
                pSocket->SetProperty(pPacer->transport->hTransportSocket, "metrics.reset", NULL);
            }
        }
        ++pPacer->sendErrors;
        // speed up send rate on next burst to make up for the backlog created on
        // this short burst.
        pPacer->bDrainQueue = true;
        return;
    } else if (pPacer->stalled) {
        LTTime stallDuration = LTTime_Subtract(now, pPacer->stallStart);
        if (LTTime_IsGreaterThan(stallDuration, pPacer->maxStallDuration)) {
            pPacer->maxStallDuration = stallDuration;
        }
        pPacer->stalled = false;
        ++pPacer->stallCount;
    }
    if (pPacer->stallCount && LTTime_IsGreaterThan(now, pPacer->nextStallReport)) {
        LTLOG_SERVER("stall", "Pacer stall - stlcnt=%lu maxstl(ms)=%lld snderrs=%lu",
            LT_Pu32(pPacer->stallCount),
            LT_Ps64(LTTime_GetMilliseconds(pPacer->maxStallDuration)),
            LT_Pu32(pPacer->sendErrors));
        pPacer->maxStallDuration = LTTime_Zero();
        pPacer->stallCount = 0;
        pPacer->sendErrors = 0;
        pPacer->nextStallReport = LTTime_Add(now, LTTime_Seconds(kStallReportMinIntervalSec));

        // Show transport metrics
        if (pPacer->transport) {
            ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, pPacer->transport->hTransportSocket);
            pSocket->SetProperty(pPacer->transport->hTransportSocket, "metrics.show", NULL);
        }
    }
}

/*  Dequeue and send packets while burst budget remains; updates pacing state, padding eligibility,
    and drain flag. Runs in pacer thread timer context. Handles re-trying currentPacket on send failure. 
    No external lock required. */
static void
WritePackets(RtpSendPacer *pPacer) {
    while (pPacer->nBurstBudget > 0 && s_pThread->GetThreadState(pPacer->hThread) != kLTThread_ThreadState_TerminatePending) {
        RtpPacket *packet = GetNextPacket(pPacer);
        if (!packet) break;

        bool bSendResult = SrtpTransport_Send(pPacer->transport, packet);
        StallDetector_Update(pPacer, bSendResult);
        if (!bSendResult) {
            pPacer->currentPacket = packet;
            break;
        }

        pPacer->currentPacket = NULL;
        pPacer->bCanSendPadding = (packet->packetType == kRtpPacketType_Padding) ||
                                  ((packet->packetType == kRtpPacketType_Media) && packet->mark);
        pPacer->nBurstBudget -= packet->payloadLen + packet->paddingLen;
        pPacer->bDrainQueue = pPacer->bDrainQueue && !PacketQueue_IsEmpty(&pPacer->sendQueue);
    }
}

// in RtpSendP thread
static void
OnSendBurst(void *pClientData) {
    RtpSendPacer *pPacer = pClientData;

    u32 nTargetBitrateBitsPerSec = RtpCongestionControl_GetTargetBitrate(pPacer->pCongestionControl);
    LTTime now = LT_GetCore()->GetKernelTime();
    LTTime timeSinceLastBurst = LTTime_Subtract(now, pPacer->lastBurstTime);
    pPacer->lastBurstTime = now;
    u32 nBurstSize = (nTargetBitrateBitsPerSec * LTTime_GetMicroseconds(timeSinceLastBurst)) / (8 * 1000000); // (bits/s * us) / (bits/byte * us/s) = bytes
    if (pPacer->bDrainQueue) {
        u32 bytesQueued = PacketQueue_GetBytesQueued(&pPacer->sendQueue);
        u32 burstAdjustment = (bytesQueued * LTTime_GetMicroseconds(timeSinceLastBurst)) / kMaxQueueWaitUs;
        nBurstSize += burstAdjustment;
    }
    if (pPacer->nBurstBudget < 0)
        pPacer->nBurstBudget += nBurstSize;
    else
        pPacer->nBurstBudget = nBurstSize;

    WritePackets(pPacer);
}

RtpSendPacer *
RtpSendPacer_Create(const char *sessionId, LTSrtpSessionImpl *srtpSession, SrtpTransport *transport, RtpCongestionControlContext *pCongestionControl) {
    RtpSendPacer *pPacer = lt_malloc(sizeof(RtpSendPacer));
    if (!pPacer) return NULL;
    lt_memset(pPacer, 0, sizeof(RtpSendPacer));
    pPacer->sessionId = lt_strdup(sessionId);
    pPacer->srtpSession = srtpSession;
    pPacer->transport = transport;
    pPacer->pCongestionControl = pCongestionControl;
    PacketQueue_Init(&pPacer->sendQueue);
    pPacer->nDroppedPacketCount = 0;
    pPacer->lastBurstTime = LT_GetCore()->GetKernelTime();
    pPacer->bCanSendPadding = false;

    char threadName[20] = {};
    lt_snprintf(threadName, sizeof(threadName), "RtpSendP-%10s", sessionId);
    pPacer->hThread = LT_GetCore()->CreateThread(threadName);
    s_pThread->SetStackSize(pPacer->hThread, 2048);
    s_pThread->SetPriority(pPacer->hThread, kPacerThreadPriority);
    s_pThread->Start(pPacer->hThread, NULL, NULL);
    s_pThread->SetTimerAbsolute(pPacer->hThread, LT_GetCore()->GetKernelTime(), LTTime_Milliseconds(kBurstIntervalMs), OnSendBurst, NULL, pPacer);

    return pPacer;
}

void
RtpSendPacer_Stop(RtpSendPacer *pPacer) {
    if (!pPacer) return;
    // KillTimer will wait for the previous timer proc to finish, so the latest popped packet data will not be leaked.
    s_pThread->KillTimer(pPacer->hThread, OnSendBurst, pPacer);
}

void
RtpSendPacer_Destroy(RtpSendPacer *pPacer) {
    if (!pPacer) return;
    RtpSendPacer_Stop(pPacer);
    lt_destroyhandle(pPacer->hThread);
    PacketQueue_Destroy(&pPacer->sendQueue);
    lt_free(pPacer->sessionId);
    lt_free(pPacer);
}

// in Webrtc thread
void
RtpSendPacer_AddPacket(RtpSendPacer *pPacer, RtpPacket *packet) {
    if (!pPacer) return;

    // If this is the start of a new frame and data from the last frame is still queued, temporarily increase the pace to 1.5x
    if (pPacer->bLastFrameComplete && !PacketQueue_IsEmpty(&pPacer->sendQueue)) {
        pPacer->bDrainQueue = true;
    }

    u32 packetsDropped = PacketQueue_Push(&pPacer->sendQueue, packet);
    if (packetsDropped) {
        // Alert and reset every kConsecutiveDroppedPackets dropped packets
        pPacer->nDroppedPacketCount += packetsDropped;
        if (pPacer->nDroppedPacketCount >= kConsecutiveDroppedPackets) {
            LTLOG_YELLOWALERT("queue.max", "Dropped %u consecutive packets session %s",
                pPacer->nDroppedPacketCount, pPacer->sessionId);
            pPacer->nDroppedPacketCount = 0;
        }
    } else {
        pPacer->nDroppedPacketCount = 0;
    }

    pPacer->bLastFrameComplete = packet->mark;
}

bool
RtpSendPacer_LibInit(void) {
    s_pThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    if (!s_pThread) {
        RtpSendPacer_LibFini();
        return false;
    }
    return true;
}

void
RtpSendPacer_LibFini(void) {
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  13-Jun-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
