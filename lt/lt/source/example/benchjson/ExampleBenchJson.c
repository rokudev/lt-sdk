/*******************************************************************************
 * example/parsejson/ExampleBenchJson.c   example of benchmarking the json library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/utility/jsonparser/LTUtilityJsonParser.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTProbe.h>
#include "TwitterJson.h"

/*__________________________________________________________________________________________________
  ExampleBenchJson static variables (all so that stack highwatermark measurements are simplified) */
static LTCore               *s_core;
static LTUtilityJsonParser  *s_parser;

static ILTThread            *s_iThread;
static LTThread             s_hThread;

static LTProbe              *s_probe;
static bool                 s_parseSuccessful;


/*_________________________________
  ExampleBenchJson ControlThread */
static bool ControlThreadInit(void) {
    LTProbe_CaptureProbe(s_probe, "ENTER ControlThreadInit");
    LTProbe_CaptureProbe(s_probe, "EXIT  ControlThreadInit");
    return true;
}

static void ControlThreadTaskProc(void *clientData) { LT_UNUSED(clientData);
    LTProbe_CaptureProbe(s_probe, "ENTER ControlThreadTaskProc");
    LTProbe_CaptureProbe(s_probe, "EXIT  ControlThreadTaskProc");
}

static void ControlThreadExit(void) {
    LTProbe_CaptureProbe(s_probe, "ENTER ControlThreadExit");
    LTProbe_CaptureProbe(s_probe, "EXIT  ControlThreadExit");
}

/*___________________________________
  ExampleBenchJson BenchmarkThread */
static bool BenchThreadInit(void) {
    LTProbe_CaptureProbe(s_probe, "ENTER BenchThreadInit");
    s_parser = lt_createobject(LTUtilityJsonParser);
    if (NULL == s_parser) {
        LTProbe_CaptureProbe(s_probe, "EXIT  BenchThreadInit, ERR NO PARSER");
        return false;
    }
    LTProbe_CaptureProbe(s_probe, "  lt_createobject(LTUtilityJsonParser");
    s_parseSuccessful = s_parser->API->ValidateJson(s_parser, s_twitterJson);
    LTProbe_CaptureProbe(s_probe, "EXIT  BenchThreadInit");
    return false; /* LTCore destroys the thread right away if false is returned from ThreadInitProc */
}
static void BenchThreadExit(void) {
    LTProbe_CaptureProbe(s_probe, "ENTER BenchThreadExit");
    if (s_parser) {
        lt_destroyobject(s_parser);
        s_parser = NULL;
    }
    LTProbe_CaptureProbe(s_probe, "EXIT  BenchThreadExitf");
}

/*______________________________________
  ExampleBenchJson Application Main() */
static int                                                                             /* this is to facilitate commenting lines out below without the compiler complaining */
ExampleBenchJson_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv); LT_UNUSED(ControlThreadInit); LT_UNUSED(ControlThreadTaskProc); LT_UNUSED(ControlThreadExit);

    // make the probe and do the first capture right away for clarity of starting point
    s_probe = LTProbe_Create("LTUtiltyJsonParser Benchmark", 15, true);
    LTProbe_CaptureProbe(s_probe, "ENTER ExampleBenchJson_Main()");

    // obtain libraries and interfaces
    s_core        = LT_GetCore();
    s_iThread     = lt_getlibraryinterface(ILTThread, s_core);

    // create and start a control thread to probe and establish the baseline of heap
    // and stack usage in ThreadInit, ThreadExit and TaskProcs
    s_hThread = s_core->CreateThread("BenchControlThread");
    s_iThread->SetPriority(s_hThread, kLTThread_PriorityHighest >> 1);
    s_iThread->Start(s_hThread, &ControlThreadInit, &ControlThreadExit);
    s_iThread->QueueTaskProc(s_hThread, &ControlThreadTaskProc, NULL, NULL);
    s_iThread->Destroy(s_hThread); /* destroy takes care of Terminate() and WaitUntilFinished(); just call Destroy() */

    // create and start the benchmarking thread
    s_hThread = s_core->CreateThread("BenchJsonThread");
    s_iThread->SetPriority(s_hThread, kLTThread_PriorityHighest >> 1);
    s_iThread->Start(s_hThread, &BenchThreadInit, &BenchThreadExit);
    s_iThread->Destroy(s_hThread);  /* look, a Start() immediately followed by Destroy(); no problem here*
                                                       only because of the thread priorities, hmmmm...    */

    // In LT when we clean up, we take care to restore all initialized static variables back to their initially initialized values and to
    // set all uninititalized static varaibles to zero, so that the entire set of statics is consistent with the the library's expectation
    // of its initial state, as specified by the author(s) of the library in source code.
    // Intializing statics when a library is loaded is usally the job a loader or helper library, e.g. libdl on Linux),
    //     libar, e.g. Santa deos ChristmasNormally an operating system  (e.g. libdl on Linux) performs the variable initialization and set.
    // We have an LT runtime dynamic loader on our product backlog; when we implement it we will handle library initialaiton, zeroing out the .bss
    // for thelibrary, etc.
    // reset for possible next go-round
    s_hThread = 0; s_iThread = NULL;

    // take a final probe to make sure we're back where we think we should be as far as stack and heap utilization
    LTProbe_CaptureProbe(s_probe, "EXIT  ExampleBenchJson_Main()");
    LTProbe_ConsolePrintReport(s_probe);
    LTProbe_Destroy(s_probe);

    // print out whether or not the massive parse succeeded or failed.
    s_core->ConsolePrint("%s %d bytes of json text.\n", s_parseSuccessful ? "SUCCESSFULLY PARSED" : "FAILED TO PARSE", (int)(sizeof(s_twitterJson) / sizeof(s_twitterJson[0])));

    // reset for possible next go-round
    s_parseSuccessful = false;
    s_core = NULL;

    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleBenchJson, 1, 0); /* (appName, version, stackSize 0=default) */

 /*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Jan-23   augustus    created
 */
