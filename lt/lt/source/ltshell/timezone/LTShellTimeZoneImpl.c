/*******************************************************************************
 * lt/source/ltshell/timezone/LTShellTimeZoneImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 *******************************************************************************
 * LT Library providing shell command 'date'
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/system/timezone/LTSystemTimeZone.h>
#include <lt/system/shell/LTSystemShell.h>

/*________________________________
_/ LTShellTimeZoneImpl #defines */
#define DATE_COMMAND            "date"
#define DATE_PRINT_PREFIX       DATE_COMMAND ": time set to "

/*____________________________________
_/ LTShellTimeZoneImpl helper types */
typedef struct TimeOutputScratchBuffer {
    union {
        LTTimeBase  timebase;
        struct {
            LTTime  clockTimeUTC;
            LTTime  clockTimeLocal;
        };
    };
    LTCalendarTime  calendarTime;
    char            buff[128];
} TimeOutputScratchBuffer;

LT_STATIC_ASSERT_SIZE_32_64(TimeOutputScratchBuffer, 160, 160) // 16 + 16 + 128

typedef struct TimeResultClientData {
    LTShell           hShell;
} TimeResultClientData;

/*________________________________________
_/ LTShellTimeZoneImpl static constants */
static const char *                                  s_pDateFormat = "yyyymmddhhmmss";

/*________________________________________
_/ LTShellTimeZoneImpl static variables */
static LTSystemShell    * s_pLTSystemShell    = NULL;
static LTSystemTimeZone * s_pLTSystemTimeZone = NULL;
static LTCore           * s_pCore             = NULL;

/*______________________________
_/         __      __          /
      ____/ /___ _/ /____     /
     / __  / __ `/ __/ _ \   /
    / /_/ / /_/ / /_/  __/  /
LT> \__,_/\__,_/\__/\___/  /  command
  _________________________\______________________
_/ date command private static helper functions */
static u16 GetNumberFromDigits(const char ** p, u32 nDigits) { u16 n = 0; for (u32 i = nDigits; i; --i, ++*p) n = n * 10 + (**p - '0'); return n; }
    /**< Parses & returns value of nDigits number from *p, advancing p for next digit. ALL CHARS IN P MUST BE DIGITS. */

static bool ParseTimeInput(const char * pTimeInputString, LTCalendarTime * pCalendarTimeToSet) {
    if (lt_strlen(pTimeInputString) != lt_strlen(s_pDateFormat)) return false;       /* incorrect string length */
    const char * p = pTimeInputString; for (; *p ; ++p) if (!lt_isdigit(*p)) return false;  /* found a nondigit */
    p = pTimeInputString;
    pCalendarTimeToSet->nYear        = GetNumberFromDigits(&p, 4);
    pCalendarTimeToSet->nMonth       = GetNumberFromDigits(&p, 2);
    pCalendarTimeToSet->nDay         = GetNumberFromDigits(&p, 2);
    pCalendarTimeToSet->nHour        = GetNumberFromDigits(&p, 2);
    pCalendarTimeToSet->nMinute      = GetNumberFromDigits(&p, 2);
    pCalendarTimeToSet->nSecond      = GetNumberFromDigits(&p, 2);
    pCalendarTimeToSet->nMillisecond = 0;
    return true;
}

/*__________________
_/ date help proc */
static void LTShellTimeZoneCommand_DateHelp(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)s_pCore->GetHandleInterface(hShell);
    if (iShell && argc) iShell->Print(hShell,
        "usage: %s [--utc] [%s]\n"
        "       %s --settimezone <zone>\n"
        "       %s --dumptimezones\n", argv[0], s_pDateFormat, argv[0], argv[0]);
}

/*_____________________
_/ date command proc */
static int LTShellTimeZoneCommand_Date(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)s_pCore->GetHandleInterface(hShell);
    TimeOutputScratchBuffer * pScratchBuff = lt_malloc(sizeof(TimeOutputScratchBuffer));
    if (NULL == pScratchBuff) return iShell->Print(hShell, DATE_COMMAND ": failed to create scratch buffer\n"), -5;
    const char * pZoneID = NULL;
    int nRetVal = 0, nPos = 0;

    pScratchBuff->buff[0] = 0;

    if (argc == 1) {
        /* No args - print local time: */
        pScratchBuff->clockTimeLocal = s_pLTSystemTimeZone->GetClockTimeLocal(&pZoneID);
        nPos = s_pLTSystemTimeZone->ClockTimeToHumanReadableString(pScratchBuff->clockTimeLocal, true, pZoneID, pScratchBuff->buff, sizeof(pScratchBuff->buff));
    }
    else {
        if (0 == lt_strcmp(argv[1], "--utc")) {
            if (argc == 2) {
                /* no date arg - print UTC: */
                nPos = s_pLTSystemTimeZone->ClockTimeToHumanReadableString(s_pCore->GetClockTimeUTC(), false, "UTC", pScratchBuff->buff, sizeof(pScratchBuff->buff));
            }
            else {
                /* Attempt to parse the third arg as date input: */
                if (ParseTimeInput(argv[2], &pScratchBuff->calendarTime)) {
                    if (s_pLTSystemTimeZone->CalendarTimeToClockTime(&pScratchBuff->calendarTime, &pScratchBuff->timebase.secondaryClockTime)) {
                        pScratchBuff->timebase.primaryClockTime = s_pCore->GetKernelTime();
                        s_pCore->SetClockTimeBaseUTC(&pScratchBuff->timebase);
                        pScratchBuff->clockTimeUTC = s_pCore->GetClockTimeUTC();
                        lt_strncpyTerm(pScratchBuff->buff, DATE_PRINT_PREFIX, sizeof(pScratchBuff->buff));
                        nPos = sizeof(DATE_PRINT_PREFIX) - 1;
                        nPos += s_pLTSystemTimeZone->ClockTimeToHumanReadableString(pScratchBuff->clockTimeUTC, false, "UTC", pScratchBuff->buff + nPos, sizeof(pScratchBuff->buff) - nPos);
                    }
                    else nRetVal = -2; /* CalendarTimeToClockTime failed */
                }
                else nRetVal = -1;  /* ParseTimeInput failed */
            }
        }
        else if (0 == lt_strcmp(argv[1], "--settimezone")) {
            if (argc < 3) nRetVal = -1; /* missing time zone argument */
            else {
                s_pLTSystemTimeZone->SetSystemTimeZoneID(argv[2]);
                pZoneID = s_pLTSystemTimeZone->GetSystemTimeZoneID();
                if (pZoneID && (0 == lt_strcmp(argv[2], pZoneID))) {
                    nPos = lt_snprintf(pScratchBuff->buff, sizeof(pScratchBuff->buff)-2, "%s: time zone set to \"%s\"", argv[0], argv[2]);
                }
                else {
                    /* set of time zone failed */
                    nRetVal = -3;
                    nPos = lt_snprintf(pScratchBuff->buff, sizeof(pScratchBuff->buff)-2, "%s: unable to set time zone to \"%s\"", argv[0], argv[2]);
                }
            }
        }
        else if (0 == lt_strcmp(argv[1], "--dumptimezones")) {
            LTTimeZone const * pTimeZone = s_pLTSystemTimeZone->GetKnownTimeZones();
            if (pTimeZone) {
                iShell->Print(hShell, "%s: valid time zones are:\n", argv[0]);
                for (; pTimeZone->pZoneID; ++pTimeZone) iShell->Print(hShell, "     %8s     %s\n", pTimeZone->pZoneID, pTimeZone->pDescription);
            }
        }
        else if (0 == lt_strcmp(argv[1], "--help")) LTShellTimeZoneCommand_DateHelp(hShell, argc, argv);
        else {
            /* attempt to parse argv[1] as a local time setting */
            if (ParseTimeInput(argv[1], &pScratchBuff->calendarTime)) {
                if (s_pLTSystemTimeZone->CalendarTimeToClockTime(&pScratchBuff->calendarTime, &pScratchBuff->timebase.secondaryClockTime)) {
                    pScratchBuff->timebase.primaryClockTime = s_pCore->GetKernelTime();
                    pZoneID = s_pLTSystemTimeZone->GetSystemTimeZoneID();
                    if (pZoneID && (0 != lt_strcmp("UTC", pZoneID))) {
                        pScratchBuff->timebase.secondaryClockTime = s_pLTSystemTimeZone->ClockTimeLocalToUTC(pScratchBuff->timebase.secondaryClockTime, pZoneID);
                    }
                    s_pCore->SetClockTimeBaseUTC(&pScratchBuff->timebase);
                    pZoneID = NULL;
                    pScratchBuff->clockTimeLocal = s_pLTSystemTimeZone->GetClockTimeLocal(&pZoneID);
                    lt_strncpyTerm(pScratchBuff->buff, DATE_PRINT_PREFIX, sizeof(pScratchBuff->buff));
                    nPos = sizeof(DATE_PRINT_PREFIX) - 1;
                    nPos += s_pLTSystemTimeZone->ClockTimeToHumanReadableString(pScratchBuff->clockTimeLocal, true, pZoneID, pScratchBuff->buff + nPos, sizeof(pScratchBuff->buff) - nPos);
                }
                else nRetVal = -2; /* CalendarTimeToClockTime failed */
            }
            else nRetVal = -1;  /* ParseTimeInput failed */
        }

    }

    if (nPos) {
        pScratchBuff->buff[nPos++] = '\n';
        pScratchBuff->buff[nPos]   = 0;
        iShell->PutString(hShell, pScratchBuff->buff);
    }
    else {
        if (nRetVal != 0) LTShellTimeZoneCommand_DateHelp(hShell, argc, argv);
    }

    lt_free(pScratchBuff);

    return nRetVal;
}


/*___________________________________________
_/ LTShellTimeZoneImpl shell command table */
static const LTSystemShell_CommandDesc s_LTShellTimeZoneCommand[] = {
    { DATE_COMMAND,     LTShellTimeZoneCommand_Date,    "sets or displays the system time",        LTShellTimeZoneCommand_DateHelp },
};

/*______________________________________________
_/ LTShellTimeZoneImpl library initialization */
static void LTShellTimeZoneImpl_LibFini(void);
static bool LTShellTimeZoneImpl_LibInit(void) {
    s_pCore = LT_GetCore();
    if ((s_pLTSystemTimeZone = lt_openlibrary(LTSystemTimeZone)) && (s_pLTSystemShell = lt_openlibrary(LTSystemShell))) {
        s_pLTSystemShell->RegisterCommands(s_LTShellTimeZoneCommand, sizeof s_LTShellTimeZoneCommand / sizeof *s_LTShellTimeZoneCommand);
    } else return LTShellTimeZoneImpl_LibFini(), false;
    return true;
}

static void LTShellTimeZoneImpl_LibFini(void) {
    if (s_pLTSystemShell)    { s_pLTSystemShell->UnregisterCommands(s_LTShellTimeZoneCommand); lt_closelibrary(s_pLTSystemShell); s_pLTSystemShell = NULL; }
    if (s_pLTSystemTimeZone) { lt_closelibrary(s_pLTSystemTimeZone); s_pLTSystemTimeZone = NULL; }
    s_pCore = NULL;
}

/*_______________________________________
_/ LTShellTimeZoneImpl library binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellTimeZone, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(LTShellTimeZone)    LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Dec-21   constantine created
 *  03-Jun-22   augustus    added ntpdate; use LTSystemTimeZone for time string formatting
 */
