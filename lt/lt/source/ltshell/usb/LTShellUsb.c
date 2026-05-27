/*******************************************************************************
 *
 * LTShellUsb: USB Manager Shell
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
#include <lt/system/usb/LTSystemUsbBusManager.h>

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellUsb, (LTSystemShell));

/** Standard LT Interfaces ****************************************************/
static struct Statics {
    ILTShell              *shell;
    LTSystemUsbBusManager *busManager;
} S;

/** Forward references ********************************************************/
static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

static void PrintMode(const char *mode, void *clientData) {
    LTShell hShell = (LTShell)clientData;
    S.shell->Print(hShell, "%s\n", mode);
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static int SHL_UsbSwitchMode(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        S.shell->Print(hShell, "usage: usb switch <mode|none>\n");
        return -1;
    }

    if (lt_strcmp(argv[1], "none") == 0) {
        S.busManager->API->ChangeMode(NULL);
    } else {
        S.busManager->API->ChangeMode(argv[1]);
    }
    return 0;
}

static int SHL_UsbGetMode(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);

    LTString currentMode = S.busManager->API->GetCurrentMode();
    if (currentMode) {
        S.shell->Print(hShell, "Current USB Bus Mode: %s\n", currentMode);
        ltstring_destroy(currentMode);
    } else {
        S.shell->Print(hShell, "No USB Bus Mode is currently set.\n");
    }
    return 0;
}

static int SHL_UsbListModes(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);

    S.shell->Print(hShell, "USB Bus Modes:\n");
    S.busManager->API->ListModes(PrintMode, (void *)hShell);
    return 0;
}


static const LTSystemShell_CommandDesc Usb_Commands[] = {
    { "help",    SHL_Help,          "list of usb commands", NULL},
    { "current", SHL_UsbGetMode,    "Get current USB mode", NULL},
    { "switch",  SHL_UsbSwitchMode, "Switch USB mode",      NULL},
    { "list",    SHL_UsbListModes,  "List USB modes",       NULL},
    { NULL, NULL, NULL, NULL}
};

static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    S.shell->Print(hShell, "usage: usb <command> [args]\nCommands:\n");
    for (int n = 0; Usb_Commands[n].pCommand; n++) {
        S.shell->Print(hShell, "  %-10s - %s\n", Usb_Commands[n].pCommand,
            Usb_Commands[n].pDescription);
    }
    return 0;
}

static int SHL_Usb(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; Usb_Commands[n].pCommand; n++) {
            if (lt_strcmp(Usb_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return Usb_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "usb", SHL_Usb, "USB Bus Manager commands", LTShell_Help}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellUsbImpl_LibFini(void) {
    LT_GetLTSystemShell()->UnregisterCommands(SHL_Commands);
    lt_destroyobject(S.busManager);
}

static bool LTShellUsbImpl_LibInit(void) {
    S.shell = lt_getlibraryinterface(ILTShell, LT_GetLTSystemShell());
    S.busManager = lt_createobject(LTSystemUsbBusManager);
    if (!S.busManager) {
        return false;
    }
    LT_GetLTSystemShell()->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellUsb, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellUsb) LTLIBRARY_DEFINITION;
