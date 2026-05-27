/*******************************************************************************
 *
 * TestNetCoreSocketDeplete - Run out of sockets, then free them, then try a transfer
 *
 * NOTE: This example reads from the WiFi router web page. If your router doesn't have
 * a web server (or the HTTP request otherwise fails), then comment out the
 * USE_ROUTER_WEB_SERVER define to read from example.com.
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>

#define USE_ROUTER_WEB_SERVER   // WiFi routers often have web servers, otherwise use example.com

#define MAX_SOCKETS       10
#define TEST_TIMEOUT      60
#define TEST_LOOPS        10    // how many times to repeat test sequence

#define MAX_SOCKET_SPEC   80    // length of socket spec
#define MAX_HTTP_REQUEST  80    // length of HTTP request
#define MAX_HTTP_REPLY    2048  // lenfth of HTTP reply (per read)

#define P LT_GetCore()->ConsolePrint

#define MAKE_CONNECTION   true  // Set false to only create and destroy sockets, no connections

static struct Statics { // gets cleared in main()
    LTCore       *core;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTNetMonitor *iNetMonitor;
    LTSocket      sockets[MAX_SOCKETS];
    u16           socketCount;
    char         *sockSpec;
    char         *request;
    char         *reply;
    bool          requestSent;
    LT_SIZE       memoryUse;
} S;

static void NullSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    P("\n=== sock event: socket H%04lx event 0x%x\n", LT_Pu32(socket), event);
    LT_UNUSED(clientData);
}

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(clientData);
    //P("=== socket[%04lx] event 0x%x\n", LT_Pu32(socket), event);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (S.requestSent) break;
            P("Posting HTTP request...\n");
            S.requestSent = true;
            pSocket->WriteSocket(socket, S.request, lt_strlen(S.request));
            break;
        case kLTSocket_Event_ReadReady:;
            s32 len = 0;
            do {
                len = pSocket->ReadSocket(socket, S.reply, MAX_HTTP_REPLY - 1);
                S.reply[len] = 0;
                LT_GetCore()->ConsoleStompString(S.reply);
            } while (len > 0);
            break;
        case kLTSocket_Event_Disconnected:
            P("Socket disconnected\n");
            pSocket->DisconnectSocket(socket);
            S.iThread->Sleep(LTTime_Milliseconds(500));
            S.core->DestroyHandle(socket);
            P("=== Memory lost: %d\n", S.memoryUse - S.core->GetAvailableSystemRAM());
            S.iThread->Terminate(S.iThread->GetCurrentThread());
            break;
        default:
            break;
    }
}

static void DoTest(void) {
    S.socketCount = 0;

    // Open sockets until we can't:
    for (u16 n = 0; n < MAX_SOCKETS; n++) {
        LTSocket sock = S.iNetCore->OpenSocket(0, "tcp port: 8000 ip: 1.2.3.4 no-connect", NullSocketEvent, NULL);
        if (!sock) break;
        S.sockets[S.socketCount++] = sock;
        P("Created socket %lx (memory %d)\n", LT_Pu32(sock), S.memoryUse - S.core->GetAvailableSystemRAM());
    }
    P("=== Able to open %d sockets\n", S.socketCount);

    // Destroy all sockets (purposely not in reverse order):
    for (u16 n = 0; n < S.socketCount; n++) {
        S.core->DestroyHandle(S.sockets[n]);
        P("Destroyed socket %lx (memory %d)\n", LT_Pu32(S.sockets[n]), S.memoryUse - S.core->GetAvailableSystemRAM());
    }
    P("=== Memory lost: %d\n==========\n", S.memoryUse - S.core->GetAvailableSystemRAM());

}

static void ConfigLocalWebIP(void) {
#ifdef USE_ROUTER_WEB_SERVER
    char spec[64];
    S.iNetCore->GetTransportSpec(0, spec, sizeof(spec));
    char *b = lt_strstr(spec, "gw:");
    char *e = lt_strstr(spec, "mask:");
    if (!b || !e) return;
    *(e-1) = 0;
    lt_snprintf(S.sockSpec, MAX_SOCKET_SPEC, "tcp ip: %s port: 80", b+4); // the wifi router
    P("socket spec: %s\n", S.sockSpec);
    lt_snprintf(S.request,  MAX_HTTP_REQUEST, "GET /nopage HTTP/1.1\r\nConnection: close\r\n\r\n"); // 404 intended
#endif
}

static void StatusChange(LTNetMonitor_Status status, void *clientData) {
    LT_UNUSED(clientData);
    if (status == kLTNetMonitor_Status_NetworkUp) {
        ConfigLocalWebIP();
        S.memoryUse = S.core->GetAvailableSystemRAM();
        for (u16 n = 0; n < TEST_LOOPS; n++) {
            DoTest();
        }
        // Now, open another, and try a transfer:
        S.requestSent = false;
        if (MAKE_CONNECTION) {
            S.sockets[S.socketCount] = S.iNetCore->OpenSocket(0, S.sockSpec, OnSocketEvent, NULL);
        } else {
            S.iThread->Terminate(S.iThread->GetCurrentThread());
        }
    }
}

static bool OnThreadStart(void) {
    if (!(S.iNetMonitor = lt_openlibrary(LTNetMonitor))) return false;
    if (!(S.iNetCore    = lt_openlibrary(LTNetCore))) return false;
    S.iNetMonitor->OnStatusChange(StatusChange, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    P("Test done\n\n");
    lt_closelibrary(S.iNetCore);         // null ok
    lt_closelibrary(S.iNetMonitor);      // null ok
    // HTTP buffer is freed below in Main()
}

static int TestNetCoreSocketDeplete_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .core = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
    };

    char *buffers = lt_malloc(MAX_SOCKET_SPEC + MAX_HTTP_REPLY + MAX_HTTP_REQUEST);
    if (!buffers) return -1;
    S.sockSpec = buffers;
    S.reply    = S.sockSpec + MAX_SOCKET_SPEC;
    S.request  = S.reply    + MAX_HTTP_REPLY;

    const char *host = (argc > 2) ? argv[2] : "example.com";
    lt_snprintf(S.sockSpec, MAX_SOCKET_SPEC, "tcp host: %s port: 80", host);
    lt_snprintf(S.request,  MAX_HTTP_REQUEST, "GET / HTTP/1.1\r\nHost: %s:80\r\nConnection: close\r\n\r\n", host);

    LTThread thread = S.core->CreateThread("TestSocket");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(TEST_TIMEOUT));

    lt_free(buffers);
    return 0;
}

define_LTLIBRARY_APPLICATION(TestNetCoreSocketDeplete, 1, 1024);
