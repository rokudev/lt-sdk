/*******************************************************************************
 * lt-firmware-example/source/shellltthread/ShellLTThread.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * LT Library that registers "ltthread" shell command
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/system/shell/LTSystemShell.h>

/*_________________________
_/ #forward declarations */
static int  ShellCommandLTThread(LTShell hShell, int argc, const char ** argv);
static int  ShellCommandLTQueue(LTShell hShell, int argc, const char ** argv);
static void ShellHelpLTThread(LTShell hShell, int argc, const char ** argv);

/*____________________
_/ static constants */
static const LTSystemShell_CommandDesc s_ltThreadShellCommands[] = {
    { "ltthread", ShellCommandLTThread, "LTThread create/destroy", ShellHelpLTThread },
    { "ltqueue", ShellCommandLTQueue, "LTThread queue", ShellHelpLTThread },
};

/*____________________
_/ static variables */
static LTSystemShell    * s_pLTSystemShell    = NULL;

/*__________________________
_/ library initialization */
static bool ShellLTThreadImpl_LibInit(void) {
    s_pLTSystemShell = lt_openlibrary(LTSystemShell);
    if (s_pLTSystemShell) {
        s_pLTSystemShell->RegisterCommands(s_ltThreadShellCommands, sizeof s_ltThreadShellCommands / sizeof s_ltThreadShellCommands[0]);
        return true;
    }
    return false;
}

static void ShellLTThreadImpl_LibFini(void) {
    if (s_pLTSystemShell) {
        s_pLTSystemShell->UnregisterCommands(s_ltThreadShellCommands);
        lt_closelibrary(s_pLTSystemShell);
        s_pLTSystemShell = NULL;
    }
}

/*_______________________________________
_/ ShellLTThread library binding */
typedef_LTLIBRARY_ROOT_INTERFACE(ShellLTThread, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(ShellLTThread)    LTLIBRARY_DEFINITION;

/*___________________
_/ ltthread help proc */
static void ShellHelpLTThread(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);

    iShell->Print(hShell, "usage: ltthread\n");
}

/* ________________________________ */
/* ________________________________ */
/* ________________________________ */
/* ________________________________ */
/* LTThread example                 */

#define THREAD_SPECIFIC_CLIENT_DATA_KEY "num"

static LTOThread * s_pThread1 = NULL;
static LTOThread * s_pThread2 = NULL;

static void MyThreadTimerProc(void * pClientData) {
    LT_UNUSED(pClientData);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    u32 threadNumber = (u32)(LT_SIZE)thread->API->GetThreadSpecificClientData(thread, THREAD_SPECIFIC_CLIENT_DATA_KEY);
    lt_consoleprint("MyThreadTimerProc - threadNumber(%lu)\n", LT_Pu32(threadNumber));
}

static void MyThreadExitProc(void) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();

    u32 threadNumber = (u32)(LT_SIZE)thread->API->GetThreadSpecificClientData(thread, THREAD_SPECIFIC_CLIENT_DATA_KEY);
    lt_consoleprint("MyThreadExitProc - threadNumber(%lu)\n", LT_Pu32(threadNumber));

    switch (threadNumber) {
        case 1:
            lt_destroyobject(s_pThread2);
            s_pThread2 = NULL;
            break;
        case 2:
            thread->API->KillTimer(thread, MyThreadTimerProc, NULL);
            break;
        default:
            lt_consoleprint("MyThreadExitProc - unknown thread\n");
            break;
    }
    lt_consoleprint("MyThreadExitProc Complete - threadNumber(%lu)\n", LT_Pu32(threadNumber));
}

static bool MyThreadInitProc(void) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();

    u32 threadNumber = (u32)(LT_SIZE)thread->API->GetThreadSpecificClientData(thread, THREAD_SPECIFIC_CLIENT_DATA_KEY);
    lt_consoleprint("MyThreadInitProc - threadNumber(%lu)\n", LT_Pu32(threadNumber));

    switch (threadNumber) {
        case 1:
            s_pThread2 = lt_createobject(LTOThread);
            s_pThread2->API->SetThreadSpecificClientData(s_pThread2, THREAD_SPECIFIC_CLIENT_DATA_KEY, NULL, (void *)2);
            s_pThread2->API->Start(s_pThread2, "MyThread2", MyThreadInitProc, MyThreadExitProc);
            break;
        case 2:
            thread->API->SetTimer(thread, LTTime_Seconds(5), MyThreadTimerProc, NULL, NULL);
            break;
        default:
            lt_consoleprint("MyThreadInitProc - unknown thread\n");
            break;
    }

    lt_consoleprint("MyThreadInitProc Complete - threadNumber(%lu)\n", LT_Pu32(threadNumber));
    return true;
}

/*____________________________
_/ ltthread command proc */
static int ShellCommandLTThread(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(hShell); LT_UNUSED(argc); LT_UNUSED(argv);

    if (NULL == s_pThread1) {
        s_pThread1 = lt_createobject(LTOThread);
        s_pThread1->API->SetThreadSpecificClientData(s_pThread1, THREAD_SPECIFIC_CLIENT_DATA_KEY, NULL, (void *)1);
        s_pThread1->API->Start(s_pThread1, "MyThread1", MyThreadInitProc, MyThreadExitProc);
    }
    else {
        lt_destroyobject(s_pThread1);
        s_pThread1 = NULL;
        LT_ASSERT(s_pThread2 == NULL);
    }

    return 0;
}

static void MyTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    u32 threadNumber = (u32)(LT_SIZE)thread->API->GetThreadSpecificClientData(thread, THREAD_SPECIFIC_CLIENT_DATA_KEY);
    lt_consoleprint("MyTaskProc - threadNumber(%lu)\n", LT_Pu32(threadNumber));
}

static void MyClientDataReleaseProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(pClientData);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    u32 threadNumber = (u32)(LT_SIZE)thread->API->GetThreadSpecificClientData(thread, THREAD_SPECIFIC_CLIENT_DATA_KEY);
    lt_consoleprint("MyClientDataReleaseProc - threadNumber(%lu), releaseReason(%lu)\n", LT_Pu32(threadNumber), LT_Pu32((u32)releaseReason));
}

static int ShellCommandLTQueue(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(hShell);
    LTOThread *thread = ((argc == 2) && (0 == lt_strcmp(argv[1], "2"))) ? s_pThread2 : s_pThread1;
    if (thread) thread->API->QueueTaskProc(thread, MyTaskProc, MyClientDataReleaseProc, NULL);
    return 0;
}



/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Apr-26   augustus    created
 */
