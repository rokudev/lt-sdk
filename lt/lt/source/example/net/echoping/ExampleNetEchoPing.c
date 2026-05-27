/*******************************************************************************
 *
 * LTNetCore Example: ICMP Echo (Ping) using LTNetEcho Protocol Library
 *
 * Very simple example to ping the remote address specified on the command line.
 *
 * How to run:
 *      ltopen LTShellWiFi
 *      wifi join <ssid> <password>
 *      ltopen LTNetMonitor
 *      ltrun ExampleNetEchoPing
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/echo/LTNetEcho.h>

static struct Statics {
    LTCore     *core;
    LTNetEcho  *echoLibrary;
    char        argAddress[32]; // stores command line argument for address
    char       *remoteAddress;  // if NULL, then ping gateway address
    LTTime      timeSent;
} S;

#define P S.core->ConsolePrint

static void OnPingEvent(LTHandle ping, u16 id, u16 count, void *clientData) { LT_UNUSED(ping); LT_UNUSED(clientData);
    if (!id) { // indicates ping sent event
        P("sent ping %2u\n", count);
        S.timeSent = S.core->GetKernelTime();
        return;
    }
    u32 msec = (u32)LTTime_GetMicroseconds(LTTime_Subtract(S.core->GetKernelTime(), S.timeSent));
    P("got  ping %2lu in %lu.%03lu msec\n", LT_Pu32(count), LT_Pu32(msec/1000), LT_Pu32(msec%1000));
}

static bool OnThreadStart(void) {
    S.echoLibrary = lt_openlibrary(LTNetEcho);
    if (S.echoLibrary) {
        S.echoLibrary->Ping(0, S.remoteAddress, 1234, 10, 500, NULL, 0, OnPingEvent, NULL);
        return true;
    }
    P("Initialization failed\n");
    return false;
}

static void OnThreadExit(void) {
    lt_closelibrary(S.echoLibrary);
}

static int ExampleNetEchoPing_Main(int argc, const char ** argv) {
    // usage: ping [<argAddress>]
    S = (struct Statics) {  // unspecified fields get cleared to zero
        .core = LT_GetCore()
    };
    if (argc > 2) {
        lt_strncpyTerm(S.argAddress, argv[2], sizeof(S.argAddress)-1);
        S.remoteAddress = S.argAddress;
    }
    LTThread thread = S.core->CreateThread("EchoPing");
    if (!thread) return -1;
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->SetStackSize(thread, 2048);
    iThread->Start(thread, OnThreadStart, OnThreadExit);
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleNetEchoPing, 1, 2048);
