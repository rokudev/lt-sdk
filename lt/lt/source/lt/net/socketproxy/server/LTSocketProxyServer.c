/*******************************************************************************
 * source/lt/net/socketproxy/server/LTSocketProxyServer.c - LTSocket proxy server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/
#include <lt/LT.h>
#include <lt/net/socketproxy/server/LTSocketProxyServer.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>
#include "../LTSocketProxy.h"

DEFINE_LTLOG_SECTION("sk.prx.srv");

static const LTTime kInactivityTimeout = LTTimeInitializer_Seconds(5);

/*______________________________________________
  LTProxiedSocket object private data members */
typedef_LTObjectImpl(LTProxiedSocket, LTProxiedSocketImpl) {
    LTList_Node node;
    LTSocket    socket;
    u32         id;
    s32         writeStatus;
    LTEvent     hEvent;
    LTAtomic    stopped;
    u8          txPacket[1536];
} LTOBJECT_API;

static LTAtomic          s_NextId;
static LTList            s_Sockets;
static LTDeviceSdioPort *s_SdioPort;
static LTOThread        *s_thread;
static LTOThread        *s_rxThread;
static ILTEvent         *s_event;

#define MIN_BLOCK_SIZE(payloadLen) (((((payloadLen) + 4) + (kLTDeviceSdioPort_BlockSize - 1)) / kLTDeviceSdioPort_BlockSize) * kLTDeviceSdioPort_BlockSize)

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData);

static void OnInactivityTimeout(void *clientData) {
    LTProxiedSocketImpl *prxSocket = clientData;
    s_rxThread->API->KillTimer(s_rxThread->API->GetCurrentThread(), OnInactivityTimeout, clientData);
    s_event->NotifyEvent(prxSocket->hEvent, LTProxiedSocket_Event_NoActivity);
}

static void LTProxiedSocketImpl_Init(LTProxiedSocketImpl *prxSocket, LTSocket socket) {
    prxSocket->socket = socket;
    s_rxThread->API->SetTimer(s_rxThread, kInactivityTimeout, OnInactivityTimeout, NULL, prxSocket);
}

static u32 LTProxiedSocketImpl_GetId(LTProxiedSocketImpl *prxSocket) {
    return prxSocket->id;
}

static void LTProxiedSocketImpl_Close(LTProxiedSocketImpl *prxSocket) {
    LTAtomic_Store(&prxSocket->stopped, 1);
}

static void LTProxiedSocketImpl_SetSocket(LTProxiedSocketImpl *prxSocket, LTSocket hSocket) {
    if (hSocket == prxSocket->socket) return;
    if (prxSocket->socket) {
        ILTSocket *iOldSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
        iOldSocket->NoSocketEvent(prxSocket->socket, OnSocketEvent);
    }
    prxSocket->socket = hSocket;
    ILTSocket *iNewSocket = lt_gethandleinterface(ILTSocket, hSocket);
    iNewSocket->OnSocketEvent(hSocket, OnSocketEvent, prxSocket);
}

static void LTProxiedSocketImpl_OnEvent(LTProxiedSocketImpl *prxSocket, LTProxiedSocket_EventProc * pCallback, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    s_event->RegisterForEvent(prxSocket->hEvent, pCallback, pClientDataReleaseProc, pClientData, false);
}

static void LTProxiedSocketImpl_NoEvent(LTProxiedSocketImpl *prxSocket, LTProxiedSocket_EventProc * pCallback) {
    s_event->UnregisterFromEvent(prxSocket->hEvent, pCallback);
}

static void DispatchSessionEvent(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTProxiedSocket_EventProc *pCallback = (LTProxiedSocket_EventProc *)pEventProc;
    LTProxiedSocket_Event event = LTArgs_u32At(0, pEventArgs);
    pCallback(event, pEventProcClientData);
}

static bool LTProxiedSocketImpl_ConstructObject(LTProxiedSocketImpl *prxSocket) {
    prxSocket->id = LTAtomic_FetchAdd(&s_NextId, 1);
    LTList_AddTail(&s_Sockets, &prxSocket->node);
    static const LTArgsDescriptor argsDescriptor = {1, {kLTArgType_u32}};
    if (!(prxSocket->hEvent = LT_GetCore()->CreateEvent(&argsDescriptor, DispatchSessionEvent, NULL, NULL, NULL))) return false;
    LTAtomic_Store(&prxSocket->stopped, 0);
    return true;
}

static void LTProxiedSocketImpl_DestructObject(LTProxiedSocketImpl *prxSocket) {
    s_rxThread->API->KillTimer(s_rxThread->API->GetCurrentThread(), OnInactivityTimeout, prxSocket);
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
    iSocket->NoSocketEvent(prxSocket->socket, OnSocketEvent);
    lt_destroyhandle(prxSocket->hEvent);
    LTList_Remove(&prxSocket->node);
}

static void OnReadReady(LTProxiedSocketImpl *prxSocket) {
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
    while (true && !LTAtomic_Load(&prxSocket->stopped)) {
        u32 payloadLen = 1;
        s32 available = iSocket->ReadSocket(prxSocket->socket, NULL, 0);
        if (available <= 0) return;
        payloadLen += available;
        prxSocket->txPacket[0] = prxSocket->id;
        prxSocket->txPacket[1] = kLTSocketOp_Event;
        prxSocket->txPacket[2] = payloadLen >> 8;
        prxSocket->txPacket[3] = payloadLen & 0xff;
        prxSocket->txPacket[4] = kLTSocket_Event_ReadReady;
        u8 *payload = prxSocket->txPacket + 5;

        iSocket->ReadSocket(prxSocket->socket, payload, payloadLen - 1);

        if (LTAtomic_Load(&prxSocket->stopped)) break;

        if (!s_SdioPort->API->Write(s_SdioPort, prxSocket->txPacket, MIN_BLOCK_SIZE(payloadLen))) {
            LTLOG_YELLOWALERT("rdready.sdio.write.err", NULL);
        }
    }
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(hSocket);
    LTProxiedSocketImpl *prxSocket = clientData;
    if (!prxSocket || LTAtomic_Load(&prxSocket->stopped)) return;

    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
    u32 payloadLen = 1;
    if (event == kLTSocket_Event_ReadReady) {
        OnReadReady(prxSocket);
    } else if (event == kLTSocket_Event_Connected) {
        u16 profileId;
        if (iSocket->GetProperty(prxSocket->socket, "srtp.protectionprofile", &profileId)) {
            payloadLen += 62;
        }
        prxSocket->txPacket[0] = prxSocket->id;
        prxSocket->txPacket[1] = kLTSocketOp_Event;
        prxSocket->txPacket[2] = payloadLen >> 8;
        prxSocket->txPacket[3] = payloadLen & 0xff;
        prxSocket->txPacket[4] = event;
        u8 *payload = prxSocket->txPacket + 5;
        iSocket->GetProperty(prxSocket->socket, "srtp.protectionprofile", payload);
        iSocket->GetProperty(prxSocket->socket, "srtp.keymaterial", payload + 2);
        if (!s_SdioPort->API->Write(s_SdioPort, prxSocket->txPacket, MIN_BLOCK_SIZE(payloadLen))) {
            LTLOG_YELLOWALERT("ose.sdio.write.err", NULL);
        }
    }
}

static void SendWriteStatus(void *clientData) {
    LTProxiedSocketImpl *prxSocket = clientData;
    if (!prxSocket || LTAtomic_Load(&prxSocket->stopped)) return;

    u32 payloadLen = 4;
    prxSocket->txPacket[0] = prxSocket->id;
    prxSocket->txPacket[1] = kLTSocketOp_WriteStatus;
    prxSocket->txPacket[2] = payloadLen >> 8;
    prxSocket->txPacket[3] = payloadLen & 0xff;
    prxSocket->txPacket[4] = prxSocket->writeStatus >> 24;
    prxSocket->txPacket[5] = prxSocket->writeStatus >> 16;
    prxSocket->txPacket[6] = prxSocket->writeStatus >> 8;
    prxSocket->txPacket[7] = prxSocket->writeStatus & 0xff;
    prxSocket->writeStatus = 0;

    if (!s_SdioPort->API->Write(s_SdioPort, prxSocket->txPacket, MIN_BLOCK_SIZE(payloadLen))) {
        LTLOG_YELLOWALERT("sws.sdio.write.err", NULL);
    }
}

static LTProxiedSocketImpl *ProxiedSocket_FindById(u32 id) {
    LTList_ForEach(pNode, &s_Sockets) {
        LTProxiedSocketImpl *prxSocket = LTList_GetNodeDataOfType(LTProxiedSocketImpl, pNode);
        if (prxSocket->id == id) return prxSocket;
    } LTList_EndForEach;
    return NULL;
}

static void SdioReadCallback(LTDeviceSdioPort *port, u32 recvLen, void *clientData) {
    LT_UNUSED(port); LT_UNUSED(clientData);

    u8 *packet = lt_malloc(recvLen);
    s_SdioPort->API->Read(s_SdioPort, packet);

    u32 id = packet[0];
    LTSocketOp op = packet[1];
    u32 payloadLen = packet[2] << 8 | packet[3];
    u8 *payload = packet + 4;

    LTProxiedSocketImpl *prxSocket = ProxiedSocket_FindById(id);
    if (!prxSocket) {
        LTLOG_YELLOWALERT("psock.nf", "Failed to find proxied socket: id=%lu", LT_Pu32(id));
        lt_free(packet);
        return;
    }
    s_rxThread->API->SetTimer(s_rxThread, kInactivityTimeout, OnInactivityTimeout, NULL, prxSocket);

    if (LTAtomic_Load(&prxSocket->stopped)) {
        lt_free(packet);
        return;
    }

    switch (op) {
        case kSocketOp_RegisterForEvents:
            s_thread->API->QueueTaskProc(s_thread, ({
                void Foo(void *clientData) {
                    LTProxiedSocketImpl *prxSocket = clientData;
                    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
                    iSocket->OnSocketEvent(prxSocket->socket, OnSocketEvent, prxSocket);
                }
                &Foo;
            }), NULL, prxSocket);
            break;

        case kSocketOp_UnregisterFromEvents:
            s_thread->API->QueueTaskProc(s_thread, ({
                void Foo(void *clientData) {
                    LTProxiedSocketImpl *prxSocket = clientData;
                    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
                    iSocket->NoSocketEvent(prxSocket->socket, OnSocketEvent);
                }
                &Foo;
            }), NULL, prxSocket);
            break;

        case kLTSocketOp_Write: {
            ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, prxSocket->socket);
            u8 dscp = payload[0];
            iSocket->SetProperty(prxSocket->socket, "dscp", &dscp);
            s32 ret = iSocket->WriteSocket(prxSocket->socket, payload + 1, payloadLen - 1);
            prxSocket->writeStatus = ret;
            s_thread->API->QueueTaskProc(s_thread, SendWriteStatus, NULL, prxSocket);
            break;
        }

        default:
            LTLOG_YELLOWALERT("pk.unhandled", NULL);
            break;
    }
    lt_free(packet);
}

/*_________________________________________
  LTProxiedSocket LTObjectApi definition */
define_LTObjectImplPublic(LTProxiedSocket, LTProxiedSocketImpl,
    Init,
    GetId,
    Close,
    SetSocket,
    OnEvent,
    NoEvent,
);

static bool Thread_Init(void) {
    if (!(s_SdioPort = lt_createobject(LTDeviceSdioPort))) return false;
    if (!s_SdioPort->API->Bind(s_SdioPort, kLTDeviceSdioPort_Socket, SdioReadCallback, NULL)) return false;
    return true;
}

static void Thread_Exit(void) {
    lt_destroyobject(s_SdioPort);   s_SdioPort = NULL;

    LTList_ForEach(pNode, &s_Sockets) {
        LTProxiedSocketImpl *sock = LTList_GetNodeDataOfType(LTProxiedSocketImpl, pNode);
        lt_destroyobject(sock);
    } LTList_EndForEach;
    LTList_Init(&s_Sockets);
}

static void LTSocketProxyServerImpl_LibFini(void) {
    lt_destroyobject(s_thread);
    lt_destroyobject(s_rxThread);
}

static bool LTSocketProxyServerImpl_LibInit(void) {
    if (!(s_event = lt_getlibraryinterface(ILTEvent, LT_GetCore()))) return false;
    LTList_Init(&s_Sockets);
    if (!(s_thread = lt_createobject(LTOThread))) return false;
    s_thread->API->SetStackSize(s_thread, 1024);
    s_thread->API->Start(s_thread, "sk.prx.srv", NULL, NULL);

    if (!(s_rxThread = lt_createobject(LTOThread))) return false;
    s_rxThread->API->SetStackSize(s_rxThread, 1024);
    s_rxThread->API->Start(s_rxThread, "sk.prx.srv.rx", Thread_Init, Thread_Exit);
    return true;
}

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTSocketProxyServer, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTSocketProxyServer, ) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTSocketProxyServer,
    (LTProxiedSocketImpl)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Oct-24   trajan      created
 */
