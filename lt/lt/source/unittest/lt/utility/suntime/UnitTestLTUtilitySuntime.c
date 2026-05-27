/*******************************************************************************
 * UnitTestLTUtilitySuntime.c
 * -----------------------------------
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/suntime/LTUtilitySuntime.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/timezone/LTSystemTimeZone.h>
#include <tilt/JiltEngine.h>

static JiltEngine     *s_engine;
static Tilt           *s_tilt;
static LTUtilitySuntime *s_suntime;
static LTSystemTimeZone *s_pLTSystemTimeZone;

struct {
    const char* name;
    double lat;
    double lon;
    s32 tz_min;
    u32 expectedRiseMin;
    u32 expectedSetMin;
} locations[] = {
    {"Mumbai, India",            19.0760,   72.8777,   330,  362, 1154},  // IST  UTC+5:30
    {"New York, USA",            40.7128,  -74.0060,  -240,  328, 1224},  // EDT  UTC-4:00 (DST)
    {"London, UK",               51.5074,   -0.1278,    60,  288, 1272},  // BST  UTC+1:00 (DST)
    {"Sydney, Australia",       -33.8688,  151.2093,   600,  419, 1011},  // AEST UTC+10:00
    {"Tokyo, Japan",             35.6895,  139.6917,   540,  269, 1133},  // JST  UTC+9:00
    {"Reykjavik, Iceland",       64.1355,  -21.8954,     0,  203, 1411},  // UTC
    {"Cape Town, South Africa", -33.9249,   18.4241,   120,  470, 1062},  // SAST UTC+2:00
    {"Buenos Aires, Argentina", -34.6037,  -58.3816,  -180,  481, 1065}   // ART  UTC-3:00
};

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

void UnitTestGetSunriseTime(Tilt *tilt) {
    u8 maxCount = sizeof(locations) / sizeof(locations[0]);
    LTTime utc;
    LTCalendarTime calendarTime = {
        .nYear        = 2025,
        .nMonth       = 6,
        .nDay         = 25,
        .nHour        = 0,
        .nMinute      = 59,
        .nSecond      = 59,
        .nMillisecond = 0,
        .nWeekday     = 0
    };
    s_pLTSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &utc);
    for (u8 i = 0; i < maxCount; i++) {
        LTTime time = s_suntime->GetSunriseTime(utc, locations[i].lat, locations[i].lon, locations[i].tz_min);
        s_pLTSystemTimeZone->ClockTimeToCalendarTime(time, &calendarTime);
        u32 riseMins = calendarTime.nHour * 60 + calendarTime.nMinute;
        TILT_ASSERT_TRUE(tilt, riseMins == locations[i].expectedRiseMin, "%s expected sunrise time not match, %d expected %d",
            locations[i].name, riseMins, locations[i].expectedRiseMin);

        time = s_suntime->GetSunsetTime(utc, locations[i].lat, locations[i].lon, locations[i].tz_min);
        s_pLTSystemTimeZone->ClockTimeToCalendarTime(time, &calendarTime);
        u32 setMins  = calendarTime.nHour * 60 + calendarTime.nMinute;
        TILT_ASSERT_TRUE(tilt, setMins == locations[i].expectedSetMin, "%s expected sunset time not match, %d expected %d",
            locations[i].name, setMins, locations[i].expectedSetMin);

        TILT_INFO(tilt, "%s %llf %llf %d %d %d", locations[i].name, locations[i].lat, locations[i].lon, locations[i].tz_min, riseMins, setMins);
    }
}

static const TiltEngineTest s_tests[] = {
    { UnitTestGetSunriseTime, "GetSunriseTime", "Validate sunrise time of different cities", 0 },
};

static void BeforeAllTests(Tilt *tilt) {
    s_tilt = tilt;
    if (!(s_suntime = lt_openlibrary(LTUtilitySuntime))) {
        TILT_EXPECT_TRUE(tilt, s_suntime != NULL, "Cannot open LTUtilitySuntime");
        return;
    }
    if (!(s_pLTSystemTimeZone = lt_openlibrary(LTSystemTimeZone))) {
        TILT_EXPECT_TRUE(tilt, s_pLTSystemTimeZone != NULL, "Cannot open LTUtilitySuntime");
        return;
    }
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_suntime);
    lt_closelibrary(s_pLTSystemTimeZone);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static int UnitTestLTUtilitySuntimeImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilitySuntimeImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilitySuntimeImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilitySuntime, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilitySuntime, UnitTestLTUtilitySuntimeImpl_Run, 1536) LTLIBRARY_DEFINITION;
