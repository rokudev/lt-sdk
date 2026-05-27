/*******************************************************************************
 *
 * LTNetCore Example: Regression Test
 *
 * This is a simple regression test for LTNetCore.
 *
 * To make this test work:
 *
 *      1. Set PEER_IP_ADDR to the address of another LT device that will provide a test endpoint.
 *      2. Run ExampleNetRegesssPeer on another LT device (network must already be up)
 *      3. Run ExampleNetRegressTest (network must already be up)
 *
 * Note: to run these programs, ltopen LTShellNet or LTNetMonitor first!
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

#define INFO "======> "
#define DONE "++++++> "
#define ERRR "!!!!!!> "

static LTCore         * pCore;
static LTNetCore      * pNetCore;
static ILTThread      * iThread;

static LTThread         MainThread;
static LTTransport      hTransport;
static LTSocket         hSocket;

static LTTime           StartTime;
static char             SpecStr[64];
static bool             TransReopened;
static u32              TestData;
static u32              ErrCount;

#define P LT_GetCore()->ConsoleStomp

/*******************************************************************************
** Test Framework
*******************************************************************************/

#define PEER_IP_ADDR "192.168.0.100" // used for UDP test

typedef enum { // Keep in sync with FuncsList below
    TS_TCP,
    TS_UDP,
    TS_ICMP,
    TS_DNS,
    TS_END
} TestType;

typedef struct {  // Socket config for each test
    TestType    type;
    char      * spec;
    char      * request;
} TestConfig;

// Modify this table as needed for local addresses:
static TestConfig TestConfigs[] = {
    {
        TS_TCP,
        "ip: 93.184.216.34 port: 80",          // use IP address for example.com
        "GET / HTTP/1.1\r\nHost: example.com:80\r\nConnection: close\r\n\r\n"
    },
    {
        TS_TCP,
        "tcp host: example.com port: 80",      // use host name
        "GET / HTTP/1.1\r\nHost: example.com:80\r\nConnection: close\r\n\r\n"
    },
    {
        TS_UDP,
        "udp ip: " PEER_IP_ADDR " port: 6789", // local LT device for UDP connection
        "ping"
    },
    {
        TS_ICMP,
        "icmp ip: 192.168.0.1",                // Local WLAN router
        NULL                                   // dynamic IP echo struct
    },
    {
        TS_ICMP,
        "icmp ip: 192.168.1.1",                // Gateway router
        NULL                                   // dynamic IP echo struct
    },
    {
        TS_UDP,
        "udp ip: " PEER_IP_ADDR " port: 6789", // local LT device for UDP connection
        "ping"
    },
    {
        TS_TCP,
        "host: rebol.com port: 80",            // another remote host by name
        "GET / HTTP/1.1\r\nHost: rebol.com:80\r\nConnection: close\r\n\r\n"
    },
    {
        TS_TCP,
        "port: 80 ip: 162.216.18.225",         // another remote host by IP (rebol.com)
        "GET / HTTP/1.1\r\nHost: rebol.com:80\r\nConnection: close\r\n\r\n"
    },
    {
        TS_DNS,
        "dns host: example.com",               // just does lookup
        NULL
    },
    {
        TS_END,
        NULL,
        NULL
    }
};

static TestConfig *ThisTest;
static void NextState(void);

static void TestInfo(LTSocket socket, const char *id) {
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    pSocket->GetSocketSpec(socket, SpecStr, sizeof(SpecStr));
    P(INFO "Socket[%02lx] %-16s -- spec: %s\n", LT_Pu32(socket), id, SpecStr);
}

static void TestFail(const char *id) {
    P(ERRR "%s\n", id);
    iThread->Terminate(MainThread);
}

static char *FormU32(char* buf, u16 max, u32 value) {  // LT has no u32tostr
    char str[12];
    u16 i = 0;
    if (value == 0) {
        if (max > 0) {
            str[0] = '0';
            i = 1;
        }
    } else while (value && i < 11) {
        str[i++] = '0' + value % 10;
        value /= 10;
    }
    while (i > 0 && --max > 0) *buf++ = str[--i];
    *buf = 0;
    return buf;
}

/*******************************************************************************
** TCP Test Functions
*******************************************************************************/

static u8   * RequestBuf  = NULL;  // allocated
static u32    RequestMax  = 512;
static bool   RequestSent = false;
static u8   * ReplyBuf    = NULL;  // allocated
static u32    ReplyMax    = 8*1024;
static u32    ReplyLen    = 0;

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *data);

static bool TestTcpStart(LTSocket socket) {
    LT_UNUSED(socket);
    P(INFO "--- Start TCP Test ---\n");
    RequestSent = false;
    ReplyBuf[0] = 0;
    ReplyLen    = 0;
    hSocket = pNetCore->OpenSocket(hTransport, ThisTest->spec, OnSocketEvent, 0);
    if (hSocket) TestInfo(hSocket, "socket-created");
    else TestFail("TCP socket failure");
    return hSocket != 0;
}

static bool TestTcpDone(LTSocket socket) {
    P(DONE "TCP done\n");
    LTTime diff_time = LTTime_Subtract(pCore->GetKernelTime(), StartTime);
    u64 usec = LTTime_GetMicroseconds(diff_time);
    u64 rate = (ReplyLen * 8 * 1000 / usec);
    P(INFO "Socket[%02lx] done - bytes: %lu usec: %ld kbps: %ld\n", LT_Pu32(socket), LT_Pu32(ReplyLen), (long)usec, (long)rate);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    pSocket->DisconnectSocket(socket);
    pCore->DestroyHandle(socket);
    //LT_GetCore()->ConsoleStompString((char*)ReplyBuf);
    NextState();
    return true; // !!! check something
}

static bool TestTcpWrite(LTSocket socket) {
    if (RequestSent) return false;
    RequestSent = true;
    P(INFO "sending http request\n"); //: %s", ThisTest->request);
    StartTime = pCore->GetKernelTime();
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    pSocket->WriteSocket(socket, ThisTest->request, lt_strlen(ThisTest->request));
    return true;
}

static bool TestTcpRead(LTSocket socket) {
    s32 i = 0;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    for(s32 len = 1; len > 0 && ReplyMax-i-1 > 0; i += len) { // space for terminator
        len = pSocket->ReadSocket(socket, ReplyBuf+i, ReplyMax-i-1);
    }
    ReplyBuf[i] = 0; // in case we want to print it
    ReplyLen += i;
    P(INFO "got http response: %lu bytes\n", LT_Pu32(ReplyLen));
    return true;
}

/*******************************************************************************
** UDP Test Functions
*******************************************************************************/

static u32 PingCount;

static bool TestUdpStart(LTSocket socket) {
    LT_UNUSED(socket);
    P(INFO "--- Start UDP Test ---\n");
    ReplyBuf[0] = 0;
    PingCount  = 0;
    hSocket = pNetCore->OpenSocket(hTransport, ThisTest->spec, OnSocketEvent, 0);
    if (hSocket) TestInfo(hSocket, "created");
    else TestFail("UDP socket failure");
    return hSocket != 0;
}

static bool TestUdpDone(LTSocket socket) {
    P(DONE "UDP done\n");
    //pNetCore->DisconnectSocket(socket);
    pCore->DestroyHandle(socket);
    NextState();
    return true;
}

static bool TestUdpWrite(LTSocket socket) {
    char buf[10] = "ping "; // local on purpose for proving WriteSocket
    FormU32(buf+5, 4, PingCount++);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    if (PingCount >= 8) pSocket->DisconnectSocket(socket);
    P(INFO "Socket[%02lx] sending UDP message: %s\n", LT_Pu32(socket), buf);
    pSocket->WriteSocket(socket, buf, lt_strlen(buf)+1);
    return true;
}

static bool TestUdpRead(LTSocket socket) {
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    s32 len = pSocket->ReadSocket(socket, ReplyBuf, ReplyMax);
    if (len <= 0) return false;
    ReplyBuf[len] = 0;
    P(INFO "Socket[%02lx] got UDP message: %s\n", LT_Pu32(socket), ReplyBuf);
    if (len == 4 && !lt_strcmp((char*)ReplyBuf, "pong")) TestUdpWrite(socket);
    return true;
}

/*******************************************************************************
** ICMP Test Functions
*******************************************************************************/

#define ICMP_ECHO  8
#define ECHO_ID    ('L' | ('T'<< 8)) // LT
#define ECHO_STR   "ROKU"
#define PING_LEN   (sizeof(struct EchoHeader) + sizeof(ECHO_STR)-1)

typedef struct EchoHeader {
    u8  type;
    u8  code;
    u16 chksum;
    u16 id;
    u16 seqno;
} EchoHeader;

static u32           PingCount;
static LTTime        StartTime;

static u16 ChecksumIcmp(const void *dataptr, u32 len) {
    // A refactored lwip_standard_chksum (inet_chksum.c)
    u32 acc = 0;
    u16 src;
    const u8 *dp = (const u8*)dataptr;
    for (; len > 1; len -= 2) {
        src  = *dp++ << 8; // most significant first (network order)
        src |= *dp++;
        acc += src;
    }
    if (len > 0) { // extra byte
        src = *dp << 8;
        acc += src;
    }
    // add deferred carry bits:
    acc = (acc >> 16) + (acc & 0x0000ffffUL);
    if ((acc & 0xffff0000UL) != 0) acc = (acc >> 16) + (acc & 0x0000ffffUL);
    return ~LT_HTONS(acc);
}

static void DumpBytes(const char *label, const u8 *bytes, u32 len) {
    P("%s: ", label);
    for (u32 i = 0; i < len; i++) P("%02x ", bytes[i]);
    P("\n");
}

static void SendPing(LTSocket socket) {
    EchoHeader *echo = (EchoHeader *)RequestBuf;
    *echo = (EchoHeader) { // unspecified fields zero'd (e.g. chksum)
        .type  = ICMP_ECHO,
        .id    = ECHO_ID,
        .seqno = LT_HTONS(PingCount)
    };
    lt_memcpy(RequestBuf + sizeof(EchoHeader), ECHO_STR, sizeof(ECHO_STR)-1);
    echo->chksum = ChecksumIcmp(RequestBuf, PING_LEN);
    StartTime = pCore->GetKernelTime();
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    pSocket->WriteSocket(socket, RequestBuf, PING_LEN);
    P(INFO "Sent ping %lu\n", LT_Pu32(PingCount));
}

static bool CheckPing(const u8 *buf, u32 len) {
    if (len < PING_LEN) return false;
    EchoHeader *e = (EchoHeader *)(buf + 20); // skip IP header
    return (buf[0] == 0x45 && e->type == 0 && e->id == ECHO_ID && e->seqno == LT_HTONS(PingCount));
}

static bool TestIcmpStart(LTSocket socket) { LT_UNUSED(socket);
    P(INFO "--- Start ICMP Test ---\n");
    RequestSent = false;
    ReplyLen    = 0;
    PingCount   = 0;
    hSocket = pNetCore->OpenSocket(hTransport, ThisTest->spec, OnSocketEvent, 0); // !!! get gateway IP
    if (hSocket) TestInfo(hSocket, "icmp");
    else TestFail("ICMP socket failed, terminating");
    return hSocket != 0;
}

static bool TestIcmpDone(LTSocket socket) { LT_UNUSED(socket);
    P(DONE "ICMP done\n");
    pCore->DestroyHandle(socket);
    NextState();
    return true;
}

static bool TestIcmpWrite(LTSocket socket) {
    SendPing(socket);
    return true;
}

static bool TestIcmpRead(LTSocket socket) {
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    u32 len = pSocket->ReadSocket(socket, ReplyBuf, ReplyMax);
    if (len <= 0) return false;
    LTTime diff_time = LTTime_Subtract(pCore->GetKernelTime(), StartTime);
    if (!CheckPing(ReplyBuf, len)) DumpBytes("BAD REPLY", ReplyBuf, len);
    u32 diff = (u32)LTTime_GetMicroseconds(diff_time);
    P(INFO "Ping %2lu: %lu.%03lu msec\n", LT_Pu32(PingCount), LT_Pu32(diff/1000), LT_Pu32(diff%1000));
    if (PingCount++ < 4) SendPing(socket);
    else pSocket->DisconnectSocket(socket);
    return true;
}

/*******************************************************************************
** ICMP Test Functions
*******************************************************************************/

static bool TestDnsStart(LTSocket socket) { LT_UNUSED(socket);
    P(INFO "--- Start DNS Test ---\n");
    hSocket = pNetCore->OpenSocket(hTransport, ThisTest->spec, OnSocketEvent, 0);
    if (hSocket) TestInfo(hSocket, "dns");
    else TestFail("DNS socket failed, terminating");
    return hSocket != 0;
}

static bool TestDnsDone(LTSocket socket) {
    P(DONE "DNS done\n");
    TestInfo(socket, "dns");
    pCore->DestroyHandle(socket);
    NextState();
    return true;
}

/*******************************************************************************
** Test Sequence "State Machine"
*******************************************************************************/

typedef bool (BoolTest)(LTSocket socket);

typedef struct {
    BoolTest  * start;
    BoolTest  * done;
    BoolTest  * write;
    BoolTest  * read;
} TestFuncs_t;

static TestFuncs_t FuncsList[] = {
    {
        TestTcpStart,
        TestTcpDone,
        TestTcpWrite,
        TestTcpRead
    },
    {
        TestUdpStart,
        TestUdpDone,
        TestUdpWrite,
        TestUdpRead
    },
    {
        TestIcmpStart,
        TestIcmpDone,
        TestIcmpWrite,
        TestIcmpRead
    },
    {
        TestDnsStart,
        TestDnsDone,
        NULL,
        NULL
    },
};

static TestFuncs_t TestFuncs;
static u8          TestIndex;
static u16         TestRepeat;

static void InitState(void) {
    TestIndex  = 0;
    TestRepeat = 4;
}

static void NextState(void) {
    ThisTest = &TestConfigs[TestIndex];
    if (ThisTest->type >= TS_END) {
        P(ERRR "END state\n");
        if (!TestRepeat--) {
            iThread->Terminate(MainThread);
            return;
        }
        TestIndex = 0;
        ThisTest = &TestConfigs[TestIndex];
    }
    P(INFO "\n\n<<<<<<<<<<< next-state[%d:%d]: type: %d spec: %s >>>>>>>>>>>\n\n", TestRepeat, TestIndex,
        ThisTest->type, ThisTest->spec);
    TestFuncs = FuncsList[ThisTest->type];
    bool success = TestFuncs.start(0);
    TestIndex++;
    if (!success) NextState();
}

/*******************************************************************************
** Event Handling Functions
*******************************************************************************/

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *data) { LT_UNUSED(data);
    //P(INFO "OnEvent[%02x] event: 0x%02x\n", socket, event);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_DnsResolved:
            TestInfo(socket, "DNS-resolved");
            if (ThisTest->type >= TS_DNS) TestDnsDone(socket);
            else pSocket->ConnectSocket(socket);
            break;

        case kLTSocket_Event_SocketReady:
            TestInfo(socket, "socket-ready");
            break;

        case kLTSocket_Event_Connected:
            TestInfo(socket, "socket-connected");
            break;

        case kLTSocket_Event_WriteReady:
            TestInfo(socket, "write-ready");
            TestFuncs.write(socket);
            break;

        case kLTSocket_Event_ReadReady:
            TestInfo(socket, "read-ready");
            TestFuncs.read(socket);
            break;

        case kLTSocket_Event_Disconnected:
            TestInfo(socket, "socket-disconnect");
            TestFuncs.done(socket);
            break;

        case kLTSocket_Event_Error:
            if (ErrCount++ % 32 == 0) P(INFO "Socket[%02lx] error count: %lu\n", LT_Pu32(socket), LT_Pu32(ErrCount)); // !!! why?
            break;

        default:
            P(INFO "Socket[%02lx] unhandled event: 0x%02x\n", LT_Pu32(socket), event);
            break;
    }
}

static void OnTransportEvent(LTTransport transport, LTTransport_Event event, void *data) {
    P(INFO "Transport[%04lx] Event: 0x%X\n", LT_Pu32(transport), event);
    if (data != &TestData) P(ERRR "Event data does not match\n");
    switch (event) {
        case kLTTransport_Event_Up:
            pNetCore->GetTransportSpec(transport, SpecStr, sizeof(SpecStr));
            P(INFO "Transport[%04lX] up: spec: %s\n", LT_Pu32(transport), SpecStr);
            P(INFO "Transport[%04lX] is-operating: %s\n", LT_Pu32(transport),
                    pNetCore->IsOperating(transport, 0) ? "yes" : "no"); // CheckUp -- more than IsUp!
            if (TransReopened) return;
            TransReopened = true;
            #ifdef TRANSPORT_REOPEN
                // Destroy and open new transport with different spec:
                pCore->DestroyHandle(transport); // CRASH - probably terminate thread bug!!!
                hTransport  = pNetCore->OpenTransport(TranSpecDhcp, OnTransportEvent, &TestData);
                if (!hTransport) {
                    P(ERRR "Cannot open transport: %s\n", TranSpecIp);
                }
            #endif
            break;
        default:
            return;
    }
    NextState();
}

/*******************************************************************************
** Main App Start & Exit
*******************************************************************************/

static bool OnThreadStart(void) {
    P(INFO "Starting...\n");
    pNetCore         = NULL;
    hTransport       = 0;
    TransReopened    = false;
    ErrCount         = 0;
    if (!(RequestBuf = lt_malloc(RequestMax))) return false;
    if (!(ReplyBuf   = lt_malloc(ReplyMax))) return false;
    if (!(pNetCore   = lt_openlibrary(LTNetCore)))  return false;
    if (!(hTransport = pNetCore->OpenTransport(NULL, OnTransportEvent, &TestData))) return false;
    InitState();
    return true;
}

static void OnThreadExit(void) {
    lt_free(ReplyBuf);
    //pCore->DestroyHandle(hTransport); // destroy the socket also
    lt_closelibrary(pNetCore);
}

static int ExampleNetRegressTest_Main(int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    pCore = LT_GetCore();

    MainThread = pCore->CreateThread("RegressTest");
    if (!MainThread) return -1;
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iThread->SetStackSize(MainThread, 3200);
    iThread->Start(MainThread, OnThreadStart, OnThreadExit);
    iThread->WaitUntilFinished(MainThread, LTTime_Seconds(120));
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleNetRegressTest, 1, 2048);
