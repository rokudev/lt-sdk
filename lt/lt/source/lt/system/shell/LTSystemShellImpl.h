/******************************************************************************
 * lt/source/system/shell/LTSystemShellImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLIMPL_H
#define ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLIMPL_H

#include <lt/system/shell/LTSystemShell.h>

typedef bool (LTSystemShellImpl_CommandEnumProc)(const LTSystemShell_CommandDesc * pCommandDesc, void * pClientData);
    /* Callback used for registered shell command enumeration.  return true from the callback to continue enumeration
       false to abort enumeration.  */

bool LTSystemShellImpl_EnumerateCommands(LTSystemShellImpl_CommandEnumProc * pEnumProc, void * pClientData);
    /* Enumerates the registered shell commands.  Internal mutex is locked during enumeration.
       returns true if enumeration completes, false if enumeration was aborted by callback proc */

bool LTSystemShellImpl_LookupCommand(const char * pCommand, LTSystemShell_CommandDesc * pCommandDescToSet);
    /* fills *pCommandDescToSet with the command desc registered for command pCommand returning true if found, false if not found.
       The command desc record is copied because the list is internally protected with a mutex */

ILTShell * LTSystemShellImpl_GetILTShell(void);


#endif /* #ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLIMPL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Nov-20   augustus    created
 *  18-Dec-20   augustus    enumeration can be aborted; LookupCommand is thread safe
 */
