/*******************************************************************************
 *
 * LTUtilitySuntime
 * -------------------
 *
 * Utility to calculate the approximate sunset/sunrise time. This is the fixed
 * fixed point algorithm based on the following formula, where it takes latitude
 * /longitude coordinates, timezone and current date as input.
 *
 * cos(omega) = tan(phi) * tan(delta)
 * where,
 *    "omega" is the solar hour angle at either sunrise (when negative value is taken)
 *            or sunset (when positive value is taken);
 *    "phi"   is the latitude of the observer on the Earth;
 *    "delta" is the sun declination.
 *
 * Reference: https://en.wikipedia.org/wiki/Sunrise_equation
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/suntime/LTUtilitySuntime.h>
#include <lt/system/timezone/LTSystemTimeZone.h>

DEFINE_LTLOG_SECTION("suntime");

static struct Statics {
    LTSystemTimeZone *pLTSystemTimeZone;
    u32 sunsetTime;
    u32 sunriseTime;
} S;

/* Fixed point constants */
enum {
    kFixedShift         = 16,                 /* Q16 fixed point calculation */
    kFixedOne           = (1 << kFixedShift),
    kPiValue            = 205887,             /* pi in Q16.16 ≈ 3.141592 * 65536*/
    kTwoPiValue         = 411774,             /* 2*pi */
    kDegtoRadFixedPoint = 1144,               /* kPiValue/180 * 65536 ≈ 0.0174533 * 65536 */
    kRadtoDegFixedPoint = 3754936,            /* 180/pi * 65536 ≈ 57.2958 * 65536   */
};

/* Fixed point multiplication and division */
#define FIXED_MUL(a, b) ((s32)(((s64)(a) * (b)) >> kFixedShift))
#define FIXED_DIV(a, b) ((s32)(((((s64)(a) << kFixedShift) + ((b) / 2)) / (b))))
#define IS_LEAP_YEAR(y) (((year) % 4 == 0) && (((y) % 100 != 0) || ((y) % 400 == 0)))

/* Fixed-point sine using a 7th order Taylor approx around 0 (input radians Q16.16)
 * Input range: -π to π (approx) */
static s32 fixedSin(s32 x) {
    // Wrap x between -π and π
    while (x > kPiValue)  x -= kTwoPiValue;
    while (x < -kPiValue) x += kTwoPiValue;

    // Use sine Taylor series: sin(x) ≈ x - x^3/6 + x^5/120 - x^7/5040
    s32 x3 = FIXED_MUL(FIXED_MUL(x,  x), x);
    s32 x5 = FIXED_MUL(FIXED_MUL(x3, x), x);
    s32 x7 = FIXED_MUL(FIXED_MUL(x5, x), x);

    s32 term1 = x;
    s32 term2 = FIXED_DIV(x3, 393216);    // 6 * 65536
    s32 term3 = FIXED_DIV(x5, 7864320);   // 120 * 65536
    s32 term4 = FIXED_DIV(x7, 330301440); // 5040 * 65536

    return term1 - term2 + term3 - term4;
}

/* Fixed-point cosine using sin(x + π/2) */
static s32 fixedCos(s32 x) {
    return fixedSin(x + (kPiValue / 2));
}

/* Fixed-point arccos approximation (input range: -1 to 1 Q16.16)
 * acos(x) ≈ π/2 - x - x^3/6 for x in -1..1 */
static s32 fixed_acos(s32 x) {
    s32 x3 = FIXED_MUL(FIXED_MUL(x, x), x);
    s32 res = (kPiValue >> 1) - x - FIXED_DIV(x3, 393216); // x^3/6
    // Bound result between 0 and π
    if (res < 0) res = 0;
    if (res > kPiValue) res -= kTwoPiValue;
    return res;
}

/* Solar declination angle, returns value in radians */
static s32 solarDeclination(u16 nDoy) {
    s32 B = FIXED_MUL(kTwoPiValue, FIXED_DIV(((s32)nDoy - 1) * kFixedOne, 23920640)); // 365 * (1 << 16)

    s32 cosB  = fixedCos(B);
    s32 sinB  = fixedSin(B);
    s32 cos2B = fixedCos(FIXED_MUL(B, 2));
    s32 sin2B = fixedSin(FIXED_MUL(B, 2));

    const s32 A  = 454;    // 0.006918 * 65536
    const s32 B1 = 26182;  // 0.399912 * 65536
    const s32 B2 = 4598;   // 0.070257 * 65536
    const s32 B3 = 442;    // 0.006758 * 65536
    const s32 B4 = 59;     // 0.000907 * 65536

    return A - FIXED_MUL(B1, cosB) + FIXED_MUL(B2, sinB) - FIXED_MUL(B3, cos2B) + FIXED_MUL(B4, sin2B);
}

// Equation of Time (EoT) in minutes (Q16.16)
static s32 equationOfTime(s32 dayOfYear) {
    s32 B = FIXED_MUL(kTwoPiValue, FIXED_DIV((dayOfYear - 1) * kFixedOne, 23920640)); // 365 * (1 << 16)
    s32 cosB  = fixedCos(B);
    s32 sin2B = fixedSin(FIXED_MUL(B, 2));
    s32 cos2B = fixedCos(FIXED_MUL(B, 2));

    const s32 E1 = 22925; // 7.416 * 65536
    const s32 E2 = 4778;  // 1.45 * 65536
    const s32 E3 = 1799;  // 0.55 * 65536

    return (FIXED_MUL(E1, sin2B) - FIXED_MUL(E2, cosB) - FIXED_MUL(E3, cos2B)) >> kFixedShift; //scale down
}

// Hour angle calculation in radians Q16.16
static s32 hourAngle(s32 latRad, s32 declRad) {
    s32 sinLat  = fixedSin(latRad);
    s32 cosLat  = fixedCos(latRad);
    s32 sinDecl = fixedSin(declRad);
    s32 cosDecl = fixedCos(declRad);

    // Solar elevation angle correction for refraction and solar disc radius: -0.83 degrees
    s32 solarDiskAngle = -543;  // -0.83° in Q16 (approx -0.0145 rad * 65536)
    s32 sinH0 = fixedSin(solarDiskAngle);

    s32 numerator = sinH0 - FIXED_MUL(sinLat, sinDecl);
    s32 denominator = FIXED_MUL(cosLat, cosDecl);

    if (denominator == 0) return 0;

    s32 val = FIXED_DIV(numerator, denominator);
    // bound val to [-1, 1] for acos
    if (val >  kFixedOne) val =  kFixedOne;
    if (val < -kFixedOne) val = -kFixedOne;
    return fixed_acos(val);
}

// Calculate sunrise and sunset times in minutes local time
static void calculateSuntime(double latDeg, double lonDeg, u16 doy, s32* sunriseMinutes, s32* sunsetMinutes) {
    s32 latFp = latDeg * kFixedOne;
    s32 latRad = FIXED_MUL(latFp, kDegtoRadFixedPoint);
    s32 declRad = solarDeclination(doy);
    s32 eot = equationOfTime(doy);
    s32 haRad = hourAngle(latRad, declRad);
    s32 haDeg = FIXED_MUL(haRad, kRadtoDegFixedPoint) >> kFixedShift;

    s32 solarNoonUTC = 720 - (4 * lonDeg) - (eot >> kFixedShift);
    s32 haMin = haDeg * 4;

    *sunriseMinutes = solarNoonUTC - haMin;
    *sunsetMinutes  = solarNoonUTC + haMin;
}

/* Return count of days from 1st Jan */
u16 countDays(u16 year, u16 month, u16 day) {
    const u16 dayBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    u16 doy = dayBeforeMonth[month - 1] + day;
    if (IS_LEAP_YEAR(year) && month > 2) doy += 1;
    return doy;
}

static bool UpdateSuntime(LTTime utc, double latitude, double longitude, s32 tzOffset) {
    LTCalendarTime calendarTime;
    S.pLTSystemTimeZone->ClockTimeToCalendarTime(utc, &calendarTime);
    u32 nDoy = countDays(calendarTime.nYear, calendarTime.nMonth, calendarTime.nDay);

    s32 sunrise;
    s32 sunset;
    calculateSuntime(latitude, longitude, nDoy, &sunrise, &sunset);
    S.sunriseTime = (u32)(sunrise + tzOffset);
    S.sunsetTime = (u32)(sunset + tzOffset);
    return true;
}

static LTTime LTUtilitySuntime_GetSunriseTime(LTTime utc, double latitude, double longitude, s32 tzOffset) {
    if (UpdateSuntime(utc, latitude, longitude, tzOffset)) {
        /* Creating new epoch by adding sunrise minutes to local current date after midnight */
        LTCalendarTime calendarTime;
        LTTime riseTimeClock;
        S.pLTSystemTimeZone->ClockTimeToCalendarTime(utc, &calendarTime);
        calendarTime.nHour   = S.sunriseTime / 60;
        calendarTime.nMinute = S.sunriseTime % 60;
        calendarTime.nSecond = 0;
        S.pLTSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &riseTimeClock);
        return riseTimeClock;
    }
    return LTTime_Zero();
}

static LTTime LTUtilitySuntime_GetSunsetTime(LTTime utc, double latitude, double longitude, s32 tzOffset) {
    if (UpdateSuntime(utc, latitude, longitude, tzOffset)) {
        /* Creating new epoch by adding sunset minutes to local current date after midnight */
        LTCalendarTime calendarTime;
        LTTime setTimeClock;
        S.pLTSystemTimeZone->ClockTimeToCalendarTime(utc, &calendarTime);
        calendarTime.nHour   = S.sunsetTime / 60;
        calendarTime.nMinute = S.sunsetTime % 60;
        calendarTime.nSecond = 0;
        S.pLTSystemTimeZone->CalendarTimeToClockTime(&calendarTime, &setTimeClock);
        return setTimeClock;
    }
    return LTTime_Zero();
}

static void LTUtilitySuntimeImpl_LibFini(void) {
    lt_closelibrary(S.pLTSystemTimeZone);
}

static bool LTUtilitySuntimeImpl_LibInit(void) {
    if (!(S.pLTSystemTimeZone = lt_openlibrary(LTSystemTimeZone))) {
        LTLOG_YELLOWALERT("f.open.timezone", "failed to open LTSystemTimeZone");
        return false;
    }
    return true;
}

define_LTLIBRARY_ROOT_INTERFACE(LTUtilitySuntime,)
    .GetSunsetTime   = &LTUtilitySuntime_GetSunsetTime,
    .GetSunriseTime  = &LTUtilitySuntime_GetSunriseTime,
LTLIBRARY_DEFINITION;
