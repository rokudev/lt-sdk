/*******************************************************************************
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * TestNetCoreMultiThread - Copyright 2023, Roku, Inc.  All rights reserved.
 *
 * Purpose:
 *      Confirm that a few transport instances from different threads can all
 *      open and close without interfering with each other.
 *
 * Results:
 *      Log should show MAX_TRANSPORT_INSTANCES of transports up and downs.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

DEFINE_LTLOG_SECTION("test.net.multi");

#define P LT_GetCore()->ConsoleStomp

#define MAX_TRANSPORT_INSTANCES 4

static struct Statics { // cleared in main()
    LTCore       *iCore;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTTransport   hTransport[MAX_TRANSPORT_INSTANCES];
    LTThread      hThread[MAX_TRANSPORT_INSTANCES];
    u32           threadCount;
    u32           transCount;
    bool          debug;
} S;

static bool OpenTransport(void);

typedef struct {
    bool used;
    u8   count;
} EventData;

static bool OnThreadStart(void) {
    LTLOG("init", "init thread");
    if (!(S.iNetCore = lt_openlibrary(LTNetCore))) return false;
    return OpenTransport();
}

static void OnThreadExit(void) {
    LTLOG("exit", "exit thread");
    lt_closelibrary(S.iNetCore);
}

static bool NewThread(void) {
    if (S.threadCount >= MAX_TRANSPORT_INSTANCES) {
        LTLOG("max", "(maximum thread instances)");
        return false;
    }
    S.iThread->Sleep(LTTime_Milliseconds(500));
    LTThread thread = S.iCore->CreateThread("NetTest");
    if (!thread) return false;
    S.hThread[S.threadCount++] = thread;
    S.iThread->SetStackSize(thread, 2000);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    return true;
}

static void KillThread(void) {
    S.iThread->Terminate(S.iThread->GetCurrentThread());
}

static void DisconnectWiFi(void) {
    LTLOG("wifi.disc", "force WiFi disconnect");
    LTDeviceWiFi *iWiFi = lt_openlibrary(LTDeviceWiFi);
    iWiFi->Disconnect(); // intentionally does not attempt to rejoin
    lt_closelibrary(iWiFi);
}

static void OnTransportEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    LT_UNUSED(hTransport);
    EventData *data = clientData;
    //LTLOG("evt", "Transport 0x%04lx event: 0x%x clientData: %ld", LT_Pu32(hTransport), event, LT_Pu32(data->count));
    switch (event) {
        case kLTTransport_Event_Up:
            if (data->used) break;
            LTLOG("up", "--- net up 0x%04lx ---", LT_Pu32(hTransport));
            data->used = true;
            if (!NewThread()) DisconnectWiFi(); // we are done, force everything down
            break;
        case kLTTransport_Event_Down:
            LTLOG("down", "--- net down 0x%04lx ---", LT_Pu32(hTransport));
            S.iCore->DestroyHandle(hTransport);
            KillThread();
            break;
        default:
            break;
    }
}

static bool OpenTransport(void) {
    if (S.transCount >= MAX_TRANSPORT_INSTANCES) {
        LTLOG("max", "(maximum transport instances)");
        return false;
    }
    EventData *data = lt_malloc(sizeof(EventData));
    *data = (EventData) { .count = S.transCount };
    LTTransport hTransport = S.iNetCore->OpenTransport(0, OnTransportEvent, data);
    LTLOG("open", "OpenTransport 0x%04lx instance %ld", LT_Pu32(hTransport), LT_Pu32(S.transCount));
    if (!hTransport) return false;
    S.hTransport[S.transCount++] = hTransport;
    return true;
}

static int TestNetCoreMultiThread_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .iCore       = LT_GetCore(),
        .iThread     = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .debug       = argc > 2 && (lt_strcmp(argv[2], "-d") == 0)
    };

    NewThread();
    return 0;
}

define_LTLIBRARY_APPLICATION(TestNetCoreMultiThread, 1, 1024);
