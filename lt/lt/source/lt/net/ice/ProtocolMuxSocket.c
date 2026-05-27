/*******************************************************************************
 * source/lt/net/ice/ProtocolMuxSocket.c - UDP socket protocol multiplexer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "ProtocolMuxSocket.h"

DEFINE_LTLOG_SECTION("sockmux");

enum {
    kMaxQueuedPackets          = 10,     // Maximum size of the RX queue before packets start being dropped
    kConsecutiveDroppedPackets = 20,     // Log alert if consecutive packets are dropped
};

typedef struct {
    LTList_Node       node;
    u32               size;
    LTNetIpv4Endpoint endpoint;
    u8                data[];
} RtcPacket;

typedef struct LTRtcProtocolSocket {
    ProtocolMuxSocket *mux;
    LTSocket           hHandle;
    LTRtcProtocol      protocol;
    LTEvent            event;
    LTList             rxQueue;
    LTMutex           *listMutex;
    LTNetIpv4Endpoint  sendEndpoint;
    LTNetIpv4Endpoint  receiveEndpoint;
    LTAtomic           readPending;
    u8                 dscp;
    u8                 nDroppedPacketCount;
    u32                nPacketsQueued;
} LTRtcProtocolSocket;

struct ProtocolMuxSocket {
    LTSocket          hBaseSocket;
    LTNetIpv4Endpoint defaultEndpoint;
    LTMutex          *socketMutex;
    LTSocket          sockets[kLTRtcProtocol_Count];
};

static LTNetCore *s_netCore;
static ILTEvent  *s_event;
static ILTThread *s_thread;
static ILTSocket s_ILTSocket;

static const LTArgsDescriptor s_SocketEventArgs = {2, { kLTArgType_lthandle, kLTArgType_u32 }}; // socket, event

static void
DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc || !args) return;
    LTSocket hSocket = LTArgs_lthandleAt(0, args);
    u32 sockEvent    = LTArgs_u32At(1, args);
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return;

    if (sockEvent == kLTSocket_Event_ReadReady) {
        LTAtomic_Store(&psock->readPending, 0);
        psock->listMutex->API->Lock(psock->listMutex);
        bool isEmpty = LTList_IsEmpty(&psock->rxQueue);
        psock->listMutex->API->Unlock(psock->listMutex);
        if (isEmpty) {
            LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
            return;
        }
    }

    (*(LTSocket_EventProc *)proc)(hSocket, sockEvent, data);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
}

static void
DispatchSocketEventImmediate(LTEvent hEvent, void *notifyImmediateEventStateClientData, void *eventProc, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTRtcProtocolSocket *psock = notifyImmediateEventStateClientData;
    LTSocket_EventProc *callback = eventProc;
    psock->listMutex->API->Lock(psock->listMutex);
    bool isEmpty = LTList_IsEmpty(&psock->rxQueue);
    psock->listMutex->API->Unlock(psock->listMutex);
    if (!isEmpty) {
        LTAtomic_Store(&psock->readPending, 0);
        callback(psock->hHandle, kLTSocket_Event_ReadReady, eventProcClientData);
    }
    callback(psock->hHandle, kLTSocket_Event_WriteReady, eventProcClientData);
}

static RtcPacket*
PeekQueue(LTRtcProtocolSocket *psock) {
    psock->listMutex->API->Lock(psock->listMutex);
    if (LTList_IsEmpty(&psock->rxQueue)) {
        psock->listMutex->API->Unlock(psock->listMutex);
        return NULL;
    }
    RtcPacket *packet = LT_CONTAINER_OF(psock->rxQueue.pNext, RtcPacket, node);
    psock->receiveEndpoint = packet->endpoint;
    psock->listMutex->API->Unlock(psock->listMutex);
    return packet;
}

static RtcPacket*
DequeuePacket(LTRtcProtocolSocket *psock) {
    psock->listMutex->API->Lock(psock->listMutex);
    if (LTList_IsEmpty(&psock->rxQueue)) {
        psock->listMutex->API->Unlock(psock->listMutex);
        return NULL;
    }
    RtcPacket *packet = LT_CONTAINER_OF(psock->rxQueue.pNext, RtcPacket, node);
    psock->receiveEndpoint = packet->endpoint;
    LTList_Remove(&packet->node);
    psock->nPacketsQueued--;
    psock->listMutex->API->Unlock(psock->listMutex);
    return packet;
}

static u32
EnqueuePacket(LTRtcProtocolSocket *psock, RtcPacket *packet) {
    u32 packetsDropped = 0;
    psock->listMutex->API->Lock(psock->listMutex);
    while (psock->nPacketsQueued >= kMaxQueuedPackets) {
        // Drop the oldest packet to make room for new packet
        RtcPacket *dropPacket = DequeuePacket(psock);
        if (!dropPacket) break;
        lt_free(dropPacket);
        ++packetsDropped;
    }
    LTList_AddTail(&psock->rxQueue, &packet->node);
    psock->nPacketsQueued++;
    psock->listMutex->API->Unlock(psock->listMutex);
    return packetsDropped;
}

static void
AddPacketToQueue(LTRtcProtocolSocket *psock, RtcPacket *packet) {
    u32 packetsDropped = EnqueuePacket(psock, packet);
    if (packetsDropped) {
        // Alert and reset every kConsecutiveDroppedPackets dropped packets
        psock->nDroppedPacketCount += packetsDropped;
        if (psock->nDroppedPacketCount >= kConsecutiveDroppedPackets) {
            LTLOG_YELLOWALERT("queue.max", "Dropped %u consecutive packets of type %d", psock->nDroppedPacketCount, psock->protocol);
            psock->nDroppedPacketCount = 0;
        }
    } else {
        psock->nDroppedPacketCount = 0;
    }
}

static void
OnReadReady(void *clientData) {
    ProtocolMuxSocket *mux = clientData;

    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, mux->hBaseSocket);
    // keep reading until no more packets are available
    while (true) {
        u32 nPacketLen = pSocket->ReadSocket(mux->hBaseSocket, NULL, 0);
        if (nPacketLen == 0) return;
        RtcPacket *packet = lt_malloc(sizeof(RtcPacket) + nPacketLen);
        if (!packet) {
            LTLOG_YELLOWALERT("pkt.alloc.err", "Error allocating RX packet data");
            return;
        }
        s32 bytesRead = pSocket->ReadSocket(mux->hBaseSocket, packet->data, nPacketLen);
        if (bytesRead < 0) {
            LTLOG_YELLOWALERT("read.err", "Socket read failed: %ld", LT_Ps32(bytesRead));
            return;
        }
        packet->size = bytesRead;
        if (!pSocket->GetProperty(mux->hBaseSocket, "recv.endpoint.v4", &packet->endpoint)) {
            LTLOG_YELLOWALERT("get.rcvr.err", "Socket get receive endpoint failed");
            return;
        }

        /* De-multiplex STUN/TURN/SRTP/DTLS per RFC 7983 section 7 */
        u8 nByte0 = packet->data[0];
        LTRtcProtocol protocol;
        // 0-3 -> STUN, 64-79 -> TURN channel
        // TURN channel messages are routed to STUN since it owns the socket.
        // Non-STUN messages are bubbled up from there to TURN via an event.
        if ((nByte0 <= 3) || ((nByte0 >= 64) && (nByte0 <= 79))) {
            protocol = kLTRtcProtocol_STUN;
        } else if ((nByte0 >= 128) && (nByte0 <= 191)) {
            protocol = kLTRtcProtocol_SRTP;
        } else if ((nByte0 >= 20) && (nByte0 <= 63)) {
            protocol = kLTRtcProtocol_DTLS;
        } else {
            LTLOG_YELLOWALERT("badproto", "Unknown RTC protocol id byte: %lx", LT_Pu32(nByte0));
            lt_free(packet);
            return;
        }

        LTSocket hProtocolSocket = mux->sockets[protocol];
        LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hProtocolSocket);
        AddPacketToQueue(psock, packet);
        if (LTAtomic_CompareAndExchange(&psock->readPending, 0, 1)) {
            s_event->NotifyEvent(psock->event, psock->hHandle, kLTSocket_Event_ReadReady);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hProtocolSocket, psock);
    }
}

static void
OnSocketEvent(LTSocket socket, LTSocket_Event event, void *pClientData) {
    LT_UNUSED(socket);
    if (!pClientData) return;
    ProtocolMuxSocket *mux = pClientData;
    switch (event) {
        case kLTSocket_Event_SocketReady:
        case kLTSocket_Event_WriteReady: break;
        case kLTSocket_Event_ReadReady:
            OnReadReady(mux);
            break;
        default:
            LTLOG("ice.sock.evt", "Unhandled socket event: %lx", LT_Pu32(event));
            break;
    }
}

static LTSocket
CreateProtocolSocket(ProtocolMuxSocket *mux, LTRtcProtocol protocol) {
    LTSocket hSocket = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTSocket, sizeof(LTRtcProtocolSocket));
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return LTHANDLE_INVALID;

    lt_memset(psock, 0, sizeof(LTRtcProtocolSocket));

    psock->hHandle              = hSocket;
    psock->mux                  = mux;
    psock->protocol             = protocol;
    psock->sendEndpoint         = LTNetIpv4Endpoint_Any();
    psock->listMutex            = lt_createobject(LTMutex);
    psock->event                = LT_GetCore()->CreateEvent(&s_SocketEventArgs, DispatchSocketEvent, NULL, DispatchSocketEventImmediate, psock);
    psock->nDroppedPacketCount  = 0;
    LTList_Init(&psock->rxQueue);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
    return hSocket;
}

ProtocolMuxSocket *
ProtocolMuxSocket_Create(LTSocket hSocket, LTNetIpv4Endpoint defaultEndpoint) {
    ProtocolMuxSocket *mux;
    do {
        mux = lt_malloc(sizeof(ProtocolMuxSocket));
        if (!mux) break;
        lt_memset(mux, 0, sizeof(*mux));

        mux->defaultEndpoint = defaultEndpoint;
        mux->socketMutex = lt_createobject(LTMutex);
        if (!mux->socketMutex) break;

        mux->hBaseSocket = hSocket;
        ILTSocket *socket = lt_gethandleinterface(ILTSocket, hSocket);
        if (!socket) break;

        socket->OnSocketEvent(mux->hBaseSocket, OnSocketEvent, mux);

        for (u32 i = 0; i < kLTRtcProtocol_Count; ++i) {
            mux->sockets[i] = CreateProtocolSocket(mux, i);
            if (mux->sockets[i] == LTHANDLE_INVALID) break;
        }
        return mux;
    } while(false);

    LTLOG_YELLOWALERT("pmux.create.err", "ProtocolMux creation failed");
    ProtocolMuxSocket_Destroy(mux);
    return NULL;
}

void
ProtocolMuxSocket_Destroy(ProtocolMuxSocket *mux) {
    if (!mux) return;
    for (u32 i = 0; i < kLTRtcProtocol_Count; ++i) {
        lt_destroyhandle(mux->sockets[i]);
    }
    lt_destroyhandle(mux->hBaseSocket);
    lt_destroyobject(mux->socketMutex);
    mux->socketMutex = NULL;
    lt_free(mux);
}

LTSocket
ProtocolMuxSocket_GetProtocolSocket(ProtocolMuxSocket *mux, LTRtcProtocol protocol) {
    return mux ? mux->sockets[protocol] : 0;
}

void
ProtocolMuxSocket_SetDefaultEndpoint(ProtocolMuxSocket *mux, LTNetIpv4Endpoint endpoint) {
    mux->defaultEndpoint = endpoint;
}

void
ProtocolMuxSocket_LibFini(void) {
    lt_closelibrary(s_netCore);     s_netCore = NULL;
}

bool
ProtocolMuxSocket_LibInit(void) {
    s_netCore = lt_openlibrary(LTNetCore);
    s_event   = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    s_thread  = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return true;
}

/*******************************************************************************
** LTSocket API
*******************************************************************************/

static void LTRtcProtocolSocket_DestroySocket(LTHandle hSocket) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return;
    lt_destroyhandle(psock->event);
    lt_destroyhandle(psock->hHandle);
    lt_destroyobject(psock->listMutex);
    while (!LTList_IsEmpty(&psock->rxQueue)) {
        RtcPacket *data = LT_CONTAINER_OF(psock->rxQueue.pNext, RtcPacket, node);
        LTList_Remove(&data->node);
        lt_free(data);
    }
    psock->nPacketsQueued = 0;
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
}

static void LTRtcProtocolSocket_OnSocketEvent(LTSocket hSocket, LTSocket_EventProc proc, void *procData) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return;
    s_event->RegisterForEvent(psock->event, proc, NULL, procData, true);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
}

static void LTRtcProtocolSocket_NoSocketEvent(LTSocket hSocket, LTSocket_EventProc proc) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return;
    s_event->UnregisterFromEvent(psock->event, proc);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
}

static void LTRtcProtocolSocket_GetSocketSpec(LTSocket hSocket, char *spec, int specSize) {
    if (!spec || !specSize) return;
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, psock->mux->hBaseSocket);
    pSocket->GetSocketSpec(psock->mux->hBaseSocket, spec, specSize);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
}

static bool LTRtcProtocolSocket_GetProperty(LTSocket hSocket, const char *name, void *value) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return false;
    bool result;
    if (lt_strcmp(name, "recv.endpoint.v4") == 0) {
        *(LTNetIpv4Endpoint *)value = psock->receiveEndpoint;
        result = true;
    } else {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, psock->mux->hBaseSocket);
        result = pSocket->GetProperty(psock->mux->hBaseSocket, name, value);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
    return result;
}

static bool LTRtcProtocolSocket_SetProperty(LTSocket hSocket, const char *name, const void *value) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return false;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, psock->mux->hBaseSocket);
    bool result;
    if (lt_strcmp(name, "send.endpoint.v4") == 0) {
        psock->sendEndpoint = *(LTNetIpv4Endpoint *)value;
        result = true;
    } else if (lt_strcmp(name, "dscp") == 0) {
        psock->dscp = *(u8 *)value;
        result = true;
    } else {
        result = pSocket->SetProperty(psock->mux->hBaseSocket, name, value);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
    return result;
}

static void LTRtcProtocolSocket_ConnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
}

static void LTRtcProtocolSocket_DisconnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
}

static s32 LTRtcProtocolSocket_WriteSocket(LTSocket hSocket, const void *data, u32 dataLen) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return 0;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, psock->mux->hBaseSocket);

    LTNetIpv4Endpoint endpoint = LTNetIpv4Endpoint_IsAny(psock->sendEndpoint) ? psock->mux->defaultEndpoint : psock->sendEndpoint;

    psock->mux->socketMutex->API->Lock(psock->mux->socketMutex);
    pSocket->SetProperty(psock->mux->hBaseSocket, "send.endpoint.v4", &endpoint);
    pSocket->SetProperty(psock->mux->hBaseSocket, "dscp",             &psock->dscp);
    s32 result = pSocket->WriteSocket(psock->mux->hBaseSocket, data, dataLen);
    psock->mux->socketMutex->API->Unlock(psock->mux->socketMutex);

    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
    return result;
}

static s32 LTRtcProtocolSocket_ReadSocket(LTSocket hSocket, void *data, u32 dataSize) {
    LTRtcProtocolSocket *psock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!psock) return 0;
    u32 packetSize = 0;
    RtcPacket *packet = NULL;
    if (!data && !dataSize) {
        packet = PeekQueue(psock);
        if (!packet) {
            LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
            return 0;
        }
        packetSize = packet->size;
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
        return packetSize;
    }
    if (data && dataSize) {
        packet = DequeuePacket(psock);
        if (!packet) {
            LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
            return 0;
        }
        packetSize = packet->size;
        if (dataSize < packetSize) {
            packetSize = dataSize;
        }
        lt_memcpy(data, packet->data, packetSize);
        lt_free(packet);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, psock);
    return packetSize;
}

define_LTLIBRARY_INTERFACE(ILTSocket, LTRtcProtocolSocket_DestroySocket)
    .OnSocketEvent    = LTRtcProtocolSocket_OnSocketEvent,
    .NoSocketEvent    = LTRtcProtocolSocket_NoSocketEvent,
    .GetSocketSpec    = LTRtcProtocolSocket_GetSocketSpec,
    .GetProperty      = LTRtcProtocolSocket_GetProperty,
    .SetProperty      = LTRtcProtocolSocket_SetProperty,
    .ConnectSocket    = LTRtcProtocolSocket_ConnectSocket,
    .DisconnectSocket = LTRtcProtocolSocket_DisconnectSocket,
    .WriteSocket      = LTRtcProtocolSocket_WriteSocket,
    .ReadSocket       = LTRtcProtocolSocket_ReadSocket,
};

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Jul-23   trajan      created
 */
