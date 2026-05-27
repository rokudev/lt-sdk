/******************************************************************************
 * example/helloworld/ExampleHelloWorld.c             LT HelloWorld application
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTCore.h>

static int ExampleHelloWorld_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    lt_consoleprint("Hello World\n");
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleHelloWorld, 1, 0); /* (appName, version, stackSize 0=default) */
