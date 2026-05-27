/*******************************************************************************
 * lt/source/ltshell/flpir/LTShellFlPIR.c
 *
 * FL PIR Sensor Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/device/flpir/LTDeviceFlPIR.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.pir");

/** Standard LT Interfaces *****************************************************/
static LTSystemShell *s_shell;
static ILTShell      *s_iShell;
static LTDeviceFlPIR *s_devFlPir;
static LTThread       s_shellPIRThread;
static ILTThread     *s_iThread;
static bool           s_eventStatus = false;

/** Utility Functions *********************************************************/
static void OnEventProc(LTDriverFlPIREvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LT_UNUSED(event);
    LT_UNUSED(pEventData);
    if (s_eventStatus) {
        LTLOG_YELLOWALERT("events", "Motion Detected !");
    }
}

static void PIRThreadExit(void) {
    if (s_devFlPir) {
        s_devFlPir->API->NoMotionEvent(s_devFlPir, NULL);
        lt_destroyobject(s_devFlPir);
    }
}

static bool PIRThreadInit(void) {
    if (!(s_devFlPir = lt_createobject(LTDeviceFlPIR))) {
        return false;
    }
    s_devFlPir->API->OnMotionEvent(s_devFlPir, OnEventProc, NULL);
    return true;
}

/** Command Functions *********************************************************/
static int LTShellFlPIR_SetPIRSensitivity(LTShell hShell, int argc, const char *argv[]) {
    u8 setPIRSensitivityValue = 0;
    if (argc != 2) {
        s_iShell->Print(hShell, "Usage: setpirsensitivity <value(0 to 100)>\n");
        return -1;
    }
    setPIRSensitivityValue = lt_strtos32(argv[1], NULL, 0);
    if (!s_devFlPir->API->SetPIRSensitivity(s_devFlPir, setPIRSensitivityValue)) {
        s_iShell->Print(hShell, "Failed to Set a PIR Sensitivity\n");
        return -1;
    }
    return 0;
}

static int LTShellFlPIR_GetPIRSensitivity(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u8 getPIRSensitivityValue = 0;
    if (!s_devFlPir->API->GetPIRSensitivity(s_devFlPir, &getPIRSensitivityValue)) {
        s_iShell->Print(hShell, "Failed to Get a PIR Sensitivity\n");
        return -1;
    }
    s_iShell->Print(hShell, "%d\n", getPIRSensitivityValue);
    return 0;
}

static int LTShellFlPIR_SetPIRZone(LTShell hShell, int argc, const char *argv[]) {
    u8 setPIRZoneValue = 0;
    if (argc < 2 || argc > 4) {
        s_iShell->Print(hShell, "Usage: setpirzone <zones>\n");
        s_iShell->Print(hShell,
                        "PIR Zone Options\n"
                        "L : Left\nM : Middle\nR : Right\n");
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        if (lt_strcmp(argv[i], "L") == 0) {
            setPIRZoneValue |= kLTDeviceFlPIRZone_Left;
        } else if (lt_strcmp(argv[i], "R") == 0) {
            setPIRZoneValue |= kLTDeviceFlPIRZone_Right;
        } else if (lt_strcmp(argv[i], "M") == 0) {
            setPIRZoneValue |= kLTDeviceFlPIRZone_Middle;
        } else {
            s_iShell->Print(hShell, "Invalid PIR Zone\n");
            return -1;
        }
    }
    s_iShell->Print(hShell, "PIR zone %x\n", setPIRZoneValue);
    if (!s_devFlPir->API->SetPIRZone(s_devFlPir, setPIRZoneValue)) {
        s_iShell->Print(hShell, "Failed to Set a PIR Zone\n");
        return -1;
    }
    return 0;
}

static int LTShellFlPIR_GetPIRZone(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTDeviceFlPIRZone getPIRZoneValue = 0;
    if (!s_devFlPir->API->GetPIRZone(s_devFlPir, &getPIRZoneValue)) {
        s_iShell->Print(hShell, "Failed to Get a PIR Zone\n");
        return -1;
    }
    if (getPIRZoneValue == kLTDeviceFlPIRZone_Left) {
        s_iShell->Print(hShell, "Left \n");
    } else if (getPIRZoneValue == kLTDeviceFlPIRZone_Right) {
        s_iShell->Print(hShell, "Right \n");
    } else if (getPIRZoneValue == kLTDeviceFlPIRZone_Middle) {
        s_iShell->Print(hShell, "Middle \n");
    } else if (getPIRZoneValue == (kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Right)) {
        s_iShell->Print(hShell, "Left Right \n");
    } else if (getPIRZoneValue == (kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle)) {
        s_iShell->Print(hShell, "Left Middle\n");
    } else if (getPIRZoneValue == (kLTDeviceFlPIRZone_Right | kLTDeviceFlPIRZone_Middle)) {
        s_iShell->Print(hShell, "Right Middle\n");
    } else if (getPIRZoneValue == (kLTDeviceFlPIRZone_Left | kLTDeviceFlPIRZone_Middle | kLTDeviceFlPIRZone_Right)) {
        s_iShell->Print(hShell, "Left Right Middle\n");
    }
    return 0;
}

static int LTShellFlPIR_SetPIRAlgo(LTShell hShell, int argc, const char *argv[]) {
    u8 setPIRAlgoValue = 0;
    if (argc != 2) {
        s_iShell->Print(hShell,
                        "Usage: setPIRalgo <value(0 or 1)>\n"
                        "0: PIR Filter Algorithm Off\n"
                        "1: PIR Filter Algorithm On\n");
        return -1;
    }
    setPIRAlgoValue = lt_strtos32(argv[1], NULL, 0);
    if ((setPIRAlgoValue != 0) && (setPIRAlgoValue != 1)) {
        s_iShell->Print(hShell, "Incorrect PIR algorithm parameter.\n");
    }
    if (!s_devFlPir->API->SetPIRAlgo(s_devFlPir, setPIRAlgoValue)) {
        s_iShell->Print(hShell, "Failed to Set a PIR Algorithm\n");
        return -1;
    }
    return 0;
}

static int LTShellFlPIR_GetPIRAlgo(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTDeviceFlPIRAlgo getPIRAlgoValue = 0;
    if (!s_devFlPir->API->GetPIRAlgo(s_devFlPir, &getPIRAlgoValue)) {
        s_iShell->Print(hShell, "Failed to Get a PIR Zone\n");
        return -1;
    }
    switch (getPIRAlgoValue) {
        case kLTDeviceFlPIRAlgo_Off:
            s_iShell->Print(hShell, "PIR Filter Algorithm Off\n");
            break;
        case kLTDeviceFlPIRAlgo_HualaiActivate:
            s_iShell->Print(hShell, "PIR Filter Algorithm On\n");
            break;
        default:
            s_iShell->Print(hShell, "Error: Invalid PIR Filter Algorithm value.\n");
            break;
    }
    return 0;
}

static int LTShellFlPIR_OnMotionEvent(LTShell hShell, int argc, const char *argv[]) {
    if ((argc != 2)) {
        s_iShell->Print(hShell, "Usage: motionevent <on/off>\n");
        return -1;
    }
    if (lt_strcmp(argv[1], "on") && lt_strcmp(argv[1], "off")) {
        s_iShell->Print(hShell, "Usage: motionevent <on/off>\n");
        return -1;
    }
    s_eventStatus = false;
    if (!lt_strcmp(argv[1], "on")) {
        s_eventStatus = true;
    }
    s_iShell->Print(hShell, "PIR Motion event %sed !\n", s_eventStatus ? "start" : "end");
    return 0;
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static const LTSystemShell_CommandDesc LTShellFlPIRCommands[] = {
    { "motionevent",       LTShellFlPIR_OnMotionEvent,     "Starts a Motion Events",        NULL },
    { "setpirsensitivity", LTShellFlPIR_SetPIRSensitivity, "set a PIR Sensitivity",         NULL },
    { "getpirsensitivity", LTShellFlPIR_GetPIRSensitivity, "get a PIR Sensitivity details", NULL },
    { "setpirzone",        LTShellFlPIR_SetPIRZone,        "set a PIR Zone",                NULL },
    { "getpirzone",        LTShellFlPIR_GetPIRZone,        "get a PIR Zone Value",          NULL },
    { "setpiralgo",        LTShellFlPIR_SetPIRAlgo,        "Set PIR Algorithm Type",        NULL },
    { "getpiralgo",        LTShellFlPIR_GetPIRAlgo,        "Get PIR Algorithm Type",        NULL }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellFlPIRImpl_LibFini(void) {
    if (s_shell) {
        s_shell->UnregisterCommands(LTShellFlPIRCommands);
        lt_closelibrary(s_shell);
    }
    s_iThread->Terminate(s_shellPIRThread);
    s_iThread->WaitUntilFinished(s_shellPIRThread, LTTime_Infinite());
    s_iThread->Destroy(s_shellPIRThread);
}

static bool LTShellFlPIRImpl_LibInit(void) {
    do {
        if (!(s_shell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("f.lib", "Failed to open FlPIR Shell Lib");
            break;
        }
        s_shell->RegisterCommands(LTShellFlPIRCommands, sizeof(LTShellFlPIRCommands) / sizeof(LTShellFlPIRCommands[0]));

        s_iShell  = lt_getlibraryinterface(ILTShell, s_shell);
        if (!(s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))) {
            LTLOG_YELLOWALERT("f.lib.thread", "Failed to get iface thread");
            break;
        }

        if (!(s_shellPIRThread = LT_GetCore()->CreateThread("pirEvents"))) {
            LTLOG_YELLOWALERT("f.thread", "Failed to allocate thread");
            break;
        }
        s_iThread->Start(s_shellPIRThread, PIRThreadInit, PIRThreadExit);
        return true;
    } while (0);
    LTShellFlPIRImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellFlPIR, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellFlPIR) LTLIBRARY_DEFINITION;

