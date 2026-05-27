/******************************************************************************
 * FlashDevice.h                            Generic Flash Programming Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef _FLASH_DEVICE_H
#define _FLASH_DEVICE_H
#include "lt/LTTypes.h"

typedef struct {
   int  (*Open)(const char * pSerialDevice, bool bAutoProgram, const char * pPlatformArgs);
   void (*Close)(void);
   int  (*Program)(u32 nAreaIdx, char * pFilename, bool bPad, bool bAutoReboot);
   int  (*Read)(u32 nAreaIdx, char * pFilename, bool bAutoReboot);
   int  (*Erase)(u32 nAreaIdx);
   int  (*Reboot)(void);
} FlashDeviceInterface;

typedef struct {
   const char * pFamily;       // Optional. When NULL/empty, match-by-family is skipped.
   const char * pDevice;       // Optional. When NULL/empty, match-by-device is skipped.
   const char * pLegacyDevice; // Optional. Used when config omits family.
   const FlashDeviceInterface * pInterface;
} FlashDeviceInterfaceMapEntry;

bool FlashDevice_Register(const FlashDeviceInterfaceMapEntry * pEntry);

const FlashDeviceInterface * GetFlashDeviceInterface(const char * pDevice, const char * pFamily);

#endif
