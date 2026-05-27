/*******************************************************************************
 *
 * Mini Web Server
 *
 * ltrun ExampleNetWebServer (will start the net if necessary)
 * Wait for the message: "Browse http://<ip>"
 * You'll see the PS process list (same as shell command)
 *
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

#define MAX_NET_BUFFER 512

static struct Statics {
    LTCore               *iCore;
    ILTThread            *iThread;
    LTNetCore            *iNetCore;
    ILTEvent             *iEvent;
    LTTransport           hTransport;
    LTSocket              hListenSocket;
    char                 *netBuffer;
    u16                   writeCount;
    u16                   totalThreads;
    LTArray              *arrayOfThreads;
    bool                  debug;
} S;

static const char *s_HttpHeader =
    "HTTP/1.1 200 OK\r\n"
    "Accept-Ranges: bytes\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *s_HtmlHeader =
    "<!doctype html>"
    "<html>"
    "<head>"
    "<title>LT Web</title>"
    "<meta http-equiv=\"Content-type\" content=\"text/html; charset=utf-8\" />"
//  "<meta http-equiv=\"refresh\" content=\"2\">" // broken
    "<style>"
    "body {font-family: sans-serif;}"
    "table {border-collapse: collapse;}"
    "tr {border: 2px solid black;}"
    "th, td {text-align: right; border: 2px solid black; padding: 5px;}"
    "</style>"
    "</head>"
    "<body >"
    "<h2>LT Thread Status</h2>\n"
    "<table>";

static const char *s_HtmlFooter =
    "</table>"
    "</body>"
    "</html>";

static const char *s_TableHeader =
    "<tr><th>Id</th><th>Name</th><th>Prio</th><th>State</th><th>Stack</th><th>Curr</th><th>Max</th><th>Heap</th><th>Max</th></tr>\n";

static const char *s_TableRow =
    "<tr><td>%lu</td><td>%s</td><td>%lu</td><td style=\"background-color: %s;\">%s</td><td>%lu</td><td>%lu</td><td>%lu</td><td>%lu</td><td>%lu</td></tr>\n";

// These functions ripped from shell PS command:
static void ThreadsSnapshotCallback(LTThread_Snapshot *arrayOfThreads, u32 nCount, void *pClientData) {
    LTArray *pArray = (LTArray *)pClientData;
    for (u32 i = 0; i < nCount; ++i) {
        pArray->API->Append(pArray, &arrayOfThreads[i]);
    }
}

static int ComparePriority(const void *pElement1, const void *pElement2, void * pClientData) {
    LTThread_Snapshot *p1 = (LTThread_Snapshot *)pElement1;
    LTThread_Snapshot *p2 = (LTThread_Snapshot *)pElement2;
    LT_UNUSED(pClientData);
    return (p1->nPriority > p2->nPriority) ? -1 :
           (p1->nPriority < p2->nPriority) ?  1 :
           (p1->nThreadNumber < p2->nThreadNumber) ? -1 : 1;
}

static void GetThreads(void) {
    S.arrayOfThreads->API->SetCount(S.arrayOfThreads, 0);
    S.iThread->SnapshotRunningThreads(ThreadsSnapshotCallback, S.arrayOfThreads);
    s.arrayOfThreads->API->Sort(S.arrayOfThreads, ComparePriority, NULL);
    S.totalThreads = S.arrayOfThreads->API->GetCount(S.arrayOfThreads);
    lt_consoleprint("%d threads\n", S.totalThreads);
}

static char *s_StateColors[] = {
    "gray",       // invalid
    "lightbrown", // not started
    "lightgreen", // ready to run
    "pink",       // running
    "lightblue",  // wait blocked
    "darkblue",   // mutex blocked
    "yellow",     // sleeping
    "cyan",       // terminate pending
    "darkgray"    // terminated
};

static void FormatThread(u16 n, char *buffer, u32 maxSize) {
    LTThread_Snapshot *pSnapshot = S.arrayOfThreads->API->Get(S.arrayOfThreads, n, NULL);
    lt_snprintf(buffer, maxSize, s_TableRow,
        LT_Pu32(pSnapshot->nThreadNumber),
        pSnapshot->name,
        LT_Pu32(pSnapshot->nPriority),
        s_StateColors[pSnapshot->nThreadState],
        S.iThread->ThreadStateToString(pSnapshot->nThreadState),
        LT_Pu32(pSnapshot->nStackSize),
        LT_Pu32(pSnapshot->nStackCurrent),
        LT_Pu32(pSnapshot->nStackHiWatermark),
        LT_Pu32(pSnapshot->nHeapCurrent),
        LT_Pu32(pSnapshot->nHeapHiWatermark)
    );
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    if (S.debug) lt_consoleprint("*** sock event: hSocket: H%04lx event: 0x%x data: %p\n", LT_Pu32(hSocket), event, clientData);
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hSocket);
    switch (event) {
        case kLTSocket_Event_SocketReady:
            break;
        case kLTSocket_Event_Connected:; // a remote client has connected
            char spec[80];
            iSocket->GetSocketSpec(hSocket, spec, 80);
            lt_consoleprint("Socket connected %lx: %s\n", LT_Pu32(hSocket), spec);
            iSocket->WriteSocket(hSocket, s_HttpHeader,  lt_strlen(s_HttpHeader));
            S.writeCount = 0;
            GetThreads();
            break;
        case kLTSocket_Event_WriteReady:
            if (S.writeCount == 0) { // small, so blast it out
                iSocket->WriteSocket(hSocket, s_HtmlHeader,  lt_strlen(s_HtmlHeader));
                iSocket->WriteSocket(hSocket, s_TableHeader, lt_strlen(s_TableHeader));
            }
            else if (S.writeCount > S.totalThreads) {
                iSocket->WriteSocket(hSocket, s_HtmlFooter, lt_strlen(s_HtmlFooter));
                iSocket->DisconnectSocket(hSocket);
                iSocket->Destroy(hSocket);
                break;
            } else {
                FormatThread(S.writeCount-1, S.netBuffer, MAX_NET_BUFFER);
                iSocket->WriteSocket(hSocket, S.netBuffer, lt_strlen(S.netBuffer));
            }
            S.writeCount++;
            break;
        case kLTSocket_Event_ReadReady:;
            s32 len = 0;
            do {
                len = iSocket->ReadSocket(hSocket, S.netBuffer, MAX_NET_BUFFER);
                if (len <= 0) break;
                S.netBuffer[len] = 0;
                LT_GetCore()->ConsolePutString(S.netBuffer);
            } while (true);
            break;
        case kLTSocket_Event_Disconnected:
            if (S.debug) lt_consoleprint("Socket destroyed: %lx\n", LT_Pu32(hSocket));
            iSocket->Destroy(hSocket);
            break;
        default: break;
    }
}

static char *GetIpAddress(char *spec) { // makes assumptions about order
    lt_consoleprint("spec %s\n", spec);
    char *s, *e;
    if (!(s = lt_strstr(spec, " ip: "))) return NULL;
    s += 5;
    if (!(e = lt_strstr(s, " gw: "))) return NULL;
    *e = 0;
    return s;
}

static void OnNetCoreEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    if (event == kLTTransport_Event_Up) {
        if (S.hListenSocket) return; // socket already open
        char spec[80];
        S.iNetCore->GetTransportSpec(hTransport, spec, 80);
        lt_consoleprint("Browse http://%s\n", GetIpAddress(spec));
        S.hListenSocket = S.iNetCore->OpenSocket(0, "tcp listen port: 80 ", OnSocketEvent, clientData);
    }
}

static void OnThreadExit(void) {
    if (S.debug) lt_consoleprint("Exiting server\n");
    lt_destroyobject(S.arrayOfThreads);
    lt_free(S.netBuffer);
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, S.hListenSocket);
    iSocket->Destroy(S.hListenSocket);
    S.hListenSocket = 0;
    S.iCore->DestroyHandle(S.hTransport);
}

static bool OnThreadStart(void) {
    S.netBuffer = lt_malloc(MAX_NET_BUFFER);
    if (!S.netBuffer) return false;
    S.arrayOfThreads = LTArray_CreateStructArray(sizeof(LTThread_Snapshot));
    if (!S.arrayOfThreads) {
        lt_free(S.netBuffer);
        return false;
    }
    S.hTransport = S.iNetCore->OpenTransport(NULL, OnNetCoreEvent, NULL);
    if (!S.hTransport) {
        OnThreadExit();
        return false;
    }
    return true;
}

static int WebServer_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore    = LT_GetCore(),
        .iThread  = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent   = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .iNetCore = lt_openlibrary(LTNetCore),
        .debug    = argc > 2 && (lt_strcmp(argv[2], "-d") == 0)
    };
    if (!S.iNetCore) return -1;
    LTThread thread = S.iCore->CreateThread("LTWebServer");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2000);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Infinite());
    return 0;
}

define_LTLIBRARY_APPLICATION(WebServer, 1, 1024);
