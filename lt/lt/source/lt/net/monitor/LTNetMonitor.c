/*******************************************************************************
 *
 * LTNetMonitor - Network Startup and Monitoring
 * ---------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * The main purpose of this module is to monitor the network connection at
 * both layers (data-link and IP). In order to do that, it watches low level
 * data traffic via the LTNetCore->IsOperating() API. When no traffic is heard
 * over a period of time, this code "nudges" the stack by making a local-net
 * request that requires a response and thus generates traffic. If no traffic
 * is heard, then a more serious nudge is required which may force the data
 * link to be re-established (e.g. reconnect to the AP on WiFi.)
 *
 * Note that if this module is run early in LT boot, it establishes the default
 * transport for all other networking code. See LTNetCore.h for details on
 * how the default transport is used.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>

DEFINE_LTLOG_SECTION("net.mon");

#define METRICS_LOGGING_PERIOD 120 // seconds
#define DHCP_TIMEOUT_SEC       32  // seconds

// Module globals:
static struct Statics {
    LTCore      *iCore;
    LTNetCore   *iNetCore;
    ILTThread   *iThread;
    ILTEvent    *iEvent;
    LTEvent      StatusEvent;
    LTThread     MonitorThread;
    const char  *TranSpec;         // NetCore transport specification
    LTTransport  hTransport;       // NetCore transport handle
    s32          LastOpCount;      // IP traffic counter
    u32          NudgeCount;       // How many nudges we've done
    u32          ResetCount;       // How many link resets we've done
    u32          MetricsPeriod;    // How often to log metrics
    u16          NoTrafficLimit;   // How many no-traffic failures before nudge
    u16          NoTrafficReset;   // How many no-traffic failures before reset
    u16          NoTrafficHeard;   // Number of checks with no IP traffic
    bool         WatchOp;          // For debugging only
    bool         bNetIsUp;         // Net is connected (both link and IP)
    bool         bIsReady;
    LTTime       dhcpStartTimestamp;    // Timestamp when DHCP starts to discover
} S;

static const LTArgsDescriptor StatusEventArgs = {1, { kLTArgType_u32 }}; // status

static void DispatchStatusEvent(LTEvent hEvent, void *statusProc, LTArgs *args, void *clientData) { LT_UNUSED(hEvent);
    (*(LTNetMonitor_StatusProc *)statusProc)((LTNetMonitor_Status)LTArgs_u32At(0, args), clientData);
}

static void NotifyImmediateEventState(LTEvent hEvent, void *eventData, void *statusProc, void *clientData) {
    LT_UNUSED(hEvent);
    LT_UNUSED(eventData);
    LTNetMonitor_Status status = S.bNetIsUp ? kLTNetMonitor_Status_NetworkUp : kLTNetMonitor_Status_NetworkDown;
    (*(LTNetMonitor_StatusProc *)statusProc)(status, clientData);
}

static void LTNetMonitor_OnStatusChange(LTNetMonitor_StatusProc statusProc, LTThread_ClientDataReleaseProc *releaseProc, void *clientData) {
    S.iEvent->RegisterForEvent(S.StatusEvent, statusProc, releaseProc, clientData, true);
}

static void LTNetMonitor_NoStatusChange(LTNetMonitor_StatusProc statusProc) {
    S.iEvent->UnregisterFromEvent(S.StatusEvent, statusProc);
}

static void OnTransportEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    LT_UNUSED(clientData);
    char spec[80]; // A practical size to limit the length of log line below
    S.iNetCore->GetTransportSpec(hTransport, spec, sizeof(spec)); // size includes terminator
    switch (event) {
        case kLTTransport_Event_Up:
            if (S.bNetIsUp) break; // already up, don't repeat it
            S.bNetIsUp = true;
            S.LastOpCount = 0;
            S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_NetworkUp);
            S.dhcpStartTimestamp = LTTime_Zero();
            LTLOG("up", "net up: %s", spec);
            break;
        case kLTTransport_Event_Down:
            S.bNetIsUp = false;
            S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_NetworkDown);
            S.dhcpStartTimestamp = LTTime_Zero();
            LTLOG("down", "net down: %s", spec);
            break;
        case kLTTransport_Event_DhcpStart:
            S.dhcpStartTimestamp = S.iCore->GetKernelTime();
            LTLOG("dhcp.start", "DHCP start to discover");
            break;
        case kLTTransport_Event_DhcpRetry:
            LTLOG("dhcp.err", "DHCP retry, could not get IP");

            if (!LTTime_IsZero(S.dhcpStartTimestamp)) {
                LTTime now = S.iCore->GetKernelTime();
                // In some corner case, getting DHCP IP could take as long as 30sec
                if (LTTime_IsGreaterThanOrEqual(LTTime_Subtract(now, S.dhcpStartTimestamp), LTTime_Seconds(DHCP_TIMEOUT_SEC))) {
                    S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_NoIp);
                }
            }
            break;
        case kLTTransport_Event_DnsTimeout:
            LTLOG_REDALERT("dns.timeout.err", "DNS Timeout err");
            S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_DNSTimeout);
            break;
        case kLTTransport_Event_ResetMetrics:
            LTLOG("metrics.reset", "Reset Metrics");
            S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_ResetMetrics);
            break;
        case kLTTransport_Event_ShowMetrics:
            LTLOG("metrics.show", "Show DbgMetrics");
            S.iEvent->NotifyEvent(S.StatusEvent, kLTNetMonitor_Status_ShowMetrics);
            break;
        default:
            break;
    }
}

static void OnTimerProc(void* data) {
    LT_UNUSED(data);
    s32 opCount = S.iNetCore->IsOperating(S.hTransport, kLTTransport_Nudge_None);
    if (S.WatchOp) LTLOG("op", "monitor: %ld", LT_Ps32(opCount));
    // Is data-link not connected?
    if (opCount < 0) {
        if (S.LastOpCount >=0) LTLOG("no.link", "net not connected");
        S.LastOpCount = -1;
        return;
    }

    if (S.MetricsPeriod++ >= METRICS_LOGGING_PERIOD) {
        S.MetricsPeriod = 0;
        LTTransport_Metrics metrics;
        S.iNetCore->GetMetrics(S.hTransport, &metrics, sizeof(metrics));
        LTLOG_DEBUG("metrics", "cc:%lu lt:%lu lr:%lu ut:%lu ur:%lu dt:%lu dr: %lu\n",
            LT_Pu32(metrics.connections),  // See struct definition for field descriptions
            LT_Pu32(metrics.lowerBytesTx),
            LT_Pu32(metrics.lowerBytesRx),
            LT_Pu32(metrics.upperBytesTx),
            LT_Pu32(metrics.upperBytesRx),
            LT_Pu32(metrics.dropPacketsTx),
            LT_Pu32(metrics.dropPacketsRx)
        );
    }

    // Has transport driver heard any inbound traffic?
    if (opCount > S.LastOpCount) {
        S.LastOpCount = opCount;
        S.NoTrafficHeard = 0; // Reset only when traffic is heard
        return;
    }
    S.NoTrafficHeard++;
    // If no traffic, nudge link periodically to see if we receive traffic:
    if (S.NoTrafficHeard % S.NoTrafficLimit == 0) {
        S.NudgeCount++;
        LTLOG_DEBUG("nudge", "notraf: %d opval: %ld nudges: %ld", S.NoTrafficHeard, LT_Ps32(S.LastOpCount), LT_Ps32(S.NudgeCount));
        S.iNetCore->IsOperating(S.hTransport, kLTTransport_Nudge_Soft);
        S.LastOpCount = 0;
        return;
    }
    // If no traffic after multiple nudges, try something drastic: reset the link. (e.g. reconnect on WiFi)
    if (S.NoTrafficHeard % S.NoTrafficReset == 0) {
        S.ResetCount++;
        LTLOG_DEBUG("reset", "notraf: %d opval: %ld resets: %ld", S.NoTrafficHeard, LT_Ps32(S.LastOpCount), LT_Ps32(S.ResetCount));
        S.iNetCore->IsOperating(S.hTransport, kLTTransport_Nudge_Reset);
        S.LastOpCount = 0;
        return;
    }
}

static bool LTNetMonitor_IsUp(void) {
    return S.bNetIsUp;
}

static bool OnThreadStart(void) {
    const char *reason;
    do {
        reason = "LTNetCore";
        S.iNetCore = lt_openlibrary(LTNetCore);
        if (!S.iNetCore) break;

        reason = "OpenTransport";
        S.hTransport = S.iNetCore->OpenTransport(S.TranSpec, OnTransportEvent, NULL);
        if (!S.hTransport) break;

        S.iThread->SetTimer(S.iThread->GetCurrentThread(), LTTime_Milliseconds(1000), OnTimerProc, NULL, NULL);
        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "failed on: %s", reason);
    S.iThread->Terminate(S.MonitorThread);
    return false;
}

static void OnThreadExit(void) {
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimerProc, NULL);
    S.iEvent->Destroy(S.StatusEvent);     // null okay
    S.iCore->DestroyHandle(S.hTransport); // null okay
    lt_closelibrary(S.iNetCore);          // null okay
}

static bool LTNetMonitorImpl_LibInit(void) {
    S = (struct Statics) {
        .iCore          = LT_GetCore(),
        .iEvent         = lt_getlibraryinterface(ILTEvent,  LT_GetCore()),
        .iThread        = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .TranSpec       = NULL, // use default transport "IpWifi dhcp",
        .NoTrafficLimit = 10,
        .NoTrafficReset = 30,
      //.WatchOp        = true
    };

    // Event must be available to let user call OnStatusChange immediately after OpenLibrary
    S.StatusEvent = S.iCore->CreateEvent(&StatusEventArgs, DispatchStatusEvent, NULL, NotifyImmediateEventState, NULL);
    if (!S.StatusEvent) return false;

    S.MonitorThread = S.iCore->CreateThread("NetMonitor");
    if (!S.MonitorThread) return false;

    S.iThread->SetStackSize(S.MonitorThread, 1536);
    if (! S.iThread->StartSynchronous(S.MonitorThread, OnThreadStart, OnThreadExit)) {
        lt_destroyhandle(S.MonitorThread);
        lt_destroyhandle(S.StatusEvent);
        return false;
    }
    return true;
}

static void LTNetMonitorImpl_LibFini(void) {
    lt_destroyhandle(S.MonitorThread);
    lt_destroyhandle(S.StatusEvent);
}

static int LTNetMonitorImpl_Run(int argc, const char **argv) {
    if (argc > 2 && lt_strcmp(argv[2], "-w") == 0) S.WatchOp = true;
    lt_openlibrary(LTNetMonitor); // stay resident
    return 0;
}

define_LTLIBRARY_ROOT_INTERFACE(LTNetMonitor, LTNetMonitorImpl_Run, 1024)

    .IsUp           = LTNetMonitor_IsUp,
    .OnStatusChange = LTNetMonitor_OnStatusChange,
    .NoStatusChange = LTNetMonitor_NoStatusChange

LTLIBRARY_DEFINITION;
