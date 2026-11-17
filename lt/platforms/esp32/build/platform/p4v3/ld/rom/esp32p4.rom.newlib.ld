/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* ROM function interface esp32p4.rom.newlib.ld for esp32p4
 *
 *
 * Generated from ./target/esp32p4/interface-esp32p4.yml md5sum 56d78222be1daa0502090a078288f4d5
 *
 * Compatible with ROM where ECO version equal or greater to 5.
 *
 * THIS FILE WAS AUTOMATICALLY GENERATED. !!! BUT EDITED !!!
 * The file was originally generated for use with newlib, but it was split into
 * multiple files to make it compatible with picolibc.
 */

/***************************************
 Group newlib
 ***************************************/

/* Functions */
_isatty_r = 0x4fc00294;
strdup = 0x4fc002f0;
strndup = 0x4fc00314;
rand_r = 0x4fc0034c;
rand = 0x4fc00350;
srand = 0x4fc00354;
atoi = 0x4fc00360;
atol = 0x4fc00364;
strtol = 0x4fc00368;
strtoul = 0x4fc0036c;
fflush = 0x4fc00370;
_fflush_r = 0x4fc00374;
_fwalk = 0x4fc00378;
_fwalk_reent = 0x4fc0037c;
__smakebuf_r = 0x4fc00380;
__swhatbuf_r = 0x4fc00384;
__swbuf_r = 0x4fc00388;
__swbuf = 0x4fc0038c;
__swsetup_r = 0x4fc00390;

/*
 * LT addition: IDF v6.1 keeps the ROM's libc routines (memcpy, memset, ...) in a
 * script of their own, esp32p4.rom.libc.ld, which IDF links alongside this one.  The
 * shared esp32p4 scripts carried them in rom.newlib.ld, and the project makefiles
 * name only that file, so it pulls the libc script in.
 */
INCLUDE esp32p4.rom.libc.ld
/*
 * memcpy, memmove, memcmp, strcpy and a few more are in a further script, which IDF
 * links conditionally: the ROM's versions are correct but not optimized for
 * misaligned access (see the note in that file).  LT has no alternative for them, so
 * it takes the ROM's.
 */
INCLUDE esp32p4.rom.libc-suboptimal_for_misaligned_mem.ld
