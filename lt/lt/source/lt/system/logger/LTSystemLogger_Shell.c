/******************************************************************************
 * LTSystemLogger_Shell.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include "LTSystemLogger_Private.h"

/* ************************************************************************** */
/*                                  Commands                                  */
/* ************************************************************************** */

static int Private_Command_Trace(LTShell hShell, int argc, const char **argv) {
    ILTShell *i = lt_gethandleinterface(ILTShell, hShell);

    if (argc == 2) {
        if (lt_strcmp(argv[1], "list") == 0) {
            LT_GetCore()->TraceKnownStreams();
        } else {
            goto error;
        }
    } else if (argc == 3) {
        bool enabled;
        if (lt_strcmp(argv[1], "enable") == 0) {
            enabled = true;
        } else if (lt_strcmp(argv[1], "disable") == 0) {
            enabled = false;
        } else {
            goto error;
        }
        char *endptr;
        u32   stream = lt_strtou32(argv[2], &endptr, 10);
        if (*endptr != '\0') {
            goto error;
        }
        LT_GetCore()->TraceSetStreamEnabled(stream, enabled);
    } else {
        goto error;
    }

    return 0;

error:
    i->Print(hShell, "Usage: trace <list|enable|disable> <stream #>\n");
    return 1;
}

static LTSystemShell_CommandDesc s_commands[] = {
    {"trace", Private_Command_Trace, "Manipulate the trace system state", NULL},
};

/* ************************************************************************** */
/*                                     API                                    */
/* ************************************************************************** */

static void Private_Shell_Init(void) {
    S.shell = lt_openlibrary(LTSystemShell);
    if (!S.shell) {
        return;
    }
    S.shell->RegisterCommands(s_commands, sizeof(s_commands) / sizeof(s_commands[0]));
}

static void Private_Shell_Fini(void) {
    S.shell->UnregisterCommands(s_commands);
    lt_closelibrary(S.shell);
    S.shell = NULL;
}
