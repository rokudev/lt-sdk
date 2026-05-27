/*******************************************************************************
 *
 * ExampleNetMonitor - Start the network and indicate when it's ready
 *
 * Note: Make sure WiFi (or whatever default transport you need) has already been
 * set-up previously. For example, use LTShellWiFi to setup your WiFi config.
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/monitor/LTNetMonitor.h>

#define P LT_GetCore()->ConsolePrint

static struct Statics { // gets cleared in main()
    ILTThread    *iThread;
    LTNetMonitor *iNetMonitor;
} S;

static void StatusChange(LTNetMonitor_Status status, void *clientData) { LT_UNUSED(clientData);
    P("Network status: 0x%x state: %s\n", status, S.iNetMonitor->IsUp() ? "up" : "down");
    if (status == kLTNetMonitor_Status_NetworkUp) {
        P("Start your network connection here, like an HTTP request\n");
        S.iThread->Terminate(S.iThread->GetCurrentThread());
    }
}

static bool OnThreadStart(void) {
    if (!(S.iNetMonitor = lt_openlibrary(LTNetMonitor))) return false;
    S.iNetMonitor->OnStatusChange(StatusChange, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    lt_closelibrary(S.iNetMonitor);
}

static int ExampleNetMonitor_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    S = (struct Statics) { // clears all other fields
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
    };
    LTThread thread = LT_GetCore()->CreateThread("NetMonitor");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2000);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(20));
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleNetMonitor, 1, 1024);
