/*******************************************************************************
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * TestNetCoreOpenRepeat - Copyright 2023, Roku, Inc.  All rights reserved.
 *
 * Purpose:
 *      Confirm that NetCore can be opened and closed several times while reading
 *      a short web page, and after each, it provides a valid default transport that
 *      works each time.
 *
 * Results:
 *      Compare that the web page gets read and that the CheckSum result is the same.
 *
 * Related: LT-972 fixed this
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>

#define P LT_GetCore()->ConsoleStomp

#define REPLY_BUF_MAX 2000

static struct Statics { // cleared in main()
    LTCore      *iCore;
    ILTThread   *iThread;
    LTNetCore   *iNetCore;
    ILTEvent    *iEvent;
    LTTransport  hTransport;
    LTSocket     hSocket;
    char        *replyBuf;
    u32          checkSum;
    u32          firstSum;
    bool         requestSent;
    bool         headerFound;
    bool         debug;
} S;

static u32 CheckSum(char *buf) {
    u32 sum = 0;
    for (s32 n = 0; buf[n]; n++) sum += (u32)buf[n];
    return sum;
}

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    if (S.debug) P("*** sock event: socket: H%04lx event: 0x%x data: %p\n", LT_Pu32(socket), event, clientData);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_Connected:
            break;
        case kLTSocket_Event_WriteReady:
            if (S.requestSent) break;
            S.requestSent = true;
            const char *req = "GET / HTTP/1.1\r\nHost: example.com:80\r\nConnection: close\r\n\r\n";
            //P("writing: %s\n", req);
            pSocket->WriteSocket(socket, req, lt_strlen(req));
            break;
        case kLTSocket_Event_ReadReady:;
            s32 len = 0;
            do {
                char *buf = S.replyBuf;
                len = pSocket->ReadSocket(socket, buf, REPLY_BUF_MAX-1);
                //P("read returned %d\n", len);
                if (len <= 0) break;
                buf[len] = 0;
                if (!S.headerFound) {
                    char *body = lt_strstr(buf, "\r\n\r\n"); // not perfect, cannot be split
                    if (body) {
                        buf = body + 4;
                        S.headerFound = true;
                    }
                }
                if (S.headerFound) S.checkSum += CheckSum(buf);
                if (S.debug) LT_GetCore()->ConsoleStompString(buf);
            } while (true);
            break;
        case kLTSocket_Event_Disconnected:
            if (S.debug) P("*** disconnect\n");
            pSocket->DisconnectSocket(socket);
            S.iThread->Terminate(S.iThread->GetCurrentThread());
            S.hSocket = 0;
            break;
        default: break;
    }
}

static void OnEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    if (S.debug) P("-*** Transport Event: %08lx %0x %p\n", LT_PLT_HANDLE(hTransport), event, clientData);
    switch (event) {
        case kLTTransport_Event_Up:
            if (S.hSocket) break; // don't do it again
            S.hSocket = S.iNetCore->OpenSocket(0, "tcp host: example.com port: 80", OnSocketEvent, clientData);
            break;
        default: break;
    }
}

static bool OnThreadStart(void) {
    S.requestSent = false;
    S.headerFound = false;
    S.checkSum = 0;
    S.hSocket = 0;
    S.replyBuf = lt_malloc(REPLY_BUF_MAX);
    if (!S.replyBuf) return false;
    S.hTransport = S.iNetCore->OpenTransport(NULL, OnEvent, (void*)0x12345678);
    return !!S.hTransport;
}

static void OnThreadExit(void) {
    if (!S.firstSum) S.firstSum = S.checkSum;
    P("Checksum: %lu (%s)\n", LT_Pu32(S.checkSum), S.firstSum != S.checkSum ? "wrong!" : "good");
    S.iCore->DestroyHandle(S.hTransport);
    lt_free(S.replyBuf);
}

static int TestNetCoreOpenRepeat_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore    = LT_GetCore(),
        .iThread  = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent   = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .iNetCore = lt_openlibrary(LTNetCore),
        .debug    = argc > 2 && (lt_strcmp(argv[2], "-d") == 0)
    };
    if (!S.iNetCore) return -1;

    LTThread thread;
    for (u8 count = 0; count < 5; count++) {
        thread = S.iCore->CreateThread("NetTest");
        if (!thread) return -1;
        S.iThread->SetStackSize(thread, 2000);
        S.iThread->Start(thread, OnThreadStart, OnThreadExit);
        S.iThread->WaitUntilFinished(thread, LTTime_Seconds(15));
        P("---------------------------------------------------------------------------\n");
    }
    return 0;
}

define_LTLIBRARY_APPLICATION(TestNetCoreOpenRepeat, 1, 1024);
