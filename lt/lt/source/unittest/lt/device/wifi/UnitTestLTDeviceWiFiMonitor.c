/*******************************************************************************
 *
 * UnitTestLTDeviceWiFiMonitor
 * ---------------------------
 *
 * Monitor-mode smoke test + directed-probe round trip.  Pure Jilt (TILT 2.0).
 *
 *   ltrun UnitTestLTDeviceWiFiMonitor [MON_CHANNEL=165] [MON_SECS=5] \
 *                                     [TARGET_BSSID=aa:bb:cc:dd:ee:ff] \
 *                                     [TARGET_SSID=...] [PROBE_COUNT=3] \
 *                                     [PROBE_WAIT_MS=500]
 *
 * Purpose: prove the BL61x monitor-mode RX path delivers raw 802.11
 * management frames and that management TX works while monitor RX is live.
 * This independently validates a driver capability; the product find-host path
 * currently uses scan plus raw Beacon/Probe Response delivery, not monitor mode.
 *
 * Procedure:
 *   1. Open LTDeviceWiFi, wait for Up, Disconnect (monitor is mutually
 *      exclusive with an STA connection).
 *   2. EnterMonitorMode(MON_CHANNEL); time the request -> onStarted ->
 *      first-RX transitions.  Baseline to beat: wl80211 needed ~9 s to first RX.
 *   3. With monitor RX still live, TX PROBE_COUNT directed probe requests to
 *      TARGET_BSSID and wait for a probe response from that BSSID; record the
 *      TX -> response latency.  Skipped (not failed) when TARGET_BSSID is
 *      unset, so the test still works as a plain RX smoke test.
 *   4. ExitMonitorMode; assert monitor RX delivered frames and report every
 *      measured number.
 *
 * A cloaked AP only answers a probe request whose SSID IE carries its exact
 * SSID, so TARGET_SSID must be supplied to probe a hidden Roku GO.  With
 * TARGET_SSID empty the probe goes out wildcard, which non-cloaked APs answer.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCountingSemaphore.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <tilt/JiltEngine.h>

/* A paired Roku host operates on a 5 GHz non-DFS channel; 165 is UNII-3.
 * 2.4 GHz works too and is much faster to park on (~13 ms vs ~185 ms). */
#define DEFAULT_MON_CHANNEL  165
#define DEFAULT_MON_SECS     5
#define DEFAULT_PROBE_COUNT  3
#define DEFAULT_PROBE_WAIT_MS 500

/* 802.11 mgmt header (24 B) + SSID IE (2 + 32) + Supported Rates IE (2 + 8). */
#define PROBE_REQ_MAX_LEN    68

/** Module state *************************************************************/

static JiltEngine    *s_engine;
static LTDeviceWiFi  *s_pWiFi;
static Tilt          *s_currentTilt;

static bool           s_deviceIsUp;
static bool           s_upDeferred;
/* autojoin is a PERSISTED setting; saved on open, restored on teardown. */
static u32            s_savedAutoJoin;
static bool           s_haveSavedAutoJoin;
static u8             s_monChannel;
static u32            s_monSecs;

/* Directed-probe target.  s_haveTarget is false when TARGET_BSSID was not
 * supplied, which skips the probe round trip instead of failing it. */
static bool           s_haveTarget;
static u8             s_targetBssid[6];
static char           s_targetSsid[33];
static u32            s_probeCount;
static u32            s_probeWaitMs;

/* Touched from the (driver-thread) RX callback — keep volatile and do only
 * trivial counting there. */
static volatile u32   s_frameCount;
static volatile u32   s_beaconCount;
static volatile u32   s_probeRespCount;
static volatile u32   s_otherMgmtCount;
static volatile bool  s_haveFirstBssid;
static u8             s_firstBssid[6];

/* Set from the driver-context onStarted completion callback: async
 * EnterMonitorMode result (the return value only means "request accepted"). */
static volatile bool  s_monStarted;
static volatile bool  s_monStartOk;

/* Latency instrumentation.  All kernel-time (monotonic uptime) stamps; the ones
 * written from the RX callback are volatile because the test thread reads them.
 * LTTime is a struct, so a plain s64 of milliseconds is used to keep the
 * cross-thread stores single-word. */
static s64            s_tMonRequestMs;      /* EnterMonitorMode called */
static volatile s64   s_tMonStartedMs;      /* onStarted fired */
static volatile s64   s_tFirstRxMs;         /* first monitor frame delivered */
static s64            s_tProbeTxMs;         /* last directed probe handed to the driver */
static volatile s64   s_tProbeRspMs;        /* probe response from s_targetBssid */
static volatile u32   s_probeRspFromTarget; /* matching probe responses seen */
static volatile u32   s_probeRspFreq;       /* monitor_rx_freq of the match (MHz) */
static int            s_probeTxRc[8];       /* TxMgmtFrame return per attempt */
static u32            s_probeTxSent;

/* Whole-suite budget.  MON_SECS sniff windows routinely exceed the engine
 * default of 30 s, which aborts the run before it can report. */
#define TILT_LIBRARY_TIMEOUT 240

/* Frame capture: with CAPTURE_SA set, the first management frame of subtype
 * CAPTURE_SUBTYPE sent BY that address (addr2) is copied and hex-dumped at the
 * end of the run.  Intended for using one board as a sniffer while another
 * associates, to see what a station actually puts on the air -- e.g. whether an
 * association request (subtype 0) carries an expected vendor IE. */
static bool           s_haveCaptureSa;
static u8             s_captureSa[6];
static u8             s_captureSubtype;
static volatile bool  s_haveCaptured;
static volatile u32   s_captureCount;   /* matches seen; the LAST is kept */
static u16            s_capturedLen;
static u8             s_capturedFrame[256];

/* SSID of the first beacon seen, so a run tells you what is actually on the
 * channel — which is how you pick TARGET_BSSID/TARGET_SSID for the probe. */
static volatile bool  s_haveBeaconSsid;
static char           s_beaconSsid[33];
static u8             s_beaconBssid[6];

/* One semaphore per wake condition: the test thread blocks on exactly the event
 * it cares about, so a beacon cannot satisfy a wait for a probe response.
 *
 * LTCountingSemaphore rather than JiltEngine's DeferTestCompletion because a
 * Jilt deferral that times out terminates the whole run and skips the remaining
 * tests — but "no frame" and "no probe response" are results this test exists to
 * MEASURE and report.  Wait() simply returns false on expiry.  Signal() is
 * ISR-safe (a naked monitor Notify), so it is valid and cheap from the fhost RX
 * callback and the driver's onStarted context. */
static LTCountingSemaphore *s_semMonStarted;
static LTCountingSemaphore *s_semFirstRx;
static LTCountingSemaphore *s_semProbeRsp;
static LTCountingSemaphore *s_semCapture;

static s64 NowMs(void) {
    return LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime());
}

/* Discard any count left by a previous run.  Statics — including these
 * semaphores — survive between ltrun invocations, and a stale count would make
 * the next Wait() return true instantly without the event ever happening.
 * maxCount is 1, so this loops at most once. */
static void DrainSem(LTCountingSemaphore *sem) {
    while (sem->API->TryWait(sem)) { }
}

static bool WaitSem(LTCountingSemaphore *sem, u32 timeoutMs) {
    return sem->API->Wait(sem, LTTime_Milliseconds(timeoutMs));
}

/** Helpers ******************************************************************/

/* Parse "aa:bb:cc:dd:ee:ff", "aa-bb-...", or "aabbccddeeff" into 6 octets. */
static bool ParseMac(const char *s, u8 out[6]) {
    u32 nibbles = 0;
    u8  acc = 0;
    if (!s) return false;
    for (; *s; s++) {
        char c = *s;
        u8 v;
        if (c == ':' || c == '-') continue;
        if      (c >= '0' && c <= '9') v = (u8)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (u8)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (u8)(c - 'A' + 10);
        else return false;
        acc = (u8)((acc << 4) | v);
        if (++nibbles > 12) return false;
        if ((nibbles & 1u) == 0u) {
            out[(nibbles / 2u) - 1u] = acc;
            acc = 0;
        }
    }
    return nibbles == 12;
}

/* Build a directed probe request. A cloaked AP requires both its destination
 * address and exact SSID to return a probe response.
 * Returns the frame length, or 0 if it would not fit. */
static u16 BuildProbeRequest(u8 *out, u16 cap, const u8 dst[6], const u8 src[6],
                             const char *ssid, u8 channel) {
    /* 5 GHz has no CCK, so advertise OFDM rates only there; on 2.4 GHz lead
     * with the CCK basic rates (high bit = "basic rate"). */
    static const u8 kRatesOfdm[] = { 0x0C, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C };
    static const u8 kRatesCck[]  = { 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C };

    u32 ssidLen = ssid ? (u32)lt_strlen(ssid) : 0u;
    if (ssidLen > 32u) ssidLen = 32u;

    u16 need = (u16)(24u + 2u + ssidLen + 2u + sizeof(kRatesOfdm));
    if (cap < need) return 0;

    u8 *p = out;
    *p++ = 0x40;                 /* frame control: mgmt (type 0), probe req (subtype 4) */
    *p++ = 0x00;
    *p++ = 0x00; *p++ = 0x00;    /* duration */
    lt_memcpy(p, dst, 6); p += 6;   /* addr1: destination */
    lt_memcpy(p, src, 6); p += 6;   /* addr2: source (our MAC) */
    lt_memcpy(p, dst, 6); p += 6;   /* addr3: BSSID */
    *p++ = 0x00; *p++ = 0x00;    /* sequence control (the MAC fills this in) */

    *p++ = 0x00;                 /* IE 0: SSID (zero length = wildcard) */
    *p++ = (u8)ssidLen;
    if (ssidLen) { lt_memcpy(p, ssid, ssidLen); p += ssidLen; }

    *p++ = 0x01;                 /* IE 1: Supported Rates */
    *p++ = (u8)sizeof(kRatesOfdm);
    lt_memcpy(p, (channel >= 36u) ? kRatesOfdm : kRatesCck, sizeof(kRatesOfdm));
    p += sizeof(kRatesOfdm);

    return (u16)(p - out);
}

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
    s_tMonStartedMs = NowMs();
    s_monStartOk = ok;
    s_monStarted = true;
    s_semMonStarted->API->Signal(s_semMonStarted);
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

    if (s_frameCount == 0) {
        s_tFirstRxMs = NowMs();
        s_semFirstRx->API->Signal(s_semFirstRx);
    }
    s_frameCount++;

    u8 fc      = f[0];
    u8 type    = (u8)((fc >> 2) & 0x3);
    u8 subtype = (u8)((fc >> 4) & 0xf);

    if (type == 0) {                       /* management */
        if (subtype == 8)      s_beaconCount++;
        else if (subtype == 5) s_probeRespCount++;
        else                   s_otherMgmtCount++;

        /* Capture-by-source. addr2 (transmitter) is at offset 10. Copy only —
         * the hex dump happens on the owner thread in TestVerifyFrames. */
        /* Keep the LAST match, not the first: a station's first association
         * attempt often differs from later ones (e.g. an IE installed only after
         * the supplicant interface exists), and the newest attempt is the
         * interesting one. The sniff window therefore always runs to MON_SECS. */
        if (s_haveCaptureSa && subtype == s_captureSubtype &&
            len >= 24 && lt_memcmp(&f[10], s_captureSa, 6) == 0) {
            u16 n = (len > sizeof(s_capturedFrame)) ? (u16)sizeof(s_capturedFrame) : len;
            lt_memcpy(s_capturedFrame, f, n);
            s_capturedLen  = n;
            s_captureCount++;
            s_haveCaptured = true;
        }

        if (!s_haveFirstBssid && len >= 22) {
            lt_memcpy(s_firstBssid, &f[16], 6);
            s_haveFirstBssid = true;
        }

        /* Capture the SSID of a beacon or probe response.  Both carry a 12-byte
         * fixed body after the 24-byte header (timestamp 8 + interval/status 2 +
         * capability 2), so the IE list starts at 36 and the SSID IE (id 0) is
         * first.  A cloaked AP beacons a zero-length SSID — an empty result here
         * is itself the signal that TARGET_SSID must come from elsewhere. */
        if (!s_haveBeaconSsid && (subtype == 8 || subtype == 5) && len >= 38) {
            u8 ieId  = f[36];
            u8 ieLen = f[37];
            if (ieId == 0 && ieLen <= 32 && (u16)(38 + ieLen) <= len) {
                lt_memcpy(s_beaconSsid, &f[38], ieLen);
                s_beaconSsid[ieLen] = '\0';
                lt_memcpy(s_beaconBssid, &f[16], 6);
                s_haveBeaconSsid = true;
            }
        }

        /* The measurement that matters: a probe RESPONSE whose BSSID (addr3) is
         * the station we just directed a probe request at. */
        if (subtype == 5 && s_haveTarget && len >= 22 &&
            lt_memcmp(&f[16], s_targetBssid, 6) == 0) {
            s_probeRspFromTarget++;
            if (s_probeRspFromTarget == 1) {
                s_tProbeRspMs = NowMs();
                /* Read inside the callback: the driver latches the frame's own
                 * primary-channel frequency, and it is only unambiguous here. */
                s_probeRspFreq = s_pWiFi->GetOption("monitor_rx_freq");
                /* Publish everything before the signal: the waiter validates
                 * the counter as soon as it wakes. */
                s_semProbeRsp->API->Signal(s_semProbeRsp);
            }
        }
    }
}

/* Hex-dump the captured frame and, for frame types whose IE list position is
 * known, enumerate its information elements.  Vendor-specific elements (id 221)
 * are called out with their OUI, which is the point of the exercise: it shows
 * whether a station put an expected vendor IE on the air. */
static void DumpCapturedFrame(Tilt *tilt) {
    const u8 *f = s_capturedFrame;
    u16 len = s_capturedLen;
    u8 subtype = (u8)((f[0] >> 4) & 0xf);

    TILT_INFO(tilt, "captured mgmt subtype %u (match %lu of the run), %u bytes, from "
                    "%02X:%02X:%02X:%02X:%02X:%02X",
              (unsigned)subtype, LT_Pu32(s_captureCount), (unsigned)len,
              f[10], f[11], f[12], f[13], f[14], f[15]);

    char line[3 * 16 + 1];
    for (u16 off = 0; off < len; off += 16) {
        u16 n = ((u16)(len - off) < 16u) ? (u16)(len - off) : 16u;
        u32 pos = 0;
        for (u16 i = 0; i < n; i++) {
            pos += (u32)lt_snprintf(line + pos, sizeof(line) - pos, "%02X ", f[off + i]);
        }
        TILT_INFO(tilt, "  %04X: %s", (unsigned)off, line);
    }

    /* IE list offset past the 24-byte header: assoc request (0) has capability +
     * listen interval; reassoc request (2) adds the current AP address; probe
     * request (4) has no fixed body. */
    u16 ieOff;
    if      (subtype == 0) ieOff = 24 + 4;
    else if (subtype == 2) ieOff = 24 + 10;
    else if (subtype == 4) ieOff = 24;
    else {
        TILT_INFO(tilt, "  (no IE walk for subtype %u)", (unsigned)subtype);
        return;
    }

    bool sawVendor = false;
    while ((u16)(ieOff + 2) <= len) {
        u8 id     = f[ieOff];
        u8 ieLen  = f[ieOff + 1];
        if ((u16)(ieOff + 2 + ieLen) > len) {
            TILT_INFO(tilt, "  IE %u truncated (len %u, %u bytes left)",
                      (unsigned)id, (unsigned)ieLen, (unsigned)(len - ieOff - 2));
            break;
        }
        if (id == 221 && ieLen >= 3) {
            sawVendor = true;
            TILT_INFO(tilt, "  IE 221 vendor len %u OUI %02X:%02X:%02X",
                      (unsigned)ieLen, f[ieOff + 2], f[ieOff + 3], f[ieOff + 4]);
        } else {
            TILT_INFO(tilt, "  IE %u len %u", (unsigned)id, (unsigned)ieLen);
        }
        ieOff = (u16)(ieOff + 2 + ieLen);
    }
    if (!sawVendor)
        TILT_INFO(tilt, "  NO vendor-specific (221) element present");
}

/** Tests ********************************************************************/

static void TestOpenDevice(Tilt *tilt) {
    s_currentTilt = tilt;
    s_pWiFi = lt_openlibrary(LTDeviceWiFi);
    TILT_ASSERT_TRUE(tilt, s_pWiFi != NULL, "cannot open LTDeviceWiFi");
    if (!s_pWiFi) return;
    /* Register before touching the device: opening the library starts the WiFi
     * state machine, so an Up status can land during the calls below and would
     * be missed if registration came last (leaving TestWiFiUp to time out). */
    s_pWiFi->OnStatusChange(OnWiFiStatus, NULL, NULL);
    /* Monitor mode is mutually exclusive with an STA connection.
     *
     * SetOption("autojoin") PERSISTS to LT settings, so leaving it at 0 would
     * silently stop the device reconnecting long after this test finished --
     * save it here and restore it in AfterAllTests (the pattern
     * UnitTestLTDeviceWiFi already follows). */
    s_savedAutoJoin = s_pWiFi->GetOption("autojoin");
    s_haveSavedAutoJoin = true;
    s_pWiFi->SetOption("autojoin", 0);
    s_pWiFi->Disconnect();
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
    /* Must be reset per run: statics persist between ltrun invocations, so a
     * leftover capture from a previous run on a DIFFERENT channel would be
     * reported as this channel's AP. */
    s_haveBeaconSsid = false;
    s_beaconSsid[0] = '\0';
    s_monStarted = false;
    s_monStartOk = false;
    s_tMonStartedMs = 0;
    s_tFirstRxMs = 0;
    DrainSem(s_semMonStarted);
    DrainSem(s_semFirstRx);

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
    s_tMonRequestMs = NowMs();
    bool ok = s_pWiFi->EnterMonitorMode(&cfg);
    TILT_ASSERT_TRUE(tilt, ok, "EnterMonitorMode request accepted");
    if (!ok) return;

    TILT_INFO(tilt, "Monitoring ch %u for up to %lu s...", (unsigned)s_monChannel,
              LT_Pu32(s_monSecs));
    /* Wakes on the first frame, which makes this window a measurement of
     * monitor-start -> first-RX rather than a fixed dwell.  A timeout is NOT a
     * failure here: TestVerifyFrames reports it. */
    (void)WaitSem(s_semFirstRx, s_monSecs * 1000u);

    /* Then wait for the start completion itself.  The vendor begins delivering
     * RX before wifi_mgmr_sniffer_enable returns, so first-RX genuinely precedes
     * onStarted (measured: 191 ms vs 192 ms).  Returning on the frame alone
     * leaves TestProbeRoundTrip's s_monStartOk check racing that ~1 ms gap and
     * skipping the probe experiment when it loses. */
    (void)WaitSem(s_semMonStarted, 2000u);

    /* Sniffer mode: the waits above return on the first frame of any kind (a
     * beacon, within ~200 ms), which is far too early to catch a specific frame
     * from another station.  Keep listening for the full MON_SECS, or until the
     * frame we were asked to capture arrives. */
    if (s_haveCaptureSa) {
        TILT_INFO(tilt, "sniffing for mgmt subtype %u from "
                        "%02X:%02X:%02X:%02X:%02X:%02X (up to %lu s)...",
                  (unsigned)s_captureSubtype,
                  s_captureSa[0], s_captureSa[1], s_captureSa[2],
                  s_captureSa[3], s_captureSa[4], s_captureSa[5],
                  LT_Pu32(s_monSecs));
        (void)WaitSem(s_semCapture, s_monSecs * 1000u);   /* never signalled: full dwell */
    }
}

/* Validate that management TX works while monitor RX is live. The current
 * product discovery path does not depend on this capability. */
static void TestProbeRoundTrip(Tilt *tilt) {
    s_currentTilt = tilt;

    if (!s_haveTarget) {
        TILT_INFO(tilt, "TARGET_BSSID not set - skipping directed-probe round trip");
        return;
    }
    TILT_ASSERT_TRUE(tilt, s_monStartOk, "monitor must be started to probe from it");
    if (!s_monStartOk) return;

    LTMacAddress mac;
    lt_memset(&mac, 0, sizeof(mac));
    TILT_ASSERT_TRUE(tilt, s_pWiFi->GetMacAddress(&mac), "GetMacAddress");
    u8 frame[PROBE_REQ_MAX_LEN];
    u16 len = BuildProbeRequest(frame, (u16)sizeof(frame), s_targetBssid,
                                mac.octet, s_targetSsid, s_monChannel);
    TILT_ASSERT_TRUE(tilt, len > 0, "probe request built");
    if (len == 0) return;

    s_probeRspFromTarget = 0;
    s_probeRspFreq       = 0;
    s_tProbeRspMs        = 0;
    s_probeTxSent        = 0;
    /* Drain before the TX below, so an unsolicited probe response to the target
     * that arrived during the listen window cannot be mistaken for a reply to
     * our probe. */
    DrainSem(s_semProbeRsp);

    u32 attempts = s_probeCount;
    if (attempts > (u32)(sizeof(s_probeTxRc) / sizeof(s_probeTxRc[0]))) {
        attempts = (u32)(sizeof(s_probeTxRc) / sizeof(s_probeTxRc[0]));
    }

    TILT_INFO(tilt, "TX %lu directed probe(s) -> %02X:%02X:%02X:%02X:%02X:%02X ssid=\"%s\" (%u B)",
              LT_Pu32(attempts),
              s_targetBssid[0], s_targetBssid[1], s_targetBssid[2],
              s_targetBssid[3], s_targetBssid[4], s_targetBssid[5],
              s_targetSsid, (unsigned)len);
    /* The valid calls name the channel on which monitor mode parked the radio. */
    u8 wrongChannel = (s_monChannel == 1u) ? 6u : 1u;
    int wrongChannelRc = s_pWiFi->TxMgmtFrame(frame, len, wrongChannel);
    TILT_ASSERT_TRUE(tilt, wrongChannelRc < 0,
                     "TxMgmtFrame rejects a channel other than the parked channel");

    s_tProbeTxMs = NowMs();
    for (u32 i = 0; i < attempts; i++) {
        s_probeTxRc[i] = s_pWiFi->TxMgmtFrame(frame, len, s_monChannel);
        s_probeTxSent++;
    }

    bool anyAccepted = false;
    for (u32 i = 0; i < s_probeTxSent; i++) {
        if (s_probeTxRc[i] == 0) anyAccepted = true;
        else TILT_INFO(tilt, "probe %lu rejected rc=%d", LT_Pu32(i), s_probeTxRc[i]);
    }
    /* A refusal here is the finding to escalate to Bouffalo: it would mean fhost
     * declines mgmt TX while the sniffer holds the radio, exactly as wl80211
     * did. */
    TILT_ASSERT_TRUE(tilt, anyAccepted, "TxMgmtFrame accepted while monitor RX live");
    if (!anyAccepted) return;

    /* No response within the window is a RESULT, not a harness failure — it is
     * reported by TestVerifyFrames alongside the TX outcome. */
    (void)WaitSem(s_semProbeRsp, s_probeWaitMs);
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
    if (s_haveBeaconSsid) {
        /* FIRST AP seen this run, not necessarily the probe target -- name it
         * that way so its cloaked/not verdict is never read as the target's.
         * Feed the pair back in as TARGET_BSSID / TARGET_SSID to probe it. */
        TILT_INFO(tilt, "first AP on ch %u: %02X:%02X:%02X:%02X:%02X:%02X ssid=\"%s\"%s",
                  (unsigned)s_monChannel,
                  s_beaconBssid[0], s_beaconBssid[1], s_beaconBssid[2],
                  s_beaconBssid[3], s_beaconBssid[4], s_beaconBssid[5],
                  s_beaconSsid, s_beaconSsid[0] ? "" : " (cloaked)");
    }

    /* Baselines these numbers exist to beat, measured on the wl80211 driver
     * against a cloaked Roku GO on ch 165: monitor start -> first RX ~9 s, and a
     * directed probe -> response round trip that was not possible at all, since
     * frame injection was rejected while monitor mode held the radio. */
    if (s_tMonStartedMs) {
        TILT_INFO(tilt, "T monitor request -> onStarted: %lu ms",
                  LT_Pu32((u32)(s_tMonStartedMs - s_tMonRequestMs)));
    }
    if (s_tFirstRxMs) {
        TILT_INFO(tilt, "T monitor request -> first RX:  %lu ms",
                  LT_Pu32((u32)(s_tFirstRxMs - s_tMonRequestMs)));
    } else {
        TILT_INFO(tilt, "T monitor request -> first RX:  no frame within %lu s",
                  LT_Pu32(s_monSecs));
    }

    if (s_haveTarget) {
        if (s_probeRspFromTarget > 0) {
            TILT_INFO(tilt, "T probe TX -> probe RSP: %lu ms (%lu responses, freq %lu MHz)",
                      LT_Pu32((u32)(s_tProbeRspMs - s_tProbeTxMs)),
                      LT_Pu32(s_probeRspFromTarget), LT_Pu32(s_probeRspFreq));
        } else {
            /* Distinguish the two negatives: the driver logs txmgmt.cfm with an
             * acked flag per frame, so "TX accepted + acked=0 + no response"
             * means the target never heard us (wrong channel / out of range /
             * cloaked and probed with a wildcard SSID), whereas "TX rejected"
             * would be the vendor refusing to transmit while monitoring. */
            TILT_INFO(tilt, "T probe TX -> probe RSP: none within %lu ms (%lu TX'd; see txmgmt.cfm acked=)",
                      LT_Pu32(s_probeWaitMs), LT_Pu32(s_probeTxSent));
        }
    }

    if (s_haveCaptureSa) {
        if (s_haveCaptured) DumpCapturedFrame(tilt);
        else TILT_INFO(tilt, "no mgmt subtype %u seen from the capture address",
                       (unsigned)s_captureSubtype);
    }

    /* The gating assertion: the monitor RX path must deliver frames at all.
     * A live channel beacons constantly, so zero frames means the RX bridge
     * is not wired / monitor mode is non-functional on this silicon. */
    TILT_ASSERT_TRUE(tilt, s_frameCount > 0, "monitor RX delivered at least one frame");
    TILT_INFO(tilt, "beacons observed: %lu", LT_Pu32(s_beaconCount));

    /* Reported separately from the TX-accepted assertion above so a failure
     * distinguishes "TX refused" from "TX went out, target silent". */
    if (s_haveTarget) {
        TILT_ASSERT_TRUE(tilt, s_probeRspFromTarget > 0,
                         "probe response received from target on monitor RX");
    }
}

/** Hooks ********************************************************************/

static void BeforeAllTests(Tilt *tilt) {
    /* File-scope state survives between ltrun invocations in one boot (statics
     * are initialized at image load, not per run), and AfterAllTests brings the
     * device back down.  Without this reset a second run in the same boot sees
     * a stale "already up" and drives monitor mode into a still-initializing
     * driver ("monitor requested before driver up"). */
    s_deviceIsUp = false;
    s_upDeferred = false;

    const char *chStr = tilt->API->GetProperty("MON_CHANNEL", "");
    u32 ch = (chStr && chStr[0]) ? lt_strtou32(chStr, NULL, 10) : 0;
    s_monChannel = (ch > 0 && ch < 256) ? (u8)ch : DEFAULT_MON_CHANNEL;

    const char *sStr = tilt->API->GetProperty("MON_SECS", "");
    u32 secs = (sStr && sStr[0]) ? lt_strtou32(sStr, NULL, 10) : 0;
    s_monSecs = (secs > 0) ? secs : DEFAULT_MON_SECS;

    const char *bssidStr = tilt->API->GetProperty("TARGET_BSSID", "");
    s_haveTarget = (bssidStr && bssidStr[0]) ? ParseMac(bssidStr, s_targetBssid) : false;
    if (bssidStr && bssidStr[0] && !s_haveTarget) {
        TILT_INFO(tilt, "TARGET_BSSID \"%s\" is not a MAC address - probe test disabled",
                  bssidStr);
    }

    lt_strncpyTerm(s_targetSsid, tilt->API->GetProperty("TARGET_SSID", ""),
                   sizeof(s_targetSsid));

    const char *cStr = tilt->API->GetProperty("PROBE_COUNT", "");
    u32 cnt = (cStr && cStr[0]) ? lt_strtou32(cStr, NULL, 10) : 0;
    s_probeCount = (cnt > 0) ? cnt : DEFAULT_PROBE_COUNT;

    const char *wStr = tilt->API->GetProperty("PROBE_WAIT_MS", "");
    u32 wait = (wStr && wStr[0]) ? lt_strtou32(wStr, NULL, 10) : 0;
    s_probeWaitMs = (wait > 0) ? wait : DEFAULT_PROBE_WAIT_MS;

    /* Sniffer mode: capture one mgmt frame of CAPTURE_SUBTYPE sent by CAPTURE_SA.
     * Default subtype 0 = association request. */
    const char *capStr = tilt->API->GetProperty("CAPTURE_SA", "");
    s_haveCaptureSa = (capStr && capStr[0]) ? ParseMac(capStr, s_captureSa) : false;
    if (capStr && capStr[0] && !s_haveCaptureSa)
        TILT_INFO(tilt, "CAPTURE_SA \"%s\" is not a MAC address - capture disabled", capStr);
    const char *cstStr = tilt->API->GetProperty("CAPTURE_SUBTYPE", "");
    s_captureSubtype = (cstStr && cstStr[0]) ? (u8)lt_strtou32(cstStr, NULL, 10) : 0;
    s_haveCaptured = false;
    s_capturedLen  = 0;
    s_captureCount = 0;
    DrainSem(s_semCapture);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_pWiFi) {
        s_pWiFi->ExitMonitorMode();
        /* Put the persisted autojoin setting back before closing, or the device
         * stops reconnecting for every boot after this test ran. */
        if (s_haveSavedAutoJoin) {
            s_pWiFi->SetOption("autojoin", (s32)s_savedAutoJoin);
            s_haveSavedAutoJoin = false;
            TILT_INFO(tilt, "autojoin restored to %lu", LT_Pu32(s_savedAutoJoin));
        }
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
    { TestOpenDevice,      "OpenDevice",      "open LTDeviceWiFi",                    0 },
    { TestWiFiUp,          "WiFiUp",          "wait for radio Up",                    0 },
    { TestEnterMonitor,    "EnterMonitor",    "enter monitor + listen",               0 },
    { TestProbeRoundTrip,  "ProbeRoundTrip",  "directed probe TX while monitoring",   0 },
    { TestVerifyFrames,    "VerifyFrames",    "monitor RX delivered frames",          0 },
};

/** Library entry point ******************************************************/

static int UnitTestLTDeviceWiFiMonitorImpl_Run(int argc, const char **argv) {
    s_engine->API->SetTestSuiteTimeout(s_engine, LTTime_Seconds(TILT_LIBRARY_TIMEOUT));
    s_engine->API->ConfigureTestSuite(s_engine, s_tests,
                                      sizeof(s_tests)/sizeof(s_tests[0]),
                                      &s_hooks);
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/* maxCount 1: these are binary "the event happened" signals, so repeated
 * Signals (e.g. every matching probe response) must not accumulate a count that
 * a later Wait would consume without the event recurring. */
static LTCountingSemaphore *CreateSignalSem(void) {
    LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
    if (sem) sem->API->Init(sem, 1, 0);
    return sem;
}

static bool UnitTestLTDeviceWiFiMonitorImpl_LibInit(void) {
    s_engine        = lt_createobject(JiltEngine);
    s_semMonStarted = CreateSignalSem();
    s_semFirstRx    = CreateSignalSem();
    s_semProbeRsp   = CreateSignalSem();
    s_semCapture    = CreateSignalSem();
    return s_engine && s_semMonStarted && s_semFirstRx && s_semProbeRsp &&
           s_semCapture;
}

static void UnitTestLTDeviceWiFiMonitorImpl_LibFini(void) {
    lt_destroyobject(s_semCapture);
    lt_destroyobject(s_semProbeRsp);
    lt_destroyobject(s_semFirstRx);
    lt_destroyobject(s_semMonStarted);
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiMonitor, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiMonitor, UnitTestLTDeviceWiFiMonitorImpl_Run, 1536)
LTLIBRARY_DEFINITION;
