/*******************************************************************************
 *
 * LTNetCore Example: Regression Test Peer
 *
 * Before running RegressTest, this program must be started on the peer device.
 *
 * See ExampleNetRegressTest for instructions.
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>

static LTCore      * pCore;
static LTNetCore   * pNetCore;
static ILTThread   * iThread;
static LTThread      MainThread;
static const char  * SockSpec = "udp port: 6789";
static LTTransport   hTransport;
static LTSocket      hSocket;
static char          Buffer[64];
static char          Spec[64];

#define P LT_GetCore()->ConsoleStomp

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *data) {
    LT_UNUSED(data);
    //P("=== Event! socket: H%04x event: 0x%x\n", socket, event);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_SocketReady:
            P("Socket ready\n");
            break;
        case kLTSocket_Event_ReadReady:;
            pSocket->GetSocketSpec(socket, Spec, 62);
            s32 len = pSocket->ReadSocket(socket, Buffer, 9);
            if (len < 0) break;
            Buffer[len] = 0;
            if (lt_strncmp(Buffer, "ping", 4)) {
                pSocket->ReadSocket(socket, Buffer+4, 58); // drain residual
                P("BAD DATA: len: %ld \"%s\"\n", LT_Ps32(len), Buffer);
            } else P("Got: \"%s\" (%s)\n", Buffer, Spec);
            pSocket->WriteSocket(socket, "pong", 4);
            break;
        default: break;
    }
}

static void OnTransportEvent(LTTransport transport, LTTransport_Event event, void *data) {
    LT_UNUSED(data);
    if (event == kLTTransport_Event_Up && !hSocket) {
        hSocket = pNetCore->OpenSocket(transport, SockSpec, OnSocketEvent, 0);
    }
}

static bool OnThreadStart(void) {
    pNetCore = lt_openlibrary(LTNetCore);
    if (pNetCore) {
        hTransport = pNetCore->OpenTransport(NULL, OnTransportEvent, NULL);
        if (hTransport) return true;
    }
    P("Failed to start\n");
    return false;
}

static void OnThreadExit(void) {
    pCore->DestroyHandle(hTransport);
    lt_closelibrary(pNetCore);
}

static int ExampleNetRegressPeer_Main(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    pCore = LT_GetCore();
    MainThread = pCore->CreateThread("RegressPeer");
    if (!MainThread) return -1;
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iThread->SetStackSize(MainThread, 1024);
    iThread->Start(MainThread, OnThreadStart, OnThreadExit);
    iThread->WaitUntilFinished(MainThread, LTTime_Infinite());
    return 0;
}
define_LTLIBRARY_APPLICATION(ExampleNetRegressPeer, 1, 2048);
