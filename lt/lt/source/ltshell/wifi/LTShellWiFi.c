/*******************************************************************************
 *
 * LTShellWiFi: WiFi Shell
 * -----------------------
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
#include <lt/device/watchdog/LTDeviceWatchdog.h> // for reboot

DEFINE_LTLOG_SECTION("ltshell.wifi");

/** Standard LT Interfaces ****************************************************/

static LTCore     *pCore;
static ILTThread  *iThread;
static LTDeviceWiFi *iWiFi;
static LTUtilityMacAddress *iMacAddress;
static LTLibrary *SelfLib;

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
enum LTShellWiFiErrorCode {
    LTShellWiFi_OK = 0,
    LTShellWiFi_PASSWD_INVALID  = -1,
    LTShellWiFi_ARG_ERR         = -2,
    LTShellWiFi_SOFTAP_FAIL     = -3,
    LTShellWiFi_IWPRIV_FAIL     = -4,
};

static LTSystemShell *SHL_Library;

/** Shell Variables ***********************************************************/

static ILTShell      *SHL_iShell;
static bool          SHL_JoinSave;
static bool          SHL_SniffMode;
static int           SHL_DebugMode;
static int           SHL_ScanRepeat;
static bool          SHL_ScanProbes;
static LTList        SHL_ScanApList;
static LTWiFi_ApInfo SHL_ApSpec;
static char          SHL_ApPass[kLTWiFi_Max_Pass+2];
static LTWiFi_ApInfo SHL_SoftApSpec;
static char          SHL_SoftApPass[kLTWiFi_Max_Pass+2];
static LTThread      SHL_Thread;
//static LTShell       SHL_Shell; // a cheat

/** Forward references ********************************************************/

static void SET_AutoJoin(bool state);
static bool SET_AutoJoinIsSet(void);
static void SET_Rejoin(bool state);
static bool SET_RejoinIsSet(void);
static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);
static int  SHL_Status(LTShell hShell, int argc, const char *argv[]);

/** Utility Functions *********************************************************/

#define CLEAR(v) lt_memset(&(v), 0, sizeof(v))

static int HasArg(int argc, const char *argv[], const char *pattern) {
    // argv[0] is command and is skipped
    for (int n = 1; n < argc; n++) {
        if (lt_strcmp(argv[n], pattern) == 0) return n;
    }
    return LTShellWiFi_OK;
}

enum Choices {  // common command choices
    kChoiceOff   = -1,
    kChoiceNone  = 0,
    kChoiceOn    = 1,
    kChoiceOther = 0x7f
};

static s8 WhatChoice(int argc, const char *argv[], const char *other) {
    if (HasArg(argc, argv, "off")) return kChoiceOff;
    if (HasArg(argc, argv, "on"))  return kChoiceOn;
    if (other && HasArg(argc, argv, other)) return kChoiceOther;
    return kChoiceNone;
}

static bool ValidPassword(const char *pass) {
    if (!pass) return false;
    int len = lt_strlen(pass);
    return len >= 8 && len <=64;
}

// This table must stay in sync with enum LTWiFi_ApSecurity
static const char *ApSecurityStrings[kLTWiFi_ApSecurity_Max] = { LTWiFi_SecurityStrings };

static void PrintApInfo(const char *prefix, LTWiFi_ApInfo *ap) {
    LT_UNUSED(prefix);
    LT_UNUSED(ap);
    LTLOG_DEBUG("ap.info", "%s AP ssid: %s pass: %s sec: %s", prefix, ap->ssid, ap->pass, ApSecurityStrings[ap->security]);
}

static void WiFiPrint(LTShell hShell, const char *fmt, ...) {
    lt_va_list args;
    lt_va_start(args, fmt);
    SHL_iShell->Print(hShell, "WiFi: ");
    SHL_iShell->VPrint(hShell, fmt, args);
    lt_va_end(args);
}

static const char *getApModeString(u8 mode) {
    enum BSS_MODE {
        IEEE80211_BSS_MODE_HE      = (1 << 0),
        IEEE80211_BSS_MODE_VHT     = (1 << 1),
        IEEE80211_BSS_MODE_HT      = (1 << 2),
        IEEE80211_BSS_MODE_LEGACY  = (1 << 3),
    };
    if (mode & IEEE80211_BSS_MODE_HE) {
        return "802.11ax";
    } else if (mode & IEEE80211_BSS_MODE_VHT) {
        return "802.11ac";
    } else if (mode & IEEE80211_BSS_MODE_HT) {
        return "802.11n";
    } else if (mode & IEEE80211_BSS_MODE_LEGACY) {
        return "legacy";
    }

    return "unknown";
}

/** Scan Result Handling ******************************************************/

typedef struct APNode {
    LTList_Node    node;
    u16           heard;
    LTWiFi_ApInfo ap;
} APNode;

static void SHL_AddApList(LTWiFi_ApInfo *new_ap) {
    APNode *apn = NULL;

    LTList_ForEach(node, &SHL_ScanApList) {
        apn = (APNode*)node;
        if (lt_strcmp(apn->ap.ssid, new_ap->ssid) == 0) { // same SSID (compare BSSID also?!!!)
            apn->heard++;
            if (new_ap->rssi <= apn->ap.rssi) return; // keep it as is
            LTList_Remove(node); // we will add it back below
            break;
        }
        apn = NULL;
    } LTList_EndForEach;

    if (!apn) {
        apn = lt_malloc(sizeof(APNode));
        LT_ASSERT(apn);
        apn->heard = 1;
    }

    // Insert in list by RSSI:
    apn->ap = *new_ap;
    LTList_ForEach(node, &SHL_ScanApList) {
        if (new_ap->rssi > ((APNode*)node)->ap.rssi) {
            LTList_InsertBefore(node, (LTList_Node *)apn);
            return;
        }
    } LTList_EndForEach;
    LTList_AddTail(&SHL_ScanApList, &apn->node); // lowest RSSI
}

static void SHL_ListApList(LTShell hShell) {
    APNode *apn;
    u16 count = 0;
    LTList_ForEach(node, &SHL_ScanApList) {
        apn = (APNode*)node;
        LTWiFi_ApInfo *ap = &apn->ap;
        char bssid[20];
        iMacAddress->MacAddressToString(&ap->bssid, bssid, ':');
        SHL_iShell->Print(hShell, "  %s hrd:%3u chan:%3u rssi:%4d sec:%-6s \"%s\"\n",
            bssid, apn->heard, ap->channel, ap->rssi, ApSecurityStrings[ap->security], ap->ssid);
        count++;
    } LTList_EndForEach;
    SHL_iShell->Print(hShell, "  %u APs heard\n", count);
}

static void SHL_FreeApList(void) {
    LTList_ForEach(node, &SHL_ScanApList) lt_free(node); LTList_EndForEach;
    LTList_Init(&SHL_ScanApList); // clears list
}

static void /*LTDeviceWiFi_ScanCallback*/ SHL_ScanCallback(LTWiFi_ApInfo *ap, void *callback_data);

static void SHL_ShowScanResults(void *data) {  // called by QueueTaskProc
    SHL_ListApList(VOIDPTR_TO_LTHANDLE(data));
    if (SHL_ScanRepeat <= 0) SHL_FreeApList();
    else iWiFi->ScanAps(NULL, SHL_ScanCallback, data);
}

/** Callback Functions ********************************************************/

static void /*LTDeviceWiFi_StatusCallback*/ SHL_PrintWiFiStatus(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(unit);
    LT_UNUSED(clientData);
    const char *msg = NULL;
    switch(status) {
        case kLTDeviceWiFi_Status_JoinStart:      msg = "joining";        break;
        case kLTDeviceWiFi_Status_JoinAssociated: msg = "associated";     break;
        case kLTDeviceWiFi_Status_Connected:      msg = "connected";      break;
        case kLTDeviceWiFi_Status_Disconnected:   msg = "disconnected";   break;
        case kLTDeviceWiFi_Status_Rejoining:      msg = "rejoin attempt"; break;
        default: break;
    }
    if (msg) LTLOG("status", "status change: %s\n", msg);
}

static void /*LTDeviceWiFi_ScanCallback*/ SHL_ScanCallback(LTWiFi_ApInfo *ap, void *callback_data) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(callback_data);
    if (!ap) {
        SHL_iShell->Print(hShell, "\n");
        if (SHL_ScanRepeat > 0) {
            SHL_ScanRepeat--;
            iThread->QueueTaskProc(SHL_Thread, SHL_ShowScanResults, NULL, LTHANDLE_TO_VOIDPTR(hShell));
        }
        return;
    }
    if (!SHL_ScanProbes) SHL_iShell->Print(hShell, ".");
    else {
        char bssid[20];
        iMacAddress->MacAddressToString(&ap->bssid, bssid, ':');
        SHL_iShell->Print(hShell, "  %s chan:%3u rssi:%4d sec:%-6s \"%s\"\n",
            bssid, ap->channel, ap->rssi, ApSecurityStrings[ap->security], ap->ssid);
    }
    SHL_AddApList(ap);
}

static void /*LTDeviceWiFi_JoinCallback*/ SHL_JoinCallback(LTWiFi_JoinStatus status, void *callback_data) {
    LTShell hShell = VOIDPTR_TO_LTHANDLE(callback_data);
    char *str;
    switch (status) {
        case kLTWiFi_JoinStatus_Success:
            str = "connected to AP (done)";
            break;
        case kLTWiFi_JoinStatus_Starting:   str = "trying to join AP"; break;
        case kLTWiFi_JoinStatus_Associated: str = "associated to AP"; break;
        case kLTWiFi_JoinStatus_Failed:     str = "failed to connect (done)"; break;
        case kLTWiFi_JoinStatus_Aborted:    str = "action terminated (done)"; break;
        default: str = "in progress...";
    }
    LTLOG("join", "%s", str);
    if (iWiFi->IsConnected() && hShell) SHL_Status(hShell, 0, NULL);
    if (iWiFi->IsConnected() && SHL_JoinSave && iWiFi->SaveApSettings(&SHL_ApSpec)) {
        PrintApInfo("SAVE", &SHL_ApSpec);
    }
}

/** Command Functions *********************************************************/

static int SHL_Scan(LTShell hShell, int argc, const char *argv[]) {
    LTWiFi_ScanSpec spec = {};
    LTWiFi_ScanSpec *spec_ptr = NULL;
    char target_ssid[256] = {0, };
    u8 target_channels[16] = {0, };
    int pos = 0;
    SHL_ScanRepeat = 1;
    SHL_ScanProbes = false;
    if (HasArg(argc, argv, "-r")) SHL_ScanRepeat = 4;    // repeat a few times
    if (HasArg(argc, argv, "-p")) SHL_ScanProbes = true; // show probes
    if ((pos = HasArg(argc, argv, "-S")) && pos && pos + 1 < argc) { // target scan SSID
        lt_strncpyTerm(target_ssid, argv[pos + 1], 256);
        spec.ssid = target_ssid;
        spec_ptr = &spec;
    }
    if ((pos = HasArg(argc, argv, "-C")) && pos && pos + 1 < argc) { // target scan, channels, comma separate
        int i = 0;
        const char *ptr = ptr = argv[pos + 1];
        char *end;
        do {
            int v = lt_strtos32(ptr, &end, 0);
            if (ptr != end) {
                target_channels[i++] = (u8)(v & 0xff);
            } else {
                break;
            }
            ptr = end + 1;
        } while(i < 15);

        spec.channel = target_channels;
        spec_ptr = &spec;
    }
    if ((pos = HasArg(argc, argv, "-R")) && pos && pos + 1 < argc) { // target scan, rssi
        spec.rssi = lt_strtos32(argv[pos + 1], NULL, 0);
        spec_ptr = &spec;
    }
    WiFiPrint(hShell, "scanning for access points... target: %s\n", (spec.ssid)?spec.ssid:"None");
    iWiFi->ScanAps(spec_ptr, SHL_ScanCallback, LTHANDLE_TO_VOIDPTR(hShell));
    return LTShellWiFi_OK;
}

static int SHL_Join(LTShell hShell, int argc, const char *argv[]) {
    // Note: typing "join" without args will use prior ApSpec
    SHL_JoinSave = false;
    SHL_ApSpec.options = 0;
    if (argc > 1 && lt_strcmp(argv[argc - 1], "-r") == 0) {
        WiFiPrint(hShell, "kLTWiFi_JoinOption_Retry enabled\n");
        SHL_ApSpec.options |= kLTWiFi_JoinOption_Retry;
        argc--;
    }
    if (argc > 1) {
        if (argc > 1) {
            lt_strncpyTerm(SHL_ApSpec.ssid, argv[1], kLTWiFi_Max_Ssid + 1);
        }
        SHL_ApSpec.pass[0] = 0;
        if (argc > 2) {
            if (ValidPassword(argv[2])) {
                lt_strncpyTerm(SHL_ApSpec.pass, argv[2], kLTWiFi_Max_Pass + 1);
                SHL_ApSpec.security = kLTWiFi_ApSecurity_Wpa2;
                PrintApInfo("JOIN", &SHL_ApSpec);
                SHL_JoinSave = true;
            } else {
                WiFiPrint(hShell, "password not valid\n");
                return LTShellWiFi_PASSWD_INVALID;
            }
        }
        lt_memset(SHL_ApSpec.bssid.octet, 0, sizeof(SHL_ApSpec.bssid.octet));
        if (argc > 3) {
            WiFiPrint(hShell, "Setting BSSID %s\n", argv[3]);
            if (!iMacAddress->StringToMacAddress(argv[3], &SHL_ApSpec.bssid)) {
                WiFiPrint(hShell, "Wrong format: %s, joining without BSSID\n", argv[3]);
                lt_memset(SHL_ApSpec.bssid.octet, 0, sizeof(SHL_ApSpec.bssid.octet));
            }
        }
        if (SHL_ApSpec.pass[0] == 0) {
            WiFiPrint(hShell, "no password specified, open AP assumed\n");
            SHL_ApSpec.security = kLTWiFi_ApSecurity_Open;
            SHL_JoinSave = true;
        }
    } else {
        WiFiPrint(hShell, "rejoin prior AP\n");
    }

    if (SHL_ApSpec.ssid[0]) WiFiPrint(hShell, "joining access point \"%s\"...%s \n", SHL_ApSpec.ssid, SHL_ApSpec.pass);
    // If no args, rejoin current AP.
    if (SET_RejoinIsSet()) SHL_ApSpec.options |= kLTWiFi_JoinOption_Rejoin;
    iWiFi->JoinAp(&SHL_ApSpec, SHL_JoinCallback, LTHANDLE_TO_VOIDPTR(hShell));
    return LTShellWiFi_OK;
}

static int SHL_AutoJoin(LTShell hShell, int argc, const char *argv[]) {
    s8 state = WhatChoice(argc, argv, NULL);
    if (!state) {
        WiFiPrint(hShell, "autojoin is %s\n", SET_AutoJoinIsSet() ? "on" : "off");
        return LTShellWiFi_OK;
    }
    SET_AutoJoin(state > 0);
    WiFiPrint(hShell, "autojoin is %s\n", state > 0 ? "on" : "off");
    return LTShellWiFi_OK;
}

static int SHL_Rejoin(LTShell hShell, int argc, const char *argv[]) {
    s8 state = WhatChoice(argc, argv, NULL);
    if (!state) {
        WiFiPrint(hShell, "rejoin is %s\n", SET_RejoinIsSet() ? "on" : "off");
        return LTShellWiFi_OK;
    }
    SET_Rejoin(state > 0);
    WiFiPrint(hShell, "rejoin is %s\n", state > 0 ? "on" : "off");
    return LTShellWiFi_OK;
}

static int SHL_Disconnect(LTShell hShell, int argc, const char *argv[]) {
    bool fake = false;
    if (HasArg(argc, argv, "-f")) fake = true; // non-state machine disconnect (simulates AP disconnect)
    if (!fake) iWiFi->Disconnect();
    else iWiFi->SetOption("disconnect", 0);
    WiFiPrint(hShell, "disconnected %s\n", fake ? "(fake)" : "");
    return LTShellWiFi_OK;
}

static int SHL_Forget(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    char *status = "not connected\n";
    if (iWiFi->IsConnected()) {
        status = "disconnected\n";
    }
    iWiFi->Disconnect();

    if (iWiFi->LoadApSettings(&SHL_ApSpec)) {
        CLEAR(SHL_ApSpec);
        CLEAR(SHL_ApPass);
        SHL_ApSpec.pass = SHL_ApPass;
        iWiFi->SaveApSettings(&SHL_ApSpec);
        status = "settings cleared\n";
    }

    WiFiPrint(hShell, status);
    return LTShellWiFi_OK;
}

static int SHL_ForgetAll(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    iWiFi->Disconnect();
    iWiFi->SaveApSettings(NULL);
    CLEAR(SHL_ApSpec);
    CLEAR(SHL_ApPass);
    SHL_ApSpec.pass = SHL_ApPass;

    WiFiPrint(hShell, "all settings cleared\n");
    return LTShellWiFi_OK;
}

static int SHL_Status(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTWiFi_ApInfo ap_info;
    CLEAR(ap_info);
    if (iWiFi->GetApInfo(&ap_info)) {
        char bssid[20];
        iMacAddress->MacAddressToString(&ap_info.bssid, bssid, ':');
        SHL_iShell->Print(hShell, "  SSID:      %s\n", ap_info.ssid);
        SHL_iShell->Print(hShell, "  BSSID:     %s\n", bssid);
        SHL_iShell->Print(hShell, "  Channel:   %u\n", ap_info.channel);
        SHL_iShell->Print(hShell, "  Security:  %s\n", ApSecurityStrings[ap_info.security]); // guaranteed in-bounds
        SHL_iShell->Print(hShell, "  RSSI:      %d dBm\n", ap_info.rssi);
        SHL_iShell->Print(hShell, "  SNR:       %d dB\n", ap_info.snr);
        SHL_iShell->Print(hShell, "  Mode:      %s\n", getApModeString(ap_info.mode));
    } else {
        /* string of enum LTWiFi_DisconnectReason in LTWiFi.h */
        const char *reason_str[kLTWiFi_DisconnectReason_Max] = {
            "UnknownReason",
            "SystemRequest",
            "DriverGeneric",
            "DriverNetdevDown",
            "DriverDfsDetection",
            "DriverApUnsupChan",
            "DriverApDfsChan",
            "DriverApKeepaliveTimeout",
            "DriverApRoam",
            "DriverNoApFound",
            "ApReceiveDeauth",
            "ApReceiveDisassoc",
            "Generic"
        };
        WiFiPrint(hShell, "not connected dcrsn: %s\n", reason_str[iWiFi->GetDiscReason()]);
    }
    // Valid even when not connected:
    LTWiFi_Metrics metrics = {};
    iWiFi->GetMetrics(&metrics, sizeof(metrics));
    SHL_iShell->Print(hShell, "  TX Frames: %lu\n", LT_Pu32(metrics.tx_frame_count));
    SHL_iShell->Print(hShell, "  RX Frames: %lu\n", LT_Pu32(metrics.rx_frame_count));
    return LTShellWiFi_OK;
}

static int SHL_SoftAp(LTShell hShell, int argc, const char *argv[]) {
    CLEAR(SHL_SoftApSpec);
    CLEAR(SHL_SoftApPass);
    SHL_SoftApSpec.pass = SHL_SoftApPass;
    if (argc <= 2) {
        WiFiPrint(hShell, "provide SSID and password\n");
        return LTShellWiFi_ARG_ERR;
    }
    if (!ValidPassword(argv[2])) {
        WiFiPrint(hShell, "password not valid\n");
        return LTShellWiFi_PASSWD_INVALID;
    }
    lt_strncpyTerm(SHL_SoftApSpec.ssid, argv[1], kLTWiFi_Max_Ssid + 1);
    lt_strncpyTerm(SHL_SoftApSpec.pass, argv[2], kLTWiFi_Max_Pass + 1);
    SHL_SoftApSpec.security = kLTWiFi_ApSecurity_Wpa2;
    SHL_SoftApSpec.channel = 6;
    if (argc > 3) {
        if (iWiFi->IsConnected()) WiFiPrint(hShell, "channel ignored when STA is connected\n");
        SHL_SoftApSpec.channel = lt_strtou32(argv[3], NULL, 10);
    }
    if (!iWiFi->StartAp(&SHL_SoftApSpec)) {
        WiFiPrint(hShell, "soft AP failed: \"%s\"\n", SHL_SoftApSpec.ssid);
        return LTShellWiFi_SOFTAP_FAIL;
    }
    WiFiPrint(hShell, "soft AP \"%s\" is up\n", SHL_SoftApSpec.ssid);
    return LTShellWiFi_OK;
}

static int SHL_Sniff(LTShell hShell, int argc, const char *argv[]) {
    int channel = 0; // means don't change channel
    const char *status;
    switch (WhatChoice(argc, argv, NULL)) {
        case kChoiceOff:
            channel = -1; // shut sniff off
            status = "disabled";
            break;
        case kChoiceNone:
            // Not on/off. Try to get channel. If 0, don't change channel.
            if (argc > 1) channel = lt_strtou32(argv[1], NULL, 10);
            // fall thru...
        default:
            status = "enabled";
            break;
    }
    SHL_SniffMode = channel >= 0;
    iWiFi->SetOption("sniff", channel);
    WiFiPrint(hShell, "sniff %s\n", status);
    return LTShellWiFi_OK;
}

static int SHL_SetLiIntvl(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        WiFiPrint(hShell, "provide listen interval\n");
        return LTShellWiFi_ARG_ERR;
    }
    u16 li = (u16)lt_strtou32(argv[1], NULL, 10);
    iWiFi->SetOption("listen_interval", li);
    WiFiPrint(hShell, "listen interval set to %u\n", li);
    return LTShellWiFi_OK;
}

static int SHL_GetLiIntvl(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u16 li = iWiFi->GetOption("listen_interval");
    WiFiPrint(hShell, "listen interval is %u\n", li);
    return LTShellWiFi_OK;
}

static int SHL_Iwpriv(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell);
    if (argc < 1) {
        return LTShellWiFi_ARG_ERR;
    }
    if (iWiFi->IwPriv(argc - 1, argv + 1, NULL)) {
        return LTShellWiFi_IWPRIV_FAIL;
    }

    return LTShellWiFi_OK;
}

static int SHL_Channel(LTShell hShell, int argc, const char *argv[]) {
    if (argc > 1 && lt_strcmp(argv[1], "list") == 0) {
        LTString chans = ltstring_create("");
        u32 channels;
        int i;
        channels = iWiFi->GetOption("channels_1-14");
        for (i = 0 ; i < 14 ; i++) if ((channels >> i) & 0x1)
                                       ltstring_appendformat(&chans, "%d,", 1+i);
        channels = iWiFi->GetOption("channels_36-144");
        for (i = 0 ; i < 28 ; i++) if ((channels >> i) & 0x1)
                                       ltstring_appendformat(&chans, "%d,", 36+i*4);
        channels = iWiFi->GetOption("channels_149-183");
        for (i = 0 ; i < 10 ; i++) if ((channels >> i) & 0x1)
                                       ltstring_appendformat(&chans, "%d,", 149+i*4);
        if (!ltstring_isempty(chans)) {
            // Trim ','
            chans[lt_strlen(chans) - 1] = '\0';
        }
        WiFiPrint(hShell, "channel list is %s\n", chans);
        ltstring_destroy(chans);
    } else {
        u32 channel = 0;
        if (argc > 1) {
            channel = lt_strtou32(argv[1], NULL, 10);
            iWiFi->SetOption("channel", channel);
        } else {
            channel = iWiFi->GetOption("channel");
        }
        WiFiPrint(hShell, "channel is %d\n", channel);
    }
    return LTShellWiFi_OK;
}

static int SHL_Info(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTWiFi_DriverInfo info;
    if (!iWiFi->GetDeviceInfo(&info)) {
        WiFiPrint(hShell, "cannot get WiFi info\n");
    } else {
        SHL_iShell->Print(hShell, "  Vendor:  %s\n", info.vendor);
        SHL_iShell->Print(hShell, "  Product: %s\n", info.product);
        SHL_iShell->Print(hShell, "  Version: %s\n", info.version);
        SHL_iShell->Print(hShell, "  Updated: %s\n", info.updated);
        char str[20];
        iMacAddress->MacAddressToString(&info.mac_address, str, ':');
        SHL_iShell->Print(hShell, "  MACAddr: %s\n", str);
    }
    return LTShellWiFi_OK;
}

static int SHL_GetMac(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTMacAddress mac;
    iWiFi->GetMacAddress(&mac); // ignore return
    char str[20];
    iMacAddress->MacAddressToString(&mac, str, ':');
    WiFiPrint(hShell, "MACAddr: %s\n", str);
    return LTShellWiFi_OK;
}

static int SHL_SetMac(LTShell hShell, int argc, const char *argv[]) {
    LTMacAddress mac;
    if (argc < 2 || !iMacAddress->StringToMacAddress(argv[1], &mac)) {
        WiFiPrint(hShell, "provide a MAC address in the form 11:22:33:44:55\n");
        return LTShellWiFi_OK;
    }
    if (iWiFi->SetMacAddress(&mac)) WiFiPrint(hShell, "MAC address set to: %s\n", argv[1]);
    else {
        WiFiPrint(hShell, "could not set MAC address\n");
        return -1;
    }
    return LTShellWiFi_OK;
}

static void SET_AutoBoot(char *command); // forward

static int SHL_AutoBoot(LTShell hShell, int argc, const char *argv[]) {
    char *command;
    switch (WhatChoice(argc, argv, "mesh")) {
        case kChoiceOff:   command = NULL; break;
        case kChoiceOther: command = "ltopen LTShellWiFi; wifi mesh"; break;
        default:
            if (WhatChoice(argc, argv, "ip") == kChoiceOther)  command = "ltopen LTNetMonitor";
            else command = "ltopen LTShellWiFi";
            break;
    }
    SET_AutoBoot(command);
    WiFiPrint(hShell, "autoboot using \"%s\"\n", command);
    return LTShellWiFi_OK;
}

static int SHL_Debug(LTShell hShell, int argc, const char *argv[]) {
    switch (WhatChoice(argc, argv, "verbose")) {
        case kChoiceOn:   SHL_DebugMode = 0x1; break;
        case kChoiceOther: SHL_DebugMode = 0x3; break;
        default:           SHL_DebugMode = 0x0; break;
    }

    if (HasArg(argc, argv, "wsm"))  SHL_DebugMode |= 0x08;
    if (HasArg(argc, argv, "rx"))   SHL_DebugMode |= 0x10;
    if (HasArg(argc, argv, "tx"))   SHL_DebugMode |= 0x20;
    if (HasArg(argc, argv, "mgmt")) SHL_DebugMode |= 0x40;
    if (HasArg(argc, argv, "ble"))  SHL_DebugMode |= 0x80;
    if (HasArg(argc, argv, "wpa"))  SHL_DebugMode |= 0x100;
    if (HasArg(argc, argv, "saved"))  SHL_DebugMode |= 0x200;
    if (HasArg(argc, argv, "status"))  SHL_DebugMode |= 0x400;

    iWiFi->SetOption("debug_mode", SHL_DebugMode);
    WiFiPrint(hShell, "debug %s [0x%x]\n", SHL_DebugMode ? "enabled" : "disabled", SHL_DebugMode);
    return LTShellWiFi_OK;
}

static int SHL_Reset(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    iWiFi->Reset();
    WiFiPrint(hShell, "driver reset...\n");
    return LTShellWiFi_OK;
}

static const LTSystemShell_CommandDesc WiFi_Commands[] = {
    { "help",               SHL_Help,       "list of WiFi commands",                 NULL },
    { "info",               SHL_Info,       "show info about WiFi",                  NULL },
    { "reset",              SHL_Reset,      "reset WiFi driver",                     NULL },
    { "debug",              SHL_Debug,      "debug [on|off|verbose|saved][wsm][rx][tx][mgmt][ble][wpa]",  NULL },
    { "scan",               SHL_Scan,       "scan for WiFi access points\n"
                                            "\t\t-T target scan, scan -S <ssid>\n"
                                            "\t\t-C target scan, scan -C <1,2,3,...>\n"
                                            "\t\t-R target scan, scan -R <RSSI>\n"
                                            "\t\t-r repeat scan\n"
                                            "\t\t-p show probes/beacons",            NULL },
    { "join",               SHL_Join,       "join <ssid> [<pass>] [bssid] [-r]\n"
                                            "\t\t-r retry until connected\n",        NULL },
    { "status",             SHL_Status,     "show WiFi connection status",           NULL },
    { "rejoin",             SHL_Rejoin,     "auto-rejoin on AP loss",                NULL },
    { "disconnect",         SHL_Disconnect, "disconnect AP [-f (fake AP discon)]",   NULL },
    { "forget",             SHL_Forget,     "forget WiFi credentials",               NULL },
    { "forgetall",          SHL_ForgetAll,  "forget all WiFi settings",              NULL },
    { "autojoin",           SHL_AutoJoin,   "enable join on startup",                NULL },
    { "autoboot",           SHL_AutoBoot,   "WiFi autoboot [off|on|mesh|ip]",        NULL },
    { "channel",            SHL_Channel,    "get, set or list current channel(s)",   NULL },
    { "getmac",             SHL_GetMac,     "show WiFi MAC address",                 NULL },
    { "setmac",             SHL_SetMac,     "set WiFi MAC address",                  NULL },
    { "softap",             SHL_SoftAp,     "start soft AP <ssid> <pass>",           NULL },
    { "iwpriv",             SHL_Iwpriv,     "run vendor specific commands <cmd> [args]", NULL },
    { "sniff",              SHL_Sniff,      "show frames [<channel>|off]",           NULL },
    { "set-listen-interval", SHL_SetLiIntvl, "set-listen-interval <interval>",        NULL },
    { "get-listen-interval", SHL_GetLiIntvl, "get-listen-interval",                   NULL },
    { NULL,         NULL,           NULL,                                    NULL }
};

static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    for (int n = 0; WiFi_Commands[n].pCommand; n++) {
        SHL_iShell->Print(hShell, "  %-10s - %s\n", WiFi_Commands[n].pCommand,
            WiFi_Commands[n].pDescription);
    }
    SHL_iShell->Print(hShell,
        "Examples:\n"
        "  wifi scan (and wait a few seconds)\n"
        "  wifi join <ssid> <password>\n"
        "  wifi join <ssid> (no password for open APs)\n"
        "  wifi autojoin on/off\n"
        "  wifi autoboot on/off/mesh/ip"
    );
    SHL_iShell->Print(hShell,
        "  wifi mesh \"dev: light state: off\"\n"
        "  wifi list\n"
        "  wifi send 1 \"hello world\"\n"
        "  wifi ping 3 100\n"
        "  wifi ping -f 3\n"
    );
    SHL_iShell->Print(hShell,
        "  wifi rejoin on/off\n"
        "  wifi getmac\n"
        "  wifi setmac 00:11:22:33:44:55 (Warning: do only once!)\n"
        "  wifi channel list\n"
        "  get  wifi* (to see wifi settings)\n"
    );
    SHL_iShell->Print(hShell,
        " wifi set-listen-interval <interval>\n"
        " wifi get-listen-interval\n"
    );
    return LTShellWiFi_OK;
}

static int SHL_WiFi(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; WiFi_Commands[n].pCommand; n++) {
            if (lt_strcmp(WiFi_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return WiFi_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help wifi" works in addition to "wifi help".
       It prints usage and calls the regular SHL_Help */
    SHL_iShell->Print(hShell, "usage: wifi <command> [args]\nCommands:\n");
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "wifi", SHL_WiFi, "wifi commands, type \"wifi\" for a list", LTShell_Help },
};

static bool SHL_InitThread(void) {
    iWiFi->OnStatusChange(SHL_PrintWiFiStatus, NULL, NULL);
    return true;
}

static void SHL_ExitThread(void) {
    iWiFi->NoStatusChange(SHL_PrintWiFiStatus);
}

static void SHL_Quit(void) {
    if (SHL_Library) {
        SHL_Library->UnregisterCommands(SHL_Commands);
        lt_closelibrary(SHL_Library);
        iThread->Destroy(SHL_Thread); // zero ok
    }
}

static bool SHL_Init(void) {
    SHL_Library = lt_openlibrary(LTSystemShell);
    if (!SHL_Library) return false;

    SHL_Thread = pCore->CreateThread("ShellWiFiStatus");
    if (!SHL_Thread) return false;
    iThread->SetStackSize(SHL_Thread, 1536);
    iThread->Start(SHL_Thread, SHL_InitThread, SHL_ExitThread);

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));

    CLEAR(SHL_ApSpec);
    CLEAR(SHL_ApPass);
    SHL_ApSpec.pass = SHL_ApPass;
    SHL_JoinSave    = false;
    SHL_SniffMode   = false;
    SHL_DebugMode   = 0;
    LTList_Init(&SHL_ScanApList);
    return true;
}

/*******************************************************************************
 * WiFi Settings
 ******************************************************************************/

static LTSystemSettings *SET_Library;

static void SET_AutoJoin(bool state) {
    iWiFi->SetOption("autojoin", state ? 1 : 0);
}

static bool SET_AutoJoinIsSet(void) {
    return !!iWiFi->GetOption("autojoin");
}

static void SET_Rejoin(bool state) {
    iWiFi->SetOption("rejoin", state ? 1 : 0);
}

static bool SET_RejoinIsSet(void) {
    return !!iWiFi->GetOption("rejoin");
}

#define LTSHELL_BOOT_KEYNAME "ltshell.boot.cmd"

static void SET_AutoBoot(char *command) {
    // Note: This will clobber any other shell boot cmd (shell should support a list).
    if (command) SET_Library->SetStringValue(LTSHELL_BOOT_KEYNAME, command);
    else SET_Library->DeleteSetting(LTSHELL_BOOT_KEYNAME);
}

static bool SET_Init(void) {
    /* open LTSystemSettings, if non-NULL we can proceed to read settings right away */
    if (!(SET_Library = lt_openlibrary(LTSystemSettings))) return false;
    return true;
}

static void SET_Quit(void) {
    lt_closelibrary(SET_Library); // null ok
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellWiFiImpl_LibFini(void) {
    SET_Quit();
    SHL_Quit();
    lt_closelibrary(iWiFi);       // null ok
    lt_closelibrary(iMacAddress); // null ok
}

static bool LTShellWiFiImpl_LibInit(void) {
    pCore = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iWiFi   = lt_openlibrary(LTDeviceWiFi);
    iMacAddress = lt_openlibrary(LTUtilityMacAddress);
    if (iWiFi && iMacAddress && SHL_Init() && SET_Init()) return true;
    LTShellWiFiImpl_LibFini();
    return false;
}

static int LTShellWiFi_Run(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    if (!SelfLib) SelfLib = LT_GetCore()->OpenLibrary("LTShellWiFi"); // open a second time so I stay resident when main exits
    return LTShellWiFi_OK;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellWiFi, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellWiFi, LTShellWiFi_Run) LTLIBRARY_DEFINITION;
