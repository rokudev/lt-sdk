/*******************************************************************************
 *
 * TestNetCoreSocketOpenClose - Test opening and closing sockets in a loop
 *
 * Opens a socket to a TCP web server, reads the page, closes the socket.
 * Repeats TEST_LOOPS number of times.
 *
 * Depending on USE_ROUTER_WEB_SERVER, the web server can be either the one
 * supplied by your WiFi router (preferred) or example.com
 *
 * Prints out the amount of system memory used (to watch for leaks.)
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

#define TEST_LOOPS        100
#define TEST_TIMEOUT      TEST_LOOPS * 3 // figure max of 3 seconds per web response

#define MAX_SOCKET_SPEC   80    // length of socket spec
#define MAX_HTTP_REQUEST  80    // length of HTTP request
#define MAX_HTTP_REPLY    2048  // lenfth of HTTP reply (per read)

#define P LT_GetCore()->ConsolePrint

static struct Statics { // gets cleared in main()
    LTCore       *core;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTNetMonitor *iNetMonitor;
    char         *sockSpec;
    LTSocket      hSocket;
    char         *request;
    char         *reply;
    bool          requestSent;
    u16           loopCount;
    LT_SIZE       memoryUse;
} S;

static void OpenSocket(void *clientData);

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    // P("\n=== sock event: socket H%04lx event 0x%x\n", LT_Pu32(socket), event);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (S.requestSent) break;
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
            if (!S.memoryUse) S.memoryUse = S.core->GetAvailableSystemRAM();
            P("Socket loop %d done, memory use: %d\n", S.loopCount, S.memoryUse - S.core->GetAvailableSystemRAM());
            pSocket->DisconnectSocket(socket);
            S.iThread->Sleep(LTTime_Milliseconds(100));
            S.core->DestroyHandle(socket);
            if (S.loopCount >= TEST_LOOPS) {
                S.iThread->Terminate(S.iThread->GetCurrentThread());
                break;
            }
            OpenSocket(clientData);
            break;
        default:
            break;
    }
}

static void OpenSocket(void *clientData) {
    S.requestSent = false;
    S.loopCount++;
    P("\n-----------------------------\n");
    P("Open count: %d socket: %s\n", S.loopCount, S.sockSpec);
    S.hSocket = S.iNetCore->OpenSocket(0, S.sockSpec, OnSocketEvent, clientData);
    if (!S.hSocket) {
        P("Socket did not open!\n");
        S.iThread->Terminate(S.iThread->GetCurrentThread());
    }
}

static void StatusChange(LTNetMonitor_Status status, void *clientData) {
    if (status == kLTNetMonitor_Status_NetworkUp && !S.hSocket) {
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
        OpenSocket(clientData);
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

static int TestNetCoreSocketOpenClose_Main(int argc, const char **argv) {
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

define_LTLIBRARY_APPLICATION(TestNetCoreSocketOpenClose, 1, 1024);
