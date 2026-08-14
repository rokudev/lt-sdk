/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 */
/* These are the newlib functions and the .bss/.data symbols which are related to 'time_t'
   or other structures which include 'time_t' (like 'struct stat').
   These ROM functions were compiled with sizeof(time_t) == 4.
   When compiling with sizeof(time_t) == 8, these functions should be excluded from the build.
 */

_isatty_r = 0x4000126c;
PROVIDE( __smakebuf_r = 0x40001530 );
PROVIDE( __swsetup_r = 0x40001560 );
PROVIDE( __swhatbuf_r = 0x4000153c );
