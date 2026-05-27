/******************************************************************************
 * platforms/apple/include/apple/AppleLTPlatformDefs.h
 *                         - Easier conditionals to tell what Apple platform
 *                           platform your current LT compilation is targeting.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <TargetConditionals.h>

/**************************************************************************
   These Apple target conditionals need to be tested in a certain order or *
   they don't work.  Don't change this.                                     *
                                                                              */
#if TARGET_IPHONE_SIMULATOR == 1
    #define LT_APPLE_PLATFORM_NAME              "iOS"
    #define LT_APPLE_PLATFORM_IPHONE_SIMULATOR
    #define LT_APPLE_PLATFORM_IOS
#elif TARGET_OS_IOS == 1
    #define LT_APPLE_PLATFORM_NAME              "iOS"
    #define LT_APPLE_PLATFORM_IOS
#elif TARGET_OS_TV == 1
    #define LT_APPLE_PLATFORM_NAME              "AppleTVOS"
    #define LT_APPLE_PLATFORM_TVOS
#elif TARGET_OS_WATCH == 1
    #define LT_APPLE_PLATFORM_NAME              "AppleWatchOS"
    #define LT_APPLE_PLATFORM_WATCHOS
#elif TARGET_OS_OSX == 1
    #define LT_APPLE_PLATFORM_NAME              "MacOS"
    #define LT_APPLE_PLATFORM_MACOS
#else
    #define LT_APPLE_PLATFORM_NAME              "UnknownAppleOS"
    #define LT_APPLE_PLATFORM_UNKNOWN
#endif

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Nov-21   augustus    created
 */
