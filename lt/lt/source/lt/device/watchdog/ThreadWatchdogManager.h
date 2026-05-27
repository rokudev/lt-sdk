/*******************************************************************************
 * lt/source/lt/device/watchdog/ThreadWatchdogManager.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Private header file for ThreadWatchdogManager.c
 *
 ******************************************************************************/

#include <lt/core/LTTime.h>

typedef void (ThreadWatchdogManager_HardBootProc)(void);

bool ThreadWatchdogManager_Initialize(ThreadWatchdogManager_HardBootProc * pHardBootProc);
void ThreadWatchdogManager_Finalize(void);
void ThreadWatchdogManager_WatchThread(LTTime responseFidelity, bool bTerminationAllowed);
void ThreadWatchdogManager_UnwatchThread(void);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  22-Apr-25   augustus    created
 */