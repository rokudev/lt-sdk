/*******************************************************************************
 * LTUtilitySuntime
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltutility_suntime LTUtilitySuntime
 * @ingroup ltutility
 *
 * @brief Utility to fetch sunset/sunise time.
 */

#ifndef LTUTILITY_SUNTIME_H
#define LTUTILITY_SUNTIME_H

#include <lt/LTTypes.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*******************************************************************************
** API
*******************************************************************************/

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilitySuntime, 1);

/**
 * @brief Utility to calculate the approximate sunset/sunrise time. It calculates
 * the time on every GetSunsetTime/GetSunriseTime request and returns time in
 * LTTime type, in an epoch format.
 * @ingroup ltutility_suntime
 */
struct LTUtilitySuntimeApi {
    INHERIT_LIBRARY_BASE
    LTTime (*GetSunsetTime)(LTTime utc, double latitude, double longitude, s32 tzOffset);
        /**<
        * Get sunset time in epoch.
        *
        * @param[in] utc current local utc epoch
        * @param[in] latitude device location latitude value
        * @param[in] longitude device location longitude value
        * @param[in] tzOffset device current timezone offset
        * @return sunset time epoch
        */

    LTTime (*GetSunriseTime)(LTTime utc, double latitude, double longitude, s32 tzOffset);
        /**<
        * Get sunrise time in epoch.
        *
        * @param[in] utc current local utc epoch
        * @param[in] latitude device location latitude value
        * @param[in] longitude device location longitude value
        * @param[in] tzOffset device current timezone offset
        * @return sunrise time epoch
        */
};

LT_EXTERN_C_END
#endif /* #ifndef LTUTILITY_SUNTIME_H */
