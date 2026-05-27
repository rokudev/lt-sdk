/*******************************************************************************
 * source/lt/net/srtp/SrtpTransport.c - SRTP Transport
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "SrtpTransport.h"
#include "SrtpSession.h"
#include "RtcpPacket.h"
#include "RtpPacketPool.h"

DEFINE_LTLOG_SECTION("rtp.transport");

enum {
    kMaxPacketBacklog = 50,
};

static void
OnDtlsSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(hSocket);
    if (!clientData) return;
    SrtpTransport *transport = clientData;

    switch (event) {
        case kLTSocket_Event_Connected:
            if ((transport->authContext = SrtpCrypto_CreateAuthContext(transport->hDtlsSocket))) {
                LTSrtpSession_OnTransportConnected(transport->session);
                break;
            }
            /* Intentional fall-through */

        case kLTSocket_Event_Disconnected:
            LTSrtpSession_OnTransportDisconnected(transport->session);
            break;

        default: break;
    }
}

static void
ReadTransportSocket(SrtpTransport *transport) {
    // keep reading until no more packets are available
    while (true) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hTransportSocket);
        s32 packetSize = pSocket->ReadSocket(transport->hTransportSocket, NULL, 0);
        if (packetSize == 0) return;
        if (packetSize < 0) {
            LTLOG_YELLOWALERT("sock.read.err", "Socket read failed: %ld", LT_Ps32(packetSize));
            return;
        }
        u8 *packetData = lt_malloc(packetSize);
        if (!packetData) {
            LTLOG_YELLOWALERT("rx.alloc.err", "Failed to allocate receive buffer");
            return;
        }
        s32 bytesRead = pSocket->ReadSocket(transport->hTransportSocket, packetData, packetSize);
        LT_ASSERT(bytesRead == packetSize);

        if (transport->authContext) {
            u8 nPayloadType = packetData[1];
            if ((nPayloadType >= 200) && (nPayloadType <= 206))
                RtcpPacket_Handle(transport->session, transport->authContext, packetData, packetSize);
            else
                RtpPacket_Handle(transport->session, transport->authContext, packetData, packetSize);
        }
        lt_free(packetData);
    }
}

static bool
SendPacket(SrtpTransport *transport, RtpPacket *packet) {
    if (packet->extMap.nTransportWideCC) {
        packet->transportSequenceNumber = transport->transportSequenceNumber++;
    }

    u32 packetLen = RtpPacket_Pack(packet, transport->authContext, transport->packetBuf, sizeof(transport->packetBuf));
    if (packetLen == 0) {
        return false;
    }

    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, transport->hTransportSocket);
    pSocket->SetProperty(transport->hTransportSocket, "dscp", &packet->dscp);
    s32 bytesWritten = pSocket->WriteSocket(transport->hTransportSocket, transport->packetBuf, packetLen);
    return (bytesWritten == (s32)packetLen);
}

static void
DrainSendQueue(SrtpTransport *transport) {
    transport->mutex->API->Lock(transport->mutex);
    while (!LTList_IsEmpty(&transport->packetQueue)) {
        RtpPacket *packet = LTList_GetNodeDataOfType(RtpPacket, transport->packetQueue.pNext);
        if (!SendPacket(transport, packet)) break;

        --transport->numPacketsQueued;
        LTList_Remove(&packet->node);

        LTSrtpSession_OnTransportSentPacket(transport->session, packet);
    }
    transport->mutex->API->Unlock(transport->mutex);
}

static void
OnTransportSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(hSocket);
    if (!clientData) return;
    SrtpTransport *transport = clientData;
    switch (event) {
        case kLTSocket_Event_ReadReady:
            ReadTransportSocket(transport);
            break;

        case kLTSocket_Event_WriteReady:
            DrainSendQueue(transport);
            break;

        default:
            break;
    }
}

static void
DrainSendQueueProc(void *clientData) {
    DrainSendQueue(clientData);
}

bool
SrtpTransport_Send(SrtpTransport *transport, RtpPacket *packet) {
    transport->mutex->API->Lock(transport->mutex);
    if (transport->numPacketsQueued > kMaxPacketBacklog) {
        RtpPacket *oldPacket = LTList_GetNodeDataOfType(RtpPacket, transport->packetQueue.pNext);
        LTList_Remove(&oldPacket->node);
        RtpPacketPool_ReleasePacket(oldPacket);
        --transport->numPacketsQueued;
    }
    LTList_AddTail(&transport->packetQueue, &packet->node);
    ++transport->numPacketsQueued;
    bool bResult = transport->thread->API->QueueTaskProcIfRequired(transport->thread, DrainSendQueueProc, NULL, transport);
    transport->mutex->API->Unlock(transport->mutex);
    return bResult;
}

void
SrtpTransport_SetTransportSocket(SrtpTransport *transport, LTSocket hSocket) {
    if (hSocket == transport->hTransportSocket) return;
    transport->mutex->API->Lock(transport->mutex);
    if (transport->hTransportSocket) {
        ILTSocket *iOldSocket = lt_gethandleinterface(ILTSocket, transport->hTransportSocket);
        iOldSocket->NoSocketEvent(transport->hTransportSocket, OnTransportSocketEvent);
    }
    transport->hTransportSocket = hSocket;
    ILTSocket *iNewSocket = lt_gethandleinterface(ILTSocket, hSocket);
    iNewSocket->OnSocketEvent(hSocket, OnTransportSocketEvent, transport);
    transport->mutex->API->Unlock(transport->mutex);
}

void
SrtpTransport_Destroy(SrtpTransport *transport) {
    ILTSocket *pDtlsSocket = lt_gethandleinterface(ILTSocket, transport->hDtlsSocket);
    pDtlsSocket->NoSocketEvent(transport->hDtlsSocket, OnDtlsSocketEvent);
    lt_destroyhandle(transport->hDtlsSocket);

    ILTSocket *pTransportSocket = lt_gethandleinterface(ILTSocket, transport->hTransportSocket);
    pTransportSocket->NoSocketEvent(transport->hTransportSocket, OnTransportSocketEvent);
    lt_destroyhandle(transport->hTransportSocket);

    SrtpCrypto_DestroyAuthContext(transport->authContext);
    transport->authContext = NULL;

    while (!LTList_IsEmpty(&transport->packetQueue)) {
        RtpPacket *packet = LTList_GetNodeDataOfType(RtpPacket, transport->packetQueue.pNext);
        LTList_Remove(&packet->node);
        RtpPacketPool_ReleasePacket(packet);
    }

    lt_destroyobject(transport->mutex);
    lt_free(transport);
}

SrtpTransport *
SrtpTransport_Create(LTSrtpSessionImpl *session, LTSocket hTransportSocket, LTSocket hDtlsSocket) {
    SrtpTransport *transport = lt_malloc(sizeof(SrtpTransport));
    if (!transport) return NULL;
    lt_memset(transport, 0, sizeof(SrtpTransport));

    do {
        transport->session = session;
        transport->thread = LT_GetCore()->GetCurrentThreadObject();
        transport->hDtlsSocket = hDtlsSocket;
        transport->hTransportSocket = hTransportSocket;
        if (!(transport->mutex = lt_createobject(LTMutex))) break;
        transport->numPacketsQueued = 0;
        LTList_Init(&transport->packetQueue);

        ILTSocket *pTransportSocket = lt_gethandleinterface(ILTSocket, hTransportSocket);
        pTransportSocket->OnSocketEvent(hTransportSocket, OnTransportSocketEvent, transport);

        ILTSocket *pDtlsSocket = lt_gethandleinterface(ILTSocket, hDtlsSocket);
        pDtlsSocket->OnSocketEvent(hDtlsSocket, OnDtlsSocketEvent, transport);
    } while(false);

    return transport;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  19-Sep-24   trajan      created
 */
