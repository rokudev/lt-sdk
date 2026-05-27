/*******************************************************************************
 *
 * "LTShellMotor",: Motor Shell
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
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/motor/LTDeviceMotorPanTilt.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.motor");
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellMotor, (LTSystemShell) );


/** Standard LT Interfaces ****************************************************/
static ILTShell         *SHL_iShell;
static struct Statics {
    LTMotorPanTilt *pMotorPanTilt;
    LTCore *pCore;
    ILTThread *iThread;
    LTThread hThread;
} S;

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

static void OnMovementEventProc(LTMotorPanTilt_MovementChangeEvent event, void *eventData, void * pClientData) {
    LTMotorPanTilt_MovementData *moveData = (LTMotorPanTilt_MovementData *)eventData;

    switch (event) {
        case kLTMotorPanTilt_MovementChangeEvent_StartedMovement:
            LTLOG("event.move", "Movement started from (%d %d) to (%d %d)",
                    moveData->fromPosition.pan,
                    moveData->fromPosition.tilt,
                    moveData->targetPosition.pan,
                    moveData->targetPosition.tilt);
            break;
        case kLTMotorPanTilt_MovementChangeEvent_InProgressMovement:
            LTLOG("event.move", "Movement in progress from (%d %d) to (%d %d)",
                    moveData->fromPosition.pan,
                    moveData->fromPosition.tilt,
                    moveData->targetPosition.pan,
                    moveData->targetPosition.tilt);
            break;
        case kLTMotorPanTilt_MovementChangeEvent_CompletedMovement:
            LTLOG("event.move", "Movement completed from (%d %d) to (%d %d)",
                    moveData->fromPosition.pan,
                    moveData->fromPosition.tilt,
                    moveData->targetPosition.pan,
                    moveData->targetPosition.tilt);
            break;
        case kLTMotorPanTilt_MovementChangeEvent_FailedMovement:
            LTLOG("event.move", "Movement failed");
            break;
        default:
            LTLOG("event.move", "Unknown event %d", event);
            break;
    }

    LT_UNUSED(pClientData);
    LT_UNUSED(eventData);
}

static void OnMotorStatusEventProc(LTMotorPanTilt_StateChangeEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    LTMotorPanTilt_Position *pos = (LTMotorPanTilt_Position *)eventData;
    LTMotorPanTilt_MovementData *moveData = (LTMotorPanTilt_MovementData *)eventData;

    switch (event) {
        case kLTMotorPanTilt_StateChangeEvent_Idle:
            LTLOG("event.status", "Motor is idle, pan %d, tilt %d", pos->pan, pos->tilt);
            break;
        case kLTMotorPanTilt_StateChangeEvent_Moving:
            LTLOG("event.status", "Motor is moving, from (%d %d) to (%d %d)",
                    moveData->fromPosition.pan,
                    moveData->fromPosition.tilt,
                    moveData->targetPosition.pan, moveData->targetPosition.tilt);
            break;
        case kLTMotorPanTilt_StateChangeEvent_Calibrating:
            LTLOG("event.status", "Motor is calibrating");
            break;
        case kLTMotorPanTilt_StateChangeEvent_Resetting:
            LTLOG("event.status", "Motor is resetting");
            break;
        case kLTMotorPanTilt_StateChangeEvent_Error:
            LTLOG("event.status", "Motor error");
            break;
        default:
            LTLOG("event.status", "Unknown event %d", event);
            break;
    }

}

static void OnSentryPathStateChangeEvent(LTMotorPanTilt_SentryEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(pClientData);
    LTLOG("event.sentry", "Sentry path event %d", event);
    LTMotorPanTilt_SentryWaypoint *waypoint = (LTMotorPanTilt_SentryWaypoint *)eventData;
    LTMotorPanTilt_Position *pos = (LTMotorPanTilt_Position *)eventData;
    switch (event) {
        case kLTMotorPanTilt_SentryEvent_MovingToWaypoint:
            LTLOG("event.sentry", "Moving to waypoint %d %d dwell %lldms",
                    waypoint->position.pan, waypoint->position.tilt, LTTime_GetMilliseconds(waypoint->dwellTime));
            break;
        case kLTMotorPanTilt_SentryEvent_Paused:
            LTLOG("event.sentry", "Sentry path paused at %d %d", pos->pan, pos->tilt);
            break;
        case kLTMotorPanTilt_SentryEvent_Dwelling:
            LTLOG("event.sentry", "Move to waypoint complete, dwelling at %d %d for %lldms",
                    waypoint->position.pan, waypoint->position.tilt, LTTime_GetMilliseconds(waypoint->dwellTime));
            break;
        default:
            LTLOG("event.sentry", "Unknown event %d", event);
            break;
    }

}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static int SHL_PanTilt(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(hShell);
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    if (argc < 2) {
        SHL_iShell->Print(hShell, "Usage: pantilt <command>\n");
        return -1;
    }

    if (lt_strcmp(argv[1], "reset") == 0) {
        SHL_iShell->Print(hShell, "Resetting Pan Tilt motors\n");
        S.pMotorPanTilt->API->Reset(S.pMotorPanTilt, NULL);
    } else if (lt_strcmp(argv[1], "move") == 0) {
        if (argc < 4) {
            SHL_iShell->Print(hShell, "Usage: pantilt move <pan> <tilt>\n");
            return -1;
        }

        LTMotorPanTilt_Position pos;
        pos.pan = lt_strtos32(argv[2], NULL, 10);
        pos.tilt = lt_strtos32(argv[3], NULL, 10);
        pos.relative = false;
        S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &pos);
        SHL_iShell->Print(hShell, "Moving Pan Tilt motors to %d %d\n", pos.pan, pos.tilt);
    } else if (lt_strcmp(argv[1], "moverel") == 0) {
        if (argc < 4) {
            SHL_iShell->Print(hShell, "Usage: pantilt moverel <pan> <tilt>\n");
            return -1;
        }

        LTMotorPanTilt_Position pos;
        pos.pan = lt_strtos32(argv[2], NULL, 10);
        pos.tilt = lt_strtos32(argv[3], NULL, 10);
        pos.relative = true;
        S.pMotorPanTilt->API->MoveToPosition(S.pMotorPanTilt, &pos);
        SHL_iShell->Print(hShell, "Moving Pan Tilt motors relative by %d %d\n", pos.pan, pos.tilt);
    } else if (lt_strcmp(argv[1], "calintv") == 0) {
        LTTime calintv;
        if (argc > 3) {
            SHL_iShell->Print(hShell, "Usage: pantilt calintv [calintv]\n");
            return -1;
        }

        if (argc == 2) {
            S.pMotorPanTilt->API->GetProperty(S.pMotorPanTilt, "calibrationInterval", &calintv);
            if (LTTime_IsInfinite(calintv)) {
                SHL_iShell->Print(hShell, "Calibration interval: infinite\n");
            } else {
                SHL_iShell->Print(hShell, "Calibration interval: %lldms\n", LTTime_GetMilliseconds(calintv));
            }
        } else {
            s32 calintvMs = lt_strtos32(argv[2], NULL, 10);
            if (calintvMs < 0) {
                calintv = LTTime_Infinite();
                SHL_iShell->Print(hShell, "Setting calibration interval to infinite\n");
            } else  if (calintvMs < 1000) {
                SHL_iShell->Print(hShell, "Invalid calibration interval\n");
                return -1;
            } else {
                calintv = LTTime_Milliseconds(calintvMs);
                SHL_iShell->Print(hShell, "Setting calibration interval to %lldms\n", LTTime_GetMilliseconds(calintv));
            }
            S.pMotorPanTilt->API->SetProperty(S.pMotorPanTilt, "calibrationInterval", &calintv);
        }
    } else if (lt_strcmp(argv[1], "sentry") == 0) {
        SHL_iShell->Print(hShell, "Setting sentry mode\n");
        LTMotorPanTilt_SentryPath *path = lt_malloc(sizeof(LTMotorPanTilt_SentryPath) + 3 * sizeof(LTMotorPanTilt_SentryWaypoint));
        path->numWaypoints = 3;
        path->waypoints[0] = (LTMotorPanTilt_SentryWaypoint){LTTime_Milliseconds(5000), { .pan =   0, .tilt =  0, .relative = false} };
        path->waypoints[1] = (LTMotorPanTilt_SentryWaypoint){LTTime_Milliseconds(5000), { .pan =  90, .tilt = 22, .relative = false} };
        path->waypoints[2] = (LTMotorPanTilt_SentryWaypoint){LTTime_Milliseconds(5000), { .pan = 180, .tilt = 44, .relative = false} };

        if (S.pMotorPanTilt->API->SetSentryPath(S.pMotorPanTilt, path)) {
            SHL_iShell->Print(hShell, "Sentry path set\n");
        } else {
            SHL_iShell->Print(hShell, "Failed to set sentry path\n");
        }
    } else if (lt_strcmp(argv[1], "sentrypause") == 0) {
        if (argc < 3) {
            SHL_iShell->Print(hShell, "Usage: pantilt sentrypause <1|0>\n");
            return -1;
        }
        if (lt_strcmp(argv[2], "0") == 0) {
            SHL_iShell->Print(hShell, "Resuming sentry mode\n");
            S.pMotorPanTilt->API->PauseSentryPath(S.pMotorPanTilt, false);
        } else {
            SHL_iShell->Print(hShell, "Pausing sentry mode\n");
            S.pMotorPanTilt->API->PauseSentryPath(S.pMotorPanTilt, true);
        }
    } else if (lt_strcmp(argv[1], "speed") == 0) {
        if (argc < 3) {
            SHL_iShell->Print(hShell, "Usage: pantilt speed <1-9>\n");
            return -1;
        }
        S.pMotorPanTilt->API->SetSpeed(S.pMotorPanTilt, lt_strtos32(argv[2], NULL, 10));
    } else {
        SHL_iShell->Print(hShell, "Unknown command: %s\n", argv[1]);
        return -1;
    }

    return 0;
}

static bool LTShellMotorImpl_ThreadStartProc(void) {
    LTMotorPanTilt_Position pos;

    S.pMotorPanTilt = lt_createobject(LTMotorPanTilt);
    if (!S.pMotorPanTilt) {
        LTLOG_REDALERT("noobj","Failed to create MotorPanTilt object");
        return false;
    }
    S.pMotorPanTilt->API->OpenMotor(S.pMotorPanTilt, kLTMotorPanTilt_Type_PanTilt);
    S.pMotorPanTilt->API->GetMinPosition(S.pMotorPanTilt, &pos);
    LTLOG("minpos", "Min position: %d %d", pos.pan, pos.tilt);
    S.pMotorPanTilt->API->GetMaxPosition(S.pMotorPanTilt, &pos);
    LTLOG("maxpos", "Max position: %d %d", pos.pan, pos.tilt);

    S.pMotorPanTilt->API->OnMotorMovementEvent(S.pMotorPanTilt, OnMovementEventProc, NULL);
    S.pMotorPanTilt->API->OnMotorStateChangeEvent(S.pMotorPanTilt, OnMotorStatusEventProc, NULL);
    S.pMotorPanTilt->API->OnSentryPathStateChangeEvent(S.pMotorPanTilt, OnSentryPathStateChangeEvent, NULL);
    return true;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "pantilt", SHL_PanTilt, "Pan Tilt motor command", NULL },
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellMotorImpl_LibFini(void) {
    S.iThread->Terminate(S.hThread);
    S.iThread->WaitUntilFinished(S.hThread, LTTime_Infinite());

    LT_GetLTSystemShell()->UnregisterCommands(SHL_Commands);
    lt_destroyobject(S.pMotorPanTilt);
}

static bool LTShellMotorImpl_LibInit(void) {
    S.pCore = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, S.pCore);
    S.hThread = S.pCore->CreateThread("LTShellMotor");
    S.iThread->Start(S.hThread, LTShellMotorImpl_ThreadStartProc, NULL);

    SHL_iShell = lt_getlibraryinterface(ILTShell, LT_GetLTSystemShell());

    LT_GetLTSystemShell()->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellMotor, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellMotor) LTLIBRARY_DEFINITION;
