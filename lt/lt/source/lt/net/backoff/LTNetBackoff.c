/*******************************************************************************
 * lt/net/backoff/LTNetBackoff.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/net/backoff/LTNetBackoff.h>

DEFINE_LTLOG_SECTION("net.backoff");

/*____________________________
  LTNetBackoff Private Data */
typedef_LTObjectImpl(LTNetBackoff, LTNetBackoffImpl) {
    LTTime minDelay;            /* Minimum delay backoff can be */
    LTTime maxInitialDelay;     /* Maximum delay of the first backoff calculation */
    LTTime maxDelay;            /* Maximum possible delay that can be returned by GetNextBackoff() */
    LTTime nextAttemptMaxDelay; /* Maximum delay that can be returned by the next call to GetNextBackoff(). Doubled every call to GetNextBackoff(), until it reaches maxDelay. */
    u32 nAttempt;               /* Current attempt. Number of times GetNextBackoff() has been called. */
    u32 maxAttempts;            /* Maximum number of attempts. Once this value is reached, GetNextBackoff() will return LTTime_Infinite() */
}
LTOBJECT_API;

static LTUtilityByteOps * s_byteOps = NULL;

/*___________________
  Helper functions */

// If a random number generator is useful in other places, consider creating an LTUtilityRandom
static LTTime RandomBetween(LTTime min, LTTime max) {
    // if min == max, skip number generation and return min
    if (LTTime_IsEqual(min, max)) return min;
    // in case min > max, return min
    if (LTTime_IsGreaterThan(min, max)) return min;

    // Range is in milliseconds to avoid u32 overflow
    u32 range = LTTime_GetMilliseconds(max) - LTTime_GetMilliseconds(min) + 1;
    // Largest possible random value that is accepted.
    // This is required since a modulo is applied to the random value to get it into the range. Without
    // this bound, the modulo operation would introduce a bias into the distribution due to the
    // extra numbers that "wrap around" when modulo'd to be within the range.
    u32 maxMultiple = LT_U32_MAX - (LT_U32_MAX % range);

    // Rejection sampling in order to generate a uniformly random number in the range
    u32 randomInt = 0;
    do {
        // GenRandomBytes will produce a uniformly distributed set of bytes, so
        // the u32 value will also be uniformly distributed.
        s_byteOps->GenRandomBytes((u8 *)&randomInt, sizeof(randomInt));

        // Accept the randomly generated number if it is less than the max
        if (randomInt < maxMultiple) {
            // Scale the value down to the range and return as LTTime
            return LTTime_Add(min, LTTime_Milliseconds(randomInt % range));
        }
        // Else, generate a new random number and try again
    } while (true);

    return LTTime_Zero();
}

/*__________________________________
  LTNetBackoff API Implementation */
static bool LTNetBackoffImpl_Set(LTNetBackoffImpl *backoff, LTTime minDelay, LTTime maxInitialDelay, LTTime maxDelay, u32 maxAttempts) {
    backoff->nAttempt = 0;
    backoff->maxAttempts = maxAttempts;
    backoff->minDelay = minDelay;
    backoff->maxInitialDelay = maxInitialDelay;
    backoff->maxDelay = maxDelay;
    // Pick a random starting delay if max initial delay is 0
    backoff->nextAttemptMaxDelay = LTTime_IsZero(maxInitialDelay) ? RandomBetween(LTTime_Zero(), maxDelay) : maxInitialDelay;
    
    // Warn client if delay ranges are suspicious
    bool result = true;
    if (LTTime_IsGreaterThan(minDelay, maxInitialDelay)) {
        LTLOG_YELLOWALERT("range.min.initial", "minDelay > maxInitialDelay");
        result = false;
    }
    if (LTTime_IsGreaterThan(minDelay, maxDelay)) {
        LTLOG_YELLOWALERT("range.min.max", "minDelay > maxDelay");
        result = false;
    }
    if (LTTime_IsGreaterThan(maxInitialDelay, maxDelay)) {
        LTLOG_YELLOWALERT("range.initial.max", "maxInitialDelay > maxDelay");
        result = false;
    }
    return result;
}

static void LTNetBackoffImpl_Reset(LTNetBackoffImpl *backoff) {
    LTNetBackoffImpl_Set(backoff, backoff->minDelay, backoff->maxInitialDelay, backoff->maxDelay, backoff->maxAttempts);
}

static LTTime LTNetBackoffImpl_GetNextBackoff(LTNetBackoffImpl *backoff) {
    // Exponential backoff with full jitter
    if (backoff->nAttempt < backoff->maxAttempts || backoff->maxAttempts == kLTNetBackoff_RetryForever) {
        // Jitter: Pick a random value between minDelay and the current max delay
        LTTime delay = RandomBetween(backoff->minDelay, backoff->nextAttemptMaxDelay);

        // Exponential: double the max possible delay for the next attempt, as long as it doesn't go over the max
        if (LTTime_IsLessThan(backoff->nextAttemptMaxDelay, LTTime_Divide(backoff->maxDelay, 2))) {
            backoff->nextAttemptMaxDelay.nNanoseconds += backoff->nextAttemptMaxDelay.nNanoseconds;
        } else {
            backoff->nextAttemptMaxDelay = backoff->maxDelay;
        }

        // Increment attempt and return new delay
        ++backoff->nAttempt;
        return delay;
    }
    // Out of attempts, indicated by a value of LTTime_Infinite
    return LTTime_Infinite();
}

static LTTime LTNetBackoffImpl_GetNextAttemptMaxDelay(const LTNetBackoffImpl *backoff) {
    return backoff->nextAttemptMaxDelay;
}

static u32 LTNetBackoffImpl_GetNumAttempts(const LTNetBackoffImpl *backoff) {
    return backoff->nAttempt;
}

static u32 LTNetBackoffImpl_GetMaxAttempts(const LTNetBackoffImpl *backoff) {
    return backoff->maxAttempts;
}

static LTTime LTNetBackoffImpl_GetRandomBackoff(const LTNetBackoffImpl *backoff) {
    return RandomBetween(backoff->maxInitialDelay, backoff->maxDelay);
}

/*_________________________________________________
  LTNetBackoff Object Constructor and Destructor */
static bool LTNetBackoffImpl_ConstructObject(LTNetBackoffImpl *backoff) {
    LT_UNUSED(backoff);
    return true;
}

static void LTNetBackoffImpl_DestructObject(LTNetBackoffImpl *backoff) {
    LT_UNUSED(backoff);
}

/*______________________________________
  LTNetBackoff LTObjectApi definition */
define_LTObjectImplPublic(LTNetBackoff, LTNetBackoffImpl,
                    Set,
                    Reset,
                    GetNextBackoff,
                    GetNextAttemptMaxDelay,
                    GetNumAttempts,
                    GetMaxAttempts,
                    GetRandomBackoff);

/*______________________________________
  LTNetBackoff Root interface binding */
static bool LTNetBackoff_LibInit(void) {
    s_byteOps = lt_openlibrary(LTUtilityByteOps);
    return s_byteOps != NULL;
}

static void LTNetBackoff_LibFini(void) {
    lt_closelibrary(s_byteOps);
}

define_LTObjectLibrary(1, LTNetBackoff_LibInit, LTNetBackoff_LibFini);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Mar-24   aurelian    created
 */