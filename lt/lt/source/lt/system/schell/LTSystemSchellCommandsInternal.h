/*******************************************************************************
 * lt/source/lt/system/shell/LTSystemShellCommandsInternal.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDSINTERNAL_H
#define ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDSINTERNAL_H

#include <lt/system/schell/LTSystemSchellCommands.h>

int ShellCommand_LTExit(LTSystemSchell *shell, int argc, const char **argv);
void LTSystemSchellCommands_Cleanup(void);

#endif /* ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDSINTERNAL_H */
