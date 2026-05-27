/*******************************************************************************
 *
 * FakeTelnet Server - "Just an empty shell"
 *
 * ltrun ExampleNetFakeTelnet (will start the net if necessary)
 * On another machine: telnet <ip>
 * Type some things.
 * Type "quit" to terminate.
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

static struct Statics {
    LTCore      *iCore;
    ILTThread   *iThread;
    LTNetCore   *iNetCore;
    ILTEvent    *iEvent;
    LTTransport  hTransport;
    LTSocket     hListenSocket;
    bool         noPrompt;
    bool         debug;
} S;

static void CmdClose(LTSocket hSocket) {
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hSocket);
    iSocket->DisconnectSocket(hSocket);
    S.iThread->Sleep(LTTime_Milliseconds(20));
    iSocket->Destroy(hSocket);
}

static void CmdQuit(LTSocket hSocket) {
    CmdClose(hSocket);
    S.iThread->Terminate(S.iThread->GetCurrentThread());
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
            const char *welcome = "Welcome to LT!\n";
            iSocket->WriteSocket(hSocket, welcome, lt_strlen(welcome));
            break;
        case kLTSocket_Event_WriteReady:
            if (S.noPrompt) break;
            S.noPrompt = true;
            iSocket->WriteSocket(hSocket, ">> ", 3);
            break;
        case kLTSocket_Event_ReadReady:;
            s32 len = 0;
            char buf[100] = {};
            do {
                len = iSocket->ReadSocket(hSocket, buf, sizeof(buf)-1);
                if (S.debug) lt_consoleprint("*** read returned %ld\n", LT_Ps32(len));
                if (len <= 0) break;
                buf[len] = 0;
                lt_consoleprint("%s", buf);
            } while (true);
            if (lt_strstr(buf, "debug")) { S.debug = true;    break; }
            if (lt_strstr(buf, "close")) { CmdClose(hSocket); break; }
            if (lt_strstr(buf, "quit"))  { CmdQuit(hSocket);  break; }
            const char *help = "Type help for help\n";
            if (lt_strstr(buf, "help")) help = "Type: debug, close, or quit\n";
            iSocket->WriteSocket(hSocket, help, lt_strlen(help));
            S.noPrompt = false;
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
        lt_consoleprint("Try: telnet %s\n", GetIpAddress(spec));
        S.hListenSocket = S.iNetCore->OpenSocket(0, "tcp listen port: 23 ", OnSocketEvent, clientData);
    }
}

static bool OnThreadStart(void) {
    S.hTransport = S.iNetCore->OpenTransport(NULL, OnNetCoreEvent, NULL);
    return !!S.hTransport;
}

static void OnThreadExit(void) {
    if (S.debug) lt_consoleprint("Exiting telnet\n");
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, S.hListenSocket);
    iSocket->Destroy(S.hListenSocket);
    S.hListenSocket = 0;
    S.iCore->DestroyHandle(S.hTransport);
}

static int FakeTelnet_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore    = LT_GetCore(),
        .iThread  = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent   = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .iNetCore = lt_openlibrary(LTNetCore),
        .debug    = argc > 2 && (lt_strcmp(argv[2], "-d") == 0)
    };
    if (!S.iNetCore) return -1;
    LTThread thread = S.iCore->CreateThread("Telnet");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2000);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Infinite());
    return 0;
}

define_LTLIBRARY_APPLICATION(FakeTelnet, 1, 1024);
