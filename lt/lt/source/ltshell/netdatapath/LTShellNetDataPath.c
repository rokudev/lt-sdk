/*******************************************************************************
 *
 * LTShellNetDataPath: NetDataPath Shell
 * ----------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/net/core/LTNetDataPath.h>
#include <lt/net/core/LTNetBuffer.h>
#include <lt/core/LTMonitor.h>

static struct Statics {
    LTCore                  *core;
    LTNetDataPath           *netDataPath;
    LTNetBuffer             *netBuffer;
    LTMonitor               *monitor;
    ILTThread               *iThread;
    LTLibrary               *SelfLib;
} S;

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static LTSystemShell *SHL_Library;

/** Shell Variables ***********************************************************/
static ILTShell      *SHL_iShell;
static LTThread      SHL_Thread;
/*******************************************************************************/

/*******************************************************************************
 *                  Forward Declarations
 ******************************************************************************/
 static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);

/******************************************************************************
 *                  Utility Functions
 ******************************************************************************/
static int HasArg(int argc, const char *argv[], const char *pattern) {
    // argv[0] is command and is skipped
    for (int n = 1; n < argc; n++) {
        if (lt_strcmp(argv[n], pattern) == 0) return n;
    }
    return 0;
}

/*******************************************************************************
 * NetDataPath Commands
 *******************************************************************************/
static int SHL_Log(LTShell hShell, int argc, const char *argv[]) {
    if (!S.netDataPath) {
        SHL_iShell->Print(hShell, "NetDataPath library not available\n");
        return 1;
    }
    if (HasArg(argc, argv, "start")) {
        SHL_iShell->Print(hShell, "Start NetDataPath Logging\n");
        S.netDataPath->EnableLog(true);
    } else if (HasArg(argc, argv, "stop")) {
        SHL_iShell->Print(hShell, "Stop NetDataPath Logging\n");
        S.netDataPath->EnableLog(false);
    } else {
        SHL_iShell->Print(hShell, "Invalid argument, please use start|stop\n");
        return 1;
    }
    
    return 0;
}

static int SHL_Stats(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!S.netDataPath) {
        SHL_iShell->Print(hShell, "NetDataPath library not available\n");
        return 1;
    }

    LTNetDataPathStats qstats;
    if (S.netDataPath->GetStatistics(&qstats)) {
        SHL_iShell->Print(hShell, "NetDataPath QueueStats:\n=====================\n");
        for (int i = 0 ; i < kLTNetDataPathQueueType_Max; i++) {
            SHL_iShell->Print(hShell, "Queue %s(%ld):       Current Size=%ld\n", LTNetDataPathQueueTypeToString(i),LT_Pu32(i), LT_Pu32(qstats.stats[i].size));
        }
        SHL_iShell->Print(hShell, "\n\nNetDataPath MemStats:\n=====================\n");
        for (u32 i = 0; i < qstats.mem_pool_stats.num_pool; i++) {
            SHL_iShell->Print(hShell, "%s:  block_count=%ld, block_size=%ld, free_count=%ld, num_alloc=%lld, num_free=%lld, alloc_rate_max=%ld(%ld msec), zero_buffers=%ld\n", qstats.mem_pool_stats.pool_stats[i].name,
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].block_count), LT_Pu32(qstats.mem_pool_stats.pool_stats[i].block_size),
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].free_count),
                            LT_Pu64(qstats.mem_pool_stats.pool_stats[i].num_alloc), LT_Pu64(qstats.mem_pool_stats.pool_stats[i].num_free),
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].alloc_rate_max), LT_Pu32(qstats.mem_pool_stats.pool_stats[i].window_msec),
                            LT_Pu32(qstats.mem_pool_stats.pool_stats[i].zero_buffers));
        }
    } else {
        SHL_iShell->Print(hShell, "Failed to get stats\n");
        return 1;
    }
    return 0;
}

static int SHL_Tickle(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    if (!S.netDataPath) {
        SHL_iShell->Print(hShell, "NetDataPath library not available\n");
        return 1;
    }
    LTNetDataPathQueueType queue_type = kLTNetDataPathQueueType_Max;
    queue_type = (LTNetDataPathQueueType)lt_strtou32(argv[1], NULL, 10);
    if (queue_type<kLTNetDataPathQueueType_Max) {
        SHL_iShell->Print(hShell, "Tickling Queue %s\n", LTNetDataPathQueueTypeToString(queue_type));
        S.netDataPath->ForceNotify(queue_type);
    } else {
        SHL_iShell->Print(hShell, "Invalid Queue Type\n");
        return 1;
    }
    SHL_iShell->Print(hShell, "Tickle Done\n");
    return 0;
}

static void poke_callback(void *context){
    LTShell hShell = VOIDPTR_TO_LTHANDLE(context);
    SHL_iShell->Print(hShell, "Poke Callback success\n");
}

static int SHL_Poke(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    if (!S.netDataPath) {
        SHL_iShell->Print(hShell, "NetDataPath library not available\n");
        return 1;
    }
    LTHandle tid = LT_Pu32(lt_strtou32(argv[1], NULL, 10));
    S.iThread->QueueTaskProc(tid, poke_callback, NULL, LTHANDLE_TO_VOIDPTR(hShell));
    SHL_iShell->Print(hShell, "Pke Done\n");
    return 0;
}

// clang-format off
static const LTSystemShell_CommandDesc Netdp_Commands[] = {
    { "help",               SHL_Help,                   "list of netdp commands",                            NULL },
    { "log",                SHL_Log,                    "Start Netdp logging\n"
                                                        "\t\tstop - stop\n"
                                                       "\t\tstart - start",                               NULL },
    { "stats",              SHL_Stats,                  "Get NetDp Stats",                            NULL },
    { "tickle",             SHL_Tickle,                 "Tickle NetDp Queue",                            NULL },
    { "poke",               SHL_Poke,                  "Poke NetDp Queue",                            NULL },
    { NULL,                 NULL,                       NULL,                                              NULL }
};
//clang-format on
static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);
static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    for (int n = 0; Netdp_Commands[n].pCommand; n++) {
        SHL_iShell->Print(hShell, "  %-10s - %s\n", Netdp_Commands[n].pCommand,
            Netdp_Commands[n].pDescription);
    }
    return 0;
}

static int SHL_NetDP(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; Netdp_Commands[n].pCommand; n++) {
            if (lt_strcmp(Netdp_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return Netdp_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    SHL_iShell->Print(hShell, "usage: netdp <command> [args]\nCommands:\n");
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "netdp", SHL_NetDP, "Net Datapath commands, type \"netdp\" for a list", LTShell_Help },
};

static bool SHL_InitThread(void) {
    return true;
}

static void SHL_ExitThread(void) {
    return;
}

static void SHL_Quit(void) {
    if (SHL_Library) {
        SHL_Library->UnregisterCommands(SHL_Commands);
        lt_closelibrary(SHL_Library);
        S.iThread->Destroy(SHL_Thread); // zero ok
    }
}

static bool SHL_Init(void) {
    SHL_Library = lt_openlibrary(LTSystemShell);
    if (!SHL_Library) return false;

    SHL_Thread = S.core->CreateThread("ShellNetDataPath");
    if (!SHL_Thread) return false;
    S.iThread->SetStackSize(SHL_Thread, 1536);
    S.iThread->Start(SHL_Thread, SHL_InitThread, SHL_ExitThread);

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));

    return true;
}

/*******************************************************************************
 * NetDataPath Settings
 ******************************************************************************/

static LTSystemSettings *SET_Library;

static bool SET_Init(void) {
    /* open LTSystemSettings, if non-NULL we can proceed to read settings right away */
    if (!(SET_Library = lt_openlibrary(LTSystemSettings))) return false;
    return true;
}

static void SET_Quit(void) {
    lt_closelibrary(SET_Library); // null ok
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellNetDataPathImpl_LibFini(void) {
    SET_Quit();
    SHL_Quit();
    lt_closelibrary(S.netDataPath);       // null ok
}

static bool LTShellNetDataPathImpl_LibInit(void) {
    S.core = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, S.core);
    S.netDataPath   = lt_openlibrary(LTNetDataPath);
    if (S.netDataPath && SHL_Init() && SET_Init()) return true;
    LTShellNetDataPathImpl_LibFini();
    return false;
}


static int LTShellNetDataPath_Run(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    if (!S.SelfLib) S.SelfLib = LT_GetCore()->OpenLibrary("LTShellNetDataPath");
    return 0;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellNetDataPath, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellNetDataPath, LTShellNetDataPath_Run) LTLIBRARY_DEFINITION;


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Nov-24   galba       created
*/