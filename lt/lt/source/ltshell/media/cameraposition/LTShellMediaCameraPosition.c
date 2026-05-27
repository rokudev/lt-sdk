/*******************************************************************************
 * LT Shell Camera Position library.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/media/cameraposition/LTMediaCameraPosition.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.shellcameraposition");
// NOTE: the macro below doesn't seem to work?
// LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellMediaCameraPosition, (LTSystemShell) (LTMediaCameraPosition) );

static struct Statics {
    LTCore *core;
    LTSystemShell *shell;
    LTMediaCameraPosition *cameraposition;
    LTThread shellCameraPositionThread;
    ILTShell *iShell;
    ILTThread *iThread;
    LTMutex *mutex;
    LTMotorPanTilt_SentryPath *customPath;
} S;

static bool LTShellMediaCameraPosition_ThreadInit(void) {
    /* Don't really need these. */
    return true;
}

static void LTShellMediaCameraPosition_ThreadExit(void) {
}

/*******************************************************************************
 * Shell Interface
 ******************************************************************************/

static int LTShellMediaCameraPosition_Status(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    LTMediaCameraPosition_Mode mode = S.cameraposition->GetMode();
    S.iShell->Print(hShell, "Mode: %d", mode);

    return 0;
}

static int LTShellMediaCameraPosition_Start(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "Starting cameraposition library. \n");
    S.cameraposition = lt_openlibrary(LTMediaCameraPosition);
    S.customPath = lt_malloc(sizeof(LTMotorPanTilt_SentryPath) + sizeof(LTMotorPanTilt_SentryWaypoint) * 4);

    return 0;
}

static int LTShellMediaCameraPosition_IdleMode(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "cameraposition: idle mode. \n");
    S.cameraposition->Switch(kLTMediaCameraPosition_IdleMode);

    return 0;
}

static int LTShellMediaCameraPosition_SentryMode(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "cameraposition: sentry mode. \n");
    S.cameraposition->Switch(kLTMediaCameraPosition_SentryMode);

    return 0;
}

static int LTShellMediaCameraPosition_TrackingMode(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "cameraposition: tracking mode. \n");
    S.cameraposition->Switch(kLTMediaCameraPosition_TrackingMode);

    return 0;
}

static int LTShellMediaCameraPosition_SentryAndTrackingMode(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "cameraposition: sentry and tracking mode. \n");
    S.cameraposition->Switch(kLTMediaCameraPosition_SentryAndTrackingMode);

    return 0;
}

static int LTShellMediaCameraPosition_ManualMove(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 3) {
        S.iShell->Print(hShell, "Usage: manualmove <pan> <tilt>\n");
        return -1;
    }

    LTMotorPanTilt_Position direction;
    direction.pan  = lt_strtos32(argv[1], NULL, 10);
    direction.tilt = lt_strtos32(argv[2], NULL, 10);
    direction.relative = true;
    LTLOG("manualmove", "Moving camera to (%d, %d)", direction.pan, direction.tilt);
    S.cameraposition->MoveManually(direction);

    return 0;
}

static int LTShellMediaCameraPosition_MarkWaypoint(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "Usage: mark <index>\n");
        return -1;
    }

    u8 index = (u8)lt_strtos32(argv[1], NULL, 10);
    if (index >= 4) {
        S.iShell->Print(hShell, "Invalid index. Must be between 0 and 3.\n");
        return -1;
    }

    if (S.cameraposition->MarkWaypoint(index)) {
        S.iShell->Print(hShell, "Waypoint marked at index %d\n", index);
        return 0;
    } else {
        S.iShell->Print(hShell, "Failed to mark waypoint\n");
        return -1;
    }
}

static int LTShellMediaCameraPosition_SetCompleteNewSentry(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 9 || (argc - 1) % 4 != 0) {
        S.iShell->Print(hShell, "Usage: setnewsentry <pan1> <tilt1> <dwellTime1> <relative1> [<pan2> <tilt2> <dwellTime2> <relative2> ...]\n");
        return -1;
    }

    u32 numWaypoints = (argc - 1) / 4;
    LTMotorPanTilt_SentryPath *newPath = lt_malloc(sizeof(LTMotorPanTilt_SentryPath) + numWaypoints * sizeof(LTMotorPanTilt_SentryWaypoint));
    if (!newPath) {
        S.iShell->Print(hShell, "Failed to allocate memory for new sentry path\n");
        return -1;
    }

    newPath->numWaypoints = numWaypoints;
    for (u32 i = 0; i < numWaypoints; i++) {
        newPath->waypoints[i].position.pan = lt_strtos32(argv[i*4 + 1], NULL, 10);
        newPath->waypoints[i].position.tilt = lt_strtos32(argv[i*4 + 2], NULL, 10);
        newPath->waypoints[i].dwellTime = LTTime_Milliseconds(lt_strtou32(argv[i*4 + 3], NULL, 10));
        newPath->waypoints[i].position.relative = (lt_strtou32(argv[i*4 + 4], NULL, 10) != 0);
    }

    if (S.cameraposition->SetNewSentryPath(newPath)) {
        S.iShell->Print(hShell, "New sentry path set successfully with %u waypoints\n", numWaypoints);
        lt_free(newPath);
        return 0;
    } else {
        S.iShell->Print(hShell, "Failed to set new sentry path\n");
        lt_free(newPath);
        return -1;
    }
}

static int LTShellMediaCameraPosition_ResetMotor(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    LTMotorPanTilt_Position position;
    lt_memset(&position,0,sizeof(position));
    if (S.cameraposition->ResetMotor(position)) {
        S.iShell->Print(hShell, "Motor reset initiated\n");
        return 0;
    } else {
        S.iShell->Print(hShell, "Failed to reset motor\n");
        return -1;
    }
}

static int LTShellMediaCameraPosition_SetMotorSpeed(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "Usage: speed <1-9>\n");
        return -1;
    }

    u8 speed = (u8)lt_strtos32(argv[1], NULL, 10);
    if (1 > speed || 9 < speed) {
        S.iShell->Print(hShell, "Invalid speed. Must be between 1 and 9.\n");
        return -1;
    }
    S.cameraposition->SetSpeed(speed) ;
    S.iShell->Print(hShell, "Motor speed is now %d\n", speed);
    return 0;
}

static int LTShellMediaCameraPosition_Stop(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    S.iShell->Print(hShell, "cameraposition: stopping\n");
    lt_closelibrary(S.cameraposition);

    return 0;
}

static int LTShellMediaCameraPosition_Help(LTShell hShell, int argc, const char *argv[]);
static const LTSystemShell_CommandDesc CameraPositionCommands[] = {
    /* Help must be first in this list */
    { "help",       LTShellMediaCameraPosition_Help,                     "- list supported commands", NULL },
    { "status",     LTShellMediaCameraPosition_Status,                   "- display camera position status", NULL },
    { "start",      LTShellMediaCameraPosition_Start,                    "- start camera position", NULL },
    { "idle",       LTShellMediaCameraPosition_IdleMode,                 "- activate idle mode for camera position", NULL },
    { "sentry",     LTShellMediaCameraPosition_SentryMode,               "- activate sentry mode for camera position", NULL },
    { "tracking",   LTShellMediaCameraPosition_TrackingMode,             "- activate tracking mode for camera position", NULL },
    { "sentrack",   LTShellMediaCameraPosition_SentryAndTrackingMode,    "- activate sentry and tracking mode for camera position", NULL },
    { "stop",       LTShellMediaCameraPosition_Stop,                     "- stop camera position", NULL },
    { "reset",      LTShellMediaCameraPosition_ResetMotor,               "- reset camera position", NULL },
    { "mark",       LTShellMediaCameraPosition_MarkWaypoint,             "- mark a waypoint at current camera position location", NULL },
    { "setnewsentry", LTShellMediaCameraPosition_SetCompleteNewSentry,   "- set a completely new sentry path", NULL },
    { "manualmove", LTShellMediaCameraPosition_ManualMove,               "- manually move camera position location", NULL },
    { "speed",      LTShellMediaCameraPosition_SetMotorSpeed,            "- set the speed of the motor", NULL },
    { }
};

static int LTShellMediaCameraPosition_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    for (int n = 0; CameraPositionCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", CameraPositionCommands[n].pCommand, CameraPositionCommands[n].pDescription);
    }
    if (argc < 2) {
        return 0;
    } else {
        return -1;
    }
}

static int LTShellMediaCameraPosition_Handler(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;

    if (argc >= 2) {
        for (int n = 0; CameraPositionCommands[n].pCommand; n++) {
            if (0 == lt_strcmp(CameraPositionCommands[n].pCommand, argv[1])) {
                cmd = n;
                break;
            }
        }
    }
    return CameraPositionCommands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /*
        This LTShell Help proc is so "help cameraposition" works in addition to "cameraposition help".
        It prints usage and calls the regular LTShellMediaCameraPosition_Help
    */
    S.iShell->Print(hShell, "usage: position <command> [args]\nCommands:\n");
    (void)LTShellMediaCameraPosition_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellMediaCameraPosition_Commands[] = {
    { "position", LTShellMediaCameraPosition_Handler, "sentry, tracking, manual mode activation and obtain status of camera position engine", LTShell_Help },
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellMediaCameraPositionImpl_LibFini(void) {
    if(S.iThread) S.iThread->Destroy(S.shellCameraPositionThread);
    lt_closelibrary(S.cameraposition);

    if (S.shell) {
        S.shell->UnregisterCommands(LTShellMediaCameraPosition_Commands);
        lt_closelibrary(S.shell);
    }

    S = (struct Statics) {};
}

static bool LTShellMediaCameraPositionImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore(),
        .mutex = lt_createobject(LTMutex),
        .customPath = NULL,
    };

    do {
        if (!(S.shell = lt_openlibrary(LTSystemShell))) {
            LTLOG("init.fail", "failed to open shell library");
            break;
        }

        S.shell->RegisterCommands(LTShellMediaCameraPosition_Commands, sizeof(LTShellMediaCameraPosition_Commands) / sizeof(LTShellMediaCameraPosition_Commands[0]));
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);

        if (!(S.shellCameraPositionThread = S.core->CreateThread("ShellCameraPosition"))) {
            LTLOG("init.fail", "failed to create thread");
            break;
        }

        S.iThread = lt_gethandleinterface(ILTThread, S.shellCameraPositionThread);
        S.iThread->SetStackSize(S.shellCameraPositionThread, 1024);
        S.iThread->Start(S.shellCameraPositionThread, LTShellMediaCameraPosition_ThreadInit, LTShellMediaCameraPosition_ThreadExit);

        return true;
    } while (false);

    LTShellMediaCameraPositionImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellMediaCameraPosition, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellMediaCameraPosition) LTLIBRARY_DEFINITION;
