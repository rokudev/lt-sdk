/******************************************************************************* *
 * UnitTestLTNetCore
 * -----------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * How to run:
 *      1. This test requires a Python-based peer to communicate with
 *      2. Modify PEER defines below to match your PEER address and port
 *      3. On PEER device: python3 UnitTestLTNetCore.py
 *      4. ltrun UnitTestLTNetCore PEER_ADDR=192.168.0.4 PEER_PORT=8888
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/utility/ipaddress/LTUtilityIPAddress.h>

#include <tilt/JiltEngine.h>

// Verbose DEBUG options used for development:
enum {
    DEBUG_SHOW_EVENTS        = false,   // to see what events are happening
    DEBUG_SHOW_PROGRESS      = false,   // to see transfer progress
    DEBUG_SHOW_DETAIL        = false,   // verbose output for every transfer
    DEBUG_SHOW_SIGNALS       = false,   // show deferred signaling
    DEBUG_ASYNC_EVENT        = false,   // immediate events process TILT signal differently
};

#define PEER_ADDR   "192.168.0.4"
#define PEER_PORT   "8888"
#define DNS_HOST    "www.google.com"
#define DNS_SPEC    "dns host: " DNS_HOST

#define BUFFER_SIZE    2048

typedef struct {
    bool         isReady;       // socket is ready
    bool         isRead;        // it's a read test
    u32          listen;        // reverse mode (TCP listen)
    char         *proto;        // tcp/udp?
    bool         commandSent;   // command has been sent to peer
    bool         listenSent;    // listen command has been sent to peer
    char         command[2];    // command (to send to peer)
    char         response[20];  // response from peer after writes
    u32          bytesExpected;
    u32          bytesTotal;
    s32          bytesLeft;
    u32          checkSum;
    u32          sockCount;     // Count for multiple open sockets
} TestVars;

static struct Statics { // gets cleared in main()
    LTCore       *iCore;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTNetMonitor *iNetMonitor;
    ILTSocket    *iSocket;
    LTUtilityIPAddress *iUtilityIPAddress;

    const char   *peerAddr;
    const char   *peerPort;
    char         *tranSpec;
    LTTransport   hTransport;

    char          sockSpec[64];
    LTSocket      hSocket; // Host socket
    LTSocket      lSocket; // Listen socket
    LTSocket      cSocket; // Listen child socket
    LTSocket     *sockets; // Socket array for multiple open sockets

    TestVars      test;

    const char   *doneSignal;
    char         *buffer;

    bool         debug;
} S;

static JiltEngine *s_engine;

//***** Helpers ****************************************************************

// This signal method works because don't run simultaneous tests:
#define SIGNAL_TEST \
    if (DEBUG_SHOW_SIGNALS) TILT_DEBUG(tilt, "<<<<< signaling: %s\n", S.doneSignal); \
    s_engine->API->SignalTestCompletion(s_engine, S.doneSignal); \
    S.doneSignal = "?"

#define WAIT_TEST(timeout) { \
    if (DEBUG_SHOW_SIGNALS) TILT_DEBUG(tilt, ">>>>> waiting on: %s\n", tilt->API->GetTestName()); \
    S.doneSignal = tilt->API->GetTestName(); \
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(timeout), NULL, NULL); \
}

static char *AppendStr(char *to, const char *from) {
    u32 len = lt_strlen(from);
    lt_memcpy(to, from, len+1);
    return to + len;
}

//***** General Operations *****************************************************

static void NetMonitorStatusChange(LTNetMonitor_Status status, void *clientData) {
    Tilt *tilt = clientData;
    TILT_INFO(tilt, "network status: 0x%x state: %s\n", status, S.iNetMonitor->IsUp() ? "up" : "down");
    if (status == kLTNetMonitor_Status_NetworkUp) {
        SIGNAL_TEST;
    }
}

static void Test_OpenNetMonitor(Tilt *tilt) { // starts networking
    S.iNetMonitor = lt_openlibrary(LTNetMonitor);
    TILT_ASSERT_TRUE(tilt, S.iNetMonitor, "Open LTNetMonitor failed");
    S.iNetMonitor->OnStatusChange(NetMonitorStatusChange, NULL, (void*)tilt);
    /// !!!! add NoStatusChange, Add closelibrary
    WAIT_TEST(30); // sometimes Wi-Fi is slow to connect!
}

static void Test_OpenNetCore(Tilt *tilt) {
    S.iNetCore = lt_openlibrary(LTNetCore);
    TILT_ASSERT_TRUE(tilt, S.iNetCore, "Open LTNetCore failed");
}

static void Test_OpenUtilityIPAddress(Tilt *tilt) {
    S.iUtilityIPAddress = lt_openlibrary(LTUtilityIPAddress);
    TILT_ASSERT_TRUE(tilt, S.iUtilityIPAddress, "Open LTUtilityIPAddress failed");
}

static void OnGoodTransportEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    Tilt *tilt = clientData;
    if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "OnGoodTransportEvent: %x", event);
    TILT_ASSERT_TRUE(tilt, hTransport != 0, "transport valid");
    TILT_ASSERT_TRUE(tilt, event != 0, "event valid");
    switch (event) {
        case kLTTransport_Event_Up:
            // This is what allows us to open sockets in subsequent tests.
            if (DEBUG_ASYNC_EVENT) {
                SIGNAL_TEST;
            }
            break;
        case kLTTransport_Event_Down:
            break;
        default:
            break;
    }
}

static void Test_TransportOpenGood(Tilt *tilt) {
    S.hTransport = S.iNetCore->OpenTransport(NULL, OnGoodTransportEvent, (void *)tilt); //"IpWifi dhcp"
    TILT_ASSERT_TRUE(tilt, S.hTransport, "OpenTransport should not fail");
    if (DEBUG_ASYNC_EVENT) {
        WAIT_TEST(5);
    }
}

//***** Tests ******************************************************************

static void CheckSum(char *buf, s32 len) {
    for (s32 n = 0; n < len; n++) S.test.checkSum += (u32)buf[n];
}

static void OnSockEvent(LTSocket hSocket, LTSocket_Event event, void *clientData);
static void OnDnsEvent(LTSocket hSocket, LTSocket_Event event, void *clientData);

static void OpenListenSocket(Tilt *tilt) {
    LTNetIpv4Endpoint ep;
    if (S.lSocket) return; // Listen socket already open
    S.sockSpec[0] = 0;
    AppendStr(S.sockSpec, S.test.proto);
    AppendStr(S.sockSpec, " listen");
    TILT_INFO(tilt, "OpenSocket: %s %lx", S.sockSpec, S.hTransport);
    S.lSocket = S.iNetCore->OpenSocket(S.hTransport, S.sockSpec, OnSockEvent, (void*)tilt);
    TILT_ASSERT_TRUE(tilt, S.lSocket, "did not open");
    S.iSocket->GetProperty(S.lSocket, "local.endpoint.v4", &ep);
    TILT_INFO(tilt, "--- listen socket = %lx, myport = %u", S.lSocket, ep.port);
}

static void CloseListenSocket(Tilt *tilt) {
    if (S.lSocket) {
        lt_destroyhandle(S.lSocket);
        S.lSocket = 0;
        if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "destroyed listen socket");
    }
}

static void OpenTestSocket(Tilt *tilt) {
    S.sockSpec[0] = 0;
    char *e;
    e = AppendStr(S.sockSpec, S.test.proto);
    e = AppendStr(e, " ip: ");
    e = AppendStr(e, S.peerAddr);
    e = AppendStr(e, " port: ");
    e = AppendStr(e, S.peerPort);
    TILT_INFO(tilt, "OpenSocket: %s %lx", S.sockSpec, S.hTransport);
    S.hSocket = S.iNetCore->OpenSocket(S.hTransport, S.sockSpec, OnSockEvent, (void*)tilt);
    TILT_ASSERT_TRUE(tilt, S.hSocket, "did not open");
    if (!S.hSocket) return;
    S.iSocket = lt_gethandleinterface(ILTSocket, S.hSocket);
    WAIT_TEST(5);
    // don't put anything here, nor in the function that calls this
}

static void OpenDns(Tilt *tilt) {
    LTSocket socket = S.iNetCore->OpenSocket(S.hTransport, DNS_SPEC, OnDnsEvent, (void*)tilt);
    S.iSocket = lt_gethandleinterface(ILTSocket, socket);
    TILT_ASSERT_TRUE(tilt, socket, "DNS socket did not open");
    WAIT_TEST(15);
}

static void PrepTest(Tilt *tilt, const char command, u32 length, bool isRead, int listen, char *proto) {
    S.test = (TestVars) {
        .isRead        = isRead,
        .listen        = listen,
        .proto         = proto, // tcp or udp
        .bytesExpected = length,
        .bytesLeft     = length,
    };
    S.test.command[0] = command;

    char out[11] = "0123456789";
    u32 len = lt_strlen(out);
    out[0] = command;
    const u32 cnt = 100;
    for (u16 n = 0; n < cnt; n++) {
        lt_memcpy(S.buffer + (n * len), out, len);
    }
    S.buffer[cnt * len] = 0;

    if (listen > 0) OpenListenSocket(tilt);
    OpenTestSocket(tilt);
}

static void OnSockEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    Tilt *tilt = clientData;
    if (DEBUG_SHOW_EVENTS) TILT_DEBUG(tilt, "%s socket<%lx> event: %x (%s)",
                                         S.test.proto, LT_PLT_HANDLE(hSocket), event, S.doneSignal);
    s32 len = 0;
    switch (event) {

        case kLTSocket_Event_SocketReady:
            S.test.isReady = true;
            break;

        case kLTSocket_Event_WriteReady:
            if (!S.test.isReady) {
                // !!! LT-823: sometimes WriteReady happens before SocketReady
                TILT_INFO(tilt, "SOCKET WAS NOT READY\n");
                S.iThread->Sleep(LTTime_Milliseconds(100));
                S.test.isReady = true;
                return;
            }
            // Do we need to indicate reverse (TCP listen) mode?
            if (S.test.listen && hSocket == S.hSocket) {
                if (S.test.listenSent) break;
                S.test.listenSent = true;
                LTNetIpv4Endpoint ep;
                S.iSocket->GetProperty(S.lSocket, "local.endpoint.v4", &ep);
                char atPort[10];
                lt_snprintf(atPort, sizeof(atPort), "@%u", ep.port);
                s32 wlen = S.iSocket->WriteSocket(hSocket, atPort, lt_strlen(atPort));
                if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "write command: @ wlen %ld\n", LT_Ps32(wlen));
                TILT_ASSERT_TRUE(tilt, wlen == (s32)lt_strlen(atPort) , "write command failed, wlen=%d", LT_Ps32(wlen));
                break;
            }
            // Do we need to indicate it's a read sequence?
            if (S.test.isRead) {
                if (S.test.commandSent) break;
                S.test.commandSent = true;
                s32 wlen = S.iSocket->WriteSocket(hSocket, S.test.command, 1);
                if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "write command: %s wlen %ld\n", S.test.command, LT_Ps32(wlen));
                TILT_ASSERT_TRUE(tilt, wlen == 1 , "write command failed, wlen=%d", LT_Ps32(wlen));
                break;
            }
            // Must be write sequence:
            if (S.test.bytesLeft > 0) {
                const s32 max_len  = lt_strlen(S.buffer); // don't include null terminator
                while (S.test.bytesLeft > 0) {
                    s32 slen = S.test.bytesLeft;
                    if (slen > max_len) slen = max_len;
                    if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "write cmd: %s slen: %4ld len: %4ld\n", S.test.command, LT_Ps32(slen), LT_Ps32(len));
                    s32 wlen = S.iSocket->WriteSocket(hSocket, S.buffer, slen);
                    if (wlen < 0) {
                        TILT_REPORT_FAILURE(tilt, "WriteSocket(%d) failed", LT_Ps32(slen));
                        break;
                    }
                    if (wlen == 0) break;

                    S.test.bytesTotal += wlen;
                    TILT_ASSERT_TRUE(tilt, slen >= wlen , "unexpected socket write length expected <= %d, got %d", LT_Ps32(slen), LT_Ps32(wlen));
                    if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "slen: %4ld wlen: %4ld total: %lu\n", LT_Ps32(slen), LT_Ps32(wlen), LT_Pu32(S.test.bytesTotal));
                    S.test.bytesLeft -= wlen;
                }

                break;
            }
            if (DEBUG_SHOW_PROGRESS) TILT_DEBUG(tilt, "command %s: wrote %d bytes", S.test.command, S.test.bytesTotal);
            if (S.test.bytesLeft == 0) {
                TILT_ASSERT_TRUE(tilt, S.test.bytesTotal == S.test.bytesExpected, "bad write length");
                S.test.bytesLeft = -1;
                //SIGNAL_TEST;  // move this to READ section even for writes
                break;
            }
            TILT_INFO(tilt, "ignoring kLTSocket_Event_WriteReady event");
            break;

        case kLTSocket_Event_ReadReady:
            if (!S.test.isReady) {
                // LT-823: sometimes ReadReady happens before SocketReady
                TILT_INFO(tilt, "SOCKET WAS NOT READY\n");
                S.iThread->Sleep(LTTime_Milliseconds(100));
                S.test.isReady = true;
            }
            // Is it a response to write test?
            if (!S.test.isRead) {
                len = S.iSocket->ReadSocket(hSocket, &S.test.response, sizeof(S.test.response) - 1);
                if (len > 0) {
                    S.test.response[len] = 0;
                    if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "write command response: %s\n", S.test.response);
                    TILT_ASSERT_TRUE(tilt, S.test.response[0] == S.test.command[0], "command response mismatch");
                    u32 byteCount = lt_strtou32(S.test.response+2, NULL, 10);
                    TILT_ASSERT_TRUE(tilt, S.test.bytesExpected == byteCount, "incorrect byte count");
                    // ToDo: Add a checksum from the peer here
                    SIGNAL_TEST;
                }
                break;
            }
            // It's a read test:
            if (!S.test.bytesExpected) break;
            do {
                len = S.iSocket->ReadSocket(hSocket, S.buffer, BUFFER_SIZE);
                if (len > 0) {
                    S.test.bytesTotal += len;
                    CheckSum(S.buffer, len);
                }
                if (DEBUG_SHOW_PROGRESS) TILT_DEBUG(tilt, "read len: %4ld total: %lu\n", LT_Ps32(len), LT_Pu32(S.test.bytesTotal));
            } while (len > 0);
            if (S.test.bytesTotal && S.test.bytesTotal >= S.test.bytesExpected) {
                TILT_ASSERT_TRUE(tilt, S.test.bytesTotal == S.test.bytesExpected, "bad read length");
                S.test.bytesExpected = 0;
                u32 check = 0;
                switch (S.test.command[0]) {
                    case 'a': check = 97;      break;
                    case 'b': check = 5750;    break;
                    case 'c': check = 57600;   break;
                    case 'd': check = 577000;  break;
                    case 'e': check = 5780000; break;
                }
                TILT_ASSERT_TRUE(tilt, S.test.checkSum == check, "read checksum mismatch");
                if (S.test.checkSum != check) TILT_INFO(tilt, "checksum got: %ld expected: %ld\n", LT_Pu32(S.test.checkSum), LT_Pu32(check));
                SIGNAL_TEST;
            }
            break;
            
        case kLTSocket_Event_Connected:
            if (hSocket == S.hSocket) break; // Ignore outgoing connect events
            // listen == 1: keep listen socket open for multiple connects
            // listen == 2: close listen socket after connect
            if (S.test.listen == 2) { 
                CloseListenSocket(tilt);
            }
            S.cSocket = hSocket;
            //OnSockEvent(hSocket, kLTSocket_Event_WriteReady, clientData);
            if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "remote connected on socket %x", hSocket);
            break;

        case kLTSocket_Event_Disconnected:
            if      (hSocket == S.hSocket) { S.hSocket = 0; }
            else if (hSocket == S.lSocket) { S.lSocket = 0; }
            else if (hSocket == S.cSocket) { S.cSocket = 0; }
            else { TILT_INFO(tilt, "disconnect received on unknown socket %x", hSocket); }
            lt_destroyhandle(hSocket);
            if (DEBUG_SHOW_DETAIL) TILT_DEBUG(tilt, "disconnected");
            break;

        case kLTSocket_Event_Error:
            TILT_REPORT_FAILURE(tilt, "%s socket error (is the PEER Python script running?)", S.test.proto);
            SIGNAL_TEST;
            break;

        default:
            break;
    }
}

static void OnDnsEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    Tilt *tilt = clientData;

    u32 serverAddr = 0;
    bool IP_OK = false;
    switch (event) {
        case kLTSocket_Event_DnsResolved:
            if (S.iSocket->GetProperty(hSocket, "dns.addr.v4", &serverAddr)) {
                IP_OK = !S.iUtilityIPAddress->IsZero(serverAddr)
                    &&  !S.iUtilityIPAddress->IsBroadcast(serverAddr)
                    &&  !S.iUtilityIPAddress->IsEqual(serverAddr, LTIPAddress_Loopback);
                if (DEBUG_SHOW_DETAIL) {
                    TILT_DEBUG(tilt, "DNS: " DNS_HOST " -> "LT_PRIIP,
                                 LT_PLT_IP(serverAddr));
                }
            }
            break;
        default:
            IP_OK = false;
            break;
    }

    if (IP_OK) {
        SIGNAL_TEST;
    } else {
        TILT_REPORT_FAILURE(tilt, "Error event=0x%x DNS: '" DNS_HOST "' -> " LT_PRIIP,
                            event, LT_PLT_IP(serverAddr));
    }
    lt_destroyhandle(hSocket);
}

static void OnMulEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    Tilt *tilt = clientData;
    u32 n;
    if (DEBUG_SHOW_EVENTS) TILT_DEBUG(tilt, "TCP socket<%lx> event: %x (%s)",
                                        LT_PLT_HANDLE(hSocket), event, S.doneSignal);

    switch (event) {
        
        case kLTSocket_Event_WriteReady:
            if (S.test.isReady == true) break; // Done opening sockets
            
            // Open next socket
            for (n = 0 ; n < S.test.sockCount ; n++) {
                if (S.sockets[n] == LTHANDLE_INVALID) {
                    S.sockets[n] = S.iNetCore->OpenSocket(
                        S.hTransport, S.sockSpec, OnMulEvent, (void*)tilt);

                    // Allow a soft-fail for the last two sockets
                    if (!S.sockets[n] && n > 8) { // LwIP has 16 sockets, Ranger has 10 sockets
                        TILT_WARNING(tilt, "Failed to open the last few sockets (#%d/%d) - I'll allow it",
                                     n + 1, S.test.sockCount);
                        S.test.sockCount = n;
                        break;
                    }
                    
                    TILT_ASSERT_TRUE(tilt, S.sockets[n], "Failed to open socket #%d/%d",
                                     n + 1, S.test.sockCount);
                    break; // for() loop
                }
            }
            if (n == S.test.sockCount) {
                // All sockets are now ready. Write to the first one
                S.test.isReady = true;
                s32 wlen = S.iSocket->WriteSocket(S.sockets[0], "A", 1);
                TILT_ASSERT_TRUE(tilt, wlen == 1 , "Socket[0] write error, wlen = %d",
                                 LT_Ps32(wlen));
            }
            break;
            
        case kLTSocket_Event_ReadReady:
            S.iSocket->ReadSocket(hSocket, &S.test.response, sizeof(S.test.response) - 1);
            break;
           
        case kLTSocket_Event_Disconnected:
            for (n = 0 ; n < S.test.sockCount ; n++) {
                if (S.sockets[n] == hSocket) {
                    S.sockets[n] = LTHANDLE_INVALID;
                    break;
                }
            }
            if (n == S.test.sockCount) {
                TILT_INFO(tilt, "disconnect received on unknown socket %x", hSocket);
            }
            lt_destroyhandle(hSocket);

            // Find the next open socket and write to it
            for (n = 0 ; n < S.test.sockCount ; n++) {
                if (S.sockets[n] != LTHANDLE_INVALID) {
                    s32 wlen = S.iSocket->WriteSocket(S.sockets[n], "A", 1);
                    TILT_ASSERT_TRUE(tilt, wlen == 1 , "Socket[%d] write error, wlen = %d",
                                     LT_Pu32(n), LT_Ps32(wlen));
                    break;
                }
            }
            // Check if all sockets are closed now so we can report a success
            if (n == S.test.sockCount) {
                lt_free(S.sockets);
                S.sockets = NULL;
                if (S.test.isReady) {
                    SIGNAL_TEST;
                } else {
                    TILT_REPORT_FAILURE(tilt, "TCP socket closed (is the PEER Python script running?)");
                }
            }
            break;

        case kLTSocket_Event_Error:
            TILT_REPORT_FAILURE(tilt, "TCP socket error (is the PEER Python script running?)");
            break;

        default:
            break;
    }
}

static void OpenMultipleSockets(Tilt *tilt, u32 sockCount) {
    u32 n;
    S.test = (TestVars) {
        .sockCount     = sockCount,
        .proto         = "tcp",
    };
    S.sockSpec[0] = 0;
    char *e;
    e = AppendStr(S.sockSpec, "tcp ip: ");
    e = AppendStr(e, S.peerAddr);
    e = AppendStr(e, " port: ");
    e = AppendStr(e, S.peerPort);

    TILT_INFO(tilt, "Open %d Sockets: %s", S.test.sockCount, S.sockSpec);

    S.sockets = lt_malloc(S.test.sockCount * sizeof(LTSocket));
    TILT_ASSERT_TRUE(tilt, S.sockets, "lt_malloc(count * sizeof(LTSocket)) failed");
    for (n = 0 ; n < S.test.sockCount ; n++) S.sockets[n] = LTHANDLE_INVALID;

    S.sockets[0] = S.iNetCore->OpenSocket(S.hTransport, S.sockSpec, OnMulEvent, (void*)tilt);
    TILT_ASSERT_TRUE(tilt, S.sockets[0], "Failed to open socket #%d/%d", 1, sockCount);
    S.iSocket = lt_gethandleinterface(ILTSocket, S.sockets[0]);

    WAIT_TEST(5);
    // don't put anything here, nor in the function that calls this
}

static void SendData(Tilt *tilt, char cmd, u32 length, char *proto) { PrepTest(tilt, cmd, length, false, 0, proto); }
static void RecvData(Tilt *tilt, char cmd, u32 length, char *proto) { PrepTest(tilt, cmd, length, true, 0, proto);  }

static void Test_TcpSend0(Tilt *tilt) { SendData(tilt, 'A',       1, "tcp"); }
static void Test_TcpSend2(Tilt *tilt) { SendData(tilt, 'B',     100, "tcp"); }
static void Test_TcpSend3(Tilt *tilt) { SendData(tilt, 'C',    1000, "tcp"); }
static void Test_TcpSend4(Tilt *tilt) { SendData(tilt, 'D',   10000, "tcp"); }
static void Test_TcpSend5(Tilt *tilt) { SendData(tilt, 'E',  100000, "tcp"); }

static void Test_TcpRecv0(Tilt *tilt) { RecvData(tilt, 'a',       1, "tcp"); }
static void Test_TcpRecv2(Tilt *tilt) { RecvData(tilt, 'b',     100, "tcp"); }
static void Test_TcpRecv3(Tilt *tilt) { RecvData(tilt, 'c',    1000, "tcp"); }
static void Test_TcpRecv4(Tilt *tilt) { RecvData(tilt, 'd',   10000, "tcp"); }
static void Test_TcpRecv5(Tilt *tilt) { RecvData(tilt, 'e',  100000, "tcp"); }

static void Test_TcpListen0(Tilt *tilt) { PrepTest(tilt, 'D', 10000, false, 1, "tcp"); }
static void Test_TcpListen1(Tilt *tilt) { PrepTest(tilt, 'd', 10000, true,  1, "tcp"); }
static void Test_TcpListen2(Tilt *tilt) { PrepTest(tilt, 'D', 10000, false, 2, "tcp"); }
static void Test_TcpListen3(Tilt *tilt) { PrepTest(tilt, 'd', 10000, true,  2, "tcp"); }

static void Test_DnsIPv4(Tilt *tilt) { OpenDns(tilt); }
static void Test_MulSock(Tilt *tilt) { OpenMultipleSockets(tilt, 16); }

static void Test_UdpSend0(Tilt *tilt) { SendData(tilt, 'A',       1, "udp"); }
static void Test_UdpSend2(Tilt *tilt) { SendData(tilt, 'B',     100, "udp"); }
static void Test_UdpSend3(Tilt *tilt) { SendData(tilt, 'C',    1000, "udp"); }

static void Test_UdpRecv0(Tilt *tilt) { RecvData(tilt, 'a',       1, "udp"); }
static void Test_UdpRecv2(Tilt *tilt) { RecvData(tilt, 'b',     100, "udp"); }
static void Test_UdpRecv3(Tilt *tilt) { RecvData(tilt, 'c',    1000, "udp"); }

//***** Tilt Test Section ******************************************************

static void BeforeAllTestsHook(Tilt *tilt) {
    S = (struct Statics) {
        .iCore   = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore())
    };
    S.peerAddr = tilt->API->GetProperty("PEER_ADDR", PEER_ADDR);
    TILT_EXPECT_TRUE(tilt, S.peerAddr != NULL, "Cannot get Peer Address property");
    S.peerPort = tilt->API->GetProperty("PEER_PORT", PEER_PORT);
    TILT_EXPECT_TRUE(tilt, S.peerPort != NULL, "Cannot get Peer Port property");

    S.buffer = lt_malloc(BUFFER_SIZE);
    TILT_EXPECT_TRUE(tilt, S.buffer != NULL, "Cannot allocate buffer");
}

static void AfterAllTestsHook(Tilt *tilt) {
    LT_UNUSED(tilt);
    CloseListenSocket(tilt);
    if (S.iNetMonitor) S.iNetMonitor->NoStatusChange(NetMonitorStatusChange);

    // Nulls are okay for any of these
    lt_destroyhandle(S.hTransport);
    lt_closelibrary(S.iNetMonitor);
    lt_closelibrary(S.iUtilityIPAddress);
    lt_closelibrary(S.iNetCore);
    lt_free(S.buffer);
}

static void AfterTestHook(Tilt *tilt) {
    if (S.hSocket) {
        lt_destroyhandle(S.hSocket);
        S.hSocket = 0;
        TILT_INFO(tilt, "destroyed socket");
    }
    if (S.cSocket) {
        lt_destroyhandle(S.cSocket);
        S.cSocket = 0;
        TILT_INFO(tilt, "destroyed listen child socket");
    }
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTestsHook,
    .AfterAllTests  = AfterAllTestsHook,
    .AfterTest      = AfterTestHook
};

#define LAZY_AF_TEST_DESCRIPTION(name) { name, #name, #name, 0 }

static const TiltEngineTest s_tests[] = {
    TILT_TEST_SECTION("General Operations"),
    LAZY_AF_TEST_DESCRIPTION(Test_OpenNetMonitor),
    LAZY_AF_TEST_DESCRIPTION(Test_OpenNetCore),
    LAZY_AF_TEST_DESCRIPTION(Test_OpenUtilityIPAddress),
    LAZY_AF_TEST_DESCRIPTION(Test_TransportOpenGood),

    TILT_TEST_SECTION("TCP Send Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpSend0),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpSend2),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpSend3),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpSend4),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpSend5),

    TILT_TEST_SECTION("TCP Recv Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpRecv0),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpRecv2),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpRecv3),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpRecv4),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpRecv5),

    TILT_TEST_SECTION("TCP Listen Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpListen0),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpListen1),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpListen2),
    LAZY_AF_TEST_DESCRIPTION(Test_TcpListen3),

    TILT_TEST_SECTION("DNS lookup Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_DnsIPv4),

    TILT_TEST_SECTION("Multiple open sockets"),
    LAZY_AF_TEST_DESCRIPTION(Test_MulSock),

    TILT_TEST_SECTION("UDP Send Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpSend0),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpSend2),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpSend3),

    TILT_TEST_SECTION("UDP Recv Tests"),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpRecv0),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpRecv2),
    LAZY_AF_TEST_DESCRIPTION(Test_UdpRecv3),
};

//***** Library API ************************************************************

static int UnitTestLTNetCoreImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetCoreImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetCoreImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetCore, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetCore, UnitTestLTNetCoreImpl_Run, 1536) LTLIBRARY_DEFINITION;
