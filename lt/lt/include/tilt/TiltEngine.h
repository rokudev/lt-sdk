/******************************************************************************
 * <tilt/TiltEngine.h>
 *    ______ ____ __   ______
 *   /_  __//  _// /  /_  __/       TILT: the Test
 *    / /   / / / /    / /                Infrastructure
 *   / /  _/ / / /___ / /                 for LT
 *  /_/  /___//_____//_/
 *
 *  Definitions used by TiltEngine, the standard executor for Tilt test
 *  libraries.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_TILT_TILT_ENGINE_H
#define LT_INCLUDE_TILT_TILT_ENGINE_H

/**
 * @file TiltEngine.h
 *
 * TILT: Test Infrastructure for LT
 * This file contains definitions that are useful for clients of TiltEngine,
 * the standard executor for Tilt test libraries.
 */


LT_EXTERN_C_BEGIN

/**
 * Exit codes for TiltEngine.
 */
enum {
    kTiltEngine_Success            = 0, //<<< All tests passed
    kTiltEngine_FailedTests        = 1, //<<< One or more tests failed
    kTiltEngine_LoadLibraryFailure = 2, //<<< Failed to load test library
    kTiltEngine_BadOptions         = 3, //<<< Invalid command-line option(s)
    kTiltEngine_InternalError      = 4, //<<< Internal error
};

LT_EXTERN_C_END
#endif // LT_INCLUDE_TILT_TILT_ENGINE_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Aug-22   pertinax    created
 */
