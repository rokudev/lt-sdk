/*******************************************************************************
 * lt/source/lt/system/timezone/LTSystemTimeZone.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/system/timezone/LTSystemTimeZone.h>
#include <lt/core/LTCore.h>

DEFINE_LTLOG_SECTION("sys.timezone")

/*_____________________________
 / LTSystemTimeZone #defines */
#define LTSYSTEMTIMEZONE_DEFAULT_TIMEZONEID           "PST"
#define LTSTZ_DEFAULT_TIME_ZONE_INDEX                 3 /* PST */
#define LTSYSTEMTIMEZONE_DEFAULT_DSTPERIOD            "M3.2.0/2:00,M11.1.0/2:00"
#define LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE           60
#define LTSYSTEMTIMEZONE_SECONDS_PER_HOUR             3600
#define LTSYSTEMTIMEZONE_SECONDS_PER_DAY              86400
#define LTSYSTEMTIMEZONE_MILLISECONDS_PER_SECOND      1000
#define LTSYSTEMTIMEZONE_NANOSECONDS_PER_MILLISECOND  1000000
#define LTSYSTEMTIMEZONE_NANOSECONDS_PER_SECOND       1000000000
#define LTSYSTEMTIMEZONE_NANOSECONDS_PER_HOUR         3600000000000

/*_____________________________________
 / LTSystemTimeZone forward declarations */
static const LTTimeZone * LTSystemTimeZone_GetTimeZoneFromID(const char * pZoneID);
static void  LTSystemTimeZone_ClockTimeToCalendarTime(LTTime clockTime, LTCalendarTime * pCalendarTimeToSet);
static bool  LTSystemTimeZone_CalendarTimeToClockTime(const LTCalendarTime * pCalendarTime, LTTime * pClockTimeToSet);

/*_____________________________________
 / LTSystemTimeZone static variables */
static LTAtomic                                 s_timeZoneIndex    = { 3 }; /* 3 is PST! */
static LTMutex                                 *s_mutex            = NULL;

enum {
    kLTSystemTimeZone_MaxZoneIDLen            = 12,
    kLTSystemTimeZone_MaxZoneNameLen          = 48,
    kLTSystemTimeZone_MaxZonePosixTZLen       = 72,
    kLTSystemTimeZone_MaxZoneDescriptionLen   = 64,
};

static struct UserZoneString {
    char posixTZString[kLTSystemTimeZone_MaxZonePosixTZLen];    /* For LTTimeZone.pZoneData */
    char nameSTD[kLTSystemTimeZone_MaxZoneIDLen];               /* For LTTimeZone.pAbbreviationSTD and pNameSTD */
    char nameDST[kLTSystemTimeZone_MaxZoneIDLen];               /* For LTTimeZone.pAbbreviationDST and pNameDST */
    char olsonName[kLTSystemTimeZone_MaxZoneNameLen];           /* For LTTimeZone.pOlsonReferent */
    char description[kLTSystemTimeZone_MaxZoneDescriptionLen];  /* For LTTimeZone.pNameGeneric and pDescription */
} s_userZoneString;

/* For quick reference to pre-calculated data. Product using this feature must update these data on its own. */
const LTTimeZone *s_referenceTimeZone;   /* If set, then the reference data is valid. */
static struct UserZoneReference {
    LTTime utcOffsetSTD;       /* Standard timezone offset */
    LTTime utcOffsetDST;       /* Daylight timezone offset */
    LTTime utcDSTStart;        /* Daylight start time */
    LTTime utcDSTEnd;          /* Daylight end time */
} s_userReferenceData;

/*_____________________________________
 / LTSystemTimeZone static constants */
static const u32                                s_daysInMonth[] =         { 31, 28, 31, 30,  31,  30,  31,  31,  30,  31,  30,  31 };
static const u32                                s_daysInMonthLeapYear[] = { 31, 29, 31, 30,  31,  30,  31,  31,  30,  31,  30,  31 };
static const u32                                s_daysInYear[] =          {  0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
static const LTTimeZone                         s_timeZones[] =
{   /*  ______________  ________________                                 ____________________  ____________________  ____________                             ____________                     _________________   ___________________   _____________________
       |LT TimeZone ID | POSIX.1 TZ DATA                                | UI ABBREVIATION-STD | UI ABBREVIATION-DST | UI NAME-STD                            | UI NAME-DST                    | UI NAME - SHORT   | OLSON DATABASE REF  | UI DESCRIPTION     /
       pZoneID         pZoneData                                        pAbbreviationSTD      pAbbreviationDST      pNameSTD                                 pNameDST                         pNameGeneric        pOlsonReferent        pDescription       */

	/* US rules: DST begins at 2am local time (without DST) on the 2nd Sunday in March and ends at 2am local time (with DST) on the 1st Sunday in November. */
    { "EST",          "EST5EDT4,M3.2.0/2:00,M11.1.0/2:00",              "EST",                "EDT",                "Eastern Standard Time",                 "Eastern Daylight Time",         "Eastern",          "US/Eastern",         "US Eastern Time" },
    { "CST",          "CST6CDT5,M3.2.0/2:00,M11.1.0/2:00",              "CST",                "CDT",                "Central Standard Time",                 "Central Daylight Time",         "Central",          "US/Central",         "US Central Time" },
    { "MST",          "MST7MDT6,M3.2.0/2:00,M11.1.0/2:00",              "MST",                "MDT",                "Mountain Standard Time",                "Mountain Daylight Time",        "Mountain",         "US/Mountain",        "US Mountain Time" },
    { "PST",          "PST8PDT7,M3.2.0/2:00,M11.1.0/2:00",              "PST",                "PDT",                "Pacific Standard Time",                 "Pacific Daylight Time",         "Pacific",          "US/Pacific",         "US Pacific Time" },
    { "AKST",         "AKST9AKDT8,M3.2.0/2:00,M11.1.0/2:00",            "AKST",               "AKDT",               "Alaska Standard Time",                  "Alaska Daylight Time",          "Alaska",           "US/Alaska",          "Alaska Time" },
    { "HST",          "HST10",                                          "HST",                "",                   "Hawaiian Standard Time",                "",                              "Hawaii",           "US/Hawaii",          "Hawaii-Aleutian Time with no Daylight Saving (Hawaii)" },
    { "HST1",         "HST10HST9",                                      "HAST",               "HADT",               "Hawaii-Aleutian Standard Time",         "Hawaii-Aleutian Daylight Time", "Aleutian",         "US/Aleutian",        "Hawaii-Aleutian Time with Daylight Saving" },
    { "MST1",         "MST7",                                           "MST",                "",                   "Mountain Standard Time",                "",                              "Arizona",          "US/Arizona",         "US MT without Daylight Saving Time (Arizona)" },
    { "EST1",         "EST5",                                           "EST",                "",                   "Eastern Standard Time",                 "",                              "East Indiana",     "US/East-Indiana",    "US ET without Daylight Saving Time (East Indiana)" },
    { "AST",          "AST4ADT3,M3.2.0/2:00,M11.1.0/2:00",              "AST",                "ADT",                "Atlantic Standard Time",                "Atlantic Daylight Time",        "Halifax",          "America/Halifax",    "Atlantic Time" },
    { "CST2",         "CST6/CDT5,M4.1.0/2:00,M10.5.0/2:00",             "CST",                "CDT",                "Central Standard Time",                 "Central Daylight Time",         "Mountain",         "Mexico/Central",     "Mexico (Mexico City)" },
    { "MST2",         "MST7/MDT6,M4.1.0/2:00,M10.5.0/2:00",             "MST",                "MDT",                "Mountain Standard Time",                "Mountain Daylight Time",        "Mountain",         "Mexico/Mountain",    "Mexico (Chihuahua)" },
    { "PST2",         "PST8PDT7,M4.1.0/2:00,M10.5.0/2:00",              "PST",                "PDT",                "Pacific Standard Time",                 "Pacific Daylight Time",         "Pacific",          "Mexico/Pacific",     "Mexico (Tijuana)" },
    { "BRT",          "BRT3BRST2,M10.3.0/0:00,M2.3.0/0:00",             "BRT",                "BRST",               "Brazil Time",                           "Brazil Summer Time",            "Brazil",           "America/Sao_Paulo",  "Brazil Time (Sao Paulo)" },
    { "NST",          "NST3:30NDT2:30,M3.2.0/0:01,M11.1.0/0:01",        "NST",                "NDT",                "Newfoundland Standard Time",            "Newfoundland Daylight Time",    "Newfoundland",     "America/St_Johns",   "Newfoundland Time"  },
    { "AZOT",         "AZOT1AZOST0,M3.5.0/1:00,M10.5.0/1:00",           "AZOT",               "AZOST",              "Azores Time",                           "Azores Summer Time",            "Azores",           "Atlantic/Azores",    "Azores Time" },

	/* European rules: DST begins at 1am UTC on the last Sunday in March and ends at 1am UTC on the last Sunday in October */
    { "GMTBST",       "GMT-0BST-1,M3.5.0/1:00,M10.5.0/2:00",            "GMT",                "BST",                "Greenwich Mean Time",                   "British Summer Time",           "London",           "London",             "London/Dublin Time" },
    { "WET",          "WET-0WEST-1,M3.5.0/1:00,M10.5.0/2:00",           "WET",                "WEST",               "Western European Time",                 "Western European Summer Time",  "Western Europe",   "WET",                "Western European Time" },
    { "CET",          "CET-1CEST-2,M3.5.0/2:00,M10.5.0/3:00",           "CET",                "CEST",               "Central European Time",                 "Central European Summer Time",  "Central Europe",   "CET",                "Central European Time" },
    { "EET",          "EET-2EEST-3,M3.5.0/3:00,M10.5.0/4:00",           "EET",                "EEST",               "Eastern European Time",                 "Eastern European Summer Time",  "Eastern Europe",   "EET",                "Eastern European Time" },
    { "MSK",          "MSK-3MSK-4,M3.5.0/2:00,M10.5.0/2:00",            "MSK",                "MSD",                "Moscow Time",                           "Moscow Daylight Time",          "Moscow",           "Europe/Moscow",      "Moscow Time" },
    { "SAMT",         "SAMT-4SAMST-5,M3.5.0/2:00,M10.5.0/2:00",         "SAMT",               "SAMST",              "Samara Time",                           "Samara Summer Time",            "Samara",           "Europe/Samara",      "Delta Time Zone (Samara)" },
    { "YEKT",         "YEKT-5YEKST-6,M3.5.0/2:00,M10.5.0/2:00",         "YEKT",               "YEKST",              "Yekaterinburg Time",                    "Yekaterinburg Summer Time",     "Yekaterinburg",    "Asia/Yekaterinburg", "Echo Time Zone (Yekaterinburg)" },
    { "IST",          "IST-5:30",                                       "IST",                "",                   "Indian Standard Time",                  "",                              "India",            "India",              "Indian Standard Time" },
    { "NPT",          "NPT-5:45",                                       "NPT",                "",                   "Nepal Time",                            "",                              "Nepal",            "Nepal",              "Nepal Time" },
    { "OMST",         "OMST-6OMSST-7,M3.5.0/2:00,M10.5.0/2:00",         "OMST",               "OMSST",              "Omsk Time",                             "Omsk Summer Time",              "Omsk",             "Asia/Omsk",          "Foxtrot Time Zone (Omsk)" },
    { "JST",          "JST-9",                                          "JST",                "",                   "Japanese Standard Time",                "",                              "Japan",            "Japan",              "Japanese Standard Time" },
    { "CXT",          "CXT-7",                                          "CXT",                "",                   "Christmas Island Time",                 "",                              "Christmas Island", "Indian/Christmas",   "Christmas Island Time (Australia)" },
    { "AWST",         "WST-8WDT-9,M10.5.0/2:00,M3.5.0/2:00",            "WST",                "WDT",                "Western Standard Time",                 "Western Daylight Time",         "Perth",            "Australia/Perth",    "Australian Western Time" },
    { "AWST1",        "WST-8",                                          "WST",                "",                   "Western Standard Time",                 "",                              "Perth",            "Australia/Perth",    "Australian Western Time without Daylight Saving Time" },
    { "ACST",         "CST-9:30CDT-10:30,M10.5.0/2:00,M3.5.0/2:00",     "CST",                "CDT",                "Central Standard Time",                 "Central Daylight Time",         "Adelaide",         "Australia/Adelaide", "Australian Central Time" },
    { "ACST1",        "CST-9:30",                                       "CST",                "CDT",                "Central Standard Time",                 "",                              "Darwin",           "Australia/Darwin",   "Australian Central Time without Daylight Saving Time (Darwin)" },
    { "AEST",         "EST-10DST-11,M10.5.0/2:00,M3.5.0/2:00",          "EST",                "EDT",                "Eastern Standard Time",                 "Eastern Daylight Time",         "Sydney",           "Australia/Sydney",   "Australian Eastern Time" },
    { "AEST1",        "EST-10",                                         "EST",                "",                   "Eastern Standard Time",                 "",                              "Brisbane",         "Australia/Brisbane", "Australian Eastern Time without Daylight Saving Time (Brisbane)" },
    { "NFT",          "NFT-11:30",                                      "NFT",                "",                   "Norfolk Time",                          "",                              "Norfolk",          "Pacific/Norfolk",    "Norfolk (Island) Time (Australia)" },
    { "NZST",         "NZST-12NZDT-13,M9.5.0/2:00,M4.1.0/3:00",         "NZST",               "NZDT",               "New Zealand Standard Time",             "New Zealand Daylight Time",     "Auckland",         "Pacific/Auckland",   "New Zealand Time (Auckland)"  },
    { "CHAST",        "CHAST-12:45CHADT-13:15,M9.5.0/2:45,M4.1.0/2:45", "CHAST",              "CHADT",              "Chatham Standard Time",                 "Chatham Daylight Time",         "Chatham",          "Pacific/Chatham",    "Chatham Time Zone" },
    { "FJT",          "FJT-12",                                         "FJT",                "",                   "Fiji Time",                             "",                              "Fiji",             "Pacific/Fiji",       "Yankee Time Zone (Fiji)" },
    { "SST",          "SST11",                                          "SST",                "",                   "Pago Pago Time",                        "",                              "Pago Pago",        "Pacific/Pago_Pago",  "X-ray Time Zone (Pago Pago)" },

	/* UTC rules: yes these really are right - the TZ environment variable form is backwards (cf PST and CET) */
    { "UTC",          "UTC-0",                                          "UTC",                "",                   "Universal Coordinated Time",            "",                              "UTC",              "UTC",                "Universal Coordinated Time" },
    { "UTC+1",        "UTC-1",                                          "UTC+1",              "",                   "Universal Coordinated Time + 1 hour",   "",                              "UTC+1",            "",                   "1 hour ahead of Universal Coordinated Time" },
    { "UTC+2",        "UTC-2",                                          "UTC+2",              "",                   "Universal Coordinated Time + 2 hours",  "",                              "UTC+2",            "",                   "2 hours ahead of Universal Coordinated Time" },
    { "UTC+3",        "UTC-3",                                          "UTC+3",              "",                   "Universal Coordinated Time + 3 hours",  "",                              "UTC+3",            "",                   "3 hours ahead of Universal Coordinated Time" },
    { "UTC+4",        "UTC-4",                                          "UTC+4",              "",                   "Universal Coordinated Time + 4 hours",  "",                              "UTC+4",            "",                   "4 hours ahead of Universal Coordinated Time" },
    { "UTC+5",        "UTC-5",                                          "UTC+5",              "",                   "Universal Coordinated Time + 5 hours",  "",                              "UTC+5",            "",                   "5 hours ahead of Universal Coordinated Time" },
    { "UTC+6",        "UTC-6",                                          "UTC+6",              "",                   "Universal Coordinated Time + 6 hours",  "",                              "UTC+6",            "",                   "6 hours ahead of Universal Coordinated Time" },
    { "UTC+7",        "UTC-7",                                          "UTC+7",              "",                   "Universal Coordinated Time + 7 hours",  "",                              "UTC+7",            "",                   "7 hours ahead of Universal Coordinated Time" },
    { "UTC+8",        "UTC-8",                                          "UTC+8",              "",                   "Universal Coordinated Time + 8 hours",  "",                              "UTC+8",            "",                   "8 hours ahead of Universal Coordinated Time" },
    { "UTC+9",        "UTC-9",                                          "UTC+9",              "",                   "Universal Coordinated Time + 9 hours",  "",                              "UTC+9",            "",                   "9 hours ahead of Universal Coordinated Time" },
    { "UTC+10",       "UTC-10",                                         "UTC+10",             "",                   "Universal Coordinated Time + 10 hours", "",                              "UTC+10",           "",                   "10 hours ahead of Universal Coordinated Time" },
    { "UTC+11",       "UTC-11",                                         "UTC+11",             "",                   "Universal Coordinated Time + 11 hours", "",                              "UTC+11",           "",                   "11 hours ahead of Universal Coordinated Time" },
    { "UTC+12",       "UTC-12",                                         "UTC+12",             "",                   "Universal Coordinated Time + 12 hours", "",                              "UTC+12",           "",                   "12 hours ahead of Universal Coordinated Time" },
    { "UTC+13",       "UTC-13",                                         "UTC+13",             "",                   "Universal Coordinated Time + 13 hours", "",                              "UTC+13",           "",                   "13 hours ahead of Universal Coordinated Time" },
    { "UTC+14",       "UTC-14",                                         "UTC+14",             "",                   "Universal Coordinated Time + 14 hours", "",                              "UTC+14",           "",                   "14 hours ahead of Universal Coordinated Time" },
    { "UTC-1",        "UTC+1",                                          "UTC-1",              "",                   "Universal Coordinated Time - 1 hour",   "",                              "UTC-1",            "",                   "1 hour behind Universal Coordinated Time" },
    { "UTC-2",        "UTC+2",                                          "UTC-2",              "",                   "Universal Coordinated Time - 2 hours",  "",                              "UTC-2",            "",                   "2 hours behind Universal Coordinated Time" },
    { "UTC-3",        "UTC+3",                                          "UTC-3",              "",                   "Universal Coordinated Time - 3 hours",  "",                              "UTC-3",            "",                   "3 hours behind Universal Coordinated Time" },
    { "UTC-4",        "UTC+4",                                          "UTC-4",              "",                   "Universal Coordinated Time - 4 hours",  "",                              "UTC-4",            "",                   "4 hours behind Universal Coordinated Time" },
    { "UTC-5",        "UTC+5",                                          "UTC-5",              "",                   "Universal Coordinated Time - 5 hours",  "",                              "UTC-5",            "",                   "5 hours behind Universal Coordinated Time" },
    { "UTC-6",        "UTC+6",                                          "UTC-6",              "",                   "Universal Coordinated Time - 6 hours",  "",                              "UTC-6",            "",                   "6 hours behind Universal Coordinated Time" },
    { "UTC-7",        "UTC+7",                                          "UTC-7",              "",                   "Universal Coordinated Time - 7 hours",  "",                              "UTC-7",            "",                   "7 hours behind Universal Coordinated Time" },
    { "UTC-8",        "UTC+8",                                          "UTC-8",              "",                   "Universal Coordinated Time - 8 hours",  "",                              "UTC-8",            "",                   "8 hours behind Universal Coordinated Time" },
    { "UTC-9",        "UTC+9",                                          "UTC-9",              "",                   "Universal Coordinated Time - 9 hours",  "",                              "UTC-9",            "",                   "9 hours behind Universal Coordinated Time" },
    { "UTC-10",       "UTC+10",                                         "UTC-10",             "",                   "Universal Coordinated Time - 10 hours", "",                              "UTC-10",           "",                   "10 hours behind Universal Coordinated Time" },
    { "UTC-11",       "UTC+11",                                         "UTC-11",             "",                   "Universal Coordinated Time - 11 hours", "",                              "UTC-11",           "",                   "11 hours behind Universal Coordinated Time" },
    { "UTC-12",       "UTC+12",                                         "UTC-12",             "",                   "Universal Coordinated Time - 12 hours", "",                              "UTC-12",           "",                   "12 hours behind Universal Coordinated Time" },
    { "UTC-13",       "UTC+13",                                         "UTC-13",             "",                   "Universal Coordinated Time - 13 hours", "",                              "UTC-13",           "",                   "13 hours behind Universal Coordinated Time" },
    { "UTC-14",       "UTC+14",                                         "UTC-14",             "",                   "Universal Coordinated Time - 14 hours", "",                              "UTC-14",           "",                   "14 hours behind Universal Coordinated Time" },
    { "User",         s_userZoneString.posixTZString,         s_userZoneString.nameSTD, s_userZoneString.nameDST,   s_userZoneString.nameSTD,       s_userZoneString.nameDST,  s_userZoneString.description, s_userZoneString.olsonName, s_userZoneString.description },
    {  NULL,           NULL,                                             NULL,                 NULL,                 NULL,                                    NULL,                            NULL,               NULL,                 NULL },
};

/*_____________________________________
 / LTSystemTimeZone inline functions */
LT_INLINE bool LTSystemTimeZone_IsLeapYear(u32 nYear) {
    return (nYear % 4 ? false : (nYear % 100 ? true : (nYear % 400 ? false : true)));
}

static u32
LTSystemTimeZone_GetTimeZoneIndexFromID(const char * pZoneID) {
    if (pZoneID && *pZoneID) {
        const LTTimeZone * pTimeZone = s_timeZones;
        u32 index = 0;
        while (pTimeZone->pZoneID) {
            if (0 == lt_strcmp(pTimeZone->pZoneID, pZoneID)) return index;
            pTimeZone++; index++;
        }
    }
    return LTSTZ_DEFAULT_TIME_ZONE_INDEX;
}

LT_INLINE const LTTimeZone * LTSystemTimeZone_GetSystemTimeZone(void) {
    u32 index = LTAtomic_Load(&s_timeZoneIndex);
    if (index >= (sizeof(s_timeZones) / sizeof(s_timeZones[0]))) index = LTSTZ_DEFAULT_TIME_ZONE_INDEX;
    return &s_timeZones[index];
}

LT_INLINE const LTTimeZone * LTSystemTimeZone_FindOrGetSystemTimeZone(const char * pZoneID) {
    const LTTimeZone * pZone = LTSystemTimeZone_GetTimeZoneFromID(pZoneID);
    return pZone ? pZone : LTSystemTimeZone_GetSystemTimeZone();
}

/*_____________________________________________
 / LTSystemTimeZone private static functions */

/* Starting with POSIX.1-2001, std and dst may also be in a quoted form like '<+09>'; this allows "+" and "-" in the names. */
static void
LTSystemTimeZone_AdvancePastZoneName(const char ** ppZoneInfo)
{
    const char * p = *ppZoneInfo;
    if (*p != '<') {
        while (*p) {
            if (((*p >= '0') && (*p <= '9')) || (*p == '-') || (*p == '+') || (*p == ',') || (*p == ':')) { break; }
            p++;
        }

    } else {
        while (*p) {
            if (*p == '>') { p++; break; }
            p++;
        }
    }
    *ppZoneInfo = p;
}

static void
LTSystemTimeZone_ReadZoneHMSAndAdvance(const char ** ppZoneInfo, u32 * pnHoursToSet, u32 * pnMinutesToSet, u32 * pnSecondsToSet, bool * pbNegativeToSet)
{
    const char * p = *ppZoneInfo;
    *pnHoursToSet = *pnMinutesToSet = *pnSecondsToSet = 0;
    *pbNegativeToSet = (*p == '-');
    if (*pbNegativeToSet || (*p == '+')) p++;

    while (*p >= '0' && *p <= '9') { *pnHoursToSet = (10 * (*pnHoursToSet)) + (*p - '0'); p++; }
    if (':' == *p) {
        p++;
        while (*p >= '0' && *p <= '9') { *pnMinutesToSet = (10 * (*pnMinutesToSet)) + (*p - '0'); p++; }
        if (':' == *p) {
            p++;
            while (*p >= '0' && *p <= '9') { *pnSecondsToSet = (10 * (*pnSecondsToSet)) + (*p - '0'); p++; }
       }
    }
    *ppZoneInfo = p;
}

static s64
LTSystemTimeZone_GetTimeZoneUTCOffsetNanoseconds(const LTTimeZone * pTimeZone)
{
    const char * pZone = pTimeZone->pZoneData;
    u32 nHours = 0, nMinutes = 0, nSeconds = 0;
    bool bNegative = false;
    LTSystemTimeZone_AdvancePastZoneName(&pZone);
    LTSystemTimeZone_ReadZoneHMSAndAdvance(&pZone, &nHours, &nMinutes, &nSeconds, &bNegative);
    s64 nOffset = nHours * LTSYSTEMTIMEZONE_SECONDS_PER_HOUR + nMinutes * LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE + nSeconds;
    nOffset *= LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_SECOND);
    if (false == bNegative) nOffset = (0 - nOffset); /* positive string offsets, e.g. PST8, represent negative numerical offsets (e.g. 8 hours behind UTC) */
    return nOffset;
}

static bool
LTSystemTimeZone_GetDSTDateAndAdvance(const char ** pZoneInfo, u16 nDSTMatchYear, LTCalendarTime * pCalendarTimeToSet)
{
    #define LTSYSTEMTIMEZONE_GET_VALUE(pointer, nValue) nValue = 0; for (;  *pointer >= '0' && *pointer <= '9';  pointer++) nValue = (10 * nValue) + (*pointer) - '0'

    bool bGood = false;
    const char * p = *pZoneInfo;
    lt_memset(pCalendarTimeToSet, 0, sizeof(*pCalendarTimeToSet));
    pCalendarTimeToSet->nYear = nDSTMatchYear;

    if (*p == 'M')
    {
        u32 nMonth = 0;
        u32 nWhichWeek = 0;
        u32 nWeekday = 0;
        p++;
        LTSYSTEMTIMEZONE_GET_VALUE(p, nMonth);
        if (nMonth >= 1 && nMonth <= 12 && *p == '.')
        {
            p++;
            LTSYSTEMTIMEZONE_GET_VALUE(p, nWhichWeek);
            if (nWhichWeek >= 1 && nWhichWeek <= 5 && *p == '.')
            {
                p++;
                LTSYSTEMTIMEZONE_GET_VALUE(p, nWeekday);
                if (nWeekday <= 6)
                {
                    // nMonth is good
                    pCalendarTimeToSet->nMonth = (u16)nMonth;
                    // figure out what day in the month based on which week and weekday
                    // we do this by figuring out first what weekday the first of this month is
                    u32 nDays = 0;
                    for (u32 nYear = 1970; nYear < (u32)nDSTMatchYear; nYear++) nDays += (LTSystemTimeZone_IsLeapYear(nYear) ? 366 : 365);
                    nDays += s_daysInYear[nMonth-1];
                    if (nMonth > 2 && LTSystemTimeZone_IsLeapYear(nDSTMatchYear)) nDays++;
                    u32 nWeekdayFirstDay = (nDays + 4) % 7;
                    // now figure out where the nth weekday falls
                    u32 nDay = ((nWeekday + 7 - nWeekdayFirstDay) % 7) + (7 * (nWhichWeek-1)) + 1;
                    const u32 * pDaysInMonth = (LTSystemTimeZone_IsLeapYear(nDSTMatchYear) ? s_daysInMonthLeapYear: s_daysInMonth);
                    u32 nDaysInMonth = pDaysInMonth[nMonth-1];
                    while (nDay > nDaysInMonth) nDay -= 7;
                    // phew! got the day
                    pCalendarTimeToSet->nDay = (u16)nDay;
                    bGood = true;
                }
            }
        }
    }
    else if (*p == 'J')
    {
        u32 nMonth = 1;
        u32 nDay = 0;
        p++;
        LTSYSTEMTIMEZONE_GET_VALUE(p, nDay);
        if (nDay > 0 && nDay < 366)
        {
            // convert nDay down to a month and day
            for (; nMonth < 13; nMonth++)
            {
                u32 nDaysInMonth = s_daysInMonth[nMonth-1]; // don't use leap year
                if (nDay <= nDaysInMonth) break;
                nDay -= nDaysInMonth;
            }
            if (nMonth < 13)
            {
                pCalendarTimeToSet->nMonth = (u16)nMonth;
                pCalendarTimeToSet->nDay = (u16)nDay;
            }
        }
    }
    else
    {
        u32 nMonth = 1;
        u32 nDay = 0;
        LTSYSTEMTIMEZONE_GET_VALUE(p, nDay);
        nDay++;
        if (nDay > 0 && nDay < 367) // 366 (leap year) allowed
        {
            // convert nDay down to a month and day
            const u32 * pDaysInMonth = (LTSystemTimeZone_IsLeapYear(nDSTMatchYear) ? s_daysInMonthLeapYear: s_daysInMonth);
            for (; nMonth < 13; nMonth++)
            {
                u32 nDaysInMonth = pDaysInMonth[nMonth-1];
                if (nDay <= nDaysInMonth) break;
                nDay -= nDaysInMonth;
            }
            if (nMonth < 13)
            {
                pCalendarTimeToSet->nMonth = (u16)nMonth;
                pCalendarTimeToSet->nDay = (u16)nDay;
                bGood = true;
            }
        }
    }

    // we've got year, month, day
    // now get the (optional) time fields
    // p is sitting on a slash, a ',' or a 0
    if (bGood)
    {
        if (*p == '/')
        {
            // we have a time spec
            p++;
            u32 nHour = 0, nMinute = 0, nSecond = 0;
            bool bNegative = false;
            LTSystemTimeZone_ReadZoneHMSAndAdvance(&p, &nHour, &nMinute, &nSecond, &bNegative);
            LT_ASSERT(false == bNegative);
            pCalendarTimeToSet->nHour = (u16)nHour;
            pCalendarTimeToSet->nMinute = (u16)nMinute;
            pCalendarTimeToSet->nSecond = (u16)nSecond;
        }
        else
        {
            // no time spec, assume the default of two am
            pCalendarTimeToSet->nHour = 2;
        }
    }

    *pZoneInfo = p;

    return bGood;
}

static bool
LTSystemTimeZone_DSTIsInEffect(LTTime time, const char * pZoneInfo, s64 nDstOffset)
{
    const char * p = pZoneInfo; /* pZoneInfo points to the DST period rule part of a zone's pZoneData, starting with the comma */
    if (*p == ',') p++;
    else p = LTSYSTEMTIMEZONE_DEFAULT_DSTPERIOD; /* no rule; assume the default US rule */

    LTTime timeDSTStart;
    LTTime timeDSTEnd;
    LTCalendarTime calendarTimeDSTStart;
    LTCalendarTime calendarTimeDSTEnd;

    /* convert the LTTime in question to calendarTime to extract the year; use calendarTimeDSTEnd as a temporary variable*/
    LTSystemTimeZone_ClockTimeToCalendarTime(time, &calendarTimeDSTEnd); // convert to calendar time for comparison

    /* read the calendar time from the zone data */
    if (! LTSystemTimeZone_GetDSTDateAndAdvance(&p, calendarTimeDSTEnd.nYear, &calendarTimeDSTStart)) return false;
    if (*p++ != ',') return false;
    if (! LTSystemTimeZone_GetDSTDateAndAdvance(&p, calendarTimeDSTStart.nYear, &calendarTimeDSTEnd)) return false;

    /* convert the calendar times to LTTime */
    LTSystemTimeZone_CalendarTimeToClockTime(&calendarTimeDSTStart, &timeDSTStart);
    LTSystemTimeZone_CalendarTimeToClockTime(&calendarTimeDSTEnd, &timeDSTEnd);

	/* The POSIX 1003.1 rule format states that the end time of DST is in local
       time (including with DST) so we have to apply the DST offset to the end time. */
	if (nDstOffset > 0) timeDSTEnd.nNanoseconds -= nDstOffset;
	else timeDSTEnd.nNanoseconds += (0 - nDstOffset);

    /* return true if DST start <= time in question < DST end, false otherwise */
    if (timeDSTStart.nNanoseconds > timeDSTEnd.nNanoseconds) return (time.nNanoseconds < timeDSTEnd.nNanoseconds || time.nNanoseconds >= timeDSTStart.nNanoseconds);
    else return (time.nNanoseconds >= timeDSTStart.nNanoseconds && time.nNanoseconds < timeDSTEnd.nNanoseconds);
}

static s64
LTSystemTimeZone_GetTimeZoneDSTAdjustmentOffsetNanoseconds(LTTime time, const LTTimeZone * pTimeZone)
{   /* returns pTimeZone's DST adjustment offset if time falls in a DST observing period of pTimeZone, 0 otherwise. */

    if (0 == *pTimeZone->pNameDST) return 0; /* empty pNameDST means the zone doesn't observe DST */

    s64 nOffset = 0;
    const char * pZone = pTimeZone->pZoneData;
    u32 nHoursUTC = 0, nMinutesUTC = 0, nSecondsUTC = 0;
    u32 nHoursDST = 0, nMinutesDST = 0, nSecondsDST = 0;
    bool bNegativeUTC = false, bNegativeDST = false;
    LTSystemTimeZone_AdvancePastZoneName(&pZone);
    LTSystemTimeZone_ReadZoneHMSAndAdvance(&pZone, &nHoursUTC, &nMinutesUTC, &nSecondsUTC, &bNegativeUTC);
    LTSystemTimeZone_AdvancePastZoneName(&pZone);
    LTSystemTimeZone_ReadZoneHMSAndAdvance(&pZone, &nHoursDST, &nMinutesDST, &nSecondsDST, &bNegativeDST);

    if ((0 == nHoursDST) && (0 == nMinutesDST) && (0 == nSecondsDST)) {
        /* DST was not specified, default to one hour forward */
        nOffset = LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_HOUR);
    }
    else {
        /* we got a DST specification, make the offset the difference between the UTC and DST offsets */
        s64 nOffsetUTC = nHoursUTC * LTSYSTEMTIMEZONE_SECONDS_PER_HOUR + nMinutesUTC * LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE + nSecondsUTC;
        nOffsetUTC *= LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_SECOND);
        if (false == bNegativeUTC) nOffsetUTC = (0 - nOffsetUTC);
        s64 nOffsetDST = nHoursDST * LTSYSTEMTIMEZONE_SECONDS_PER_HOUR + nMinutesDST * LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE + nSecondsDST;
        nOffsetDST *= LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_SECOND);
        if (false == bNegativeDST) nOffsetDST = (0 - nOffsetDST);
        nOffset = nOffsetDST - nOffsetUTC;
    }

    // ok, we have a dst offset, now we have to see if dst is in effect
    return LTSystemTimeZone_DSTIsInEffect(time, pZone, nOffset) ? nOffset : 0;
}

static void
LTSystemTimeZone_ConvertUTCToLocalTimeWithZone(LTTime * pTimeUTC, const LTTimeZone * pZone) {
    LT_ASSERT(NULL != pTimeUTC && NULL != pZone);
    s64 nOffset = LTSystemTimeZone_GetTimeZoneUTCOffsetNanoseconds(pZone);
    if (nOffset < 0) pTimeUTC->nNanoseconds -= (0 - nOffset);
    else pTimeUTC->nNanoseconds += nOffset;

    nOffset = LTSystemTimeZone_GetTimeZoneDSTAdjustmentOffsetNanoseconds(*pTimeUTC, pZone);
    if (nOffset < 0) pTimeUTC->nNanoseconds -= (0 - nOffset);
    else pTimeUTC->nNanoseconds += nOffset;
}

/*______________________________________________
  LTSystemTimeZone public interface functions */
static const LTTimeZone *
LTSystemTimeZone_GetKnownTimeZones(void) {
    return s_timeZones;
}

static const LTTimeZone *
LTSystemTimeZone_GetTimeZoneFromID(const char * pZoneID) {
    if (pZoneID && *pZoneID) {
        const LTTimeZone * pTimeZone = s_timeZones;
        while (pTimeZone->pZoneID) {
            if (0 == lt_strcmp(pTimeZone->pZoneID, pZoneID)) return pTimeZone;
            pTimeZone++;
        }
    }
    return NULL;
}

static const char *
LTSystemTimeZone_GetSystemTimeZoneID(void) {
    return LTSystemTimeZone_GetSystemTimeZone()->pZoneID;
}

static void
LTSystemTimeZone_SetSystemTimeZoneID(const char * pZoneID) {
    const LTTimeZone * pZone = LTSystemTimeZone_GetTimeZoneFromID(pZoneID);
    if (pZone) LTAtomic_Store(&s_timeZoneIndex, LTSystemTimeZone_GetTimeZoneIndexFromID(pZoneID));
}

static bool
IsClockTimeUTCDaylightSaving(const LTTime clockTimeUTC, const struct UserZoneReference *pRefZoneData) {
    if (LTTime_IsZero(pRefZoneData->utcDSTStart) || LTTime_IsZero(pRefZoneData->utcDSTEnd)) return false;
    return LTTime_IsLessThanOrEqual(pRefZoneData->utcDSTStart, clockTimeUTC) && LTTime_IsLessThan(clockTimeUTC, pRefZoneData->utcDSTEnd);
}

static bool
LTSystemTimeZone_IsClockTimeUTCDaylightSaving(const LTTime clockTimeUTC, const char * pZoneID) {
    const LTTimeZone * pZone = LTSystemTimeZone_FindOrGetSystemTimeZone(pZoneID);
    if (NULL == pZone || (0 == *pZone->pNameDST)) return false;

    /* If quick reference data available */
    if (s_referenceTimeZone == pZone) {
        struct UserZoneReference refZoneData;
        s_mutex->API->Lock(s_mutex);
        lt_memcpy(&refZoneData, &s_userReferenceData, sizeof(s_userReferenceData));
        s_mutex->API->Unlock(s_mutex);
        return IsClockTimeUTCDaylightSaving(clockTimeUTC, &refZoneData);
    }

    /* do a partial time conversion to local - just the UTC offset for standard time */
    LTTime timeUTC = clockTimeUTC;
    s64 nOffset = LTSystemTimeZone_GetTimeZoneUTCOffsetNanoseconds(pZone);
    if (nOffset < 0) timeUTC.nNanoseconds -= (0 - nOffset);
    else timeUTC.nNanoseconds += nOffset;

    /* now see if there is a DST Adjustment, if so, timeUTC is in this zone's Daylight Saving time period*/
    nOffset = LTSystemTimeZone_GetTimeZoneDSTAdjustmentOffsetNanoseconds(timeUTC, pZone);

    return nOffset ? true : false;
}

static LTTime
LTSystemTimeZone_GetClockTimeLocal(const char ** ppSystemTimeZoneIDOfClockTimeLocalToSet) {
    LTTime timeUTC = LT_GetCore()->GetClockTimeUTC();
    const LTTimeZone * pZone = LTSystemTimeZone_GetSystemTimeZone();

    /* If quick reference data available */
    if (s_referenceTimeZone == pZone) {
        struct UserZoneReference refZoneData;
        s_mutex->API->Lock(s_mutex);
        lt_memcpy(&refZoneData, &s_userReferenceData, sizeof(s_userReferenceData));
        s_mutex->API->Unlock(s_mutex);
        if (IsClockTimeUTCDaylightSaving(timeUTC, &refZoneData)) {
            LTTime_AddTo(timeUTC, refZoneData.utcOffsetDST);
        } else {
            LTTime_AddTo(timeUTC, refZoneData.utcOffsetSTD);
        }
        if (ppSystemTimeZoneIDOfClockTimeLocalToSet) *ppSystemTimeZoneIDOfClockTimeLocalToSet = pZone->pZoneID;
        return timeUTC;
    }

    if (pZone && timeUTC.nNanoseconds) LTSystemTimeZone_ConvertUTCToLocalTimeWithZone(&timeUTC, pZone); /* now timeUTC is actually local time */
    else pZone = NULL;

    if (ppSystemTimeZoneIDOfClockTimeLocalToSet) *ppSystemTimeZoneIDOfClockTimeLocalToSet = pZone ? pZone->pZoneID : NULL;
    return timeUTC; /* timeUTC has been converted to local, return */
}

static LTTime
LTSystemTimeZone_ClockTimeUTCToLocal(const LTTime clockTimeUTC, const char * pZoneID) {
    const LTTimeZone * pZone = LTSystemTimeZone_FindOrGetSystemTimeZone(pZoneID);
    LTTime clockTimeLocal = clockTimeUTC;

    /* If quick reference data available */
    if (s_referenceTimeZone == pZone) {
        struct UserZoneReference refZoneData;
        s_mutex->API->Lock(s_mutex);
        lt_memcpy(&refZoneData, &s_userReferenceData, sizeof(s_userReferenceData));
        s_mutex->API->Unlock(s_mutex);
        if (IsClockTimeUTCDaylightSaving(clockTimeLocal, &refZoneData)) {
            LTTime_AddTo(clockTimeLocal, refZoneData.utcOffsetDST);
        } else {
            LTTime_AddTo(clockTimeLocal, refZoneData.utcOffsetSTD);
        }
        return clockTimeLocal;
    }

    if (pZone) LTSystemTimeZone_ConvertUTCToLocalTimeWithZone(&clockTimeLocal, pZone);
    return clockTimeLocal;
}

static LTTime
LTSystemTimeZone_ClockTimeLocalToUTC(const LTTime clockTimeLocal, const char * pZoneID) {
    const LTTimeZone * pZone = LTSystemTimeZone_FindOrGetSystemTimeZone(pZoneID);
    LTTime clockTimeUTC = clockTimeLocal;

    /* If quick reference data available */
    if (s_referenceTimeZone == pZone) {
        struct UserZoneReference refZoneData;
        s_mutex->API->Lock(s_mutex);
        lt_memcpy(&refZoneData, &s_userReferenceData, sizeof(s_userReferenceData));
        s_mutex->API->Unlock(s_mutex);
        if (IsClockTimeUTCDaylightSaving(clockTimeLocal, &refZoneData)) {
            LTTime_SubtractFrom(clockTimeUTC, refZoneData.utcOffsetDST);
        } else {
            LTTime_SubtractFrom(clockTimeUTC, refZoneData.utcOffsetSTD);
        }
        return clockTimeUTC;
    }

    if (pZone) {
        s64 nOffset = LTSystemTimeZone_GetTimeZoneDSTAdjustmentOffsetNanoseconds(clockTimeUTC, pZone);
        if (nOffset < 0) clockTimeUTC.nNanoseconds += (0 - nOffset);
        else clockTimeUTC.nNanoseconds -= nOffset;

        nOffset = LTSystemTimeZone_GetTimeZoneUTCOffsetNanoseconds(pZone);
        if (nOffset < 0) clockTimeUTC.nNanoseconds += (0 - nOffset);
        else clockTimeUTC.nNanoseconds -= nOffset;
    }

    return clockTimeUTC;
}

static void
LTSystemTimeZone_ClockTimeToCalendarTime(LTTime clockTime, LTCalendarTime * pCalendarTimeToSet) {
    /* set the floor of clockTime 1/1/1970, the earliest support conversion at present.  Do this instead of
       refusing to convert and leaving *pCalendarTimeToSet uninitialized for the unsuspecting client.
       Time wont start until 1/1/1970 in the local time zone.  */
    if (clockTime.nNanoseconds < 0) clockTime.nNanoseconds = 0; /* clip time < 1/1/1970 to 1/1/1970 */
    u64 nMillis = clockTime.nNanoseconds / LT_CONSTU64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_MILLISECOND);
    u32 nSeconds = (u32)(nMillis / LT_CONSTU64(LTSYSTEMTIMEZONE_MILLISECONDS_PER_SECOND));  // total seconds
    u32 nMilliseconds = (u32)(nMillis % LT_CONSTU64(LTSYSTEMTIMEZONE_MILLISECONDS_PER_SECOND)); // leftover milliseconds
    u32 nDays = nSeconds / LTSYSTEMTIMEZONE_SECONDS_PER_DAY;
    u32 nWeekday = (nDays + 4) % 7;
    nSeconds -= (nDays * LTSYSTEMTIMEZONE_SECONDS_PER_DAY);

    u32 nYear = 1970;
    u32 nDaysInYear = (LTSystemTimeZone_IsLeapYear(nYear) ? 366 : 365);
    while (nDays >= nDaysInYear) {
        nYear++;
        nDays -= nDaysInYear;
        nDaysInYear = (LTSystemTimeZone_IsLeapYear(nYear) ? 366 : 365);
    }

    u32 nMonth = 1;
    const u32 * pDaysInMonth = (LTSystemTimeZone_IsLeapYear(nYear) ? s_daysInMonthLeapYear: s_daysInMonth);
    while (nDays >= pDaysInMonth[nMonth-1]) {
        nDays -= pDaysInMonth[nMonth-1];
        nMonth++;
    }

    u32 nHour = nSeconds / LTSYSTEMTIMEZONE_SECONDS_PER_HOUR;
    nSeconds %= LTSYSTEMTIMEZONE_SECONDS_PER_HOUR;
    u32 nMinute = nSeconds / LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE;
    nSeconds %= LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE;

    pCalendarTimeToSet->nYear = (u16)nYear;
    pCalendarTimeToSet->nMonth = (u16)nMonth;
    pCalendarTimeToSet->nDay = (u16)nDays+1;
    pCalendarTimeToSet->nHour = (u16)nHour;
    pCalendarTimeToSet->nMinute = (u16)nMinute;
    pCalendarTimeToSet->nSecond = (u16)nSeconds;
    pCalendarTimeToSet->nMillisecond = (u16)nMilliseconds;
    pCalendarTimeToSet->nWeekday = (u16)nWeekday;
}

static bool
LTSystemTimeZone_CalendarTimeToClockTime(const LTCalendarTime * pCalendarTime, LTTime * pClockTimeToSet) {
    if (pCalendarTime->nYear < 1970 || pCalendarTime->nYear > 2100) return false;
    if (pCalendarTime->nMonth < 1 || pCalendarTime->nMonth > 12) return false;
    if (pCalendarTime->nDay < 1) return false;
    const u32 * pDaysInMonth = (LTSystemTimeZone_IsLeapYear(pCalendarTime->nYear) ? s_daysInMonthLeapYear : s_daysInMonth);
    u32 nDaysInMonth = pDaysInMonth[pCalendarTime->nMonth-1];
    if ((u32)pCalendarTime->nDay > nDaysInMonth) return false;
    if (pCalendarTime->nHour > 23) return false;
    if (pCalendarTime->nMinute > 59) return false;
    if (pCalendarTime->nSecond > 59) return false;
    if (pCalendarTime->nMillisecond > 999) return false;

    u32 nDays = 0;
    for (u32 nYear = 1970; nYear < (u32)pCalendarTime->nYear; nYear++) nDays += (LTSystemTimeZone_IsLeapYear(nYear) ? 366 : 365);
    nDays += s_daysInYear[pCalendarTime->nMonth-1];
    if (pCalendarTime->nMonth > 2 && LTSystemTimeZone_IsLeapYear(pCalendarTime->nYear)) nDays++;
    nDays += pCalendarTime->nDay -1;

    u32 nSeconds = nDays * LTSYSTEMTIMEZONE_SECONDS_PER_DAY;
    nSeconds += pCalendarTime->nHour * LTSYSTEMTIMEZONE_SECONDS_PER_HOUR;
    nSeconds += pCalendarTime->nMinute * LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE;
    nSeconds += pCalendarTime->nSecond;

    pClockTimeToSet->nNanoseconds = ((s64)nSeconds * LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_SECOND)) + ((s64)pCalendarTime->nMillisecond * LT_CONSTS64(LTSYSTEMTIMEZONE_NANOSECONDS_PER_MILLISECOND));

    return true;
}

static const char * GetPeriodName (u32 n, u32 nLimit, const char * pNames) { return               n < nLimit ? pNames + n * 3 : "";               }
static const char * GetWeekdayName(u32 n)                                  { return GetPeriodName(n,  7, "SunMonTueWedThuFriSat");                }
static const char * GetMonthName  (u32 n)                                  { return GetPeriodName(n, 12, "JanFebMarAprMayJunJulAugSepOctNovDec"); }
static const char * GetAMPM       (u32 n)                                  { return GetPeriodName(n,  2, "am pm ");                               }
static int
LTSystemTimeZone_ClockTimeToHumanReadableString(const LTTime clockTime, bool b12Hour, const char * pZoneID, char * pStringBuffToFill, u32 nStringBuffSize) {
    if (NULL == pStringBuffToFill || (0 == nStringBuffSize)) return 0;
    *pStringBuffToFill = 0; if (nStringBuffSize < 48) return 0;
    LTCalendarTime * pCalendarTime = lt_malloc(sizeof(*pCalendarTime));
    if (pCalendarTime) {
        LTSystemTimeZone_ClockTimeToCalendarTime(clockTime, pCalendarTime);
        u16 nAMPM = 2;
        if (b12Hour) { nAMPM = pCalendarTime->nHour > 12; if (!(pCalendarTime->nHour %= 12)) pCalendarTime->nHour = 12; }
        // see if we fall into DST in the pZoneID time zone that we're given
        const LTTimeZone * pTimeZone = LTSystemTimeZone_FindOrGetSystemTimeZone(pZoneID);
        if (pTimeZone) {
            /* done with pZoneID, reuse it for printing the proper zone abbreviated name */
            pZoneID = LTSystemTimeZone_GetTimeZoneDSTAdjustmentOffsetNanoseconds(clockTime, pTimeZone) ? pTimeZone->pAbbreviationDST : pTimeZone->pAbbreviationSTD;
        }
        LT_SIZE nChars = lt_snprintf(pStringBuffToFill, (LT_SIZE)nStringBuffSize, "%.3s %.3s %02u, %04u %02u:%02u:%02u.%03u %.3s",
            GetWeekdayName(pCalendarTime->nWeekday), GetMonthName(pCalendarTime->nMonth - 1), pCalendarTime->nDay, pCalendarTime->nYear,
            pCalendarTime->nHour, pCalendarTime->nMinute, pCalendarTime->nSecond, pCalendarTime->nMillisecond, GetAMPM(nAMPM));
        if (nChars + 7 <= (LT_SIZE)nStringBuffSize) nChars += lt_snprintf(pStringBuffToFill + nChars, 7,  "%s", pZoneID ? pZoneID : "UTC");
        lt_free(pCalendarTime);
        return (int)nChars;
    }
    return 0;
}

/** https://data.iana.org/time-zones/theory.html
A proleptic TZ string has the following format:
    stdoffset[dst[offset][,date[/time],date[/time]]]

std and dst
    are 3 or more characters specifying the standard and daylight saving time (DST) zone abbreviations.
    Starting with POSIX.1-2001, std and dst may also be in a quoted form like '<+09>'; this allows "+" and "-" in the names.
offset
    is of the form '[±]hh:[mm[:ss]]' and specifies the offset west of UT. 'hh' may be a single digit; 0≤hh≤24.
    The default DST offset is one hour ahead of standard time.
date[/time],date[/time]
    specifies the beginning and end of DST. If this is absent, the system supplies its own ruleset for DST, typically current US DST rules.
time
    takes the form 'hh:[mm[:ss]]' and defaults to 02:00.
    This is the same format as the offset, except that a leading '+' or '-' is not allowed.
date
    takes one of the following forms:
    Mm.n.d (0[Sunday]≤d≤6[Saturday], 1≤n≤5, 1≤m≤12)
        for the dth day of week n of month m of the year, where week 1 is the first week in which day d appears, and '5' stands for the last week in which day d appears (which may be either the 4th or 5th week). Typically, this is the only useful form; the n and Jn forms are rarely used.
*/
static bool
LTSystemTimeZone_SetUserTimeZone(
    LTTime utcOffsetSTD, LTTime utcOffsetDST, LTTime utcDSTStart, LTTime utcDSTEnd, 
    const char *pAbbreviationSTD, const char *pAbbreviationDST, const char *pOlsonName, const char *pDescription)
{
    /* Must have STD timezone ID/name and Olson name. */
    if (!pAbbreviationSTD || !pOlsonName || *pAbbreviationSTD == 0 || *pOlsonName == 0) return false;
    if (lt_strlen(pAbbreviationSTD) >= kLTSystemTimeZone_MaxZoneIDLen ||
        (pAbbreviationDST && lt_strlen(pAbbreviationDST) >= kLTSystemTimeZone_MaxZoneIDLen))
    {
        return false;
    }
    /* Quoted zone */
    if (pAbbreviationSTD[0] == '-' || pAbbreviationSTD[0] == '+' || ('0' <= pAbbreviationSTD[0] && pAbbreviationSTD[0] <= '9') ||
        (pAbbreviationDST && (pAbbreviationDST[0] == '-' || pAbbreviationDST[0] == '+' || ('0' <= pAbbreviationDST[0] && pAbbreviationDST[0] <= '9'))))
    {
        if (lt_strlen(pAbbreviationSTD) >= kLTSystemTimeZone_MaxZoneIDLen - 2) return false;
        if (pAbbreviationDST && lt_strlen(pAbbreviationDST) >= kLTSystemTimeZone_MaxZoneIDLen - 2) return false;
    }

    /* Both utcOffsetSTD and utcOffsetDST must be an LTTime offset that is between (-24 hours..+24 hours), non-inclusive */
    if (LTTime_IsLessThanOrEqual(utcOffsetSTD, LTTime_Seconds(-LTSYSTEMTIMEZONE_SECONDS_PER_DAY)) ||
        LTTime_IsLessThanOrEqual(LTTime_Seconds(LTSYSTEMTIMEZONE_SECONDS_PER_DAY), utcOffsetSTD) ||
        LTTime_IsLessThanOrEqual(utcOffsetDST, LTTime_Seconds(-LTSYSTEMTIMEZONE_SECONDS_PER_DAY)) ||
        LTTime_IsLessThanOrEqual(LTTime_Seconds(LTSYSTEMTIMEZONE_SECONDS_PER_DAY), utcOffsetDST))
    {
        return false;
    }

    /* Hold all local zone values in this struct to reduce stack. */
    struct ZoneValue {
        s32 offset;
        LTCalendarTime calendarDSTStart;
        LTCalendarTime calendarDSTEnd;
        u16 weekDSTStart;
        u16 weekDSTEnd;
        s8 utcOffsetSTDHour;
        u8 utcOffsetSTDMinute;
        s8 utcOffsetDSTHour;
        u8 utcOffsetDSTMinute;
    };
    struct ZoneValue *pZoneValue = lt_malloc(sizeof(struct ZoneValue));
    if (!pZoneValue) return false;
    lt_memset(pZoneValue, 0, sizeof(struct ZoneValue));

    /* Convert STD offset */
    pZoneValue->offset = LTTime_GetSeconds(utcOffsetSTD);
    if (pZoneValue->offset < 0) {
        pZoneValue->offset = -pZoneValue->offset;
        pZoneValue->utcOffsetSTDHour = pZoneValue->offset / LTSYSTEMTIMEZONE_SECONDS_PER_HOUR;
    } else {
        pZoneValue->utcOffsetSTDHour = -(pZoneValue->offset / LTSYSTEMTIMEZONE_SECONDS_PER_HOUR);
    }
    pZoneValue->utcOffsetSTDMinute = (pZoneValue->offset % LTSYSTEMTIMEZONE_SECONDS_PER_HOUR) / LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE;

    if (LTTime_IsZero(utcDSTStart) || LTTime_IsZero(utcDSTEnd)) { /* STD only */
        if (pAbbreviationSTD[0] == '-' || pAbbreviationSTD[0] == '+' || ('0' <= pAbbreviationSTD[0] && pAbbreviationSTD[0] <= '9'))
        {
            /* Quoted zone, <-05>5:00 */
            lt_snprintf(s_userZoneString.posixTZString, sizeof(s_userZoneString.posixTZString),
                        "<%s>%d:%02u", pAbbreviationSTD, pZoneValue->utcOffsetSTDHour, pZoneValue->utcOffsetSTDMinute);

        } else {
            /* Example, "UserSTD7:00" */
            lt_snprintf(s_userZoneString.posixTZString, sizeof(s_userZoneString.posixTZString),
                        "%s%d:%02u", pAbbreviationSTD, pZoneValue->utcOffsetSTDHour, pZoneValue->utcOffsetSTDMinute);
        }
        lt_memset(s_userZoneString.nameDST, 0, sizeof(s_userZoneString.nameDST));

    } else { /* STD + DST */

        /* Must have a DST timezone ID/name.
         * utcDSTStart must be less than utcDSTEnd. */
        if (!pAbbreviationDST || *pAbbreviationDST == 0 || LTTime_IsLessThanOrEqual(utcDSTEnd, utcDSTStart)) {
            lt_free(pZoneValue);
            return false;
        }

        /* Example: 1710057600, Sunday, March 10,   2024 2:00:00 AM GMT-06:00 CST */
        /* Example: 1730617200, Sunday, November 3, 2024 2:00:00 AM GMT-05:00 CDT */
        LTSystemTimeZone_ClockTimeToCalendarTime(LTTime_Seconds(LTTime_GetSeconds(utcDSTStart) + LTTime_GetSeconds(utcOffsetSTD)), &pZoneValue->calendarDSTStart);
        LTSystemTimeZone_ClockTimeToCalendarTime(LTTime_Seconds(LTTime_GetSeconds(utcDSTEnd) + LTTime_GetSeconds(utcOffsetDST)), &pZoneValue->calendarDSTEnd);
        bool bTest = false;
        /* If utcDSTStart and utcDSTEnd are in the same year (for northern hemisphere）
         * then, the month of utcDSTStart must be smaller than the month of utcDSTEnd. */
        if ((pZoneValue->calendarDSTStart.nYear == pZoneValue->calendarDSTEnd.nYear) && (pZoneValue->calendarDSTStart.nMonth < pZoneValue->calendarDSTEnd.nMonth)) bTest = true;
        /* If the year of utcDSTStart is 1 smaller than the year of utcDSTEnd (for southern hemisphere)
         * then, the month of utcDSTStart must be larger than the month of utcDSTEnd. */
        if (!bTest && (pZoneValue->calendarDSTStart.nYear + 1 == pZoneValue->calendarDSTEnd.nYear) && (pZoneValue->calendarDSTStart.nMonth > pZoneValue->calendarDSTEnd.nMonth)) bTest = true;
        if (!bTest) {
            lt_free(pZoneValue);
            return false;
        }

        /* Week 1 is the first week in which day d appears. */
        pZoneValue->weekDSTStart = (pZoneValue->calendarDSTStart.nDay - 1) / 7 + 1;
        pZoneValue->weekDSTEnd = (pZoneValue->calendarDSTEnd.nDay - 1) / 7 + 1;
        if (pZoneValue->weekDSTStart == 4) pZoneValue->weekDSTStart = 5;
        if (pZoneValue->weekDSTEnd == 4) pZoneValue->weekDSTEnd = 5;

        /* Convert DST offset. */
        pZoneValue->offset = LTTime_GetSeconds(utcOffsetDST);
        if (pZoneValue->offset < 0) {
            pZoneValue->offset = - pZoneValue->offset;
            pZoneValue->utcOffsetDSTHour = pZoneValue->offset / LTSYSTEMTIMEZONE_SECONDS_PER_HOUR;
        } else {
            pZoneValue->utcOffsetDSTHour = -(pZoneValue->offset / LTSYSTEMTIMEZONE_SECONDS_PER_HOUR);
        }
        pZoneValue->utcOffsetDSTMinute = (pZoneValue->offset % LTSYSTEMTIMEZONE_SECONDS_PER_HOUR) / LTSYSTEMTIMEZONE_SECONDS_PER_MINUTE;

        if (pAbbreviationSTD[0] == '-' || pAbbreviationSTD[0] == '+' || ('0' <= pAbbreviationSTD[0] && pAbbreviationSTD[0] <= '9') ||
            pAbbreviationDST[0] == '-' || pAbbreviationDST[0] == '+' || ('0' <= pAbbreviationDST[0] && pAbbreviationDST[0] <= '9'))
        {
            /* Quoted zone, <-02>2:00<-01>1:00,M3.5.0/1:00,M10.5.0/0:00 */
            lt_snprintf(s_userZoneString.posixTZString, sizeof(s_userZoneString.posixTZString),
                        "<%s>%d:%02u<%s>%d:%02u,M%u.%u.%u/%02u:%02u,M%u.%u.%u/%02u:%02u",
                        pAbbreviationSTD, pZoneValue->utcOffsetSTDHour, pZoneValue->utcOffsetSTDMinute, pAbbreviationDST, pZoneValue->utcOffsetDSTHour, pZoneValue->utcOffsetDSTMinute,
                        pZoneValue->calendarDSTStart.nMonth, pZoneValue->weekDSTStart, pZoneValue->calendarDSTStart.nWeekday, pZoneValue->calendarDSTStart.nHour, pZoneValue->calendarDSTStart.nMinute,
                        pZoneValue->calendarDSTEnd.nMonth, pZoneValue->weekDSTEnd, pZoneValue->calendarDSTEnd.nWeekday, pZoneValue->calendarDSTEnd.nHour, pZoneValue->calendarDSTEnd.nMinute);

        } else {
            /* Example, UserSTD6:00UserDST5:00,M3.2.0/02:00,M11.1.0/02:00 */
            lt_snprintf(s_userZoneString.posixTZString, sizeof(s_userZoneString.posixTZString),
                        "%s%d:%02u%s%d:%02u,M%u.%u.%u/%02u:%02u,M%u.%u.%u/%02u:%02u",
                        pAbbreviationSTD, pZoneValue->utcOffsetSTDHour, pZoneValue->utcOffsetSTDMinute, pAbbreviationDST, pZoneValue->utcOffsetDSTHour, pZoneValue->utcOffsetDSTMinute,
                        pZoneValue->calendarDSTStart.nMonth, pZoneValue->weekDSTStart, pZoneValue->calendarDSTStart.nWeekday, pZoneValue->calendarDSTStart.nHour, pZoneValue->calendarDSTStart.nMinute,
                        pZoneValue->calendarDSTEnd.nMonth, pZoneValue->weekDSTEnd, pZoneValue->calendarDSTEnd.nWeekday, pZoneValue->calendarDSTEnd.nHour, pZoneValue->calendarDSTEnd.nMinute);
        }
        lt_strncpyTerm(s_userZoneString.nameDST, pAbbreviationDST, sizeof(s_userZoneString.nameDST));
    }
    lt_free(pZoneValue);

    lt_strncpyTerm(s_userZoneString.olsonName, pOlsonName, sizeof(s_userZoneString.olsonName));
    if (pDescription) lt_strncpyTerm(s_userZoneString.description, pDescription, sizeof(s_userZoneString.description));

    s_mutex->API->Lock(s_mutex);
    s_userReferenceData.utcOffsetSTD = utcOffsetSTD;
    s_userReferenceData.utcOffsetDST = utcOffsetDST;
    s_userReferenceData.utcDSTStart = utcDSTStart;
    s_userReferenceData.utcDSTEnd = utcDSTEnd;
    s_referenceTimeZone = LTSystemTimeZone_GetTimeZoneFromID("User");
    s_mutex->API->Unlock(s_mutex);

    LTLOG_SERVER("set.user", "%s %s", s_userZoneString.olsonName, s_userZoneString.posixTZString);
    LTSystemTimeZone_SetSystemTimeZoneID("User");
    return true;
}

/*__________________________________________
  LTSystemTimeZone library initialization */
static bool LTSystemTimeZoneImpl_LibInit(void) {
    s_mutex = lt_createobject(LTMutex);
    if (!s_mutex) return false;
    s_referenceTimeZone = NULL;
    lt_memset(&s_userZoneString, 0, sizeof(s_userZoneString));
    lt_memset(&s_userReferenceData, 0, sizeof(s_userReferenceData));
    LTSystemTimeZone_SetSystemTimeZoneID(LTSYSTEMTIMEZONE_DEFAULT_TIMEZONEID);
    return true;
}

static void LTSystemTimeZoneImpl_LibFini(void) {
    lt_destroyobject(s_mutex);
}

/*__________________________________________________
  LTSystemTimeZone library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemTimeZone)

    .GetKnownTimeZones                  = &LTSystemTimeZone_GetKnownTimeZones,
    .GetTimeZoneFromID                  = &LTSystemTimeZone_GetTimeZoneFromID,

    .GetSystemTimeZoneID                = &LTSystemTimeZone_GetSystemTimeZoneID,
    .SetSystemTimeZoneID                = &LTSystemTimeZone_SetSystemTimeZoneID,

    .IsClockTimeUTCDaylightSaving       = &LTSystemTimeZone_IsClockTimeUTCDaylightSaving,

    .GetClockTimeLocal                  = &LTSystemTimeZone_GetClockTimeLocal,

    .ClockTimeUTCToLocal                = &LTSystemTimeZone_ClockTimeUTCToLocal,
    .ClockTimeLocalToUTC                = &LTSystemTimeZone_ClockTimeLocalToUTC,

    .ClockTimeToCalendarTime            = &LTSystemTimeZone_ClockTimeToCalendarTime,
    .CalendarTimeToClockTime            = &LTSystemTimeZone_CalendarTimeToClockTime,

    .ClockTimeToHumanReadableString     = &LTSystemTimeZone_ClockTimeToHumanReadableString,

    .SetUserTimeZone                    = &LTSystemTimeZone_SetUserTimeZone,

LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Jan-03   augustus    created
 *  20-Jan-03   augustus    1.0 STATUS: about 10% complete
 *  08-Mar-04   augustus    will I ever implement this class? Enquiring minds want to know.
 *                          added GetUptimeMilliseconds() and GetUptimeNanoseconds()
 *  29-Oct-04   augustus    reimplemented GetUptimeMilliseconds() and GetUptimeNanoseconds()
 *                          to use the new /dev/roku/cascade device on deschutes
 *  27-Mar-05   augustus    changed GetUptimeMilliseconds to return a u64
 *  05-Aug-05   augustus    made VC++7 compiler happy
 *  08-Nov-20   augustus    ported to LT
 *  17-Dec-21   augustus    clamp negative time to 1/1/1970 in ClockTimeToCalendarTime
 *  22-Dec-21   augustus    set a default system time zone in LibInit
 *  18-Jan-21   augustus    added ClockTimeToHumanReadableString
 *  05-Jun-22   augustus    ClockTimeToHumanReadableString returns nChars written to buffer
 */
