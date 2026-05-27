/*******************************************************************************
 * <lt/system/timezone/LTSystemTimeZone.h>   LTSystemTimeZone lib root interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltsystem_timezone LTSystemTimeZone
 * @ingroup ltsystem
 * @{
 *
 * @brief This library provides access to the system time zone, and
 * converts clock time to and from that time zone.
 */

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_TIMEZONE_LTSYSTEMTIMEZONE_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_TIMEZONE_LTSYSTEMTIMEZONE_H

#include <lt/core/LTTime.h>
LT_EXTERN_C_BEGIN

/* ________________________________________
___LTSystemTimeZone forward declarations */
typedef struct LTTimeZone LTTimeZone;
typedef struct LTCalendarTime LTCalendarTime;

/**
 * @struct LTSystemTimeZone
 * @brief The API for timezone operations.
 */

/* ________________
___LTSystemTimeZone library root interface */
TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTSystemTimeZone, 1);

struct LTSystemTimeZoneApi {

    INHERIT_LIBRARY_BASE

    const LTTimeZone * (* GetKnownTimeZones)(void);
    /**< Gets the list of known time zones.
     *
     *   GetKnownTimeZones() returns a pointer to the first element of a C array
     *   of LTTimeZone pointers, which represents known world time zones. pZoneID
     *   of the last element of the array is NULL. So we can iterate over it as follows:<pre>
     *       const LTTimeZone * pTimeZone = pLTSystemTimeZone->GetKnownTimeZones();
     *       while (pTimeZone->pZoneID) {
     *           LT_GetCore()->ConsolePrint("Got time zone: %s with id: %s\n", pTimeZone->pNameGeneric, pTimeZone->pZoneID);
     *           pTimeZone++;
     *       };
     *   </pre>
     *
     *   @return a pointer to the first element of the LTTimeZone array. */

    const LTTimeZone * (* GetTimeZoneFromID)(const char * pZoneID);
    /**< Gets the LTTimeZone identified by pZoneID.
     *
     * @param[out]  pZoneID The zone ID of the time zone to get.
     * @return      a pointer to time zone identified by pZoneID or NULL if not found.
     * @note        Valid pZoneID values can be determined by calling GetKnownTimeZones()
     * @see         GetKnownTimeZones() */

    const char * (* GetSystemTimeZoneID)(void);
    /**< Gets the currently set system time zone ID.
     *
     * @return the time zone ID of the currently set system time zone. */

    void (* SetSystemTimeZoneID)(const char * pZoneID);
    /**< Sets the currently set system time zone ID.
     *
     * @param[in] pZoneID The zone ID to set as the currently set system time zone. */

    bool (* IsClockTimeUTCDaylightSaving)(const LTTime clockTimeUTC, const char * pZoneID);
    /**< Determines whether or a clock time UTC falls within Daylight Saving Time of a given time zone.
     *
     * @param[in] clockTimeUTC The clock time UTC to determine whether or not is with DST of pZoneID.
     * @param[in] pZoneID The time zone ID to use for the determination.  If NULL, the system time zone is used.
     * @return whether or not the clock time UTC falls within the Daylight Saving Time period of pZoneID. */

    LTTime (* GetClockTimeLocal)(const char ** ppSystemTimeZoneIDOfClockTimeLocalToSet);
    /**< Gets the local time in nanoseconds since 1/1/1970.
     *
     * @param[out] ppSystemTimeZoneIDOfClockTimeLocalToSet the address of a const char * that, if non-NULL, will be set to
     *             point to the const char * time zone ID of the current system time zone, corresponding to the local clock
     * @return the local clock time in the currently set system time zone.
     * @note If the system time zone is not set, clock time UTC is returned */

    LTTime (* ClockTimeUTCToLocal)(const LTTime clockTimeUTC, const char * pZoneID);
    /**< Converts clock time UTC into clock time local.
     *
     * @param[in] clockTimeUTC The clock time utc to convert to local time.
     * @param[in] pZoneID The time zone ID of the time zone to use for the conversion, if NULL the currently set system time zone is used
     * @return the clock time UTC converted to clock time local.
     * @note If pZoneID is NULL and the system time zone is not set, no conversion is performed and clockTimeUTC is returned.
    */

    LTTime   (* ClockTimeLocalToUTC)(const LTTime clockTimeLocal, const char * pZoneID);
    /**< Converts clock time local into clock time UTC.
     *
     * @param[in] clockTimeLocal The clock time local to convert to UTC time.
     * @param[in] pZoneID The time zone ID of the time zone to use for the conversion; if NULL, the currently set system time zone is used.
     * @return the clock time local converted to clock time UTC.
     * @note If pZoneID is NULL and the system time zone is not set, no conversion is performed and clockTimeLocal is returned.
     */

    void     (* ClockTimeToCalendarTime)(const LTTime clockTime, LTCalendarTime * pCalendarTimeToSet);
    /**< Converts clock time into calendar time.
     *
     * @param[in]  clockTime The clock time to convert to calendar time.
     * @param[out] pCalendarTimeToSet The calendar structure to fill with the converted clock time.
     */

    bool     (* CalendarTimeToClockTime)(const LTCalendarTime * pCalendarTime, LTTime * pClockTimeToSet);
    /**< Converts calendar time into clock time.
     *
     * @param[in]  pCalendarTime The calendar time structure to convert to clock time.
     * @param[out] pClockTimeToSet The clock time LTTime structure to fill with the converted calendar time.
     * @return @c true if pCalendarTime is a valid calendar time between 1/1/1970 12:00:00.000 AM and 12/31/2100 12:59:59.999 PM, @c false otherwise.
     */

    int      (* ClockTimeToHumanReadableString)(const LTTime clockTime, bool b12Hour, const char * pZoneID, char * pStringBuffToFill, u32 nStringBuffSize);
    /**< Converts clock time into a human-readable string.
     *
     * @param[in]  clockTime The clock time to convert into a human-readable string.
     * @param[in]  b12Hour @c true to output time in 12 hour format with am/pm, or @c false to output 24 hour [military] time without am/pm.
     * @param[in]  pZoneID The time zone ID to include in the human readable string, or NULL for the system time zone.
     * @param[out] pStringBuffToFill The C string buffer to fill with the human-readable string.
     * @param[in]  nStringBuffSize The size of pstringBuffToFill, in bytes. Must be >= 48.
     * @return the number of characters (excluding the NULL terminator) that were written to pStringBuffToFill.
     * @note  pStringBuffToFill must be non-NULL and point to an allocated buffer of at least nStringBuffSize bytes which must be >= 48.
     *        If pStringBuffToFill points to a buffer less than 48 bytes then *pStringBuffToFill will be set to 0 (empty string).
     */

    bool (* SetUserTimeZone)(LTTime utcOffsetSTD, LTTime utcOffsetDST, LTTime utcDSTStart, LTTime utcDSTEnd, const char *pAbbreviationSTD, const char *pAbbreviationDST, const char *pOlsonName, const char *pDescription);
    /**< Sets the system time to a user defined time zone 
     *
     * @param[in] utcOffsetSTD      the offset time relative to UTC used when standard time is in effect, e.g. LTTime_Seconds(-8 * 3600) (8 hours behind UTC).
     * @param[in] utcOffsetDST      the offset time relative to UTC used when daylight saving time is in effect. e.g. (LTTime_Seconds(-7 * 3600) (7 hours behind UTC).
     * @param[in] utcDSTStart       the UTC time of the year when daylight saving time starts, zero if zone doesn't observe daylight saving time.
     * @param[in] utcDSTEnd         the UTC time of the year when daylight saving time ends, zero if zone doesn't observe daylight saving time.
     * @param[in] pAbbreviationSTD  the abbreviated time zone name for generating human readable time strings when in standard time, e.g. PST, EST, GMT, etc. This string must be non-NULL and non-empty, with a max length of 11 characters.
     * @param[in] pAbbreviationDST  the abbreviated time zone name for generating human readable time strings when in daylight saving time, e.g. PDT, EDT, BST, etc. If DST is used, This string must be non-NULL and non-empty, with a max length of 11 characters.
     * @param[in] pOlsonName        the Olson name, for LTTimeZone.pOlsonReferent. This string must be non-NULL and non-empty, with a max length of 47 characters.
     * @param[in] pDescription      the zone description, for LTTimeZone.pNameGeneric and pDescription. Optional. If NULL, keep the existing description. If empty, clear the description.
     * @return true if user time zone set, false if parameters are invalid
     * 
     * @note This function generates an internal LTTimeZone structure in which
     *         pAbbreviationSTD is used for (copied into) the fields:
     *           LTTimeZone.pAbbreviationSTD,
     *           LTTimeZone.pNameSTD,
     *         and pAbbreviationDST is used for (copied into) the fields:
     *           LTTimeZone.pAbbreviationDST,
     *           LTTimeZone.pNameDST
     *
     *       Both utcOffsetSTD and utcOffsetDST must be an LTTime offset that is between (-24 hours..+24 hours), non-inclusive
     *       utcDSTStart must be less than utcDSTEnd and both must serve the same DST period.
     *       If either utcDSTStart or utcDSTStart is zero, then the 3 parameters utcOffsetDST, utcDSTStart, and utcDSTEnd must all be zero
     */
};

/**_____________
  LTTimeZone */
typedef struct LTTimeZone {
    const char * pZoneID;             /**< unique zone ID, NULL-terminated C-string */
    const char * pZoneData;           /**< zone data in IANA POSIX.1003.1 TZ time zone information format */
    const char * pAbbreviationSTD;    /**< abbreviation for standard time */
    const char * pAbbreviationDST;    /**< abbreviation for daylight saving time */
    const char * pNameSTD;            /**< name for standard time */
    const char * pNameDST;            /**< name for daylight saving time */
    const char * pNameGeneric;        /**< generic name */
    const char * pOlsonReferent;      /**< referent to the Olson Database */
    const char * pDescription;        /**< description */
} LTTimeZone;  /**< LTTimeZone represents local time zones with time zone offset and DST information in IANA POSIX.1003.1 TZ time zone information format */

/**_________________
  LTCalendarTime */
typedef struct LTCalendarTime {
    u16 nYear;          /**< year                             */
    u16 nMonth;         /**< month (1 == January)             */
    u16 nDay;           /**< day of month (starting at 1)     */
    u16 nHour;          /**< hour (0 to 23)                   */
    u16 nMinute;        /**< minute (0 to 59)                 */
    u16 nSecond;        /**< second (0 to 59)                 */
    u16 nMillisecond;   /**< millisecond (0 to 999)           */
    u16 nWeekday;       /**< weekday (0 == Sunday)            */
} LTCalendarTime; /**< LTCalendarTime represents date and time in a format fit for human consumption */

#ifndef DOXY_SKIP
LT_STATIC_ASSERT_SIZE_32_64(LTTimeZone, 36, 72)
LT_STATIC_ASSERT_SIZE_32_64(LTCalendarTime, 16, 16)
#endif

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_TIMEZONE_LTSYSTEMTIMEZONE_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 *  22-Dec-21   augustus    added ClockTimeToHumanReadableString
 */
