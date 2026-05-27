/******************************************************************************
 * lt/source/system/shell/LTShellImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTShellImpl.h"
#include "LTSystemShellImpl.h"
#include "LTConsoleShellImpl.h"
#include <lt/core/LTCore.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/system/shell/LTShellBanner.h>

#define LTSHELL_PROMPT          			"LT> "
#define LTSHELL_ENABLE_DISALLOWANCE_GRANT 	1

/**
 * Resources:
 *   https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 *    Section on "PC-Style Function Keys" talks about keys and modifiers.
 *    This leads to the definition of ctrl-alt-del as "CSI 3 ; 7 ~".
 */

enum {
    kLTShell_DefaultCRLFSetting     = true,
    kLTShell_DefaultEchoSetting     = true,
    kLTShell_MaxHistory             = 10,
    kLTShell_KeyStrokeIdleEnable_ms = 10000,
};

enum {
    kAsciiBackspace      = 0x08,
    kAsciiTab            = 0x09,
    kAsciiNewLine        = 0x0a,
    kAsciiCarriageReturn = 0x0d,
    kAsciiCtrlU          = 0x15,
    kAsciiCtrlT          = 0x14,
    kAsciiCtrlW          = 0x17,
    kAsciiEscape         = 0x1b,
    kAsciiDelete         = 0x7F,
};

typedef enum {
    kMetaNone,              /* No metacharacter found */
    kMetaCarriageReturn,
    kMetaNewLine,
    kMetaUp,
    kMetaDown,
    kMetaLeft,
    kMetaRight,
    kMetaBackspace,
    kMetaKillLine,
    kMetaKillWord,
    kMetaInsert,
    kMetaDelete,
    kMetaCtrlAltDel,
    kMetaCtrlT,
    kMetaTab,
    kMetaUnknown            /* Metacharacter start found and not yet identified
                               or unknown metacharacter encountered. */
} MetaChar;
    /* MetaChars represent actionable events from the user. They are
       either a single mapped ASCII character or a sequence of ASCII
       characters.  Do not add ASCII characters here. See the kAscii... enums above. */

static ILTThread                *s_pIThread              = NULL;
static LTCore                   *s_pCore                 = NULL;

#if LTSHELL_ENABLE_DISALLOWANCE_GRANT
static u32                       s_nDisallowanceGrant    = 0;
#endif

static void LTShellImpl_Print(LTShell hShell, const char *pFormat, ...) {
    lt_va_list args;
    lt_va_start(args, pFormat);
    ShellPriv * pShell = s_pCore->ReserveHandlePrivateData(hShell);
    pShell->VPrintProc(pShell, pFormat, args);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    lt_va_end(args);
}

static void LTShellImpl_VPrint(LTShell hShell, const char *pFormat, lt_va_list args) {
    ShellPriv * pShell = s_pCore->ReserveHandlePrivateData(hShell);
    pShell->VPrintProc(pShell, pFormat, args);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
}

static void LTShellImpl_PutChar(LTShell hShell, char ch) {
    ShellPriv * pShell = s_pCore->ReserveHandlePrivateData(hShell);
    pShell->PutCharProc(pShell, ch);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
}

static void LTShellImpl_PutString(LTShell hShell, const char *pString) {
    ShellPriv * pShell = s_pCore->ReserveHandlePrivateData(hShell);
    pShell->PutStringProc(pShell, pString);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
}

static void LTShellImpl_PrintShellBanner(LTShell hShell) {
    LTShellImpl_PutString(hShell, ROKU_LT_BANNER);
}

static void LTShellImpl_PrintShellWelcome(LTShell hShell) {
    ILTShell *iShell = (ILTShell *)s_pCore->GetNameCheckedHandleInterface(hShell, "ILTShell");
    if (iShell) {
        LTShellImpl_PutString(hShell, WELCOME_STRING_1);
        LTShellImpl_PutString(hShell, LT_GetCore()->GetLibraryBuildVersion());
        LTShellImpl_PutString(hShell, WELCOME_STRING_2);
    }
}

static void LTShellImpl_PrintPrompt(LTHandle hShell) {
    ShellPriv * pShell = s_pCore->ReserveHandlePrivateData(hShell);
    pShell->PutStringProc(pShell, LTSHELL_PROMPT);
    pShell->Flush(pShell);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
}

static void LTShellImpl_CopyBannerString(const char * pBannerString, u32 nBannerStringLen, char **ppBuffToSet, u32 *pBuffLen) {
    u32 nChars = LT_MIN(*pBuffLen, nBannerStringLen);
    if (nChars) {
        lt_memcpy(*ppBuffToSet, pBannerString, nChars);
        *ppBuffToSet += nChars;
        *pBuffLen -= nChars;
    }
}

u32 LTShellImpl_GetShellBanner(char * pBuffToFill, u32 nBuffSize) {
    const char * pBuildVersion = LT_GetCore()->GetLibraryBuildVersion();
    if (pBuffToFill && nBuffSize) {
        u32 nSize = nBuffSize; char * pBuff = pBuffToFill;
        LTShellImpl_CopyBannerString(ROKU_LT_BANNER,   sizeof(ROKU_LT_BANNER)   -1, &pBuff, &nSize);
        LTShellImpl_CopyBannerString(WELCOME_STRING_1, sizeof(WELCOME_STRING_1) -1, &pBuff, &nSize);
        LTShellImpl_CopyBannerString(pBuildVersion,    lt_strlen(pBuildVersion)   , &pBuff, &nSize);
        LTShellImpl_CopyBannerString(WELCOME_STRING_2, sizeof(WELCOME_STRING_2) -1, &pBuff, &nSize);
        LTShellImpl_CopyBannerString(LTSHELL_PROMPT,   sizeof(LTSHELL_PROMPT)   -1, &pBuff, &nSize);
        return nBuffSize - nSize;
    }
    return 0;
}

static bool StageCommand(CommandHistory *ptr, const char * pCommand) {
    if (!ptr) return false;
    /* tracking history... Is this command same as previous one? */
    LTArray *pItems = ptr->pItems;
    LTString prev = NULL;
    if (pItems->API->GetCount(pItems)) {
        prev = pItems->API->Get(pItems, pItems->API->GetCount(pItems) - 1, NULL);
    }
    /* If prev is null or, pCommand is different from prev,
       add pCommand to history. */
    if (prev == NULL || lt_strcmp(prev, pCommand) != 0) {
        /* Put a copy of the command into history: */
        LTString pTemp = ltstring_create(pCommand);
        pItems->API->Append(pItems, pTemp);
        return true;
    }
    return false;
}

static void CommitStaged(CommandHistory *ptr, bool keep) {
    if (!ptr) return;
    LTString pTemp = NULL;
    LTArray *pItems = ptr->pItems;
    if (keep) {
        if (pItems->API->GetCount(ptr->pItems) >= kLTShell_MaxHistory) {
            /* Remove earliest item and destroy it: */
            pTemp = pItems->API->Get(pItems, 0, NULL);
            pItems->API->Remove(pItems, 0);
            ltstring_destroy(pTemp);
            /* Push earliest ahead by one: */
            ptr->nEarliestNumber++;
        }
    } else {
        pTemp = pItems->API->Get(pItems, pItems->API->GetCount(pItems) - 1, NULL);
        pItems->API->Remove(pItems, pItems->API->GetCount(pItems) - 1);
        ltstring_destroy(pTemp);
    }
}

static void TokenizeCommand(LTArray *pArgv, LTString pCommand) {
    char *pArg = pCommand;
    while (1) {
        /* advance over leading whitespace */
        while (*pArg == ' ' || *pArg == '\t') pArg++;
        if (0 == *pArg) break;

        /* add the arg, and advance to end */
        if (*pArg == '"') {
            /* quoted arg - start past quote, end on 0 or quote */
            pArg++;
            pArgv->API->Append(pArgv, pArg); /* don't advance yet; in case of "" we want empty string */
            while (*pArg && *pArg != '"') pArg++;
        }
        else {
            /* non-quoted arg, start here, end on 0 or whitespace */
            pArgv->API->Append(pArgv, pArg);
            pArg++;
            while (*pArg && *pArg != ' ' && *pArg != '\t') pArg++;
        }
        /* (null-term the arg and advance) or stop */
        if (*pArg) *pArg++ = 0; else break;
    }
}

LT_INLINE bool InEscapeSequence(ShellPriv *pShell) { return pShell->escapeStart != -1; }

static void StartEscapeSequence(ShellPriv *pShell) {
    if (InEscapeSequence(pShell)) {
        /* New escape sequence started before finishing the previous one.
           Discard previous sequence and start accumulating the new one: */
        pShell->pBuffer[pShell->escapeStart] = '\0';
    }
    pShell->escapeStart = lt_strlen(pShell->pBuffer);
}

static void EndEscapeSequence(ShellPriv *pShell) {
    pShell->pBuffer[pShell->escapeStart] = '\0';
    pShell->escapeStart = -1;
}

static MetaChar EvaluateCSISequence(ShellPriv *pShell, char c, bool firstChar) {
    LT_ASSERT(InEscapeSequence(pShell));
    MetaChar mc = kMetaUnknown;
    ltstring_appendchar(&pShell->pBuffer, c);
    if (!firstChar && 0x40 <= (u8) c && (u8) c <= 0x7F) {
        const char *pEscapeString = pShell->pBuffer + pShell->escapeStart;
        if (lt_strcmp(pEscapeString, "[A") == 0) {
            mc = kMetaUp;
        } else if (lt_strcmp(pEscapeString, "[B") == 0) {
            mc = kMetaDown;
        } else if (lt_strcmp(pEscapeString, "[C") == 0) {
            mc = kMetaRight;
        } else if (lt_strcmp(pEscapeString, "[D") == 0) {
            mc = kMetaLeft;
        } else if (lt_strcmp(pEscapeString, "[2~") == 0) {
            mc = kMetaInsert;
        } else if (lt_strcmp(pEscapeString, "[3~") == 0) {
            mc = kMetaDelete;
        } else if (lt_strcmp(pEscapeString, "[3;7~") == 0) {
            /* delete key, with ctrl-alt modifier */
            mc = kMetaCtrlAltDel;
        }
        EndEscapeSequence(pShell);
    }
    return mc;
}

static MetaChar EvaluateMetaChar(ShellPriv *pShell, char c) {
    if (c == kAsciiEscape) {
        StartEscapeSequence(pShell);
        return kMetaUnknown;
    }
    if (!InEscapeSequence(pShell)) {
        switch (c) {
        case kAsciiBackspace:
        case kAsciiDelete:         return kMetaBackspace;
        case kAsciiNewLine:        return kMetaNewLine;
        case kAsciiCarriageReturn: return kMetaCarriageReturn;
        case kAsciiCtrlU:          return kMetaKillLine;
        case kAsciiCtrlW:          return kMetaKillWord;
        case kAsciiCtrlT:          return kMetaCtrlT;
        case kAsciiTab:            return kMetaTab;
        default:                   return kMetaNone;
        }
    }
    /* Escape sequences only at this point: */
    LT_ASSERT(InEscapeSequence(pShell));
    u32 length = lt_strlen(pShell->pBuffer);
    const bool firstChar = pShell->escapeStart == (s32) length;
    LT_ASSERT(pShell->escapeStart <= (s32) length);
    if (firstChar) {
        switch (c) {
        case kAsciiDelete:
            EndEscapeSequence(pShell);
            return kMetaKillWord;
        case '[':
            break;
        default:
            /* Not a recognized escape sequence */
            EndEscapeSequence(pShell);
            return kMetaNone;
        }
    }
    return EvaluateCSISequence(pShell, c, firstChar);
}

LT_INLINE void ToggleInsertMode(ShellPriv *pShell) {
    #if 0
    /* We don't have visual indicators for mode.
       Don't toggle - leave in insert mode: */
    pShell->insert = !pShell->insert;
    #endif
    pShell->insert = true;
    #if 0
    // Not sure how to show insert/overwrite mode - DECSCUSR does not work on minicom. */
    LTShellImpl_Print(pShell->handle, "\x1b[%d q", pShell->insert ? 5 : 1);
    LTShellImpl_Print(pShell->handle, "\x1b[?%dc", pShell->insert ? 4 : 6);
    #endif
}

static void EraseOneChar(ShellPriv *pShell, int which) {
    if (ltstring_isempty(pShell->pBuffer)) return;
    if (pShell->cursorPos < 0) {
        if (which < 0) {
            ltstring_removechars(&pShell->pBuffer, (u32)-1, 1);
            pShell->MoveCursorRelativeProc(pShell, -1);
            pShell->UpdateToEndOfLineProc(pShell, NULL);
        }
    } else {
        /* This behavior is independent of insert/overwrite modes.
           Remove character from buffer, and reflect update in pos: */
        ltstring_removechars(&pShell->pBuffer, pShell->cursorPos + which, 1);
        pShell->cursorPos += which;
        pShell->MoveCursorRelativeProc(pShell, which);
        pShell->UpdateToEndOfLineProc(pShell, pShell->pBuffer + pShell->cursorPos);
        if ((u32)pShell->cursorPos == lt_strlen(pShell->pBuffer)) {
            pShell->cursorPos = -1;
        }
    }
}

static void EraseLine(ShellPriv *pShell) {
    pShell->EraseEntireLineProc(pShell);
    pShell->PutCharProc(pShell, '\r');
    LTShellImpl_PrintPrompt(pShell->handle);
    ltstring_empty(pShell->pBuffer);
}

static void EraseOneWord(ShellPriv *pShell) {
    if (pShell->cursorPos == 0) return;
    u32 pos = (pShell->cursorPos > 0) ? (u32)pShell->cursorPos : lt_strlen(pShell->pBuffer);
    /* Remove spaces that trail the previous word: */
    u32 prevSpace = pos - 1;
    while (pShell->pBuffer[prevSpace] == ' ') {
        prevSpace--;
    }
    /* Find first space before the current position: */
    //FIXME:
    enum { kLTString_EndPos = (u32)-1 };
    prevSpace = kLTString_EndPos;//s_pIString->FindReverse(pShell->pBuffer, " ", prevSpace);
    if (prevSpace != kLTString_EndPos) {
        /* we are positioned on a second of subsequent word */
        LT_ASSERT(prevSpace < pos);
        /* Pos was originally the starting position.  here, change pos to the number of chars to re/move: */
        prevSpace++;
        pos = pos - prevSpace;
        ltstring_removechars(&pShell->pBuffer, prevSpace, pos);
    } else {
        /* we are positioned on the first word */
        ltstring_removechars(&pShell->pBuffer, 0, pos);
        if (ltstring_isempty(pShell->pBuffer)) pShell->cursorPos = -1;
    }
    /* Apply reposition to internal position and screen: */
    if (pShell->cursorPos > 0) {
        pShell->cursorPos -= pos;
    }
    pShell->MoveCursorRelativeProc(pShell, 0 - (s32)pos);
    pShell->UpdateToEndOfLineProc(pShell, (pShell->cursorPos != -1) ? pShell->pBuffer + pShell->cursorPos : NULL);
}

#if LTSHELL_ENABLE_DISALLOWANCE_GRANT
static void ReallowSleepMode(void *pClientData) {
    LT_UNUSED(pClientData);
    lt_reallowsleepmode(s_nDisallowanceGrant);
    s_nDisallowanceGrant = 0;
    s_pIThread->KillTimer(s_pIThread->GetCurrentThread(), &ReallowSleepMode, NULL);
}
#endif

static void HandleInputChar(ShellPriv *pShell, char c) {
    /* Prevent the system from entering sleep mode during input operations. */
#if LTSHELL_ENABLE_DISALLOWANCE_GRANT
    if (s_nDisallowanceGrant == 0) {
        s_nDisallowanceGrant = lt_disallowsleepmode();
        s_pIThread->SetTimer(pShell->hThread,
                             LTTime_Milliseconds(kLTShell_KeyStrokeIdleEnable_ms),
                             &ReallowSleepMode,
                             NULL,
                             NULL);
    } else {
        s_pIThread->RestartTimer(pShell->hThread, &ReallowSleepMode, NULL);
    }
#endif
    /* Valid printable chars occur between 0x20 and 0x7F: */
    if ((u8) c < 0x20 || 0x7F < (u8) c) return;
    if (pShell->cursorPos < 0) {
        ltstring_appendchar(&pShell->pBuffer, c);
    } else if (pShell->insert) /* insert mode */ {
        /* Put the new char into the buffer: */
        char buff[2] = {c, '\0'};
        ltstring_insert(&pShell->pBuffer, pShell->cursorPos, buff);
        pShell->UpdateToEndOfLineProc(pShell, pShell->pBuffer + pShell->cursorPos);
        pShell->cursorPos++;
    } else /* overwrite mode */ {
        pShell->pBuffer[pShell->cursorPos] = c;
        pShell->cursorPos++;
        if (pShell->cursorPos == (s32)lt_strlen(pShell->pBuffer)) {
            pShell->cursorPos = -1;
        }
    }
    if (pShell->echo) pShell->EchoCharProc(pShell, c);
}

static void ShowCommand(ShellPriv *pShell, LTString pCommand) {
    if (pCommand) {
        ltstring_set(&pShell->pBuffer, pCommand);
        pShell->PutStringProc(pShell, pShell->pBuffer);
    }
}

static void RepositionCursor(ShellPriv *pShell, s32 incr) {
    u32 length = lt_strlen(pShell->pBuffer);
    s32 pos = pShell->cursorPos;
    if (incr < 0 && pos == -1) {
        /* only if moving away from right edge */
        pos = (s32) length;
    }
    /* Increment should not allow movement past the edges.
       If condition is false, zero out the increment: */
    incr *= (incr < 0 && pos > 0) || (incr > 0 && pos != -1);
    /* Apply the increment: */
    pShell->MoveCursorRelativeProc(pShell, incr);
    pShell->cursorPos = pos + incr;
    if (pShell->cursorPos == (s32) length) {
        pShell->cursorPos = -1;
    }
}

static void PrepForCommand(ShellPriv *pShell, char c) {
    if (pShell->echo) {
        pShell->PutCharProc(pShell, c);
        if (pShell->crlf && c == kAsciiCarriageReturn) {
            pShell->PutCharProc(pShell, kAsciiNewLine);
        }
    }
}

static void RunSingleCommand(ShellPriv *pShell, LTString pCommand) {
    if (!pCommand || ltstring_isempty(pCommand)) return;
    LTArray *pArgv  = pShell->pArgv;
    LT_ASSERT(pArgv->API->GetCount(pArgv) == 0);
    TokenizeCommand(pArgv, pCommand);
    const int argc = pArgv->API->GetCount(pArgv);
    if (argc) {
        const char **argv = pArgv->API->GetStorage(pArgv);
        int err = 0;
        LTSystemShell_CommandDesc commandDesc;
        if (LTSystemShellImpl_LookupCommand(argv[0], &commandDesc)) {
            if (commandDesc.pCommandProc) {
                LTShell hShell = pShell->handle;
                if (0 != (err = commandDesc.pCommandProc(hShell, argc, argv))) {
                    if (s_pCore->IsHandleValid(hShell)) {
                        LTShellImpl_Print(hShell, "%s: returned %d\n", argv[0], err);
                    }
                }
            }
            else {
                LTShellImpl_PutString(pShell->handle, argv[0]);
                LTShellImpl_PutString(pShell->handle, ": malformed command registration\n");
            }
        }
        else {
            err = 1;
            LTShellImpl_PutString(pShell->handle, argv[0]);
            LTShellImpl_PutString(pShell->handle, ": command not found\n");
        }
    }
    pArgv->API->SetCount(pArgv, 0);
}

static const u32 kCharNotFound = 0xffffffff;

static u32 FindChar(const char *pString, char c, u32 pos) {
    char *pPos = lt_strchr(pString + pos, c);
    if (!pPos) return kCharNotFound;
    return pPos - pString;
}

static void RunCommands(ShellPriv *pShell, const char *pCommand) {
    if (!pCommand || ltstring_isempty(pCommand)) return;
    const bool commandStaged = StageCommand(pShell->pHistory, pCommand);
    u32 pos     = kCharNotFound;
    u32 qpos    = 0;
    u32 dpos    = 0;
    u32 prevpos = 0;
    u32 qcount  = 0;
    LTShell hShell = pShell->handle;
    do {
        qpos = FindChar(pCommand, '"',  pos + 1);
        dpos = FindChar(pCommand, ';',  pos + 1);
        if (qpos < dpos) {
            pos = qpos; /* move up to qpos */
            qcount++;
        } else if (dpos < qpos) {
            pos = dpos;
        } else {
            LT_ASSERT(qpos == kCharNotFound && dpos == kCharNotFound);
            /* Reached end, update position and force qcount to even */
            pos = dpos;
            qcount += qcount & 1;
        }
        if (!(qcount & 1) && (pos == dpos)) {
            u32 nbytes = ((pos == kCharNotFound) ? lt_strlen(pCommand) : pos) - prevpos;
            LTString pSubCommand = ltstring_createsubstring(pCommand, prevpos, nbytes);
            if (pSubCommand && !ltstring_isempty(pSubCommand)) {
                /* Run the embedded command and destroy the substring: */
                RunSingleCommand(pShell, pSubCommand);
                if (!s_pCore->IsHandleValid(hShell)) break;
                ltstring_destroy(pSubCommand);
            }
            prevpos = pos + 1;
        }
    } while (qpos < kCharNotFound || dpos < kCharNotFound);
    if (commandStaged && s_pCore->IsHandleValid(hShell)) {
        /* Commit even if command failed.  User may choose to scroll back and correct it. */
        CommitStaged(pShell->pHistory, /* err == 0 */ true);
    }
}

static void CleanupAfterCommand(ShellPriv *pShell) {
    ltstring_empty(pShell->pBuffer);
    LTShellImpl_PrintPrompt(pShell->handle);
    pShell->currentCommand = kLTShell_MaxHistory;
        /* Clear this state after a command executes so that
           the next scroll through history starts at the bottom: */
    pShell->cursorPos = -1;
        /* After a command is run, the cursor position returns to default state. */
}

static LTString GetAdjacentCommand(ShellPriv *pShell, s32 *pCurrentCommand, int increment) {
    LT_ASSERT(pCurrentCommand);
    if (!pShell->pHistory) return NULL;
    LTArray *pItems = pShell->pHistory->pItems;
    int nItems = pItems->API->GetCount(pItems);
    if (nItems == 0) return NULL;
    *pCurrentCommand += increment;
    if (*pCurrentCommand >= nItems) {
        if (increment > 0) {
            *pCurrentCommand = kLTShell_MaxHistory;
            return NULL;
        }
        *pCurrentCommand = nItems - 1;
    } else if (*pCurrentCommand < 0) {
        *pCurrentCommand = 0;
    }
    return pItems->API->Get(pItems, *pCurrentCommand, NULL);
}

static void Reboot(ShellPriv *pShell) {
    pShell->PutStringProc(pShell, "Reboot requested\n");
    LTDeviceWatchdog *pWatchdog = (LTDeviceWatchdog *)LT_GetCore()->OpenLibrary("LTDeviceWatchdog");
    if (pWatchdog)
        pWatchdog->Reboot();
    else
        pShell->PutStringProc(pShell, "Unable to access the watchdog\n");
}

/*__________________
_/ Init functions */
void LTShellImpl_LibInit(void) {
    s_pCore                 = LT_GetCore();
    s_pIThread              = lt_getlibraryinterface(ILTThread, s_pCore);
    LTConsoleShellImpl_LibInit();
}

void LTShellImpl_LibFini(void) {
    LTConsoleShellImpl_LibFini();
    s_pIThread              = NULL;
    s_pCore                 = NULL;
}

/*____________________________
_/ Shell creation functions */
ShellPriv *LTShellImpl_CreateShellPriv(void) {
    LTShell hShell     = s_pCore->CreateHandle((LTInterface *) LTSystemShellImpl_GetILTShell(), sizeof(ShellPriv));
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);

    pShell->handle                 = hShell;
    pShell->hThread                = 0;
    pShell->pArgv                  = lt_createobject(LTArray);
    pShell->pBuffer                = NULL;
    pShell->pHistory               = NULL;
    pShell->crlf                   = kLTShell_DefaultCRLFSetting;
    pShell->echo                   = kLTShell_DefaultEchoSetting;
    pShell->escapeStart            = -1;
    pShell->currentCommand         = kLTShell_MaxHistory;
    pShell->cursorPos              = -1;
    pShell->insert                 = true; // always in insert mode
    pShell->prevCharNewline        = false;
    /* These should be filled in by the implementation: */
    pShell->IsConsoleShell         = NULL;
    pShell->PutStringProc          = NULL;
    pShell->VPrintProc             = NULL;
    pShell->PutCharProc            = NULL;
    pShell->MoveCursorRelativeProc = NULL;
    pShell->EraseEntireLineProc    = NULL;
    pShell->UpdateToEndOfLineProc  = NULL;

    LTShellImpl_SetHistoryOn(hShell, true);

    return pShell;
}

void LTShellImpl_ShellReady(ShellPriv *pShell) {
    pShell->hThread = s_pIThread->GetCurrentThread();

    bool showBanner = true;
    bool showWelcome = true;
    LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
    if (productConfig) {
        // Default is to display the shell banner and welcome message. Product config can be used
        // to disable one or both, by setting the respective config values to 0.
        u32 libSection = productConfig->GetLibraryConfigSection("LTShell");
        if (libSection && productConfig->IsIntegerZero(libSection, "showBanner")) showBanner = false;
        if (libSection && productConfig->IsIntegerZero(libSection, "showWelcome")) showWelcome = false;
        lt_closelibrary(productConfig);
    }

    if (showBanner)  LTShellImpl_PrintShellBanner(pShell->handle);
    if (showWelcome) LTShellImpl_PrintShellWelcome(pShell->handle);
    LTShellImpl_PrintPrompt(pShell->handle);
}

/*____________________
_/ Utility functions */

void LTShellImpl_OnCharactersReceived(ShellPriv *pShell, const char *pChars, u32 nChars) {
    LTShell hShell = pShell->handle;
    for (; nChars; --nChars, ++pChars) {
        /* This function dispatches the key, or key sequence, to the respective function(s). */
        MetaChar mc = EvaluateMetaChar(pShell, *pChars);
        switch (mc) {
        case kMetaNone:
            HandleInputChar(pShell, *pChars);
            break;
        case kMetaCarriageReturn: /* drop through */
        case kMetaNewLine:
            PrepForCommand(pShell, *pChars);
            RunCommands(pShell, pShell->pBuffer);
            /* Once newline has arrived, the displayed command cannot be modified. We are free to modify
               pBuffer, which RunCommands() does. Don't access contents of pBuffer after RunCommands() finishes. */
            /* After running 'exit' hShell may no longer be valid. */
            if (s_pCore->IsHandleValid(hShell)) {
                if (kLTThread_ThreadState_TerminatePending != s_pIThread->GetThreadState(pShell->hThread)) {
                    CleanupAfterCommand(pShell);
                }
            }
            break;
        case kMetaUp:
        case kMetaDown:
            EraseLine(pShell);
            LTString pCommand = GetAdjacentCommand(pShell, &pShell->currentCommand, (mc == kMetaUp) ? -1 : +1);
            ShowCommand(pShell, pCommand);
            break;
        case kMetaLeft:
        case kMetaRight:
            RepositionCursor(pShell, mc == kMetaLeft ? -1 : +1);
            break;
        case kMetaBackspace:
            EraseOneChar(pShell, -1);
            break;
        case kMetaKillLine:
            EraseLine(pShell);
            break;
        case kMetaKillWord:
            EraseOneWord(pShell);
            break;
        case kMetaInsert:
            ToggleInsertMode(pShell);
            break;
        case kMetaDelete:
            EraseOneChar(pShell, 0);
            break;
        case kMetaCtrlAltDel:
            Reboot(pShell);
            break;
        case kMetaCtrlT:
            pShell->PutStringProc(pShell, "ps\n");
            RunCommands(pShell, "ps");
            pShell->PutStringProc(pShell, LTSHELL_PROMPT);
            pShell->Flush(pShell);
            break;
        case kMetaTab:
            break;
        case kMetaUnknown:
            /* Start of Metacharacter found, but don't know which one. Character has been consumed. Don't do anything here. */
            break;
        }
    }
}

bool LTShellImpl_IsConsoleShell(LTShell hShell) {
    bool bIsConsoleShell = false;
    ShellPriv * pShell = (ShellPriv *) s_pCore->ReserveHandlePrivateData(hShell);
    if (pShell) {
        bIsConsoleShell = pShell->IsConsoleShell(pShell);
        s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    }
    return bIsConsoleShell;
}

void LTShellImpl_TemporarySimulateShellRestart(LTShell hShell) {
    if (LTShellImpl_IsHistoryOn(hShell)) {
        LTShellImpl_SetHistoryOn(hShell, false);
        LTShellImpl_SetHistoryOn(hShell, true);
    }
    LTShellImpl_PrintShellBanner(hShell);
    LTShellImpl_PrintShellWelcome(hShell);
}

bool LTShellImpl_IsEchoOn(LTShell hShell) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    bool bRetVal = pShell ? pShell->echo : false;
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    return bRetVal;
}

bool LTShellImpl_IsCRLFOn(LTShell hShell) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    bool bRetVal = pShell ? pShell->crlf : false;
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    return bRetVal;
}

bool LTShellImpl_IsHistoryOn(LTShell hShell) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    bool bRetVal = pShell ? (NULL != pShell->pHistory) : false;
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    return bRetVal;
}

void LTShellImpl_SetEchoOn(LTShell hShell, bool bOn) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    if (pShell) {
        pShell->echo = bOn;
        s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    }
}

void LTShellImpl_SetCRLFOn(LTShell hShell, bool bOn) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    if (pShell) {
        pShell->crlf = bOn;
        s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    }
}

void LTShellImpl_SetHistoryOn(LTShell hShell, bool bOn) {
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    if (! pShell) return;
    if (bOn && (NULL == pShell->pHistory)) {
        pShell->pHistory                    = lt_malloc(sizeof(CommandHistory));
        pShell->pHistory->pItems            = lt_createobject(LTArray);
        pShell->pHistory->nEarliestNumber   = 1;
    }
    else if ((false == bOn) && pShell->pHistory) {
        LTArray * pItems = pShell->pHistory->pItems;
        u32 const nItems = pItems->API->GetCount(pItems);
        LTString pHistoryItem;
        for (u32 i = 0; i < nItems; ++i) {
            pHistoryItem = pItems->API->Get(pItems, i, NULL);
            ltstring_destroy(pHistoryItem);
        }
        lt_destroyobject(pItems);
        lt_free(pShell->pHistory);
        pShell->pHistory = NULL;
    }
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
}

void LTShellImpl_Reboot(LTShell hShell) {
    if (LTShellImpl_IsConsoleShell(hShell)) {
        ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
        if (pShell) {
            Reboot(pShell);
            s_pCore->ReleaseHandlePrivateData(hShell, pShell);
        }
    }
}

/*_______________________
_/ History Enumeration */
bool LTShellImpl_EnumerateHistory(LTShell hShell, LTShellImpl_HistoryEnumProc *pEnumProc, void *pClientData) {
    bool bRetVal = true;
    ShellPriv * pShell = (ShellPriv *)s_pCore->ReserveHandlePrivateData(hShell);
    if (pShell) {
        LTArray * pItems = pShell->pHistory->pItems;
        u32 nItems = pItems->API->GetCount(pItems);
        u32 nBase = pShell->pHistory->nEarliestNumber;
        LTString pHistoryItem;
        for (u32 i = 0; i < nItems; i++) {
            pHistoryItem = pItems->API->Get(pItems, i, NULL);
            if (! (*pEnumProc)(hShell, nBase + i, pHistoryItem, pClientData)) { bRetVal = false; break; }
        }
        s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    }
    return bRetVal;
}

/*_____________________
_/ Interface Binding */
static void LTShellImpl_DestroyShell(LTShell hShell) {
    ShellPriv * pShell = (ShellPriv *) s_pCore->ReserveHandlePrivateData(hShell);
    s_pCore->ReleaseHandlePrivateData(hShell, pShell);
    if (pShell->pArgv) {
        lt_destroyobject(pShell->pArgv);
        pShell->pArgv = NULL;
    }
    LTShellImpl_SetHistoryOn(pShell->handle, false); /* frees history data */
    pShell->Destroy(pShell->pImpl);
    ltstring_destroy(pShell->pBuffer);
}

define_LTLIBRARY_INTERFACE(ILTShell, LTShellImpl_DestroyShell) {
    .Print          = &LTShellImpl_Print,
    .VPrint         = &LTShellImpl_VPrint,
    .PutChar        = &LTShellImpl_PutChar,
    .PutString      = &LTShellImpl_PutString,
    .PrintPrompt    = &LTShellImpl_PrintPrompt
} LTLIBRARY_DEFINITION;
