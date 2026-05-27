/*******************************************************************************
 * LTNetIperf3 Unit Test
 *
 * Test the Iperf3 server/ client implementation with Iperf3
 *
 * This set requires that the remote endpoint is passed to TILT in the
 * "PEER_ADDR" property.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/iperf3/LTNetIperf3.h>
#include <lt/core/LTCore.h>

#include <tilt/JiltEngine.h>

enum {
    kDefaultIPerfPort  = 5201,
    kMaxPort           = 65535,
    kWriteSize         = 1460,    // bytes
    kTestDuration      = 10,      // seconds
    kReportInterval    =  1,      // seconds
    kSocketSpecSize    = 128,     // bytes
    kUdpBitrate        = 1048576, // bits/sec
    kIpAddrSize        = 46,      // bytes
    kUdpBitRateUnlimit = LT_U32_MAX,
};
static const LTNetIperf3_Parameters LTNetIperf3_ParametersDefault = {
    .streams = 1,
    .port    = kDefaultIPerfPort,
    .udp     = false,
    .time    = kTestDuration,
    .bitrate = kUdpBitRateUnlimit,
};

typedef struct NetIperfCBParam {
    LTThread       hThread;
    Tilt          *tilt;
    const char    *ipaddr;
    LTNetIperf3_Parameters param;
    bool           isComplete;
    bool           isTimeout;
    bool           isError;
    bool           isServerReady;
} NetIperfCBParam_t;

static struct {
    LTNetIperf3   *netIperf;
    ILTThread     *iThread;
} S;

static JiltEngine *s_engine;

static const char *NetIperfUtil_GetPEER_ADDR(Tilt *tilt) {
    const char * peer_addr = tilt->API->GetProperty("PEER_ADDR", "");
    if (lt_strlen(peer_addr) == 0) {
        TILT_REPORT_FAILURE(tilt, "No peer IPv4 address specified.");
    }
    return peer_addr;
}

static void NetIperfUtil_ClientCallback(const char *report, void *clientData) {
    if (clientData == NULL) {
        return;
    }
    NetIperfCBParam_t *pClientData = (NetIperfCBParam_t *)clientData;
    pClientData->isComplete = (lt_strncmp(report, "results", 7) == 0);
    pClientData->isError = (lt_strncmp(report, "Error:", 6) == 0);
}

static void NetIperfUtil_ServerCallback(const char *report, void *clientData) {
    NetIperfCBParam_t *pClientData = (NetIperfCBParam_t *)clientData;
    pClientData->isServerReady = (lt_strncmp(report, "server_ready", 12) == 0);
}

static void NetIperfUtil_TimerProc(void *clientData) {
    NetIperfCBParam_t *pClientData = (NetIperfCBParam_t *)clientData;
    if (pClientData) {
        pClientData->isTimeout = true;
    }
    S.iThread->KillTimer(pClientData->hThread , NetIperfUtil_TimerProc, pClientData);
}

static bool RunNetIperfClient(Tilt *tilt, NetIperfCBParam_t *pClientData) {
    LT_UNUSED(tilt); // reserved
    S.netIperf->KillClient("Unittest");
    S.iThread->Sleep(LTTime_Milliseconds(1500));
    S.netIperf->RunClient(pClientData->ipaddr, &pClientData->param, NetIperfUtil_ClientCallback, pClientData);
    S.iThread->SetTimer(pClientData->hThread , LTTime_Seconds(pClientData->param.time + 1), NetIperfUtil_TimerProc, NULL, pClientData);
    while(!pClientData->isComplete && !pClientData->isTimeout && !pClientData->isError) {
        S.iThread->Sleep(LTTime_Milliseconds(100));
    }
    S.iThread->KillTimer(pClientData->hThread , NetIperfUtil_TimerProc, pClientData);
    return (pClientData->isComplete && !pClientData->isTimeout && !pClientData->isError);
}

static void RunNetIperfTcpClient(Tilt *tilt) {
    NetIperfCBParam_t *pClientData = lt_malloc(sizeof(NetIperfCBParam_t));
    lt_memset(pClientData, 0, sizeof(NetIperfCBParam_t));
    pClientData->param = LTNetIperf3_ParametersDefault;
    pClientData->tilt = tilt;
    pClientData->hThread = S.iThread->GetCurrentThread();
    pClientData->ipaddr = NetIperfUtil_GetPEER_ADDR(tilt);

    TILT_EXPECT_TRUE(tilt, RunNetIperfClient(tilt, pClientData), "IperfTCP Client Failure");
    lt_free(pClientData);
    return;
}

static void RunNetIperfUdpClient(Tilt *tilt) {
    NetIperfCBParam_t *pClientData = lt_malloc(sizeof(NetIperfCBParam_t));
    lt_memset(pClientData, 0, sizeof(NetIperfCBParam_t));
    pClientData->param = LTNetIperf3_ParametersDefault;
    pClientData->param.udp = true;
    pClientData->tilt = tilt;
    pClientData->hThread = S.iThread->GetCurrentThread();
    pClientData->ipaddr = NetIperfUtil_GetPEER_ADDR(tilt);

    TILT_EXPECT_TRUE(tilt, RunNetIperfClient(tilt, pClientData), "IperfUDP Client Failure");
    lt_free(pClientData);
}

static void RunNetIperfUdpMultiClient(Tilt *tilt) {
    NetIperfCBParam_t *pClientData = lt_malloc(sizeof(NetIperfCBParam_t));
    lt_memset(pClientData, 0, sizeof(NetIperfCBParam_t));
    pClientData->param = LTNetIperf3_ParametersDefault;
    pClientData->param.udp = true;
    pClientData->param.streams = 2;
    pClientData->tilt = tilt;
    pClientData->hThread = S.iThread->GetCurrentThread();
    pClientData->ipaddr = NetIperfUtil_GetPEER_ADDR(tilt);

    TILT_EXPECT_TRUE(tilt, RunNetIperfClient(tilt, pClientData), "IperfUDP Multi Stream Client Failure");
    lt_free(pClientData);
}

static void RunNetIperfTcpMultiClient(Tilt *tilt) {
    NetIperfCBParam_t *pClientData = lt_malloc(sizeof(NetIperfCBParam_t));
    lt_memset(pClientData, 0, sizeof(NetIperfCBParam_t));
    pClientData->param = LTNetIperf3_ParametersDefault;
    pClientData->param.udp = false;
    pClientData->param.streams = 2;
    pClientData->tilt = tilt;
    pClientData->hThread = S.iThread->GetCurrentThread();
    pClientData->ipaddr = NetIperfUtil_GetPEER_ADDR(tilt);

    TILT_EXPECT_TRUE(tilt, RunNetIperfClient(tilt, pClientData), "IperfTCP Multi Stream Client Failure");
    lt_free(pClientData);
}

static void RunNetIperfTcpServer(Tilt *tilt) {
    LT_UNUSED(tilt);
    LTNetIperf3_Parameters params = LTNetIperf3_ParametersDefault;
    NetIperfCBParam_t *pClientData = lt_malloc(sizeof(NetIperfCBParam_t));
    lt_memset(pClientData, 0, sizeof(NetIperfCBParam_t));
    S.netIperf->RunServer(&params, NetIperfUtil_ServerCallback, pClientData);
    S.iThread->Sleep(LTTime_Milliseconds(1000));
    TILT_EXPECT_TRUE(tilt, (pClientData->isServerReady), "IperfTCP Server Failure");
    S.netIperf->KillServer(NULL);

    lt_free(pClientData);
}

static void BeforeAllTests(Tilt *tilt) {
    lt_memset(&S, 0, sizeof(S));
    S.iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    S.netIperf = lt_openlibrary(LTNetIperf3);
    TILT_EXPECT_TRUE(tilt, S.netIperf != NULL, "Cannot open LTNetIperf3");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(S.netIperf);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { RunNetIperfTcpClient,      "TCPIperfClient",      "Runs the Iperf3 TCP Client test. Connecting to Iperf3 server at PEER_ADDR",           0 },
    { RunNetIperfUdpClient,      "UDPIperfClient",      "Runs the Iperf3 UDP Client test. Connecting to Iperf3 server at PEER_ADDR",           0 },
    { RunNetIperfTcpMultiClient, "TCPIperfMultiClient", "Runs the Iperf Client test with 2 streams. Connecting to Iperf3 server at PEER_ADDR", 0 },
    { RunNetIperfUdpMultiClient, "UDPIperfMultiClient", "Runs the Iperf Client test with 2 streams. Connecting to Iperf3 server at PEER_ADDR", 0 },
    { RunNetIperfTcpServer,      "TCPIperfServer",      "Runs the Iperf3 TCP Server test.",                                                    0 },
};

static int UnitTestLTNetIperf3Impl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetIperf3Impl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetIperf3Impl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetIperf3, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetIperf3, UnitTestLTNetIperf3Impl_Run, 1536) LTLIBRARY_DEFINITION;
