/*******************************************************************************
 *
 * ExampleNetPingPong
 *
 * Sends a "ping" to peer device which then sends a "pong" back. Repeats.
 *
 * This same example will use TCP or UDP depending on the -t command line option.
 *
 * To run this, you will need another LT device to respond to the ping.
 *
 * IMPORTANT NOTE:
 * Prior to running this code, either LTNetMonitor or LTShellNet must be opened
 * to start networking. And of course, WiFi must have been setup at some earlier
 * time with LTShellWiFi.
 *
 * To run UDP:
 *      server: ltrun ExampleNetPingPong
 *      client: ltrun ExampleNetPingPong -a <address-of-listener>
 *
 * To run TCP:
 *      server: ltrun ExampleNetPingPong -t
 *      client: ltrun ExampleNetPingPong -t -a <address-of-listener>
 *
 * To set number of packets with -c <num> and msec between each with -m <msec>
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

#define P LT_GetCore()->ConsoleStomp

static struct Statics { // gets cleared in main()
    LTCore      *core;
    LTNetCore   *netCore;
    ILTThread   *iThread;
    LTThread     hMainThread;
    char        *peerAddress;
    char         sockSpec[64];
    ILTSocket   *iSocket;
    LTSocket     hSocket;
    LTSocket     hServer;
    u16          pingTime;
    u16          maxPings;
    u16          pingCount;
    bool         debug;
    bool         useTcp;
} S;

typedef struct {
    char str[4];
    s16  count;
} PingPacket;

static void SendPing(LTSocket socket, bool isPing, u16 count) {
    PingPacket pp = { .count = LT_HTONS(count) };
    char *str = isPing ? "Ping" : "Pong";
    P("send: %s count: %d\n", str, count);
    P("<<<<<<<<<<<<< %s\n", str);
    lt_memcpy(pp.str, str, 4);
    S.iSocket->WriteSocket(socket, &pp, sizeof(pp));
}

static s16 ReadPing(LTSocket socket, bool isPing) {
    PingPacket pp = {};
    s32 len = S.iSocket->ReadSocket(socket, &pp, sizeof(pp));
    if (sizeof(pp) != len) {
        P("*** Packet size difference: %lu %ld\n", LT_Pu32(sizeof(pp)), LT_Ps32(len));
        return 0;
    }
    P(">>>>>>>>>>>>> %s\n", pp.str);
    char *str = isPing ? "Pong" : "Ping";
    if (lt_strncmp(pp.str, str, 4)) {
        P("*** Error: packet did not match expected result: expected %s got %s\n", str, pp.str);
    }
    s16 count = LT_NTOHS(pp.count); // sign difference
    return pp.str[1] == 'o' ? -count : count;
}

static void OnTimer(void *clientData) { LT_UNUSED(clientData);
    if (S.pingCount >= S.maxPings) {
        S.iThread->KillTimer(S.hMainThread, OnTimer, clientData);
        S.iThread->Terminate(S.hMainThread);
        return;
    }
    SendPing(S.hSocket, true, ++S.pingCount);
}

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *data) {
    LT_UNUSED(data);
    if (S.debug) P("=== Event! socket: H%04lx event: 0x%lx\n", LT_Pu32(socket), LT_Pu32(event));
    switch (event) {
        case kLTSocket_Event_ConnectRequest: // TCP server only
            S.iSocket->ConnectSocket(socket);
            S.hServer = S.hSocket;
            S.hSocket = socket;
            break;
        case kLTSocket_Event_ReadReady:;
            s16 count = ReadPing(socket, S.peerAddress); // negative for pong count
            if (count) {
                const char *which = count > 0 ? "Ping" : "Pong";
                if (count < 0) count = -count;
                P("recv: %s count: %d\n", which, count);
                if (!S.peerAddress) SendPing(S.hSocket, false, ++S.pingCount);
            }
            break;
        case kLTSocket_Event_Disconnected:
            S.iSocket->DisconnectSocket(socket);
            // fall thru
        case kLTSocket_Event_SocketError:
            S.iThread->Terminate(S.hMainThread);
            break;
        default:
            break;
    }
}

static char *AppendStr(char *to, const char *from) {
    u32 len = lt_strlen(from);
    lt_memcpy(to, from, len+1);
    return to + len;
}

static bool OnThreadStart(void) {
    do {
        if (!(S.netCore = lt_openlibrary(LTNetCore))) break;
        P("Opening socket: %s\n", S.sockSpec);
        if (!(S.hSocket = S.netCore->OpenSocket(0, S.sockSpec, OnSocketEvent, 0))) break;
        S.iSocket = lt_gethandleinterface(ILTSocket, S.hSocket);
        if (S.useTcp && !S.peerAddress) S.hServer = S.hSocket;
        if (S.peerAddress) S.iThread->SetTimer(S.hMainThread, LTTime_Milliseconds(S.pingTime), OnTimer, NULL, NULL);
       return true;
    } while (false);
    P("Initialization failed\n");
    return false;
}

static void OnThreadExit(void) {
    S.core->DestroyHandle(S.hSocket);
    if (S.hServer) S.core->DestroyHandle(S.hServer);
    lt_closelibrary(S.netCore);
}

static int ExampleNetPingPong_Main(int argc, const char **argv) {
    S = (struct Statics) {
        .core     = LT_GetCore(),
        .iThread  = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .pingTime = 100,
        .maxPings = 3,
    };

    for (int n = 2; n < argc; n++) {
        if (argv[n][0] == '-') {
            switch (argv[n][1]) {
                case 't': S.useTcp      = true; break;
                case 'd': S.debug       = true; break;
                case 'a': S.peerAddress = (char*)argv[n + 1]; break;
                case 'c': S.maxPings    = lt_strtou32(argv[n+1], NULL, 10); break;
                case 'm': S.pingTime    = lt_strtou32(argv[n+1], NULL, 10); break;
                default:
                    P("*** Usage: -a <address> (listener doesn't need address)\n");
                    P("    Options:\n"
                      "\t-t for TCP (else UDP)\n"
                      "\t-c ping count\n"
                      "\t-m msec between pings\n"
                      "\t-d for debug\n");
                    return -1;
            }
        }
    }
    P("Running %s with %s\n", S.peerAddress ? "PINGER" : "PONGER", S.useTcp ? "TCP" : "UDP");
    if (S.peerAddress) P("Sending %u times every %u msec\n", S.maxPings, S.pingTime);

    // Make socket spec:
    char *s;
    s = AppendStr(S.sockSpec, S.useTcp ? "tcp " : "udp ");
    if (S.peerAddress) { // client side
        s = AppendStr(s, "ip: ");
        s = AppendStr(s, S.peerAddress); // follow with a space...
        s = AppendStr(s, " port: 8888 ");
        if (!S.useTcp) s = AppendStr(s, "myport: 8887 ");
    } else { // server side
        if (S.useTcp) s = AppendStr(s, "listen ");
        else s = AppendStr(s, "port: 8887 ");
        s = AppendStr(s, "myport: 8888 ");
    }
    if (S.debug) AppendStr(s, "debug");

    S.hMainThread = S.core->CreateThread("PingPong");
    if (!S.hMainThread) return -1;
    S.iThread->SetStackSize(S.hMainThread, 2048);
    S.iThread->Start(S.hMainThread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(S.hMainThread, LTTime_Infinite());
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleNetPingPong, 1, 2048);
