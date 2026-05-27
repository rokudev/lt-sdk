/******************************************************************************
 * ExampleNetRpns
 *
 * This is a very basic RPNS example. It connects to a mock Roku Cloud
 * server, requests a JWT token from the token issuing service, requests an
 * RPNS key from the key service, connects to the dialtone server, and waits
 * for incoming messages.
 *
 * Usage:
 *     - Run the mock Roku cloud with:
 *         python3 tools/python/mock-cloud/roku_cloud.py
 *
 *     - Start the LT RPNS example:
 *         ltrun ExampleNetRpns
 *
 *     - Send RPNS commands to the device (interactively or scripted):
 *         Interactive: Open web browser to http://localhost:8080
 *         Scripted:    python3 tools/python/mock-cloud/dashboard_client.py <device-serial> <command>
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/rpns/LTNetRpns.h>
#include <lt/product/config/LTProductConfig.h>
#include <tilt/TiltImpl.c>
#include <example/mockcloudconfig/MockCloudConfig.h>

enum {
    kCommand_PowerOff        = 0x10,
    kCommand_PowerOn         = 0x11,
    kCommand_ConfigIndicator = 0x20,
    kCommand_PowerReport     = 0x40,
};

/** Standard Interfaces *******************************************************/

static LTCore         *s_core;
static LTNetCore      *s_netcore;
static ILTThread      *s_thread;
static LTNetRpns      *s_rpns;

/** Module Globals ************************************************************/

static LTThread             s_mainThread;
static LTHandle             s_transport;
static LTRpns               s_hRpns;
static u8                   s_power;

/** Utility Functions *********************************************************/

static void Terminate(const char *msg) {
    LTLOG("term", "Done. Reporting %s --- terminating program", msg);
    s_thread->Terminate(s_mainThread);
}

static void OnTimer(void * data) {
    LT_UNUSED(data);
    Terminate("no network");
}

static void SendPowerReport(void) {
    u8 msg[2] = { kCommand_PowerReport, s_power };
    s_rpns->WriteMessage(s_hRpns, "iot", msg, sizeof(msg));
}

static void HandleRpnsCommand(RpnsMessageBuffer *msgBuf) {
    if (msgBuf->bufLen != 1) {
        LTLOG_DEBUG("msg.badlen", "Bad message length %lu", LT_Pu32(msgBuf->bufLen));
        return;
    }

    switch(msgBuf->buf[0]) {
        case kCommand_PowerOff:
            s_power = 0;
            SendPowerReport();
            break;

        case kCommand_PowerOn:
            s_power = 1;
            SendPowerReport();
            break;
    }
}

/** Callbacks *****************************************************************/

static void OnRpnsTestEvents(LTRpns hRpns, LTRpns_Event event, void *clientData) {
    LT_UNUSED(hRpns);
    LT_UNUSED(clientData);
    switch (event) {
        case kLTRpns_Event_ReadReady: {
            RpnsMessageBuffer *msgBuf = s_rpns->ReadMessage(s_hRpns);
            if (msgBuf) {
                HandleRpnsCommand(msgBuf);
                lt_free(msgBuf);
            }
            break;
        }

        case kLTRpns_Event_DTConnected:
            LTLOG_DEBUG("dt.conn", "Connected to dialtone server");
            SendPowerReport();
            break;

        case kLTRpns_Event_WriteReady:
            break;

        case kLTRpns_Event_DTDisconnected:
            Terminate("disconnected");
            break;

        default:
            break;
    }
}

/** Main Thread Code **********************************************************/

static void OnTransportEvent(LTTransport transport, LTTransport_Event event, void *data) {
    LT_UNUSED(data);
    if (event == kLTTransport_Event_Up) {
        s_hRpns = s_rpns->Create(transport);
        s_rpns->Connect(s_hRpns, OnRpnsTestEvents, NULL);
        LTThread hThread = s_thread->GetCurrentThread();
        s_thread->KillTimer(hThread, OnTimer, LTHANDLE_TO_VOIDPTR(hThread));
    }
}

static bool OnThreadStart(void) {
    LTThread hThread = s_thread->GetCurrentThread();
    s_thread->SetTimer(hThread, LTTime_Seconds(30), OnTimer, NULL, LTHANDLE_TO_VOIDPTR(hThread));
    s_transport = s_netcore->OpenTransport("IpWifi dhcp", OnTransportEvent, NULL);
    return s_transport != 0;
}

static void OnThreadExit(void) {
    LTThread hThread = s_thread->GetCurrentThread();
    s_thread->KillTimer(hThread, OnTimer, LTHANDLE_TO_VOIDPTR(hThread));
    lt_destroyhandle(s_hRpns);
    lt_destroyhandle(s_transport);
}

static void StartRpns(void) {
    s_mainThread = s_core->CreateThread("TestRpns");
    if (!s_mainThread) return;
    s_thread = lt_getlibraryinterface(ILTThread, s_core);
    s_thread->SetStackSize(s_mainThread, 2048);
    s_thread->Start(s_mainThread, OnThreadStart, OnThreadExit);
    s_thread->WaitUntilFinished(s_mainThread, LTTime_Infinite());
    lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
}

static void TestRpns(const TiltImplReportingCallbacks *trc) {
    TILT_MESSAGE(trc, "system update start");
    StartRpns();
    TILT_MESSAGE(trc, "system update done");
}

static TiltImplTestSpecifier s_rpnsTests[] = {
    { TestRpns,            "Rpns",  "Rpns test", 0 },
};

static void ShutdownExampleNetRpns(void) {
    if (s_mainThread) lt_destroyhandle(s_mainThread);
    s_mainThread = 0;
    s_thread = NULL;
    if (s_netcore) lt_closelibrary(s_netcore);
    s_netcore = NULL;
    if (s_rpns) lt_closelibrary(s_rpns);
    s_rpns = NULL;
    s_core = NULL;
    ShutdownTilt();
}

static bool ExampleNetRpnsImpl_LibInit(void) {
    s_core   = LT_GetCore();
    s_thread = lt_getlibraryinterface(ILTThread, s_core);
    MockConfig();
    do {
        lt_openlibrary(LTProductConfig);
        s_rpns = lt_openlibrary(LTNetRpns);
        if (!s_rpns) break;
        s_netcore = lt_openlibrary(LTNetCore);
        if (!s_netcore) break;
        return InitializeTilt(s_rpnsTests, sizeof(s_rpnsTests) / sizeof(s_rpnsTests[0]), NULL);
    } while (0);

    ShutdownExampleNetRpns();
    return false;
}

static void ExampleNetRpnsImpl_LibFini(void) {
    ShutdownExampleNetRpns();
}

static int ExampleNetRpns_Run(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    StartRpns();
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleNetRpns, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(ExampleNetRpns, ExampleNetRpns_Run, 4096) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(ExampleNetRpns, (ITilt))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Feb-23   trajan      created
 */
