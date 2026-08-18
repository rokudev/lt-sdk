/*******************************************************************************
 *
 * UnitTestLTDeviceWiFiMonitor
 * ---------------------------
 *
 * Monitor-mode smoke test.  Pure Jilt (TILT 2.0).
 *
 *   ltrun UnitTestLTDeviceWiFiMonitor [MON_CHANNEL=165] [MON_SECS=5]
 *
 * Purpose: prove the BL61x monitor-mode RX path actually delivers raw 802.11
 * management frames (beacons / probe responses) to LTWiFi_MonitorRxCallback.
 * EnterMonitorMode is implemented but otherwise has zero callers, so this is
 * the gating check for the "directed probe + monitor RX"
 * find-host path (see hostlink-unified plan, "Host-roam gap").
 *
 * Procedure:
 *   1. Open LTDeviceWiFi, wait for Up, Disconnect (monitor is mutually
 *      exclusive with an STA connection).
 *   2. EnterMonitorMode(MON_CHANNEL).
 *   3. Listen MON_SECS; count frames by 802.11 type/subtype in the RX callback.
 *   4. Assert at least one frame arrived (beacons should be plentiful on a live
 *      channel); report beacon / probe-response / other counts.
 *   5. ExitMonitorMode.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <tilt/JiltEngine.h>

#define DEFAULT_MON_CHANNEL  165   /* 5 GHz: 2.4 GHz STA/PHY is broken on BL618D (plan Risk #9) */
#define DEFAULT_MON_SECS     5

/** Module state *************************************************************/

static JiltEngine    *s_engine;
static LTDeviceWiFi  *s_pWiFi;
static Tilt          *s_currentTilt;

static bool           s_deviceIsUp;
static bool           s_upDeferred;
static u8             s_monChannel;
static u32            s_monSecs;

/* Touched from the (driver-thread) RX callback — keep volatile and do only
 * trivial counting there. */
static volatile u32   s_frameCount;
static volatile u32   s_beaconCount;
static volatile u32   s_probeRespCount;
static volatile u32   s_otherMgmtCount;
static volatile bool  s_haveFirstBssid;
static u8             s_firstBssid[6];
static volatile bool  s_listenDeferred;

/* Set from the driver-context onStarted completion callback: async
 * EnterMonitorMode result (the return value only means "request accepted"). */
static volatile bool  s_monStarted;
static volatile bool  s_monStartOk;

/** Callbacks ****************************************************************/

static void OnWiFiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);
    if (status == kLTDeviceWiFi_Status_Up) {
        s_deviceIsUp = true;
        if (s_currentTilt) TILT_INFO(s_currentTilt, "wifi: Up");
        if (s_upDeferred) {
            s_upDeferred = false;
            s_engine->API->SignalTestCompletion(s_engine, NULL);
        }
    }
}
/* Async EnterMonitorMode completion.  Driver context: stage flags only. */
static void OnMonitorStarted(bool ok, void *ctx) {
    LT_UNUSED(ctx);
    s_monStartOk = ok;
    s_monStarted = true;
}


/* Raw 802.11 frame from monitor mode.  Runs in driver context: count only.
 * frame[0] = frame-control byte: bits 2-3 = type (0 = management),
 * bits 4-7 = subtype (8 = beacon, 5 = probe response).  For mgmt frames
 * addr3 (BSSID) is at byte offset 16. */
static void OnMonitorRx(void const *frame, u16 len, s8 rssi, void *ctx) {
    LT_UNUSED(rssi);
    LT_UNUSED(ctx);
    const u8 *f = (const u8 *)frame;
    if (!f || len < 1) return;

    s_frameCount++;
    if (s_listenDeferred) {
        s_listenDeferred = false;
        s_engine->API->SignalTestCompletion(s_engine, NULL);
    }

    u8 fc      = f[0];
    u8 type    = (u8)((fc >> 2) & 0x3);
    u8 subtype = (u8)((fc >> 4) & 0xf);

    if (type == 0) {                       /* management */
        if (subtype == 8)      s_beaconCount++;
        else if (subtype == 5) s_probeRespCount++;
        else                   s_otherMgmtCount++;

        if (!s_haveFirstBssid && len >= 22) {
            lt_memcpy(s_firstBssid, &f[16], 6);
            s_haveFirstBssid = true;
        }
    }
}

/** Tests ********************************************************************/

static void TestOpenDevice(Tilt *tilt) {
    s_currentTilt = tilt;
    s_pWiFi = lt_openlibrary(LTDeviceWiFi);
    TILT_ASSERT_TRUE(tilt, s_pWiFi != NULL, "cannot open LTDeviceWiFi");
    if (!s_pWiFi) return;
    /* Monitor mode is mutually exclusive with an STA connection. */
    s_pWiFi->SetOption("autojoin", 0);
    s_pWiFi->Disconnect();
    s_pWiFi->OnStatusChange(OnWiFiStatus, NULL, NULL);
}

static void TestWiFiUp(Tilt *tilt) {
    s_currentTilt = tilt;
    if (s_deviceIsUp) {
        TILT_INFO(tilt, "device already up");
        return;
    }
    s_upDeferred = true;
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(10), NULL, NULL);
}

static void TestEnterMonitor(Tilt *tilt) {
    s_currentTilt = tilt;
    TILT_ASSERT_TRUE(tilt, s_deviceIsUp, "device must be Up before monitor");

    s_frameCount = s_beaconCount = s_probeRespCount = s_otherMgmtCount = 0;
    s_haveFirstBssid = false;
    s_monStarted = false;
    s_monStartOk = false;

    LTWiFi_MonitorConfig cfg;
    lt_memset(&cfg, 0, sizeof(cfg));
    cfg.channel      = s_monChannel;
    cfg.callback     = OnMonitorRx;
    cfg.ctx          = NULL;
    cfg.onStarted    = OnMonitorStarted;
    cfg.onStartedCtx = NULL;

    /* Async: true only means the request was accepted; the actual start
     * result arrives via onStarted (asserted in TestVerifyFrames, after the
     * listen window has long absorbed the driver-side confirm wait). */
    bool ok = s_pWiFi->EnterMonitorMode(&cfg);
    TILT_ASSERT_TRUE(tilt, ok, "EnterMonitorMode request accepted");
    if (!ok) return;

    TILT_INFO(tilt, "Monitoring ch %u for %lu s...", (unsigned)s_monChannel, LT_Pu32(s_monSecs));
    s_listenDeferred = true;
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(s_monSecs), NULL, NULL);
}

static void TestVerifyFrames(Tilt *tilt) {
    s_currentTilt = tilt;
    s_pWiFi->ExitMonitorMode();

    TILT_ASSERT_TRUE(tilt, s_monStarted, "onStarted completion delivered");
    TILT_ASSERT_TRUE(tilt, s_monStartOk, "driver reported monitor started");

    TILT_INFO(tilt, "frames=%lu  beacons=%lu  probe_resp=%lu  other_mgmt=%lu",
              LT_Pu32(s_frameCount), LT_Pu32(s_beaconCount),
              LT_Pu32(s_probeRespCount), LT_Pu32(s_otherMgmtCount));
    if (s_haveFirstBssid) {
        TILT_INFO(tilt, "first mgmt BSSID %02X:%02X:%02X:%02X:%02X:%02X",
                  s_firstBssid[0], s_firstBssid[1], s_firstBssid[2],
                  s_firstBssid[3], s_firstBssid[4], s_firstBssid[5]);
    }

    /* The gating assertion: the monitor RX path must deliver frames at all.
     * A live channel beacons constantly, so zero frames means the RX bridge
     * is not wired / monitor mode is non-functional on this silicon. */
    TILT_ASSERT_TRUE(tilt, s_frameCount > 0, "monitor RX delivered at least one frame");
    TILT_INFO(tilt, "beacons observed: %lu", LT_Pu32(s_beaconCount));
}

/** Hooks ********************************************************************/

static void BeforeAllTests(Tilt *tilt) {
    const char *chStr = tilt->API->GetProperty("MON_CHANNEL", "");
    u32 ch = (chStr && chStr[0]) ? lt_strtou32(chStr, NULL, 10) : 0;
    s_monChannel = (ch > 0 && ch < 256) ? (u8)ch : DEFAULT_MON_CHANNEL;

    const char *sStr = tilt->API->GetProperty("MON_SECS", "");
    u32 secs = (sStr && sStr[0]) ? lt_strtou32(sStr, NULL, 10) : 0;
    s_monSecs = (secs > 0) ? secs : DEFAULT_MON_SECS;
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_pWiFi) {
        s_pWiFi->ExitMonitorMode();
        s_pWiFi->NoStatusChange(OnWiFiStatus);
        lt_closelibrary(s_pWiFi);
        s_pWiFi = NULL;
    }
}

/** Test table ***************************************************************/

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestOpenDevice,   "OpenDevice",   "open LTDeviceWiFi",                 0 },
    { TestWiFiUp,       "WiFiUp",       "wait for radio Up",                 0 },
    { TestEnterMonitor, "EnterMonitor", "enter monitor + listen",            0 },
    { TestVerifyFrames, "VerifyFrames", "monitor RX delivered frames",       0 },
};

/** Library entry point ******************************************************/

static int UnitTestLTDeviceWiFiMonitorImpl_Run(int argc, const char **argv) {
    s_engine->API->ConfigureTestSuite(s_engine, s_tests,
                                      sizeof(s_tests)/sizeof(s_tests[0]),
                                      &s_hooks);
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceWiFiMonitorImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceWiFiMonitorImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiMonitor, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiMonitor, UnitTestLTDeviceWiFiMonitorImpl_Run, 1536)
LTLIBRARY_DEFINITION;
