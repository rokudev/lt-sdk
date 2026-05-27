/******************************************************************************
 * <lt/core/LTTime.h>     struct LTTime - inline struct representing time ticks
 *                                                               in nanoseconds
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/**
 * @defgroup ltcore_time LTTime
 * @ingroup ltcore
 * @{
 *
 * @brief A signed 64-bit numeric value in units of nanoseconds that the compiler won't type coerce.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTTIME_H
#define ROKU_LT_INCLUDE_LT_CORE_LTTIME_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/*_______________
 / typedef LTTime - a signed 64 bit numeric value in units of nanoseconds that the compiler won't type coerce */
typedef struct { /* this struct must always be sizeof(s64) so it can be passed by value with zero to minimal penalty. Do not change. */
    s64 nNanoseconds;
    /**< The LTTime nanoseconds value represents a duration.  The duration can be an absolute duration, e.g. 100ns, or a duration
      *  relative to the system boot time, therefore representing system uptime (KernelTime), or a duration relative to the UTC Unix
      *  epoch (January 1, 1970 12:00:00.000000000am UTC) therefore representing clock time UTC.
      *
      *  The LTCore interface provides functions to retrieve the kernel time (uptime), and to get and set timeUTC (time since the Unix epoch).
      *  These are: pCore->GetKernelTime(), pCore->GetClockTimeUTC() and pCore->SetClockTimeUTC(), where LTCore * pCore = LT_GetCore();
      *  The LTSystemTimeZone library provides timezone functionality including getting and setting of the system local time zone, retrieving
      *  local time (based on the system local time zone), converting UTC to local time and vice-versa in any time zone, and for converting
      *  LTTime to and from LTCalendar time, all respecting local time zone standard and daylight saving time rules for all conversions.
      */
} LTTime; /**< LTTime represents a nanosecond precision time duration, either an absolute duration, a duration from system boot, or a duration from the unix epoch */

/* LTTime constant values */
#define     LTTIME_ZERO         (LT_CONSTS64(0))
#define     LTTIME_INFINITE     (LT_S64_MAX)

/*_______________________
 / LTTime API Functions */
/* Creation Functions for constant values and setting, e.g. LTTime time = LTTime_Microseconds(50); */
LT_INLINE LTTime LTTime_Zero(void)                          LT_ISR_SAFE { return (LTTime){ LTTIME_ZERO }; }
LT_INLINE LTTime LTTime_Infinite(void)                      LT_ISR_SAFE { return (LTTime){ LTTIME_INFINITE }; }
LT_INLINE LTTime LTTime_Nanoseconds(s64 nanoseconds)        LT_ISR_SAFE { return *((LTTime *)(&nanoseconds)); }
LT_INLINE LTTime LTTime_Microseconds(s64 microseconds)      LT_ISR_SAFE { return LTTime_Nanoseconds(microseconds * 1000); }
LT_INLINE LTTime LTTime_Milliseconds(s64 milliseconds)      LT_ISR_SAFE { return LTTime_Nanoseconds(milliseconds * 1000000); }
LT_INLINE LTTime LTTime_Seconds(s64 seconds)                LT_ISR_SAFE { return LTTime_Nanoseconds(seconds      * 1000000000); }

/* Initializer Macros because the following doesn't work:
      static LTTime t = LTTime_Milliseconds(50);
   For the static case use:
      static LTTime t = LTTimeInitializer_Milliseconds(50); */
#define LTTimeInitializer_Zero()                        (const LTTime){ .nNanoseconds = LTTIME_ZERO }
#define LTTimeInitializer_Infinite()                    (const LTTime){ .nNanoseconds = LTTIME_INFINITE }
#define LTTimeInitializer_Nanoseconds(nanoseconds)      (const LTTime){ .nNanoseconds = (s64)((nanoseconds)) }
#define LTTimeInitializer_Microseconds(microseconds)    (const LTTime){ .nNanoseconds = (s64)(((s64)(microseconds)) * LT_CONSTS64(1000)) }
#define LTTimeInitializer_Milliseconds(milliseconds)    (const LTTime){ .nNanoseconds = (s64)(((s64)(milliseconds)) * LT_CONSTS64(1000000)) }
#define LTTimeInitializer_Seconds(seconds)              (const LTTime){ .nNanoseconds = (s64)(((s64)(seconds))      * LT_CONSTS64(1000000000)) }

/* Getters for getting and conversion, e.g. LTTime fiftyMS = LTTime_Milliseconds(50); LTTime microsecondsInFiftyMS = LTTime_GetMicroseconds(fiftyMS); */
LT_INLINE s64    LTTime_GetNanoseconds(LTTime time)         LT_ISR_SAFE { return time.nNanoseconds; }
LT_INLINE s64    LTTime_GetMicroseconds(LTTime time)        LT_ISR_SAFE { return time.nNanoseconds / 1000; }
LT_INLINE s64    LTTime_GetMilliseconds(LTTime time)        LT_ISR_SAFE { return time.nNanoseconds / 1000000; }
LT_INLINE s64    LTTime_GetSeconds(LTTime time)             LT_ISR_SAFE { return time.nNanoseconds / 1000000000; }

/* Whole and Fractional time parts of LTTime as LTTime*/
LT_INLINE LTTime LTTime_WholeNanoseconds(LTTime time)       LT_ISR_SAFE { return time; }
LT_INLINE LTTime LTTime_WholeMicroseconds(LTTime time)      LT_ISR_SAFE { time.nNanoseconds /= 100;        time.nNanoseconds *= 100;        return time; }
LT_INLINE LTTime LTTime_WholeMilliseconds(LTTime time)      LT_ISR_SAFE { time.nNanoseconds /= 1000000;    time.nNanoseconds *= 1000000;    return time; }
LT_INLINE LTTime LTTime_WholeSeconds(LTTime time)           LT_ISR_SAFE { time.nNanoseconds /= 1000000000; time.nNanoseconds *= 1000000000; return time; }
LT_INLINE LTTime LTTime_FractionalMicroseconds(LTTime time) LT_ISR_SAFE { time.nNanoseconds -= ((time.nNanoseconds / LT_CONSTS64(1000))       * LT_CONSTS64(1000));       return time; }
LT_INLINE LTTime LTTime_FractionalMilliseconds(LTTime time) LT_ISR_SAFE { time.nNanoseconds -= ((time.nNanoseconds / LT_CONSTS64(1000000))    * LT_CONSTS64(1000000));    return time; }
LT_INLINE LTTime LTTime_FractionalSeconds(LTTime time)      LT_ISR_SAFE { time.nNanoseconds -= ((time.nNanoseconds / LT_CONSTS64(1000000000)) * LT_CONSTS64(1000000000)); return time; }

/* Comparison functions */
LT_INLINE bool  LTTime_IsInfinite(LTTime time)                    LT_ISR_SAFE  { return (time.nNanoseconds == LTTIME_INFINITE); }
LT_INLINE bool  LTTime_IsZero(LTTime time)                        LT_ISR_SAFE  { return (time.nNanoseconds == LTTIME_ZERO); }
LT_INLINE bool  LTTime_IsEqual(LTTime t1, LTTime t2)              LT_ISR_SAFE  { return (t1.nNanoseconds == t2.nNanoseconds); }
LT_INLINE bool  LTTime_IsLessThan(LTTime t1, LTTime t2)           LT_ISR_SAFE  { return (t1.nNanoseconds <  t2.nNanoseconds); }
LT_INLINE bool  LTTime_IsLessThanOrEqual(LTTime t1, LTTime t2)    LT_ISR_SAFE  { return (t1.nNanoseconds <=  t2.nNanoseconds); }
LT_INLINE bool  LTTime_IsGreaterThan(LTTime t1, LTTime t2)        LT_ISR_SAFE  { return (t1.nNanoseconds >  t2.nNanoseconds); }
LT_INLINE bool  LTTime_IsGreaterThanOrEqual(LTTime t1, LTTime t2) LT_ISR_SAFE  { return (t1.nNanoseconds >=  t2.nNanoseconds); }

/* Arithmetic functions */
#define               LTTime_AddTo(t1, t2)                                  LT_ISR_SAFE  LTTimeInline_AddTo(&t1, t2)
#define               LTTime_SubtractFrom(t1, t2)                           LT_ISR_SAFE  LTTimeInline_SubtractFrom(&t1, t2)
#define               LTTime_MultiplyBy(t1, m)                              LT_ISR_SAFE  LTTimeInline_MultiplyBy(&t1, m)
#define               LTTime_DivideBy(t1, d)                                LT_ISR_SAFE  LTTimeInline_DivideBy(&t1, d)

LT_INLINE void   LTTimeInline_AddTo(LTTime * pT1, LTTime t2)           LT_ISR_SAFE { pT1->nNanoseconds += t2.nNanoseconds; }
LT_INLINE void   LTTimeInline_SubtractFrom(LTTime * pT1, LTTime t2)    LT_ISR_SAFE { pT1->nNanoseconds -= t2.nNanoseconds; }
LT_INLINE void   LTTimeInline_MultiplyBy(LTTime * pT1, s64 multiplier) LT_ISR_SAFE { pT1->nNanoseconds *= multiplier;     }
LT_INLINE void   LTTimeInline_DivideBy(LTTime * pT1, s64 divisor)      LT_ISR_SAFE { pT1->nNanoseconds /= divisor;        }

LT_INLINE LTTime LTTime_Add(LTTime t1, LTTime t2)                      LT_ISR_SAFE { LTTime_AddTo(t1, t2);              return t1; }
LT_INLINE LTTime LTTime_Subtract(LTTime t1, LTTime t2)                 LT_ISR_SAFE { LTTime_SubtractFrom(t1, t2);       return t1; }
LT_INLINE LTTime LTTime_Multiply(LTTime t1, s64 multiplier)            LT_ISR_SAFE { LTTime_MultiplyBy(t1, multiplier); return t1; }
LT_INLINE LTTime LTTime_Divide(LTTime t1, s64 divisor)                 LT_ISR_SAFE { LTTime_DivideBy(t1, divisor);      return t1; }
LT_INLINE s64    LTTime_Ratio(LTTime t1, LTTime t2)                    LT_ISR_SAFE { return t1.nNanoseconds /= t2.nNanoseconds;      }

/* sanity check sizse */
#ifndef DOXY_SKIP
LT_STATIC_ASSERT_SIZE_32_64(LTTime, 8, 8)
#endif /* #ifndef DOXY_SKIP */

/*___________________
 / typedef LTTimeBase - struct that captures the time relationship between two distinct clocks at a single point in time, e.g. when kerneltime was this, time UTC was that */
typedef struct LTTimeBase {
    LTTime primaryClockTime;        ///<* A fixed point in time of the primary clock (usually local system's kernel time)
    LTTime secondaryClockTime;      ///<* Value of the secondary clock when primary was at primaryClockTime (secondary clock usually timeUTC)
} LTTimeBase;   ///<* LTTimeBase allows for the accurate and stable representation of the state of two clocks in relation to each other at a fixed point in time
                ///   This is used to correct for the error introduced after calculating/fetching the time UTC.  Namely the non-determinisim in getting the
                ///   kernel time, calculating an offset, and then storing the offset somewhere for later use in retrieiving timeUTC.
                ///   The use of LTTimeBase enables any determination of offset of the secondary clock relative to the primary to be captured at that point and used
                ///    as the offset itself, immutable.

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTTIME_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jan-19   augustus    created
 *  23-May-19   constantine Doxygenation
 *  30-Jun-19   augustus    got rid of C equivalents
 *  01-Jul-19   augustus    added Zero(), SetZero(), IsZero()
 *  10-Jul-19   augustus    added non-static versions of Nanoseconds(), Microseconds(), Milliseconds(), and Seconds()
 *  26-Aug-19   augustus    added LT_ISR_SAFE markers
 *  27-Aug-19   constantine Move Doxymentation to lt/doc/lt/core/LTTime.dox
 *  30-Apr-20   augustus    re-created in C
 *  17-Oct-20   augustus    added LTCalendarTime and LTTimeZone
 *  17-Oct-20   augustus    moved LTCalendarTime and LTTimeZone to LTSystemTimeZone.h
 *  17-May-22   augustus    added LTTimeBase
 *  10-Oct-22   augustus    no more m_
 */
