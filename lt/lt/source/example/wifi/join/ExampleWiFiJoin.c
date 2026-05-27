/*******************************************************************************
 *
 * WiFiJoin Example
 *
 * This is an example of how to join a WiFi access point.
 *
 * ltrun ExampleWiFiJoin <ssid> <password>
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

// When no shell arguments, use these defaults:
#define DEFAULT_WIFI_SSID "mywifiap"
#define DEFAULT_WIFI_PASS "pass1234"

#define CONNECT_SECONDS 5  // how long to stay connected

static struct Statics {
    LTCore        *core;
    ILTThread     *iThread;
    LTDeviceWiFi  *iWifi;
    LTWiFi_ApInfo  ApSpec;
    char           Password[65];
} S;

static void Terminate(void) {
    lt_consoleprint("**** Thread terminating\n");
    S.iThread->Terminate(S.iThread->GetCurrentThread()); // kills the timer also
}

static void OnTimer(void *clientData) {
    S.iWifi->Disconnect();
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimer, clientData);
}

static void OnWifiJoinStatus(LTWiFi_JoinStatus status, void *clientData) {
    LT_UNUSED(clientData);
    const char *stat;
    switch (status) {
        case kLTWiFi_JoinStatus_Scanning: stat = "scanning"; break;
        case kLTWiFi_JoinStatus_Success:  stat = "success";  break;
        case kLTWiFi_JoinStatus_Failed:   stat = "failed";   break;
        default:
            lt_consoleprint("---- Other join status: %d\n", status);
            return;
    }
    lt_consoleprint("---- Join status: %s\n", stat);
}

static void OnWifiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);

    switch (status) {
        case kLTDeviceWiFi_Status_Up:
            lt_consoleprint("**** Wifi up\n");
            lt_consoleprint("**** Attempt to join: %s (%s)\n", S.ApSpec.ssid, S.ApSpec.pass);
            S.iWifi->JoinAp(&S.ApSpec, OnWifiJoinStatus, clientData);
            break;
        case kLTDeviceWiFi_Status_Connected:
            lt_consoleprint("**** Wifi connected!\n");
            lt_consoleprint("**** WiFi disconnect in %d seconds...\n", CONNECT_SECONDS);
            S.iThread->SetTimer(S.iThread->GetCurrentThread(), LTTime_Seconds(CONNECT_SECONDS), OnTimer, NULL, NULL);
            break;
        case kLTDeviceWiFi_Status_JoinFailed:
            lt_consoleprint("**** Wifi join failed!\n");
            Terminate();
            break;
        case kLTDeviceWiFi_Status_Disconnected:
            lt_consoleprint("**** Wifi disconnected!\n");
            Terminate();
            break;
        default:
            break;
    }
}

static bool OnThreadStart(void) {
    if (!(S.iWifi = lt_openlibrary(LTDeviceWiFi))) return false;
    S.iWifi->OnStatusChange(OnWifiStatus, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    if (S.iWifi) S.iWifi->NoStatusChange(OnWifiStatus);
    //lt_closelibrary(S.iWifi); // on ESP32 cannot close WiFi
}

static int ExampleWiFiJoin_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .core = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .ApSpec.pass = S.Password
    };

    const char *ssid = (argc > 2) ? argv[2] : DEFAULT_WIFI_SSID;
    const char *pass = (argc > 3) ? argv[3] : DEFAULT_WIFI_PASS;
    lt_strncpyTerm(S.ApSpec.ssid, ssid, sizeof(S.ApSpec.ssid));
    lt_strncpyTerm(S.Password, pass, sizeof(S.Password));
    S.ApSpec.security = kLTWiFi_ApSecurity_Wpa2;

    LTThread thread = S.core->CreateThread("WiFiJoin");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Infinite());
    S.iThread->Destroy(thread);
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleWiFiJoin, 1, 1024);
