/******************************************************************************
 * bootloader.rom.ld
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/* No definition for ESP32-P4 target.  The esp32 and esp32s3 carry a ROM address
   table here to reach entries the generated rom.ld does not name; the esp32p4
   needs none.  The file is kept so the link line stays the same across chips. */
