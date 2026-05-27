/*******************************************************************************
 *
 * TestWiFiJoinRetry Example
 *
 * This tests if a join will properly retry for an access point that has already
 * been saved with SaveApSettings(). The state machine will retry forever until
 * this program forces it to stop.
 *
 * To run:
 *    1. shut off the target AP
 *    2. run this program (provide ssid and password args if you want)
 *    3. turn the AP back on
 *    4. when the AP is back up, the join should be successful
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

#define P  LT_GetCore()->ConsolePrint

#define MAX_RETRIES 20

// When no shell arguments, use these defaults:
#define DEFAULT_WIFI_SSID "mywifiap"
#define DEFAULT_WIFI_PASS "pass1234"

static struct Statics {
    LTCore        *iCore;
    ILTThread     *iThread;
    LTDeviceWiFi  *iWifi;
    LTWiFi_ApInfo  apSpec;
    char           password[65];
    u8             failCount;
    bool           isJoining;
} S;

static void Terminate(void) {
    S.iWifi->Disconnect(); // disconnect or if not yet connected, stop trying
    S.iThread->Terminate(S.iThread->GetCurrentThread());
}

static void OnWifiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);
    switch (status) {
        case kLTDeviceWiFi_Status_JoinFailed:
            P("**** Wifi join failed %d times!\n", ++S.failCount);
            if (S.failCount > MAX_RETRIES) {
                Terminate();
                break;
            }
            // fall thru
        case kLTDeviceWiFi_Status_Up:
            P("**** Joining: %s (%s)\n", S.apSpec.ssid, S.apSpec.pass);
            if (!S.isJoining) {
                S.iWifi->SaveApSettings(&S.apSpec);
                S.iWifi->JoinAp(&S.apSpec, NULL, NULL);
                S.isJoining = true;
            }
            return;
        case kLTDeviceWiFi_Status_Connected:
            P("**** Wifi connected!\n");
            if (!S.failCount) P("???? Did you forget to turn off your AP first?\n");
            S.iThread->Sleep(LTTime_Milliseconds(1000));
            Terminate();
            break;
        default:
            // P("**** Wifi status event: %d\n", status);
            return;
    }
}

static bool OnThreadStart(void) {
    if (!(S.iWifi = lt_openlibrary(LTDeviceWiFi))) return false;
    S.iWifi->SaveApSettings(NULL); // erase settings (a race)
    S.iWifi->OnStatusChange(OnWifiStatus, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    if (S.iWifi) S.iWifi->NoStatusChange(OnWifiStatus);
    // lt_closelibrary(S.iWifi); leave it open
    P("TEST DONE\n");
}

static int TestWiFiJoinRetry_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .apSpec.pass = S.password
    };

    const char *ssid = (argc > 2) ? argv[2] : DEFAULT_WIFI_SSID;
    const char *pass = (argc > 3) ? argv[3] : DEFAULT_WIFI_PASS;
    lt_strncpyTerm(S.apSpec.ssid, ssid, sizeof(S.apSpec.ssid));
    lt_strncpyTerm(S.password, pass, sizeof(S.password));
    S.apSpec.security = kLTWiFi_ApSecurity_Wpa2;
    S.apSpec.options  = kLTWiFi_JoinOption_Retry;

    LTThread thread = S.iCore->CreateThread("WiFiJoin");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Infinite());
    return 0;
}

define_LTLIBRARY_APPLICATION(TestWiFiJoinRetry, 1, 1024);
