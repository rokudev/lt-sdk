/*******************************************************************************
 *
 * TestNetCoreMultiIo - Perform multiple network I/O
 *
 * Starts the number of threads indicated on the command line and uses
 * them to read a file from a web server.
 *
 * If no command line argument is given, then only one thread will run.
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

#define TEST_TIMEOUT      600
#define MAX_SOCKETS       1
#define MAX_HTTP_REPLY    1024*8

DEFINE_LTLOG_SECTION("test.mio");

static struct Statics { // gets cleared in main()
    LTCore       *core;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTNetMonitor *iNetMonitor;
    bool          isUp;
    u16           socketMax;   // maximum sockets to allow
    u16           socketCount; // number of active sockets
    u16           socketId;    // unique id for socket data
} S;

typedef struct HttpControl {
    u16       idNum;
    LTSocket  hSocket;
    char     *sockSpec;
    char     *httpReq;
    bool      reqSent;
    char     *replyBuf;
    u32       replySize;
    u32       totalBytes;
    u32       threshold;
    LTTime    timeZero;
} HttpControl;

static void PrintStats(HttpControl *http) {
    u32 usec = (u32)LTTime_GetMicroseconds(LTTime_Subtract(S.core->GetKernelTime(), http->timeZero));
    u32 kbps = http->totalBytes * 8000 / usec;
    if (kbps > 99999) kbps = 99999; // avoid nonsense
    LTLOG("read", "%d: bytes: %6lu msec:%5lu.%03lu kbps:%6lu", http->idNum, LT_Pu32(http->totalBytes), LT_Pu32(usec)/1000, LT_Pu32(usec)%1000, LT_Pu32(kbps));
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    // PS("=== Event: socket: H%04lx event: 0x%lx\n", LT_Pu32(socket), LT_Pu32(event));
    ILTSocket   *iSocket = lt_gethandleinterface(ILTSocket, hSocket);
    HttpControl *http = (HttpControl*)clientData;
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (http->reqSent) break;
            LTLOG("send", "Sending request %d", http->idNum);
            http->reqSent = true;
            iSocket->WriteSocket(hSocket, http->httpReq, lt_strlen(http->httpReq));
            break;
        case kLTSocket_Event_ReadReady:
            if (!http->replyBuf) break;
            if (LTTime_IsZero(http->timeZero)) http->timeZero = S.core->GetKernelTime();
            s32 len = 0;
            do {
                // Just read the pending data and discard it
                len = iSocket->ReadSocket(hSocket, http->replyBuf, http->replySize);
                if (len > 0) http->totalBytes += len;
            } while (len > 0);
            if (http->totalBytes > http->threshold) {
                PrintStats(http);
                http->threshold = http->totalBytes + (50*1024);
            }
            break;
        case kLTSocket_Event_Connected:
            LTLOG("conn", "Connected socket %d", http->idNum);
            break;
        case kLTSocket_Event_Disconnected:
            LTLOG("disc", "Disconnected socket %d", http->idNum);
            iSocket->DisconnectSocket(hSocket);
            PrintStats(http);
            lt_free(http->replyBuf);
            http->replyBuf = NULL;
            S.core->DestroyHandle(hSocket);
            lt_free(http);
            S.socketCount--;
            S.iThread->Destroy(S.iThread->GetCurrentThread());
            break;
        case kLTSocket_Event_SocketReady:
        case kLTSocket_Event_DnsResolved:
            break;
        default:
            LTLOG("odd!", "Odd Event: socket: H%04lx event: 0x%lx", LT_Pu32(hSocket), LT_Pu32(event));
            break;
    }
}

static bool OnIoStart(void) {
    lt_consoleprint("\n\nStarting thread %d (0x%lx), memory avail: %ld\n", S.socketId, LT_PLT_HANDLE(S.iThread->GetCurrentThread()), LT_Pu32(S.core->GetAvailableSystemRAM()));
    const char *reason;
    do {
        reason = "http malloc";
        HttpControl *http = lt_malloc(sizeof(HttpControl));
        if (!http) break;
        *http = (struct HttpControl) {
            .idNum     = S.socketId,
            .sockSpec  = "tcp host: rebol.com port: 80",
            .httpReq   = "GET /r300kb HTTP/1.1\r\nHost: rebol.com:80\r\nConnection: close\r\n\r\n",
            .replySize = MAX_HTTP_REPLY
        };
        reason = "replybuf malloc";
        if (!(http->replyBuf = lt_malloc(http->replySize))) break;
        reason = "OpenSocket";
        if (!S.iNetCore->OpenSocket(0, http->sockSpec, OnSocketEvent, http)) {
            lt_free(http->replyBuf);
            lt_free(http);
            break;
        }
        return true;
    } while (false);
    lt_consoleprint("!!!!!!! Thread start failed. Reason: %s !!!!!!!\n", reason);
    return false;
}

static void OnIoExit(void) {
    lt_consoleprint("Thread 0x%lx terminated, memory avail: %ld\n", LT_PLT_HANDLE(S.iThread->GetCurrentThread()), LT_Pu32(S.core->GetAvailableSystemRAM()));
}

static void StartThread(void) {
    char name[10];
    lt_snprintf(name, 10, "T%d", ++S.socketId);
    LTThread thread = S.core->CreateThread(name);
    if (!thread) {
        LTLOG("fail", "StartThread failed (%s)!", name);
        return;
    }
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnIoStart, OnIoExit);
}

static void OnTimer(void *clientData) {
    LT_UNUSED(clientData);
    if (S.socketCount < S.socketMax) {
        S.socketCount++;
        StartThread();
    }
}

static void OnNetStatusChange(LTNetMonitor_Status status, void *clientData) {
    LT_UNUSED(clientData);
    if (!S.isUp && status == kLTNetMonitor_Status_NetworkUp) {
        S.isUp = true;
        S.iThread->SetTimer(S.iThread->GetCurrentThread(), LTTime_Seconds(1), OnTimer, NULL, NULL);
    }
}

static bool OnMainStart(void) {
    if (!(S.iNetMonitor = lt_openlibrary(LTNetMonitor))) return false;
    if (!(S.iNetCore    = lt_openlibrary(LTNetCore))) return false;
    S.iNetMonitor->OnStatusChange(OnNetStatusChange, NULL, NULL);
    return true;
}

static void OnMainExit(void) {
    lt_closelibrary(S.iNetCore);         // null ok
    lt_closelibrary(S.iNetMonitor);      // null ok
}

static int TestNetCoreMultiIo_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    S = (struct Statics) { // clears all other fields
        .core = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .socketMax = MAX_SOCKETS
    };
    if (argc > 2) {
        S.socketMax = lt_strtou32(argv[2], NULL, 10);
    }
    LTThread thread = S.core->CreateThread("MultiIo");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnMainStart, OnMainExit);
    // Don't wait. Return to shell so we can use other commands.
    //S.iThread->WaitUntilFinished(thread, LTTime_Seconds(TEST_TIMEOUT));
    return 0;
}

define_LTLIBRARY_APPLICATION(TestNetCoreMultiIo, 1, 1024);
