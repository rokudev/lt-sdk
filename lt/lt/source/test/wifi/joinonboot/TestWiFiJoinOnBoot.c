/*******************************************************************************
 *
 * TestWiFiJoinOnBoot - Test Join on fresh boot (over and over)
 *
 * Starts WiFi networking, joins an AP, and reads a web page.
 * When successful, reboots the device. Set ltshell.boot.cmd "ltrun TestWiFiJoinOnBoot"
 * to cycle the test continuously.
 *
 * If the join cannot be completed in the given time, then this program
 * does not reboot, but returns to the shell for further investigation.
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
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

#define TEST_TIMEOUT_SECONDS 30 // Maximum time to wait for network connection

#define MAX_SOCKET_SPEC   80    // length of socket spec
#define MAX_HTTP_REQUEST  80    // length of HTTP request
#define MAX_HTTP_REPLY    2048  // lenfth of HTTP reply (per read)

static struct Statics { // gets cleared in main()
    LTCore       *core;
    ILTThread    *iThread;
    LTNetCore    *iNetCore;
    LTNetMonitor *iNetMonitor;
    char         *sockSpec;
    LTSocket      hSocket;
    char         *request;
    char         *reply;
    bool          requestSent;
} S;

static void Reboot(void) {
    LTDeviceWatchdog *pWatchdog = lt_openlibrary(LTDeviceWatchdog);
    pWatchdog->Reboot();
    // lt_closelibrary(pWatchdog); unnecessary and races with reboot
}

static void OnSocketEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(clientData);
    // lt_consoleprint("\n=== sock event: socket H%04lx event 0x%x\n", LT_Pu32(socket), event);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (S.requestSent) break;
            S.requestSent = true;
            pSocket->WriteSocket(socket, S.request, lt_strlen(S.request));
            break;
        case kLTSocket_Event_ReadReady:;
            s32 len = 0;
            do {
                len = pSocket->ReadSocket(socket, S.reply, MAX_HTTP_REPLY - 1);
                S.reply[len] = 0;
                LT_GetCore()->ConsoleStompString(S.reply);
            } while (len > 0);
            break;
        case kLTSocket_Event_Disconnected:
            lt_consoleprint("Disconnect and reboot...\n");
            pSocket->DisconnectSocket(socket);
            Reboot();
            // The following line should be meaningless given the reboot, but from time-to-time the
            // reboot does not work, and this termination must be done.
            S.iThread->Terminate(S.iThread->GetCurrentThread());
            break;
        default:
            break;
    }
}

static void StatusChange(LTNetMonitor_Status status, void *clientData) { LT_UNUSED(clientData);
    if (status == kLTNetMonitor_Status_NetworkUp && !S.hSocket) {
        lt_consoleprint("Open socket: %s\n", S.sockSpec);
        S.hSocket = S.iNetCore->OpenSocket(0, S.sockSpec, OnSocketEvent, clientData);
    }
}

static bool OnThreadStart(void) {
    if (!(S.iNetMonitor = lt_openlibrary(LTNetMonitor))) return false;
    if (!(S.iNetCore    = lt_openlibrary(LTNetCore))) return false;
    S.iNetMonitor->OnStatusChange(StatusChange, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    lt_closelibrary(S.iNetCore);         // null ok
    lt_closelibrary(S.iNetMonitor);      // null ok
    lt_consoleprint("Test failed (timeout)\n\n");
}

static int TestWiFiJoinOnBoot_Main(int argc, const char **argv) {
    S = (struct Statics) { // clears all other fields
        .core = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
    };

    char *buffers = lt_malloc(MAX_SOCKET_SPEC + MAX_HTTP_REPLY + MAX_HTTP_REQUEST);
    if (!buffers) return -1;
    S.sockSpec = buffers;
    S.reply    = S.sockSpec + MAX_SOCKET_SPEC;
    S.request  = S.reply    + MAX_HTTP_REPLY;

    const char *host = (argc > 2) ? argv[2] : "example.com";
    lt_snprintf(S.sockSpec, MAX_SOCKET_SPEC, "tcp host: %s port: 80", host);
    lt_snprintf(S.request,  MAX_HTTP_REQUEST, "GET / HTTP/1.1\r\nHost: %s:80\r\nConnection: close\r\n\r\n", host);

    LTThread thread = S.core->CreateThread("TestWiFi");
    if (!thread) return -1;
    S.iThread->SetStackSize(thread, 2048);
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(TEST_TIMEOUT_SECONDS));
    S.iThread->Destroy(thread);
    lt_free(buffers);
    return 0;
}

define_LTLIBRARY_APPLICATION(TestWiFiJoinOnBoot, 1, 1024);
