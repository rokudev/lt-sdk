/*******************************************************************************
 *
 * LTShellNet: Network Shell
 * -------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/echo/LTNetEcho.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/net/iperf3/LTNetIperf3.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

DEFINE_LTLOG_SECTION("shell.net");

#define PS(x...) lt_gethandleinterface(ILTShell, hShell)->Print(hShell, x)

/*******************************************************************************
 * Static Variables
 ******************************************************************************/

static struct Statics {
    LTCore        *core;
    LTSystemShell *shell;
    LTNetCore     *netCore;
    LTNetMonitor  *netMonitor;
    LTNetEcho     *netEcho;
    LTNetIperf3   *netIperf;
    LTLibrary     *selfLib;
} S;

/** Forward Declarations ******************************************************/

static int LTShellNet_Help(LTShell hShell, int argc, const char *argv[]);

/** Utility Functions *********************************************************/

static u16 HasArg(int argc, const char *argv[], const char *pattern) {
    // argv[0] is command and is skipped
    for (u16 n = 1; n < argc; n++) {
        if (lt_strcmp(argv[n], pattern) == 0) return n;
    }
    return 0;
}

/** Command Functions *********************************************************/

static int LTShellNet_AutoBoot(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc); LT_UNUSED(argv);
    // Note: This will clobber any other shell boot cmd
    LTSystemSettings *lib = lt_openlibrary(LTSystemSettings);
    if (!lib) return -1;
    lib->SetStringValue("ltshell.boot.cmd", "ltopen LTShellNet");
    lt_closelibrary(lib);
    PS("net autoboot enabled\n");
    return 0;
}

typedef struct {
    LTShell hShell;
    char    hostName[1];
} DnsLookup;

static void OnDnsEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    DnsLookup  *pDnsLookup = clientData;
    LTShell     hShell = pDnsLookup->hShell;
    const char *hostName = pDnsLookup->hostName;

    switch (event) {
        case kLTSocket_Event_DnsResolved: {
            ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hSocket);
            u32 serverAddr;
            if (iSocket->GetProperty(hSocket, "dns.addr.v4", &serverAddr)) {
                PS("DNS: %s -> %lu.%lu.%lu.%lu\n", hostName,
                   LT_Pu32((serverAddr >> 0) & 0xFF),
                   LT_Pu32((serverAddr >> 8) & 0xFF),
                   LT_Pu32((serverAddr >> 16) & 0xFF),
                   LT_Pu32((serverAddr >> 24) & 0xFF));
             } else {
                 PS("Cannot get IPv4 address property for hostName '%s'\n",
                    hostName);
            }
            break;
        }

	case kLTSocket_Event_DnsError:
            PS("DNS error when resolving hostname '%s'\n", hostName);
            break;

        case kLTSocket_Event_DnsTimeout:
            PS("DNS timeout while resolving hostname '%s'\n", hostName);
            break;

        default:
            PS("Unknown socket error when resolving hostname '%s'\n", hostName);
   }

   lt_destroyhandle(hSocket);
   lt_free(pDnsLookup);
}

static int LTShellNet_Host(LTShell hShell, int argc, const char *argv[]) {
    const LT_SIZE sockSpecMax = 256;
    char *sockSpec = lt_malloc(sockSpecMax);
    if (!sockSpec) {
        PS("Cannot allocate socket specification buffer\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        const char    *hostName = argv[i];
        const LT_SIZE  hostNameLen = lt_strlen(hostName);
        DnsLookup     *pDnsLookup = lt_malloc(sizeof(DnsLookup) + hostNameLen);
        if (!pDnsLookup) {
            PS("Cannot allocate memory for resolving hostName '%s'\n", hostName);
            ret++;
            break;
        }
        pDnsLookup->hShell = hShell;
        lt_memcpy(pDnsLookup->hostName, hostName, hostNameLen + 1);

        lt_snprintf(sockSpec, sockSpecMax, "dns host: %s", hostName);
        LTSocket hSocket = S.netCore->OpenSocket(0, sockSpec, OnDnsEvent,
                                                 pDnsLookup);
        if (hSocket == LTHANDLE_INVALID) {
            lt_free(pDnsLookup);
            PS("Cannot create DNS socket for hostName '%s'\n", hostName);
            ret++;
            break;
        }
    }

    lt_free(sockSpec);
    return ret;
}

static int LTShellNet_Status(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc); LT_UNUSED(argv);
    char buff[80] = {};
    S.netCore->GetTransportSpec(0, buff, sizeof(buff));
    PS("Config: %s\n", buff);

    PS("Status: %s\n", S.netMonitor->IsUp() ? "up" : "down");

    LTTransport_Metrics metrics = {};
    S.netCore->GetMetrics(0, &metrics, sizeof(metrics));
    PS("Data-link L2 bytes:\n");
    PS("  RX: %lu\n", LT_Pu32(metrics.lowerBytesRx));
    PS("  TX: %lu\n", LT_Pu32(metrics.lowerBytesTx));
    PS("Transport L3 bytes:\n");
    PS("  RX: %lu\n", LT_Pu32(metrics.upperBytesRx));
    PS("  TX: %lu\n", LT_Pu32(metrics.upperBytesTx));
    PS("Dropped packets:\n");
    PS("  RX: %lu\n", LT_Pu32(metrics.dropPacketsRx));
    PS("  TX: %lu\n", LT_Pu32(metrics.dropPacketsTx));
    return 0;
}

static void OnPingEvent(LTHandle hPing, u16 id, u16 count, void *data) { LT_UNUSED(hPing);
    LTShell hShell = VOIDPTR_TO_LTHANDLE(data);
    static LTTime TimeSent;
    if (!id) {
        if (count == 1) PS("\n"); // avoid collision with shell prompt
        PS("sent ping %2u\n", count);
        TimeSent = S.core->GetKernelTime();
    } else {
        LTTime delta = LTTime_Subtract(S.core->GetKernelTime(), TimeSent);
        u32 msec = (u32)LTTime_GetMicroseconds(delta);
        if (id) PS("got  ping %2u in %ld.%03ld msec\n", count, LT_Pu32(msec/1000), LT_Pu32(msec%1000));
    }
}

static int LTShellNet_Ping(LTShell hShell, int argc, const char *argv[]) {
    if (!S.netEcho) S.netEcho = lt_openlibrary(LTNetEcho);
    if (!S.netEcho) {
        PS("Error: LTNetEcho did not open\n");
        return -1;
    }
    const char *dest = NULL; // if no IP dest, then ping gateway
    if (argc > 1) dest = argv[1];
    u16 count = 3;
    u16 n = HasArg(argc, argv, "-c");
    if (n == 1) dest = NULL;
    if (n && argv[n+1]) {
        count = lt_strtou32(argv[n+1], NULL, 10);
    }
    u16 msec = 500;
    n = HasArg(argc, argv, "-i");
    if (n == 1) dest = NULL;
    if (n && argv[n+1]) {
        msec = lt_strtou32(argv[n+1], NULL, 10);
    }
    if (dest) PS("Ping %s\n", dest);
    S.netEcho->Ping(0, dest, 1234, count, msec, NULL, 0, OnPingEvent, LTHANDLE_TO_VOIDPTR(hShell));
    return 0;
}

static int LTShellNet_WD(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(hShell); LT_UNUSED(argc); LT_UNUSED(argv);
    LTDeviceWiFi  *wifi = lt_openlibrary(LTDeviceWiFi);
    if (!wifi) return -1;
    // This method of disconnect is for testing purposes only. It is done here to provide
    // a way to test automatic reconnect (because this method bypasses the WiFi state machine.)
    wifi->SetOption("disconnect", 0);
    lt_closelibrary(wifi);
    return 0;
}

static int LTShellNet_Iperf(LTShell hShell, int argc, const char *argv[]) {
    if (!S.netIperf) S.netIperf = lt_openlibrary(LTNetIperf3);
    if (!S.netIperf) {
        PS("Error: LTNetIperf3 did not open\n");
        return -1;
    }

    bool server = false;
    bool client = false;
    const char *ipaddr = NULL;
    LTNetIperf3_Parameters params = {0};
    u16 n;

    if (HasArg(argc, argv, "-s")) {
        server = true;
    }
    if (HasArg(argc, argv, "-u")) {
        params.udp = true;
    }
    if ((n = HasArg(argc, argv, "-c")) > 0 && argv[n+1]) {
        client = true;
        ipaddr = argv[n+1];
    }
    if ((n = HasArg(argc, argv, "-p")) > 0 && argv[n+1]) {
        params.port = lt_strtou32(argv[n+1], NULL, 10);
    }
    if ((n = HasArg(argc, argv, "-b")) > 0 && argv[n+1]) {
        params.bitrate = lt_strtou32(argv[n+1], NULL, 10);
        if (params.bitrate == 0) {
            params.bitrate = ~0; // 0 ok, unlimited bitrate
        }
    }
    if ((n = HasArg(argc, argv, "-t")) > 0 && argv[n+1]) {
        params.time = lt_strtou32(argv[n+1], NULL, 10);
        PS("time = %d\n", params.time);
    }
    if ((n = HasArg(argc, argv, "-d")) > 0 && argv[n+1]) {
        params.dscp = lt_strtou32(argv[n+1], NULL, 10);
        PS("dscp = %d\n", params.dscp);
    }
    if ((n = HasArg(argc, argv, "-P")) > 0 && argv[n+1]) {
        params.prio = lt_strtou32(argv[n+1], NULL, 10);
        PS("priority = %d\n", params.prio);
    }
    if ((n = HasArg(argc, argv, "-m")) > 0 && argv[n+1]) {
        params.mss = lt_strtou32(argv[n+1], NULL, 10);
        if (params.mss > 1460) {
            params.mss = 1460;
        }
        PS("MSS = %d\n", params.mss);
    }
    params.streams = 1;
    if ((n = HasArg(argc, argv, "-S")) > 0 && argv[n+1]) {
        params.streams = lt_strtou32(argv[n+1], NULL, 10);
        if (params.streams > 8) {
            params.streams = 8;
        }
        PS("Stream = %d\n", params.streams);
    }

    if (server) {
        PS("Running iPerf3 server, port %ld\n",
           LT_Pu32(params.port ? params.port : kLTNetIperf3_Default_Port));
        S.netIperf->RunServer(&params, NULL, NULL);
    } else if (client) {
        PS("Running iPerf3 %s client, port %ld\n",
           params.udp ? "udp" : "tcp",
           LT_Pu32(params.port ? params.port : kLTNetIperf3_Default_Port));
        S.netIperf->RunClient(ipaddr, &params, NULL, NULL);
    } else {
        PS("Usage: iperf3 {-c <ipaddr> | -s} [-u | -p port | -t time | -b bitrate]\n");
        PS("When invoked with '-c', provide server IP address.\n");
        PS("-u          : Use UDP rather than TCP.\n");
        PS("-p port     : Default port is 5201.\n");
        PS("-t time     : Default time is 10 seconds.\n");
        PS("-b bitrate  : Default bitrate is unlimited for TCP and 1Mbit/sec for UDP.\n");
        PS("              Use '-b 0' for unlimited bitrate (useful for UDP).\n");
        PS("-d dscp     : default dscp is 0.\n");
        PS("-P priority : default thread priority is 0.\n");
        PS("-m mss      : default MSS is 1460. does not allow MSS is bigger than 1460\n");
        PS("-S streams  : default stream is 1, up to 8\n");
    }
    return 0;
}
static int LTShellNet_LwipStat(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell); LT_UNUSED(argc); LT_UNUSED(argv);
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!deviceConfig) {
        return 0;
    }
    const char *defaultName = deviceConfig->GetDefaultNetTransport();
    lt_closelibrary(deviceConfig);
    LTLibrary *trans_lib = S.core->OpenLibrary(defaultName);
    if (!trans_lib) {
        return 0;
    }
    LTNetDriver *drv = lt_getlibraryinterface(LTNetDriver, trans_lib);
    if (drv && drv->ShowLwipStat) {
        drv->ShowLwipStat(NULL, false);
    }
    lt_closelibrary(trans_lib);
    return 0;
}
static const LTSystemShell_CommandDesc NetCommands[] = {
    { "help",       LTShellNet_Help,     "list of net commands",               NULL },
    { "autoboot",   LTShellNet_AutoBoot, "enable LTShellNet autoboot",         NULL },
    { "host",       LTShellNet_Host,     "host hostname [hostname ...]",       NULL },
    { "status",     LTShellNet_Status,   "print network details and metrics",  NULL },
    { "metrics",    LTShellNet_Status,   "print network details and metrics",  NULL },
    { "ping",       LTShellNet_Ping,     "ping [<ip>] [-c <count>] [-i msec]", NULL },
    { "wd",         LTShellNet_WD,       "WiFi disconnect for testing only",   NULL },
    { "iperf3",     LTShellNet_Iperf,    "iperf3 {-s|-c host} [options]",      NULL },
    { "lwipstat",   LTShellNet_LwipStat, "shows lwip stats",                   NULL },
    { }
};

static int LTShellNet_Help(LTShell hShell, int argc, const char *argv[]) { LT_UNUSED(argc); LT_UNUSED(argv);
    for (int n = 0; NetCommands[n].pCommand; n++) {
        PS("  %-10s - %s\n", NetCommands[n].pCommand, NetCommands[n].pDescription);
    }
    return 0;
}


static int LTShellNet_Net(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; NetCommands[n].pCommand; n++) {
            if (0 == lt_strcmp(NetCommands[n].pCommand, argv[1])) {
                cmd = n;
                break;
            }
        }
    }
    return NetCommands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help net" works in addition to "net help".
       It prints usage and calls the regular LTShellNet_Help */
    PS("usage: net <command> [args]\nCommands:\n");
    (void)LTShellNet_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellNet_Commands[] = {
    { "net", LTShellNet_Net, "net commands, type \"net\" for a list", LTShell_Help },
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellNetImpl_LibFini(void) {
    lt_closelibrary(S.netMonitor); // null okay
    lt_closelibrary(S.netEcho);    // null okay
    lt_closelibrary(S.netCore);    // null okay
    if (S.shell) {
        S.shell->UnregisterCommands(LTShellNet_Commands);
        lt_closelibrary(S.shell);
    }
    S = (struct Statics) {};
}

static bool LTShellNetImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore()
    };
    const char *reason;
    do {
        reason = "no shell";
        if (!(S.shell = lt_openlibrary(LTSystemShell))) break;
        S.shell->RegisterCommands(LTShellNet_Commands, sizeof(LTShellNet_Commands) / sizeof(LTShellNet_Commands[0]));

        reason = "no LTNetCore";
        if (!(S.netCore = lt_openlibrary(LTNetCore))) break;

        reason = "no LTNetMonitor";
        if (!(S.netMonitor = lt_openlibrary(LTNetMonitor))) break;

        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTShellNetImpl_LibFini();
    return false;
}

static int LTShellNet_Run(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.selfLib) S.selfLib = S.core->OpenLibrary("LTShellNet"); // open a second time so I stay resident when main exits
    return 0;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellNet, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellNet, LTShellNet_Run, 0) LTLIBRARY_DEFINITION;
