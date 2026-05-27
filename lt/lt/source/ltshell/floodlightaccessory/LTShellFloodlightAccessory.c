/*******************************************************************************
 * lt/source/ltshell/floodlightaccessory/LTShellFloodlightAccessory.c
 *
 * FloodlightAccessory Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/floodlightaccessory/LTDeviceFloodlightAccessory.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.floodlightaccessory");

enum {
    kSettingBufferLength       = 1,
    kFwVersionBufferLength     = 8,
    kSystemSettingBufferLength = 0,
};

static struct Statics {
    LTSystemShell    *shell;
    LTThread          hThread;
    LTHandle          hDevice;
    ILTShell         *iShell;
    ILTThread        *iThread;
    bool              printEvent;
    LTDeviceFloodlightAccessory *iFloodlightAccessory;
} S;

typedef struct {
    const char *command;
    LTDeviceFloodlightAccessory_Endpoint endpoint;
    LTDeviceFloodlightAccessory_Setting  setting;
} CommandMap;

static const CommandMap kCommandMap[] = {
    {"sensitivity",    kLTDeviceFloodlightAccessory_Endpoint_PIR,         kLTDeviceFloodlightAccessory_Setting_Sensitivity   },
    {"zones",          kLTDeviceFloodlightAccessory_Endpoint_PIR,         kLTDeviceFloodlightAccessory_Setting_Zones         },
    {"cooldowntime",   kLTDeviceFloodlightAccessory_Endpoint_PIR,         kLTDeviceFloodlightAccessory_Setting_CooldownTime  },
    {"brightness",     kLTDeviceFloodlightAccessory_Endpoint_Floodlight,  kLTDeviceFloodlightAccessory_Setting_Brightness    },
    {"fadetime",       kLTDeviceFloodlightAccessory_Endpoint_Floodlight,  kLTDeviceFloodlightAccessory_Setting_FadeTime      },
    {"flashfrequency", kLTDeviceFloodlightAccessory_Endpoint_Floodlight,  kLTDeviceFloodlightAccessory_Setting_FlashFrequency},
    {NULL, 0, 0}
};

static const char *kDeviceBootReason[] = {"Hard_Reset", "Soft_Reset", "Watchdog_Reset", "Brown_Out", "Unknown"};

/***************** Utility Functions *********************************************************/

static void EventCallback(LTDeviceFloodlightAccessory_Event event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(pEventData);
    switch (event) {
        case kLTDeviceFloodlightAccessory_Event_PirTrigger:
            if (S.printEvent) LTLOG("evt.motion", "Pir event detected");
            break;
        case kLTDeviceFloodlightAccessory_Event_Overheated:
            if (S.printEvent) LTLOG("evt.overheat", "Floodlight overheated");
            break;
        case kLTDeviceFloodlightAccessory_Event_PhotoSensorTrigger:
            if (S.printEvent) LTLOG("evt.brightness", "Floodlight brightness changed");
            break;
        default:
            break;
    }
}

static bool InitFloodlightAccessory(void) {
    S.iFloodlightAccessory->API->OnPIRTrigger(EventCallback, NULL);
    S.iFloodlightAccessory->API->OnFloodlightOverheatedState(EventCallback, NULL);
    S.iFloodlightAccessory->API->OnFloodlightBrightnessChange(EventCallback, NULL);
    return true;
}

static void DeinitFloodlightAccessory(void) {
    S.iFloodlightAccessory->API->NoPIRTrigger(EventCallback);
    S.iFloodlightAccessory->API->NoFloodlightOverheatedState(EventCallback);
    S.iFloodlightAccessory->API->NoFloodlightBrightnessChange(EventCallback);
}

static bool SettingCheck(const char *arg, LTDeviceFloodlightAccessory_Endpoint *endpoint, LTDeviceFloodlightAccessory_Setting *setting) {
    for (u8 i = 0; kCommandMap[i].command != NULL; ++i) {
        if (lt_strncmp(arg, kCommandMap[i].command, lt_strlen(arg)) == 0 &&
            lt_strlen(arg) == lt_strlen(kCommandMap[i].command)) {
            *endpoint = kCommandMap[i].endpoint;
            *setting = kCommandMap[i].setting;
            return true;
        }
    }
    return false;
}

/************Supported Command Functions ************************************/
static int LTShellFloodlightAccessory_SetCommand(LTShell hShell, int argc, const char *argv[]) {
    LTDeviceFloodlightAccessory_Setting setting;
    LTDeviceFloodlightAccessory_Endpoint endpoint;
    if (argc < 3) {
        S.iShell->Print(hShell, "Usage: accessory set <setting> <data> \n\t\tset setting[sensitivity/zone/cooldowntime] data\n\t\tset setting[brightness/flashfrequency/fadetime] data\n");
        return -1;
    }
    if (SettingCheck(argv[1], &endpoint, &setting)) {
        u8 data = lt_strtou32(argv[2], NULL, 0);
        if (!(S.iFloodlightAccessory->API->SetCommand(endpoint, setting, &data, kSettingBufferLength))) {
            S.iShell->Print(hShell, "Command failed !\n");
            return -1;
        }
    } else {
        S.iShell->Print(hShell, "Invalid Setting: %s \n", argv[1]);
        return -1;
    }
    return 0;
}

static int LTShellFloodlightAccessory_GetCommand(LTShell hShell, int argc, const char *argv[]) {
    LTDeviceFloodlightAccessory_Setting setting;
    LTDeviceFloodlightAccessory_Endpoint endpoint;
    if (argc < 2) {
        S.iShell->Print(hShell, "Usage: accessory get <setting>\n");
        S.iShell->Print(hShell, "Usage: accessory get <setting>\n\t\tget setting[sensitivity/zone/cooldowntime]\n\t\tget setting[brightness/flashfrequency/fadetime]\n");
        return -1;
    }

    if (SettingCheck(argv[1], &endpoint, &setting)) {
        u8 data;
        if (!(S.iFloodlightAccessory->API->GetCommand(endpoint, setting, &data, kSettingBufferLength))) {
            S.iShell->Print(hShell, "Command failed !\n");
            return -1;
        }
        S.iShell->Print(hShell, "%d ", data);
    } else {
        S.iShell->Print(hShell, "Invalid Setting: %s \n", argv[1]);
        return -1;
    }
    return 0;
}

static int LTShellFloodlightAccessory_AuthenticateDevice(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(S.iFloodlightAccessory->API->SetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_Authenticate, NULL, kSystemSettingBufferLength))) {
        S.iShell->Print(hShell, "Authentication Failed !\n");
        return -1;
    }
    S.iShell->Print(hShell, "Authenticated!\n");
    return 0;
}

static int LTShellFloodlightAccessory_GetFirmwareVersion(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u8 data[kFwVersionBufferLength] = {};
    if (!(S.iFloodlightAccessory->API->GetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_FwVersion, data, kFwVersionBufferLength))) {
        S.iShell->Print(hShell, "Failed to Get Current Firmware Version\n");
        return -1;
    }
    S.iShell->Print(hShell, "Firmware version %s\n", data);
    return 0;
}

static int LTShellFloodlightAccessory_Restart(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(S.iFloodlightAccessory->API->SetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_Restart, NULL, kSystemSettingBufferLength))) {
        S.iShell->Print(hShell, "Failed to Restart\n");
        return -1;
    }
    return 0;
}

static int LTShellFloodlightAccessory_GetBootReason(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u8 reason;
    if (!(S.iFloodlightAccessory->API->GetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_BootReason, &reason, kSettingBufferLength))) {
        S.iShell->Print(hShell, "Failed to Get Boot Reason\n");
        return -1;
    }
    S.iShell->Print(hShell, "Boot reason: %s\n", kDeviceBootReason[reason - 1]);
    return 0;
}

static int LTShellFloodlightAccessory_Event(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell);
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    S.printEvent ^= true;
    return 0;
}

static int LTShellFloodlightAccessory_Help(LTShell hShell, int argc, const char *argv[]);

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static const LTSystemShell_CommandDesc FloodlightAccessoryCommands[] = {
    { "help"         , LTShellFloodlightAccessory_Help              , "list supported commands"                                    , NULL },
    { "set"          , LTShellFloodlightAccessory_SetCommand        , "set <setting> <data>\n"
                                                                      "\t\tset setting[sensitivity/zones/cooldowntime] data\n"
                                                                      "\t\tset setting[brightness/flashfrequency/fadetime] data"   , NULL },
    { "get"          , LTShellFloodlightAccessory_GetCommand        , "get <setting>\n"
                                                                      "\t\tget setting[sensitivity/zones/cooldowntime]\n"
                                                                      "\t\tget setting[brightness/flashfrequency/fadetime]"        , NULL },
    { "eventlogs"    , LTShellFloodlightAccessory_Event             , "log events from endpoint PIR & floodlight"                  , NULL },
    { "authenticate" , LTShellFloodlightAccessory_AuthenticateDevice, "authenticate the device"                                    , NULL },
    { "fwversion",     LTShellFloodlightAccessory_GetFirmwareVersion, "get current firmware version"                               , NULL },
    { "bootreason",    LTShellFloodlightAccessory_GetBootReason,      "get accessory boot Reason"                                   , NULL },
    { "restart"      , LTShellFloodlightAccessory_Restart,            "accessory restart"                                          , NULL },
    { }
};

/*******************************************************************************
 * Shell Commands Help Function
 ******************************************************************************/
static int LTShellFloodlightAccessory_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);
    for (int n = 0; FloodlightAccessoryCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, " %-12s - %s\n", FloodlightAccessoryCommands[n].pCommand, FloodlightAccessoryCommands[n].pDescription);
    }
    return argc < 2 ? 0 : -1;
}

static int LTShellFloodlightAccessory_Handler(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc < 2) {
        return FloodlightAccessoryCommands[cmd].pCommandProc(hShell, argc - 1, argv + 1);
    }
    for (int n = 0; FloodlightAccessoryCommands[n].pCommand; n++) {
        if (0 == lt_strcasecmp(FloodlightAccessoryCommands[n].pCommand, argv[1])) {
            cmd = n;
            break;
        }
    }
    return FloodlightAccessoryCommands[cmd].pCommandProc(hShell, argc - 1, argv + 1);
}

static void LTShell_Help(LTShell hShell, int argc, const char **argv) {
    /* It prints usage and calls the regular LTShellFloodlightAccessory_Help */
    S.iShell->Print(hShell, "usage: accessory <command> [args]\nCommands:\n");
    (void)LTShellFloodlightAccessory_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellFloodlightAccessory_Commands[] = {
    { "accessory", LTShellFloodlightAccessory_Handler, "manage and obtain status of accessory", LTShell_Help }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellFloodlightAccessoryImpl_LibFini(void) {
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
    lt_destroyobject(S.iFloodlightAccessory);
    S.iFloodlightAccessory = NULL;
    if (S.shell) {
        S.shell->UnregisterCommands(LTShellFloodlightAccessory_Commands);
        lt_closelibrary(S.shell);
        S.shell = NULL;
    }
}

static bool LTShellFloodlightAccessoryImpl_LibInit(void) {
    do {
        if (!(S.iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))) {
            LTLOG_YELLOWALERT("f.lib.thread", "Failed to get iface thread");
            break;
        }
        if (!(S.shell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("init.fail", "cannot open LTSystemShell");
            break;
        }
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);
        S.iFloodlightAccessory = lt_createobject(LTDeviceFloodlightAccessory);
        if (!S.iFloodlightAccessory) break;
        S.iFloodlightAccessory->API->SetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_Authenticate, 0, 0);

        S.hThread = LT_GetCore()->CreateThread("ShellFloodlightAccessory");
        S.iThread->SetStackSize(S.hThread, 2048);
        S.iThread->Start(S.hThread, InitFloodlightAccessory, DeinitFloodlightAccessory);

        S.shell->RegisterCommands(LTShellFloodlightAccessory_Commands,
                                    sizeof(LTShellFloodlightAccessory_Commands) / sizeof(LTShellFloodlightAccessory_Commands[0]));
        return true;
    } while (0);
    LTShellFloodlightAccessoryImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellFloodlightAccessory, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellFloodlightAccessory) LTLIBRARY_DEFINITION;
