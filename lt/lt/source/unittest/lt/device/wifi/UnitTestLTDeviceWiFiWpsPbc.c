/*******************************************************************************
 *
 * UnitTestLTDeviceWiFiWpsPbc
 * --------------------------
 *
 * Box + remote WPS PBC pairing test.  Pure Jilt (TILT 2.0).
 *
 *   ltrun UnitTestLTDeviceWiFiWpsPbc [PBC_TIMEOUT_SEC=120] [HOST_SSID_PREFIX=DIRECT-roku-]
 *
 * This replicates the Roku host (TV/box) + remote pairing scenario.  There is
 * NO human button press: the Roku host placed in WiFi-pairing mode is the
 * trigger.  The remote advertises a Roku vendor extension inside its WPS M1
 * message so the Roku registrar recognises it and provisions credentials.
 *
 * Test procedure:
 *   1. Open LTDeviceWiFi, LTDeviceWiFiPairing and LTDeviceAuthentication.
 *   2. Wait for the device to come Up.
 *   3. Scan and select the Roku host AP (policy lives here in the test/app
 *      layer: SSID-prefix match, best RSSI).  Override the prefix with the
 *      HOST_SSID_PREFIX=<str> property.
 *   4. Build the Roku WPS M1 vendor IE and call LTDeviceWiFiPairing::
 *      StartPbc(timeout, m1Ie, assocIe, target) with the chosen AP.  The
 *      driver performs an OPEN association to that AP carrying the WPS
 *      assoc IE, then runs EAP-WSC.
 *   5. Watch for the credential, then WPA2-PSK reconnect via LTDeviceWiFi.
 *
 * Thin driver-level test:  no app glue.
 * Sole purpose:  prove that the BL61x LT-supplicant WPS path completes with a
 * real Roku host in pairing mode and surfaces credentials.
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
#include <lt/device/wifi/LTDeviceWiFiPairing.h>
#include <lt/device/authentication/LTDeviceAuthentication.h>
#include <tilt/JiltEngine.h>

#define DEFAULT_PBC_TIMEOUT_SEC  120
/* Roku hosts advertise their WiFi-Direct SSID as "DIRECT-roku-xxx-xxxxxx" in
 * pairing mode; match that prefix by default. */
#define DEFAULT_HOST_SSID_PREFIX "DIRECT-roku-"
#define MAX_SSID_PREFIX          33

/* Roku WPS pairing vendor extension. */
#define ROKU_WPS_OUI_0           0xC8
#define ROKU_WPS_OUI_1           0x3A
#define ROKU_WPS_OUI_2           0x6B
#define ROKU_WPS_M1_MSG_TYPE     0x01     /* simple-auth M1 */
#define ROKU_WPS_M1_IE_LEN       8        /* OUI(3) + type(1) + chipid(4) */

/* Roku vendor-specific IE carried in the WPS (re)association request.  The Roku
 * host registrar requires this IE to accept a credential-less WPS enrollee;
 * without it the box treats the STA as a plain WPA2-PSK client and disassociates
 * before EAP-WSC starts.  This is policy (Roku OUI), so it lives in the
 * test/app layer and is handed to the driver via StartWpsPbc().  Full 802.11
 * element: id 0xDD, len 0x05, OUI C8:3A:6B (Roku) + 2 reserved bytes. */
#define ROKU_WPS_ASSOC_IE_LEN    7
static const u8 s_rokuAssocIe[ROKU_WPS_ASSOC_IE_LEN] = {
    0xDD, 0x05, ROKU_WPS_OUI_0, ROKU_WPS_OUI_1, ROKU_WPS_OUI_2, 0x00, 0x00
};

static const char *ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

/** Standard interfaces ******************************************************/

static LTCore                  *s_pCore;
static LTDeviceWiFi            *s_pWiFi;
static LTDeviceWiFiPairing     *s_pPairing;
static LTDeviceAuthentication  *s_pAuth;
static JiltEngine              *s_engine;

/** Module state *************************************************************/

static u32   s_pbcTimeoutSec;
static bool  s_deviceIsUp;
static bool  s_upDeferred;
static bool  s_connected;
static Tilt *s_currentTilt;
static u8    s_m1Ie[ROKU_WPS_M1_IE_LEN];

/* WPS-harvested credential + a mutable passphrase buffer for the post-WPS
 * WPA2-PSK reconnect (LTWiFi_ApInfo.pass is a non-const char*). */
static LTWiFi_Credentials s_wpsCreds;
static bool               s_wpsCredsOk;
static char               s_connectPass[kLTWiFi_Max_Pass + 1];
static LTWiFi_ApInfo      s_connectAp;

/* Host-selection (policy) state. */
static char          s_hostPrefix[MAX_SSID_PREFIX];
static bool          s_hostFound;
static LTWiFi_ApInfo s_hostAp;        ///< best matching Roku host AP
static s8            s_hostBestRssi;

/** Callbacks ****************************************************************/

static void OnWiFiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);
    Tilt *tilt = s_currentTilt;

    switch (status) {
        case kLTDeviceWiFi_Status_Up:
            s_deviceIsUp = true;
            if (tilt) TILT_INFO(tilt, "wifi: Up");
            if (s_upDeferred) {
                s_upDeferred = false;
                s_engine->API->SignalTestCompletion(s_engine, NULL);
            }
            break;
        case kLTDeviceWiFi_Status_Connected:
            s_connected = true;
            if (tilt) TILT_INFO(tilt, "wifi: Connected (WPS exchange + association complete)");
            s_engine->API->SignalTestCompletion(s_engine, NULL);
            break;
        case kLTDeviceWiFi_Status_JoinFailed:
            if (tilt) TILT_WARNING(tilt, "wifi: JoinFailed");
            break;
        default:
            break;
    }
}

/* Scan result callback (policy layer): match the Roku host by SSID prefix and
 * keep the strongest-RSSI candidate.  A NULL ap marks end-of-scan. */
static void OnScanResult(LTWiFi_ApInfo *ap, void *callback_data) {
    LT_UNUSED(callback_data);
    if (ap == NULL) {
        /* End of results: wake the test. */
        s_engine->API->SignalTestCompletion(s_engine, NULL);
        return;
    }

    u32 prefixLen = (u32)lt_strlen(s_hostPrefix);
    if (prefixLen == 0 || lt_strncmp(ap->ssid, s_hostPrefix, prefixLen) != 0)
        return;

    if (!s_hostFound || ap->rssi > s_hostBestRssi) {
        s_hostFound    = true;
        s_hostBestRssi = ap->rssi;
        s_hostAp       = *ap;
        s_hostAp.pass  = NULL;   /* never dereference scan-owned storage later */
    }
}

/** Tests ********************************************************************/

static void TestOpenDevice(Tilt *tilt) {
    s_currentTilt = tilt;
    s_pWiFi = lt_openlibrary(LTDeviceWiFi);
    TILT_ASSERT_TRUE(tilt, s_pWiFi != NULL, "cannot open LTDeviceWiFi");
    s_pAuth = lt_openlibrary(LTDeviceAuthentication);
    TILT_ASSERT_TRUE(tilt, s_pAuth != NULL, "cannot open LTDeviceAuthentication");
    s_pPairing = lt_createobject(LTDeviceWiFiPairing);
    TILT_ASSERT_TRUE(tilt, s_pPairing != NULL, "cannot open LTDeviceWiFiPairing");
    /* WPS drives association itself; disable autojoin to avoid races. */
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

static void TestScanForHost(Tilt *tilt) {
    s_currentTilt  = tilt;
    s_hostFound    = false;
    s_hostBestRssi = -128;

    TILT_INFO(tilt, "Scanning for Roku host AP (SSID prefix \"%s\")...", s_hostPrefix);
    /* Default wildcard scan; OnScanResult applies the host-match policy and
     * signals completion when it receives the end-of-scan (NULL) marker. */
    s_pWiFi->ScanAps(NULL, OnScanResult, NULL);
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(15), NULL, NULL);
}

static void TestHostFound(Tilt *tilt) {
    s_currentTilt = tilt;
    TILT_ASSERT_TRUE(tilt, s_hostFound, "found a Roku host AP in PBC pairing mode");
    TILT_INFO(tilt, "Selected host \"%s\" bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%u rssi=%d",
              s_hostAp.ssid,
              s_hostAp.bssid.octet[0], s_hostAp.bssid.octet[1], s_hostAp.bssid.octet[2],
              s_hostAp.bssid.octet[3], s_hostAp.bssid.octet[4], s_hostAp.bssid.octet[5],
              (unsigned)s_hostAp.channel, (int)s_hostAp.rssi);
}

static void TestStartWps(Tilt *tilt) {
    s_currentTilt = tilt;
    s_connected = false;

    /* Build the Roku WPS M1 vendor IE: OUI(3) + msg-type(1) + chip id(4 LE). */
    u32 chipId = s_pAuth->GetChipID();
    s_m1Ie[0] = ROKU_WPS_OUI_0;
    s_m1Ie[1] = ROKU_WPS_OUI_1;
    s_m1Ie[2] = ROKU_WPS_OUI_2;
    s_m1Ie[3] = ROKU_WPS_M1_MSG_TYPE;
    s_m1Ie[4] = (u8)(chipId       & 0xFF);
    s_m1Ie[5] = (u8)((chipId >> 8) & 0xFF);
    s_m1Ie[6] = (u8)((chipId >> 16) & 0xFF);
    s_m1Ie[7] = (u8)((chipId >> 24) & 0xFF);

    LTWiFi_VendorIE m1Ie = { .data = s_m1Ie, .length = ROKU_WPS_M1_IE_LEN };

    /* The Roku assoc-request vendor IE (policy).  The driver appends it after
     * the standard WPS IE in the (re)association request. */
    LTWiFi_VendorIE assocIe = { .data = s_rokuAssocIe, .length = ROKU_WPS_ASSOC_IE_LEN };

    if (!s_hostFound) {
        TILT_ASSERT_TRUE(tilt, false, "no host AP selected (scan step failed)");
        return;
    }

    TILT_INFO(tilt, "Starting WPS open-assoc to host \"%s\" (chip id 0x%08lX), timeout %lu s.",
              s_hostAp.ssid, LT_Pu32(chipId), LT_Pu32(s_pbcTimeoutSec));
    /* Targeted WPS: pass the scanned host AP.  The driver performs
     * an OPEN association to this BSSID carrying the WPS assoc IE + the Roku
     * assoc vendor IE, then runs EAP-WSC.  Host-selection policy was applied
     * above in OnScanResult; the Roku OUI policy is supplied here. */
    bool ok = s_pPairing->API->StartPbc(s_pPairing, s_pbcTimeoutSec, &m1Ie, &assocIe, &s_hostAp);
    TILT_ASSERT_TRUE(tilt, ok, "StartPbc returned true");

    /*
     * WPS PBC produces a CREDENTIAL, not a sustained connection.  This mirrors
     * the Realtek/ElkWifi enrollee flow (wifi_wps_config.c): wps_start() does an
     * open-system association + EAP-WSC, receives the credential, and the AP
     * then disassociates ("AP will send a disassociate frame after STA
     * connected, need reconnect here").  The actual connection happens AFTERWARD
     * via wps_connect_to_AP_by_certificate() -- a normal WPA2-PSK join using the
     * harvested credential.  So here we wait for the credential to land, not for
     * a Connected event.
     */
    s_wpsCredsOk = false;
    for (u32 elapsed = 0; elapsed < s_pbcTimeoutSec + 10; ++elapsed) {
        if (s_pPairing->API->GetCredentials(s_pPairing, &s_wpsCreds)) {
            s_wpsCredsOk = true;
            break;
        }
        s_pCore->GetCurrentThreadObject()->API->Sleep(LTTime_Seconds(1));
    }
    TILT_ASSERT_TRUE(tilt, s_wpsCredsOk, "WPS produced a credential");
}

static void TestGetCredentials(Tilt *tilt) {
    s_currentTilt = tilt;
    TILT_ASSERT_TRUE(tilt, s_wpsCredsOk, "WPS credential available");
    TILT_ASSERT_FALSE(tilt, lt_strlen(s_wpsCreds.ssid) == 0, "credentials carry an SSID");
    TILT_INFO(tilt, "WPS SSID:     \"%s\"", s_wpsCreds.ssid);
    TILT_INFO(tilt, "WPS security: %s",
              (s_wpsCreds.security < kLTWiFi_ApSecurity_Max) ? ApSecurityStrings[s_wpsCreds.security] : "?");
    /* Length only -- do not log the passphrase verbatim. */
    TILT_INFO(tilt, "WPS pass len: %lu", LT_Pu32((u32)lt_strlen(s_wpsCreds.pass)));
}

static void TestConnectWithCred(Tilt *tilt) {
    s_currentTilt = tilt;
    TILT_ASSERT_TRUE(tilt, s_wpsCredsOk, "have a WPS credential to connect with");

    /*
     * Post-WPS reconnect: a normal
     * WPA2-PSK join using the harvested SSID/passphrase.  This is the step that
     * actually associates + 4-way-handshakes and yields kLTDeviceWiFi_Status_
     * Connected. The post-WPS path is plain WPA2-PSK.
     */
    lt_memset(&s_connectAp, 0, sizeof(s_connectAp));
    lt_strncpyTerm(s_connectAp.ssid, s_wpsCreds.ssid, sizeof(s_connectAp.ssid));
    lt_strncpyTerm(s_connectPass, s_wpsCreds.pass, sizeof(s_connectPass));
    s_connectAp.pass     = s_connectPass;
    s_connectAp.security = s_wpsCreds.security;

    /*
     * Seed the join with the host's known BSSID + channel from the scan
     * (s_hostAp) so the driver connects directly on the host's channel rather
     * than issuing a chan=0 full scan on every attempt.  The post-WPS
     * host keeps the same BSSID/channel it advertised during pairing, and the
     * scan-based reconnect intermittently auth-fails -- slow
     * enough that the retry loop blew the Tilt suite timeout in CI.  Use
     * kLTWiFi_JoinOption_DirectBssid (bssid+channel, skip the SSID-match scan):
     * the driver's proven post-WPS connect path.
     */
    s_connectAp.bssid   = s_hostAp.bssid;
    s_connectAp.channel = s_hostAp.channel;
    s_connectAp.options = kLTWiFi_JoinOption_DirectBssid;

    /*
     * Retry loop mirroring ElkWifi wps_connect_to_AP_by_certificate(): right
     * after WPS the host SoftAP tears down the WPS association and may restart
     * for the credentialed STA, so the first join attempt can race the AP.  Elk
     * retries with a settle delay; we do the same, waiting for Connected
     * between attempts.
     */
    const int kMaxAttempts = 5;
    s_connected = false;
    for (int attempt = 1; attempt <= kMaxAttempts && !s_connected; ++attempt) {
        TILT_INFO(tilt, "Reconnect attempt %d/%d to \"%s\" (WPA2-PSK)...",
                  attempt, kMaxAttempts, s_connectAp.ssid);
        s_pWiFi->JoinAp(&s_connectAp, NULL, NULL);

        /* Wait up to ~8 s for this attempt to reach Connected. */
        for (int t = 0; t < 8 && !s_connected; ++t) {
            s_pCore->GetCurrentThreadObject()->API->Sleep(LTTime_Seconds(1));
            if (s_pWiFi->IsConnected()) {
                s_connected = true;
            }
        }
        if (!s_connected && attempt < kMaxAttempts) {
            s_pCore->GetCurrentThreadObject()->API->Sleep(LTTime_Seconds(1));   /* settle before retry */
        }
    }

    /*
     * Persist the harvested credential (stored at
     * pairing).  This lets the find-host reconnect test (UnitTestWiFiFindHost)
     * rejoin via LoadApSettings without a manual HOST_PASS.  Capture the live
     * BSSID + channel from the connected AP so a later reconnect can match the
     * host by BSSID (its SSID is cloaked outside pairing mode) and connect on
     * its primary channel.
     */
    if (s_connected) {
        LTWiFi_ApInfo apInfo;
        if (s_pWiFi->GetApInfo(&apInfo)) {
            s_connectAp.bssid   = apInfo.bssid;
            s_connectAp.channel = apInfo.channel;
        }
        if (s_pWiFi->SaveApSettings(&s_connectAp))
            TILT_INFO(tilt, "Saved AP cred bssid %02X:%02X:%02X:%02X:%02X:%02X ch %u",
                      s_connectAp.bssid.octet[0], s_connectAp.bssid.octet[1], s_connectAp.bssid.octet[2],
                      s_connectAp.bssid.octet[3], s_connectAp.bssid.octet[4], s_connectAp.bssid.octet[5],
                      (unsigned)s_connectAp.channel);
    }

    TILT_ASSERT_TRUE(tilt, s_connected, "post-WPS WPA2-PSK reconnect connected");
}

static void TestVerifyConnected(Tilt *tilt) {
    s_currentTilt = tilt;
    TILT_ASSERT_TRUE(tilt, s_connected, "post-WPS WPA2-PSK reconnect reached Connected");
}

static void TestDisconnect(Tilt *tilt) {
    s_currentTilt = tilt;
    s_pWiFi->Disconnect();
}

/** Hooks ********************************************************************/

static void BeforeAllTests(Tilt *tilt) {
    const char *tStr = tilt->API->GetProperty("PBC_TIMEOUT_SEC", "");
    u32 t = (tStr && tStr[0]) ? lt_strtou32(tStr, NULL, 10) : 0;
    s_pbcTimeoutSec = (t > 0) ? t : DEFAULT_PBC_TIMEOUT_SEC;

    const char *prefix = tilt->API->GetProperty("HOST_SSID_PREFIX", DEFAULT_HOST_SSID_PREFIX);
    lt_strncpyTerm(s_hostPrefix, (prefix && prefix[0]) ? prefix : DEFAULT_HOST_SSID_PREFIX,
                   sizeof(s_hostPrefix));

    s_pCore = LT_GetCore();
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (s_pPairing) {
        lt_destroyobject(s_pPairing);
        s_pPairing = NULL;
    }
    if (s_pWiFi) {
        s_pWiFi->NoStatusChange(OnWiFiStatus);
        s_pWiFi->Disconnect();
        lt_closelibrary(s_pWiFi);
        s_pWiFi = NULL;
    }
    if (s_pAuth) {
        lt_closelibrary(s_pAuth);
        s_pAuth = NULL;
    }
}

/** Test table ***************************************************************/

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestOpenDevice,      "OpenDevice",      "open LTDeviceWiFi",          0 },
    { TestWiFiUp,          "WiFiUp",          "wait for radio Up",          0 },
    { TestScanForHost,     "ScanForHost",     "scan for Roku host AP",      0 },
    { TestHostFound,       "HostFound",       "host AP selected",           0 },
    { TestStartWps,        "StartWpsPbc",     "start PBC + harvest cred",   0 },
    { TestGetCredentials,  "GetCredentials",  "credential available",       0 },
    { TestConnectWithCred, "ConnectWithCred", "WPA2-PSK reconnect",         0 },
    { TestVerifyConnected, "VerifyConnected", "Connected event observed",   0 },
    { TestDisconnect,      "Disconnect",      "leave AP",                   0 },
};

/** Library entry point ******************************************************/

static int UnitTestLTDeviceWiFiWpsPbcImpl_Run(int argc, const char **argv) {
    s_engine->API->ConfigureTestSuite(s_engine, s_tests,
                                      sizeof(s_tests)/sizeof(s_tests[0]),
                                      &s_hooks);
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceWiFiWpsPbcImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceWiFiWpsPbcImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiWpsPbc, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFiWpsPbc, UnitTestLTDeviceWiFiWpsPbcImpl_Run, 1536)
LTLIBRARY_DEFINITION;
