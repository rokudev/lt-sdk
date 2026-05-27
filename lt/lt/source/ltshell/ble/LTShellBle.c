/*******************************************************************************
 *
 * LTShellBle: BLE Shell
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
#include <lt/core/LTCore.h>
#include <lt/device/ble/LTDeviceBle.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>


LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellBle, (LTUtilityByteOps));

/** Standard LT Interfaces ****************************************************/

static LTCore     *pCore;
static ILTThread  *iThread;
static LTDeviceBle *iBle;
static LTUtilityMacAddress *iMacAddress;
static LTLibrary *SelfLib;
bool flag = true;

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static LTSystemShell *SHL_Library;

/** Shell Variables ***********************************************************/
static ILTShell      *SHL_iShell;
static LTThread      SHL_Thread;

/** Utility Functions *********************************************************/

#define CLEAR(v) lt_memset(&(v), 0, sizeof(v))
static int HasArg(int argc, const char *argv[], const char *pattern) {
    // argv[0] is command and is skipped
    for (int n = 1; n < argc; n++) {
        if (lt_strcmp(argv[n], pattern) == 0) return n;
    }
    return 0;
}


static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);

static int SHL_Start(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    /*
    if (iBle->Start() != 0) {
        SHL_iShell->Print(hShell, "BleControllerStart failed\n");
        return 1;
    }
    */
    return 0;
}

static int SHL_Stop(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    iBle->Stop();
    return 0;
}

static int SHL_Adv(LTShell hShell, int argc, const char *argv[]) {
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }

    if (HasArg(argc, argv, "start")) {
        SHL_iShell->Print(hShell, "Start Advertisement (Not Implemented)\n");
    } else if (HasArg(argc, argv, "stop")) {
        SHL_iShell->Print(hShell, "Stop Advertisement\n");
        iBle->StopAdvertise();
    } else {
        SHL_iShell->Print(hShell, "Invalid argument, please use start|stop\n");
        return 1;
    }
    SHL_iShell->Print(hShell, "Success\n");
    return 0;
}

static int SHL_Cmd(LTShell hShell, int argc, const char *argv[]) {
    int rc  = 0;
    u8 *resp = NULL;
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        rc = 1;
        goto done;
    }
    if (argc<2) {
        SHL_iShell->Print(hShell, "Invalid usage of set-cmd,"
            " Please provide the command string\n");
        rc = 1;
        goto done;
    }
    u32 respLen = 0;
    if (argc == 3) {
        respLen = lt_strtou32(argv[2], NULL, 0);
    }
    if (respLen) {
        resp = (u8 *)lt_malloc(respLen);
        if (!resp) {
            SHL_iShell->Print(hShell, "Memory allocation failed\n");
            rc = 1;
            goto done;
        }
    }
    if (!iBle->SendPrivVenCommand(argv[1], resp, respLen)) {
        SHL_iShell->Print(hShell, "SendPrivVenCommand failed\n");
        rc = 1;
        goto done;
    }
    if (resp && respLen) {
        SHL_iShell->Print(hShell, "Response: ");
        for (u32 i = 0; i < respLen; i++) {
            SHL_iShell->Print(hShell, "%02X ", resp[i]);
        }
        SHL_iShell->Print(hShell, "\n");
    }
    SHL_iShell->Print(hShell, "Success\n");
done:
    lt_free(resp);
    return rc;
}

static int SHL_HciCmd(LTShell hShell, int argc, const char *argv[]) {
    int rc  = 0;
    u32 bufferSize = 0;
    u8 *resp = NULL;
    u8 buffer[bufferSize];
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        rc = 1;
        goto done;
    }
    if (argc < 2) {
        SHL_iShell->Print(hShell, "Invalid usage of hci-cmd,"
            " Please provide the opcode and command string\n");
        rc = 1;
        goto done;
    }
    u16 opcode = lt_strtou32(argv[1], NULL, 0);
    if (argc > 2) {
        u32 hexStrLen = lt_strlen(argv[2]);
        if (hexStrLen) {
            if (hexStrLen % 2 != 0) {
                SHL_iShell->Print(hShell, "Hex string must have an even number of characters.\n");
                rc = 1;
                goto done;
            }
            bufferSize = hexStrLen / 2;
            LT_GetLTUtilityByteOps()->HexDecode(argv[2], hexStrLen, buffer, bufferSize);
        }
    }

    u32 respLen = 0;
    if (argc > 3) {
        respLen = lt_strtou32(argv[3], NULL, 0);
    }
    if (respLen) {
        resp = (u8 *)lt_malloc(respLen);
        if (!resp) {
            SHL_iShell->Print(hShell, "Memory allocation failed\n");
            rc = 1;
            goto done;
        }
    }

    if (!iBle->SendHciCommand(opcode, buffer, bufferSize, resp, respLen)) {
        SHL_iShell->Print(hShell, "SendHciCommand failed\n");
        rc = 1;
        goto done;
    }
    if (resp && respLen) {
        SHL_iShell->Print(hShell, "Response: ");
        for (u32 i = 0; i < respLen; i++) {
            SHL_iShell->Print(hShell, "%02X ", resp[i]);
        }
        SHL_iShell->Print(hShell, "\n");
    }
    SHL_iShell->Print(hShell, "Success\n");
done:
    lt_free(resp);
    return rc;
}

static int SHL_Log(LTShell hShell, int argc, const char *argv[]) {
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    if (HasArg(argc, argv, "start")) {
        SHL_iShell->Print(hShell, "Start BLE Logging\n");
        iBle->EnableLogging(true);
    } else if (HasArg(argc, argv, "stop")) {
        SHL_iShell->Print(hShell, "Stop BLE Logging\n");
        iBle->EnableLogging(false);
    } else {
        SHL_iShell->Print(hShell, "Invalid argument, please use start|stop\n");
        return 1;
    }
    
    return 0;
}

static int SHL_GetMac(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    LTMacAddress own_mac;
    lt_memset(&own_mac, 0, sizeof(LTMacAddress));
    iBle->GetOwnAddress(&own_mac);
    SHL_iShell->Print(hShell, "BLE MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
        own_mac.octet[0], own_mac.octet[1], own_mac.octet[2],
        own_mac.octet[3], own_mac.octet[4], own_mac.octet[5]);
    return 0;
}

static int SHL_GetPeerMac(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    LTMacAddress peer_mac;
    lt_memset(&peer_mac, 0, sizeof(LTMacAddress));
    iBle->GetPeerAddress(&peer_mac);
    SHL_iShell->Print(hShell, "BLE Peer MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
        peer_mac.octet[0], peer_mac.octet[1], peer_mac.octet[2],
        peer_mac.octet[3], peer_mac.octet[4], peer_mac.octet[5]);
    return 0;
}

static int SHL_GetDevName(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    const char *devName = iBle->GetDeviceName();
    SHL_iShell->Print(hShell, "BLE Device Name: %s\n", devName);
    return 0;
}

static int SHL_UpdateConnectionParams(LTShell hShell, int argc, const char *argv[]) {
    if (!iBle) {
        SHL_iShell->Print(hShell, "BLE library not available\n");
        return 1;
    }
    if (argc < 2) {
        SHL_iShell->Print(hShell,
                          "Invalid usage of update-conn-param,"
                          " Please provide the connection interval_min, interval_max and supervision timeout\n");
        return 1;
    }
    LTBleUpdConnParams ble_conn_params;
    lt_memset(&ble_conn_params, 0x00, sizeof(LTBleUpdConnParams));
    LTBleDeviceHandle ble_handle = lt_strtou32(argv[1], NULL, 16);
    SHL_iShell->Print(hShell, "Updating connection parameters for handle 0x%lX\n", LT_Pu32(ble_handle));
    if (argc > 2) {
        ble_conn_params.itvl_min = lt_strtou32(argv[2], NULL, 0);
        SHL_iShell->Print(hShell, "itvl_min: %ld\n", LT_Pu32(ble_conn_params.itvl_min));
    }
    if (argc > 3) {
        ble_conn_params.itvl_max = lt_strtou32(argv[3], NULL, 0);
        SHL_iShell->Print(hShell, "itvl_max: %ld\n", LT_Pu32(ble_conn_params.itvl_max));
    }
    if (argc > 4) {
        ble_conn_params.latency = lt_strtou32(argv[4], NULL, 0);
        SHL_iShell->Print(hShell, "latency: %ld\n", LT_Pu32(ble_conn_params.latency));
    }
    if (argc > 5) {
        ble_conn_params.supervision_timeout = lt_strtou32(argv[5], NULL, 0);
        SHL_iShell->Print(hShell, "supervision_timeout: %ld\n", LT_Pu32(ble_conn_params.supervision_timeout));
    }
    if (!iBle->UpdateConnectionParams(ble_handle, &ble_conn_params)) {
        SHL_iShell->Print(hShell, "UpdateConnectionParams failed\n");
        return 1;
    }
    SHL_iShell->Print(hShell, "Success\n");

    return 0;
}

// clang-format off
static const LTSystemShell_CommandDesc Ble_Commands[] = {
    { "help",               SHL_Help,                   "list of Ble commands",                            NULL },
    { "log",                SHL_Log,                    "Start BLE logging\n"
                                                        "\t\tstop - stop\n"
                                                       "\t\tstart - start",                               NULL },
    { "start",              SHL_Start,                  "Start BLE Controller",                            NULL },
    { "stop",               SHL_Stop,                   "Stop BLE Controller",                             NULL },
    { "adv",                SHL_Adv,                    "Advertise on BLE [start|stop]\n"
                                                       "\t\tstop - stop adv\n"
                                                       "\t\tstart - start adv",                           NULL },
    { "priv-cmd",           SHL_Cmd,                    "Set Private Vendor Command\n"
                                                       "\t\tble priv-cmd <cmd> <resp-len>",               NULL },
    { "hci-cmd",            SHL_HciCmd,                 "Set HCI Command\n"
                                                       "\t\tble hci-cmd <opcode> <cmd-param> <resp-len>", NULL },
    { "get-mac",            SHL_GetMac,                 "Get BLE Mac Address",                             NULL },
    { "get-peer-mac",       SHL_GetPeerMac,            "Get BLE Peer Mac Address",                        NULL },
    { "get-dev-name",       SHL_GetDevName,             "Get BLE Advertisement Name",                      NULL },
    { "update-params",      SHL_UpdateConnectionParams, "Update BLE Connection Parameters\n"
                                                       "\t\t\t\tble update-conn-params <ble_handle>" 
                                                       "<itvl_min> <itvl_max> <latency>" 
                                                       "<supervision_timeout>",                           NULL },
    { NULL,                 NULL,                       NULL,                                              NULL }
};
//clang-format on

static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    for (int n = 0; Ble_Commands[n].pCommand; n++) {
        SHL_iShell->Print(hShell, "  %-10s - %s\n", Ble_Commands[n].pCommand,
            Ble_Commands[n].pDescription);
    }
    return 0;
}

static int SHL_Ble(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; Ble_Commands[n].pCommand; n++) {
            if (lt_strcmp(Ble_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return Ble_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    SHL_iShell->Print(hShell, "usage: ble <command> [args]\nCommands:\n");
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "ble", SHL_Ble, "ble commands, type \"ble\" for a list", LTShell_Help },
};

static bool SHL_InitThread(void) {
    return true;
}

static void SHL_ExitThread(void) {
    return;
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

    SHL_Thread = pCore->CreateThread("ShellBleStatus");
    if (!SHL_Thread) return false;
    iThread->SetStackSize(SHL_Thread, 1536);
    iThread->Start(SHL_Thread, SHL_InitThread, SHL_ExitThread);

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));

    return true;
}

/*******************************************************************************
 * BLE Settings
 ******************************************************************************/

static LTSystemSettings *SET_Library;

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

static void LTShellBleImpl_LibFini(void) {
    SET_Quit();
    SHL_Quit();
    lt_closelibrary(iBle);       // null ok
    lt_closelibrary(iMacAddress); // null ok
}

static bool LTShellBleImpl_LibInit(void) {
    pCore = LT_GetCore();
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    iBle   = lt_openlibrary(LTDeviceBle);
    iMacAddress = lt_openlibrary(LTUtilityMacAddress);
    if (iBle && iMacAddress && SHL_Init() && SET_Init()) return true;
    LTShellBleImpl_LibFini();
    return false;
}


static int LTShellBle_Run(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    if (!SelfLib) SelfLib = LT_GetCore()->OpenLibrary("LTShellBle");
    return 0;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellBle, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellBle, LTShellBle_Run) LTLIBRARY_DEFINITION;


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Feb-24   galba       created
 
*/