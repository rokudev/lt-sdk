/******************************************************************************
 * lt/source/system/shell/LTShellImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLIMPL_H
#define ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLIMPL_H

#include <lt/core/LTArray.h>
#include <lt/core/LTThread.h>
#include <lt/system/shell/LTShell.h>

typedef struct {
    u32              nEarliestNumber;
    LTArray         *pItems; /* Array of LTString pointers */
} CommandHistory;

typedef struct ShellPriv ShellPriv;

typedef struct ShellPriv {
    LTHandle   handle;
        /* This is the LTShell handle. Some internal functions need to map back
           from ShellPriv to the LTHandle. Holding the handle here facilitates
           that mapping. It is not an indication of ownership. */
    LTThread   hThread;
    LTArray   *pArgv;
    LTString   pBuffer;
        /* When invoking the command, the argv array points into this buffer. */
    CommandHistory *pHistory;
        /* Allocating indicates history is being tracked. NULL value indicates
           history tracking is not available. */
    bool crlf;
    bool echo;
    s32 escapeStart;
        /* Store the offset into pBuffer where the escape sequence is located.
           Don't store a pointer into pBuffer. The buffer can get reallocated,
           if more space is required, invalidating the pointer. Safer to store
           the offset into pBuffer. */
    s32 currentCommand;
        /* Which command, from history, are we displaying?
           If equal to kLTShell_MaxHistory, then user is typing in own command.
           If between zero and num-1, then user is scrolling through history. */
    s32 cursorPos;
        /* On an empty line, when the user enters a command, cursorPos is set
           to -1 to indicate that new text is appended to the buffer.
           The left/right arrow keys reposition the cursor in a command.
           When this happens, cursorPos is a non-negative value indicating
           where the change will occur. The user can then insert additional
           text or modify existing text, depending on the insertion mode. */
    bool insert;
        /* A value of true indicates new text will be inserted, amid existing
           text. A value of false indicates existing text will be replaced. */
    bool prevCharNewline;

    /********  Variables below this line belong to the implementation  ********/

    void *pImpl;

    bool (*IsConsoleShell)(ShellPriv *priv);
    void (*PutStringProc)(ShellPriv *priv, const char *str);
    void (*VPrintProc)(ShellPriv *priv, const char *fmt, lt_va_list args);
    void (*PutCharProc)(ShellPriv *priv, char c);
    void (*EchoCharProc)(ShellPriv *priv, char c);
    void (*MoveCursorRelativeProc)(ShellPriv *priv, s32 incr);
    void (*EraseEntireLineProc)(ShellPriv *priv);
    void (*UpdateToEndOfLineProc)(ShellPriv *priv, const char *str);
    void (*Flush)(ShellPriv *priv);
    void (*Destroy)(void *priv);
} ShellPriv;

/*__________________
_/ Init functions */
void LTShellImpl_LibInit(void);
void LTShellImpl_LibFini(void);

/*____________________________
_/ Shell creation functions */
ShellPriv *LTShellImpl_CreateShellPriv(void);
void LTShellImpl_ShellReady(ShellPriv *pPriv);

/*____________________
_/ Utility functions */
void LTShellImpl_OnCharactersReceived(ShellPriv *pPriv, const char *pChars, u32 nChars);
bool LTShellImpl_IsConsoleShell(LTShell hShell);
void LTShellImpl_TemporarySimulateShellRestart(LTShell hShell);
bool LTShellImpl_IsEchoOn(LTShell hShell);
bool LTShellImpl_IsCRLFOn(LTShell hShell);
bool LTShellImpl_IsHistoryOn(LTShell hShell);
void LTShellImpl_SetEchoOn(LTShell hShell, bool bOn);
void LTShellImpl_SetCRLFOn(LTShell hShell, bool bOn);
void LTShellImpl_SetHistoryOn(LTShell hShell, bool bOn);
void LTShellImpl_Reboot(LTShell hShell);
u32  LTShellImpl_GetShellBanner(char * pBuffToFill, u32 nBuffSize); // returns the number of chars put into *pBuffToFill

typedef bool (LTShellImpl_HistoryEnumProc)(LTShell hShell, u32 nHistoryItemNumber, const LTString pHistoryItem, void *pClientData);
    /* Callback used for registered shell command enumeration.  return true from the callback to continue enumeration
       false to abort enumeration.  */

bool LTShellImpl_EnumerateHistory(LTShell hShell, LTShellImpl_HistoryEnumProc *pEnumProc, void *pClientData);
    /* Enumerates the registered shell commands.  Internal mutex is locked during enumeration.
       returns true if enumeration completes, false if enumeration was aborted by callback proc */

#endif /* #ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLIMPL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Nov-20   caligula    created
 *  18-Dec-20   augustus    CreateNetworkShell takes a socket; added utility functions
 */
