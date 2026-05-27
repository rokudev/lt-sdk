/*******************************************************************************
 *
 * WiFiScan Example
 *
 * This example shows of how to scan for access points and print AP information.
 *
 * ltrun ExampleWiFiScan
 *
 * The scan will be repeated every few seconds (see defines below).
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

#define MAX_SCANS   5
#define SCAN_PERIOD LTTime_Seconds(3)

static struct Statics { // gets cleared in main()
    LTCore              *core;
    ILTThread           *iThread;
    LTDeviceWiFi        *iWifi;
    LTUtilityMacAddress *iMac;
    LTWiFi_ScanSpec      scanSpec;
    u16                  scanCount;
    u16                  apCount;
} S;

static const char *ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

static void Terminate(void) {
    lt_consoleprint("**** Thread terminating\n");
    S.iThread->Terminate(S.iThread->GetCurrentThread()); // kills timer also
}

static void OnScanAp(LTWiFi_ApInfo *ap, void *clientData); // forward

static void OnTimer(void *clientData) {
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimer, clientData);
    if (++S.scanCount > MAX_SCANS) Terminate();
    else S.iWifi->ScanAps(&S.scanSpec, OnScanAp, clientData);
}

static void OnScanAp(LTWiFi_ApInfo *ap, void *clientData) {
    if (!ap) {
        lt_consoleprint("*** Scan complete. Found %u APs.\n\n", S.apCount);
        S.iThread->SetTimer(S.iThread->GetCurrentThread(), SCAN_PERIOD, OnTimer, NULL, clientData);
        return;
    }
    S.apCount++;
    char bssid[20];  // !!! define in MacAddr lib
    S.iMac->MacAddressToString(&ap->bssid, bssid, ':');
    lt_consoleprint("  %2u: %s chan: %3u rssi: %4d sec: %-6s ssid: \"%s\"\n",
        S.apCount, bssid, ap->channel, ap->rssi, ApSecurityStrings[ap->security], ap->ssid);
}

static void OnWifiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    switch (status) {
        case kLTDeviceWiFi_Status_Up:
            lt_consoleprint("**** Wifi up\n");
            S.iWifi->ScanAps(&S.scanSpec, OnScanAp, clientData);
            break;
        case kLTDeviceWiFi_Status_ScanStart:
            S.apCount = 0;
            lt_consoleprint("**** Wifi scanning...\n");
            break;
        default:
            break;
    }
}

static bool OnThreadStart(void) {
    if (!(S.iWifi = lt_openlibrary(LTDeviceWiFi))) return false;
    if (!(S.iMac = lt_openlibrary(LTUtilityMacAddress))) {
        lt_closelibrary(S.iWifi);
        return false;
    }
    S.iWifi->OnStatusChange(OnWifiStatus, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    if (S.iWifi) S.iWifi->NoStatusChange(OnWifiStatus);
    lt_closelibrary(S.iMac);
    //lt_closelibrary(S.iWifi); // on ESP32 cannot close WiFi
}

static int ExampleWiFiScan_Main(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    S = (struct Statics) { // clears all other fields
        .core = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
    };
    LTThread thread = S.core->CreateThread("WiFiScan");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Infinite());
    S.iThread->Destroy(thread);
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleWiFiScan, 1, 1024);
