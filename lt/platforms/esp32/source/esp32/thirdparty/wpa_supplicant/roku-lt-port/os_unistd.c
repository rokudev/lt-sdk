/******************************************************************************
 * os_unistd.c                                     ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * port/os_xtensa.c is hostap's own file and implements os_sleep() on the two
 * <unistd.h> entry points below.  The image is linked -nodefaultlibs, so
 * nothing supplies them; these do, on LTThread.  Prototypes come from newlib's
 * header so the definitions cannot drift from what the caller saw.
 */

#include <unistd.h>

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>

unsigned
sleep(unsigned int __seconds) {
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Seconds(__seconds));
    return 0;
}

int
usleep(useconds_t __useconds) {
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Microseconds(__useconds));
    return 0;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
