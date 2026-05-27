/*******************************************************************************
 * platforms/linux/include/closefrom.h
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef ROKU_PLATFORM_LINUX_INCLUDE_LINUXCLOSEFROM_H
#define ROKU_PLATFORM_LINUX_INCLUDE_LINUXCLOSEFROM_H

#include <unistd.h>

#if !defined(__GLIBC__) || (__GLIBC__ <= 2 && __GLIBC_MINOR__ < 34)
// Version 2.34 and newer of glibc provides Solaris's closefrom(2) but
// older versions and uClibc do not. Provide our own implementation
// in this case.

#include <sys/resource.h>
#include <sys/time.h>

static void closefrom(int fd) {
    int max_fd = 1024;
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        max_fd = rlim.rlim_cur;
    }
    for (; fd < max_fd; fd++) {
        close(fd);
    }
}
#endif

#endif // #ifndef ROKU_PLATFORM_LINUX_INCLUDE_LINUXCLOSEFROM_H
