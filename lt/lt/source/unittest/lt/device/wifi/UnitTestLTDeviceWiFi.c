/*******************************************************************************
 *
 * UnitTestLTDeviceWiFi
 * --------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * To run: ltrun TiltEngine  UnitTestLTDeviceWiFi
 *
 * Test note for successful result:
 *
 *   1. A longer TiltEngine timeout is required for this test in order to complete
 *      all the scans and joins. See TILT_LIBRARY_TIMEOUT setting below.
 *
 *   2. Make sure there are a few APs in reception distance. If the AP scan
 *      finds no APs, that is considered a failure.
 *
 *   3. Pass AP_SSID and AP_PASS as Tilt test properties for making connections to your AP.
 *
 ******************************************************************************/

#include <lt/LT.h>
#define TILT_LIBRARY_TIMEOUT 90
#include <tilt/TiltImpl.c>
#include <lt/device/wifi/LTDeviceWiFi.h>

//#define P LT_GetCore()->ConsolePrint
#define CLEAR(v) lt_memset(&(v), 0, sizeof(v))

#define DEFAULT_AP_SSID   "mywifiap"  // override with AP_SSID test parameter
#define DEFAULT_AP_PASS   "pass1234"  // override with AP_PASS test parameter
#define MAX_JOIN_TIME     20          // max time for AP to connect (timeout)
#define JOIN_SETTLE_TIME  3           // seconds to delay after a join to "let it settle"

static struct Statics {
    LTCore              *core;
    LTDeviceWiFi        *wifiLib;
    LTUtilityMacAddress *macLib;
    const char          *doneSignal;
    u16                  apCount;
    u16                  apFound;
    bool                 deviceIsUp;
    bool                 upDeferred;
    bool                 isConnected;
    bool                 joinFailed;
    bool                 ignoreDisconnect;
    bool                 joinStarted;
    bool                 wifiAutoJoin;
    bool                 wifiRejoinEnabled;
    const char          *ssid;
    const char          *pass;
    LTWiFi_ApInfo        ap;
} S;

// Strings below must match LTDeviceWiFi.h kLTDeviceWiFi_Status
static const char *TestWiFi_StatusStrings[kLTDeviceWiFi_Status_Max] = {
    "unknown",
    "reset",
    "down",
    "start",
    "up",
    "scan-start",
    "scan-result",
    "scan-done",
    "join-start",
    "join-assoc",
    "join-auth",
    "join-done",
    "join-failed",
    "connected",
    "disconnected",
    "rejoining",
};

static const char *ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

static void TestOpenDevice(const TiltImplReportingCallbacks *trc) {
    if (!(S.wifiLib = lt_openlibrary(LTDeviceWiFi))) {
        TILT_REPORT_FAILURE(trc, "cannot open LTDeviceWiFi");
        return;
    }
    if (!(S.macLib = lt_openlibrary(LTUtilityMacAddress))) {
        TILT_REPORT_FAILURE(trc, "cannot open LTUtilityMacAddress");
        return;
    }
    // Save WiFi variables to restore after testing
    S.wifiAutoJoin = S.wifiLib->GetOption("autojoin");
    S.wifiRejoinEnabled = S.wifiLib->GetOption("rejoin");
    // set auto reconnection to false to prevent unexpected failure
    S.wifiLib->SetOption("setting_clear", 1 /* Save setting */);
    S.wifiLib->Disconnect();
}

static void HandleWiFiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) { LT_UNUSED(unit);
    TiltImplReportingCallbacks *trc = (TiltImplReportingCallbacks *)clientData;

    const char *msg = "bad-status";
    if (status <= kLTDeviceWiFi_Status_Rejoining) msg = TestWiFi_StatusStrings[status];
    TILT_MESSAGE(trc, "wifi status: %s", msg);

    switch (status) {
        case kLTDeviceWiFi_Status_Up:
            S.deviceIsUp = true;
            if (S.upDeferred) {
                trc->SignalCompletion("WiFiUp");
                TILT_ASSERT_TRUE(trc, true, "WiFiUp");
            }
            break;
        case kLTDeviceWiFi_Status_Connected:
            S.isConnected = true;
            break;
        case kLTDeviceWiFi_Status_Disconnected:
            S.isConnected = false;
            if (!S.ignoreDisconnect) {
                S.ignoreDisconnect = true;
                trc->SignalCompletion("Disconnect");
                TILT_ASSERT_TRUE(trc, !S.wifiLib->IsConnected(), "not connected");
            }
            break;
        case kLTDeviceWiFi_Status_JoinFailed:
            S.joinFailed = true;
            if (lt_strcmp("JoinFailed", S.doneSignal) == 0) {
                trc->SignalCompletion(S.doneSignal);
            }
            break;
        case kLTDeviceWiFi_Status_JoinStart:
            S.joinStarted = true;
            break;
        default:
            break;
    }
}

static void TestWiFiUp(const TiltImplReportingCallbacks *trc) {
    S.wifiLib->OnStatusChange(HandleWiFiStatus, NULL, (void*)trc);
    // The above may trigger an immediate UP event, but not always.
    // This causes a race condition. However, Because this test and the
    // event handler run in the same thread, use booleans to solve the problem.
    if (S.deviceIsUp) {
        TILT_ASSERT_TRUE(trc, true, "WiFiUp");
    } else {
        S.upDeferred = true;
        trc->DeferCompletion(LTTime_Seconds(3), NULL, NULL);
    }
}

static void TestGetDeviceInfo(const TiltImplReportingCallbacks *trc) {
    LTWiFi_DriverInfo info;
    bool okay = S.wifiLib->GetDeviceInfo(&info);
    TILT_ASSERT_TRUE(trc, okay, "GetDeviceInfo");
    if (!okay) return;
    TILT_EXPECT_FALSE(trc, S.macLib->IsZero(&info.mac_address), "MAC is zero");
    char str[20] = "?";  // !!! use const from lib include
    S.macLib->MacAddressToString(&info.mac_address, str, ':');
    TILT_MESSAGE(trc, "Vendor:  %s", info.vendor);
    TILT_ASSERT_FALSE(trc, lt_strlen(info.vendor) == 0, "no vendor");
    TILT_MESSAGE(trc, "Product: %s", info.product);
    TILT_ASSERT_FALSE(trc, lt_strlen(info.product) == 0, "no product");
    TILT_MESSAGE(trc, "Version: %s", info.version);
    TILT_ASSERT_FALSE(trc, lt_strlen(info.version) == 0, "no version");
    TILT_MESSAGE(trc, "Updated: %s", info.updated);
    TILT_ASSERT_FALSE(trc, lt_strlen(info.updated) == 0, "no updated");
    TILT_MESSAGE(trc, "MacAddr: %s", str);
}

static void TestGetMacAddress(const TiltImplReportingCallbacks *trc) {
    LTMacAddress mac;
    bool okay = S.wifiLib->GetMacAddress(&mac);
    TILT_ASSERT_TRUE(trc, okay, "GetMacAddress");
    if (!okay) return;
    TILT_EXPECT_FALSE(trc, S.macLib->IsZero(&mac), "MAC is zero");
    char str[20] = "?";
    S.macLib->MacAddressToString(&mac, str, ':');
    TILT_MESSAGE(trc, "MacAddr: %s", str);
}

static void HandleScanAps(LTWiFi_ApInfo *ap, void *clientData) {
    TiltImplReportingCallbacks *trc = (TiltImplReportingCallbacks *)clientData;
    if (!ap) {
        TILT_ASSERT_TRUE(trc, S.apCount > 0, "no APs found");
        trc->SignalCompletion("ScanAps");
        return;
    }
    S.apCount++;
    if (lt_strcmp(S.ssid, ap->ssid) == 0) S.apFound++;
    char bssid[20];
    S.macLib->MacAddressToString(&ap->bssid, bssid, ':');
    TILT_MESSAGE(trc, "  %s chan: %3u RSSI: %4d sec: %-6s SSID: \"%s\"", bssid, ap->channel, ap->rssi, ApSecurityStrings[ap->security], ap->ssid);
}

static void TestScanAps(const TiltImplReportingCallbacks *trc) {
    S.apCount = 0;
    S.wifiLib->ScanAps(NULL, HandleScanAps, (void*)trc);
    trc->DeferCompletion(LTTime_Seconds(8), NULL, NULL);
}

static void TestFoundAp(const TiltImplReportingCallbacks *trc) {
    TILT_ASSERT_TRUE(trc, S.apFound > 0, "specified AP not found");
    TILT_MESSAGE(trc, "AP found %d times", S.apFound);
}

static void ZeroFoundAp(const TiltImplReportingCallbacks *trc) {
    LT_UNUSED(trc);
    S.apFound = 0;
}

static void HandleJoinAp(LTWiFi_JoinStatus status, void *clientData);
static void HandleRetryAp(LTWiFi_JoinStatus status, LTDeviceUnit unit, void *clientData);

static void StartJoinAp(const TiltImplReportingCallbacks *trc, const char *ssid, char *pass, u16 options) {
    CLEAR(S.ap);
    S.ap.pass = pass;
    S.ap.options = options;
    if (ssid) {
        lt_strncpyTerm(S.ap.ssid, ssid, kLTWiFi_Max_Ssid + 1);
    }
    S.ap.security = kLTWiFi_ApSecurity_Wpa2;
    S.joinStarted = false;
    S.joinFailed = false;
    TILT_MESSAGE(trc, "Joining SSID: %s pass: %s security: %d", S.ap.ssid, S.ap.pass, S.ap.security);
    if (S.ap.options & kLTWiFi_JoinOption_Retry) {
        S.wifiLib->OnStatusChange(HandleRetryAp, NULL, (void*)trc);
        S.wifiLib->JoinAp(&S.ap, NULL, (void*)trc);
    } else {
        S.wifiLib->JoinAp(&S.ap, HandleJoinAp, (void*)trc);
    }
    trc->DeferCompletion(LTTime_Seconds(MAX_JOIN_TIME), NULL, NULL);
}

static void HandleJoinAp(LTWiFi_JoinStatus status, void *clientData) {
    const TiltImplReportingCallbacks *trc = (TiltImplReportingCallbacks *)clientData;
    char *str;
    bool okay = false;
    if (!S.joinStarted) {
        TILT_MESSAGE(trc, "join status: not started...");
        return;
    }

    switch (status) {
        case kLTWiFi_JoinStatus_Failed:
            str = "failed to connect";
            break;
        case kLTWiFi_JoinStatus_Success:
            okay = true;
            str = "connected to AP";
            break;
        default:
            TILT_MESSAGE(trc, "join status: in progress...");
            return;
    }
    TILT_MESSAGE(trc, "join status: %s", str);

    if (lt_strstr(S.doneSignal, "Bad")) okay = !okay;
    TILT_ASSERT_TRUE(trc, okay, str);
    trc->SignalCompletion(S.doneSignal);
}

static void HandleRetryAp(LTWiFi_JoinStatus status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    const TiltImplReportingCallbacks *trc = (TiltImplReportingCallbacks *)clientData;
    if (!S.joinStarted) {
        TILT_MESSAGE(trc, "join status: not started...");
        return;
    }

    switch (status) {
        case kLTDeviceWiFi_Status_Connected:
            TILT_MESSAGE(trc, "connected to AP");
            S.wifiLib->NoStatusChange(HandleRetryAp);
            trc->SignalCompletion(S.doneSignal);
            break;
        default:
            TILT_MESSAGE(trc, "join status: in progress...");
            break;;
    }
}

static void TestJoinAp(const TiltImplReportingCallbacks *trc) {
    S.doneSignal = trc->GetTestName();
    StartJoinAp(trc, S.ssid, (char *)S.pass, 0);
}

static void TestRetryAp(const TiltImplReportingCallbacks *trc) {
    // TestJoinAp() but with JoinOption_Retry enabled
    S.doneSignal = trc->GetTestName();
    StartJoinAp(trc, S.ssid, (char *)S.pass, kLTWiFi_JoinOption_Retry);
}

static void TestRejoinAp(const TiltImplReportingCallbacks *trc) {
    S.doneSignal = trc->GetTestName();
    TILT_MESSAGE(trc, "Rejoining current ssid\n");
    StartJoinAp(trc, NULL, NULL, kLTWiFi_JoinOption_Rejoin);
}

static void TestJoinFailed(const TiltImplReportingCallbacks *trc) {
    if (!S.joinFailed) {
        // We haven't yet received join failed event. Defer to HandleWiFiStatus()
        S.doneSignal = trc->GetTestName();
        trc->DeferCompletion(LTTime_Seconds(MAX_JOIN_TIME), NULL, NULL);
    }
}

static void TestJoinBadPass(const TiltImplReportingCallbacks *trc) {
    S.doneSignal = trc->GetTestName();
    StartJoinAp(trc, S.ssid, "badpassword", 0);
}

static void TestJoinBadSsid(const TiltImplReportingCallbacks *trc) {
    S.doneSignal = trc->GetTestName();
    StartJoinAp(trc, "badssid", (char *)S.pass, 0);
}

static void TestIsConnected(const TiltImplReportingCallbacks *trc) {
    TILT_ASSERT_TRUE(trc, S.wifiLib->IsConnected(), "is connected");
}

static void TestIsNotConnected(const TiltImplReportingCallbacks *trc) {
    TILT_ASSERT_FALSE(trc, S.wifiLib->IsConnected(), "is not connected");
}

static void TestGetApInfo(const TiltImplReportingCallbacks *trc) {
    LTWiFi_ApInfo ap;
    bool okay = S.wifiLib->GetApInfo(&ap);
    TILT_ASSERT_TRUE(trc, okay, "GetDeviceInfo");
    if (!okay) return;
    char bssid[20] = "?";
    TILT_MESSAGE(trc, "SSID:     %s", ap.ssid);
    TILT_ASSERT_FALSE(trc, lt_strlen(ap.ssid) == 0, "no ssid");
    TILT_EXPECT_FALSE(trc, S.macLib->IsZero(&ap.bssid), "bssid is zero");
    S.macLib->MacAddressToString(&ap.bssid, bssid, ':');
    TILT_MESSAGE(trc, "BSSID:    %s", bssid);
    TILT_MESSAGE(trc, "Channel:  %u", ap.channel);
    TILT_ASSERT_FALSE(trc, ap.channel == 0, "no channel");
    TILT_MESSAGE(trc, "RSSI:     %d", ap.rssi);
    TILT_ASSERT_FALSE(trc, ap.rssi == 0, "no RSSI");
    TILT_MESSAGE(trc, "Security: %s", ApSecurityStrings[ap.security]);
    TILT_ASSERT_FALSE(trc, ap.security == 0, "no security");
}

static void TestGetNoApInfo(const TiltImplReportingCallbacks *trc) {
    LTWiFi_ApInfo ap;
    bool okay = S.wifiLib->GetApInfo(&ap);
    TILT_ASSERT_FALSE(trc, okay, "!GetDeviceInfo");
}

static void TestDisconnect(const TiltImplReportingCallbacks *trc) {
    S.wifiLib->Disconnect();
    S.ignoreDisconnect = false;
    trc->DeferCompletion(LTTime_Seconds(MAX_JOIN_TIME), NULL, NULL);
}

static void TestDisconnectReason(const TiltImplReportingCallbacks *trc) {
    // If this driver returns a valid kLTWiFi_DisconnectReason_* it
    // should be "kLTWiFi_DisconnectReason_ApReceiveDisassoc"
    // or "kLTWiFi_DisconnectReason_ApReceiveDeauth" after a disconnect
    LTWiFi_DisconnectReason dcrsn = S.wifiLib->GetOption("DisconnectReason");
    if (dcrsn != kLTWiFi_DisconnectReason_Unknown) {
        TILT_EXPECT_TRUE(trc, dcrsn == kLTWiFi_DisconnectReason_ApReceiveDisassoc
                         || dcrsn == kLTWiFi_DisconnectReason_ApReceiveDeauth
                         || dcrsn == kLTWiFi_DisconnectReason_System,
                         "DisconnectReason(%d) wasn't ApReceiveDisassoc/Deauth/System", dcrsn);
    }
}

static void TestBadSsidReason(const TiltImplReportingCallbacks *trc) {
    // If this driver returns a valid kLTWiFi_DisconnectReason_* it
    // should be "kLTWiFi_DisconnectReason_DriverNoApFound"
    // after trying to join a non-existent AP
    LTWiFi_DisconnectReason dcrsn = S.wifiLib->GetOption("DisconnectReason");
    if (dcrsn != kLTWiFi_DisconnectReason_Unknown) {
        TILT_EXPECT_TRUE(trc, dcrsn == kLTWiFi_DisconnectReason_DriverNoApFound,
                         "DisconnectReason(%d) wasn't DriverNoApFound", dcrsn);
    }
}

static void TestBadPassReason(const TiltImplReportingCallbacks *trc) {
    // If this driver returns a valid kLTWiFi_DisconnectReason_* it
    // should be "kLTWiFi_DisconnectReason_ApReceiveDeauth"
    // after trying to join with a bad password
    LTWiFi_DisconnectReason dcrsn = S.wifiLib->GetOption("DisconnectReason");
    if (dcrsn != kLTWiFi_DisconnectReason_Unknown) {
        TILT_EXPECT_TRUE(trc, dcrsn == kLTWiFi_DisconnectReason_ApReceiveDeauth
                         || dcrsn == kLTWiFi_DisconnectReason_DriverApKeepaliveTimeout
                         || dcrsn == kLTWiFi_DisconnectReason_Generic,
                         "DisconnectReason(%d) wasn't ApReceiveDeauth, DriverApKeepaliveTimeout "
                         "or kLTWiFi_DisconnectReason_Generic", dcrsn);
    }
}

static void DelaySettle(const TiltImplReportingCallbacks *trc) {
    LT_UNUSED(trc);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->Sleep(LTTime_Seconds(JOIN_SETTLE_TIME));
}

static void BeforeAllTestsHook(const TiltImplReportingCallbacks * trc) {
    S.ssid = trc->GetProperty("AP_SSID", DEFAULT_AP_SSID);
    S.pass = trc->GetProperty("AP_PASS", DEFAULT_AP_PASS);
    S.ignoreDisconnect = true;
}
static void AfterAllTestsHook(const TiltImplReportingCallbacks * trc) {
    LT_UNUSED(trc);
    S.wifiLib->SetOption("autojoin", S.wifiAutoJoin);
    S.wifiLib->SetOption("rejoin", S.wifiRejoinEnabled);
    S.wifiLib->Disconnect();
}

static TiltImplTestHooks testHooks = {
    .BeforeAllTests = BeforeAllTestsHook,
    .AfterAllTests = AfterAllTestsHook
};

static bool UnitTestLTDeviceWiFiImpl_LibInit(void) {
    S = (struct Statics){ .core = LT_GetCore() };

    static const TiltImplTestSpecifier tests[] = {
        { TestOpenDevice,     "OpenDevice",     "open device",           0 },
        { DelaySettle,        "DelaySettle",    "let it settle",         0 },

        TILT_TEST_SECTION("General information"),

        { TestWiFiUp,         "WiFiUp",         "confirm wifi is up",    0 },
        { TestGetDeviceInfo,  "GetDeviceInfo",  "vendor, product, etc",  0 },
        { TestGetMacAddress,  "GetMacAddress",  "get mac address",       0 },
        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },
        { TestGetNoApInfo,    "GetNoApInfo",    "no AP info",            0 },

        //*** Connect/Disconnect tests *************************************

        TILT_TEST_SECTION("Join the specified AP"),

        { TestRetryAp,        "RetryAp",        "join AP, Retry=true",   0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },
        { TestGetApInfo,      "GetApInfo",      "get ap information",    0 },
        { DelaySettle,        "DelaySettle",    "let it settle",         0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },

        TILT_TEST_SECTION("Disconnect while associated"),

        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestDisconnectReason,"DisconnectReason","disconnect reason",   0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },
        { TestGetNoApInfo,    "GetNoApInfo",    "no AP info",            0 },

        TILT_TEST_SECTION("Disconnect while not associated (no-op)"),

        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },

        TILT_TEST_SECTION("Rejoin after explicit disconnect"),

        { TestJoinAp,         "JoinAp",         "join AP",               0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },
        { TestGetApInfo,      "GetApInfo",      "get ap information",    0 },
        { DelaySettle,        "DelaySettle",    "let it settle",         0 },

        TILT_TEST_SECTION("Rejoin while associated"),

        { TestRejoinAp,       "RejoinAp",       "rejoin AP",             0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },
        { TestGetApInfo,      "GetApInfo",      "get ap information",    0 },
        { DelaySettle,        "DelaySettle",    "let it settle",         0 },

        //*** Scan tests ***************************************************
        // Note: enter this test while still associated to save test time

        TILT_TEST_SECTION("Scan while associated"),

        { ZeroFoundAp,        "ZeroFoundAp",    "reset counter",         0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestFoundAp,        "FoundAp",        "check for specific AP", 0 },

        TILT_TEST_SECTION("Scan while not associated"),

        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },
        { ZeroFoundAp,        "ZeroFoundAp",    "reset counter",         0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestScanAps,        "ScanAps",        "scan for APs",          0 },
        { TestFoundAp,        "FoundAp",        "check for specific AP", 0 },

        TILT_TEST_SECTION("Rejoin after scan tests"),

        { TestJoinAp,         "JoinAp",         "join AP",               0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },
        { TestGetApInfo,      "GetApInfo",      "get ap information",    0 },
        { DelaySettle,        "DelaySettle",    "let it settle",         0 },
        { TestIsConnected,    "IsConnected",    "verify connection",     0 },

        //*** Attempt to join with bad credentials **************************

        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },

        TILT_TEST_SECTION("Fail to join with bad SSID, no rejoin"),

        { TestJoinBadSsid,    "JoinBadSsid",    "fail with bad SSID",    0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },
        { TestJoinFailed,     "JoinFailed",     "verify no rejoin",      0 },
        { TestBadSsidReason,  "BadSsidReason",  "verify dcrsn = NoAp",   0 },

        TILT_TEST_SECTION("Fail to join with bad password"),

        { TestJoinBadPass,    "JoinBadPass",    "fail with bad password",0 },
        { TestIsNotConnected, "IsNotConnected", "verify no connection",  0 },
        { TestBadPassReason,  "BadPassReason",  "verify dcrsn = auth",   0 },

        TILT_TEST_SECTION("Restore WiFi connection"),

        { TestDisconnect,     "Disconnect",     "disconnect AP",         0 },
        { TestRetryAp,        "RetryAp",        "join AP, Retry=true",   0 },
    };

    return InitializeTilt(tests, sizeof(tests)/sizeof(tests[0]), &testHooks);
}

static void UnitTestLTDeviceWiFiImpl_LibFini(void) {
    ShutdownTilt();
    lt_closelibrary(S.macLib);
    // Let console output finish before possible crash on exit (on ESP32):
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    iThread->Sleep(LTTime_Seconds(2));
    lt_closelibrary(S.wifiLib);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFi, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFi) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(UnitTestLTDeviceWiFi, (ITilt))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Mar-23   hadrian     implementation using Tilt framework
 *  06-Apr-23   hadrian     rework to help ESP32 get further without crashing
 */
