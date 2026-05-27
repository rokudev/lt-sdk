/*******************************************************************************
 * lt-firmware-example/source/shellltevent/ShellLTEvent.c
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
#include <lt/utility/byteops/LTUtilityByteOps.h>

/*_________________________
_/ #forward declarations */
static int  ShellCommandLTEvent(LTShell hShell, int argc, const char ** argv);
static void ShellHelpLTEvent(LTShell hShell, int argc, const char ** argv);

/*____________________
_/ static constants */
static const LTSystemShell_CommandDesc s_ltThreadShellCommands[] = {
    { "ltevent", ShellCommandLTEvent, "LTEvent example start/stop", ShellHelpLTEvent }
};

/*____________________
_/ static variables */
static LTSystemShell       * s_pLTSystemShell    = NULL;
static LTUtilityByteOps    * s_pUtilityByteOps    = NULL;
static ILTEvent            * iLTEvent = NULL;

/*__________________________
_/ library initialization */
static bool ShellLTEventImpl_LibInit(void) {
    if (NULL != (s_pUtilityByteOps = lt_openlibrary(LTUtilityByteOps))) {
        s_pLTSystemShell = lt_openlibrary(LTSystemShell);
        if (s_pLTSystemShell) {
            s_pLTSystemShell->RegisterCommands(s_ltThreadShellCommands, sizeof s_ltThreadShellCommands / sizeof s_ltThreadShellCommands[0]);
            iLTEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
            return true;
        }
    }

    return false;
}

static void ShellLTEventImpl_LibFini(void) {
    if (s_pLTSystemShell) {
        s_pLTSystemShell->UnregisterCommands(s_ltThreadShellCommands);
        lt_closelibrary(s_pLTSystemShell);
        s_pLTSystemShell = NULL;
    }
    lt_closelibrary(s_pUtilityByteOps);
}

/*_______________________________________
_/ ShellLTEvent library binding */
typedef_LTLIBRARY_ROOT_INTERFACE(ShellLTEvent, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(ShellLTEvent)    LTLIBRARY_DEFINITION;

/*___________________
_/ ltthread help proc */
static void ShellHelpLTEvent(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);

    iShell->Print(hShell, "usage: ltevent\n");
}

/* ________________________________ */
/* ________________________________ */
/* ________________________________ */
/* ________________________________ */
/* LTEvent example                 */

#define KEY_ROTATION_TIME       LTTime_Seconds(5)

/* type of new key event procedure */
typedef void (NewKeyEventProc)(LTTime startTimeValid, LTTime stopTimeValid, const char *newKey, void *pClientData);

/* threads, event, event args descriptor */
static LTOThread                *s_pThreadEventDispatch   = NULL;
static LTOThread                *s_pThreadEventReceiver1  = NULL;
static LTOThread                *s_pThreadEventReceiver2  = NULL;
static LTEvent                  s_hNewKeyEvent    = 0;
static const LTArgsDescriptor   s_newKeyEventArgs = { 3,
                                                      {
                                                        /* LTTime */ kLTArgType_s64,
                                                        /* LTTime */ kLTArgType_s64,
                                                                     kLTArgType_charstar
                                                      }
                                                    };

/* current key information; owned and accessed by dispatch thread only */
static LTTime s_startTimeValid;
static LTTime s_stopTimeValid;
static char s_currentKey[33];   /* 16 byte hex string (32 bytes) + null term */

/* helper functions (by convention) to make event registration type safe */
static void OnNewKeyEvent(NewKeyEventProc *pNewKeyEventProc, void *pClientData) {
    iLTEvent->RegisterForEvent(
        s_hNewKeyEvent,     /* event */
        pNewKeyEventProc,   /* event proc */
        NULL,               /* clientData release proc */
        pClientData,
        true);              /* bNotifyEventStateImmediately */
}

static void NoNewKeyEvent(NewKeyEventProc *pNewKeyEventProc) {
    iLTEvent->UnregisterFromEvent(s_hNewKeyEvent, pNewKeyEventProc);
}

/* receiver thread event proc */
static void MyNewKeyEventProc(LTTime startTimeValid, LTTime stopTimeValid, const char *newKey, void *pClientData) {
    LT_UNUSED(pClientData);

    LTCore *pCore = LT_GetCore();
    LTOThread *thread = pCore->GetCurrentThreadObject();

    char threadName[kLTThread_MaxNameBuff];
    char startTimeString[24];
    char stopTimeString[24];

    thread->API->GetName(thread, threadName);
    pCore->FormatCanonicalTimeString(startTimeValid, startTimeString, sizeof(startTimeString), false);
    pCore->FormatCanonicalTimeString(stopTimeValid, stopTimeString, sizeof(stopTimeString), false);
    lt_consoleprint("NewKeyEventProc(%s) - key %s valid from %ss to %ss\n", threadName, newKey, startTimeString, stopTimeString);
}

/* receiver thread init and exit procs */
static bool ReceiverThreadInitProc(void) {
    OnNewKeyEvent(MyNewKeyEventProc, NULL);
    return true;
}

static void ReceiverThreadExitProc(void) {
    NoNewKeyEvent(MyNewKeyEventProc);
}

/* dispatch thread timer proc */
static void DispatchThreadTimerProc(void *pClientData) {

    LT_UNUSED(pClientData);

    /* generate the next key in the key rotation */
    s_startTimeValid = LT_GetCore()->GetKernelTime();
    s_stopTimeValid = LTTime_Add(s_startTimeValid, KEY_ROTATION_TIME);
    s_pUtilityByteOps->GenRandomBytesAsHexString(16, s_currentKey, sizeof(s_currentKey));
     /* note uses hardware entropy if LTDriverCryptoEntropy object available, otherwise software entropy.
      *
      * From LTUtilityByteOps.h:
      *   %GenRandomBytesAsHexString() uses a very fast, very small algorithm developed in 2014 by
      *   Melissa O'Neil of Harvey Mudd college called PCG-XSH-RR.  Though technically not
      *   cryptographically secure, it is statistically non-predictable and generates
      *   2^64 different streams of uniformly distributed non-recurring bit patterns in
      *   a 2^64 number space.
      *
      *   For more info, see http://www.pcg-random.org/pdf/hmc-cs-2014-0905.pdf
      *
      * Note: for true cryptographically secure ops, use LTSystemCrypto, not LTUtilityByteOps
      */

    /* notify the new key event */
    iLTEvent->NotifyEvent(s_hNewKeyEvent, LTTime_GetNanoseconds(s_startTimeValid), LTTime_GetNanoseconds(s_stopTimeValid), s_currentKey);
}

/* dispatch thread event dispatch helper functions */
static void DispatchNewKeyEventProc(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    /* runs in the context of each receiver thread for type-safe dispatch to receiver thread */
    LT_UNUSED(hEvent);
    ((NewKeyEventProc *)pEventProc)(
        LTTime_Nanoseconds(LTArgs_s64At(0, pEventArgs)),
        LTTime_Nanoseconds(LTArgs_s64At(1, pEventArgs)),
        LTArgs_charstarAt(2, pEventArgs),
        pEventProcClientData);
}

static void DispatchNewKeyEventImmediateProc(LTEvent hEvent, void * pNotifyImmediateEventStateClientData, void * pEventProc, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LT_UNUSED(pNotifyImmediateEventStateClientData);
    ((NewKeyEventProc *)pEventProc)(s_startTimeValid, s_stopTimeValid, s_currentKey, pEventProcClientData);
}

static void DispatchNewKeyEventCompleteProc(LTEvent hEvent, LTArgs * pEventArgs) {
    LT_UNUSED(hEvent);
    LT_UNUSED(pEventArgs);

    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();

    char threadName[kLTThread_MaxNameBuff];
    thread->API->GetName(thread, threadName);

    lt_consoleprint("EventCompleteProc(%s)\n", threadName);
}

static bool DispatchThreadInitProc(void) {
    LTCore *pCore = LT_GetCore();
    LTOThread *thread = pCore->GetCurrentThreadObject();

    /* create the event */
    s_hNewKeyEvent = pCore->CreateEvent(
        &s_newKeyEventArgs,              /* event args descriptor */
        DispatchNewKeyEventProc,                /* dispatch helper proc */
        DispatchNewKeyEventCompleteProc,        /* dispatch complete proc */
        DispatchNewKeyEventImmediateProc,       /* immediate (on register) helper proc */
        NULL);                                  /* immediate helper proc client data */

    /* manually run the timer proc to initialize the first key */
    DispatchThreadTimerProc(NULL);

    /* set the timer for the key rotation */
    thread->API->SetTimer(thread, KEY_ROTATION_TIME, DispatchThreadTimerProc, NULL, NULL);

    return true;
}

static void DispatchThreadExitProc(void) {
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    thread->API->KillTimer(thread, DispatchThreadTimerProc, NULL);
    lt_destroyhandle(s_hNewKeyEvent);
}

/*____________________________
_/ ltthread command proc */
static int ShellCommandLTEvent(LTShell hShell, int argc, const char ** argv) {
    LT_UNUSED(hShell); LT_UNUSED(argc); LT_UNUSED(argv);

    if (NULL == s_pThreadEventDispatch) {
        s_pThreadEventDispatch  = lt_createobject(LTOThread);
        s_pThreadEventReceiver1 = lt_createobject(LTOThread);
        s_pThreadEventReceiver2 = lt_createobject(LTOThread);

        s_pThreadEventDispatch->API->SetStackSize(s_pThreadEventDispatch,   1024);
        s_pThreadEventReceiver1->API->SetStackSize(s_pThreadEventReceiver1, 1024);
        s_pThreadEventReceiver2->API->SetStackSize(s_pThreadEventReceiver2, 1024);

        s_pThreadEventDispatch->API->StartSynchronous(s_pThreadEventDispatch,   "Dispatch",  DispatchThreadInitProc, DispatchThreadExitProc);
        s_pThreadEventReceiver1->API->Start(s_pThreadEventReceiver1, "Receiver1", ReceiverThreadInitProc, ReceiverThreadExitProc);
        s_pThreadEventReceiver2->API->Start(s_pThreadEventReceiver2, "Receiver2", ReceiverThreadInitProc, ReceiverThreadExitProc);
    }
    else {
        lt_destroyobject(s_pThreadEventReceiver2);
        lt_destroyobject(s_pThreadEventReceiver1);
        lt_destroyobject(s_pThreadEventDispatch);
        s_pThreadEventDispatch  = NULL;
        s_pThreadEventReceiver1 = NULL;
        s_pThreadEventReceiver2 = NULL;
    }

    return 0;
}



/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  01-May-26   augustus    created
 */
