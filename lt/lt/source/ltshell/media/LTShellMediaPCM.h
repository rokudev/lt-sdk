/*******************************************************************************
 * Local definitions for the PCM media playback and capture.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAPCM_H
#define ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAPCM_H
#include <lt/LT.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>

LT_EXTERN_C_BEGIN

// Playback
bool PlayPCMFile(LTShell hShell, LTFile *pFile, LTMediaFormat fmt);
void StopPCMPlayback(void);

// Capture
bool StartPCMCapture(LTShell hShell, LTFile *pFile, LTMediaFormat fmt);
void StopPCMCapture(void);

LT_EXTERN_C_END
#endif /* ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAPCM_H */
