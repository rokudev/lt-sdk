/******************************************************************************
 * LTNetworkShellImpl.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SYSTEM_SHELL_LTNETWORKSHELLIMPL_H
#define ROKU_LT_SYSTEM_SHELL_LTNETWORKSHELLIMPL_H

bool LTNetworkShellImpl_LibInit(void);

void LTNetworkShellImpl_LibFini(void);

bool LTNetworkShellImpl_StartNetworkShellSubsystem(void);

void LTNetworkShellImpl_StopNetworkShellSubsystem(void);

#endif // ROKU_LT_SYSTEM_SHELL_LTNETWORKSHELLIMPL_H
