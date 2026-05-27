/******************************************************************************
 * Ameba.h                                     Common methods for Ameba Devices
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef _AMEBA_H
#define _AMEBA_H

enum {
    kInitialBaudRate       = 115200,
    kFastBaudRate          = 3000000,

    kDefaultTimeoutMS      = 2500,
};

enum ModeOfMystery {
   ELDRITCH_HORROR  = 0,
   CTHULHU_SLUMBERS = 1,
};

void AmebaSetModeOfMystery(u8 mode);
int AmebaPing(void);
int AmebaSendUCFGandSetSpeed(u32 baud_rate);
int AmebaErase(u32 offset, u32 num_sectors);
int AmebaProgram(u32 offset, Image * pImage);
int AmebaVerifyChecksum(Image * pImage);
int AmebaRebootTarget(void);

#endif

