/*******************************************************************************
 *
 * LTShellPushButton: PushButton Shell
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
#include <lt/device/pushbutton/LTDevicePushButton.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.pb");

/** Standard LT Interfaces ****************************************************/
static LTDevicePushButton *s_DevPB;
static ILTShell * SHL_iShell;
/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static LTSystemShell *SHL_Library;

/** Forward references ********************************************************/
static int  SHL_Info(LTShell hShell, int argc, const char *argv[]);
static void ButtonPressed(u32 btn, void *data);
static void ButtonReleased(u32 btn, void *data);

/** Utility Functions *********************************************************/

/** Callback Functions ********************************************************/
static void ButtonPressed(u32 btn, void *data) {
    LT_SIZE dataint = (LT_SIZE)data;
    unsigned int udataint = (unsigned int)dataint;
    LTLOG("pressed", "Button %u is pressed, data %u", (unsigned int)btn, udataint);
}

static void ButtonReleased(u32 btn, void *data) {
    LT_SIZE dataint = (LT_SIZE)data;
    unsigned int udataint = (unsigned int)dataint;
    LTLOG("release", "Button %u is released, data %u", (unsigned int)btn, udataint);
}

/** Command Functions *********************************************************/

static int SHL_Info(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 nDeviceUnits = s_DevPB->GetNumDeviceUnits();

    for (u32 nDevice = 0; nDevice < nDeviceUnits; ++nDevice) {
        char pBankName[32];
        s_DevPB->GetPushButtonNameFromIndex(nDevice, pBankName, 32);
        SHL_iShell->Print(hShell, "Button %u:\t %s \t Value: %u\n", (unsigned int)nDevice, pBankName, s_DevPB->IsButtonPressed(nDevice));
    }
    return 0;
}

static int SHL_Get(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: getbutton buttonname\n");
    }

    u32 nDeviceUnitIndex = 0;
    if (s_DevPB->GetPushButtonIndexFromName(argv[1], &nDeviceUnitIndex)) {
        SHL_iShell->Print(hShell, "Button %u:\t %s \t Value: %u\n", (unsigned int)nDeviceUnitIndex, argv[1], s_DevPB->IsButtonPressed(nDeviceUnitIndex));
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain unit number for %s\n", argv[1]);
    }
    return 0;
}

static int SHL_SetIRQ(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: setirq button_name\n");
    }

    u32 nDeviceUnitIndex = 0;
    if (s_DevPB->GetPushButtonIndexFromName(argv[1], &nDeviceUnitIndex)) {
        s_DevPB->RegisterForButtonPress(nDeviceUnitIndex, ButtonPressed, NULL, (void *)12345);
        s_DevPB->RegisterForButtonRelease(nDeviceUnitIndex, ButtonReleased, NULL, (void *)12346);
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain unit number for %s\n", argv[1]);
    }
    return 0;
}

static int SHL_ClearIRQ(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    SHL_iShell->Print(hShell, "Clearing irqs\n");
    u32 nDeviceUnitIndex = 0;
    if (s_DevPB->GetPushButtonIndexFromName(argv[1], &nDeviceUnitIndex)) {
        s_DevPB->UnregisterForButtonPress(nDeviceUnitIndex, ButtonPressed);
        s_DevPB->UnregisterForButtonRelease(nDeviceUnitIndex, ButtonReleased);
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain unit number for %s\n", argv[1]);
    }
    return 0;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "listbuttons",  SHL_Info,      "List available buttons",       NULL },
    { "getbutton",    SHL_Get,       "Get button value",             NULL },
    { "setirq",       SHL_SetIRQ,    "Register for button events",   NULL },
    { "clearirq",     SHL_ClearIRQ,  "Unregister for button events", NULL },
};

static void SHL_Quit(void) {
    if (SHL_Library) {
        SHL_Library->UnregisterCommands(SHL_Commands);
        lt_closelibrary(SHL_Library);
    }
}

static bool SHL_Init(void) {
    SHL_Library = lt_openlibrary(LTSystemShell);
    if (!SHL_Library) return false;

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellPushButtonImpl_LibFini(void) {
    SHL_Quit();
    lt_closelibrary(s_DevPB);       // null ok
}

static bool LTShellPushButtonImpl_LibInit(void) {
    s_DevPB = lt_openlibrary(LTDevicePushButton);
    if (s_DevPB && SHL_Init()) return true;
    LTShellPushButtonImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellPushButton, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellPushButton) LTLIBRARY_DEFINITION;
