/*******************************************************************************
 * lt/source/lt/system/shell/LTSystemShellCommands.c             LT System Shell
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <lt/system/schell/LTSystemSchellCommands.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include "LTSystemSchellCommandsInternal.h"

/* This is a universal set of basic commands, please do not add commands here */

/*________________________
  LTSystemShellCommands */

typedef_LTObjectImpl(LTSystemShellCommands, LTSystemShellCommandsImpl) {
    /* I am a singleton so nothing goes here (for now) */
} LTOBJECT_API;

/* LTRun */

typedef struct {
    LTLibrary   *pLibrary;
    const char **argv;
    int          argc;
    int          retVal;
} LTRunClientData;

static void LTRunTaskProc(void * pClientData) {
    LTRunClientData *pCD = (LTRunClientData *)pClientData;
    pCD->retVal = pCD->pLibrary->Run(pCD->argc, pCD->argv);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    iThread->Terminate(iThread->GetCurrentThread());
}

static int ShellCommand_LTRun(LTSystemSchell *shell, int argc, const char **argv) {
    LTCore    *pCore = LT_GetCore();
    ILTThread *iThread;

    LTRunClientData clientData = { .argv = argv, .argc = argc };

    LTThread hThread = 0;
    u32 stackSize    = 0;
    do {
        if (argc < 2 || 0 == argv[1][0]) {
            shell->API->PutString(shell, "usage: ltrun <library> [args]\n");
            break;
        }
        if (NULL == (clientData.pLibrary = pCore->OpenLibrary(argv[1]))) { shell->API->Print(shell, "ltrun: failed to open library %s\n", argv[1]); break; }
        if (0 == (hThread = pCore->CreateThread(argv[1]))) { shell->API->PutString(shell, "ltrun: failed to create run thread\n"); break; }
        iThread = (ILTThread *)pCore->GetHandleInterface(hThread);
        if (NULL == clientData.pLibrary->Run) { shell->API->Print(shell, "ltrun: %s has no Run() function\n", argv[1]); break; }
        if (0 != (stackSize = clientData.pLibrary->GetRunFunctionStacksizeRequirement())) iThread->SetStackSize(hThread, stackSize);
        iThread->Start(hThread, NULL, NULL);
        iThread->QueueTaskProc(hThread, LTRunTaskProc, NULL, &clientData);
        iThread->WaitUntilFinished(hThread, LTTime_Infinite());
        shell->API->Print(shell, "ltrun: %s exited with code %d\n", argv[1], clientData.retVal);
    } while (false);

    if (hThread) pCore->DestroyHandle(hThread);
    if (clientData.pLibrary) pCore->CloseLibrary(clientData.pLibrary);
    return 0;
}

/* Memstat */

static int ShellCommand_Memstat(LTSystemSchell *shell, int argc, const char **argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    LTCore *pCore = LT_GetCore();

    u64 memstat = pCore->SnapshotMemstat();
    LT_SIZE bigBlock = pCore->GetLargestAvailableBlockInRAM();

    /* NOTE: These units match the current convention in LTCore */
    char *unit  = "k";
    if (bigBlock >= (1024 << 10)) {
        unit = "mb"; bigBlock >>= 10;
        if (bigBlock >= (1024 << 10)) {
            unit = "gb"; bigBlock >>= 10;
            if (bigBlock >= (1024 << 10)) {
                unit = "tb"; bigBlock >>= 10;
            }
        }
    }
    LT_SIZE bigBlockFractional = ((bigBlock % 1024) * 100) >> 10;
    bigBlock >>= 10;

    enum { kMemstatBuff = 42 };
    char *buf = lt_malloc(kMemstatBuff);
    if (buf) {
        pCore->FormatCanonicalMemstatString(memstat, buf, kMemstatBuff, false);
        shell->API->Print(shell, "memstat: %s, largest free block: %lu.%02lu%s\n",
                              buf, LT_PLT_SIZE(bigBlock), LT_PLT_SIZE(bigBlockFractional), unit);
        lt_free(buf);
    } else {
        shell->API->Print(shell, "memstat: out of memory for report\n");
    }
    return 0;
}

/* Version */

static int ShellCommand_Version(LTSystemSchell *shell, int argc, const char **argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    shell->API->Print(shell, "Software version %s\n", LT_GetCore()->GetLibraryBuildVersion());
    return 0;
}

/* Sleepmode */

struct DisallowanceEnumClientData { LTSystemSchell *shell; u32 nCount; u32 nGrant; };
static bool DisallowanceEnumProc(u32 nGrantNumber, void * pCallerAddress, void * pClientData) {
    struct DisallowanceEnumClientData * pCD = (struct DisallowanceEnumClientData *)pClientData;
    if (pCD->nGrant) { /* searching for nGrant */
        if (pCD->nGrant == nGrantNumber) return false; /* found, abort enumeration */
    }
    else { /* printing grants */
        if (1 == ++pCD->nCount) pCD->shell->API->Print(pCD->shell, "__  _____  ______\n #  Grant  Caller\n");
        pCD->shell->API->Print(pCD->shell, "%2lu  %5lu  0x%lx\n", LT_Pu32(pCD->nCount), LT_Pu32(nGrantNumber), LT_PLT_SIZE((LT_SIZE)pCallerAddress));
    }
    return true;
}

static int ShellCommand_SleepMode(LTSystemSchell *shell, int argc, const char **argv) {
    int status = -1;
    struct DisallowanceEnumClientData cd = { .shell = shell, .nCount = 0, .nGrant = 0 };
    if (argc == 2) {
        if (0 == lt_strcmp(argv[1], "disallow")) {
            if (0 == (cd.nGrant = lt_disallowsleepmode())) { shell->API->Print(shell, "%s: no disallowance grants available\n", argv[0]); status = -2; }
            else { shell->API->Print(shell, "%s: issued grant %lu\n", argv[0], LT_Pu32(cd.nGrant)); status = 0; }
        }
        else if (0 == lt_strcmp(argv[1], "disallowances")) {
            LT_GetCore()->EnumerateSleepModeDisallowanceGrants(&DisallowanceEnumProc, &cd);
            if (0 == cd.nCount) shell->API->Print(shell, "%s: no disallowance grants issued\n", argv[0]);
            status = 0;
        }
    }
    else if (argc == 3) {
        if (0 == lt_strcmp(argv[1], "reallow")) {
            cd.nGrant = lt_strtou32(argv[2], NULL, 10);
            if (cd.nGrant && (cd.nGrant <= kLTCore_MaxSleepModeModeDisallowanceGrants)) {
                if (! LT_GetCore()->EnumerateSleepModeDisallowanceGrants(&DisallowanceEnumProc, &cd)) {
                    /* enumeration was aborted, the grant was found! */
                    LT_GetCore()->ReallowSleepMode(cd.nGrant);
                    shell->API->Print(shell, "%s: grant %lu reallowed\n", argv[0], LT_Pu32(cd.nGrant));
                    status = 0;
                }
                else { shell->API->Print(shell, "%s: no such grant %lu\n", argv[0], LT_Pu32(cd.nGrant)); status = -3; }
            }
        }
    }

    if (status == -1) shell->API->Print(shell, "usage: %s disallow|reallow <1..%lu>|disallowances\n", argv[0], LT_Pu32(kLTCore_MaxSleepModeModeDisallowanceGrants));
    return status;
}

/* Watchdog */

static int ShellCommand_Watchdog(LTSystemSchell *shell, int argc, const char **argv) {
    int status = -1;
    if (argc == 2) {
        LTDeviceWatchdog *watchdog = lt_openlibrary(LTDeviceWatchdog);
        if (!watchdog) return -2;
        status = 0;
        if (lt_strcmp(argv[1], "disable") == 0) {
            watchdog->DisableTimer();
        } else if (lt_strcmp(argv[1], "enable") == 0) {
            watchdog->EnableTimer();
        } else if (lt_strcmp(argv[1], "reboot") == 0) {
            watchdog->Reboot();
        } else {
            status = -1;
        }
        lt_closelibrary(watchdog);
    }
    if (status < 0) shell->API->Print(shell, "usage: %s disable|enable|reboot\n", argv[0]);
    return status;
}

/* PS */

typedef struct {
    LTTime    runTime;
    LTHandle  hThread;
} ThreadRunTime;

static LTArray *s_pLastRunTimes = NULL;

static int SortPSArrayCompareFunction(const void *pElement1, const void *pElement2, void *pClientData) {
    LTThread_Snapshot *p1 = (LTThread_Snapshot *)pElement1;
    LTThread_Snapshot *p2 = (LTThread_Snapshot *)pElement2;
    LT_UNUSED(pClientData);
    return (p1->nPriority > p2->nPriority) ? -1 :
           (p1->nPriority < p2->nPriority) ?  1 :
           (p1->nThreadNumber < p2->nThreadNumber) ? -1 : 1;
}

static void ThreadsSnapshotCallback(LTThread_Snapshot *pSnapshots, u32 nCount,  void *pClientData) {
    LTArray *pArray = (LTArray *)pClientData;
    for (u32 i = 0; i < nCount; ++i) {
        pArray->API->Append(pArray, &pSnapshots[i]);
    }
}

static int ShellCommand_PS(LTSystemSchell *shell, int argc, const char **argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    LTCore *pCore = LT_GetCore();
    ILTThread *iThread = (ILTThread *)pCore->GetLibraryInterface((LTLibrary *)pCore, "ILTThread");
    LTArray *pSnapshots = LTArray_CreateStructArray(sizeof(LTThread_Snapshot));
    iThread->SnapshotRunningThreads(ThreadsSnapshotCallback, pSnapshots);
    LTTime snapshotTime = pCore->GetKernelTime();
    static LTTime s_lastSnapshotTime = { .nNanoseconds = 0 };
    LTTime intervalTime = LTTime_Subtract(snapshotTime, s_lastSnapshotTime);
    s_lastSnapshotTime = snapshotTime;
    u32 nThreads = pSnapshots->API->GetCount(pSnapshots);
    LTArray *pRunTimes = LTArray_CreateStructArray(sizeof(ThreadRunTime));
    if (pRunTimes && intervalTime.nNanoseconds) {
        pRunTimes->API->SetCount(pRunTimes, nThreads);
        // Sort threads and display output
        pSnapshots->API->Sort(pSnapshots, SortPSArrayCompareFunction, NULL);
        shell->API->PutString(shell, "                                                  ___________________    ________________\n");
        shell->API->PutString(shell, "  ID  NAME                  PRIO  STATE           STACK   CURR    MAX    HEAP CURR    MAX   %CPU\n");
        for (u32 i = 0; i < nThreads; ++i) {
            LTThread_Snapshot *pSnapshot = pSnapshots->API->Get(pSnapshots, i, NULL);
            // Populate next run-time array
            ThreadRunTime runTime = { .hThread = pSnapshot->hThread, .runTime = pSnapshot->runTime };
            pRunTimes->API->Set(pRunTimes, i, &runTime);
            // Find thread's previous run time, if thread existed
            LTTime lastRunTime = LTTime_Zero();
            if (s_pLastRunTimes) {
                for (u32 j = 0; j < s_pLastRunTimes->API->GetCount(s_pLastRunTimes); ++j) {
                    ThreadRunTime *pRunTime = s_pLastRunTimes->API->Get(s_pLastRunTimes, j, NULL);
                    if (pRunTime->hThread == pSnapshot->hThread) {
                        lastRunTime = pRunTime->runTime;
                        break;
                    }
                }
            }
            float percentCPU = 100.0 * ((float)(pSnapshot->runTime.nNanoseconds - lastRunTime.nNanoseconds)) / (float)intervalTime.nNanoseconds;
            shell->API->Print(shell, "%4lu%2s%-20s  %4lu  %-15s%6lu  %5lu %6lu        %5lu %6lu   %.2f\n",
                LT_Pu32(pSnapshot->nThreadNumber),
                pSnapshot->reserved1 ? "*" : "", pSnapshot->name,
                LT_Pu32(pSnapshot->nPriority),
                iThread->ThreadStateToString(pSnapshot->nThreadState),
                LT_Pu32(pSnapshot->nStackSize),
                LT_Pu32(pSnapshot->nStackCurrent),
                LT_Pu32(pSnapshot->nStackHiWatermark),
                LT_Pu32(pSnapshot->nHeapCurrent),
                LT_Pu32(pSnapshot->nHeapHiWatermark),
                percentCPU
            );
        }
        // Rotate out thread run times
        void *pTemp = s_pLastRunTimes;
        s_pLastRunTimes = pRunTimes;
        lt_destroyobject(pTemp);
    } else {
        lt_destroyobject(s_pLastRunTimes);
        s_pLastRunTimes = NULL;
        shell->API->PutString(shell, "ps: memory error or bad time base\n");
    }
    lt_destroyobject(pSnapshots);
    return 0;
}

static bool MeasureCommandsEnumProc(LTSystemSchell *shell, const LTSystemShell_CommandDesc *desc, void *clientData) {
    LT_UNUSED(shell);
    int *maxLen = (int *)clientData;
    int len = lt_strlen(desc->name) + 1;
    if (len > *maxLen) *maxLen = len;
    return true;
}

static bool ListCommandsEnumProc(LTSystemSchell *shell, const LTSystemShell_CommandDesc *desc, void *clientData) {
    int maxLen = *((int *)clientData);
    shell->API->PutString(shell, " ");
    shell->API->PutString(shell, desc->name);
    if (desc->desc) {
        int spaces = maxLen - lt_strlen(desc->name);
        if (spaces < 1) spaces = 1;
        for (int i = 0; i < spaces; i++) shell->API->PutString(shell, " ");
        shell->API->PutString(shell, "- ");
        shell->API->PutString(shell, desc->desc);
    }
    shell->API->PutString(shell, "\n");
    return true;
}

typedef struct PrintCommandHelpClientData {
    int argc;
    const char **argv;
} PrintCommandHelpClientData;

static bool PrintCommandHelpEnumProc(LTSystemSchell *shell, const LTSystemShell_CommandDesc *desc, void *clientData) {
    PrintCommandHelpClientData *cd = (PrintCommandHelpClientData *)clientData;
    if (0 == lt_strcmp(desc->name, cd->argv[1])) {
        if (desc->helpProc) {
            (*(desc->helpProc))(shell, cd->argc, cd->argv);
        }
        else {
            shell->API->PutString(shell, "help: no help for ");
            shell->API->PutString(shell, cd->argv[1]);
            shell->API->PutString(shell, "\n");
        }
        return false; /* abort enumeration */
    }
    return true;
}

static int ShellCommand_Help(LTSystemSchell *shell, int argc, const char **argv) {
    if (argc == 1) {
        int maxLen = 0;
        shell->API->EnumerateCommands(shell, &MeasureCommandsEnumProc, &maxLen);
        shell->API->PutString(shell, "Valid commands are:\n");
        shell->API->EnumerateCommands(shell, &ListCommandsEnumProc, &maxLen);
    }
    else {
        PrintCommandHelpClientData cd = { .argc = argc, .argv = argv };
        if (shell->API->EnumerateCommands(shell, &PrintCommandHelpEnumProc, &cd)) {
            shell->API->PutString(shell, "help: '");
            shell->API->PutString(shell, argv[1]);
            shell->API->PutString(shell, "': unknown command\n");
        }
    }
    return 0;
}

void LTSystemSchellCommands_Cleanup(void) {
    if (s_pLastRunTimes) {
        lt_destroyobject(s_pLastRunTimes);
        s_pLastRunTimes = NULL;
    }
}

static const LTSystemShell_CommandDesc s_commands[] = {
    { "?",          ShellCommand_Help,      "displays this list",                        NULL  },
    { "help",       ShellCommand_Help,      "provides help on a command",                NULL  },
    { "ltrun",      ShellCommand_LTRun,     "opens LT Library, calls Run() and closes",  NULL  },
    { "memstat",    ShellCommand_Memstat,   "memory usage",                              NULL  },
    { "ps",         ShellCommand_PS,        "reports running threads",                   NULL  },
    { "sleepmode",  ShellCommand_SleepMode, "controls sleepmode",                        NULL  },
    { "version",    ShellCommand_Version,   "software version",                          NULL  },
    { "wdog",       ShellCommand_Watchdog,  "watchdog",                                  NULL  },
    { "ltexit",     ShellCommand_LTExit,    "exit the LT operating system",              NULL  }
};

static void LTSystemShellCommandsImpl_GetCommands(LTSystemShell_CommandTable *table) {
    table->commands    = s_commands;
    table->numCommands = sizeof(s_commands) / sizeof(s_commands[0]);
}

static bool
LTSystemShellCommandsImpl_ConstructObject(LTSystemShellCommandsImpl *commands) {
    LT_UNUSED(commands);
    return true;
}

static void
LTSystemShellCommandsImpl_DestructObject(LTSystemShellCommandsImpl *commands) {
    LT_UNUSED(commands);
}

define_LTObjectImplPublic(LTSystemShellCommands, LTSystemShellCommandsImpl,
    GetCommands
);

