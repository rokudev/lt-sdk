/*******************************************************************************
 * lt/source/ltshell/lightsensor/LTShellLightSensor.c
 *
 * FL Light Sensor Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/device/lightsensor/LTDeviceLightSensor.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.lightsensor");

/** Standard LT Interfaces ****************************************************/
static struct Statics {
    LTCore              *core;
    LTSystemShell       *shell;
    ILTShell            *iShell;
    LTDeviceLightSensor *lightSensor;
} S;

/** Helper Functions **********************************************************/

static u16 ChannelNameToFlag(const char *channelName) {
    if (lt_strcmp(channelName, "visible") == 0) {
        return kLTDeviceLightSensor_Channel_Visible;
    } else if (lt_strcmp(channelName, "ir") == 0) {
        return kLTDeviceLightSensor_Channel_IR;
    } else if (lt_strcmp(channelName, "red") == 0) {
        return kLTDeviceLightSensor_Channel_Red;
    } else if (lt_strcmp(channelName, "green") == 0) {
        return kLTDeviceLightSensor_Channel_Green;
    } else if (lt_strcmp(channelName, "blue") == 0) {
        return kLTDeviceLightSensor_Channel_Blue;
    }
    return (u16)lt_strtou32(channelName, NULL, 0);
}

/** Command Functions *********************************************************/
static int LTShellLightSensor_GetSupportedChannels(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    u16 channels = S.lightSensor->API->GetSupportedChannels(S.lightSensor);
    S.iShell->Print(hShell, "supported channel mask: 0x%04lx\n", LT_Pu32(channels));
    return 0;
}

static int LTShellLightSensor_ChannelValue(LTShell hShell, int argc, const char *argv[]) {
    u16 channel;

    if (argc != 2) {
        S.iShell->Print(hShell, "get <channel name|channel flag>\n");
        return -1;
    }

    channel = ChannelNameToFlag(argv[1]);
    if (!channel) {
        S.iShell->Print(hShell, "Invalid channel name or flag: %s\n", argv[1]);
        return -1;
    }

    IlluminanceValue value = 0;
    if (!(S.lightSensor->API->GetChannelValue(S.lightSensor, channel, &value))) {
        S.iShell->Print(hShell, "Failed to Get Current Light Value\n");
        return -1;
    }
    S.iShell->Print(hShell, "%lu.%03lu lux\n", LT_Pu32(value/1000), LT_Pu32(value%1000));
    return 0;
}

static int LTShellLightSensor_HelpCmd(LTShell hShell, int argc, const char *argv[]);

static const LTSystemShell_CommandDesc LightSensorCommands[] = {
    {"help",     LTShellLightSensor_HelpCmd,              "list commands",          NULL},
    {"channels", LTShellLightSensor_GetSupportedChannels, "get supported channels", NULL},
    {"get",      LTShellLightSensor_ChannelValue,         "get value of a channel", NULL},
    { NULL,      NULL,                                     NULL,                    NULL}
};

static int LTShellLightSensor_HelpCmd(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    S.iShell->Print(hShell, "usage: als <command> [args]\nCommands:\n");
    for (int n = 0; LightSensorCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %-10s - %s\n", LightSensorCommands[n].pCommand,
            LightSensorCommands[n].pDescription);
    }
    S.iShell->Print(hShell,
        "Examples:\n"
        "  als channels\n"
        "  als get visible\n"
        "  als get 0x04\n"
    );
    return 0;
}

static void LTShellLightSensor_Help(LTShell hShell, int argc, const char *argv[]) {
    LTShellLightSensor_HelpCmd(hShell, argc, argv);
}

static int LTShellLightSensor_ALS(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; LightSensorCommands[n].pCommand; n++) {
            if (lt_strcmp(LightSensorCommands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return LightSensorCommands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static const LTSystemShell_CommandDesc LightSensorRootCommands[] = {
    { "als",        LTShellLightSensor_ALS,  "ALS Command", LTShellLightSensor_Help}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellLightSensorImpl_LibFini(void) {
    if (S.shell) {
        S.shell->UnregisterCommands(LightSensorRootCommands);
        lt_closelibrary(S.shell);
    }
    if (S.lightSensor) {
        lt_destroyobject(S.lightSensor);
    }
    S = (struct Statics) {};
}

static bool LTShellLightSensorImpl_LibInit(void) {
    S = (struct Statics) {
        .core  = LT_GetCore(),
    };
    do {
        if (!(S.lightSensor = lt_createobject(LTDeviceLightSensor))) {
            LTLOG_YELLOWALERT("f.open.dev", "failed to open light sensor device");
            break;
        }
        if (!(S.shell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("f.open.shell", "failed to open system shell");
            break;
        }
        S.shell->RegisterCommands(LightSensorRootCommands, sizeof(LightSensorRootCommands) / sizeof(LightSensorRootCommands[0]));
        if (!(S.iShell = lt_getlibraryinterface(ILTShell, S.shell))) {
            LTLOG_YELLOWALERT("f.iface.shell", "failed to open Shell interface");
            break;
        }
        return true;
    } while (0);
    LTShellLightSensorImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellLightSensor, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellLightSensor) LTLIBRARY_DEFINITION;
