/*******************************************************************************
 * lt/source/example/dhrystone/ExampleDhrystone.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include "libdhry_2.1.h"

/*____________________________
  ExampleDhrystone #defines */
#define DEFAULT_ITERATIONS      1048576      /* 2^20 */
#define MAXIMUM_ITERATIONS      1073741824   /* 2^30 */

/*____________________________________________
  ExampleDhrystone static helper functions */
static void MyDhrystoneConsolePrintFunction(void * pClientData, const char * pFormatString, ...) {
    LT_UNUSED(pClientData);
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LT_GetCore()->ConsolePrintV(pFormatString, args);
    lt_va_end(args);
    //lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(1));
}

/*______________________________________
  ExampleDhrystone Application Main() */
static int
ExampleDhrystone_Main(int argc, const char **argv) {
    LTCore *core = LT_GetCore();
    LTOThread * thread = core->GetCurrentThreadObject();
    u8 priorityOld = thread->API->GetPriority(thread);
    float dMIPS = 0.0;
    s32 nNumIterations = (argc == 3 && (0 != lt_strcmp(argv[2], "--default"))) ? lt_strtos32(argv[2], NULL, 10) : DEFAULT_ITERATIONS;
    if (nNumIterations < 1) return core->ConsolePrint("dhry: number_of_runs must be > 0\n"), 0;

    bool bReenableWatchdog = core->IsLibraryOpen("LTDeviceWatchdog");
    LTDeviceWatchdog *watchdog = lt_openlibrary(LTDeviceWatchdog);
    bReenableWatchdog = (watchdog && bReenableWatchdog) ? watchdog->IsEnabled() : false;

    thread->API->SetPriority(thread, kLTThread_PriorityHighest);

    if (watchdog) watchdog->DisableTimer();
    if (nNumIterations == DEFAULT_ITERATIONS) {
        while ((dMIPS == 0.0) && (nNumIterations <= MAXIMUM_ITERATIONS)) {
            dMIPS = run_dhrystone_benchmark(nNumIterations, &MyDhrystoneConsolePrintFunction, NULL);
            nNumIterations <<= 1;
        }
    }
    else dMIPS = run_dhrystone_benchmark(nNumIterations, &MyDhrystoneConsolePrintFunction, NULL);
    if (bReenableWatchdog) watchdog->EnableTimer();

    thread->API->SetPriority(thread, priorityOld);

    if (watchdog) lt_closelibrary(watchdog);
    return 0;
}

/*__________________________________________
  ExampleDhrystone APPLICATION DEFINITION */
define_LTLIBRARY_APPLICATION(ExampleDhrystone, 1, 1024); /* (appName, version, stackSize 0=default) */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Mar-21   augustus    created
 */
