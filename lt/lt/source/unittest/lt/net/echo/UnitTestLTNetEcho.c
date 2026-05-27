/*******************************************************************************
 * LTNetEcho Unit Test
 *
 * Test the ICMP Echo implementation by sending ICMP Echo Request packets
 * to a specified remote IPv4 address.
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
#include <lt/net/echo/LTNetEcho.h>
#include <lt/core/LTCore.h>

#include <tilt/JiltEngine.h>

enum { knTestPings = 4 };

/*******************************************************************************
 * Test Vars and Data typedefs
 *******************************************************************************/
typedef struct {
    LTNetEcho       *netEchoLib;
    LTNetCore       *netCoreLib;
    LTLibrary       *netMonitor;
    Tilt            *tilt;
    const char      *remoteAddr;
    ILTThread       *iThread;
    LTThread         echoThread;
    u32              nTestPings;
    u32              pingPeriod_mS;
    u32              pingTxCount;
    u32              pingRxCount;
    LTTime           roundTripThreshold_mS;
    u32              testTimeout_mS;    /* Should be > (kNTestPings * kPingPeriod_mS) */
} TestVars;

static JiltEngine  *s_engine;
static TestVars    *s_pTestVars = NULL;

/*******************************************************************************
 * Callback for LTNetEcho->Ping()
 *******************************************************************************/
static void OnPingEvent(LTHandle hPing, u16 id, u16 seqno, void *data) {
    LT_UNUSED(hPing);
    LT_UNUSED(data);
    TestVars *pTestVars = s_pTestVars;
    if (!pTestVars)
        return;

    if (seqno > knTestPings) {
        TILT_DEBUG(pTestVars->tilt, "Unexpected Ping %s %2u", id ? "Rx" : "Tx", seqno);
        return;
    }
    static LTTime TimeSent[knTestPings + 1];
    if (!id) {
        pTestVars->pingTxCount++;
        TimeSent[seqno] = LT_GetCore()->GetKernelTime();
        TILT_DEBUG(pTestVars->tilt, "Tx Ping %2u", seqno);
    } else {
        pTestVars->pingRxCount++;
        LTTime delta = LTTime_Subtract(LT_GetCore()->GetKernelTime(), TimeSent[seqno]);
        u32 uSec = (u32)LTTime_GetMicroseconds(delta);
        TILT_DEBUG(pTestVars->tilt, "Rx Ping %2u: roundtrip T = %ld.%03ld mS",
                                        seqno, LT_Pu32(uSec / 1000), LT_Pu32(uSec % 1000));

        TILT_EXPECT_TRUE(pTestVars->tilt, LTTime_IsGreaterThan(pTestVars->roundTripThreshold_mS, delta),
                            "Ping %d Round Trip Time (%d mS) exceeds Threshold (%d mS)",
                             seqno, uSec / 1000, (u32)LTTime_GetMicroseconds(pTestVars->roundTripThreshold_mS));
    }
}

/*******************************************************************************
 * Echo Thread Init Proc
 *******************************************************************************/
static bool OnEchoThreadInit(void) {
    /* Open Net Echo */
    s_pTestVars->netEchoLib = lt_openlibrary(LTNetEcho);
    TILT_EXPECT_TRUE(s_pTestVars->tilt, s_pTestVars->netEchoLib != NULL,
                        "Failed to open LTNetEcho Lib");
    if (s_pTestVars->netEchoLib == NULL) return false;

    s_pTestVars->iThread->Sleep(LTTime_Milliseconds(100));

    /* Transmit Ping */
    TILT_DEBUG(s_pTestVars->tilt, "PingConfig: address = '%s', count = %d, period_mS = %d",
                  s_pTestVars->remoteAddr, s_pTestVars->nTestPings, s_pTestVars->pingPeriod_mS);
    s_pTestVars->netEchoLib->Ping(0, s_pTestVars->remoteAddr, 1234, s_pTestVars->nTestPings,
                  s_pTestVars->pingPeriod_mS, NULL, 0, OnPingEvent, NULL);

    return true;
}

/*******************************************************************************
 * Wait for Test Completion (or Timeout)
 *******************************************************************************/
static void WaitForTestCompletion(void) {
    TILT_DEBUG(s_pTestVars->tilt, "waiting for Ping callbacks...");
    u32 timeoutElapsed_mS = 0;
    const u32 kPollingPeriod_mS = 10;

    while (s_pTestVars->pingRxCount < s_pTestVars->nTestPings && timeoutElapsed_mS < s_pTestVars->testTimeout_mS) {
        s_pTestVars->iThread->Sleep(LTTime_Milliseconds(kPollingPeriod_mS));    /* Wait between loop conditional tests */
        timeoutElapsed_mS += kPollingPeriod_mS;
    }
    TILT_DEBUG(s_pTestVars->tilt, "T = %d mS. Count: expected = %d, measured(Tx,Rx) = (%d,%d)",
        timeoutElapsed_mS, s_pTestVars->nTestPings, s_pTestVars->pingTxCount, s_pTestVars->pingRxCount);
}

/*******************************************************************************
 * Attempt net connection.
 * - checks for existing Transport Spec.
 * - if no transport exists, open LTNetMonitor (which will attempt connection)
 * - wait until net-up (or timeout)
 * @returns true = connect success, false = connect fail
 *******************************************************************************/
static bool AttemptNetConnect(void) {
    char spec[80] = {};
    s_pTestVars->netCoreLib->GetTransportSpec(0, spec, sizeof(spec));

    if (!spec[0]) {
        s_pTestVars->netMonitor = LT_GetCore()->OpenLibrary("LTNetMonitor");
        if (!s_pTestVars->netMonitor) {
            TILT_WARNING(s_pTestVars->tilt, "could not open LTNetMonitor lib");
            return false;
        }
        /* Wait for LTNetMonitor lib transport to Init */
        const LTTime kConnectTimeout_mS      = LTTime_Milliseconds(15000);
        const LTTime kConnectPollInterval_mS = LTTime_Milliseconds(1000);
        LTTime timer_mS                      = LTTime_Milliseconds(0);

        do {
            s_pTestVars->iThread->Sleep(kConnectPollInterval_mS);
            timer_mS = LTTime_Add(timer_mS, kConnectPollInterval_mS);
            TILT_DEBUG(s_pTestVars->tilt, "Waiting for net connect. t_elapsed: %d mS", LTTime_GetMilliseconds(timer_mS));
        } while ((s_pTestVars->netCoreLib->IsOperating(0, 0) == -1) && LTTime_IsLessThan(timer_mS, kConnectTimeout_mS));

        if (s_pTestVars->netCoreLib->IsOperating(0, 0) == -1) { return false; }
    }
    return true;
}

/**********************************************************************************
 * Run PING with specified remote address, wait for acknowledgement (echo), verify.
 **********************************************************************************/
static void RunNetEchoTest(Tilt *tilt) {
    const char *peer_addr = tilt->API->GetProperty("PEER_ADDR", "");
    TILT_ASSERT_TRUE(tilt, lt_strlen(peer_addr) > 0, "No peer IPv4 address specified.");

    TestVars *pTestVars = lt_malloc(sizeof(TestVars));
    TILT_ASSERT_TRUE(tilt, pTestVars != NULL, "could not allocate memory for test Vars");
    lt_memset(pTestVars, 0, sizeof(TestVars));
    s_pTestVars = pTestVars;

    /* Init Test Vars */
    s_pTestVars->tilt = tilt;
    s_pTestVars->remoteAddr = peer_addr;
    s_pTestVars->nTestPings = knTestPings;
    s_pTestVars->pingPeriod_mS = 250;
    s_pTestVars->testTimeout_mS = 5000;
    s_pTestVars->roundTripThreshold_mS = LTTime_Milliseconds(2000);

    /* Open "LTNet*" libraries */
    s_pTestVars->netCoreLib = lt_openlibrary(LTNetCore);
    TILT_ASSERT_TRUE(tilt, s_pTestVars->netCoreLib != NULL, "Failed to open LTNetCore Lib");
    s_pTestVars->iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    TILT_ASSERT_TRUE(tilt, s_pTestVars->iThread != NULL, "Failed to get Thread interface");

    /* Check for Available Net */
    if (!AttemptNetConnect()) {
        TILT_REPORT_CANNOT_RUN(tilt, "could not connect to transport");
        return;
    }

    /* Start Echo Thread */
    s_pTestVars->echoThread = LT_GetCore()->CreateThread("EchoPing");
    TILT_ASSERT_TRUE(tilt, s_pTestVars->echoThread != 0, "Failed to create echo thread");
    s_pTestVars->iThread->SetStackSize(s_pTestVars->echoThread, 2048);
    s_pTestVars->iThread->Start(s_pTestVars->echoThread, OnEchoThreadInit, NULL);

    WaitForTestCompletion();
    /* Verify Ping Counts */
    if (s_pTestVars->pingRxCount > 0 && s_pTestVars->pingRxCount != s_pTestVars->nTestPings) {
        TILT_WARNING(tilt, "some pings lost - expected: %d pings. measured(Tx, Rx): (%d, %d)",
                         s_pTestVars->nTestPings, s_pTestVars->pingTxCount, s_pTestVars->pingRxCount);
    }
    TILT_EXPECT_TRUE(tilt,
        (s_pTestVars->nTestPings == s_pTestVars->pingTxCount) && (s_pTestVars->pingRxCount > 0),
                                        "expected: %d pings. measured(Tx, Rx): (%d, %d)",
        s_pTestVars->nTestPings, s_pTestVars->pingTxCount, s_pTestVars->pingRxCount);

    /* Cleanup will be performed by the after test hook (see above). */
}

static void AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);

    TestVars *pTestVars = s_pTestVars;
    /* Nullify */
    s_pTestVars = NULL;

    if (pTestVars->netMonitor) {
        lt_closelibrary(pTestVars->netMonitor);
    }
    if (pTestVars->netEchoLib) {
        lt_closelibrary(pTestVars->netEchoLib);
    }
    if (pTestVars->netCoreLib) {
        lt_closelibrary(pTestVars->netCoreLib);
    }

    /* Destroy Thread */
    pTestVars->iThread->Destroy(pTestVars->echoThread);

    /* Free Test Data */
    lt_free(pTestVars);
}

static const TiltEngineTestHooks s_hooks = {
    .AfterTest = AfterTest,
};

static const TiltEngineTest s_tests[] = {
    { RunNetEchoTest, "NetEcho", "Runs the Net Echo test. Targeted at specified ping remote address", 0 },
};

static int UnitTestLTNetEchoImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetEchoImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetEchoImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetEcho, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetEcho, UnitTestLTNetEchoImpl_Run, 1536) LTLIBRARY_DEFINITION;
