/*******************************************************************************
 * lt-firmware-example/source/shellhelloworld/ShellHelloWorld.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * LT Library that registers "hello" shell command
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/system/shell/LTSystemShell.h>

/*_________________________
_/ #forward declarations */
static int  ShellCommandHello(LTShell hShell, int argc, const char ** argv);
static void ShellHelpHello(LTShell hShell, int argc, const char ** argv);

/*____________________
_/ static constants */
static const LTSystemShell_CommandDesc s_helloWorldShellCommands[] = {
    { "hello", ShellCommandHello, "hello world example", ShellHelpHello },
};

/*____________________
_/ static variables */
static LTSystemShell    * s_pLTSystemShell    = NULL;

/*__________________________
_/ library initialization */
static bool ShellHelloWorldImpl_LibInit(void) {
    s_pLTSystemShell = lt_openlibrary(LTSystemShell);
    if (s_pLTSystemShell) {
        s_pLTSystemShell->RegisterCommands(s_helloWorldShellCommands, sizeof s_helloWorldShellCommands / sizeof s_helloWorldShellCommands[0]);
        return true;
    }
    return false;
}

static void ShellHelloWorldImpl_LibFini(void) {
    if (s_pLTSystemShell) {
        s_pLTSystemShell->UnregisterCommands(s_helloWorldShellCommands);
        lt_closelibrary(s_pLTSystemShell);
        s_pLTSystemShell = NULL;
    }
}

/*___________________
_/ hello help proc */
static void ShellHelpHello(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->Print(hShell, "usage: hello\n");
}

/*____________________________
_/ hello command proc */
static int ShellCommandHello(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->Print(hShell, "%s world!\n", argv[0]);
    return 0;
}

/*_______________________________________
_/ LTShellTimeZoneImpl library binding */
typedef_LTLIBRARY_ROOT_INTERFACE(ShellHelloWorld, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(ShellHelloWorld)    LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Mar-26   augustus    created
 */
