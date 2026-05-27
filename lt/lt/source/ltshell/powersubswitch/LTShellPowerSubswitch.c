/*******************************************************************************
 *
 * LTShellPowerSubswitch: Shell handler for LTDevicePowerSubswitch library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/powersubswitch/LTDevicePowerSubswitch.h>

DEFINE_LTLOG_SECTION("ltshell.pwrsubswitch");


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

/* Static references to LT libraries and interfaces, opened for long periods of time */
static struct Statics {
    LTCore *core;
    LTSystemShell *shell;
    ILTShell *iShell;
    LTArray *powerSubswitches;
} S;

typedef struct {
    LTString switchName;
    LTDevicePowerSubswitch *powerSubswitch;
} PowerSubswitchRecord;


/*******************************************************************************
 * Shell Interface
 ******************************************************************************/

static PowerSubswitchRecord *GetSubswitch(const char *switchName, bool create) {
    PowerSubswitchRecord *record = NULL;

    for (u32 n = 0; n < S.powerSubswitches->API->GetCount(S.powerSubswitches); n++) {
        record = (PowerSubswitchRecord *)S.powerSubswitches->API->Get(S.powerSubswitches, n, NULL);
        if (lt_strcmp(record->switchName, switchName) == 0) {
            return record;
        } else {
            record = NULL;
        }
    }

    // Not found an existing subswitch record
    if (create) {
        record = (PowerSubswitchRecord *)lt_malloc(sizeof(PowerSubswitchRecord));
        if (!record) return NULL;

        record->powerSubswitch = lt_createobject(LTDevicePowerSubswitch);
        if (record->powerSubswitch) {
            record->powerSubswitch->API->SetUnitName(record->powerSubswitch, switchName);
            record->switchName = ltstring_create(switchName);
            S.powerSubswitches->API->Append(S.powerSubswitches, record);
            return record;
        } else {
            LTLOG("create.fail", "Failed to create PowerSubswitch");
            lt_free(record);
            return NULL;
        }
    }

    // Couldn't find or create the subswitch record
    return NULL;
}

static int LTShellPowerSubSwitch_List(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    if (argc != 1) {
        S.iShell->Print(hShell, "Usage: power list\n");
        return -1;
    }

    if (!S.powerSubswitches->API->GetCount(S.powerSubswitches)) {
        S.iShell->Print(hShell, "No known power switches\n");
        return 0;
    }

    S.iShell->Print(hShell, "Device | Switch | Name\n");
    S.iShell->Print(hShell, "-------+--------+----------------\n");
    for (u32 n = 0; n < S.powerSubswitches->API->GetCount(S.powerSubswitches); n++) {
        PowerSubswitchRecord *record = (PowerSubswitchRecord *)S.powerSubswitches->API->Get(S.powerSubswitches, n, NULL);
        S.iShell->Print(hShell, "%s | %s | %s\n",
            record->powerSubswitch->API->IsDevicePoweredOn(record->powerSubswitch) ? "On    " : "Off   ",
            record->powerSubswitch->API->IsSwitchOn(record->powerSubswitch) ? "On    " : "Off   ",
            record->switchName);
    }
    return 0;
}

static int LTShellPowerSubSwitch_On(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "power on <switch_name>\n");
        return -1;
    }

    // Check if we already know about the named switch, and get an instance of it if required
    PowerSubswitchRecord *record = GetSubswitch(argv[1], true);
    if (!record) {
        S.iShell->Print(hShell, "Failed to create %s PowerSubswitch\n", argv[1]);
        return -1;
    }

    // Turn on the power switch instance
    record->powerSubswitch->API->SwitchOn(record->powerSubswitch);
    S.iShell->Print(hShell, "Power switch %s turned on\n", argv[1]);
    return 0;
}

static int LTShellPowerSubSwitch_Off(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "power off <switch_name>\n");
        return -1;
    }

    // Check if we already know about the named switch, and get an instance of it if required
    PowerSubswitchRecord *record = GetSubswitch(argv[1], true);
    if (!record) {
        S.iShell->Print(hShell, "Failed to create %s PowerSubswitch\n", argv[1]);
        return -1;
    }

    // Turn off the power switch instance
    record->powerSubswitch->API->SwitchOff(record->powerSubswitch);
    S.iShell->Print(hShell, "Power switch %s turned off\n", argv[1]);
    return 0;
}

static int LTShellPowerSubSwitch_AllOff(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "power off <switch_name>\n");
        return -1;
    }

    // Check if we already know about the named switch, don't create it it hasn't previously been controlled from the shell
    PowerSubswitchRecord *record = GetSubswitch(argv[1], false);
    if (!record) {
        S.iShell->Print(hShell, "Failed to find %s PowerSubswitch (must have previously attached with 'on' or 'off' commands)\n", argv[1]);
        return -1;
    }

    // Turn off the power switch instance
    record->powerSubswitch->API->TurnAllSwitchesOff(record->powerSubswitch);
    S.iShell->Print(hShell, "Power switch %s forced off entirely\n", argv[1]);
    return 0;
}

static int LTShellPowerSubSwitch_Forget(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "power forget <switch_name>\n");
        return -1;
    }

    // Find the power switch instance and destroy it (which will also switch it off)
    PowerSubswitchRecord *record = NULL;
    for (u32 n = 0; n < S.powerSubswitches->API->GetCount(S.powerSubswitches); n++) {
        record = (PowerSubswitchRecord *)S.powerSubswitches->API->Get(S.powerSubswitches, n, NULL);
        if (lt_strcmp(record->switchName, argv[1]) == 0) {
            S.powerSubswitches->API->Remove(S.powerSubswitches, n);
            lt_destroyobject(record->powerSubswitch);
            lt_free(record->switchName);
            lt_free(record);
            S.iShell->Print(hShell, "Power switch %s forgotten\n", argv[1]);
            return 0;
        }
    }

    S.iShell->Print(hShell, "Unknown power switch: %s\n", argv[1]);
    return -1;
}

/* Forward-decl for PowerSubSwitchCommands array due to circular references */
static int LTShellPowerSubSwitch_Help(LTShell hShell, int argc, const char *argv[]);

static const LTSystemShell_CommandDesc PowerSubSwitchCommands[] = {
    { "help",   LTShellPowerSubSwitch_Help,   "- list supported sub-commands", NULL },
    { "list",   LTShellPowerSubSwitch_List,   "- list state of known power switches", NULL },
    { "on",     LTShellPowerSubSwitch_On,     "- turn on a named power switch", NULL },
    { "off",    LTShellPowerSubSwitch_Off,    "- turn off a named power switch", NULL },
    { "alloff", LTShellPowerSubSwitch_AllOff, "- force off all instances of a named power switch", NULL },
    { "forget", LTShellPowerSubSwitch_Forget, "- power off and forget a named power switch", NULL },
    { }
};

static int LTShellPowerSubSwitch_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    for (int n = 0; PowerSubSwitchCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", PowerSubSwitchCommands[n].pCommand, PowerSubSwitchCommands[n].pDescription);
    }
    if (argc < 2) {
        return 0;
    } else {
        return -1;
    }
}

static int LTShellPowerSubSwitch_Handler(LTShell hShell, int argc, const char *argv[]) {
    if (argc > 1) {
        for (int n = 0; PowerSubSwitchCommands[n].pCommand; n++) {
            if (0 == lt_strcasecmp(PowerSubSwitchCommands[n].pCommand, argv[1])) {
                return PowerSubSwitchCommands[n].pCommandProc(hShell, argc-1, argv+1);
            }
        }
    }

    // Command not found so just display help
    return LTShellPowerSubSwitch_Help(hShell, 0, NULL);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help power" works in addition to "power help".
    * It prints usage and calls the regular LTShellPowerSubSwitch_Help
    */
    S.iShell->Print(hShell, "usage: power <command> [args]\nCommands:\n");
    (void)LTShellPowerSubSwitch_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellPowerSubSwitch_Commands[] = {
    { "power", LTShellPowerSubSwitch_Handler, "control LTDevicePowerSubSwitch subsystem power switches", LTShell_Help },
};


/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellPowerSubSwitchImpl_LibFini(void) {
    if (S.powerSubswitches) {
        for (u32 n = 0; n < S.powerSubswitches->API->GetCount(S.powerSubswitches); n++) {
            PowerSubswitchRecord *record = (PowerSubswitchRecord *)S.powerSubswitches->API->Get(S.powerSubswitches, n, NULL);
            lt_destroyobject(record->powerSubswitch);
            lt_free(record->switchName);
            lt_free(record);
        }
        lt_destroyobject(S.powerSubswitches);
        S.powerSubswitches = NULL;
    }

    if (S.shell) {
        S.shell->UnregisterCommands(LTShellPowerSubSwitch_Commands);
        lt_closelibrary(S.shell);
    }

    S = (struct Statics) {};
}

static bool LTShellPowerSubSwitchImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore(),
    };

    const char *reason;
    do {
        reason = "no shell";
        if (!(S.shell = lt_openlibrary(LTSystemShell))) break;
        S.shell->RegisterCommands(LTShellPowerSubSwitch_Commands, sizeof(LTShellPowerSubSwitch_Commands) / sizeof(LTShellPowerSubSwitch_Commands[0]));
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);

        reason = "no LTArray";
        if (!(S.powerSubswitches = lt_createobject(LTArray))) break;

        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTShellPowerSubSwitchImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellPowerSubSwitch, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellPowerSubSwitch) LTLIBRARY_DEFINITION;
