/*******************************************************************************
 * <lt/net/backoff/LTNetBackoff.h> 
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_BACKOFF_LTNETBACKOFF_H
#define ROKU_LT_INCLUDE_LT_NET_BACKOFF_LTNETBACKOFF_H

#include <lt/LTObject.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

enum {
    kLTNetBackoff_RetryForever = LT_U32_MAX
};

typedef_LTObject(LTNetBackoff, 1) {
    bool (*Set)(LTNetBackoff *backoff, LTTime minDelay, LTTime maxInitialDelay, LTTime maxDelay, u32 maxAttempts);
    /**< Initialize the backoff object with parameters for calculating exponential backoff with full jitter
     *
     * @param backoff initialized LTNetBackoff object
     * @param minDelay enforce a minimum delay on backoff. If the calculated backoff is less than minDelay, minDelay will be returned.
     * @param maxInitialDelay maximum delay time for the first retry in exponential backoff. If this is set to 0,
     * the backoff start will pick a value between 0 and maxDelay. This value controls how exponential backoff will scale with every retry.
     * @param maxDelay maximum delay time for exponential backoff. The backoff time can be equal
     * to but never exceed this value.
     * @param maxAttempts maximum attempts allowed for exponential backoff. For example, if maxAttempts is 3, then
     * 1. GetNextBackoff() -> returns backoff
     * 2. GetNextBackoff() -> returns backoff
     * 3. GetNextBackoff() -> returns backoff
     * 4. GetNextBackoff() -> Max attempts reached! Will return LTTime_Infinite()!
     * 
     * @return true on success, false if parameters do not follow the relations.
     * @important minDelay should be <= maxInitialDelay
     * @important minDelay should be <= maxDelay
     * @important maxInitialDelay should be <= maxDelay
     * 
     * @see GetNextBackoff()
     */

    void (*Reset)(LTNetBackoff *backoff);
    /**< Reset the state of the backoff to the last Set(). Backoff object can then be reused with the same parameters.
     *
     * Note that if maxInitialDelay was 0, then the starting backoff will be assigned new random value between 0 and maxDelay.
     *
     * @param backoff initialized LTNetBackoff object
     */

    LTTime (*GetNextBackoff)(LTNetBackoff *backoff);
    /**< Returns the backoff time for the next attempt.
     *
     * The backoff time is calculated as exponential backoff with full jitter and is based on the implementation in FreeRTOS.
     * The returned backoff delay can be expressed as:
     * delay = random(0, min(baseDelay * 2^attempt, maxDelay)), where attempt begins from 0.
     *
     * Example:
     * baseDelay = random(minDelay, maxInitialDelay)
     * Attempt 0: delay = random(minDelay, min(baseDelay * 2^0, maxDelay))
     * Attempt 1: delay = random(minDelay, min(baseDelay * 2^1, maxDelay))
     * Attempt 2: delay = random(minDelay, min(baseDelay * 2^2, maxDelay))
     * ...
     *
     * @param backoff initialized LTNetBackoff object
     * @return LTTime the time client should wait before next attempt.
     * If maxAttempts is reached, LTTime_Infinite() is returned to indicate there are no more retries.
     */

    LTTime (*GetNextAttemptMaxDelay)(const LTNetBackoff *backoff);
    /**< Get the upper bound of what the next delay returned by GetNextBackoff() will be. This value is always <= maxDelay.
     *
     * @param backoff initialized LTNetBackoff object
     */

    u32 (*GetNumAttempts)(const LTNetBackoff *backoff);
    /**< Get the current number of attempts in the backoff. This is the number of times GetNextBackoff() has been called. 
     * 
     * @param backoff initialized LTNetBackoff object
     * @return current number of backoff attempts (number of times GetNextBackoff() was called)
    */
    
    u32 (*GetMaxAttempts)(const LTNetBackoff *backoff);
    /**< Get the max number of attempts backoff was configured with by Set()
     * 
     * @param backoff initialized LTNetBackoff object
     * @return max number of backoff attempts
     */

    LTTime (*GetRandomBackoff)(const LTNetBackoff *backoff);
    /**< Returns a uniformly distributed random time between maxInitialDelay and maxDelay, in the scale of milliseconds.
     *
     * The primary purpose of this function is to expose the random number generator within LTNetBackoff
     * for testing. However, it may also be useful as a way to get a random backoff instead of an
     * exponential backoff with jitter.
     *
     * @note this function will not increment the attempt count. It will
     * only compute a value using maxInitialDelay and maxDelay
     *
     * @param backoff initialized LTNetBackoff object
     * @return LTTime a uniformly distributed time between maxInitialDelay and maxDelay
     */
}
LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_BACKOFF_LTNETBACKOFF_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Mar-24   aurelian    created
 */
