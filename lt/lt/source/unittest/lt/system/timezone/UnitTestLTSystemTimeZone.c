/*******************************************************************************
 * <unittest/lt/system/timezone/UnitTestLTSystemTimezone.c>
 * Unit Test Function Name: TestLTTimeZone()
 *
 *  Tests LTCore Functions: UTCToLocalTime()
 *                          LocalTimeToUTC()
 *                          LTTimeToCalendarTime()
 *                          CalendarTimeToLTTime()
 *                          IsTimeUTCLocalDaylightSaving()
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/system/timezone/LTSystemTimeZone.h>
#include <tilt/JiltEngine.h>

/*************************************************
 * file UnitTestLTSystemTimeZone.c #defines */
#define LT_TIMEZONE_TEST_TAG_SECTION                "timezone"
#define LT_TIMEZONE_TEST_DEPENDENCY_FAILURE_TAG     LT_TIMEZONE_TEST_TAG_SECTION ".dependency.failure"
#define LT_TIMEZONE_TEST_TZ_FAILURE_TAG             LT_TIMEZONE_TEST_TAG_SECTION ".UTC<->DST.failure"
#define LT_TIMEZONE_TEST_IS_DST_FAILURE_TAG         LT_TIMEZONE_TEST_TAG_SECTION ".test_inside_DST.failure"
#define LT_TIMEZONE_TEST_TZ_CHECK_RESULT(expectedTimeString) \
    if (0 != lt_strcmp(*pString, expectedTimeString)) { \
        TILT_REPORT_FAILURE(s_tilt, LT_TIMEZONE_TEST_TZ_FAILURE_TAG ": timezone='%s', expected='%s'", \
                            *pString, expectedTimeString); \
        return false; \
    }
#define LT_TIMEZONE_TEST_TZ_UTC(expectedTimeString) \
    nError++; \
    FormatTimeUTCAsCalendarTimeString(pString, timeUTC); \
    LT_TIMEZONE_TEST_TZ_CHECK_RESULT(expectedTimeString);
#define LT_TIMEZONE_TEST_TZ_ZONE(zone, expectedTimeString) \
    nError++; \
    FormatTimeUTCInSpecifiedLocalTimezoneAsCalendarTimeString(pString, timeUTC, zone); \
    LT_TIMEZONE_TEST_TZ_CHECK_RESULT(expectedTimeString);
#define LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME(zone, bExpected) \
    nError++; \
    if (bExpected != s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(timeUTC, zone)) { \
        TILT_REPORT_FAILURE(s_tilt, LT_TIMEZONE_TEST_IS_DST_FAILURE_TAG ": timezone='%s', isdst='%s', expected=%s", \
                            zone, \
                            s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(timeUTC, zone) ? "true" : "false", \
                            bExpected ? "true" : "false"); \
        return false; \
    }

#define LT_TIMEZONE_TEST_VERBOSE_INFO 1
#if     LT_TIMEZONE_TEST_VERBOSE_INFO
    #define LT_TIMEZONE_TEST_DO_TEST(function, errorBase, pLTString, description) \
            TILT_INFO(tilt, LT_TIMEZONE_TEST_TAG_SECTION ".testing." description); \
            if (! function(errorBase, &pLTString)) break;
#else
    #define LT_TIMEZONE_TEST_DO_TEST(function, errorBase, pLTString, description) \
        if (! function(errorBase, &pLTString)) break;
#endif


/*********************************************************
 * file UnitTestLTSystemTimeZone.c static variables */
static LTCore                           *s_pCore           = NULL;
static LTSystemTimeZone                 *s_pSystemTimeZone = NULL;
static JiltEngine                       *s_engine;
static Tilt                             *s_tilt;

/*********************************************************
 * file UnitTestLTSystemTimeZone.c static functions */
static void
FormatCalendarTimeString(LTString * pStringToFormatInto, const LTCalendarTime * pCalendarTime, const char * pTimeZoneAbbreviation) {
    ltstring_format(pStringToFormatInto, "%04u/%02u/%02u %02u:%02u:%02u (%s)",
        pCalendarTime->nYear, pCalendarTime->nMonth, pCalendarTime->nDay,
        pCalendarTime->nHour, pCalendarTime->nMinute, pCalendarTime->nSecond, pTimeZoneAbbreviation);
}

static void
FormatTimeUTCInSpecifiedLocalTimezoneAsCalendarTimeString(LTString * pStringToFormatInto, LTTime timeUTC, const char * pTimeZoneID) {
    LTCalendarTime calendarTime;
    s_pSystemTimeZone->ClockTimeToCalendarTime(s_pSystemTimeZone->ClockTimeUTCToLocal(timeUTC, pTimeZoneID), &calendarTime);
    const LTTimeZone * pTimeZone = s_pSystemTimeZone->GetTimeZoneFromID(pTimeZoneID);
    const char * pTimeZoneAbbreviation = "";
    if (pTimeZone) pTimeZoneAbbreviation = s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(timeUTC, pTimeZoneID) ? pTimeZone->pAbbreviationDST : pTimeZone->pAbbreviationSTD;
    else pTimeZoneAbbreviation = "???";
    FormatCalendarTimeString(pStringToFormatInto, &calendarTime, pTimeZoneAbbreviation);
}

static void
FormatTimeUTCAsCalendarTimeString(LTString * pStringToFormatInto, LTTime timeUTC) {
    FormatTimeUTCInSpecifiedLocalTimezoneAsCalendarTimeString(pStringToFormatInto, timeUTC, "UTC");
}

static bool TestEuropeanSpringDST(int nError, LTString * pString) { LT_UNUSED(nError);  /* actually used, but hidden from clang by the macro */
    // European rule is change occurs at 1am UTC
    LTTime timeUTC;
    LTCalendarTime calendarTime = {
        .nYear        = 2007,
        .nMonth       = 3,
        .nDay         = 25,
        .nHour        = 0,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LTTime_SubtractFrom(timeUTC, LTTime_Seconds(3600)); /* subtract an hour */
    LT_TIMEZONE_TEST_TZ_UTC (          "2007/03/24 23:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("GMTBST", "2007/03/24 23:59:59 (GMT)");
    LT_TIMEZONE_TEST_TZ_ZONE("WET",    "2007/03/24 23:59:59 (WET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("WET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("CET",    "2007/03/25 00:59:59 (CET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("EET",    "2007/03/25 01:59:59 (EET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EET", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (          "2007/03/25 00:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("GMTBST", "2007/03/25 00:59:59 (GMT)");
    LT_TIMEZONE_TEST_TZ_ZONE("WET",    "2007/03/25 00:59:59 (WET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("WET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("CET",    "2007/03/25 01:59:59 (CET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("EET",    "2007/03/25 02:59:59 (EET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EET", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (          "2007/03/25 01:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("GMTBST", "2007/03/25 02:00:00 (BST)");
    LT_TIMEZONE_TEST_TZ_ZONE("WET",    "2007/03/25 02:00:00 (WEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("WET", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CET",    "2007/03/25 03:00:00 (CEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CET", true);
    LT_TIMEZONE_TEST_TZ_ZONE("EET",    "2007/03/25 04:00:00 (EEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EET", true);

    return true;
}

static bool TestEuropeanAutumnDST(int nError, LTString * pString) { LT_UNUSED(nError);  /* actually used, but hidden from clang by the macro */
    LTTime timeUTC;
    LTCalendarTime calendarTime = {
        .nYear        = 2007,
        .nMonth       = 10,
        .nDay         = 28,
        .nHour        = 0,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LT_TIMEZONE_TEST_TZ_UTC (          "2007/10/28 00:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("GMTBST", "2007/10/28 01:59:59 (BST)");
    LT_TIMEZONE_TEST_TZ_ZONE("WET",    "2007/10/28 01:59:59 (WEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("WET", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CET",    "2007/10/28 02:59:59 (CEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CET", true);
    LT_TIMEZONE_TEST_TZ_ZONE("EET",    "2007/10/28 03:59:59 (EEST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EET", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (          "2007/10/28 01:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("GMTBST", "2007/10/28 01:00:00 (GMT)");
    LT_TIMEZONE_TEST_TZ_ZONE("WET",    "2007/10/28 01:00:00 (WET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("WET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("CET",    "2007/10/28 02:00:00 (CET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CET", false);
    LT_TIMEZONE_TEST_TZ_ZONE("EET",    "2007/10/28 03:00:00 (EET)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EET", false);

    return true;
}

static bool TestAmericanSpringDST(int nError, LTString * pString) { LT_UNUSED(nError);  /* actually used, but hidden from clang by the macro */
    // US rule is at 2am local time.
    LTTime timeUTC;
    LTCalendarTime calendarTime = {
        .nYear        = 2007,
        .nMonth       = 3,
        .nDay         = 11,
        .nHour        = 1,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600 * 5)); /* Advance to just before 2am EST. */
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 06:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 01:59:59 (EST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 00:59:59 (CST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/10 23:59:59 (MST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/10 22:59:59 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 07:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 03:00:00 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 01:00:00 (CST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 00:00:00 (MST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/10 23:00:00 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 07:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 03:59:59 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 01:59:59 (CST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 00:59:59 (MST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/10 23:59:59 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 08:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 04:00:00 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 03:00:00 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 01:00:00 (MST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 00:00:00 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 08:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 04:59:59 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 03:59:59 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 01:59:59 (MST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 00:59:59 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 09:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 05:00:00 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 04:00:00 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 03:00:00 (MDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 01:00:00 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 09:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 05:59:59 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 04:59:59 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 03:59:59 (MDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 01:59:59 (PST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 10:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 06:00:00 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 05:00:00 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 04:00:00 (MDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 03:00:00 (PDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (       "2007/03/11 10:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("EST", "2007/03/11 06:59:59 (EDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST", "2007/03/11 05:59:59 (CDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST", "2007/03/11 04:59:59 (MDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST", true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST", "2007/03/11 03:59:59 (PDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST", true);

    return true;
}

static bool TestAmericanFallDST(int nError, LTString * pString) { LT_UNUSED(nError);    /* actually used, but hidden from clang by the macro */
    // US rule is at 2am local time.
    LTCalendarTime calendarTime = {
        .nYear        = 2007,
        .nMonth       = 11,
        .nDay         = 4,
        .nHour        = 1,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };

    LTTime timeUTC;
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600 * 4)); /* Advance to just before 2am EDT. */
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 05:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 01:59:59 (EDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 00:59:59 (CDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/03 23:59:59 (MDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/03 22:59:59 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/03 21:59:59 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 06:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 01:00:00 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 01:00:00 (CDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 00:00:00 (MDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/03 23:00:00 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/03 22:00:00 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 06:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 01:59:59 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 01:59:59 (CDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 00:59:59 (MDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/03 23:59:59 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/03 22:59:59 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 07:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 02:00:00 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 01:00:00 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 01:00:00 (MDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 00:00:00 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/03 23:00:00 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 07:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 02:59:59 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 01:59:59 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 01:59:59 (MDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 00:59:59 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/03 23:59:59 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 08:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 03:00:00 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 02:00:00 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 01:00:00 (MST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 01:00:00 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/04 00:00:00 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 08:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 03:59:59 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 02:59:59 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 01:59:59 (MST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 01:59:59 (PDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  true);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/04 00:59:59 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 09:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 04:00:00 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 03:00:00 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 02:00:00 (MST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 01:00:00 (PST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/04 01:00:00 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3599));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 09:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 04:59:59 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 03:59:59 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 02:59:59 (MST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 01:59:59 (PST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/04 01:59:59 (AKDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/11/04 10:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("EST",  "2007/11/04 05:00:00 (EST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("EST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("CST",  "2007/11/04 04:00:00 (CST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("CST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("MST",  "2007/11/04 03:00:00 (MST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("MST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("PST",  "2007/11/04 02:00:00 (PST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("PST",  false);
    LT_TIMEZONE_TEST_TZ_ZONE("AKST", "2007/11/04 01:00:00 (AKST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("AKST", false);

    return true;
}

static bool TestNewZealandSpringDST(int nError, LTString * pString) { LT_UNUSED(nError);    /* actually used, but hidden from clang by the macro */
    // NZ rule is 2am(local)->3am.
    LTCalendarTime calendarTime = {
        .nYear        = 2007,
        .nMonth       = 9,
        .nDay         = 29,
        .nHour        = 13,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };

    LTTime timeUTC;
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LTTime_SubtractFrom(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/09/29 12:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2007/09/30 00:59:59 (NZST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/09/29 13:59:59 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2007/09/30 01:59:59 (NZST)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/09/29 14:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2007/09/30 03:00:00 (NZDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2007/09/29 15:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2007/09/30 04:00:00 (NZDT)"); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", true);

    return true;
}

static bool TestNewZealandAutumnDST(int nError, LTString * pString) { LT_UNUSED(nError);    /* actually used, but hidden from clang by the macro */
    // NZ rule is 3am(DST)->2am
    LTCalendarTime calendarTime = {
        .nYear        = 2008,
        .nMonth       = 4,
        .nDay         = 5,
        .nHour        = 13,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };

    LTTime timeUTC;
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LTTime_SubtractFrom(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2008/04/05 12:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2008/04/06 01:59:59 (NZDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2008/04/05 13:59:59 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2008/04/06 02:59:59 (NZDT)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", true);

    LTTime_AddTo(timeUTC, LTTime_Seconds(1));
    LT_TIMEZONE_TEST_TZ_UTC (        "2008/04/05 14:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2008/04/06 02:00:00 (NZST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", false);

    LTTime_AddTo(timeUTC, LTTime_Seconds(3600));
    LT_TIMEZONE_TEST_TZ_UTC (        "2008/04/05 15:00:00 (UTC)" );
    LT_TIMEZONE_TEST_TZ_ZONE("NZST", "2008/04/06 03:00:00 (NZST)" ); LT_TIMEZONE_TEST_IS_DAYLIGHT_SAVING_TIME("NZST", false);

    return true;
}

static bool TestRawUTCOffsets(int nError, LTString * pString) { LT_UNUSED(nError);  /* actually used, but hidden from clang by the macro */
    LTCalendarTime calendarTime = {
        .nYear        = 2008,
        .nMonth       = 4,
        .nDay         = 5,
        .nHour        = 12,
        .nMinute      = 0,
        .nSecond      = 0,
        .nMillisecond = 0,
        .nWeekday     = 0     /* weekday unimportant and not used for conversion into LTTime */
    };

    LTTime timeUTC;
    s_pSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &timeUTC);

    LT_TIMEZONE_TEST_TZ_UTC (          "2008/04/05 12:00:00 (UTC)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-1",  "2008/04/05 11:00:00 (UTC-1)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-2",  "2008/04/05 10:00:00 (UTC-2)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-3",  "2008/04/05 09:00:00 (UTC-3)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-4",  "2008/04/05 08:00:00 (UTC-4)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-5",  "2008/04/05 07:00:00 (UTC-5)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-6",  "2008/04/05 06:00:00 (UTC-6)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-7",  "2008/04/05 05:00:00 (UTC-7)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-8",  "2008/04/05 04:00:00 (UTC-8)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-9",  "2008/04/05 03:00:00 (UTC-9)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-10", "2008/04/05 02:00:00 (UTC-10)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-11", "2008/04/05 01:00:00 (UTC-11)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-12", "2008/04/05 00:00:00 (UTC-12)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-13", "2008/04/04 23:00:00 (UTC-13)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC-14", "2008/04/04 22:00:00 (UTC-14)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+1",  "2008/04/05 13:00:00 (UTC+1)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+2",  "2008/04/05 14:00:00 (UTC+2)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+3",  "2008/04/05 15:00:00 (UTC+3)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+4",  "2008/04/05 16:00:00 (UTC+4)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+5",  "2008/04/05 17:00:00 (UTC+5)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+6",  "2008/04/05 18:00:00 (UTC+6)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+7",  "2008/04/05 19:00:00 (UTC+7)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+8",  "2008/04/05 20:00:00 (UTC+8)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+9",  "2008/04/05 21:00:00 (UTC+9)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+10", "2008/04/05 22:00:00 (UTC+10)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+11", "2008/04/05 23:00:00 (UTC+11)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+12", "2008/04/06 00:00:00 (UTC+12)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+13", "2008/04/06 01:00:00 (UTC+13)");
    LT_TIMEZONE_TEST_TZ_ZONE("UTC+14", "2008/04/06 02:00:00 (UTC+14)");

    return true;
}

/*
 **************************************
 **************************************
 ** ```````````````````````````````` **
 ** ````` Main screen turn on. ````` **
 ** ````      What happen?      ```` **
 ** ```` How are you gentlemen?  ``` **
 ** ```                         ```` **
 ** ````    TestLTTimeZone     ````` **
 ** ```````````````````````````````` **
 ** ```````````````````````````````` **
 ** ```````````````````````````````` **
 **************************************/
static void TestLTTimeZone(Tilt * tilt) {
    TILT_INFO(tilt, LT_TIMEZONE_TEST_TAG_SECTION ".initiate");
    LTString pLTString = NULL;

    do {
        if (NULL == (pLTString = ltstring_create(""))) { TILT_REPORT_FAILURE(tilt, LT_TIMEZONE_TEST_DEPENDENCY_FAILURE_TAG "ltstring_alloc"); break; }
        LT_TIMEZONE_TEST_DO_TEST(TestEuropeanSpringDST,   100, pLTString, "European Spring DST transitions");
        LT_TIMEZONE_TEST_DO_TEST(TestEuropeanAutumnDST,   200, pLTString, "European Autumn DST transitions");
        LT_TIMEZONE_TEST_DO_TEST(TestAmericanSpringDST,   300, pLTString, "American Spring DST transitions");
        LT_TIMEZONE_TEST_DO_TEST(TestAmericanFallDST,     400, pLTString, "American Fall DST transitions");
        LT_TIMEZONE_TEST_DO_TEST(TestNewZealandSpringDST, 500, pLTString, "New Zealand Spring DST transitions");
        LT_TIMEZONE_TEST_DO_TEST(TestNewZealandAutumnDST, 600, pLTString, "New Zealand Autumn DST transitions");
        TILT_INFO(tilt, LT_TIMEZONE_TEST_TAG_SECTION                   ".mulling.Where is Old Zealand, Captain?");
        LT_TIMEZONE_TEST_DO_TEST(TestRawUTCOffsets,       700, pLTString, "Raw UTC Offsets");
    }
    while (false);

    if (pLTString) ltstring_destroy(pLTString);
    TILT_INFO(tilt, LT_TIMEZONE_TEST_TAG_SECTION ".Work Complete, Captain!");
}

static void TestUserTimeZoneDSTNorthern(Tilt *tilt, LTTime utcOffsetSTD, LTTime utcOffsetDST, LTTime utcDSTStart, LTTime utcDSTEnd, const char *pNameSTD, const char *pNameDST, const char *pOlsonName, const char *pDescription) {
    TILT_INFO(tilt, "Test %s", pOlsonName);
    TILT_EXPECT_TRUE(tilt, s_pSystemTimeZone->SetUserTimeZone(utcOffsetSTD, utcOffsetDST, utcDSTStart, utcDSTEnd, pNameSTD, pNameDST, pOlsonName, pDescription), "Set user timezone failed");

    LTTime utc1 = LTTime_Seconds(LTTime_GetSeconds(utcDSTStart) - 1); // STD
    LTTime utc2 = LTTime_Seconds(LTTime_GetSeconds(utcDSTStart) + 1); // DST
    LTTime utc3 = LTTime_Seconds(LTTime_GetSeconds(utcDSTEnd) + 1);   // STD
    TILT_EXPECT_FALSE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc1, NULL), "Before DST %lld < %lld", LT_Ps64(LTTime_GetSeconds(utc1)), LT_Ps64(LTTime_GetSeconds(utcDSTStart)));
    TILT_EXPECT_TRUE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc2, NULL), "During DST %lld < %lld < %lld", LT_Ps64(LTTime_GetSeconds(utcDSTStart)), LT_Ps64(LTTime_GetSeconds(utc2)), LT_Ps64(LTTime_GetSeconds(utcDSTEnd)));
    TILT_EXPECT_FALSE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc3, NULL), "After DST %lld < %lld", LT_Ps64(LTTime_GetSeconds(utcDSTEnd)), LT_Ps64(LTTime_GetSeconds(utc3)));

    LTTime utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc1, NULL);
    s32 local = LTTime_GetSeconds(utcLocal);
    s32 utc = LTTime_GetSeconds(utc1);
    s32 offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
    utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc2, NULL);
    local = LTTime_GetSeconds(utcLocal);
    utc = LTTime_GetSeconds(utc2);
    offset = LTTime_GetSeconds(utcOffsetDST);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "DST offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
    utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc3, NULL);
    local = LTTime_GetSeconds(utcLocal);
    utc = LTTime_GetSeconds(utc3);
    offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
}

static void TestUserTimeZoneDSTSouthern(Tilt *tilt, LTTime utcOffsetSTD, LTTime utcOffsetDST, LTTime utcDSTStart, LTTime utcDSTEnd, const char *pNameSTD, const char *pNameDST, const char *pOlsonName, const char *pDescription) {
    TILT_INFO(tilt, "Test %s", pOlsonName);
    TILT_EXPECT_TRUE(tilt, s_pSystemTimeZone->SetUserTimeZone(utcOffsetSTD, utcOffsetDST, utcDSTStart, utcDSTEnd, pNameSTD, pNameDST, pOlsonName, pDescription), "Set user timezone failed");

    LTTime utc1 = LTTime_Seconds(LTTime_GetSeconds(utcDSTStart) - 1); // STD
    LTTime utc2 = LTTime_Seconds(LTTime_GetSeconds(utcDSTStart) + 1); // DST
    LTTime utc3 = LTTime_Seconds(LTTime_GetSeconds(utcDSTEnd) + 1);   // STD
    TILT_EXPECT_FALSE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc1, NULL), "Before DST %lld < %lld", LT_Ps64(LTTime_GetSeconds(utc1)), LT_Ps64(LTTime_GetSeconds(utcDSTStart)));
    TILT_EXPECT_TRUE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc2, NULL), "During DST %lld < %lld < %lld", LT_Ps64(LTTime_GetSeconds(utcDSTStart)), LT_Ps64(LTTime_GetSeconds(utc2)), LT_Ps64(LTTime_GetSeconds(utcDSTEnd)));
    TILT_EXPECT_FALSE(tilt, s_pSystemTimeZone->IsClockTimeUTCDaylightSaving(utc3, NULL), "After DST %lld < %lld", LT_Ps64(LTTime_GetSeconds(utcDSTEnd)), LT_Ps64(LTTime_GetSeconds(utc3)));

    LTTime utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc1, NULL);
    s32 local = LTTime_GetSeconds(utcLocal);
    s32 utc = LTTime_GetSeconds(utc1);
    s32 offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
    utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc2, NULL);
    local = LTTime_GetSeconds(utcLocal);
    utc = LTTime_GetSeconds(utc2);
    offset = LTTime_GetSeconds(utcOffsetDST);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "DST offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
    utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc3, NULL);
    local = LTTime_GetSeconds(utcLocal);
    utc = LTTime_GetSeconds(utc3);
    offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
}

static void TestUserTimeZoneSTD(Tilt *tilt, LTTime utcOffsetSTD, LTTime utcOffsetDST, LTTime utcDSTStart, LTTime utcDSTEnd, const char *pNameSTD, const char *pNameDST, const char *pOlsonName, const char *pDescription) {
    TILT_INFO(tilt, "Test %s", pOlsonName);
    TILT_EXPECT_TRUE(tilt, s_pSystemTimeZone->SetUserTimeZone(utcOffsetSTD, utcOffsetDST, utcDSTStart, utcDSTEnd, pNameSTD, pNameDST, pOlsonName, pDescription), "Set user timezone failed");
    LTTime utc1 = LTTime_Seconds(1706770800); // STD
    LTTime utc2 = LTTime_Seconds(1719813600); // STD
    LTTime utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc1, NULL);
    s32 local = LTTime_GetSeconds(utcLocal);
    s32 utc = LTTime_GetSeconds(utc1);
    s32 offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
    utcLocal = s_pSystemTimeZone->ClockTimeUTCToLocal(utc2, NULL);
    local = LTTime_GetSeconds(utcLocal);
    utc = LTTime_GetSeconds(utc2);
    offset = LTTime_GetSeconds(utcOffsetSTD);
    TILT_EXPECT_TRUE(tilt, local - utc == offset, "STD offset %ld - %ld == %ld", LT_Ps32(local), LT_Ps32(utc), LT_Ps32(offset));
}

static void TestLTTimeZoneUserData(Tilt *tilt) {
    /* Test STD+DST timezones */
    TestUserTimeZoneDSTNorthern(tilt, LTTime_Seconds(-360 * 60), LTTime_Seconds(-300 * 60), LTTime_Seconds(1710057600), LTTime_Seconds(1730617200), "CST", "CDT", "America/Chicago", "Central (CST/CDT) - US & Canada; Mexico near US border");
    TestUserTimeZoneDSTNorthern(tilt, LTTime_Seconds(-210 * 60), LTTime_Seconds(-150 * 60), LTTime_Seconds(1710048600), LTTime_Seconds(1730608200), "NST", "NDT", "America/St_Johns", "Newfoundland (NST/NDT)");
    TestUserTimeZoneDSTNorthern(tilt, LTTime_Seconds( -60 * 60), LTTime_Seconds(   0 * 60), LTTime_Seconds(1711846800), LTTime_Seconds(1729990800), "AZOT", "AZOST", "Atlantic/Azores", "Azores");
    TestUserTimeZoneDSTNorthern(tilt, LTTime_Seconds(   0 * 60), LTTime_Seconds(  60 * 60), LTTime_Seconds(1711846800), LTTime_Seconds(1729990800), "GMT", "BST", "Europe/London", "United Kingdom (GMT/BST)");
    TestUserTimeZoneDSTSouthern(tilt, LTTime_Seconds( 570 * 60), LTTime_Seconds( 630 * 60), LTTime_Seconds(1728145800), LTTime_Seconds(1743870600), "ACST", "ACDT", "Australia/Adelaide", "South Australia (ACST/ACDT)");
    TestUserTimeZoneDSTSouthern(tilt, LTTime_Seconds( 720 * 60), LTTime_Seconds( 780 * 60), LTTime_Seconds(1727532000), LTTime_Seconds(1743861600), "NZST", "NZDT", "Pacific/Auckland", "New Zealand (NZST/NZDT)");

    /* Test STD-only timezones */
    TestUserTimeZoneSTD(tilt, LTTime_Seconds(-420 * 60), LTTime_Seconds(0), LTTime_Seconds(0), LTTime_Seconds(0), "MST", NULL, "America/Phoenix", "Mountain Standard (MST) - Arizona; western Mexico; Yukon");
    TestUserTimeZoneSTD(tilt, LTTime_Seconds(   0 * 60), LTTime_Seconds(0), LTTime_Seconds(0), LTTime_Seconds(0), "GMT", NULL, "Africa/Abidjan", "Far western Africa; Iceland (GMT)");
    TestUserTimeZoneSTD(tilt, LTTime_Seconds( 330 * 60), LTTime_Seconds(0), LTTime_Seconds(0), LTTime_Seconds(0), "IST", NULL, "Asia/Kolkata", "India (IST)");

    /* Test numeric timezones */
    TestUserTimeZoneDSTNorthern(tilt, LTTime_Seconds(   0 * 60), LTTime_Seconds( 120 * 60), LTTime_Seconds(1711846800), LTTime_Seconds(1729990800), "+00", "+02", "Antarctica/Troll", "Troll Station in Antarctica");
    TestUserTimeZoneDSTSouthern(tilt, LTTime_Seconds( 630 * 60), LTTime_Seconds( 660 * 60), LTTime_Seconds(1728142200), LTTime_Seconds(1743865200), "+10:30", "+11", "Australia/Lord_Howe", "Lord Howe Island");
    TestUserTimeZoneSTD(tilt, LTTime_Seconds( 345 * 60), LTTime_Seconds(0), LTTime_Seconds(0), LTTime_Seconds(0), "+05:45", NULL, "Asia/Kathmandu", "Nepal");
}

static const TiltEngineTest s_tests[] = {
    { TestLTTimeZone,         "TestLTTimeZone",         "Tests all aspects of timezoning", 0 },
    { TestLTTimeZoneUserData, "TestLTTimeZoneUserData", "Tests user timezone",             0 },
};

/*******************************************************  Hook functions */

void BeforeAllTests(Tilt *tilt) {
    s_tilt  = tilt;
    s_pCore = LT_GetCore();
    s_pSystemTimeZone = lt_openlibrary(LTSystemTimeZone);
    TILT_ASSERT_TRUE(tilt, s_pSystemTimeZone, "Cannot open LTSystemTimeZone");
}

void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_pSystemTimeZone);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

static int UnitTestLTSystemTimeZoneImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemTimeZoneImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemTimeZoneImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemTimeZone, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemTimeZone, UnitTestLTSystemTimeZoneImpl_Run, 1536) LTLIBRARY_DEFINITION;

