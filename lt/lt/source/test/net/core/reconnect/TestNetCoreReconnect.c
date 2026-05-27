/*******************************************************************************
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * TestNetCoreReconnect - Copyright 2023, Roku, Inc.  All rights reserved.
 *
 * Purpose:
 *      Confirm that networking will automatically reconnect on unexpected loss of
 *      the AP link. This test uses the SetOption "disconnect" which is an "unexpected"
 *      disconnect as if the AP itself disassociated.
 *
 * Results:
 *      You should see multiple net down and net up indications.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

#define P LT_GetCore()->ConsoleStomp

#define RECONNECT_MAX 10  // how many times to disconnect/reconnect

static struct Statics { // cleared in main()
    LTCore       *iCore;
    ILTThread    *iThread;
    LTNetMonitor *iNetMonitor;
    LTDeviceWiFi *iWifi;
    u32           connectCount;
    bool          timerActive;
    bool          debug;
} S;

static void OnTimer(void *clientData) {
    LT_UNUSED(clientData);
    //S.iWifi->Disconnect(); // do not use this because it is "do not try again"
    S.iWifi->SetOption("disconnect", 0);
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimer, NULL);
    S.timerActive = false;
}

static void StatusChange(LTNetMonitor_Status status, void *clientData) { LT_UNUSED(clientData);
    if (S.debug) P("Network status: 0x%x state: %s\n", status, S.iNetMonitor->IsUp() ? "up" : "down");
    if (status == kLTNetMonitor_Status_NetworkUp) {
        P("\n****** Connected %ld times ******\n", LT_Pu32(++S.connectCount));
        if (S.connectCount < RECONNECT_MAX) {
            S.iThread->SetTimer(S.iThread->GetCurrentThread(), LTTime_Seconds(5), OnTimer, NULL, NULL);
            S.timerActive = true;
        } else {
            S.iThread->Terminate(S.iThread->GetCurrentThread());
        }
    }
}

static void OnWifiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);
    if (!S.debug) return;
    switch (status) {
        case kLTDeviceWiFi_Status_Up:
            P("**** Wifi up\n");
            break;
        case kLTDeviceWiFi_Status_Connected:
            P("**** Wifi connected!\n");
            break;
        case kLTDeviceWiFi_Status_JoinFailed:
            P("**** Wifi join failed!\n");
            break;
        case kLTDeviceWiFi_Status_Disconnected:
            P("**** Wifi disconnected!\n");
            break;
        default:
            break;
    }
}

static bool OnThreadStart(void) {
    if (!(S.iNetMonitor = lt_openlibrary(LTNetMonitor))) return false;
    S.iNetMonitor->OnStatusChange(StatusChange, NULL, NULL);
    if (!(S.iWifi = lt_openlibrary(LTDeviceWiFi))) return false;
    S.iWifi->OnStatusChange(OnWifiStatus, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    if (S.timerActive) S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimer, NULL);
    if (S.iNetMonitor) {
        S.iNetMonitor->NoStatusChange(StatusChange);
        lt_closelibrary(S.iNetMonitor);
    }
    if (S.iWifi) {
        S.iWifi->NoStatusChange(OnWifiStatus);
        //lt_closelibrary(S.iWifi);
    }
}

static int TestNetCoreReconnect_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore       = LT_GetCore(),
        .iThread     = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .debug       = argc > 2 && (lt_strcmp(argv[2], "-d") == 0)
    };
    LTThread thread = S.iCore->CreateThread("NetTest");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2000);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(600));
    return 0;
}

define_LTLIBRARY_APPLICATION(TestNetCoreReconnect, 1, 1024);
