/*******************************************************************************
 *
 * ExampleNetHttpSimpler - Simpler network example of TCP for HTTP
 *
 * Assuming the network is already up, this example reads an index page
 * from a website and prints it to the console.

 * This is just an example of using TCP. Programs that need to use HTTP should
 * use the LTNetHttpCliet library (it handles TLS, redirection, etc.)
 *
 * The difference between this example and ExampleNetHttpSimple is that it
 * does not need to start networking. It assumes default transport is already up.
 *
 * To run:
 *      ltopen LTNetMonitor (optional if Net is not already up)
 *      ltrun ExampleNetHttpSimpler <hostname> (example.com is the default)
 *
 * Note: To run LTNetMonitor, be sure WiFi has been configured with LTShellWiFi.
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

#define MAX_SOCKET_SPEC   80    // length of socket spec
#define MAX_HTTP_REQUEST  80    // length of HTTP request
#define MAX_HTTP_REPLY    2048  // lenfth of HTTP reply (per read)

#define P LT_GetCore()->ConsolePrint

static struct Statics { // gets cleared in main()
    LTCore      *core;
    ILTThread   *iThread;
    LTNetCore   *iNet;
    char        *sockSpec;
    LTSocket     hSocket;
    char        *request;
    char        *reply;
    bool         requestSent;
} S;

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(clientData);
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
            P("\n");
            pSocket->DisconnectSocket(socket);
            S.iThread->Terminate(S.iThread->GetCurrentThread());
            break;
        default:
            break;
    }
}

static bool OnThreadStart(void) {
    if (!(S.iNet = lt_openlibrary(LTNetCore))) return false;
    if (!(S.hSocket = S.iNet->OpenSocket(0, S.sockSpec, OnSocketEvent, NULL))) return false;
    return true;
}

static void OnThreadExit(void) {
    S.core->DestroyHandle(S.hSocket); // null ok
    lt_closelibrary(S.iNet);          // null ok
}

static int ExampleNetHttpSimpler_Main(int argc, const char **argv) {
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

    LTThread thread = S.core->CreateThread("HttpSimpler");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(15));

    lt_free(buffers);
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleNetHttpSimpler, 1, 1024);
