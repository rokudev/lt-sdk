/*******************************************************************************
 * platforms/linux/source/linux/driver/watchdog/LinuxDriverWatchdogImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Driver Library for watchdog functions
 *
 * Provides Linux-specific control for enabling, disabling, resetting, and
 * setting the timeout interval of watchdogs.
 *
 * Also provides general system reset.
 *
 * This implementation uses "The Linux Watchdog driver API" described here:
 * https://www.kernel.org/doc/Documentation/watchdog/watchdog-api.txt
 *
 ******************************************************************************/
/** @file LinuxDriverWatchdogImpl.c Implementation of watchdog driver for Linux
 */

#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <linux/watchdog.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

static struct Statics {
    LTMutex  *mutex;
    int       watchdogFd;
    int       watchdogTimeout;
} S;

/*******************************************************************************
 * Utility functions                                                          */

static void DirectConsoleLog(const char *format, ...) {
    char buffer[256];

    lt_va_list args;
    lt_va_start(args, format);
    int count = lt_vsnprintf(buffer, sizeof(buffer), format, args);
    lt_va_end(args);
    if (count <= 0) return;

    struct iovec iov[3];
    iov[0].iov_base = "\n";
    iov[0].iov_len = 1;
    iov[1].iov_base = buffer;
    iov[1].iov_len = count;
    iov[2] = iov[0];
    ssize_t written = writev(STDERR_FILENO, iov, sizeof(iov) / sizeof(iov[0]));
    LT_UNUSED(written);
}

static void ForcedReboot(const char *) __attribute__((noreturn));

static void ForcedReboot(const char *reason) {
    DirectConsoleLog("Initiating ungraceful reboot: %s", reason);
    reboot(RB_AUTOBOOT);
    // Safety net only because the above system call should never return.
    __builtin_unreachable();
}

/*
 * From https://www.kernel.org/doc/Documentation/watchdog/watchdog-api.txt :
 *
 * Magic Close feature:
 *
 * If a driver supports "Magic Close", the driver will not disable the
 * watchdog unless a specific magic character 'V' has been sent to
 * /dev/watchdog just before closing the file.  If the userspace daemon
 * closes the file without sending this special character, the driver
 * will assume that the daemon (and userspace in general) died, and will
 * stop pinging the watchdog without disabling it first.  This will then
 * cause a reboot if the watchdog is not re-opened in sufficient time.
 */

static bool DisarmWatchdog(int watchdogFd) {
    static const char magic = 'V';
    return (write(watchdogFd, &magic, sizeof(magic)) == (ssize_t)sizeof(magic));
}

static int OpenWatchdogDevice(int *pTimeout) {
    int fd = open("/dev/watchdog", O_WRONLY);
    if (fd != -1) {
        if (ioctl(fd, WDIOC_SETTIMEOUT, pTimeout) == 0) return fd;

        DisarmWatchdog(fd);
        close(fd);
    }

    return -1;
}

/*******************************************************************************
 * ILTDriverWatchdog implementation                                           */

static bool LinuxWatchdog_ResetTimer(void) {
    bool ret = false;

    S.mutex->API->Lock(S.mutex);

    if (S.watchdogFd != -1) {
        ret = (ioctl(S.watchdogFd, WDIOC_KEEPALIVE, 0) == 0);
    } else {
        ret = true;
    }

    S.mutex->API->Unlock(S.mutex);

    return ret;
}

static bool LinuxWatchdog_DisableTimer(void) {
    bool ret = false;

    S.mutex->API->Lock(S.mutex);

    if (S.watchdogFd != -1) {
        if (DisarmWatchdog(S.watchdogFd)) {
            close(S.watchdogFd);
            S.watchdogFd = -1;
            ret = true;
        }
    } else {
       ret = true;
    }

    S.mutex->API->Unlock(S.mutex);

    return ret;
}

static bool LinuxWatchdog_EnableTimer(void) {
    bool ret = false;

    S.mutex->API->Lock(S.mutex);

    if (S.watchdogFd == -1) {
        S.watchdogFd = OpenWatchdogDevice(&S.watchdogTimeout);
    }
    ret = (S.watchdogFd != -1);

    S.mutex->API->Unlock(S.mutex);

    return ret;
}

static bool LinuxWatchdog_IsEnabled(void) {
    S.mutex->API->Lock(S.mutex);

    bool enabled = (S.watchdogFd != -1);

    S.mutex->API->Unlock(S.mutex);

    return enabled;
}

static bool LinuxWatchdog_SetTimeout(LTTime ltTimeout) {
    bool ret = false;

    LTTime_AddTo(ltTimeout, LTTime_Milliseconds(500));
    int timeout = LTTime_GetSeconds(ltTimeout);
    if (timeout <= 0) return false;

    S.mutex->API->Lock(S.mutex);

    S.watchdogTimeout = timeout;
    if (S.watchdogFd != -1) {
        ret = (ioctl(S.watchdogFd, WDIOC_SETTIMEOUT, &S.watchdogTimeout) == 0);
    } else {
        ret = true;
    }

    S.mutex->API->Unlock(S.mutex);

    return ret;
}

static void LinuxWatchdog_Reboot(void) {
    S.mutex->API->Lock(S.mutex);

    // Configure the watchdog to force a reboot in 20 seconds.
    int rebootTimeout = 20;
    if (S.watchdogFd != -1) {
        if (ioctl(S.watchdogFd, WDIOC_SETTIMEOUT, &rebootTimeout) != 0) {
            // We cannot set the watchdog timer. Reboot immediately.
            ForcedReboot("Cannot set watchdog timer");
        }
    } else {
        S.watchdogFd = OpenWatchdogDevice(&rebootTimeout);
        if (S.watchdogFd == -1) ForcedReboot("Cannot open watchdog device");
    }
    ioctl(S.watchdogFd, WDIOC_KEEPALIVE, 0);
     /*
      * The call to close() below does not disable the watchdog. It just
      * gets the kernel warning message out of the way which makes it easier
      * to read the console output while the device is rebooting.
      */
    close(S.watchdogFd);
    S.watchdogFd = -1;

    // With the watchdog enabled attempt a graceful shutdown before that happens.
    DirectConsoleLog("Initiating graceful reboot");
    sync();
    if (kill(1, SIGTERM) != 0)
        ForcedReboot("Cannot signal init process");
    exit(0);
}

static LTBootReason LinuxWatchdog_GetBootReason(const char ** pReasonString) {
    static const char * reasonString = "Not implemented";
    if (pReasonString) *pReasonString = reasonString;
    return kLTBootReason_Undefined;
}

define_LTLIBRARY_INTERFACE(ILTDriverWatchdog) {
    .Reboot = LinuxWatchdog_Reboot,
    .ResetTimer = LinuxWatchdog_ResetTimer,
    .EnableTimer = LinuxWatchdog_EnableTimer,
    .DisableTimer = LinuxWatchdog_DisableTimer,
    .IsEnabled = LinuxWatchdog_IsEnabled,
    .SetTimeout = LinuxWatchdog_SetTimeout,
    .GetBootReason = LinuxWatchdog_GetBootReason,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverWatchdog, (ILTDriverWatchdog))

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceWatchdog, LinuxDriverWatchdog);

static void LinuxDriverWatchdogImpl_LibFini(void) {
    LinuxWatchdog_DisableTimer();

    lt_destroyobject(S.mutex);
    S.mutex = NULL;
}

static bool LinuxDriverWatchdogImpl_LibInit(void) {
    S = (struct Statics) {
        .watchdogFd      = -1,
        .watchdogTimeout = 60
    };

    S.mutex = lt_createobject(LTMutex);
    if (!S.mutex) return false;

    return true;
}

static u32 LinuxDriverWatchdogImpl_GetNumDeviceUnits(void) {
    return 0;
}

LTDeviceUnit LinuxDriverWatchdogImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    return 0;
}
