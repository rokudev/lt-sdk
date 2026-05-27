/******************************************************************************
 * lt/source/system/shell/LTConsoleShell.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

#include "LTShellImpl.h"

#define LTSHELL_BOOT_KEYNAME                "ltshell.boot.cmd"
#define LTSHELL_CONSOLETHREAD_STACKSIZE     1832
#define SHELL_SPECIFIC_CLIENTDATA_KEY       "LTConSh"

DEFINE_LTLOG_SECTION("consoleshell");

static ILTThread            *s_pIThread        = NULL;
static LTCore               *s_pCore           = NULL;
static LTSystemSettings     *s_pSystemSettings = NULL;
static LTHandle              s_hConsoleShell   = 0;

static void OnCharactersReceived(char *pChars, u32 nChars, void *pClientData);
static void OnBreakReceived(void *pClientData);

typedef struct {
    LTThread hThread;
} ConsolePrivate;

LT_INLINE void VT100_SaveCursor(void)       { s_pCore->ConsolePutString("\x1b\x37"); }
LT_INLINE void VT100_RestoreCursor(void)    { s_pCore->ConsolePutString("\x1b\x38"); }
LT_INLINE void VT100_EraseEntireLine(void)  { s_pCore->ConsolePutString("\x1b[2K");  }
LT_INLINE void VT100_EraseToEndOfLine(void) { s_pCore->ConsolePutString("\x1b[0K");  }
LT_INLINE void VT100_MoveCursorRelative(s32 numCols) {
    if (numCols)
        s_pCore->ConsolePrint("\x1b[%ld%c", LT_Ps32((numCols < 0) ? -numCols : numCols), (numCols < 0) ? 'D' : 'C');
}

static bool IsConsoleShell(ShellPriv *priv)                           { LT_UNUSED(priv); return true; }
static void PutString(ShellPriv *priv, const char *str)               { LT_UNUSED(priv); s_pCore->ConsolePutString(str); }
static void VPrint(ShellPriv *priv, const char *fmt, lt_va_list args) { LT_UNUSED(priv); s_pCore->ConsolePrintV(fmt, args); }
static void PutChar(ShellPriv *priv, char c)                          { LT_UNUSED(priv); s_pCore->ConsolePutChars(&c, 1); }
static void EchoChar(ShellPriv *priv, char c)                         { LT_UNUSED(priv); s_pCore->ConsoleStompChar(c); }
static void MoveCursorRelative(ShellPriv *priv, s32 incr)             { LT_UNUSED(priv); VT100_MoveCursorRelative(incr); }
static void EraseEntireLine(ShellPriv *priv)                          { LT_UNUSED(priv); VT100_EraseEntireLine(); }
static void Flush(ShellPriv *priv)                                    { LT_UNUSED(priv); }

/* Erase from column to end of line and update from column on with the string: */
void UpdateToEndOfLine(ShellPriv *priv, const char *str) {
    LT_UNUSED(priv);
    VT100_SaveCursor();
    /* Erase to end of line and redraw with new char inserted: */
    VT100_EraseToEndOfLine();
    if (str) {
        s_pCore->ConsolePutString(str);
    }
    VT100_RestoreCursor();
}

static bool ConsoleThreadInitProc(void) {
    ShellPriv *pShell = (ShellPriv *) s_pIThread->GetThreadSpecificClientData(s_pIThread->GetCurrentThread(), SHELL_SPECIFIC_CLIENTDATA_KEY);
    LTShellImpl_ShellReady(pShell);

    // start the network shell - no don't do this, it makes the system active, hindering perf measurement
    //LTNetworkShellImpl_StartNetworkShellSubsystem();

    /* read the boot command from the registry and execute it: */
    if (NULL != (s_pSystemSettings = lt_openlibrary(LTSystemSettings))) {
        LTString pValue = NULL;
        if (s_pSystemSettings->GetStringValue(LTSHELL_BOOT_KEYNAME, &pValue)) {
            u32 len = lt_strlen(pValue);
            if (len) {
                bool bEcho = pShell->echo;
                pShell->echo = false;
                /* print the startup string on the console */
                LTLOG("startup", "Executing \"%s\"", pValue);
                /* Push the command in value into the interpreter with echo off */
                OnCharactersReceived(pValue, len, pShell);
                OnCharactersReceived("\n", 1, pShell);
                pShell->echo = bEcho;
            }
            ltstring_destroy(pValue);
        }
    }
    /* After the boot command is executed, register the character handler with LTCore: */
    s_pCore->SetConsoleCharactersReceivedProc(OnCharactersReceived, OnBreakReceived, pShell);
    return true;
}

static void ConsoleThreadExitProc(void) {
    s_pCore->SetConsoleCharactersReceivedProc(NULL, NULL, NULL);
    if (NULL != s_pSystemSettings) {
        lt_closelibrary(s_pSystemSettings);
        s_pSystemSettings = NULL;
    }
}

static void StartConsoleThread(ShellPriv *pShell) {
    ConsolePrivate *pPriv = (ConsolePrivate *) pShell->pImpl;
    pPriv->hThread = s_pCore->CreateThread("ConsoleShell");
    s_pIThread = (ILTThread *) s_pCore->GetHandleInterface(pPriv->hThread);
    s_pIThread->SetStackSize(pPriv->hThread, LTSHELL_CONSOLETHREAD_STACKSIZE);
    s_pIThread->SetThreadSpecificClientData(pPriv->hThread, SHELL_SPECIFIC_CLIENTDATA_KEY, NULL, pShell);
    s_pIThread->Start(pPriv->hThread, &ConsoleThreadInitProc, &ConsoleThreadExitProc);
}

static void
StopAndDestroyConsoleThread(void) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(s_hConsoleShell);
    LTThread hThread = pShell && pShell->pImpl ? ((ConsolePrivate *)pShell->pImpl)->hThread : 0;
    if (pShell) s_pCore->ReleaseHandlePrivateData(s_hConsoleShell, pShell);
    if (hThread) {
        s_pIThread->Terminate(hThread);
        s_pIThread->WaitUntilFinished(hThread, LTTime_Infinite());
        s_pCore->DestroyHandle(hThread);
    }
}

void LTConsoleShellImpl_StopConsoleThread(void) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(s_hConsoleShell);
    LTThread hThread = pShell && pShell->pImpl ? ((ConsolePrivate *)pShell->pImpl)->hThread : 0;
    if (pShell) s_pCore->ReleaseHandlePrivateData(s_hConsoleShell, pShell);
    if (hThread) {
        s_pIThread->Terminate(hThread);
        s_pIThread->WaitUntilFinished(hThread, LTTime_Infinite());
    }
}

static bool IsReceiveCharOkay(char c) {
    if (c < 32) {
        switch (c) {
        case 8:     /*  BS   */
        case 9:     /* TAB   */
        case 10:    /*  LF   */
        case 13:    /*  CR   */
        case 20:    /* ctrl-T */
        case 27:    /* ESC   */
            break;  /* allow the above */
        default:
            return false; /* filter out all other < 32; this will also filter out 128-255 since char c is signed */
        }
    }
    return true;
}

static void OnBreakReceived(void *pClientData) { LT_UNUSED(pClientData); s_pCore->ConsoleStompString("ctrl-c\n"); }

static void OnCharactersReceived(char *pChars, u32 nChars, void *pClientData) {
    ShellPriv *pShell = (ShellPriv *) pClientData;
    /* Filter out characters that the shell must not receive from the console (according to IsReceiveCharOkay()).
     * Consolidate "good" chars together and pass them along: */
    for (; nChars && !IsReceiveCharOkay(*pChars); --nChars, ++pChars);              /* Find the first good char */
    if (nChars) do {
        u32 nCharsRemaining = nChars;
        char *pPut = pChars;                                                            /* where to begin putting good chars */
        for (; nCharsRemaining && IsReceiveCharOkay(*pPut); --nCharsRemaining, ++pPut); /* find the first char to filter out */
        if (!nCharsRemaining) {                                                         /* reached the end, found nothing to filter */
            LTShellImpl_OnCharactersReceived(pShell, pChars, nChars);
            break;
        }
        char *pLook = pPut + 1;                                                         /* where to begin looking for good chars */
        for (--nChars, --nCharsRemaining; nCharsRemaining; --nCharsRemaining, ++pLook)
            if (IsReceiveCharOkay(*pLook)) *pPut++ = *pLook; else --nChars;             /* gather up good chars */
        if (nChars)
            LTShellImpl_OnCharactersReceived(pShell, pChars, nChars);           /* pass along all good chars */
    } while (0);
};

static void DestroyConsoleShell(void *pPriv) /* called by ShellImpl */ {
    StopAndDestroyConsoleThread();
    lt_free(pPriv);
}

static LTShell CreateConsoleShell(void) {
    s_pCore                          = LT_GetCore();
    ShellPriv *pShell             = LTShellImpl_CreateShellPriv();
    pShell->IsConsoleShell         = IsConsoleShell;
    pShell->PutStringProc          = PutString;
    pShell->VPrintProc             = VPrint;
    pShell->PutCharProc            = PutChar;
    pShell->EchoCharProc           = EchoChar;
    pShell->MoveCursorRelativeProc = MoveCursorRelative;
    pShell->EraseEntireLineProc    = EraseEntireLine;
    pShell->UpdateToEndOfLineProc  = UpdateToEndOfLine;
    pShell->Flush                  = Flush;
    pShell->Destroy                = DestroyConsoleShell;
    pShell->pImpl                  = lt_malloc(sizeof(ConsolePrivate));
    ConsolePrivate *pPriv         = (ConsolePrivate *) pShell->pImpl;
    pPriv->hThread                 = 0;
    StartConsoleThread(pShell);
    return pShell->handle;
}

void LTConsoleShellImpl_LibInit(void) {

    /* Disable the watchdog if no one has the watchdog library open.  This is so the watchdog, which is enabled by the bootloader
       won't reboot us when we are happily using the shell in development mode.  If the watchdog library is already opened before
       we are, we won't mess with the watchdog; if someone comes along later to operate they watchdog, they may.  */
    LTDeviceWatchdog * deviceWatchdog = LT_GetCore()->IsLibraryOpen("LTDeviceWatchdog") ? NULL : lt_openlibrary(LTDeviceWatchdog);
    if (deviceWatchdog) { deviceWatchdog->DisableTimer(); lt_closelibrary(deviceWatchdog); }

    if (! s_hConsoleShell) s_hConsoleShell = CreateConsoleShell();
}

void LTConsoleShellImpl_LibFini(void) {
    if (s_hConsoleShell) {
        s_pCore->Destroy(s_hConsoleShell);
        s_hConsoleShell = 0;
    }
    s_pIThread = NULL;
    s_pCore    = NULL;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jul-21   augustus    leave s_pSystemSettings open after reading boot cmd
 *  08-Sep-21   augustus    turn off echo while feeding startup command; instead log it
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  18-Oct-22   augustus    updated for thread and event api enhancements
*/
