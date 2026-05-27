/******************************************************************************
 * lt/source/system/shell/LTShellCommands.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLCOMMANDS_H
#define ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLCOMMANDS_H

#include <lt/system/shell/LTSystemShell.h>

/*__________________
_/ Init functions */
void LTShellCommands_LibInit(void);
void LTShellCommands_LibFini(void);

/*____________________
_/ Utility functions */
const LTSystemShell_CommandDesc * LTShellCommands_GetInbuiltShellCommands(void);
      u32                         LTShellCommands_GetNumInbuiltShellCommands(void);

#endif /* #ifndef ROKU_LT_SOURCE_LT_SYSTEM_SHELL_LTSHELLCOMMANDS_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Dec-20   augustus    created
 *  26-Jan-21   augustus    added LibInit andLibFini in support of ltlib --open
 */
