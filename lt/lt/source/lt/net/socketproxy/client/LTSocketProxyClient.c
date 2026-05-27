/*******************************************************************************
 * source/lt/net/socketproxy/client/LTSocketProxyClient.c - LTSocket proxy client
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTMonitor.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include <lt/net/socketproxy/client/LTSocketProxyClient.h>
#include "../LTSocketProxy.h"

DEFINE_LTLOG_SECTION("sk.prx.clt");

typedef struct {
    LTList_Node node;
    u32 size;
    u8 data[];
} ReceivedPacket;

typedef struct {
    LTList_Node node;
    u32         id;
    LTSocket    hSocket;
    LTEvent     hEvent;
    LTList      rxQueue;
    LTAtomic    readPending;
    LTMonitor  *monitor;
    u8          txPacket[1536];
    u16         srtpProtectionProfile;
    u8         *srtpKeyMaterial;
    u8          dscp;
    s32         writeStatus;
} SocketProxyData;

static LTDeviceSdioPort *s_SdioPort;
static ILTEvent         *s_iEvent;
static const ILTSocket   s_ILTSocket;
static LTList            s_Sockets;
static LTOThread        *s_thread;
static LTOThread        *s_rxThread;

#define MIN_BLOCK_SIZE(payloadLen) (((((payloadLen) + 4) + (kLTDeviceSdioPort_BlockSize - 1)) / kLTDeviceSdioPort_BlockSize) * kLTDeviceSdioPort_BlockSize)

/*____________________________________________
  SocketProxy LTSocket public api functions */
static void
SocketProxy_DestroySocket(LTHandle hSocket) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return;
    LTList_Remove(&sock->node);
    if (sock->hEvent) lt_destroyhandle(sock->hEvent);
    while (!LTList_IsEmpty(&sock->rxQueue)) {
        ReceivedPacket *packet = LT_CONTAINER_OF(sock->rxQueue.pNext, ReceivedPacket, node);
        LTList_Remove(&packet->node);
        lt_free(packet);
    }
    lt_destroyobject(sock->monitor);
    sock->monitor = NULL;
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
}

static void
SocketProxy_OnSocketEvent(LTSocket hSocket, LTSocket_EventProc proc, void *procData) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return;
    s_iEvent->RegisterForEvent(sock->hEvent, proc, NULL, procData, false);

    sock->txPacket[0] = sock->id;
    sock->txPacket[1] = kSocketOp_RegisterForEvents;
    sock->txPacket[2] = 0;
    sock->txPacket[3] = 0;

    if (!s_SdioPort->API->Write(s_SdioPort, sock->txPacket, MIN_BLOCK_SIZE(0))) {
        LTLOG_YELLOWALERT("ose.write.err", NULL);
    }

    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
}

static void
SocketProxy_NoSocketEvent(LTSocket hSocket, LTSocket_EventProc proc) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return;
    s_iEvent->UnregisterFromEvent(sock->hEvent, proc);

    sock->txPacket[0] = sock->id;
    sock->txPacket[1] = kSocketOp_UnregisterFromEvents;
    sock->txPacket[2] = 0;
    sock->txPacket[3] = 0;

    if (!s_SdioPort->API->Write(s_SdioPort, sock->txPacket, MIN_BLOCK_SIZE(0))) {
        LTLOG_YELLOWALERT("nse.write.err", NULL);
    }

    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
}

static void
SocketProxy_GetSocketSpec(LTSocket hSocket, char *spec, int specSize) {
    LT_UNUSED(hSocket);
    LT_UNUSED(spec);
    LT_UNUSED(specSize);
}

static bool
SocketProxy_GetProperty(LTSocket hSocket, const char *name, void *value) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return false;

    if (lt_strcmp(name, "srtp.protectionprofile") == 0) {
        lt_memcpy(value, &sock->srtpProtectionProfile, sizeof(u16));
    } else if (lt_strcmp(name, "srtp.keymaterial") == 0) {
        lt_memcpy(value, sock->srtpKeyMaterial, 60);
    } else {
        LTLOG_YELLOWALERT("getprop.notimp", name);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
    return true;
}

static bool
SocketProxy_SetProperty(LTSocket hSocket, const char *name, const void *value) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return false;

    if (lt_strcmp(name, "dscp") == 0) {
        sock->dscp = *(u8 *)value;
    } else if (lt_strcmp(name, "metrics.reset") == 0) {
        // ToDo
    } else if (lt_strcmp(name, "metrics.show") == 0) {
        // ToDo
    } else {
        LTLOG_YELLOWALERT("setprop.notimp", name);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
    return true;
}

static void
SocketProxy_ConnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
    LTLOG_YELLOWALERT("conn.notimp", NULL);
}

static void
SocketProxy_DisconnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
    LTLOG_YELLOWALERT("disconn.notimp", NULL);
}

static s32
SocketProxy_WriteSocket(LTSocket hSocket, const void *data, u32 dataLen) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return 0;
    u32 payloadLen = dataLen + 1;

    if (payloadLen + 4 > sizeof(sock->txPacket)) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
        LTLOG_YELLOWALERT("sk.wr.ovf", NULL);
        return -1;
    }

    sock->txPacket[0] = sock->id;
    sock->txPacket[1] = kLTSocketOp_Write;
    sock->txPacket[2] = payloadLen >> 8;
    sock->txPacket[3] = payloadLen & 0xff;
    sock->txPacket[4] = sock->dscp;
    lt_memcpy(sock->txPacket + 5, data, dataLen);
    sock->monitor->API->Enter(sock->monitor);
    s32 ret = 0;
    bool bRet;
    if (!(bRet = s_SdioPort->API->Write(s_SdioPort, sock->txPacket, MIN_BLOCK_SIZE(payloadLen)))) {
        LTLOG_YELLOWALERT("ws.write.err", NULL);
    }
    if (bRet && !sock->monitor->API->Wait(sock->monitor, LTTime_Milliseconds(200))) {
        LTLOG_YELLOWALERT("ws.nm.tout", "socket write timeout");
        ret = 0;
    } else {
        if (bRet) ret = sock->writeStatus;
        else      ret = 0;
        sock->writeStatus = 0;
    }
    sock->monitor->API->Exit(sock->monitor);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
    return ret;
}

static s32
SocketProxy_ReadSocket(LTSocket hSocket, void *data, u32 dataSize) {
    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return 0;
    if (LTList_IsEmpty(&sock->rxQueue)) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
        return 0;
    }
    ReceivedPacket *packet = LT_CONTAINER_OF(sock->rxQueue.pNext, ReceivedPacket, node);
    u32 packetLen = packet->size;
    if (!data && !dataSize) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
        return packetLen;
    }
    if (data && dataSize) {
        if (dataSize < packetLen) {
            packetLen = dataSize;
        }
        LTList_Remove(&packet->node);
        lt_memcpy(data, packet->data, packetLen);
        lt_free(packet);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
    return packetLen;
}

define_LTLIBRARY_INTERFACE(ILTSocket, SocketProxy_DestroySocket)
    .OnSocketEvent    = SocketProxy_OnSocketEvent,
    .NoSocketEvent    = SocketProxy_NoSocketEvent,
    .GetSocketSpec    = SocketProxy_GetSocketSpec,
    .GetProperty      = SocketProxy_GetProperty,
    .SetProperty      = SocketProxy_SetProperty,
    .ConnectSocket    = SocketProxy_ConnectSocket,
    .DisconnectSocket = SocketProxy_DisconnectSocket,
    .WriteSocket      = SocketProxy_WriteSocket,
    .ReadSocket       = SocketProxy_ReadSocket,
};

/*______________________________________________
 / LTSocketProxyClient library implementation */

SocketProxyData *SocketProxy_FindById(u32 id) {
    LTList_ForEach(pNode, &s_Sockets) {
        SocketProxyData *sock = LTList_GetNodeDataOfType(SocketProxyData, pNode);
        if (sock->id == id) return sock;
    } LTList_EndForEach;
    return NULL;
}

static void
SdioReadCallback(LTDeviceSdioPort *port, u32 recvLen, void *clientData) {
    LT_UNUSED(port); LT_UNUSED(clientData);

    u8 *packet = lt_malloc(recvLen);
    if (!packet) {
        LTLOG_YELLOWALERT("sdio.rd.oom", NULL);
        return;
    }
    s_SdioPort->API->Read(s_SdioPort, packet);

    u32 id = packet[0];
    LTSocketOp op = packet[1];
    u32 payloadLen = (packet[2] << 8 | packet[3]);
    u8 *payload = packet + 4;

    SocketProxyData *sock = SocketProxy_FindById(id);
    if (!sock) {
        LTLOG_YELLOWALERT("psock.nf", "Failed to find socket proxy: id=%lu", LT_Pu32(id));
        goto done;
    }
    switch (op) {
        case kLTSocketOp_Event: {
            LTSocket_Event event = payload[0];
            if (event == kLTSocket_Event_ReadReady) {
                u32 packetLen = payloadLen - 1;
                u8 *packetData = payload + 1;

                ReceivedPacket *packet = lt_malloc(sizeof(ReceivedPacket) + packetLen);
                packet->size = packetLen;
                lt_memcpy(packet->data, packetData, packetLen);
                LTList_AddTail(&sock->rxQueue, &packet->node);
                if (LTAtomic_CompareAndExchange(&sock->readPending, 0, 1)) {
                    s_iEvent->NotifyEvent(sock->hEvent, sock->hSocket, event);
                }
            } else if ((event == kLTSocket_Event_Connected) && (payloadLen > 1)) {
                lt_memcpy(&sock->srtpProtectionProfile, payload + 1, sizeof(u16));
                sock->srtpKeyMaterial = lt_malloc(60);
                lt_memcpy(sock->srtpKeyMaterial, payload + 3, 60);
                s_iEvent->NotifyEvent(sock->hEvent, sock->hSocket, event);
            }
            break;
        }
        case kLTSocketOp_WriteStatus: {
            sock->writeStatus = (packet[4] << 24 | packet[5] << 16 | packet[6] << 8 | packet[7]);
            sock->monitor->API->Enter(sock->monitor);
            sock->monitor->API->Notify(sock->monitor);
            sock->monitor->API->Exit(sock->monitor);
            break;
        }
        default:
            LTLOG_YELLOWALERT("ev.unhandled", "Unhandled socket op: %lu", LT_Pu32(op));
    }
done:
    lt_free(packet);
}

static void
DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *clientData) {
    LT_UNUSED(event);
    if (!proc || !args) return;
    LTSocket hSocket = LTArgs_lthandleAt(0, args);
    u32 sockEvent    = LTArgs_u32At(1, args);

    SocketProxyData *sock = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sock) return;
    if (sockEvent == kLTSocket_Event_ReadReady) {
        LTAtomic_Store(&sock->readPending, 0);
    }

    (*(LTSocket_EventProc *)proc)(hSocket, sockEvent, clientData);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sock);
}

static LTSocket
LTSocketProxyClientImpl_GetSocket(u32 id) {
    LTSocket hSocket = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTSocket, sizeof(SocketProxyData));
    SocketProxyData *sockData = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!sockData) return LTHANDLE_INVALID;
    do {
        sockData->id = id;
        sockData->hSocket = hSocket;
        if (!(sockData->monitor = lt_createobject(LTMonitor))) break;
        static const LTArgsDescriptor eventArgs = { .nNumArgs = 2, .argTypes = { kLTArgType_lthandle, kLTArgType_u32 } };
        sockData->hEvent = LT_GetCore()->CreateEvent(&eventArgs, DispatchSocketEvent, NULL, NULL, NULL);
        LTList_Init(&sockData->rxQueue);
        LTList_AddTail(&s_Sockets, &sockData->node);
        LTAtomic_Store(&sockData->readPending, 0);
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, sockData);
        return hSocket;
    } while(false);

    LTLOG_YELLOWALERT("get.sock", "failed to get socket");
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, sockData);
    return LTHANDLE_INVALID;
}

static bool
Thread_Init(void) {
    if (!(s_SdioPort = lt_createobject(LTDeviceSdioPort))) return false;
    if (!s_SdioPort->API->Bind(s_SdioPort, kLTDeviceSdioPort_Socket, SdioReadCallback, NULL)) return false;
    return true;
}

static void
Thread_Exit(void) {
    lt_destroyobject(s_SdioPort);   s_SdioPort = NULL;
}

static void
LTSocketProxyClientImpl_LibFini(void) {
    lt_destroyobject(s_thread);
    lt_destroyobject(s_rxThread);

    LTList_ForEach(pNode, &s_Sockets) {
        SocketProxyData *sock = LTList_GetNodeDataOfType(SocketProxyData, pNode);
        lt_destroyobject(sock);
    } LTList_EndForEach;
    LTList_Init(&s_Sockets);
}

static bool
LTSocketProxyClientImpl_LibInit(void) {
    LTList_Init(&s_Sockets);
    s_iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    if (!(s_thread = lt_createobject(LTOThread))) return false;
    s_thread->API->Start(s_thread, "sk.prx.clt", NULL, NULL);
    if (!(s_rxThread = lt_createobject(LTOThread))) return false;
    s_rxThread->API->Start(s_rxThread, "sk.prx.clt.rx", Thread_Init, Thread_Exit);
    return true;
}

/*  _________________________
    Library Root Interface */
define_LTLIBRARY_ROOT_INTERFACE(LTSocketProxyClient, ) {
    .GetSocket = LTSocketProxyClientImpl_GetSocket,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTSocketProxyClient, (ILTSocket));

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Oct-24   trajan      created
 */
