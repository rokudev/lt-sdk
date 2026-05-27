/*******************************************************************************
 *
 * UnitTestLTDeviceWiFi
 * --------------------
 *
 * See LTDeviceWiFi.h for interface documentation.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/shell/LTSystemShell.h>

#define TEST_NAME "wifi.test"
DEFINE_LTLOG_SECTION(TEST_NAME);

/** Standard LT Interfaces ****************************************************/

static LTCore       *pCore;
static ILTThread    *iThread;
static LTDeviceWiFi *iWiFi;
static LTUtilityMacAddress *iMacAddress;

static const char *ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

#if 0 //Not used at this time
static LTWiFi_ApSecurity StringToApSecurity(const char *sec) {
    for (int n = 0; n < kLTWiFi_ApSecurity_Max; n++) {
        if (lt_strcmp(sec, ApSecurityStrings[n]) == 0) return n;
    }
    return kLTWiFi_ApSecurity_Unknown;
}
#endif

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static LTSystemShell *TestWiFi_Library;

/** Shell Variables ***********************************************************/

static ILTShell      *TestWiFi_iShell;

static LTWiFi_ApInfo ApSpec;
static char          ApPass[kLTWiFi_Max_Pass+2];
static LTWiFi_ApInfo ApSaved;
static char          ApSavedPass[kLTWiFi_Max_Pass+2];
static LTThread      ShellStatus_Thread;

/** Forward references ********************************************************/

static int TestWiFi_GetApInfo(LTShell hShell, int argc, const char *argv[]);
static int TestWiFi_ListCommands(LTShell hShell, int argc, const char *argv[]);

/** Utility Functions *********************************************************/

#define CLEAR(v) lt_memset(v, 0, sizeof(*v))

static bool ValidStr(char const *str, int max_len, char *error_msg) {
    int length = lt_strlen(str);
    if (length > 0 || length <= max_len) return true;
    LTLOG("str.bad", "%s", error_msg);
    return false;
}

#if 0
static void PrintApInfo(const char *prefix, LTWiFi_ApInfo *ap) {
    LTLOG_DEBUG("ap.info", "%s AP ssid: %s pass: %s sec: %s", prefix, ap->ssid, ap->pass, ApSecurityStrings[ap->security]);
}
#endif

static void WiFiPrint(LTShell hShell, const char *fmt, ...) {
    lt_va_list args;
    lt_va_start(args, fmt);
    TestWiFi_iShell->Print(hShell, "WiFi: ");
    TestWiFi_iShell->VPrint(hShell, fmt, args);
    lt_va_end(args);
}

static void FailTest(LTShell hShell, const char *fmt, ...) {
    lt_va_list args;
    lt_va_start(args, fmt);
    TestWiFi_iShell->Print(hShell, "FAIL: ");
    TestWiFi_iShell->VPrint(hShell, fmt, args);
    lt_va_end(args);
}

/** Argument Parsing **********************************************************/

typedef struct TestWiFi_Args {
    char ssid[kLTWiFi_Max_Ssid+2];
    char pass[kLTWiFi_Max_Pass+2];
    u8   channels[20];
} TestWiFi_Args;

// All commands can use the same flags. Returns false on error. Returns true if any arg was provided.
static bool TestWiFi_ParseArgs(LTShell hShell, int argc, const char *argv[], TestWiFi_Args *args) {
    LT_UNUSED(hShell);
    CLEAR(args);
    bool result = false;
    char *ssid = NULL;
    char *pass = NULL;
    for (int n = 0; n < argc; n++) {
        char const *arg = argv[n];
        if (arg[0] == '-') {
            switch (arg[1]) {
                case 's':
                    n++;
                    if (n < argc) ssid = (char*)argv[n];
                    break;
                case 'p':
                    n++;
                    if (n < argc) pass = (char*)argv[n];
                    break;
                case 'c':
                    n++;
                    if (n < argc) {
                        args->channels[0] = lt_strtou32(argv[n], 0, 10);
                        args->channels[1] = 1; // maybe allow more later
                    }
                    break;
                //case 'w': // pending: wait
                //case 't': // pending: timeout
                default:
                    LTLOG("arg.bad", "unknown test option: %s", arg);
                    return false;
            }
        }
    }

    // Validate and use program command line arguments if provided:
    if (ssid) {
        if (!ValidStr(ssid, kLTWiFi_Max_Ssid, "bad SSID length")) return false;
        lt_strncpyTerm(args->ssid, ssid, kLTWiFi_Max_Ssid + 1);
        result = true;
    }
    if (pass) {
        if (!ValidStr(pass, kLTWiFi_Max_Pass, "bad password length")) return false;
        if (lt_strlen(pass) < 8) {
            LTLOG("pass.short", "password too short");
            return -1;
        }
        lt_strncpyTerm(args->pass, pass, kLTWiFi_Max_Pass);
        result = true;
    }
    return result;
}

/** Callback Functions ********************************************************/

/* Strings below must match LTDeviceWiFi.h kLTDeviceWiFi_Status */
static const char *TestWiFi_StatusStrings[kLTDeviceWiFi_Status_Max] = {
    "unknown",
    "reset",
    "down",
    "start",
    "up",
    "scan-start",
    "scan-result",
    "scan-done",
    "join-start",
    "join-assoc",
    "join-auth",
    "join-done",
    "join-failed",
    "connected",
    "disconnected",
    "rejoining",
    "mesh-up",
    "mesh-down"
};

static LTDeviceWiFi_Status TestWiFi_FindStatus(const char *str) {
    for (u16 n = 0; n < kLTDeviceWiFi_Status_Max; n++) {
        if (lt_strcmp(TestWiFi_StatusStrings[n], str) == 0) return n;
    }
    return 0;
}

static LTDeviceWiFi_Status LastStatusEvent;

static void /*LTDeviceWiFi_StatusCallback*/ TestWiFi_PrintWiFiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit); LT_UNUSED(clientData);
    LastStatusEvent = status;
    const char *msg = NULL;
    if (status <= kLTDeviceWiFi_Status_Rejoining) msg = TestWiFi_StatusStrings[status];
    else msg = "bad-status";

    if (status == kLTDeviceWiFi_Status_Up && !ApSaved.ssid[0] && iWiFi->LoadApSettings(&ApSaved)) {
        LTLOG("ap.load", "AP settings loaded for: %s", ApSaved.ssid);
    }

    LTLOG("status", "status: %s\n", msg);
}

static void /*LTDeviceWiFi_ScanCallback*/ TestWiFi_ScanApsCallback(LTWiFi_ApInfo *ap, void *callback_data) {
    LTShell hShell = (LTShell) callback_data;
    if (!ap) return;
    char bssid[20];
    iMacAddress->MacAddressToString(&ap->bssid, bssid, ':');
    TestWiFi_iShell->Print(hShell, "  %s chan: %3u rssi: %4d sec: %-6s ssid: \"%s\"\n",
        bssid, ap->channel, ap->rssi, ApSecurityStrings[ap->security], ap->ssid);
}

static void /*LTDeviceWiFi_JoinCallback*/ TestWiFi_JoinApCallback(LTWiFi_JoinStatus status, void *callback_data) {
    LT_UNUSED(callback_data);
    //LTShell hShell = (LTShell) callback_data;
    char *str;
    switch (status) {
        case kLTWiFi_JoinStatus_Success:    str = "connected to AP (done)";   break;
        case kLTWiFi_JoinStatus_Associated: str = "associated to AP";         break;
        case kLTWiFi_JoinStatus_Failed:     str = "failed to connect (done)"; break;
        case kLTWiFi_JoinStatus_Aborted:    str = "action terminated (done)"; break;
        default: str = "in progress...";
    }
    LTLOG("join", "%s", str);
}

/** Command Functions *********************************************************/

static int TestWiFi_GetDeviceInfo(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTWiFi_DriverInfo info;
    if (!iWiFi->GetDeviceInfo(&info)) {
        FailTest(hShell, "cannot get WiFi info\n");
    } else {
        TestWiFi_iShell->Print(hShell, "  Vendor:  %s\n", info.vendor);
        TestWiFi_iShell->Print(hShell, "  Product: %s\n", info.product);
        TestWiFi_iShell->Print(hShell, "  Version: %s\n", info.version);
        TestWiFi_iShell->Print(hShell, "  Updated: %s\n", info.updated);
        char str[20];
        iMacAddress->MacAddressToString(&info.mac_address, str, ':');
        TestWiFi_iShell->Print(hShell, "  MACAddr: %s\n", str);
    }
    return 0;
}

static LTWiFi_ScanSpec ScanSpec;
TestWiFi_Args ScanArgs;

static int TestWiFi_ScanAps(LTShell hShell, int argc, const char *argv[]) {
    CLEAR(&ScanSpec);
    if (TestWiFi_ParseArgs(hShell, argc, argv, &ScanArgs)) {
        ScanSpec.ssid = ScanArgs.ssid; // over lifetime of call
        ScanSpec.channel = ScanArgs.channels; // over lifetime of call
    }
    LastStatusEvent = kLTDeviceWiFi_Status_Unknown;
    iWiFi->ScanAps(&ScanSpec, TestWiFi_ScanApsCallback, (void*)hShell);
    return 0;
}

static int TestWiFi_JoinAp(LTShell hShell, int argc, const char *argv[]) {
    // Note: typing "join" without args will use prior ApSpec
    TestWiFi_Args args;
    if (TestWiFi_ParseArgs(hShell, argc, argv, &args)) {
        lt_strncpyTerm(ApSpec.ssid, args.ssid, kLTWiFi_Max_Ssid + 1);
        ApSpec.pass = ApPass;
        lt_strncpyTerm(ApSpec.pass, args.pass, kLTWiFi_Max_Pass);
        ApSpec.channel = args.channels[0];
    }
    if (!ApSpec.ssid[0]) {
        ApSpec = ApSaved; // use stored value, note pass pointer gets set
    }
    if (!ApSpec.ssid[0]) {
        WiFiPrint(hShell, "AP SSID was not specified\n");
        return -1;
    }
    LastStatusEvent = kLTDeviceWiFi_Status_Unknown;
    WiFiPrint(hShell, "joining access point \"%s\"... \n", ApSpec.ssid);
    ApSpec.security = kLTWiFi_ApSecurity_Wpa2;
    if (!ApSpec.pass[0]) {
        WiFiPrint(hShell, "no password specified, open AP assumed\n");
        ApSpec.security = kLTWiFi_ApSecurity_Open;
    }
    iWiFi->JoinAp(&ApSpec, TestWiFi_JoinApCallback, (void*)hShell);
    return 0;
}

static int TestWiFi_IsConnected(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    WiFiPrint(hShell, "Connected: %s\n", iWiFi->IsConnected() ? "true" : "false");
    return 0;
}

static int TestWiFi_Disconnect(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    WiFiPrint(hShell, "Disconnect\n");
    iWiFi->Disconnect();
    return 0;
}

static int TestWiFi_GetApInfo(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTWiFi_ApInfo ap_info;
    CLEAR(&ap_info);
    if (iWiFi->GetApInfo(&ap_info)) {
        char bssid[20];
        iMacAddress->MacAddressToString(&ap_info.bssid, bssid, ':');
        TestWiFi_iShell->Print(hShell, "  SSID:     %s\n", ap_info.ssid);
        TestWiFi_iShell->Print(hShell, "  BSSID:    %s\n", bssid);
        TestWiFi_iShell->Print(hShell, "  Channel:  %u\n", ap_info.channel);
        TestWiFi_iShell->Print(hShell, "  Security: %s\n", ApSecurityStrings[ap_info.security]); // guaranteed in-bounds
        TestWiFi_iShell->Print(hShell, "  RSSI:     %d dBm\n", ap_info.rssi);
        TestWiFi_iShell->Print(hShell, "  SNR:      %d dB\n", ap_info.snr);
    } else {
        WiFiPrint(hShell, "not connected\n");
    }
    return 0;
}

static int TestWiFi_GetMacAddress(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTMacAddress mac;
    iWiFi->GetMacAddress(&mac); // ignore return
    char str[20];
    iMacAddress->MacAddressToString(&mac, str, ':');
    WiFiPrint(hShell, "MACAddr: %s\n", str);
    return 0;
}

#if 0
static int TestWiFi_SetMacAddress(LTShell hShell, int argc, const char *argv[]) {
    LTMacAddress mac;
    if (argc < 2 || !iMacAddress->StringToMacAddress(argv[1], &mac)) {
        WiFiPrint(hShell, "provide a MAC address in the form 11:22:33:44:55\n");
        return 0;
    }
    if (iWiFi->SetMacAddress(&mac)) WiFiPrint(hShell, "MAC address set to: %s\n", argv[1]);
    else {
        WiFiPrint(hShell, "could not set MAC address\n");
        return -1;
    }
    return 0;
}
#endif

static int TestWiFi_Reset(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    iWiFi->Reset();
    WiFiPrint(hShell, "driver reset...\n");
    return 0;
}

/** Default Test Sequence *****************************************************/

static char DefaultTests[] =
    "GetDeviceInfo\n"
    "ScanAps\n"
    "Wait scan-done\n"
    "ScanAps -c 6\n"
    "Wait scan-done\n"
    "ScanAps -s netflix\n"
    "Wait scan-done\n"
    "JoinAp\n"
    "Wait connected\n" // change to join-done when true event wait is implemented !!!
    "GetApInfo\n"
    "IsConnected\n"
    "Disconnect\n"
    "JoinAp -s unknown-AP\n"  // should fail
    "Wait disconnected\n" // change to join-done when true event wait is implemented !!!
;

static char *DefaultArgs[10];

static int TestWiFi_DoCommand(LTShell hShell, int argc, const char *argv[]);

static int DoDefaultTests(LTShell hShell, int ignore, const char *argv[]) {
    LT_UNUSED(hShell);
    LT_UNUSED(ignore);
    LT_UNUSED(argv);
    int argc = 1;
    DefaultArgs[argc] = DefaultTests;
    for (int n = 0; DefaultTests[n]; n++) {
//        LT_GetCore()->ConsolePrint("---- %2d %d %3u\n", n, argc, DefaultTests[n]);
//        iThread->Sleep(LTTime_Milliseconds(200));
        if (DefaultTests[n] == ' ') {
            DefaultTests[n] = 0;
            DefaultArgs[++argc] = DefaultTests+n+1;
            continue;
        }
        if (DefaultTests[n] == '\n') {
            DefaultTests[n] = 0;
            argc++;
            LT_GetCore()->ConsolePrint("DO %s (%d args)\n", DefaultArgs[1], argc);
            iThread->Sleep(LTTime_Milliseconds(300));
            TestWiFi_DoCommand(hShell, argc, (const char **)DefaultArgs);
            argc = 1;
            DefaultArgs[argc] = DefaultTests+n+1;
            continue;
        }
    }
    LT_GetCore()->ConsolePrint("DONE\n");
    iThread->Sleep(LTTime_Milliseconds(200));
    return 0;
}

// !!! NOTE: This needs to be replaced. Rather than sleep polling, this should use
// LT event callbacks not not use sleep.
static int TestWiFi_WaitStatus(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell);
    LTDeviceWiFi_Status status_event = 0;
    if (argc > 1) status_event = TestWiFi_FindStatus(argv[1]);
    TestWiFi_iShell->Print(hShell, "Wait for %s ", TestWiFi_StatusStrings[status_event]);
    for (u32 timeout = 0; timeout < 20 && LastStatusEvent != status_event; timeout++) {
        TestWiFi_iShell->Print(hShell, ".");
        iThread->Sleep(LTTime_Milliseconds(500));
    }
    TestWiFi_iShell->Print(hShell, "\n");
    return LastStatusEvent == status_event ? -1 : 0;
}

static const LTSystemShell_CommandDesc TestWiFi_Commands[] = {
    { "help",          TestWiFi_ListCommands,  "",                                                       NULL,   NULL },
    { "GetDeviceInfo", TestWiFi_GetDeviceInfo, "",                                                       NULL,   NULL },
    { "ScanAps",       TestWiFi_ScanAps,       "[-s <ssid>] [-c <channel>]",                             NULL,   NULL },
    { "JoinAp",        TestWiFi_JoinAp,        "-s <ssid> [-p <password>] [-c <channel>] [-b <bssid>]",  NULL,   NULL },
    { "IsConnected",   TestWiFi_IsConnected,   "",                                                       NULL,   NULL },
    { "Disconnect",    TestWiFi_Disconnect,    "",                                                       NULL,   NULL },
    { "GetApInfo",     TestWiFi_GetApInfo,     "",                                                       NULL,   NULL },
    { "GetMacAddress", TestWiFi_GetMacAddress, "",                                                       NULL,   NULL },
    //{ "SetMacAddress", TestWiFi_SetMacAddress, "<mac-address>",  NULL,   NULL }, // Not advisable due to limited spare EFuse
    { "Reset",         TestWiFi_Reset,         "",                                                       NULL,   NULL },
    { "Wait",          TestWiFi_WaitStatus,    "",                                                       NULL,   NULL },
    { "All",           DoDefaultTests,         "",                                                       NULL,   NULL },
    { "all",           DoDefaultTests,         "",                                                       NULL,   NULL },
    { NULL,            NULL,                   NULL,                                                     NULL,   NULL }
};

static int TestWiFi_ListCommands(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    TestWiFi_iShell->Print(hShell, "usage: test-wifi <command> [args]\nCommands:\n");
    for (int n = 0; TestWiFi_Commands[n].pCommand; n++) {
        TestWiFi_iShell->Print(hShell, " %s %s\n", TestWiFi_Commands[n].pCommand,
            TestWiFi_Commands[n].pDescription);
    }
    return 0;
}

static int TestWiFi_DoCommand(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; TestWiFi_Commands[n].pCommand; n++) {
            if (lt_strcmp(TestWiFi_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return TestWiFi_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void TestWiFi_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help wifi" works in addition to "wifi help".
       It prints usage and calls the regular TestWiFi_Help */
    (void)TestWiFi_ListCommands(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc TestWiFi_Command[] = {
    {"test-wifi", TestWiFi_DoCommand, "WiFi tests, type \"test-wifi\" for a list", TestWiFi_Help, NULL },
};

/** Test Init and Quit ********************************************************/

static bool ShellStatus_InitThread(void) {
    iWiFi->OnStatusChange(TestWiFi_PrintWiFiStatus, NULL, NULL);
    return true;
}

static void ShellStatus_ExitThread(void) {
    iWiFi->NoStatusChange(TestWiFi_PrintWiFiStatus);
}

static void TestWiFi_Quit(void) {
    if (TestWiFi_Library) {
        TestWiFi_Library->UnregisterCommands(TestWiFi_Command);
        iThread->Destroy(ShellStatus_Thread); // zero ok
        pCore->CloseLibrary((LTLibrary *)TestWiFi_Library);
        TestWiFi_Library = NULL;
    }
}

static bool TestWiFi_Init(void) {
    TestWiFi_Library = lt_openlibrary(LTSystemShell);
    if (!TestWiFi_Library) return false;

    ShellStatus_Thread = pCore->CreateThread("PrintWiFiStatus");
    if (!ShellStatus_Thread) return false;
    iThread->Start(ShellStatus_Thread, ShellStatus_InitThread, ShellStatus_ExitThread);

    TestWiFi_iShell = lt_getlibraryinterface(ILTShell, TestWiFi_Library);
    TestWiFi_Library->RegisterCommands(TestWiFi_Command, 1);
    CLEAR(&ApSpec);
    CLEAR(ApPass);
    ApSpec.pass = ApPass;
    LastStatusEvent = kLTDeviceWiFi_Status_Unknown;
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void UnitTestLTDeviceWiFiImpl_LibFini(void) {
    TestWiFi_Quit();
    lt_closelibrary(iWiFi);       // null ok
    lt_closelibrary(iMacAddress); // null ok
}

static bool UnitTestLTDeviceWiFiImpl_LibInit(void) {
    pCore = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iWiFi   = lt_openlibrary(LTDeviceWiFi);
    iMacAddress = lt_openlibrary(LTUtilityMacAddress);
    if (!(iWiFi && iMacAddress && TestWiFi_Init())) {
        UnitTestLTDeviceWiFiImpl_LibFini();
        return false;
    }
    // If AP was saved in settings, allow it to be used by default.
    ApSaved = (LTWiFi_ApInfo){};
    ApSaved.pass = ApSavedPass; // storage for password
    ApSaved.pass[0] = 0;
    return true;
}

static int UnitTestLTDeviceWiFiImpl_Run(int argc, const char ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTLOG("run", "Running preset WiFi test sequence. Use ltopen for separate commands.");
    return 0;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFi, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceWiFi, UnitTestLTDeviceWiFiImpl_Run, 4096) LTLIBRARY_DEFINITION;
