/******************************************************************************
 * lt/source/system/shell/LTShellCommands.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include "LTShellCommands.h"
#include "LTSystemShellImpl.h"
#include "LTShellImpl.h"
#include "LTNetworkShellImpl.h"
#include "LTConsoleShellImpl.h"
#include "LTConsoleUSB.h"
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/system/logger/LTSystemLogger.h>
#include <lt/system/settings/LTSystemSettings.h>

//DEFINE_LTLOG_SECTION("shellcommands");

/*____________________________
_/ LTShellCommands #defines */
#define HELP_STRING_LINE_BUFFER_SIZE        256
#define HELP_STRING_MAX_COMMAND_LEN         (HELP_STRING_LINE_BUFFER_SIZE - 8)
#define LTSHELLCOMMAND_USAGE_STTY           "usage: stty [{crlf|echo} [on|off] ]\n"
#define LTSHELLCOMMAND_USAGE_HISTORY        "usage: history [on|off]\n"
#define LTSHELLCOMMAND_USAGE_MEMSTAT        "usage: memstat [settag <tag> | heapinfo [tag] ]\n"
#define LTSHELLCOMMAND_USAGE_PS             "usage: ps [<id | name> [prio [0..30] | terminate | heapinfo | wdog-set <fidelityMS> [no-term] | wdog-clear] ]\n"
#define LTSHELLCOMMAND_USAGE_SLEEP          "usage: sleep <seconds>\n"
#define LTSHELLCOMMAND_USAGE_WATCHDOG       "usage: watchdog [enable|disable|reset|crash|settimeout <seconds>]\n"
#define LTSHELLCOMMAND_USAGE_LTHANDLE       "usage: lthandle <count | list> [type]\n"
#define LTSHELLCOMMAND_USAGE_WAITFOR        "usage: waitfor <network-down|network-up|wifi-connected|wifi-disconnected> <seconds>\n"
#define LTSHELLCOMMAND_USAGE_SLEEPMODE      "usage: sleepmode <setdelay <milliseconds> | cleardelay | setproc | clearproc | abort <on|off> | disallow | reallow <grantNumber> | disallowances>\n"

/*_________________________________________________________
_/ LTShellCommands command function forward declarations */
static int ShellCommand_Help(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Clear(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Stty(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Version(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Echo(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_History(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Memstat(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Rawstat(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_PS(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTList(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTOpen(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTClose(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTRun(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTExit(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Reboot(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Sleep(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Uptime(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Watchdog(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Exit(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_LTHandle(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_TelnetD(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Get(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Set(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_Log(LTShell hShell, int argc, const char ** argv);
#ifdef LT_DEBUG
    static int ShellCommand_ReadReg(LTShell hShell, int argc, const char ** argv);
    static int ShellCommand_WriteReg(LTShell hShell, int argc, const char ** argv);
#endif
static int ShellCommand_ConsolePrint(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_WaitFor(LTShell hShell, int argc, const char ** argv);
static int ShellCommand_SleepMode(LTShell hShell, int argc, const char ** argv);

/*______________________________________________________
_/ LTShellCommands help function forward declarations */
static void ShellHelp_Stty(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_History(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_Memstat(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_PS(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_Watchdog(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_Log(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_LTHandle(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_WaitFor(LTShell hShell, int argc, const char ** argv);
static void ShellHelp_SleepMode(LTShell hShell, int argc, const char ** argv);

/*___________________________________________________
_/ LTShellCommands table of inbuilt shell commands */
static const LTSystemShell_CommandDesc s_shellInbuiltCommandDescs[] = {
    { "?",              ShellCommand_Help,              "displays this list",                                       NULL               },
    { "help",           ShellCommand_Help,              "provides help on a command",                               NULL               },
    { "clear",          ShellCommand_Clear,             "clears the terminal window",                               NULL               },
    { "stty",           ShellCommand_Stty,              "sets shell terminal parameters",                           ShellHelp_Stty     },
    { "consoleprint",   ShellCommand_ConsolePrint,      "sets consoleprint on or off",                              NULL               },
    { "version",        ShellCommand_Version,           "displays library build version(s) (try -v)",               NULL               },
    { "echo",           ShellCommand_Echo,              "echos text",                                               NULL               },
    { "history",        ShellCommand_History,           "displays, enables or disables command history",            ShellHelp_History  },
    { "memstat",        ShellCommand_Memstat,           "displays memory statistics",                               ShellHelp_Memstat  },
    { "rawstat",        ShellCommand_Rawstat,           "displays raw unformatted memory statistics",               NULL               },
    { "ps",             ShellCommand_PS,                "reports and controls running threads",                     ShellHelp_PS       },
    { "ltlist",         ShellCommand_LTList,            "lists open and available LT Libraries",                    NULL               },
    { "ltopen",         ShellCommand_LTOpen,            "opens LT Libraries",                                       NULL               },
    { "ltclose",        ShellCommand_LTClose,           "close LT Libraries",                                       NULL               },
    { "ltrun",          ShellCommand_LTRun,             "opens LT Library, calls Run(), and closes",                NULL               },
    { "reboot",         ShellCommand_Reboot,            "restarts the device",                                      NULL               },
    { "sleep",          ShellCommand_Sleep,             "sleep for the given number of seconds",                    NULL               },
    { "uptime",         ShellCommand_Uptime,            "reports the system's uptime",                              NULL               },
    { "watchdog",       ShellCommand_Watchdog,          "controls for the watchdog",                                ShellHelp_Watchdog },
    { "lthandle",       ShellCommand_LTHandle,          "list or count LTHandles by type",                          ShellHelp_LTHandle },
    { "telnetd",        ShellCommand_TelnetD,           "starts Shell Telnet Daemon",                               NULL               },
    { "get",            ShellCommand_Get,               "obtain value of a setting",                                NULL               },
    { "set",            ShellCommand_Set,               "change value of a setting",                                NULL               },
    { "log",            ShellCommand_Log,               "manipulate logging subsystem",                             ShellHelp_Log      },
    { "exit",           ShellCommand_Exit,              "exits the shell",                                          NULL               },
    { "ltexit",         ShellCommand_LTExit,            "exits the LT Operating System",                            NULL               },
#ifdef LT_DEBUG
    { "readreg",        ShellCommand_ReadReg,           "Reads from a register [hex-address] [count (optional)]",   NULL               },
    { "writereg",       ShellCommand_WriteReg,          "Writes to a register [hex-address] [hex-value]",           NULL               },
#endif
    { "waitfor",        ShellCommand_WaitFor,           "Wait for network up or similar events",                    ShellHelp_WaitFor  },
    { "sleepmode",      ShellCommand_SleepMode,          "reports/controls sleep mode parameters",                  ShellHelp_SleepMode  }
};

/*_________________________________
_/ LTShellCommands private types */
typedef struct {
    ILTShell  * iShell;
    LTString    pString;
    LTShell     hShell;
    int         nMaxLength;
} EnumCommandsClientData;

typedef struct {
    ILTShell  * iShell;
} EnumHistoryClientData;

typedef struct {
    LTLibrary * pLibrary;
    const char ** argv;
    int argc;
    int nRetVal;
} LTRunClientData;

typedef struct {
    LTArray   * pOpenedLibs;
    LTArray   * pInstalledLibs;
} LTLibEnumLibsClientData;

struct ShellOpenedLibrary {
    LTLibrary * pLibrary;
    struct ShellOpenedLibrary * pNext;
};

typedef struct {
    LTTime    runTime;
    LTHandle  hThread;
} ThreadRunTime;

/*____________________________________________
_/ LTShellCommands private static variables */
static LTDeviceWatchdog             * s_pWatchdog               = NULL;

static LTMutex                      * s_mutex                   = NULL;
static struct ShellOpenedLibrary    * s_pShellOpenedLibraries   = NULL;
static LTArray                      * s_pLastRunTimes           = NULL;

/*___________________________________________________________________
_/ LTShellCommands initialization */
void LTShellCommands_LibInit(void) {
    s_mutex = lt_createobject(LTMutex);
}

static void LTShellCommands_CloseAllShellOpenedLibraries(LTHandle hShell) {
    struct ShellOpenedLibrary * pTemp;
    LTCore * pCore = LT_GetCore();
    s_mutex->API->Lock(s_mutex);
    while (s_pShellOpenedLibraries) {
        pTemp = s_pShellOpenedLibraries->pNext;
        char *pLibName = hShell ? lt_strdup(s_pShellOpenedLibraries->pLibrary->GetLibraryExtrinsicName()) : NULL;
        pCore->CloseLibrary(s_pShellOpenedLibraries->pLibrary);
        if (pLibName) {
            ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
            iShell->Print(hShell, "ltexit: %s closed\n", pLibName);
            lt_free(pLibName);
        }
        lt_free(s_pShellOpenedLibraries);
        s_pShellOpenedLibraries = pTemp;
    }
    s_mutex->API->Unlock(s_mutex);
}

void LTShellCommands_LibFini(void) {
    if (s_pLastRunTimes) {
        lt_destroyobject(s_pLastRunTimes);
        s_pLastRunTimes = NULL;
    }
    LTShellCommands_CloseAllShellOpenedLibraries(0);
    lt_destroyobject(s_mutex);
    s_mutex = NULL;
    lt_closelibrary(s_pWatchdog);
    s_pWatchdog = NULL;
}

/*___________________________________________________________________
_/ LTShellCommands command table access functions (library private) */
const LTSystemShell_CommandDesc * LTShellCommands_GetInbuiltShellCommands(void) { return s_shellInbuiltCommandDescs; }
u32 LTShellCommands_GetNumInbuiltShellCommands(void) { return sizeof(s_shellInbuiltCommandDescs) / sizeof(s_shellInbuiltCommandDescs[0]); }

/*____________________________________________________
_/ LTShellCommands private static utility functions */
static bool
LTShellCommands_MeasureCommandNamesEnumProc(const LTSystemShell_CommandDesc * pCommandDesc, void * pClientData) {
    EnumCommandsClientData * pCD = (EnumCommandsClientData *)pClientData;
    int nLen = (pCommandDesc->pCommand && pCommandDesc->pDescription != kLTSystemShell_HiddenCommand) ? lt_strlen(pCommandDesc->pCommand) : 0;
    if (nLen > pCD->nMaxLength) pCD->nMaxLength = nLen;
    return true;
}

static bool
LTShellCommands_PrintCommandNamesAndDescriptionEnumProc(const LTSystemShell_CommandDesc * pCommandDesc, void * pClientData) {
    EnumCommandsClientData * pCD = (EnumCommandsClientData *)pClientData;
    int nLen;
    if (pCommandDesc->pCommand && *pCommandDesc->pCommand && pCommandDesc->pDescription != kLTSystemShell_HiddenCommand) {
        nLen = lt_strlen(pCommandDesc->pCommand);
        if (nLen > HELP_STRING_MAX_COMMAND_LEN) nLen = HELP_STRING_MAX_COMMAND_LEN;
        ltstring_empty(pCD->pString);
        ltstring_appendchar(&pCD->pString, ' ');
        ltstring_appendbytes(&pCD->pString, pCommandDesc->pCommand, nLen);
        if (pCommandDesc->pDescription) {
            nLen = pCD->nMaxLength - nLen;
            while (nLen--) ltstring_appendchar(&pCD->pString, ' ');
            ltstring_append(&pCD->pString, " - ");
            nLen = HELP_STRING_LINE_BUFFER_SIZE - (int)lt_strlen(pCD->pString) - 4;
            ltstring_appendbytes(&pCD->pString, pCommandDesc->pDescription, nLen);
        }
        ltstring_appendchar(&pCD->pString, '\n');
        pCD->iShell->PutString(pCD->hShell, pCD->pString);
    }
    return true;
}

static bool
LTShellCommands_PrintHistoryEnumProc(LTShell hShell, u32 nHistoryItemNumber, const LTString pHistoryItem, void * pClientData) {
    EnumHistoryClientData * pCD = (EnumHistoryClientData *)pClientData;
    pCD->iShell->Print(hShell, " %5lu  %s\n", LT_Pu32(nHistoryItemNumber), pHistoryItem);
    return true;
}

static void
ThreadsSnapshotByIDCallback(LTThread_Snapshot * pSnapshots, u32 nCount, void * pClientData) {
    LTThread_Snapshot * pSnapshotOut = (LTThread_Snapshot *)pClientData;
    for (u32 i = 0; i < nCount; ++i) {
        if (pSnapshots[i].nThreadNumber == pSnapshotOut->nThreadNumber) {
            *pSnapshotOut = pSnapshots[i];
            break;
        }
    }
}

static LTThread
LTShellCommands_FindThreadFromID(u32 nThreadID) {
    /* This is a lame way to find handles from thread ids, but thread ids while unique are not
       the way to refer to threads, handles are, so the id is just for the purpose of convenient
       shell-command-ing, so it is intentionally obscured for the time being */
    LTThread hThread = 0;
    if (nThreadID) {
        LTThread_Snapshot snapshot;
        lt_memset(&snapshot, 0, sizeof(snapshot));
        snapshot.nThreadNumber = nThreadID;
        ILTThread * iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        iThread->SnapshotRunningThreads(ThreadsSnapshotByIDCallback, &snapshot);
        hThread = snapshot.hThread;
    }
    return hThread;
}

static void
ThreadsSnapshotCallback(LTThread_Snapshot * pSnapshots, u32 nCount,  void * pClientData) {
    LTArray * pArray = (LTArray *)pClientData;
    for (u32 i = 0; i < nCount; ++i) {
        pArray->API->Append(pArray, &pSnapshots[i]);
    }
}

static void
LTShellCommands_FindThreadsFromName(const char * pName, LTArray * pSnapshotsOut) {
    /* returns a list if threads with the given name in pSnapshots */
    if (pName != NULL) {
        ILTThread * iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        // get all thread snapshots
        iThread->SnapshotRunningThreads(ThreadsSnapshotCallback, pSnapshotsOut);
        for (u32 i = 0; i < pSnapshotsOut->API->GetCount(pSnapshotsOut); ++i) {
            LTThread_Snapshot * pSnapshot = pSnapshotsOut->API->Get(pSnapshotsOut, i, NULL);
            if (lt_strcmp(pName, pSnapshot->name) != 0) {
                // name does not match, so remove it from the array
                pSnapshotsOut->API->Remove(pSnapshotsOut, i);
                --i;
            }
        }
    }
}

static int
LTShellCommands_PrintThreadSnapshot(LTShell hShell, LTThread hThread) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    ILTThread * iThread = (ILTThread *)pCore->GetHandleInterface(hThread);
    LTThread_Snapshot snapshot;
    snapshot.nStructureSize = sizeof(snapshot);
    iThread->GetSnapshot(hThread, &snapshot);
    iShell->Print(hShell, "             ID:  %lu\n",  LT_Pu32(snapshot.nThreadNumber));
    iShell->Print(hShell, "           Name:%2s%s\n",  snapshot.reserved1 ? "*" : "", snapshot.name);
    iShell->Print(hShell, "       Priority:  %lu\n",  LT_Pu32(snapshot.nPriority));
    iShell->Print(hShell, "          State:  %s\n",   iThread->ThreadStateToString(snapshot.nThreadState));
    iShell->Print(hShell, "     Stack Size:  %lu\n",  LT_Pu32(snapshot.nStackSize));
    iShell->Print(hShell, " Max Stack Used:  %lu\n",  LT_Pu32(snapshot.nStackHiWatermark));
    iShell->Print(hShell, "      Heap Used:  %lu\n",  LT_Pu32(snapshot.nHeapCurrent));
    iShell->Print(hShell, "  Max Heap Used:  %lu\n",  LT_Pu32(snapshot.nHeapHiWatermark));
    return 0;
}

static int
LTShellCommands_SortPSArrayCompareFunction(const void * pElement1, const void * pElement2, void * pClientData) {
    LTThread_Snapshot * p1 = (LTThread_Snapshot *)pElement1;
    LTThread_Snapshot * p2 = (LTThread_Snapshot *)pElement2;
    LT_UNUSED(pClientData);
    return (p1->nPriority > p2->nPriority) ? -1 :
           (p1->nPriority < p2->nPriority) ?  1 :
           (p1->nThreadNumber < p2->nThreadNumber) ? -1 : 1;
}

static int
LTShellCommands_SortOpenLibrariesArrayCompareFunction(const void * pElement1, const void * pElement2, void * pClientData) {
    LT_UNUSED(pClientData);
    return lt_strcmp(((LTCore_LibrarySnapshot *)pElement1)->name, ((LTCore_LibrarySnapshot *)pElement2)->name);
}

static int
LTShellCommands_SortInstalledLibrariesArrayCompareFunction(const void * pElement1, const void * pElement2, void * pClientData) {
    LT_UNUSED(pClientData);
    return lt_strcmp((const char *)pElement1, (const char *)pElement2);
}

static bool
LTShellCommands_RecordInstalledLibsEnumProc(const char * pLibName, void * pClientData) {
    LTLibEnumLibsClientData * pCD = (LTLibEnumLibsClientData *)pClientData;
    u32 nCount = pCD->pOpenedLibs->API->GetCount(pCD->pOpenedLibs);
    bool bFound = false;
    for (u32 i = 0; i < nCount && !bFound; ++i) {
        bFound = (lt_strcasecmp(pCD->pOpenedLibs->API->Get(pCD->pOpenedLibs, i, NULL), pLibName) == 0);
    }
    if (!bFound) {
        // installed lib is not opened; therefore we haven't printed it yet!!
        pCD->pInstalledLibs->API->Append(pCD->pInstalledLibs, pLibName);
    }

    return true; /* continue enumerating */
}

static void
ShellCommand_EnumOpenLibrariesProc(LTCore_LibrarySnapshot * pSnapshot, void * pClientData) {
    LTLibEnumLibsClientData * pCD = (LTLibEnumLibsClientData *)pClientData;
    pCD->pOpenedLibs->API->Append(pCD->pOpenedLibs, pSnapshot);
}

static bool
LTShellCommands_IsShellOpenedLibInternal(const char * pLibName) {
    struct ShellOpenedLibrary * pCurr = s_pShellOpenedLibraries;
    while (pCurr) {
        if (0 == lt_strcmp(pLibName, pCurr->pLibrary->GetLibraryExtrinsicName())) return true;
        pCurr = pCurr->pNext;
    }
    return false;
}

/*_____________________________________________________
_/ LTShellCommands private static TaskProc for LTRun */
static void LTRunTaskProc(void * pClientData) {
    LTRunClientData * pCD = (LTRunClientData *)pClientData;
    pCD->nRetVal = pCD->pLibrary->Run(pCD->argc, pCD->argv);
    ILTThread * iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    iThread->Terminate(iThread->GetCurrentThread());
}

/*_________________________________________
_/ LTShellCommands COMMAND FUNCTIONS!!!! */
static int
ShellCommand_Help(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    if (argc == 1) {
        EnumCommandsClientData clientData = { .iShell = iShell, .pString = NULL, .hShell = hShell, .nMaxLength = 0 };
        iShell->PutString(hShell, "Valid commands are:\n");
        LTSystemShellImpl_EnumerateCommands(&LTShellCommands_MeasureCommandNamesEnumProc, &clientData);
        if (clientData.nMaxLength > HELP_STRING_MAX_COMMAND_LEN) clientData.nMaxLength = HELP_STRING_MAX_COMMAND_LEN;
        LTSystemShellImpl_EnumerateCommands(&LTShellCommands_PrintCommandNamesAndDescriptionEnumProc, &clientData);
        ltstring_destroy(clientData.pString);
    }
    else {
        LTSystemShell_CommandDesc commandDesc;
        if (LTSystemShellImpl_LookupCommand(argv[1], &commandDesc)) {
            if (commandDesc.pHelpProc) {
                (void)commandDesc.pHelpProc(hShell, argc-1, &argv[1]);
            }
            else  {
                iShell->PutString(hShell, "help: No help for ");
                iShell->PutString(hShell, argv[1]);
                iShell->PutString(hShell, "\n");
            }
        }
        else {
            iShell->PutString(hShell, "help: '");
            iShell->PutString(hShell, argv[1]);
            iShell->PutString(hShell, "': unknown command\n");
        }
    }
    return 0;
}

static int
ShellCommand_Clear(LTShell hShell, int argc, const char ** argv) {
    /* Note: ShellCommand_Exit temporarily calls this function directly with NULL argv
             so don't access argv until ShellCommand_Exit is fixed */
    LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc != 1) iShell->PutString(hShell, "usage: clear\n");
    else iShell->Print(hShell, "%c[H%c[2J", 27, 27); // vt100 clear sequence
    return 0;
}

static int ShellCommand_Stty(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);

    // validate argc is 1, 2, or 3 and for 2 or 3 argv[1] param is valid
    if (argc < 1 || argc > 3) goto usage;
    bool bEcho = (argc > 1) ? (0 == lt_strcmp(argv[1], "echo")) : true;
    bool bCRLF = (argc > 1) ? (0 == lt_strcmp(argv[1], "crlf")) : true;
    if ((! bEcho) && (! bCRLF)) goto usage;
    // for argc 3 validate argv[2] and set echo or crlf accordingly
    if (argc == 3) {
        bool  bON = (0 == lt_strcmp(argv[2], "on"));
        bool bOFF = (0 == lt_strcmp(argv[2], "off"));
        if ((! bON) && (! bOFF)) goto usage;
        if (bEcho) LTShellImpl_SetEchoOn(hShell, bON);
        else LTShellImpl_SetCRLFOn(hShell, bON);
    }

    // output the result:
    //  for argc == 1:  stty: echo is on; crlf is off
    //  for argc == 2:  stty: echo is on
    //  for argc == 3:  stty: echo is now on
    iShell->PutString(hShell, "stty: ");
    if (bEcho) {
        iShell->PutString(hShell, "echo is ");
        if (argc == 3) iShell->PutString(hShell, "now ");
        iShell->PutString(hShell, LTShellImpl_IsEchoOn(hShell) ? "on" : "off");
    }
    if (argc == 1) iShell->PutString(hShell, "; ");
    if (bCRLF) {
        iShell->PutString(hShell, "crlf is ");
        if (argc == 3) iShell->PutString(hShell, "now ");
        iShell->PutString(hShell, LTShellImpl_IsCRLFOn(hShell) ? "on" : "off");
    }
    iShell->PutString(hShell, "\n");

    return 0;

usage:
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_STTY);
    return 0;
}

static int ShellCommand_Version(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    LTLibEnumLibsClientData clientData;
    enum { kNormal, kBrief, kVerbose, kUsage } outputMode = kUsage;
    switch (argc) {
    case 1:
        outputMode = kNormal;
        break;
    case 2:
        if (argv[1][0] == '-' && argv[1][2] == '\0') switch (argv[1][1]) {
            case 'v': outputMode = kVerbose; break;
            case 'b': outputMode = kBrief;   break;
            default:                         break;
        }
        break;
    default:
        break;
    }
    switch (outputMode) {
    case kNormal:
        iShell->Print(hShell, "version: software version %s\n", pCore->GetLibraryBuildVersion());
        break;
    case kBrief:
        iShell->Print(hShell, "%s\n", pCore->GetSoftwareVersion());
        break;
    case kVerbose:
        // snapshot the open libraries, and print out the build version string of each
        clientData.pOpenedLibs = LTArray_CreateStructArray(sizeof(LTCore_LibrarySnapshot));
        pCore->SnapshotOpenLibraries(&ShellCommand_EnumOpenLibrariesProc, &clientData);
        bool bGotVersionString = false;
        // convert to array for sorting
        const u32 nCount = clientData.pOpenedLibs->API->GetCount(clientData.pOpenedLibs);
        if (nCount > 0) {
            enum { kVersionStringBuffLen = 128 };
            char * pVersionString = lt_malloc(kVersionStringBuffLen);
            if (pVersionString) {
                clientData.pOpenedLibs->API->Sort(clientData.pOpenedLibs, LTShellCommands_SortOpenLibrariesArrayCompareFunction, NULL);
                iShell->PutString(hShell, "\nLIB NAME                        BUILD VERSION\n");
                for (u32 i = 0; i < nCount; i++) {
                    LTCore_LibrarySnapshot * pSnapshot = clientData.pOpenedLibs->API->Get(clientData.pOpenedLibs, i, NULL);
                    bGotVersionString = pCore->GetLibraryBuildVersionString(pSnapshot->name, pVersionString, kVersionStringBuffLen);
                    iShell->Print(hShell, " %-30s  %s\n", pSnapshot->name, bGotVersionString ? pVersionString : "???");
                }
                lt_free(pVersionString);
            }
        }
        lt_destroyobject(clientData.pOpenedLibs);
        break;
    default:
        iShell->PutString(hShell, "usage: version [-v|-b]\n");
        break;
    }
    return 0;
}

static int ShellCommand_Echo(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    for (int i = 1; i < argc; i++) {
        iShell->PutString(hShell, argv[i]);
        if ((i+1) < argc) iShell->PutChar(hShell, ' ');
    }
    iShell->PutChar(hShell, '\n');
    return 0;
}

static int ShellCommand_History(LTShell hShell, int argc, const char ** argv) {
    LTCore   * pCore  = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);

    // validate argc is 1 or 2 and for 2 argv[1] is valid
    if (argc < 1 || argc > 2) goto usage;

    // if argc is 2 , validate argv[1] and set history enablement
    bool bON;
    if (argc == 2) {
             bON  = (argc > 1) ? (0 == lt_strcmp(argv[1], "on"))  : true;
        bool bOFF = (argc > 1) ? (0 == lt_strcmp(argv[1], "off")) : true;
        if ((! bON) && (! bOFF)) goto usage;
        LTShellImpl_SetHistoryOn(hShell, bON);
    }
    bON = LTShellImpl_IsHistoryOn(hShell);

    // report history / status
    if (argc == 1) {
        if (bON) {
            EnumHistoryClientData clientData = { .iShell = iShell };
            (void)LTShellImpl_EnumerateHistory(hShell, &LTShellCommands_PrintHistoryEnumProc, &clientData);
        }
        else {
            iShell->PutString(hShell, "history: history is off; use 'history on' to enable\n");
        }
    }
    else {
        iShell->Print(hShell, "history: history is now %s\n", bON ? "on" : "off");
    }

    return 0;

usage:
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_HISTORY);
    return 0;
}

static int
ShellCommand_Rawstat(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argv);
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc != 1) {
        iShell->PutString(hShell, "usage: rawstat\n");
        return 0;
    }

    iShell->Print(hShell, "rawstat:             GetTotalSystemRAM = %lu\n", LT_PLT_SIZE(pCore->GetTotalSystemRAM()));
    iShell->Print(hShell, "rawstat:         GetAvailableSystemRAM = %lu\n", LT_PLT_SIZE(pCore->GetAvailableSystemRAM()));
    iShell->Print(hShell, "rawstat:      GetSystemRAMLowWatermark = %lu\n", LT_PLT_SIZE(pCore->GetSystemRAMLowWatermark()));
    iShell->Print(hShell, "rawstat: GetLargestAvailableBlockInRAM = %lu\n", LT_PLT_SIZE(pCore->GetLargestAvailableBlockInRAM()));
    iShell->Print(hShell, "rawstat:  GetCurrentRAMAllocationCount = %lu\n", LT_PLT_SIZE(pCore->GetCurrentRAMAllocationCount()));
    iShell->Print(hShell, "rawstat:      GetMaxRAMAllocationCount = %lu\n", LT_PLT_SIZE(pCore->GetMaxRAMAllocationCount()));
    iShell->Print(hShell, "rawstat:               SnapshotMemstat = 0x%llx\n", LT_Pu64(pCore->SnapshotMemstat()));
    return 0;
}

typedef struct MemstatBlockInfoClientData {
    ILTThread * iThread;
    ILTShell  * iShell;
    LTShell     hShell;
    u32         count;
} MemstatBlockInfoClientData;

static bool MemstatBlockInfoEnumCB(const LTCore_HeapAllocatedBlockInfo * pBlockInfoStructs, u32 nNumBlockInfoStructs, void * pClientData) {
    MemstatBlockInfoClientData * pCD = (MemstatBlockInfoClientData *)pClientData;
    LTCore * pCore = LT_GetCore();
    if (0 == pCD->count && nNumBlockInfoStructs) {
        #if DISABLE_LT_CALLSITE
            pCD->iShell->Print(pCD->hShell, "  _______          ____    ______\n");
            pCD->iShell->Print(pCD->hShell, "  ADDRESS          SIZE    THREAD\n");
        #else
            pCD->iShell->Print(pCD->hShell, "  _______          ____    ______               ______\n");
            pCD->iShell->Print(pCD->hShell, "  ADDRESS          SIZE    THREAD               CALLER ----> FILE:LINE WHERE ALLOCATED\n");
        #endif
    }
    pCD->count += nNumBlockInfoStructs;
    while (nNumBlockInfoStructs--) {
        char threadName[kLTThread_MaxNameBuff];
        if (pBlockInfoStructs->hThread && pCore->IsHandleValid(pBlockInfoStructs->hThread)) {
            pCD->iThread->GetName(pBlockInfoStructs->hThread, threadName);
        }
        else {
            lt_snprintf(threadName, sizeof(threadName), "%lu", LT_PLT_HANDLE(pBlockInfoStructs->hThread));
        }
        if (pBlockInfoStructs->callsite.returnAddress) {
            pCD->iShell->Print(pCD->hShell, "  0x%08lX   %8lu    %-20s 0x%08lX   %s:%lu\n",
                LT_PLT_SIZE(pBlockInfoStructs->pAddr), LT_Pu32(pBlockInfoStructs->nBytes), threadName,
                LT_PLT_SIZE(pBlockInfoStructs->callsite.returnAddress), pBlockInfoStructs->callsite.file, LT_PLT_SIZE(pBlockInfoStructs->callsite.line));
        }
        else if (pBlockInfoStructs->callsite.file) {
            pCD->iShell->Print(pCD->hShell, "  0x%08lX   %8lu    %-20s              %s:%lu\n",
                LT_PLT_SIZE(pBlockInfoStructs->pAddr), LT_Pu32(pBlockInfoStructs->nBytes), threadName, pBlockInfoStructs->callsite.file, LT_PLT_SIZE(pBlockInfoStructs->callsite.line));
        }
        else {
            pCD->iShell->Print(pCD->hShell, "  0x%08lX   %8lu    %-20s\n",
                LT_PLT_SIZE(pBlockInfoStructs->pAddr), LT_Pu32(pBlockInfoStructs->nBytes), threadName);
        }
        pBlockInfoStructs++;
    }
    return true;
}


static int
ShellCommand_Memstat(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    ILTThread * iThread = (ILTThread *)pCore->GetLibraryInterface((LTLibrary *)pCore, "ILTThread");

    if (argc > 3) goto usage;

    char tag = '\0';
    bool bHeapInfo = false;
    if (argc > 1) {
        const char *arg = argv[1];
        if (lt_strcmp(arg, "settag") == 0) {
            if (argc != 3) goto usage;
            if (lt_strlen(argv[2]) != 1) goto usage;
            pCore->SetHeapTag(argv[2][0]);
            return 0;
        } else if (lt_strcmp(arg, "heapinfo") == 0) {
            bHeapInfo = true;
            if (argc == 3) {
                if (lt_strlen(argv[2]) != 1) goto usage;
                tag = argv[2][0];
            }
        }
        else goto usage;
    }

    if (! bHeapInfo) {
        u64 memstat = pCore->SnapshotMemstat();
        LT_SIZE nBigBlock = pCore->GetLargestAvailableBlockInRAM();

        // calc nBigBlock and units; default is k; change to mb, gb, or tb as necessary
        char * pUnit  = "k";
        if (nBigBlock >= (1024 << 10)) {
            pUnit = "mb"; nBigBlock >>= 10;
            if (nBigBlock >= (1024 << 10)) {
                pUnit = "gb"; nBigBlock >>= 10;
                if (nBigBlock >= (1024 << 10)) {
                    pUnit = "tb"; nBigBlock >>= 10;
                }
            }
        }
        LT_SIZE nBigBlockFractional = ((nBigBlock % 1024) * 100) >> 10;
        nBigBlock >>= 10;

        enum { kMemstatBuff = 42 };
        char * buff = lt_malloc(kMemstatBuff);
        if (buff) {
            pCore->FormatCanonicalMemstatString(memstat, buff, kMemstatBuff, false);
            iShell->Print(hShell, "memstat: %s, largest free block: %lu.%02lu%s\n",
                buff, LT_PLT_SIZE(nBigBlock), LT_PLT_SIZE(nBigBlockFractional), pUnit);
            lt_free(buff);
        }
        else {
            iShell->Print(hShell, "memstat: out of memory for report\n");
        }
    }

    if (bHeapInfo) {
        MemstatBlockInfoClientData clientData = { .iThread = iThread, .iShell = iShell, .hShell = hShell, .count = 0 };
        if (pCore->EnumerateHeapAllocatedBlockInfo(0, tag, &MemstatBlockInfoEnumCB, &clientData)) {
            if (tag) {
                iShell->Print(hShell, "%lu allocated blocks with tag %c\n", LT_Pu32(clientData.count), tag);
            } else {
                iShell->Print(hShell, "%lu allocated blocks\n", LT_Pu32(clientData.count));
            }
        }
        else {
            iShell->Print(hShell, "memstat: heapinfo unavailable\n");
        }
    }

    return 0;

usage:
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_MEMSTAT);
    return 0;
}

typedef struct SetThreadWatchdogClientData {
    LTTime fidelity;
    bool bAllowTermination;
} SetThreadWatchdogClientData;

static void LTShellCommands_SetThreadWatchdogClientDataReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    lt_free(pClientData);
}

static void LTShellCommands_SetThreadWatchdogTaskProc(void *pClientData) {
    if (pClientData) {
        SetThreadWatchdogClientData * pCD = (SetThreadWatchdogClientData *)pClientData;
        s_pWatchdog->WatchThread(pCD->fidelity, pCD->bAllowTermination);
    }
    else s_pWatchdog->UnwatchThread();
}

static bool LTShellCommands_OpenLTDeviceWatchdog(LTShell hShell, const char *cmd) {
    if (NULL == s_pWatchdog) {
         s_pWatchdog = lt_openlibrary(LTDeviceWatchdog);
         if (NULL == s_pWatchdog) {
             ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
             iShell->Print(hShell, "%s: watchdog is unavailalble\n", cmd);
             return false;
         }
    }
    return true;
}

static int
ShellCommand_PS(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    if (argc > 5) goto usage;
    ILTThread * iThread = (ILTThread *)pCore->GetLibraryInterface((LTLibrary *)pCore, "ILTThread");
    // argc == 1 : 'ps'
    if (argc == 1) {
        LTArray * pSnapshots = LTArray_CreateStructArray(sizeof(LTThread_Snapshot));
        iThread->SnapshotRunningThreads(ThreadsSnapshotCallback, pSnapshots);
        LTTime snapshotTime = pCore->GetKernelTime();
        static LTTime s_lastSnapshotTime = { .nNanoseconds = 0 };
        LTTime intervalTime = LTTime_Subtract(snapshotTime, s_lastSnapshotTime);
        s_lastSnapshotTime = snapshotTime;
        u32 nThreads = pSnapshots->API->GetCount(pSnapshots);
        LTArray * pRunTimes = LTArray_CreateStructArray(sizeof(ThreadRunTime));
        if (pRunTimes && intervalTime.nNanoseconds) {
            pRunTimes->API->SetCount(pRunTimes, nThreads);
            // Sort threads and display output
            pSnapshots->API->Sort(pSnapshots, LTShellCommands_SortPSArrayCompareFunction, NULL);
            iShell->PutString(hShell, "                                                  ___________________    ________________\n");
            iShell->PutString(hShell, "  ID  NAME                  PRIO  STATE           STACK   CURR    MAX    HEAP CURR    MAX   %CPU\n");
            for (u32 i = 0; i < nThreads; ++i) {
                LTThread_Snapshot * pSnapshot = pSnapshots->API->Get(pSnapshots, i, NULL);
                // Populate next run-time array
                ThreadRunTime runTime = { .hThread = pSnapshot->hThread, .runTime = pSnapshot->runTime };
                pRunTimes->API->Set(pRunTimes, i, &runTime);
                // Find thread's previous run time, if thread existed
                LTTime lastRunTime = LTTime_Zero();
                if (s_pLastRunTimes) {
                    for (u32 j = 0; j < s_pLastRunTimes->API->GetCount(s_pLastRunTimes); ++j) {
                        ThreadRunTime * pRunTime = s_pLastRunTimes->API->Get(s_pLastRunTimes, j, NULL);
                        if (pRunTime->hThread == pSnapshot->hThread) {
                            lastRunTime = pRunTime->runTime;
                            break;
                        }
                    }
                }
                float percentCPU = 100.0 * ((float)(pSnapshot->runTime.nNanoseconds - lastRunTime.nNanoseconds)) / (float)intervalTime.nNanoseconds;
                iShell->Print(hShell, "%4lu%2s%-20s  %4lu  %-15s%6lu  %5lu %6lu        %5lu %6lu   %.2f\n",
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
            void * pTemp = s_pLastRunTimes;
            s_pLastRunTimes = pRunTimes;
            lt_destroyobject(pTemp);
        } else {
            lt_destroyobject(s_pLastRunTimes);
            s_pLastRunTimes = NULL;
            iShell->PutString(hShell, "ps: memory error or bad time base\n");
        }
        lt_destroyobject(pSnapshots);
        return 0;
    }
    u32 nThreadID = lt_strtou32(argv[1], NULL, 10);
    LTThread hThread = 0;
    if ((0 == nThreadID) && (0 != lt_strcmp(argv[1], "0"))) {
        LTArray * pSnapshots = LTArray_CreateStructArray(sizeof(LTThread_Snapshot));
        LTShellCommands_FindThreadsFromName(argv[1], pSnapshots);
        u32 nSnapshots = pSnapshots->API->GetCount(pSnapshots);
        if (nSnapshots == 1) {
            LTThread_Snapshot * snapshot = pSnapshots->API->Get(pSnapshots, 0, NULL);
            hThread = snapshot->hThread;
            nThreadID = snapshot->nThreadNumber;
        } else if (nSnapshots > 1) {
            iShell->Print(hShell, "There are multiple threads with the same name\n");
            for (u32 i = 0; i < nSnapshots; ++i) {
                LTThread_Snapshot * snapshot = pSnapshots->API->Get(pSnapshots, i, NULL);
                LTShellCommands_PrintThreadSnapshot(hShell, snapshot->hThread);
                iShell->PutString(hShell, "\n");
            }
            lt_destroyobject(pSnapshots);
            // return and ignore other commandline args as they don't support multiple threads (at least for now)
            return 0;
        }
        lt_destroyobject(pSnapshots);
    } else {
        hThread = LTShellCommands_FindThreadFromID(nThreadID);
    }
    if (0 == hThread) return iShell->PutString(hShell, "ps: invalid thread\n"), 0;
    // argc == 2 : 'ps <id>'
    if (argc == 2) return LTShellCommands_PrintThreadSnapshot(hShell, hThread);
    // argc == 3 : 'ps <id> prio' or 'ps <id> terminate'
    if (argc == 3) {
        if (0 == lt_strcmp(argv[2], "prio")) {
            iShell->Print(hShell, "ps: thread %lu has priority %lu\n", LT_Pu32(nThreadID), LT_Pu32(iThread->GetPriority(hThread)));
        }
        else if (0 == lt_strcmp(argv[2], "terminate")) {
                 if (iThread->IsSystemThread(hThread))  iShell->PutString(hShell, "ps: can't terminate system thread\n");
            else if (iThread->IsCurrentThread(hThread)) iShell->PutString(hShell, "ps: won't terminate current thread\n");
            else {
                iShell->Print(hShell, "ps: terminating thread %lu\n", LT_Pu32(nThreadID));
                iThread->Terminate(hThread);
            }
        }
        else if (0 == lt_strcmp(argv[2], "heapinfo")) {
            MemstatBlockInfoClientData clientData = { .iThread = iThread, .iShell = iShell, .hShell = hShell, .count = 0 };
            if (pCore->EnumerateHeapAllocatedBlockInfo(hThread, 0, &MemstatBlockInfoEnumCB, &clientData)) {
                iShell->Print(hShell, "ps: thread %lu has %lu heap allocated blocks\n", LT_Pu32(nThreadID), LT_Pu32(clientData.count));
            }
            else {
                iShell->Print(hShell, "ps: heapinfo unavailable\n");
            }
        }
        else if (0 == lt_strcmp(argv[2], "wdog-clear")) {
            if (! LTShellCommands_OpenLTDeviceWatchdog(hShell, "ps")) return -1;
            iThread->QueueTaskProc(hThread, &LTShellCommands_SetThreadWatchdogTaskProc, NULL, NULL);
            iShell->Print(hShell, "ps: clearing watchdog monitoring on thread %lu\n", LT_Pu32(nThreadID));
        }
        else goto usage;
        return 0;
    }

    if ((argc == 4 || argc == 5) && (0 == lt_strcmp(argv[2], "wdog-set"))) {
        if (! LTShellCommands_OpenLTDeviceWatchdog(hShell, "ps")) return -1;
        u32 nFidelity = lt_strtou32(argv[3], NULL, 10);
        bool bAllowTermination = true;
        if (argc == 5) {
            if (0 == lt_strcmp(argv[4], "no-term")) {
              bAllowTermination = false;
            }
            else {
               goto usage;
            }
        }
        SetThreadWatchdogClientData *pCD = lt_malloc(sizeof(*pCD));
        if (pCD) {
            pCD->fidelity = LTTime_Milliseconds(nFidelity);
            pCD->bAllowTermination = bAllowTermination;
            iThread->QueueTaskProc(hThread, &LTShellCommands_SetThreadWatchdogTaskProc, LTShellCommands_SetThreadWatchdogClientDataReleaseProc, pCD);
            iShell->Print(hShell, "ps: setting watchdog monitoring on thread %lu\n", LT_Pu32(nThreadID));
        }
        else {
            iShell->Print(hShell, "ps: wachdog monitoring on thread %lu couldn't be set due to low memory situation\n", LT_Pu32(nThreadID));
            return -1;
        }
        return 0;
    }

    // argc == 4 : 'ps <id> prio [0..30]'
    if (0 != lt_strcmp(argv[2], "prio")) goto usage;
    u32 nPriority = lt_strtou32(argv[3], NULL, 10);
    if ((0 == nPriority && (0 != lt_strcmp(argv[3], "0"))) || (nPriority > kLTThread_PriorityHighest)) goto usage;
    if (iThread->IsSystemThreadTimeCritical(hThread)) iShell->PutString(hShell, "ps: can't set priority of system thread\n");
    else {
        // Reuse argc to store previous priority
        argc = (int)iThread->GetPriority(hThread);
        if ((int)nPriority != argc) {
            iThread->SetPriority(hThread, (u8)nPriority);
            iShell->Print(hShell, "ps: thread %lu changed priority from %lu to %lu\n", LT_Pu32(nThreadID), LT_Pu32(argc), LT_Pu32(nPriority));
        } else {
            iShell->Print(hShell, "ps: thread %lu already priority %lu\n", LT_Pu32(nThreadID), LT_Pu32(nPriority));
        }
    }
    return 0;
usage:
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_PS);
    return 0;
}

static void PrintLTHandleTableHeader(ILTShell * iShell, LTShell hShell, const char *pInterfaceName) {
    if (pInterfaceName) {
        int len = lt_strlen(pInterfaceName) + 1; /* room for an extra space */
        while (len--) iShell->PutChar(hShell, '_');
    }
    iShell->Print(hShell, "__________\n%s%sLT Handles\n", pInterfaceName ? pInterfaceName : "", pInterfaceName ? " " : "");
    iShell->PutString(hShell, "______  ____  _____  _____  ____________  _____             ____\n");
    iShell->PutString(hShell, "handle  pool  index  cycle  reservations  state             type\n");
}

static int
ShellCommand_LTHandle(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argv);
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    bool bCount = false;
    bool bList = false;
    const char *pInterfaceName = NULL;
    if (argc > 1 && argc <= 3) {
             if (0 == lt_strcmp(argv[1], "count")) bCount = true;
        else if (0 == lt_strcmp(argv[1], "list"))   bList = true;
        if (argc == 3 && argv[2][0]) pInterfaceName = argv[2];
    }
    if ((! bCount) && (! bList)) return iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_LTHANDLE), 0;

    // get the handle count
    u32 nHandles = pCore->GetHandlesByInterfaceName(NULL, 0, pInterfaceName);
    bool bAll = pInterfaceName && (0 == lt_strcmp(pInterfaceName, "all"));
    u32 nTotalCount = 0;
    if (bList && nHandles) {
        // get the list
        char threadName[kLTThread_MaxNameBuff];
        LTHandle * pHandles = lt_malloc(nHandles * sizeof(LTHandle));
        if (pHandles) {
            nHandles = pCore->GetHandlesByInterfaceName(pHandles, nHandles, pInterfaceName);
            if (nHandles) {
                if (bAll) PrintLTHandleTableHeader(iShell, hShell, pInterfaceName);
                for (u32 i = 0; i < nHandles; i++) {
                    LTInterface *pInterface = NULL;
                    const char *pThreadName = NULL;
                    const char *interfaceName = "____";
                    void *privateData = pCore->ReserveHandlePrivateData(pHandles[i]);
                    if (privateData) {
                        if ((! bAll) && (0 == nTotalCount)) PrintLTHandleTableHeader(iShell, hShell, pInterfaceName);
                        nTotalCount++;
                        pInterface = pCore->GetHandleInterface(pHandles[i]);
                        if (pInterface) {
                            interfaceName = pInterface->GetInterfaceName();
                            if (0 == lt_strcmp(interfaceName, "ILTThread")) {
                                ((ILTThread *)pInterface)->GetName(pHandles[i], threadName);
                                pThreadName = threadName;
                            }
                        }
                    }
                    iShell->Print(hShell, "%02X%02X%02X    %02d     %02d     %02X  %12d  %-16s  %s%s%s%s\n",
                        ((int)pHandles[i] >> 16) & 0xFF, ((int)pHandles[i] >> 8) & 0xFF, (int)pHandles[i] & 0xFF,
                        ((int)pHandles[i] >> 16) & 0xFF, ((int)pHandles[i] >> 8) & 0xFF, (int)pHandles[i] & 0xFF,
                        (int)pCore->GetHandleReservationCount(pHandles[i])-1, pCore->GetHandleStateString(pHandles[i]), interfaceName,
                    pThreadName ? "  [" : "", pThreadName ? pThreadName : "", pThreadName ? "]" : "");
                    if (privateData)  pCore->ReleaseHandlePrivateData(pHandles[i], privateData);
                }
            }
            lt_free(pHandles);
            if (bAll) {
                iShell->Print(hShell, "%lu allocated handle slots (%lu used, %lu available), %lu total bytes used\n", LT_Pu32(nHandles), LT_Pu32(nTotalCount), LT_Pu32(nHandles-nTotalCount), LT_Pu32(pCore->GetTotalHandleBytesOverhead()));
                return 0;
            }
            nHandles = nTotalCount;
            if (nHandles) {
                iShell->Print(hShell, "%lu %s handle%s%s", LT_Pu32(nHandles), pInterfaceName ? pInterfaceName : "total", (nHandles == 1 ? "" : "s"), (pInterfaceName ? "\n" : ""));
                if (! pInterfaceName) iShell->Print(hShell, ", %lu total bytes used\n", LT_Pu32(pCore->GetTotalHandleBytesOverhead()));
                return 0;
            }
        }
        else {
            iShell->PutString(hShell, "lthandle: insufficient memory to list handles\n");
            return -1;
        }
    }

    if (bAll) {
        nTotalCount = pCore->GetHandlesByInterfaceName(NULL, 0, NULL);
        iShell->Print(hShell, "lthandle: %lu allocated handle slots (%lu used, %lu available)", LT_Pu32(nHandles), LT_Pu32(nTotalCount), LT_Pu32(nHandles-nTotalCount));
        pInterfaceName = NULL;
    }
    else iShell->Print(hShell, "lthandle: %lu %s handle%s%s", LT_Pu32(nHandles), pInterfaceName ? pInterfaceName : "total", (nHandles == 1 ? "" : "s"), (pInterfaceName ? "\n" : ""));
    if (! pInterfaceName) iShell->Print(hShell, ", %lu total bytes used\n", LT_Pu32(pCore->GetTotalHandleBytesOverhead()));

    return 0;
}

#ifdef LT_DEBUG
    // sanity check to assert during compile in debug mode if kLTLibraryMaxNameLen or kLTInterface_MaxNameLen ever changes.
    // If it does we will want to revisit the tabulation algorithm in ListLibraries below
    typedef int LTList_NameLengthCheck[kLTLibrary_MaxNameLen == 39 ? 1 : -1];
    typedef int LTList_InterfaceNameLengthCheck[kLTInterface_MaxNameLen == 39 ? 1 : -1];
#endif

static int
ShellCommand_LTList(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argv);
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    LTLibEnumLibsClientData clientData;
    if (argc > 1) return iShell->PutString(hShell, "usage: ltlist\n"), 0;

    clientData.pOpenedLibs    = LTArray_CreateStructArray(sizeof(LTCore_LibrarySnapshot));
    clientData.pInstalledLibs = LTArray_CreateStructArray(kLTLibrary_MaxNameBufferSize);

    // snapshot the open libraries
    pCore->SnapshotOpenLibraries(&ShellCommand_EnumOpenLibrariesProc, &clientData);
    u32 nNumSnapshots = clientData.pOpenedLibs->API->GetCount(clientData.pOpenedLibs);
    if (nNumSnapshots > 0) {
        // print out the opened libraries snapshots
        clientData.pOpenedLibs->API->Sort(clientData.pOpenedLibs, LTShellCommands_SortOpenLibrariesArrayCompareFunction, NULL);
        iShell->PutString(hShell, "Installed LT Libraries:\n______________\nCURRENTLY OPEN\n");
        iShell->PutString(hShell, "  LIB NAME                           LIB INTERFACE                            LIB TYPE    OPEN COUNT\n");
        //iShell->PutString(hShell, "   123456789012345678901234567890    123456789012345678901234567890 v12345    12345678      12345678\n");
        s_mutex->API->Lock(s_mutex); // because we call LTShellCommands_IsShellOpenedLibInternal
        for (u32 i = 0; i < nNumSnapshots; i++) {
            LTCore_LibrarySnapshot * pSnapshot = clientData.pOpenedLibs->API->Get(clientData.pOpenedLibs, i, NULL);
            iShell->Print(hShell, "   %-30s    %s v%lu", pSnapshot->name, pSnapshot->rootInterfaceName, LT_Pu32(pSnapshot->nRootInterfaceVersion));
            int padding = pSnapshot->nRootInterfaceVersion;
            padding = (padding < 10) ? 3 : (padding < 100) ? 4 : (padding < 1000) ? 5 : (padding < 10000) ? 6 : 7; // " vN"
            padding += lt_strlen(pSnapshot->rootInterfaceName) > 30 ? 30 : lt_strlen(pSnapshot->rootInterfaceName);
            padding = (30 + 7) - padding;
            padding = (padding < 0) ? 0  : padding;
            padding += 4;
            if (padding > kLTInterface_MaxNameLen) padding = kLTInterface_MaxNameLen;
            pSnapshot->rootInterfaceName[padding] = 0;
            while (padding--) pSnapshot->rootInterfaceName[padding] = ' ';
            iShell->Print(hShell, "%s%-8s%14lu%s\n",
                pSnapshot->rootInterfaceName,
                (pSnapshot->rootInterfaceType == kLTInterfaceType_DeviceLibraryRoot) ? "Device" : (pSnapshot->rootInterfaceType == kLTInterfaceType_DriverLibraryRoot) ? "Driver" : "Standard",
                LT_Pu32(pSnapshot->nOpenCount), LTShellCommands_IsShellOpenedLibInternal(pSnapshot->name) ? "*" : "");
        }
        s_mutex->API->Unlock(s_mutex);
    }

    // now gather all of the 'installed' library names; the enum cb will trim the names of libraries we've already reported
    pCore->EnumerateInstalledLibraries(&LTShellCommands_RecordInstalledLibsEnumProc, &clientData);
    u32 nCount = clientData.pInstalledLibs->API->GetCount(clientData.pInstalledLibs);
    if (nCount > 0) {
        clientData.pInstalledLibs->API->Sort(clientData.pInstalledLibs, LTShellCommands_SortInstalledLibrariesArrayCompareFunction, NULL);
        iShell->PutString(hShell, "_________________\nAVAILABLE TO OPEN\n");
        for (u32 i = 0; i < nCount; i++) iShell->Print(hShell, "   %s\n", (const char *)clientData.pInstalledLibs->API->Get(clientData.pInstalledLibs, i, NULL));
    }

    lt_destroyobject(clientData.pOpenedLibs);
    lt_destroyobject(clientData.pInstalledLibs);

    return 0;
}

static int
ShellCommand_LTOpen(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);

    if (argc != 2) return iShell->PutString(hShell, "usage: ltopen <library>\n"), 0;
    if (NULL == argv[1] || 0 == argv[1][0]) return iShell->PutString(hShell, "ltopen: invalid library name\n"), -1;

    // check to see if we already have it opened
    s_mutex->API->Lock(s_mutex);
    if (LTShellCommands_IsShellOpenedLibInternal(argv[1])) {
        s_mutex->API->Unlock(s_mutex);
        return iShell->Print(hShell, "ltopen: %s already opened\n", argv[1]), 0;
    }
    s_mutex->API->Unlock(s_mutex);

    // open it
    LTLibrary * pLibrary = pCore->OpenLibrary(argv[1]);
    if (! pLibrary) return iShell->Print(hShell, "ltopen: failed to open %s\n", argv[1]), -1;

    // remember it
    struct ShellOpenedLibrary * pNew = (struct ShellOpenedLibrary *)lt_malloc(sizeof(*pNew));
    if (! pNew) { pCore->CloseLibrary(pLibrary); return iShell->PutString(hShell, "ltopen: allocation failure\n"), -1; }
    pNew->pLibrary = pLibrary;
    s_mutex->API->Lock(s_mutex);
    pNew->pNext = s_pShellOpenedLibraries;
    s_pShellOpenedLibraries = pNew;
    s_mutex->API->Unlock(s_mutex);

    iShell->Print(hShell, "ltopen: %s opened\n", argv[1]);
    return 0;
}

static int
ShellCommand_LTClose(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);

    if (argc != 2) return iShell->PutString(hShell, "usage: ltclose <library>\n"), 0;
    if (NULL == argv[1] || 0 == argv[1][0]) return iShell->PutString(hShell, "ltclose: invalid library name\n"), -1;

    // find the record in the list
    s_mutex->API->Lock(s_mutex);
    struct ShellOpenedLibrary * pLast = NULL;
    struct ShellOpenedLibrary * pCurr = s_pShellOpenedLibraries;
    while (pCurr) {
        if (0 == lt_strcmp(argv[1], pCurr->pLibrary->GetLibraryExtrinsicName())) {
            if (NULL == pLast) s_pShellOpenedLibraries = pCurr->pNext;
            else pLast->pNext = pCurr->pNext;
            break;
        }
        pLast = pCurr;
        pCurr = pCurr->pNext;
    }
    s_mutex->API->Unlock(s_mutex);
    if (NULL == pCurr) return iShell->Print(hShell, "ltclose: %s is not a shell opened library\n", argv[1]), -1;
    pCore->CloseLibrary(pCurr->pLibrary);
    lt_free(pCurr);

    iShell->Print(hShell, "ltclose: %s closed\n", argv[1]);
    return 0;
}

static int
ShellCommand_LTRun(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    ILTThread * iThread;

    LTRunClientData clientData;
    clientData.argc = argc;
    clientData.argv = argv;
    clientData.nRetVal = 0;
    clientData.pLibrary = NULL;

    LTThread hThread = 0;
    u32 nStackSize = 0;
    do
    {
        if (argc < 2 || 0 == argv[1][0]) {
            iShell->PutString(hShell, "usage: ltrun <library> [args]\n");
            break;
        }
        if (NULL == (clientData.pLibrary = pCore->OpenLibrary(argv[1]))) { iShell->Print(hShell, "ltrun: failed to open library %s\n", argv[1]); break; }
        if (0 == (hThread = pCore->CreateThread(argv[1]))) { iShell->PutString(hShell, "ltrun: failed to create run thread\n"); break; }
        iThread = (ILTThread *)pCore->GetHandleInterface(hThread);
        if (NULL == clientData.pLibrary->Run) { iShell->Print(hShell, "ltrun: %s has no Run() function\n", argv[1]); break; }
        if (0 != (nStackSize = clientData.pLibrary->GetRunFunctionStacksizeRequirement())) iThread->SetStackSize(hThread, nStackSize);
        iThread->Start(hThread, NULL, NULL);
        iThread->QueueTaskProc(hThread, LTRunTaskProc, NULL, &clientData);
        iThread->WaitUntilFinished(hThread, LTTime_Infinite());
        iShell->Print(hShell, "ltrun: %s exited with code %d\n", argv[1], clientData.nRetVal);
    }
    while (false);

    if (hThread) pCore->DestroyHandle(hThread);
    if (clientData.pLibrary) pCore->CloseLibrary(clientData.pLibrary);
    return 0;
}

static int ShellCommand_TelnetD(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv); LT_UNUSED(hShell);
#if 0
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc != 1) {
        iShell->PutString(hShell, "usage: telnetd\n");
    }
    else {
        iShell->Print(hShell, "telnetd: telnetd is %s\n", LTNetworkShellImpl_StartNetworkShellSubsystem() ? "active on port 4444" : "inactive");
    }
#endif
    return 0;
}

typedef struct {
    LTShell            hShell;
    ILTShell         * iShell;
    LTSystemSettings * pSettings;
    bool               bRecursive;
    bool               bSuccess;
} GetCallbackData;

static bool ShellCommand_GetCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    GetCallbackData * pData = (GetCallbackData *)pClientData;
    if (!pData->bRecursive && pKeySuffix[0] != '\0') return true;
    switch (type) {
    case kLTSystemSettingsDataType_Integer:;
        s64 nTemp;
        pData->bSuccess = pData->pSettings->GetIntegerValue(pKey, &nTemp);
        if (pData->bSuccess) pData->iShell->Print(pData->hShell, "%s = %lld [0x%llx]\n", pKey, LT_Ps64(nTemp), LT_Ps64(nTemp));
        break;
    case kLTSystemSettingsDataType_String:;
        LTString pString = NULL;
        pData->bSuccess = pData->pSettings->GetStringValue(pKey, &pString);
        if (pData->bSuccess) pData->iShell->Print(pData->hShell, "%s = \"%s\"\n", pKey, pString);
        ltstring_destroy(pString);
        break;
    case kLTSystemSettingsDataType_Binary:
        pData->bSuccess = true;
        pData->iShell->Print(pData->hShell, "%s = <binary>\n", pKey);
        break;
    default:
        break;
    }
    return true;
}

static int ShellCommand_Get(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    GetCallbackData clientData = {
        .hShell     = hShell,
        .iShell     = (ILTShell *)pCore->GetHandleInterface(hShell),
        .pSettings  = (LTSystemSettings *)pCore->OpenLibrary("LTSystemSettings"),
        .bRecursive = false,
        .bSuccess   = false
    };
    if (clientData.pSettings) {
        if (argc == 2) {
            u32 nLen = lt_strlen(argv[1]);
            char * pName = (char *)lt_malloc(nLen + 1);
            if (!pName) return 0;
            if (nLen > 0 && argv[1][nLen - 1] == '*') {
                nLen--;
                clientData.bRecursive = true;
            }
            lt_strncpyTerm(pName, argv[1], nLen + 1);
            clientData.pSettings->EnumerateSettingsWithPrefix(pName, ShellCommand_GetCallback, (void *)&clientData);
            if (!clientData.bSuccess) clientData.iShell->Print(hShell, "get: get failed\n");
            lt_free(pName);
        } else {
            clientData.iShell->Print(hShell, "usage: get <name|prefix*>\n");
        }
        pCore->CloseLibrary((LTLibrary *)clientData.pSettings);
    }
    return 0;
}

static int ShellCommand_Set(LTShell hShell, int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    ILTShell * iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    bool bPrintUsage = false;
    bool bSuccess = false;
    LTSystemSettings * pSettings = (LTSystemSettings *)pCore->OpenLibrary("LTSystemSettings");
    if (pSettings) {
        if (argc == 4 && argv[1][0] == '-') {
            switch(argv[1][1]) {
            case 'b':;
                LTUtilityByteOps * pByteOps = (LTUtilityByteOps *)pCore->OpenLibrary("LTUtilityByteOps");
                if (pByteOps) {
                    u32 nInputLen = lt_strlen(argv[3]);
                    u32 nOutputLen = pByteOps->GetBase64DecodeBufferRequirement(nInputLen);
                    u8 * pBuffer = lt_malloc(nOutputLen);
                    if (pBuffer) {
                        nOutputLen = pByteOps->Base64Decode(argv[3], nInputLen, pBuffer, nOutputLen);
                        bSuccess = pSettings->SetBinaryValue(argv[2], pBuffer, nOutputLen);
                        lt_free(pBuffer);
                    }
                    pCore->CloseLibrary((LTLibrary *)pByteOps);
                }
                break;
            case 'i':;
                s64 nValue = lt_strtos64(argv[3], NULL, 0);
                bSuccess = pSettings->SetIntegerValue(argv[2], nValue);
                break;
            default:
                bPrintUsage = true;
                break;
            }
        } else if (argc == 3) {
            if (argv[1][0] == '-') {
                if (argv[1][1] == 'd') bSuccess = pSettings->DeleteSetting(argv[2]);
                else bPrintUsage = true;
            } else {
                bSuccess = pSettings->SetStringValue(argv[1], argv[2]);
            }
        } else {
            bPrintUsage = true;
        }
        pCore->CloseLibrary((LTLibrary *)pSettings);
    }
    if (bPrintUsage) iShell->Print(hShell, "usage: set [-i|-b|-d] <name> [<value>]\n");
    else if (bSuccess) iShell->Print(hShell, "set: set succeeded\n");
    else iShell->Print(hShell, "set: set failed\n");
    return 0;
}

static int ShellCommand_Log(LTShell hShell, int argc, const char ** argv) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);
    if (! iShell) return -1;
    if (argc < 2) return ShellHelp_Log(hShell, argc, argv), -2;
    int result = 0;
    LTSystemLogger *logger = lt_openlibrary(LTSystemLogger);
    if (logger) {
        if (lt_strcmp(argv[1], "level") == 0) {
            if (argc <= 3) {
                LTCore_LogFlags filterLevel;
                if (argc == 3) {
                    filterLevel = lt_strtou32(argv[2], NULL, 10);
                    logger->SetFilterLevel(filterLevel & kLTCore_LogFlags_LogTypeMask);
                } else {
                    filterLevel = logger->GetFilterLevel();
                }
                iShell->Print(hShell, "min level: %lu\n", LT_Pu32(filterLevel));
            }
            else result = -2;
        } else if (lt_strcmp(argv[1], "echo") == 0) {
            LT_SIZE len = 0;
            for (int i = 2; i < argc; ++i) {
                len += lt_strlen(argv[i]) + 1;
            }
            char *buffer = lt_malloc(len);
            char *next   = buffer;
            for (int i = 2; i < argc; ++i) {
                len = lt_strlen(argv[i]);
                lt_memcpy(next, argv[i], len);
                next    += len;
                *next++  = ' ';
            }
            *--next = '\0';
            logger->Print("shell",
                          "echo",
                          kLTCore_LogFlags_LogTypeLog | kLTCore_LogFlags_LogToConsole,
                          "%s",
                          buffer);
            lt_free(buffer);
        } else {
            if (lt_strcmp(argv[1], "stats") == 0) logger->DebugPrintStatistics();
            else result = -2;
        }
    lt_closelibrary(logger);
    }

    if (result != 0) ShellHelp_Log(hShell, argc, argv);
    return result;
}

static bool ActuateLTExit(void) {
    LTConsoleShellImpl_StopConsoleThread();
    LT_GetCore()->DestroyHandle(lt_getlibraryinterface(ILTThread, LT_GetCore())->GetCurrentThread());
    LT_GetCore()->TerminateLT(0);
    return false;
}

static int ShellCommand_LTExit(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argv);
    if (argc != 1) {
        ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
        iShell->PutString(hShell, "usage: ltexit\n");
        return 0;
    }
    /* first close all of our open libraries, otherwise one that holds us open might become the
       last open reference count holder of our library and deadlock as our LibFini blocks on closing
       them as they block on closing us */
    LTShellCommands_CloseAllShellOpenedLibraries(hShell);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    iThread->Terminate(iThread->GetCurrentThread());
    LTThread hThread = LT_GetCore()->CreateThread("ltexit");
    if (hThread) {
        iThread->SetPriority(hThread, kLTThread_PriorityHighest);
        iThread->Start(hThread, &ActuateLTExit, NULL);
    }
    return 0;
}

static int ShellCommand_Reboot(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    LTShellImpl_Reboot(hShell);
    return 0;
}

static int ShellCommand_Sleep(LTShell hShell, int argc, const char ** argv) {
    LTCore    *pCore = LT_GetCore();
    ILTShell  *iShell = (ILTShell *)pCore->GetHandleInterface(hShell);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, pCore);

    if (argc != 2) {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_SLEEP);
        return 1;
    }

    u32 seconds = lt_strtou32(argv[1], NULL, 10);
    if (seconds == 0) {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_SLEEP);
        return 1;
    }

    iShell->Print(hShell, "Sleeping for %lu seconds\n", LT_Pu32(seconds));
    iThread->Sleep(LTTime_Seconds(seconds));
    iShell->Print(hShell, "Slept for %lu seconds\n", LT_Pu32(seconds));

    return 0;
}

static int ShellCommand_Uptime(LTShell hShell, int argc, const char ** argv) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);

    LT_UNUSED(argc);
    LT_UNUSED(argv);

    const LTTime uptime = LT_GetCore()->GetKernelTime();
    iShell->Print(hShell, "uptime: %lld nanoseconds\n",
                  LT_Ps64(LTTime_GetNanoseconds(uptime)));

    s64 seconds = LTTime_GetSeconds(uptime);
    const s64 days = seconds / 86400;
    seconds %= 86400;
    const s64 hours = seconds / 3600;
    seconds %= 3600;
    const s64 minutes = seconds / 60;
    iShell->Print(hShell, "uptime: %lld day(s), %lld hour(s), %lld minute(s)\n",
                  LT_Ps64(days), LT_Ps64(hours), LT_Ps64(minutes));

    return 0;
}

static int ShellCommand_Watchdog(LTShell hShell, int argc, const char ** argv) {
    enum Command { kNone, kStatus, kEnable, kDisable, kReset, kSetTimeout, kCrash };
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    enum Command command = kNone;
    u32 timeout = 0;

    switch (argc) {
    case 1:
        command = kStatus;
        break;

    case 2:
        if (0 == lt_strcmp(argv[1], "enable") ||
            0 == lt_strcmp(argv[1], "on")) {
            command = kEnable;
        } else if (0 == lt_strcmp(argv[1], "disable") ||
                   0 == lt_strcmp(argv[1], "off")) {
            command = kDisable;
        } else if (0 == lt_strcmp(argv[1], "reset") ||
                   0 == lt_strcmp(argv[1], "kick") ||
                   0 == lt_strcmp(argv[1], "tickle")) {
            command = kReset;
        } else if (0 == lt_strcmp(argv[1], "status")) {
            command = kStatus;
        } else if (0 == lt_strcmp(argv[1], "crash")) {
            command = kCrash;
        }
        break;

    case 3:
        if (0 != lt_strcmp(argv[1], "settimeout")) break;
        command = kSetTimeout;
        timeout = lt_strtou32(argv[2], NULL, 10);
        if (!timeout) {
            iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WATCHDOG);
            return 1;
        }
    }
    if (kNone == command) {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WATCHDOG);
        return 1;
    }

    if (! LTShellCommands_OpenLTDeviceWatchdog(hShell, "watchdog")) return 1;

    int ret = 0;
    switch (command) {
        case kNone:
            break;

        case kStatus:
            {
                iShell->Print(hShell, "watchdog: Watchdog is %sabled.\n", s_pWatchdog->IsEnabled() ? "en" : "dis");
                const char *bootReason = NULL;
                s_pWatchdog->GetBootReason(&bootReason);
                iShell->Print(hShell, "Reset reason: %s\n", bootReason);
            }
            break;

        case kEnable:
            if (s_pWatchdog->EnableTimer()) {
                iShell->PutString(hShell, "watchdog: Watchdog is enabled.\n");
            } else {
                ret = 1;
                iShell->PutString(hShell, "watchdog: Failed to enable Watchdog\n");
            }
            break;

        case kDisable:
            if (s_pWatchdog->DisableTimer()) {
                iShell->PutString(hShell, "watchdog: Watchdog is disabled.\n");
            } else {
                ret = 1;
                iShell->PutString(hShell, "watchdog: Failed to disable Watchdog\n");
            }
            break;

        case kReset:
            if (s_pWatchdog->ResetTimer()) {
                iShell->PutString(hShell, "watchdog: Watchdog timer reset\n");
            } else {
                ret = 1;
                iShell->PutString(hShell, "watchdog: Failed to reset Watchdog timer\n");
            }
            break;

        case kCrash:
            {
                iShell->PutString(hShell, "watchdog: Crashing the system\n");
                // Trigger a crash
                LT_ASSERT(0);
            }
            break;

        case kSetTimeout:
            if (s_pWatchdog->SetTimeout(LTTime_Seconds(timeout))) {
                iShell->Print(hShell, "watchdog: Set Watchdog timeout to %lu seconds.\n", LT_Pu32(timeout));
            } else {
                ret = 1;
                iShell->PutString(hShell, "watchdog: Failed to set Watchdog timeout\n");
            }
            break;

        default:
            break;
    }

    return ret;
}

static int ShellCommand_Exit(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argv);
    if (argc != 1) {
        ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
        iShell->PutString(hShell, "usage: exit\n");
        return 0;
    }
    // temporary until shells are exitable
    if (LTShellImpl_IsConsoleShell(hShell)) {
        // if we're the console shell, just simulate an exit and restart by clearing
        // the terminal window and printing the banner text again
        ShellCommand_Clear(hShell, 1, NULL);
        LTShellImpl_TemporarySimulateShellRestart(hShell);
    }
    else {
        // Destroy the network shell, disconnecting the client
        LT_GetCore()->Destroy(hShell);
    }
    return 0;
}

#ifdef LT_DEBUG
static int ShellCommand_ReadReg(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc < 2) {
        iShell->PutString(hShell, "usage: readreg [hex-address] [count (optional)]\n");
        iShell->PutString(hShell, "       ex: readreg 0x3ff44088 (GPIO_PIN0 config)\n");
        iShell->PutString(hShell, "       ex: readreg 0x3ff44088 32 (GPIO_PIN[0-32] config)\n");
        iShell->PutString(hShell, "WARNING: Using an invalid register address could crash the system\n");
    } else {
        volatile u32 * pRegAddr = (volatile u32 *)(LT_SIZE)lt_strtou32(argv[1], NULL, 16);
        if ((LT_SIZE)pRegAddr & 0x3) {
            iShell->Print(hShell, "Register addresses must be 4-byte aligned\n");
        } else {
            if (argc == 2) {
                u32 nVal = *pRegAddr;
                iShell->Print(hShell, "Register %p = 0x%08lx\n", pRegAddr, LT_Pu32(nVal));
            } else {
                u8 count = lt_strtou32(argv[2], NULL, 10);
                for (u8 i = 0; i < count; ++i, ++pRegAddr) {
                    u32 nVal = *pRegAddr;
                    iShell->Print(hShell, "Register %p [%02d] = 0x%08lx\n", pRegAddr, i, LT_Pu32(nVal));
                }
            }
        }
    }
    return 0;
}

static int ShellCommand_WriteReg(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc < 3) {
        iShell->PutString(hShell, "usage: writereg [hex-address] [hex-value]\n");
        iShell->PutString(hShell, "       ex: writereg 0x3ff44088 0x8100(Enables rising interrupts for GPIO_PIN0)\n");
        iShell->PutString(hShell, "WARNING: Using an invalid register address could crash the system\n");
    } else {
        volatile u32 * pRegAddr = (volatile u32 *)(LT_SIZE)lt_strtou32(argv[1], NULL, 16);
        if ((LT_SIZE)pRegAddr & 0x3) {
            iShell->Print(hShell, "Register addresses must be 4-byte aligned\n");
        } else {
            u32 nVal = lt_strtou32(argv[2], NULL, 16);
            iShell->Print(hShell, "Writing 0x%lx to register %p\n", LT_Pu32(nVal), pRegAddr);
            *pRegAddr = nVal;
        }
    }
    return 0;
}
#endif // LT_DEBUG

static int ShellCommand_ConsolePrint(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    if (argc == 1) {
        return 0;
    } else if (argc == 2) {
        if (LTShellImpl_IsConsoleShell(hShell)) {
            iShell->PutString(hShell, "consoleprint can only be enabled/disabled on Network Shell\n");
            return 0;
        } else {
            // Only network shell supports enabling/disabling consoleprint
            // 'consoleprint on' or 'consoleprint off' ... toggle state appropriately
            int newstate = (lt_strcmp(argv[1], "off") == 0) ? 0
                         : (lt_strcmp(argv[1], "on" ) == 0) ? 1
                         : -1;
            if (newstate >= 0) {
                return 0;
            }
            // drop through to print usage
        }
    }

    // print usage
    iShell->PutString(hShell, "usage: consoleprint [on|off]\n");
    return 0;
}

static int WaitForNetwork(LTShell hShell, const LTTime *endTime,
                       bool expectedState) {
    LTCore    *pCore = LT_GetCore();
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, pCore);
    ILTShell  *iShell = lt_gethandleinterface(ILTShell, hShell);

    LTNetMonitor *pNetMonitor = lt_openlibrary(LTNetMonitor);
    if (!pNetMonitor) {
        iShell->PutString(hShell, "Cannot open LTNetMonitor library\n");
        return 1;
    }

    for (LTTime now = pCore->GetKernelTime();
         pNetMonitor->IsUp() != expectedState &&
             LTTime_IsLessThanOrEqual(now, *endTime);
         now = pCore->GetKernelTime()) {
        iThread->Sleep(LTTime_Milliseconds(100));
    }

    int ret = 0;
    if (pNetMonitor->IsUp() != expectedState) {
        ret = 1;
        iShell->PutString(hShell, "Network not in expected state.\n");
    }

    lt_closelibrary(pNetMonitor);
    return ret;
}

static int WaitForNetworkUp(LTShell hShell, const LTTime *endTime) {
    return WaitForNetwork(hShell, endTime, true);
}

static int WaitForNetworkDown(LTShell hShell, const LTTime *endTime) {
    return WaitForNetwork(hShell, endTime, false);
}

static int WaitForWiFi(LTShell hShell, const LTTime *endTime,
                       bool expectedState) {
    LTCore    *pCore = LT_GetCore();
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, pCore);
    ILTShell  *iShell = lt_gethandleinterface(ILTShell, hShell);

    LTDeviceWiFi *pDeviceWiFi = lt_openlibrary(LTDeviceWiFi);
    if (!pDeviceWiFi) {
        iShell->PutString(hShell, "Cannot open LTDeviceWiFi library\n");
        return 1;
    }

    for (LTTime now = pCore->GetKernelTime();
         pDeviceWiFi->IsConnected() != expectedState &&
             LTTime_IsLessThanOrEqual(now, *endTime);
         now = pCore->GetKernelTime()) {
        iThread->Sleep(LTTime_Milliseconds(100));
    }

    int ret = 0;
    if (pDeviceWiFi->IsConnected() != expectedState) {
        ret = 1;
        iShell->PutString(hShell, "WiFi not in expected state.\n");
    }

    lt_closelibrary(pDeviceWiFi);
    return ret;
}

static int WaitForWiFiConnected(LTShell hShell, const LTTime *endTime) {
    return WaitForWiFi(hShell, endTime, true);
}

static int WaitForWiFiDisconnected(LTShell hShell, const LTTime *endTime) {
    return WaitForWiFi(hShell, endTime, false);
}

static int ShellCommand_WaitFor(LTShell hShell, int argc, const char ** argv) {
    ILTShell *iShell = lt_gethandleinterface(ILTShell, hShell);

    if (argc != 3) {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WAITFOR);
        return 0;
    }

    int (*waitFunc)(LTHandle, const LTTime *) = NULL;
    if (lt_strcmp(argv[1], "network-down") == 0) {
        waitFunc = WaitForNetworkDown;
    } else if (lt_strcmp(argv[1], "network-up") == 0) {
        waitFunc = WaitForNetworkUp;
    } else if (lt_strcmp(argv[1], "wifi-connected") == 0) {
        waitFunc = WaitForWiFiConnected;
    } else if (lt_strcmp(argv[1], "wifi-disconnected") == 0) {
        waitFunc = WaitForWiFiDisconnected;
    } else {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WAITFOR);
        return 1;
    }
    LT_ASSERT(waitFunc);

    u32 timeout = lt_strtou32(argv[2], NULL, 10);
    if (!timeout) {
        iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WAITFOR);
        return 1;
    }

    LTTime endTime = LT_GetCore()->GetKernelTime();
    LTTime_AddTo(endTime, LTTime_Seconds(timeout));
    return waitFunc(hShell, &endTime);
}

static bool CountDisallowanceGrantEnumProc(u32 nGrantNumber, void * pCallerAddress, void * pClientData) {
    LT_UNUSED(nGrantNumber); LT_UNUSED(pCallerAddress);
    u32 * pCount = (u32 *)pClientData;
    *pCount = *pCount + 1;
    return true;
}

static u32 CountDisallowanceGrants(void) {
    u32 nCount = 0;;
    LT_GetCore()->EnumerateSleepModeDisallowanceGrants(&CountDisallowanceGrantEnumProc, &nCount);
    return nCount;
}

typedef struct PrintDisallowanceGrantsClientData {
    ILTShell * iShell;
    LTShell hShell;
    u32 nCount;
} PrintDisallowanceGrantsClientData;

static bool PrintDisallowanceGrantsEnumProc(u32 nGrantNumber, void * pCallerAddress, void * pClientData) {
    PrintDisallowanceGrantsClientData * pCD = (PrintDisallowanceGrantsClientData *)pClientData;
    pCD->nCount++;
    if (1 == pCD->nCount) {
        pCD->iShell->Print(pCD->hShell, "__  _____  _____\n");
        pCD->iShell->Print(pCD->hShell, " #  Grant  Caller\n");
    }
    pCD->iShell->Print(pCD->hShell, "%2lu  %5lu  0x%lx\n", LT_Pu32(pCD->nCount), LT_Pu32(nGrantNumber), LT_PLT_SIZE((LT_SIZE)pCallerAddress));
    return true;
}

static void PrintDisallowanceGrants(ILTShell *iShell, LTShell hShell) {
    PrintDisallowanceGrantsClientData cd = { .iShell = iShell, .hShell = hShell, .nCount = 0 };
    LT_GetCore()->EnumerateSleepModeDisallowanceGrants(&PrintDisallowanceGrantsEnumProc, &cd);
    iShell->Print(hShell, "\nsleepmode: %lu disallowance grant%s issued\n", LT_Pu32(cd.nCount), cd.nCount == 1 ? "" : "s");
}

static LTTime LTCore_DummyEnterSleepModeProc(LTTime durationUntilFirstTimerWakeup, void * pClientData) {
    LT_UNUSED(durationUntilFirstTimerWakeup);
    LTShell hShell = VOIDPTR_TO_LTHANDLE(pClientData);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    LTCore *pCore = LT_GetCore();
    char timeBuff[24];
    LTTime startTime = pCore->GetKernelTime();
    pCore->FormatCanonicalTimeString(startTime, timeBuff, sizeof(timeBuff), true);

    iShell->Print(hShell, "%s[sleepmode.enter] DummyEnterSleepModeProc entering simulated sleep mode...\n", timeBuff);
    pCore->FlushConsoleOutput();
    LT_SIZE nMask = pCore->Disable();
    startTime = pCore->GetKernelTime();
    LTTime stopTime = LTTime_Add(startTime, LTTime_Milliseconds(350));
    while (LTTime_IsLessThan(pCore->GetKernelTime(), stopTime)) { }
    stopTime = pCore->GetKernelTime();
    pCore->Enable(nMask);
    pCore->FormatCanonicalTimeString(startTime, timeBuff, sizeof(timeBuff), true);
    iShell->Print(hShell, "%s[sleepmode.exit] DummyEnterSleepModeProc exiting simulated sleep mode...\n", timeBuff);
    pCore->FlushConsoleOutput();
    return LTTime_Subtract(stopTime, startTime);
}

static bool s_bAbortSleepMode = false;
static u32  s_nSleepAbortDisallowanceGrant = 0;

static LTTime ShellCommand_SleepActionEventProc(LTCore_SleepAction action, LTTime whenToAwakenOrTimeSpentInSleep, void * pClientData) {
    LTTime time = LTTime_Zero();
    LTShell hShell = VOIDPTR_TO_LTHANDLE(pClientData);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    LTCore *pCore = LT_GetCore();
    char kernelTimeBuff[24];
    char sleepTimeBuff[24];
    LTTime kernelTime = pCore->GetKernelTime();
    pCore->FormatCanonicalTimeString(kernelTime, kernelTimeBuff, sizeof(kernelTimeBuff), true);
    pCore->FormatCanonicalTimeString(whenToAwakenOrTimeSpentInSleep, sleepTimeBuff, sizeof(sleepTimeBuff), action == kLTCore_SleepAction_AwakenedFromSleep ? false : true);
    switch (action) {
        case kLTCore_SleepAction_GoingToSleep:
            if (s_bAbortSleepMode) {
                iShell->Print(hShell, "%s[sleep.action] received kLTCore_SleepAction_GoingToSleep until %s, taking disallowance grant\n", kernelTimeBuff, sleepTimeBuff);
                s_nSleepAbortDisallowanceGrant = pCore->DisallowSleepMode();
            }
            else {
                iShell->Print(hShell, "%s[sleep.action] received kLTCore_SleepAction_GoingToSleep until %s\n", kernelTimeBuff, sleepTimeBuff);
            }
            break;
        case kLTCore_SleepAction_SleepAborted:
            if (s_nSleepAbortDisallowanceGrant) {
                iShell->Print(hShell, "%s[sleep.action] received kLTCore_SleepAction_SleepAborted, returning disallowance grant\n", kernelTimeBuff);
                pCore->ReallowSleepMode(s_nSleepAbortDisallowanceGrant);
            }
            else {
                iShell->Print(hShell, "%s[sleep.action] received kLTCore_SleepAction_SleepAborted\n", kernelTimeBuff);
            }
            break;
        case kLTCore_SleepAction_AwakenedFromSleep:
            iShell->Print(hShell, "%s[sleep.action] received kLTCore_SleepAction_AwakenedFromSleep after %ss\n", kernelTimeBuff, sleepTimeBuff);
            break;
        default:
            iShell->Print(hShell, "%s[sleep.action] received unknown sleep action\n", kernelTimeBuff);
            break;
    }
    return time;
}

static int
ShellCommand_SleepMode(LTShell hShell, int argc, const char **argv) {
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    LTCore *pCore = LT_GetCore();
    int nRetVal = 0;
    bool bUsage = false;

    if (argc == 3) {
        if (0 == lt_strcmp(argv[1], "setdelay")) {
            u32 nMS = lt_strtou32(argv[2], NULL, 10);
            LTTime delay = LTTime_Milliseconds(nMS);
            pCore->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(delay, LTTime_Zero());
            if (LTTime_IsZero(delay)) {
                iShell->Print(hShell, "sleepmode: sleep mode disabled\n");
            }
            else {
                char timeBuff[24];
                pCore->FormatCanonicalTimeString(delay, timeBuff, sizeof(timeBuff), false);
                iShell->Print(hShell, "sleepmode: sleep mode delay set to %ss\n", timeBuff);
            }
        }
        else if (0 == lt_strcmp(argv[1], "reallow")) {
            u32 nDisallowanceGrant = lt_strtou32(argv[2], NULL, 10);
            if (nDisallowanceGrant && (nDisallowanceGrant <= kLTCore_MaxSleepModeModeDisallowanceGrants)) {
                pCore->ReallowSleepMode(nDisallowanceGrant);
                u32 nCount = CountDisallowanceGrants();
                iShell->Print(hShell, "sleepmode: disallowance grant %lu returned, %lu/%lu remain issued\n", LT_Pu32(nDisallowanceGrant), LT_Pu32(nCount), LT_Pu32(kLTCore_MaxSleepModeModeDisallowanceGrants));
            }
            else {
                iShell->Print(hShell, "sleepmode: grant number must be from 1 to %d\n", (int)kLTCore_MaxSleepModeModeDisallowanceGrants);
            }
        }
        else if (0 == lt_strcmp(argv[1], "abort")) {
            if (0 == lt_strcmp(argv[2], "on")) {
                s_bAbortSleepMode = true;
                iShell->Print(hShell, "sleepmode: will take disallowance grant when GoingToSleep and release when Aborted\n");
            }
            else if (0 == lt_strcmp(argv[2], "off")) {
                s_bAbortSleepMode = false;
                iShell->Print(hShell, "sleepmode: will not take disallowance grant when GoingToSleep\n");
            }
            else bUsage = true;
        }
        else bUsage = true;
    }
    else if (argc == 2) {
        if (0 == lt_strcmp(argv[1], "cleardelay")) {
            pCore->SetEnterSleepModeIdleDelayAndMinimumSleepDuration(LTTime_Zero(), LTTime_Zero());
            iShell->Print(hShell, "sleepmode: sleep mode disabled\n");
        }
        else if (0 == lt_strcmp(argv[1], "setproc")) {
            pCore->SetEnterSleepModeProc(&LTCore_DummyEnterSleepModeProc, LTHANDLE_TO_VOIDPTR(hShell));
            pCore->OnSleepAction(&ShellCommand_SleepActionEventProc, LTHANDLE_TO_VOIDPTR(hShell));
            iShell->Print(hShell, "sleepmode: dummy EnterSleepModeProc set\n");
        }
        else if (0 == lt_strcmp(argv[1], "clearproc")) {
            pCore->SetEnterSleepModeProc(NULL, NULL);
            pCore->NoSleepAction(&ShellCommand_SleepActionEventProc);
            iShell->Print(hShell, "sleepmode: EnterSleepModeProc cleared\n");
        }
        else if (0 == lt_strcmp(argv[1], "disallow")) {
            u32 nGrant = lt_disallowsleepmode();
            u32 nCount = CountDisallowanceGrants();
            if (0 == nGrant) iShell->Print(hShell, "sleepmode: no disallowance grants available, %lu/%lu issued\n", LT_Pu32(nCount), LT_Pu32(kLTCore_MaxSleepModeModeDisallowanceGrants));
            else iShell->Print(hShell, "sleepmode: issued disallowance grant %lu, %lu/%lu issued\n", LT_Pu32(nGrant), LT_Pu32(nCount), LT_Pu32(kLTCore_MaxSleepModeModeDisallowanceGrants));
        }
        else if (0 == lt_strcmp(argv[1], "disallowances")) {
            PrintDisallowanceGrants(iShell, hShell);
        }
        else bUsage = true;
    }
    else bUsage = true;

    if (bUsage) iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_SLEEPMODE);
    return nRetVal;
}


/*______________________________________
_/ LTShellCommands HELP FUNCTIONS!!!! */
static void
ShellHelp_Stty(LTShell hShell, int argc, const char **argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_STTY);
    iShell->PutString(hShell, "The stty (sET tELEtyPE) command changes and prints terminal settings:\n");
    iShell->PutString(hShell, "  echo - echo input characters back to the terminal\n");
    iShell->PutString(hShell, "  crlf - evaluate newline as carriage-return line-feed or line-feed\n");
    iShell->PutString(hShell, "Example commands:\n");
    iShell->PutString(hShell, "  stty               - prints echo and crlf settings\n");
    iShell->PutString(hShell, "  stty echo          - prints echo setting\n");
    iShell->PutString(hShell, "  stty crlf          - prints crlf setting\n");
    iShell->PutString(hShell, "  stty echo [on|off] - turns echo setting on or off\n");
    iShell->PutString(hShell, "  stty crlf [on|off] - turns crlf setting on or off\n");
}

static void
ShellHelp_History(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_HISTORY);
    iShell->PutString(hShell, "  history     - reports command history\n");
    iShell->PutString(hShell, "  history on  - turns command history on\n");
    iShell->PutString(hShell, "  history off - turns command history off\n");
}

static void
ShellHelp_Memstat(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_MEMSTAT);
    iShell->PutString(hShell, "  memstat                     - shows heap usage summary statistics\n");
    iShell->PutString(hShell, "  memstat settag <tag>        - sets tag character for subsequent heap allocations\n");
    iShell->PutString(hShell, "  memstat heapinfo            - shows info for all allocated heap blocks (if enabled)\n");
    iShell->PutString(hShell, "  memstat heapinfo <tag>      - shows info for allocated heap blocks tagged with <tag> (if enabled)\n");
}

static void
ShellHelp_PS(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_PS);
    iShell->PutString(hShell, "  ps                                            - reports list of running threads\n");
    iShell->PutString(hShell, "  ps <id | name>                                - reports info on thread <id | name>\n");
    iShell->PutString(hShell, "  ps <id | name> prio                           - reports priority of thread <id | name>\n");
    iShell->PutString(hShell, "  ps <id | name> prio [0..30]                   - sets priority of thread <id | name>\n");
    iShell->PutString(hShell, "  ps <id | name> terminate                      - instructs thread <id | name> to terminate\n");
    iShell->PutString(hShell, "  ps <id | name> heapinfo                       - reports heap allocated block info for thread <id | name>\n");
    iShell->PutString(hShell, "  ps <id | name> wdog-set <fidelityMS> [noterm] - watchdog guarantees thread <id | name > response-fidelity/non-termination or reboot\n");
    iShell->PutString(hShell, "  ps <id | name> wdog-clear                     - removes watchdog guarantees from thread\n");
}

static void
ShellHelp_Watchdog(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WATCHDOG);
    iShell->PutString(hShell, "  watchdog                    - reports watchdog enablement status\n");
    iShell->PutString(hShell, "  watchdog enable             - enables the watchdog\n");
    iShell->PutString(hShell, "  watchdog disable            - disables the watchdog\n");
    iShell->PutString(hShell, "  watchdog reset              - resets the watchdog\n");
    iShell->PutString(hShell, "  watchdog settimeout seconds - sets timeout for the watchdog\n");
    iShell->PutString(hShell, "  watchdog crash              - Induce a crash\n");
}

static void
ShellHelp_LTHandle(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_LTHANDLE);
    iShell->PutString(hShell, "  lthandle count      - reports count of all valid LTHandles\n");
    iShell->PutString(hShell, "  lthandle count type - reports count of valid LTHandles of specific type, e.g. lthandle count ILTMutex\n");
    iShell->PutString(hShell, "  lthandle count  all - reports count of allocated valid and invalid handle pool slots\n");
    iShell->PutString(hShell, "  lthandle list       - lists all valid LTHandles\n");
    iShell->PutString(hShell, "  lthandle list  type - lists valid LTHandles of a specific type, e.g. lthandle list ILTMutex\n");
    iShell->PutString(hShell, "  lthandle list   all - lists all allocated valid and invalid handle pool slots\n");
}

static void ShellHelp_Log(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell,
        "usage: log <command>\n"
        "Commands:\n"
        "  level [0..N] - Get the current logging level or set to new_level\n"
        "  stats        - Print logging subsystem statistics\n"
    );
}

static void
ShellHelp_WaitFor(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_WAITFOR);
    iShell->PutString(hShell, "  waitfor network-down       - wait until LT's network stack is down\n");
    iShell->PutString(hShell, "  waitfor network-up         - wait until LT's network stack is up\n");
    iShell->PutString(hShell, "  waitfor wifi-connected     - wait until LT's driver is connected to the WiFi\n");
    iShell->PutString(hShell, "  waitfor wifi-disconnected  - wait until LT's driver is not connected to the WiFi\n");
}

static void
ShellHelp_SleepMode(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = lt_gethandleinterface(ILTShell, hShell);
    iShell->PutString(hShell, LTSHELLCOMMAND_USAGE_SLEEPMODE);
    iShell->PutString(hShell, "  sleepmode setdelay <milliseconds> - sets the enter sleep mode idle delay, enabling sleep mode\n");
    iShell->PutString(hShell, "  sleepmode cleardelay              - clears the sleep mode idle delay, disabling sleep mode\n");
    iShell->PutString(hShell, "  sleepmode setproc                 - sets dummy EnterSleepModeProc\n");
    iShell->PutString(hShell, "  sleepmode clearproc               - clears EnterSleepModeProc\n");
    iShell->PutString(hShell, "  sleepmode disallow                - issues a disallowance grant, preventing sleep mode entry\n");
    iShell->PutString(hShell, "  sleepmode abort <on|off>          - turns on and off aborting of sleep in SleepAction event handler\n");
    iShell->PutString(hShell, "  sleepmode reallow <grantNumber>   - revokes a disallowance grant, reallowing sleep mode entry\n");
    iShell->PutString(hShell, "  sleepmode disallowances           - lists all issued disallowance grants\n");
}
