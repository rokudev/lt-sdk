/*******************************************************************************
 * Local definitions for the shell handlers for LTDeviceMedia
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIADRIVERS_H
#define ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIADRIVERS_H

bool LTShellVideoDriverImpl_LibInit(void);
void LTShellVideoDriverImpl_LibFini(void);
void LTShellVideoDriverImpl_SetMotionSnapshotURL(const char *pSnapshotURL);

#endif  /* ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIADRIVERS_H */