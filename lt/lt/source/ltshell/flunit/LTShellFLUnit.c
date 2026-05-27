/*******************************************************************************
 * lt/source/ltshell/flunit/LTShellFLUnit.c
 *
 * FLUnit Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#define DATA_BUFFER_64_SIZE 64

DEFINE_LTLOG_SECTION("ltshell.flunit");

#define ENCRYPTION_KEY_LENGTH   16U

static struct FLShellObject {
    LTSystemShell    *shell;
    LTThread          hThread;
    LTHandle          hDevice;
    ILTShell         *iShell;
    LTUtilityByteOps *byteOps;
    ILTThread        *iThread;
    LTDeviceFLUnit   *iFLUnitDevice;
    bool              printEvent;
} S;

typedef union {
    u8 bytes[4];
    struct {
        u16 ordLight;
        u16 irLight;
    };
} LightData;

/***************** Utility Functions *********************************************************/

static void EventCallback(LTDriverFLUnitEvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(pEventData);

    if (event == kLTDriverFLUnitEvent_PirTrigger) {
        if (S.printEvent) {
            LTLOG_YELLOWALERT("flevent", "pirEvent detected");
        }
    }
}

static bool InitFLUnit(void) {
    S.iFLUnitDevice->OnPIRTrigger(&S.hDevice, EventCallback, NULL);
    return true;
}

static void DeinitFLUnit(void) {
    S.iFLUnitDevice->NoPIRTrigger(&S.hDevice, NULL);
}

/************Supported Command Functions ************************************/

static int LTShellFLUnit_SetCommand(LTShell hShell, int argc, const char *argv[]) {
    u8 buff[20] = {0};
    if (argc < 2) {
        S.iShell->Print(hShell, "Usage: flunit set <command> <data> \n");
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        S.byteOps->HexDecode(argv[i], 2, &buff[i - 1], 1);
    }
    if (!(S.iFLUnitDevice->SetCommand(&S.hDevice, buff[0], &buff[1], argc - 2))) {
        S.iShell->Print(hShell, "Command failed !\n");
        return -1;
    }
    return 0;
}

static int LTShellFLUnit_GetCommand(LTShell hShell, int argc, const char *argv[]) {
    u8  buff[20]       = {0};
    u32 expectedLength = 0;
    if (argc < 3) {
        S.iShell->Print(hShell, "Usage: flunit get <command> <expected bytes> \n");
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        S.byteOps->HexDecode(argv[i], 2, &buff[i - 1], 1);
    }
    expectedLength = buff[1];

    if (!(S.iFLUnitDevice->GetCommand(&S.hDevice, buff[0], &buff[1], expectedLength))) {
        S.iShell->Print(hShell, "Command failed %d!\n", expectedLength);
        return -1;
    }

    for (u8 i = 0; i < expectedLength; i++) {
        S.iShell->Print(hShell, "%x ", buff[i + 1]);
    }
    S.iShell->Print(hShell, "\n");
    return 0;
}

static int LTShellFLUnit_GetDeviceMacID(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    const u8 macLength = 8;
    u8 macId[macLength];
    if (!(S.iFLUnitDevice->GetMacID(&S.hDevice, macId))) {
        S.iShell->Print(hShell, "Failed to Get MacID\n");
        return -1;
    }
    for (u8 i = 0; i < macLength; i++) {
        S.iShell->Print(hShell, "%x:", macId[i]);
    }
    S.iShell->Print(hShell, "\b \n");
    return 0;
}

static int LTShellFLUnit_AuthenticateDevice(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(S.iFLUnitDevice->AuthenticateDevice(&S.hDevice))) {
        S.iShell->Print(hShell, "Authentication Failed !\n");
        return -1;
    }
    S.iShell->Print(hShell, "Authenticated!\n");
    return 0;
}

static int LTShellFLUnit_GetFirmwareVersion(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    static u32 currentFWVersion = 0;
    if (!(S.iFLUnitDevice->FirmwareVersion(&S.hDevice, &currentFWVersion))) {
        S.iShell->Print(hShell, "Failed to Get Current Firmware Version\n");
        return -1;
    }
    S.iShell->Print(hShell, "%d\n", currentFWVersion);
    return 0;
}

static int LTShellFLUnit_GetHardwareVersion(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 currentHWVersion = 0;
    if (!(S.iFLUnitDevice->HardwareVersion(&S.hDevice, &currentHWVersion))) {
        S.iShell->Print(hShell, "Failed to get Hardware Version !\n");
        return -1;
    }
    S.iShell->Print(hShell, "%d\n", currentHWVersion);
    return 0;
}

static int LTShellFLUnit_GetEncryptionKey(LTShell hShell, int argc, const char *argv[]) {
    u8  buff[20]  = {0};
    u32 keyLength = ENCRYPTION_KEY_LENGTH;
    if (argc < (int)(keyLength + 1)) {
        S.iShell->Print(hShell, "Usage: flunit getkey <random 16 bytes> \n");
        return -1;
    }
    for (u32 i = 1; i <= keyLength; i++) {
        S.byteOps->HexDecode(argv[i], 2, &buff[i - 1], 1);
    }

    if (!(S.iFLUnitDevice->GetEncryptedKey(&S.hDevice, buff))) {
        S.iShell->Print(hShell, "Command failed !\n");
        return -1;
    }

    for (u32 i = 0; i < keyLength; i++) {
        S.iShell->Print(hShell, "%x ", buff[i]);
    }
    S.iShell->Print(hShell, "\n");
    return 0;
}

static int LTShellFLUnit_GetWatchDogTime(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u16 watchDogTimer = 0;
    if (!(S.iFLUnitDevice->GetWatchdogTimer(&S.hDevice, &watchDogTimer))) {
        S.iShell->Print(hShell, "Failed to Get WatchDogTimer !\n");
        return 0;
    }
    S.iShell->Print(hShell, "%d sec\n", watchDogTimer);
    return 0;
}

static int LTShellFLUnit_SetWatchDogTime(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u16 watchdogTimer = 0;
    if (argc != 2) {
        S.iShell->Print(hShell, "Usage: flunit setwatchDogtime <watchdogtime in seconds>\n");
        return -1;
    }
    watchdogTimer = (u16)lt_strtos32(argv[1], NULL, 0);
    if (!(S.iFLUnitDevice->SetWatchdogTimer(&S.hDevice, watchdogTimer))) {
        S.iShell->Print(hShell, "Failed to Set WatchDogTime !\n");
        return -1;
    }
    return 0;
}

static int LTShellFLUnit_GetDeviceType(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u8 deviceType = 0;
    if (!(S.iFLUnitDevice->GetDeviceType(&S.hDevice, &deviceType))) {
        S.iShell->Print(hShell, "Failed to Get device ID !\n");
        return -1;
    }
    S.iShell->Print(hShell, "%d\n", deviceType);
    return 0;
}

static int LTShellFLUnit_Restart(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(S.iFLUnitDevice->SCMRestart(&S.hDevice))) {
        S.iShell->Print(hShell, "Failed to Restart\n");
        return 0;
    }
    return 0;
}

static int LTShellFLUnit_GetRestartReason(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u8 reason[DATA_BUFFER_64_SIZE] = {};
    if (!(S.iFLUnitDevice->SCMRestartReason(&S.hDevice, reason))) {
        S.iShell->Print(hShell, "Failed to Get SCMRestart Reason\n");
        return -1;
    }
    S.iShell->Print(hShell, "Reason: %s\n", reason);
    return 0;
}

static int LTShellFLUnit_Event(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell);
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    S.printEvent ^= true;
    return 0;
}

static int LTShellFLUnit_Help(LTShell hShell, int argc, const char *argv[]);

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static const LTSystemShell_CommandDesc FLUnitCommands[] = {
    { "help"         , LTShellFLUnit_Help              , "list supported commands"            , NULL },
    { "set"          , LTShellFLUnit_SetCommand        , "set <command> <params>"             , NULL },
    { "get"          , LTShellFLUnit_GetCommand        , "get <command> <params>"             , NULL },
    { "getmacid"     , LTShellFLUnit_GetDeviceMacID    , "get device Mac ID"                  , NULL },
    { "authenticate" , LTShellFLUnit_AuthenticateDevice, "authenticate the device"            , NULL },
    { "fwversion"    , LTShellFLUnit_GetFirmwareVersion, "get current firmware version"       , NULL },
    { "hwversion"    , LTShellFLUnit_GetHardwareVersion, "get current hardware version"       , NULL },
    { "getkey"       , LTShellFLUnit_GetEncryptionKey  , "get encryption key"                 , NULL },
    { "getwdt"       , LTShellFLUnit_GetWatchDogTime   , "get watchdog time"                  , NULL },
    { "setwdt"       , LTShellFLUnit_SetWatchDogTime   , "set watchdog time"                  , NULL },
    { "getdevtype"   , LTShellFLUnit_GetDeviceType     , "check device type"                  , NULL },
    { "restart"      , LTShellFLUnit_Restart           , "device restart"                     , NULL },
    { "restartreason", LTShellFLUnit_GetRestartReason  , "get device restart reason"          , NULL },
    { "eventlogs"    , LTShellFLUnit_Event             , "get trigger event [PIR/Photosensor]", NULL },
    { }
};

/*******************************************************************************
 * Shell Commands Help Function
 ******************************************************************************/

static int LTShellFLUnit_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);
    for (int n = 0; FLUnitCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %20s\t - %s\n", FLUnitCommands[n].pCommand, FLUnitCommands[n].pDescription);
    }
    if (argc < 2) {
        return 0;
    } else {
        return -1;
    }
}

static int LTShellFLUnit_Handler(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc < 2) {
        return FLUnitCommands[cmd].pCommandProc(hShell, argc - 1, argv + 1);
    }
    for (int n = 0; FLUnitCommands[n].pCommand; n++) {
        if (0 == lt_strcasecmp(FLUnitCommands[n].pCommand, argv[1])) {
            cmd = n;
            break;
        }
    }
    return FLUnitCommands[cmd].pCommandProc(hShell, argc - 1, argv + 1);
}

static void LTShell_Help(LTShell hShell, int argc, const char **argv) {
    /* It prints usage and calls the regular LTShellFLUnit_Help */
    S.iShell->Print(hShell, "usage: flunit <command> [args]\nCommands:\n");
    (void)LTShellFLUnit_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellFLUnit_Commands[] = {
    { "flunit", LTShellFLUnit_Handler, "manage and obtain status of FLUnit Device", LTShell_Help }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellFLUnitImpl_LibFini(void) {
    if (S.iThread != NULL) {
        if (S.hThread != 0) {
            S.iThread->Terminate(S.hThread);
            S.iThread->WaitUntilFinished(S.hThread, LTTime_Milliseconds(2000));
            S.iThread->Destroy(S.hThread);
            S.hThread = 0;
        }
    }
    LT_GetCore()->Destroy(S.hDevice);
    S.hDevice = 0;
    lt_closelibrary(S.iFLUnitDevice);
    S.iFLUnitDevice = NULL;
    lt_closelibrary(S.byteOps);
    S.byteOps = NULL;
    if (S.shell) {
        S.shell->UnregisterCommands(LTShellFLUnit_Commands);
        lt_closelibrary(S.shell);
        S.shell = NULL;
    }
}

static bool LTShellFLUnitImpl_LibInit(void) {
    S.iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    do {
        if (!(S.byteOps = lt_openlibrary(LTUtilityByteOps))) {
            LTLOG_YELLOWALERT("init.fail", "cannot open LTUtilityByteOps");
            break;
        }

        if (!(S.shell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("init.fail", "cannot open LTSystemShell");
            break;
        }
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);
        S.iFLUnitDevice = lt_openlibrary(LTDeviceFLUnit);
        if (!S.iFLUnitDevice) return false;
        S.hDevice = S.iFLUnitDevice->CreateDeviceUnitHandle(0);
        S.iFLUnitDevice->AuthenticateDevice(&S.hDevice);

        // Arbitrary values to initialize, as sensitivity defaults to zero.
        u8 data[3] = {250, 0x80, 0};  //Sensitivity (0-255), PIR Zone (0x20|0x40|0x80), PIR Algo (0/1)
        S.iFLUnitDevice->SetCommand(&S.hDevice, LT_FL_CMD_SET_PIR_PARAMETER, data, 3);

        S.hThread = LT_GetCore()->CreateThread("ShellFLUnit");
        S.iThread->SetStackSize(S.hThread, 2048);
        S.iThread->Start(S.hThread, InitFLUnit, DeinitFLUnit);

        S.shell->RegisterCommands(LTShellFLUnit_Commands,
                                    sizeof(LTShellFLUnit_Commands) / sizeof(LTShellFLUnit_Commands[0]));
        return true;
    } while (0);
    LTShellFLUnitImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellFLUnit, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellFLUnit) LTLIBRARY_DEFINITION;
