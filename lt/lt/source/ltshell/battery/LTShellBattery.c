/*******************************************************************************
 *
 * LTShellBattery: Battery Shell
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
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/battery/LTDeviceBattery.h>

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellBattery, (LTSystemShell) (LTDeviceBattery));

/** Standard LT Interfaces ****************************************************/
static struct Statics {
    ILTShell         *SHL_iShell;
    LTBatterySystem             *bms;
} S;

/** Forward references ********************************************************/
static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static void PrintBatterySystemInfo(LTShell hShell, LTDeviceBatteryInfo *info) {
    S.SHL_iShell->Print(hShell, "      Status: ");
    switch(info->status) {
        case kLTDeviceBatteryStatus_Unknown:
            S.SHL_iShell->Print(hShell, "Unknown\n");
            break;
        case kLTDeviceBatteryStatus_Charging:
            S.SHL_iShell->Print(hShell, "Charging\n");
            break;
        case kLTDeviceBatteryStatus_Discharging:
            S.SHL_iShell->Print(hShell, "Discharging\n");
            break;
        case kLTDeviceBatteryStatus_NotPresent:
            S.SHL_iShell->Print(hShell, "Not Charging\n");
            break;
        case kLTDeviceBatteryStatus_Idle:
            S.SHL_iShell->Print(hShell, "Idle\n");
            break;
        default:
            S.SHL_iShell->Print(hShell, "Invalid\n");
            break;
    }

    S.SHL_iShell->Print(hShell, "      Design Capacity: %ldmA\n", LT_Pu32(info->battery.designCapacity));
    S.SHL_iShell->Print(hShell, "      Battery State of Charge: %ld%c\n", LT_Pu32(info->battery.stateOfCharge), '%');
    S.SHL_iShell->Print(hShell, "      Battery Voltage: %ldmV\n", LT_Pu32(info->battery.voltage));
    S.SHL_iShell->Print(hShell, "      Battery Current: %ldmA\n", LT_Pu32(info->battery.current));
    S.SHL_iShell->Print(hShell, "      Battery Temperature: %ld.%ldC\n", LT_Pu32(info->battery.temperature/10), LT_Pu32(info->battery.temperature%10));
    S.SHL_iShell->Print(hShell, "      Charger Battery Voltage: %ldmV\n", LT_Pu32(info->charger.batteryVoltage));
    S.SHL_iShell->Print(hShell, "      Charger Battery Current: %ldmA\n", LT_Pu32(info->charger.batteryCurrent));
    S.SHL_iShell->Print(hShell, "      Charger Adaptor Voltage: %ldmV\n", LT_Pu32(info->charger.adaptorVoltage));
    S.SHL_iShell->Print(hShell, "      Charger Adaptor Current: %ldmA\n", LT_Pu32(info->charger.adaptorCurrent));
    S.SHL_iShell->Print(hShell, "      Charger System Voltage: %ldmV\n", LT_Pu32(info->charger.systemVoltage));
    S.SHL_iShell->Print(hShell, "      Charger Temperature: %ld.%ldC\n", LT_Pu32(info->charger.temperature/10), LT_Pu32(info->charger.temperature%10));
    S.SHL_iShell->Print(hShell, "      Charger Input Current Limit: %lumA\n", LT_Pu32(info->charger.inputCurrentLimit));
}

static void BatteryStatusEventHandler(LTDeviceBatteryInfo info, void *pClientData) {
    LTShell hShell = (LTShell)VOIDPTR_TO_LTHANDLE(pClientData);
    S.SHL_iShell->Print(hShell, "Battery Status Changed\n");
    PrintBatterySystemInfo(hShell, &info);
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static int SHL_BatteryOpen(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    S.bms = LT_GetLTDeviceBattery()->GetBatterySystemByName("battery0");
    if (!S.bms) {
        S.SHL_iShell->Print(hShell, "Failed to create battery device unit handle\n");
        return -1;
    }
    return 0;
}

static int SHL_BatteryListen(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }
    S.bms->API->OnStatusChangeCallback(S.bms, BatteryStatusEventHandler, LTHANDLE_TO_VOIDPTR(hShell));
    return 0;
}

static int SHL_BatteryInfo(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);

    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    LTDeviceBatteryInfo info;
    if (!S.bms->API->GetInfo(S.bms, &info)) {
        S.SHL_iShell->Print(hShell, "Failed to get battery info\n");
        return -1;
    }

    PrintBatterySystemInfo(hShell, &info);
    return 0;
}

static int SHL_BatteryEnableCharging(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        S.SHL_iShell->Print(hShell, "usage: battery enable_charge <0|1>\n");
        return -1;
    }

    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    bool enableCharging = lt_strtou32(argv[1], NULL, 10);
    if (!S.bms->API->EnableCharging(S.bms, enableCharging)) {
        S.SHL_iShell->Print(hShell, "Failed to %s battery charging\n", enableCharging ? "enable" : "disable");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "%s battery charging\n", enableCharging ? "Enabled" : "Disabled");
    }
    return 0;
}

static int SHL_BatteryDisableInputPower(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        S.SHL_iShell->Print(hShell, "usage: battery disable_input_power <0|1>\n");
        return -1;
    }

    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    bool disableInputPower = lt_strtou32(argv[1], NULL, 10);
    if (!S.bms->API->DisableInputPower(S.bms, disableInputPower)) {
        S.SHL_iShell->Print(hShell, "Failed to %s input power\n", disableInputPower ? "disable" : "enable");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "%s input power\n", disableInputPower ? "Disabled" : "Enabled");
    }
    return 0;
}

static int SHL_BatterySetInputCurrentLimit(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        S.SHL_iShell->Print(hShell, "usage: battery set_input_current <current_mA>\n");
        S.SHL_iShell->Print(hShell, "       current_mA: 100-3200mA in 20mA steps\n");
        return -1;
    }

    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    u32 currentLimitMa = lt_strtou32(argv[1], NULL, 10);
    if (!S.bms->API->SetInputCurrentLimit(S.bms, (u16)currentLimitMa)) {
        S.SHL_iShell->Print(hShell, "Failed to set input current limit to %ldmA\n", LT_Pu32(currentLimitMa));
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "Set input current limit to %ldmA\n", LT_Pu32(currentLimitMa));
    }
    return 0;
}

static int SHL_BatterySetOperatingMode(LTShell hShell, int argc, const char *argv[]) {
    u32 modeArg;

    if (argc < 2) {
        S.SHL_iShell->Print(hShell, "usage: battery setbatterymode <mode 0 = Normal, 1= Shipping, 2 = Shutdown>\n");
        return -1;
    }

    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    modeArg = lt_strtou32(argv[1], NULL, 10);

    switch (modeArg){
        case 0:
            if (S.bms->API->SetOperatingMode(S.bms, kLTDeviceBattery_OperatingMode_Normal)) {
                S.SHL_iShell->Print(hShell, "Set Battery Mode to Normal\n");
            } else {
                S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Normal\n");
                return -1;
            }
            break;
        case 1:
            if (S.bms->API->SetOperatingMode(S.bms, kLTDeviceBattery_OperatingMode_Shipping)) {
                S.SHL_iShell->Print(hShell, "Set Battery Mode to Shipping\n");
            } else {
                S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Shipping\n");
                return -1;
            }
            break;
        case 2:
            if (S.bms->API->SetOperatingMode(S.bms, kLTDeviceBattery_OperatingMode_Shutdown)) {
                S.SHL_iShell->Print(hShell, "Set Battery Mode to Shutdown\n");
            } else {
                S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Shutdown\n");
                return -1;
            }
            break;
        default:
            break;
    }
    return 0;
}

static int SHL_BatteryModeShutdown(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    if (!S.bms->API->SetProperty(S.bms, "shutdown_battery", NULL)) {
        S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Shutdown\n");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "Set Battery Mode to Shutdown\n");
    }
    return 0;
}

static int SHL_BatteryModeShipping(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    if (!S.bms->API->SetProperty(S.bms, "ship_battery", NULL)) {
        S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Shipping\n");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "Set Battery Mode to Shipping\n");
    }
    return 0;
}

static int SHL_BatteryModeReset(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    if (!S.bms->API->SetProperty(S.bms, "reset_battery", NULL)) {
        S.SHL_iShell->Print(hShell, "Failed to set Battery Mode to Normal\n");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "Set Battery Mode to Normal\n");
    }
    return 0;
}

static int SHL_BatteryGetManufacturer(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    LTString manufacturer = ltstring_create("");
    if (!S.bms->API->GetProperty(S.bms, "Manufacturer", &manufacturer)) {
        S.SHL_iShell->Print(hShell, "Failed to get battery manufacturer\n");
        return -1;
    } else {
        S.SHL_iShell->Print(hShell, "Battery manufacturer is \"%s\"\n", manufacturer);
    }
    return 0;
}

static int SHL_BatteryDebugSoC(LTShell hShell, int argc, const char *argv[]) {
    if (!S.bms) {
        if (SHL_BatteryOpen(hShell, 0, NULL) != 0) {
            S.SHL_iShell->Print(hShell, "Cannot open Battery device unit\n");
            return -1;
        }
    }

    u32 soc = 0xFFFFFFFF;
    if (argc >= 2) {
        soc = lt_strtou32(argv[1], NULL, 10);
    }

    if (!S.bms->API->SetProperty(S.bms, "debug_soc", (void *)soc)) {
        S.SHL_iShell->Print(hShell, "Failed to set battery state of charge\n");
        return -1;
    }
    return 0;
}

static const LTSystemShell_CommandDesc Battery_Commands[] = {
    { "help",                    SHL_Help,                        "list of battery commands",                      NULL},
    { "open",                    SHL_BatteryOpen,                 "Opens the battery system",                      NULL},
    { "listen",                  SHL_BatteryListen,               "Listen for battery status changes",             NULL},
    { "info",                    SHL_BatteryInfo,                 "Get Battery system info",                       NULL},
    { "enable_charge",           SHL_BatteryEnableCharging,       "Enable/Disable battery charging",               NULL},
    { "setmode",                 SHL_BatterySetOperatingMode,     "Set battery operating mode",                    NULL},
    { "shutdown_battery",        SHL_BatteryModeShutdown,         "Put battery pack into shutdown mode",           NULL},
    { "ship_battery",            SHL_BatteryModeShipping,         "Put battery pack into shipping mode",           NULL},
    { "reset_battery",           SHL_BatteryModeReset,            "Reset battery pack to normal mode",             NULL},
    { "manufacturer",            SHL_BatteryGetManufacturer,      "Get battery pack manufacturer",                 NULL},
    { "disable_input_power",     SHL_BatteryDisableInputPower,    "Enable/Disable input power",                    NULL},
    { "set_input_current_limit", SHL_BatterySetInputCurrentLimit, "Set input current limit",                       NULL},
    { "setsoc",                  SHL_BatteryDebugSoC,             "Debug: override battery SoC value",             NULL},
    { NULL,          NULL,            NULL,                         NULL}
};

static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    S.SHL_iShell->Print(hShell, "usage: battery <command> [args]\nCommands:\n");
    for (int n = 0; Battery_Commands[n].pCommand; n++) {
        S.SHL_iShell->Print(hShell, "  %-10s - %s\n", Battery_Commands[n].pCommand,
            Battery_Commands[n].pDescription);
    }
    S.SHL_iShell->Print(hShell,
        "Examples:\n"
        "  battery setmode 1\n"
        "  battery enable_charge 0\n"
    );
    return 0;
}

static int SHL_Battery(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; Battery_Commands[n].pCommand; n++) {
            if (lt_strcmp(Battery_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return Battery_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "bms",        SHL_Battery,  "Battery Management System commands", LTShell_Help}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellBatteryImpl_LibFini(void) {
    if (S.bms) {
        S.bms->API->NoStatusChangeCallback(S.bms, BatteryStatusEventHandler);
        lt_destroyobject(S.bms);
        S.bms = NULL;
    }
    LT_GetLTSystemShell()->UnregisterCommands(SHL_Commands);
}

static bool LTShellBatteryImpl_LibInit(void) {
    S.SHL_iShell= lt_getlibraryinterface(ILTShell, LT_GetLTSystemShell());
    LT_GetLTSystemShell()->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellBattery, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellBattery) LTLIBRARY_DEFINITION;
