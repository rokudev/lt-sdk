/*******************************************************************************
 * lt/source/ltshell/suntime/LTShellSuntime.c
 *
 * LTUtilitySuntime Shell Application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/utility/suntime/LTUtilitySuntime.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/system/timezone/LTSystemTimeZone.h>
#include <lt/system/settings/LTSystemSettings.h>

DEFINE_LTLOG_SECTION("ltshell.suntime");

/** Standard LT Interfaces ****************************************************/
static struct Statics {
    LTUtilitySuntime    *pSuntime;
    LTSystemTimeZone    *pSystemTimeZone;
    LTSystemShell       *hShell;
} S;

static int LTShellSuntime_Sunrise(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc < 4) {
        iShell->Print(hShell, "Usage: sunrise <latitude> <longitude> <timezone offset in minutes>");
        return -1;
    }
    LTCalendarTime calendarTime;
    double latitude = lt_strtod(argv[1], NULL);
    double longitude = lt_strtod(argv[2], NULL);
    s32 tzOffset = (s32)lt_strtod(argv[3], NULL);
    LTTime clockTimeLocal = S.pSystemTimeZone->GetClockTimeLocal(NULL);
    LTTime time = S.pSuntime->GetSunriseTime(clockTimeLocal, latitude, longitude, tzOffset);
    S.pSystemTimeZone->ClockTimeToCalendarTime(time, &calendarTime);
    iShell->Print(hShell, "Sunrise Time: %02d:%02d\n", calendarTime.nHour, calendarTime.nMinute);
    return 0;
}

static int LTShellSuntime_Sunset(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc < 4) {
        iShell->Print(hShell, "Usage: sunset <latitude> <longitude> <timezone offset in minutes>");
        return -1;
    }
    LTCalendarTime calendarTime;
    double latitude = lt_strtod(argv[1  ], NULL);
    double longitude = lt_strtod(argv[2], NULL);
    s32 tzOffset = (s32)lt_strtod(argv[3], NULL);
    LTTime clockTimeLocal = S.pSystemTimeZone->GetClockTimeLocal(NULL);
    LTTime time = S.pSuntime->GetSunsetTime(clockTimeLocal, latitude, longitude, tzOffset);
    S.pSystemTimeZone->ClockTimeToCalendarTime(time, &calendarTime);
    iShell->Print(hShell, "Sunset Time: %02d:%02d\n", calendarTime.nHour, calendarTime.nMinute);
    return 0;
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static const LTSystemShell_CommandDesc s_commands[] = {
    { "sunrise", LTShellSuntime_Sunrise, "get sunrise clock time", NULL},
    { "sunset",  LTShellSuntime_Sunset,  "get sunset clock time",  NULL}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellSuntimeImpl_LibFini(void) {
    if (S.hShell) {
        S.hShell->UnregisterCommands(s_commands);
    }
    lt_closelibrary(S.hShell);
    lt_closelibrary(S.pSuntime);
    lt_closelibrary(S.pSystemTimeZone);
    S = (struct Statics) {};
}

static bool LTShellSuntimeImpl_LibInit(void) {
    do {
        if (!(S.pSuntime = lt_openlibrary(LTUtilitySuntime))) {
            LTLOG_YELLOWALERT("f.open.suntime", "failed to open suntime utility");
            break;
        }
        if (!(S.pSystemTimeZone = lt_openlibrary(LTSystemTimeZone))) {
            LTLOG_YELLOWALERT("f.open.timezone", "failed to open LTSystemTimeZone");
            break;
        }
        if (!(S.hShell = lt_openlibrary(LTSystemShell))) {
            LTLOG_YELLOWALERT("f.open.shell", "failed to open system shell");
            break;
        }
        S.hShell->RegisterCommands(s_commands, sizeof(s_commands) / sizeof(s_commands[0]));
        return true;
    } while (0);
    LTShellSuntimeImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellSuntime, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellSuntime) LTLIBRARY_DEFINITION;
