/*******************************************************************************
 * lt/source/ltshell/floodlight/LTShellFloodlight.c
 * 
 * FL Light Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/floodlight/LTDeviceFloodlight.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.floodlight");

/** Standard LT Interfaces ****************************************************/
static LTSystemShell *s_shell;
static ILTShell      *s_iShell;
static LTDeviceFloodlight  *s_FLLight;

/** Command Functions *********************************************************/

static int LTShellFloodlight_SetObjectBrightness(LTShell hShell, int argc, const char *argv[]) {
    u32 setObjBtValue = 0;
    if (argc != 2) {
        s_iShell->Print(hShell, "Usage: setobjbright <0-100)>\n");
        return -1;
    }
    setObjBtValue = lt_strtos32(argv[1], NULL, 0);
    if (!s_FLLight->API->SetObjectBrightness(s_FLLight, setObjBtValue)) {
        s_iShell->Print(hShell, "Failed to set the object brightness\n");
        return -1;
    }
    return 0;
}

static int LTShellFloodlight_GetObjectBrightness(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 getBtValue;
    s_FLLight->API->GetObjectBrightness(s_FLLight, &getBtValue);
    if (!getBtValue) {
        s_iShell->Print(hShell, "Failed to retrieve the object brightness\n");
        return -1;
    }
    s_iShell->Print(hShell, "%d%%\n", getBtValue);
    return 0;
}

static int LTShellFloodlight_GetCurrentBrightness(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 getAmbBtValue = 0;
    if (!(s_FLLight->API->GetCurrentBrightness(s_FLLight, &getAmbBtValue))) {
        s_iShell->Print(hShell, "Failed to retrieve current brightness\n");
        return -1;
    }
    s_iShell->Print(hShell, "%d%%\n", getAmbBtValue);
    return 0;
}

static int LTShellFloodlight_SetFlashFrequency(LTShell hShell, int argc, const char *argv[]) {
    u32 setFGValue = 0;
    if (argc != 2) {
        s_iShell->Print(hShell, "Usage: setflashfreq <0-10>\n");
        return -1;
    }
    setFGValue = lt_strtos32(argv[1], NULL, 0);

    if (!(s_FLLight->API->SetFlashFrequency(s_FLLight, setFGValue))) {
        s_iShell->Print(hShell, "Failed to set the flash frequency.\n");
        return -1;
    }
    return 0;
}

static int LTShellFloodlight_GetFlashFrequency(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 getFGValue = 0;
    if (!(s_FLLight->API->GetFlashFrequency(s_FLLight, &getFGValue))) {
        s_iShell->Print(hShell, "Failed to retrieve the flash frequency.\n");
        return -1;
    }
    s_iShell->Print(hShell, "%d\n", getFGValue);
    return 0;
}

static int LTShellFloodlight_SetAccommodationTime(LTShell hShell, int argc, const char *argv[]) {
    u32 setATValue;
    if (argc != 2) {
        s_iShell->Print(hShell, "Usage: setacctime <unit of 100 milliseconds>\n");
        return -1;
    }
    setATValue = lt_strtos32(argv[1], NULL, 0);

    if (!(s_FLLight->API->SetAccommodationTime(s_FLLight, setATValue))) {
        s_iShell->Print(hShell, "Failed to set the accommodation time.\n");
        return -1;
    }
    return 0;
}

static int LTShellFloodlight_GetAccommodationTime(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u32 accoTime;
    s_FLLight->API->GetAccommodationTime(s_FLLight, &accoTime);
    s_iShell->Print(hShell, "%d \n", accoTime);
    return 0;
}

static int LTShellFloodlight_LightOn(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(s_FLLight->API->LightOn(s_FLLight))) {
        s_iShell->Print(hShell, "Failed to turn on the light.\n");
        return -1;
    }
    return 0;
}

static int LTShellFloodlight_LightOff(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!(s_FLLight->API->LightOff(s_FLLight))) {
        s_iShell->Print(hShell, "Failed to turn off the light.\n");
        return -1;
    }
    return 0;
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static const LTSystemShell_CommandDesc FLLightCommands[] = {
    {"lighton"     , LTShellFloodlight_LightOn             , "light on"                                         , NULL},
    {"lightoff"    , LTShellFloodlight_LightOff            , "light off"                                        , NULL},
    {"setobjbright", LTShellFloodlight_SetObjectBrightness , "set light object brightness [0 to 100]"           , NULL},
    {"getobjbright", LTShellFloodlight_GetObjectBrightness , "retrieve light object brightness"                 , NULL},
    {"getcurbright", LTShellFloodlight_GetCurrentBrightness, "retrieve light current brightness"                , NULL},
    {"setflashfreq", LTShellFloodlight_SetFlashFrequency   , "set light flash frequency [0 to 10]Hz"            , NULL},
    {"getflashfreq", LTShellFloodlight_GetFlashFrequency   , "retrieve light flash frequency"                   , NULL},
    {"setacctime"  , LTShellFloodlight_SetAccommodationTime, "set accommodation time [unit of 100 milliseconds]", NULL},
    {"getacctime"  , LTShellFloodlight_GetAccommodationTime, "retrieve light accommodation time"                , NULL},
    {}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellFloodlightImpl_LibFini(void) {
    if (s_shell) {
        s_shell->UnregisterCommands(FLLightCommands);
        lt_closelibrary(s_shell);
        s_iShell = NULL;
    }
    if (s_FLLight) {
        lt_destroyobject(s_FLLight);
        s_FLLight = NULL;
    }
}

static bool LTShellFloodlightImpl_LibInit(void) {
    do {
        if (!(s_shell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("init.fail.ltshell", "Failed to open system shell library");
            break;
        }
        s_shell->RegisterCommands(FLLightCommands, sizeof(FLLightCommands) / sizeof(FLLightCommands[0]));
        if (!(s_iShell = lt_getlibraryinterface(ILTShell, s_shell))) {
            LTLOG_YELLOWALERT("init.fail.ishell", "Failed to open ILTShell library");
            break;
        }
        if (!(s_FLLight = lt_createobject(LTDeviceFloodlight))) {
            LTLOG_YELLOWALERT("init.fail.flight", "Failed to create LTDeviceFloodlight object");
            break;
        }
        return true;
    } while (0);
    LTShellFloodlightImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellFloodlight, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellFloodlight) LTLIBRARY_DEFINITION;
