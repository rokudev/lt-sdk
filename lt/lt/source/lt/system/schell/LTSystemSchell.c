/*******************************************************************************
 * lt/source/lt/system/shell/LTSystemShell.c                     LT System Shell
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <lt/core/LTThread.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/product/config/LTProductConfig.h>

#include "LTSystemSchellCommandsInternal.h"

#define ROKU_LT_BANNER                                              \
"\n      ____          __             __  ______"                   \
"\n     / __ \\ ____   / /__ __  __   / / /_  __/"                  \
"\n    / /_/ // __ \\ / //_// / / /  / /   / /"                     \
"\n   / _, _// /_/ // ,<  / /_/ /  / /___/ /"                       \
"\n  /_/ |_| \\____//_/|_\\ \\__,_/  /_____/_/ operating system"    \
"\n\n\n  THIS IS NOT A SYNTAX ERROR\n\n"

#define LTSYSTEMSHELL_BOOTCMD_KEY     "ltshell.boot.cmd"

static LTCore          *s_core;
static LTSystemSchell  *s_console;
static LTOThread       *s_consoleThread;

enum {
    /* Resource limits */
    kLTSystemShell_MaxCmdLength        = 256,  /**< Max characters per command line */
    kLTSystemShell_MaxCmdHistoryLength = 5,    /**< Max scrollable history length */
    kLTSystemShell_MaxCmdQueueLength   = 10,   /**< Max command lines to queue (including history) */
    kLTSystemShell_MaxCmdArguments     = 32,   /**< Max command line arguments */
    kLTSystemShell_PrintBufferSize     = 512,  /**< Default print buffer size (PrintV) */

    kLTSystemShell_KeyStrokeIdle_ms    = 10000 /**< Idle time before reallowing sleep mode */
};

/*_______________________
  LTSystemShell Object */

/*
 *  TODO: Update to new object system (when available) -- will impact singleton implementations mostly.
 *  TODO: Update to new object system (when available) -- Refactor library name "Scell" (see below).
 */

typedef LTSystemShell_OutputCharsProc  OutputCharsProc;

typedef_LTObjectImpl(LTSystemSchell, LTSystemSchellImpl) {
    LTMutex           *mutex;              /**< General mutex */
    LTOThread         *thread;             /**< Thread for running commands */
    LTString           prompt;             /**< Prompt string to print */
    LTArray           *cmdTables;          /**< Tables of supported command procs */
    LTArray           *cmdQueue;           /**< Queue of commands to run (plus command history) */
    LTString           cmdBuffer;          /**< Working command buffer */
    LTString           printBuffer;        /**< Print buffer */
    const char       **argv;               /**< Argument vector for current command */
    OutputCharsProc   *outputCharsProc;    /**< Proc for outputting chars to underlying interface */
    u32                disallowanceGrant;  /**< Disallowance grant for sleep mode */
    u16                cmdBufLen;          /**< Current length of command buffer */
    u16                printBufSize;       /**< Current size of the print buffer */
    bool               isOutputEnabled;    /**< Is the output currently enabled (false for silence). */
    bool               isInteractive;      /**< Is interactivity currently enabled (false to block input source) */
    u8                 argvLen;            /**< Current number of argument vector entries */
    u8                 swallow;            /**< Number of characters to swallow in interactive shell */
    u8                 cmdIndex;           /**< Index of the next command in command queue */
    u8                 cmdHistIndex;       /**< Current index within command history */
} LTOBJECT_API;

/*__________________________________
  LTSystemShell Command Executive */

static bool LTSystemSchellImpl_EnumerateCommands(LTSystemSchellImpl *shell,
                                                    LTSystemShell_EnumerateCommandsProc proc,
                                                    void *clientData);

static bool FindCommandProc(LTSystemSchell *shell_, const LTSystemShell_CommandDesc *desc, void *clientData) {
    LTSystemSchellImpl *shell = (LTSystemSchellImpl *)shell_;
    const LTSystemShell_CommandDesc **descOut = (const LTSystemShell_CommandDesc **)clientData;
    if (lt_strcmp(shell->argv[0], desc->name) == 0) {
        *descOut = desc;
        return false;
    }
    return true;
}

static const LTSystemShell_CommandDesc *LookupCommand(LTSystemSchellImpl *shell) {
    LTSystemShell_CommandDesc *desc = NULL;
    LTSystemSchellImpl_EnumerateCommands(shell, FindCommandProc, &desc);
    return desc;
}

/* Command-line parsing functionality support:
 *  Determine argument vector and strip whitespace between arguments.
 *  Quoting of literal arguments with ".  */
static int ParseCommand(LTSystemSchellImpl *shell, char *cmd) {
    if (!cmd) return 0;
    int argc = 0;
    while (1) {
        if (argc + 1 > shell->argvLen) {
            const char **argv = shell->argv;
            if (argc + 1 <= kLTSystemShell_MaxCmdArguments) {
                argv = lt_realloc(argv, (argc + 1) * sizeof(char *));
            } else {
                argv = NULL;
            }
            if (argv == NULL) {
                static const char s_args[] = "<argv error>\n";
                if (shell->isOutputEnabled) shell->outputCharsProc(s_args, sizeof(s_args));
                return 0;
            }
            shell->argv    = argv;
            shell->argvLen = argc + 1;
        }
        char findMe = ' ';
        while (*cmd == findMe) cmd++;
        if (*cmd == '\0') break;
        if (*cmd == '"') {
            shell->argv[argc++] = ++cmd;
            findMe = '"';
        } else {
            shell->argv[argc++] = cmd++;
        }
        while (1) {
            if (*cmd == findMe) break;
            if (*cmd == '\0') return argc;
            cmd++;
        }
        *cmd++ = '\0';
    }
    return argc;
}

/* Display the initial prompt */
static void InitialPromptProc(void *clientData) {
    LTSystemSchellImpl *shell = clientData;
    if (shell->isOutputEnabled) shell->outputCharsProc(shell->prompt, lt_strlen(shell->prompt));
}

static int RunCommand(LTSystemSchellImpl *shell, char *cmdString, int *argc) {
    int retVal = -1;
    *argc = ParseCommand(shell, cmdString);
    if (*argc > 0) {
        /* Look up command in table and run it */
        const LTSystemShell_CommandDesc *cmdDesc = LookupCommand(shell);
        if (cmdDesc) {
            retVal = cmdDesc->proc((LTSystemSchell *)shell, *argc, shell->argv);
            if (retVal != 0) {
                shell->API->Print((LTSystemSchell *)shell, "%s: returned %d\n", shell->argv[0], retVal);
            }
        } else {
            shell->API->Print((LTSystemSchell *)shell, "%s: command not found\n", shell->argv[0]);
        }
    }
    return retVal;
}

/* Parse and run boot string command(s) on console (ONLY) */
static void BootStringProc(void *clientData) {
    LTString bootStr = (LTString)clientData;
    char *cmd    = bootStr;
    char *cmdEnd = bootStr;
    while (1) {
        char ch = *cmdEnd;
        if (ch == '\n') *cmdEnd = '\0';
        if (*cmdEnd == '\0') {
            s_console->API->Print(s_console, "Running: %s\n", cmd);
            int argc = -1;
            RunCommand((LTSystemSchellImpl *)s_console, cmd, &argc);
            if (ch == '\0') break;
            cmd = cmdEnd + 1;
        }
        cmdEnd++;
    }
    ltstring_destroy(bootStr);
}

/* Parse and run interactive shell commands */
static void ShellInteractiveCommandProc(void *clientData) {
    LTSystemSchellImpl *shell = clientData;
    LTArray *cmdQueue = shell->cmdQueue;
    u32 cmdCount = cmdQueue->API->GetCount(cmdQueue) - shell->cmdIndex;
    while (cmdCount--) {
        /* Copy command from buffer and parse it */
        LTString cmdString = lt_strdup(cmdQueue->API->Get(cmdQueue, shell->cmdIndex, NULL));
        int argc = 0;
        RunCommand(shell, cmdString, &argc);
        ltstring_destroy(cmdString);
        if (argc > 0) {
            /* Sequence command history */
            if (shell->cmdIndex == kLTSystemShell_MaxCmdHistoryLength) {
                LTArray_RemoveAndFree(cmdQueue, 0);
            } else {
                shell->cmdIndex++;
            }
        } else {
            /* Ignore command */
            LTArray_RemoveAndFree(cmdQueue, shell->cmdIndex);
        }
        shell->outputCharsProc(shell->prompt, lt_strlen(shell->prompt));
    }
    shell->cmdHistIndex = shell->cmdIndex;
}

static u32 ScrollCmdHistory(LTSystemSchellImpl *shell, char ch) {
    enum { kCursorUp = 1, kCursorDown = 2, kCursorLeft = 3 };
    if ((ch == kCursorUp   && shell->cmdHistIndex == 0) ||
        (ch == kCursorDown && shell->cmdHistIndex >= shell->cmdIndex - 1)) return 0;

    s32 len = lt_strlen(shell->cmdBuffer);
    while (len-- > 0) shell->outputCharsProc("\b \b", 3);

    if (ch == kCursorLeft) {
        /* Clear buffer and reset index */
        ltstring_empty(shell->cmdBuffer);
        shell->cmdHistIndex = shell->cmdIndex;
    } else {
        if (ch == kCursorUp) shell->cmdHistIndex--;
        else shell->cmdHistIndex++;  /* CursorDown is only other possibility */

        LTArray *cmdQueue = shell->cmdQueue;
        ltstring_set(&shell->cmdBuffer, cmdQueue->API->Get(cmdQueue, shell->cmdHistIndex, NULL));
    }

    shell->cmdBufLen = lt_strlen(shell->cmdBuffer);
    return shell->cmdBufLen;
}

static void ReallowSleepMode(void *pClientData) {
    LTSystemSchellImpl *shell = (LTSystemSchellImpl *)pClientData;
    lt_reallowsleepmode(shell->disallowanceGrant);
    shell->disallowanceGrant = 0;
    shell->thread->API->KillTimer(shell->thread, &ReallowSleepMode, shell);
}

/*___________________________
  LTSystemShell Public API */

/* Receive input chars and convert into raw commands (with scrollable command history) */
static void LTSystemSchellImpl_PutInputChars(LTSystemSchellImpl *shell, const char *chars, u32 numChars) {
    enum { kCursorUp = 1, kCursorDown = 2, kCursorLeft = 3 };
    if (!shell->isInteractive) return;
   /* Prevent the system from entering sleep mode during input operations. */
    if (shell->disallowanceGrant == 0) {
        shell->disallowanceGrant = lt_disallowsleepmode();
        shell->thread->API->SetTimer(shell->thread, LTTime_Milliseconds(kLTSystemShell_KeyStrokeIdle_ms),
                                        &ReallowSleepMode, NULL, shell);
    } else {
        shell->thread->API->RestartTimer(shell->thread, &ReallowSleepMode, shell);
    }
    const char *out;
    LTArray    *cmdQueue = shell->cmdQueue;
    u16         cmdCount = cmdQueue->API->GetCount(cmdQueue);
    u16         outLen;
    while (numChars--) {
        if (cmdCount >= kLTSystemShell_MaxCmdQueueLength) {
            static const char s_dropped[] = "<Commands Throttled>\n";
            shell->outputCharsProc(s_dropped, sizeof(s_dropped));
            return;
        }
        char ch = *chars++;
        if (shell->swallow) {
            /* Cursor up and down are xterm escape sequences '<esc>[A' and '<esc>[B', respectively. */
            if (--shell->swallow) continue;
            if (ch == 'A') ch = kCursorUp;
            else if (ch == 'B') ch = kCursorDown;
            else if (ch == 'D') ch = kCursorLeft;
            else continue;
        }
        if (shell->cmdBufLen >= kLTSystemShell_MaxCmdLength) {
            static const char s_tooLong[] = "<Line Too Long>\n";
            shell->outputCharsProc(s_tooLong, sizeof(s_tooLong));
            ltstring_empty(shell->cmdBuffer);
            shell->cmdBufLen = 0;
            return;
        }
        outLen = 1;
        switch (ch) {
        case 27:   /* Escape (swallow next two characters) */
            shell->swallow = 2;
            outLen         = 0;
            break;
        case '\b': /* Backspace */
        case 127:  /* Delete */
            outLen = 0;
            if (!ltstring_isempty(shell->cmdBuffer)) {
                ltstring_removechars(&shell->cmdBuffer, (u32)-1, 1);
                shell->cmdBufLen--;
                out    = "\b \b";
                outLen = 3;
            }
            break;
        case '\n': /* New line */
        case '\r': /* Carriage return */
            /* Queue the command and reset the buffer */
            cmdQueue->API->Append(cmdQueue, shell->cmdBuffer);
            shell->cmdBuffer = NULL;
            shell->cmdBufLen = 0;
            ++cmdCount;
            shell->thread->API->QueueTaskProcIfRequired(shell->thread, ShellInteractiveCommandProc, NULL, shell);
            out = "\n";
            break;
        case kCursorUp:   /* History back */
        case kCursorDown: /* History forward */
        case kCursorLeft: /* Clear line and reset history indexing */
            outLen = ScrollCmdHistory(shell, ch);
            out    = shell->cmdBuffer;
            break;
        default: /* Anything else (reject non-printable characters) */
            if (ch > 31) {
                ltstring_appendchar(&shell->cmdBuffer, ch);
                shell->cmdBufLen++;
                out = &ch;
            } else {
                outLen = 0;
            }
            break;
        }
        if (outLen > 0) shell->outputCharsProc(out, outLen);
    }
}

static void LTSystemSchellImpl_PrintV(LTSystemSchellImpl *shell, const char *format, lt_va_list args) {
    if (!shell->isOutputEnabled) return;
    shell->mutex->API->Lock(shell->mutex);
    if (!shell->printBuffer) {
        shell->printBuffer = lt_malloc(shell->printBufSize);
    }
    if (shell->printBuffer) {
        u32 len = lt_vsnprintf(shell->printBuffer, shell->printBufSize, format, args);
        if (len <= shell->printBufSize) shell->outputCharsProc(shell->printBuffer, len);
    }
    shell->mutex->API->Unlock(shell->mutex);
}

static void LTSystemSchellImpl_Print(LTSystemSchellImpl *shell, const char *format, ...) {
    lt_va_list args;
    lt_va_start(args, format);
    LTSystemSchellImpl_PrintV(shell, format, args);
    lt_va_end(args);
}

static void LTSystemSchellImpl_PutString(LTSystemSchellImpl *shell, const char *string) {
    if (!shell->isOutputEnabled) return;
    shell->outputCharsProc(string, lt_strlen(string));
}

static void LTSystemSchellImpl_PutChars(LTSystemSchellImpl *shell, const char *chars, u32 numChars) {
    if (!shell->isOutputEnabled) return;
    shell->outputCharsProc(chars, numChars);
}

static void LTSystemSchellImpl_RegisterCommands(LTSystemSchellImpl *shell, LTSystemShell_CommandTable *table) {
    shell->mutex->API->Lock(shell->mutex);
    if (table) shell->cmdTables->API->Append(shell->cmdTables, table);
    shell->mutex->API->Unlock(shell->mutex);
}

static void LTSystemSchellImpl_UnregisterCommands(LTSystemSchellImpl *shell, LTSystemShell_CommandTable *table) {
    shell->mutex->API->Lock(shell->mutex);
    LTArray *cmdTables = shell->cmdTables;
    for (s32 ix = cmdTables->API->GetCount(cmdTables) - 1; ix >= 0; ix--) {
        LTSystemShell_CommandTable *cmdTable = cmdTables->API->Get(cmdTables, ix, NULL);
        if (cmdTable == table) {
            cmdTables->API->Remove(cmdTables, ix);
            break;
        }
    }
    shell->mutex->API->Unlock(shell->mutex);
}

static bool LTSystemSchellImpl_EnumerateCommands(LTSystemSchellImpl *shell,
                                                    LTSystemShell_EnumerateCommandsProc proc,
                                                    void *clientData) {
    LTArray *cmdTables = shell->cmdTables;
    shell->mutex->API->Lock(shell->mutex);
    for (s32 ix = cmdTables->API->GetCount(cmdTables) - 1; ix >= 0; ix--) {
        LTSystemShell_CommandTable *cmdTable = cmdTables->API->Get(cmdTables, ix, NULL);
        for (u32 iy = 0; iy < cmdTable->numCommands; iy++) {
            if (!proc((LTSystemSchell *)shell, &cmdTable->commands[iy], clientData)) {
                shell->mutex->API->Unlock(shell->mutex);
                return false;
            }
        }
    }
    shell->mutex->API->Unlock(shell->mutex);
    return true;
}

static int LTSystemSchellImpl_ExecuteCommand(LTSystemSchellImpl *shell, const char *command) {
    /* Reserve shell for thread (if not already reserved). */
    shell->mutex->API->Lock(shell->mutex);
    LTOThread *thread = s_core->GetCurrentThreadObject();
    if (!shell->thread) shell->thread = thread;
    else if (shell->thread != thread) thread = NULL;
    shell->mutex->API->Unlock(shell->mutex);
    if (!thread) return -1;
    /* Duplicate string, parse and run */
    LTString cmdString = lt_strdup(command);
    if (!cmdString) return -1;
    if (shell->isOutputEnabled) {
        /* Display incoming command */
        if (shell->cmdBufLen == 0) shell->outputCharsProc(">>> ", 4);
        else shell->outputCharsProc(" >>> ", 5);
        shell->outputCharsProc(cmdString, lt_strlen(cmdString));
        shell->outputCharsProc("\n", 1);
    }
    int argc = 0;
    int retVal = RunCommand(shell, cmdString, &argc);
    ltstring_destroy(cmdString);
    shell->API->Print((LTSystemSchell *)shell, "%s: returned %d\n", shell->argv[0], retVal);
    if (shell->isInteractive) {
        /* Restore prompt and (possibly) incomplete command */
        shell->outputCharsProc(shell->prompt, lt_strlen(shell->prompt));
        shell->outputCharsProc(shell->cmdBuffer, shell->cmdBufLen);
    }
    return retVal;
}

static void LTSystemSchellImpl_SetPrompt(LTSystemSchellImpl *shell, const char *prompt) {
    ltstring_set(&shell->prompt, prompt);
}

static void LTSystemSchellImpl_EnableOutput(LTSystemSchellImpl *shell, bool enable) {
    shell->isOutputEnabled = enable;
    if (!shell->isOutputEnabled) shell->isInteractive = false;
}

static void LTSystemSchellImpl_SetPrintBufferSize(LTSystemSchellImpl *shell, u32 printBufferSize) {
    shell->mutex->API->Lock(shell->mutex);
    if (shell->printBuffer) {
        char *buffer = lt_realloc(shell->printBuffer, printBufferSize);
        if (buffer) {
            shell->printBuffer  = buffer;
            shell->printBufSize = printBufferSize;
        }
    } else {
        shell->printBufSize = printBufferSize;
    }
    shell->mutex->API->Unlock(shell->mutex);
}

static bool LTSystemSchellImpl_GoInteractive(LTSystemSchellImpl *shell) {
    /* Reserve shell for thread (if not already reserved). */
    shell->mutex->API->Lock(shell->mutex);
    LTOThread *thread = s_core->GetCurrentThreadObject();
    if (!shell->thread) shell->thread = thread;
    else if (shell->thread != thread) thread = NULL;
    shell->mutex->API->Unlock(shell->mutex);
    if (thread) {
        shell->isInteractive = true;
        shell->thread->API->QueueTaskProc(shell->thread, InitialPromptProc, NULL, shell);
        return true;
    }
    return false;
}

static void LTSystemSchellImpl_SetOutputCharsProc(LTSystemSchellImpl *shell, OutputCharsProc *outputCharsProc) {
    shell->outputCharsProc = outputCharsProc ? outputCharsProc : s_core->ConsolePutChars;
}

static void LTSystemSchellImpl_DestructObject(LTSystemSchellImpl *shell);

static bool LTSystemSchellImpl_ConstructObject(LTSystemSchellImpl *shell) {
    shell->mutex     = lt_createobject(LTMutex);
    shell->cmdQueue  = lt_createobject_typed(LTArray, List);
    shell->cmdTables = lt_createobject(LTArray);
    shell->prompt    = ltstring_create("LT> ");

    if ( shell->mutex     == NULL || shell->cmdQueue == NULL ||
         shell->cmdTables == NULL || shell->prompt   == NULL) {
        LTSystemSchellImpl_DestructObject(shell);
        return false;
    }

    shell->cmdTables->API->InitAsStructArray(shell->cmdTables, sizeof(LTSystemShell_CommandTable));
    shell->cmdTables->API->TuneAllocation(shell->cmdTables, 2, 2);
    LTSystemSchellImpl_SetOutputCharsProc(shell, NULL);

    shell->printBufSize    = kLTSystemShell_PrintBufferSize;
    shell->isOutputEnabled = true;
    return true;
}

static void DestroyPointerArray(LTArray *ptrArray) {
    u32 count = ptrArray->API->GetCount(ptrArray);
    while (count--) {
        LTArray_RemoveAndFree(ptrArray, 0);
    }
    lt_destroyobject(ptrArray);
}

static void LTSystemSchellImpl_DestructObject(LTSystemSchellImpl *shell) {
    ltstring_destroy(shell->printBuffer);
    shell->printBuffer = NULL;
    ltstring_destroy(shell->cmdBuffer);
    shell->cmdBuffer = NULL;
    ltstring_destroy(shell->prompt);
    shell->prompt = NULL;
    lt_free(shell->argv);
    shell->argv = NULL;

    DestroyPointerArray(shell->cmdQueue);
    lt_destroyobject(shell->cmdTables);

    lt_destroyobject(shell->mutex);
}

define_LTObjectImplPublic(LTSystemSchell, LTSystemSchellImpl,
    Print,
    PrintV,
    PutString,
    PutChars,
    RegisterCommands,
    UnregisterCommands,
    EnumerateCommands,
    ExecuteCommand,
    SetPrompt,
    EnableOutput,
    SetPrintBufferSize,
    GoInteractive,
    SetOutputCharsProc,
    PutInputChars
);

/*______________________________
  LTSystemShellConsole Object */

typedef_LTObjectImpl(LTSystemSchellConsole, LTSystemSchellConsoleImpl) {
    /* I am a singleton, so empty (for now) */
} LTOBJECT_API;

static LTSystemSchell *LTSystemSchellConsoleImpl_GetConsoleShell(void) {
    return s_console;
}

static bool LTSystemSchellConsoleImpl_ConstructObject(LTSystemSchellConsoleImpl *console) {
    /* I am a singleton, so empty (for now), TODO: eventually move Start() implementation here */
    LT_UNUSED(console);
    return true;
}

static void LTSystemSchellConsoleImpl_DestructObject(LTSystemSchellConsoleImpl *console) {
    /* I am a singleton, so empty (for now), TODO: eventually move Fini() implementation here */
    LT_UNUSED(console);
}

define_LTObjectImplPublic(LTSystemSchellConsole, LTSystemSchellConsoleImpl,
    GetConsoleShell
);

/*__________
  Console */

enum {
    kLTSystemShellConsole_StackSize  = 1832
};

static void Console_OnCharactersReceived(char *chars, u32 numChars, void *clientData) {
    LT_UNUSED(clientData);
    s_console->API->PutInputChars(s_console, chars, numChars);
}

static void Console_OnBreakReceived(void *clientData) {
    LT_UNUSED(clientData);
    s_core->ConsoleStompString("ctrl-c\n");
}

static bool Console_ThreadInitProc(void) {
    /* Register default commands (if present) */
    LTSystemShellCommands *commands = lt_createobject(LTSystemShellCommands);
    if (commands) {
        LTSystemShell_CommandTable table;
        commands->API->GetCommands(&table);
        s_console->API->RegisterCommands(s_console, &table);
        lt_destroyobject(commands);
    }

    bool showBanner = true;
    LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
    if (productConfig) {
        // Default is to display the shell banner. Product config can be used
        // to disable it, by setting the respective config values to 0.
        u32 libSection = productConfig->GetLibraryConfigSection("LTShell");
        if (libSection && productConfig->IsIntegerZero(libSection, "showBanner")) showBanner = false;
        lt_closelibrary(productConfig);
    }

    /* Print the banner */
    if (showBanner) s_core->ConsolePutString(ROKU_LT_BANNER);

    /* Use LTCore console function(s) and set callbacks */
    s_console->API->SetOutputCharsProc(s_console, NULL);
    s_core->SetConsoleCharactersReceivedProc(Console_OnCharactersReceived, Console_OnBreakReceived, NULL);

    /* Read the boot string from settings and execute it */
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (settings) {
        LTString bootStr = NULL;
        if (settings->GetStringValue(LTSYSTEMSHELL_BOOTCMD_KEY, &bootStr)) {
            s_consoleThread->API->QueueTaskProc(s_consoleThread, BootStringProc, NULL, bootStr);
        }
        lt_closelibrary(settings);
    }

    /* Enter interactive mode */
    s_console->API->GoInteractive(s_console);
    return true;
}

static void Console_ThreadFiniProc(void) {
    s_core->SetConsoleCharactersReceivedProc(NULL, NULL, NULL);
}

/*_______________________________________
  LTSystemShell Root interface binding */

/* TODO: eventually refactor this "Scell" library name */

static int LTSystemScellImpl_Run(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    /* This run function must exist for LTSystemShell to be a genesis library */
    return 0;
}

static bool LTSystemScellImpl_LibInit(void) {
    s_core = LT_GetCore();
    LTDeviceIdentity *deviceIdentity = lt_openlibrary(LTDeviceIdentity);

    if (deviceIdentity && deviceIdentity->IsUnsecured() != kLTSecurityStatus_Success &&
        deviceIdentity->IsManufacturingFirmware() != kLTSecurityStatus_Success) {
        /* We are running on a unit that has been secured - it has an LTDeviceIdentity library
           and that library has reported that the unit is not unsecured, i.e. it is secured.
           We can only run the shell if we have an LTAT entitlement to do so. */
        if (deviceIdentity->CheckLTATClaims(kLTSecurityClaimGroup2, kLTSecurityClaimGroup2_EnableConsoleInput) != kLTSecurityStatus_Success) {
            /* This is a secured unit that doesn't have the LTAT entitlement for running LTSystemShell; refuse loading */
            lt_closelibrary(deviceIdentity);
            s_core->EnableSerialConsole(false);
            return false;
        }
        LT_GetCore()->EnableSerialConsole(true);
    } else {
        /* This unit is not secured. Either it has no LTDeviceIdentity library which
           means it can't be secured, or it does and the LTDeviceIdentity library has
           reported the unit is unsecured. We allow running LTSystemShell on
           unsecured units. */
        s_core->EnableSerialConsole(true);
    }
    if (deviceIdentity) lt_closelibrary(deviceIdentity);

    s_console = lt_createobject(LTSystemSchell);
    if (!s_console) return false;

    /* Run interactive console shell */
    s_consoleThread = lt_createobject(LTOThread);
    if (s_consoleThread) {
        s_consoleThread->API->SetStackSize(s_consoleThread, kLTSystemShellConsole_StackSize);
        s_consoleThread->API->Start(s_consoleThread, "Console", Console_ThreadInitProc, Console_ThreadFiniProc);
    } else {
        s_core->ConsolePutString("Cannot start console");
    }

    /* Disable the watchdog if the watchdog library isn't already open. This is so the watchdog, which is
     * usually enabled by the bootloader, won't cause a reboot when the product is used in developer mode. */
    LTDeviceWatchdog *wd = LT_GetCore()->IsLibraryOpen("LTDeviceWatchdog") ? NULL : lt_openlibrary(LTDeviceWatchdog);
    if (wd) {
        wd->DisableTimer();
        lt_closelibrary(wd);
    }
    return true;
}

static void LTSystemScellImpl_LibFini(void) {
    if (s_core) {
        LTSystemSchellCommands_Cleanup();
        if (s_consoleThread) {
            s_consoleThread->API->Terminate(s_consoleThread);
            s_consoleThread->API->WaitUntilFinished(s_consoleThread, LTTime_Infinite());
            lt_destroyobject(s_consoleThread);
            s_consoleThread = NULL;
        }
        if (s_console) {
            lt_destroyobject(s_console);
            s_console = NULL;
        }
        s_core = NULL;
    }
}

static bool ExitLT(void) {
    LTSystemScellImpl_LibFini();
    LT_GetCore()->DestroyHandle(lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread());
    LT_GetCore()->TerminateLT(0);
    return false;
}

int ShellCommand_LTExit(LTSystemSchell *shell, int argc, const char **argv) {
    LT_UNUSED(shell); LT_UNUSED(argc); LT_UNUSED(argv);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    iThread->Terminate(iThread->GetCurrentThread());
    LTThread hThread = LT_GetCore()->CreateThread("LTExit");
    if (hThread) {
        iThread->SetPriority(hThread, kLTThread_PriorityHighest);
        iThread->Start(hThread, &ExitLT, NULL);
    }
    return 0;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTSystemScell, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(LTSystemScell, LTSystemScellImpl_Run, 512) LTLIBRARY_DEFINITION;
