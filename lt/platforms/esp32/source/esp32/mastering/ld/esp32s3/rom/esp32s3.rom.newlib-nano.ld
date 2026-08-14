/* ROM function interface esp32s3.rom.newlib-nano.ld for esp32s3
 *
 *
 * Generated from ./interface-esp32s3.yml md5sum 39c4ce259b11323b9404c192b01b712b
 *
 * Compatible with ROM where ECO version equal or greater to 0.
 *
 * THIS FILE WAS AUTOMATICALLY GENERATED. DO NOT EDIT.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 */

/***************************************
 Group newlib_nano_format
 ***************************************/

/* Functions */
__sprint_r = 0x4000156c;
_fiprintf_r = 0x40001578;
_fprintf_r = 0x40001584;
_printf_common = 0x40001590;
_printf_i = 0x4000159c;
_vfiprintf_r = 0x400015a8;
_vfprintf_r = 0x400015b4;
fiprintf = 0x400015c0;
fprintf = 0x400015cc;
printf = 0x400015d8;
vfiprintf = 0x400015e4;
vfprintf = 0x400015f0;
