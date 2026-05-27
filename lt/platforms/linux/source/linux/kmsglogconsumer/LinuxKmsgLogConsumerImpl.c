/*******************************************************************************
 * platforms/linux/source/linux/misc/logging/LinuxKmsgLogConsumerImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Linux LT Library for logging to the Linux kernel log ring buffer.
 * This allows retrieving the messages after the LT binary "ltrun" has
 * crashed e.g. via the "dmesg" Linux shell command.
 *
 * It also redirects standard error to the kernel ring buffer to preserve
 * error messages from the C library (e.g. in case of heap corruption).
 *
 * This implementation uses "/dev/kmsg" described here:
 * https://www.kernel.org/doc/Documentation/ABI/testing/dev-kmsg
 *
 ******************************************************************************/
/** @file LinuxKmsgLogConsumerImpl.c Implementation of "/dev/kmsg" logging for Linux
 */

#include <sys/uio.h>

#include <fcntl.h>
#include <limits.h>
#include <syslog.h>
#include <unistd.h>

#include <lt/system/logger/LTSystemLogger.h>

static struct Statics {
    int             oldStdErrFd, devKmsgFd;
    LTSystemLogger *pLogger;
    LTLogConsumer   hConsumer;
    bool            wasConsoleEnabled;
    char            prefixBuffer[24];
    char            sectionBuffer[256];
    char            logBuffer[1024];
} S;

static void Private_Setup(void *pClientData) {
    LT_UNUSED(pClientData);

    char ttyName[PATH_MAX];
    if (ttyname_r(STDOUT_FILENO, ttyName, sizeof(ttyName)) == 0 &&
        lt_strcmp(ttyName, "/dev/console") != 0) {
        /* Don't disable normal error logging when not on the system console. */
        S.wasConsoleEnabled = false;
        return;
    }

    LT_ASSERT(S.pLogger);
    S.wasConsoleEnabled = S.pLogger->IsConsoleEnabled();
    if (S.wasConsoleEnabled) S.pLogger->EnableConsole(false);
}

static void Private_Cleanup(LTThread_ReleaseReason releaseReason,
                            void *pClientData) {
    LT_UNUSED(releaseReason);
    LT_UNUSED(pClientData);

    LT_ASSERT(S.pLogger);
    if (S.wasConsoleEnabled) S.pLogger->EnableConsole(true);
}

static void LogToDevKmsg(const ILTLogConsumer *api,
                         LTLogConsumer consumer,
                         const LTLogMessage *msg) {
    if (S.devKmsgFd == -1) return;
    if (msg->recordType) return;

    if (msg->printPayload.format == NULL) return;

    LT_SIZE logBufferLen = sizeof(S.logBuffer);
    if (api->Print(consumer, msg, false, false, S.logBuffer, &logBufferLen) != LTLog_Error_None) {
        return;
    }

    u32 syslogFlags = LOG_DAEMON;
    switch (msg->level & kLTCore_LogFlags_LogTypeMask) {
    case kLTCore_LogFlags_LogTypeRaw:
        LT_ASSERT(S.pLogger);
        if (!S.pLogger->IsConsoleEnabled()) {
            // The console log consumer is disabled. Directly output
            // raw log lines to keep "LTSystemShell" working.
            ssize_t written = write(STDOUT_FILENO, S.logBuffer, logBufferLen);
            LT_UNUSED(written);
        }
        return;

#ifndef LT_DEBUG
    case kLTCore_LogFlags_LogTypeVerbose:
    case kLTCore_LogFlags_LogTypeDebugLog:
        return;

#else
    case kLTCore_LogFlags_LogTypeVerbose:
        syslogFlags |= LOG_INFO;
        break;

    case kLTCore_LogFlags_LogTypeDebugLog:
        syslogFlags |= LOG_DEBUG;
        break;
#endif

    case kLTCore_LogFlags_LogTypeLog:
        syslogFlags |= LOG_NOTICE;
        break;

    case kLTCore_LogFlags_LogTypeYellowAlert:
        syslogFlags |= LOG_ERR;
        break;

    case kLTCore_LogFlags_LogTypeRedAlert:
        syslogFlags |= LOG_CRIT;
        break;

    case kLTCore_LogFlags_LogTypeAssert:
        syslogFlags |= LOG_EMERG;
    }

    int prefixLen = lt_snprintf(S.prefixBuffer, sizeof(S.prefixBuffer),
                                "<%u>LT: ", LT_Pu32(syslogFlags));
    if (prefixLen <= 0 || prefixLen >= (int)sizeof(S.prefixBuffer)) return;

    int sectionLen = lt_snprintf(S.sectionBuffer, sizeof(S.sectionBuffer),
                                 "[%s%s%s] ",
                                 msg->printPayload.section,
                                 msg->printPayload.tag ? "." : "",
                                 msg->printPayload.tag ? msg->printPayload.tag : "");
    if (sectionLen <= 0 || sectionLen >= (int)sizeof(S.sectionBuffer)) return;

    struct iovec iov[4];
    int iovcnt = 3;
    iov[0].iov_base = S.prefixBuffer;
    iov[0].iov_len = prefixLen;
    iov[1].iov_base = S.sectionBuffer;
    iov[1].iov_len = sectionLen;
    iov[2].iov_base = S.logBuffer;
    iov[2].iov_len = logBufferLen;
    if (logBufferLen == 0 || S.logBuffer[logBufferLen - 1] != '\n') {
        static char lineFeed = '\n';
        iov[3].iov_base = &lineFeed;
        iov[3].iov_len = sizeof(lineFeed);
        iovcnt++;
    }

    LT_ASSERT(S.devKmsgFd != -1);
    ssize_t written = writev(S.devKmsgFd, iov, iovcnt);
    LT_UNUSED(written);
}

static void LinuxKmsgLogConsumerImpl_LibFini(void) {
    if (S.hConsumer) {
        LT_ASSERT(S.pLogger);
        S.pLogger->RemoveConsumer(S.hConsumer);
        S.hConsumer = 0;
    }

    lt_closelibrary(S.pLogger);
    S.pLogger = NULL;

    if (S.devKmsgFd != -1) {
        close(S.devKmsgFd);
        S.devKmsgFd = -1;
    }

    if (S.oldStdErrFd != -1) {
        dup2(S.oldStdErrFd, STDERR_FILENO);
        close(S.oldStdErrFd);
        S.oldStdErrFd = -1;
    }
}

static bool LinuxKmsgLogConsumerImpl_LibInit(void) {
    S = (struct Statics) {
        .oldStdErrFd = -1,
        .devKmsgFd   = -1,
    };

    do {
        S.oldStdErrFd = dup(STDERR_FILENO);
        if (S.oldStdErrFd == -1) break;

        S.devKmsgFd = open("/dev/kmsg", O_WRONLY);
        if (S.devKmsgFd == -1) break;
        if (dup2(S.devKmsgFd, STDERR_FILENO) == -1) break;

        S.pLogger = lt_openlibrary(LTSystemLogger);
        if (!S.pLogger) break;

        S.hConsumer = S.pLogger->AddConsumer(LogToDevKmsg, Private_Setup,
                                             Private_Cleanup, NULL);
        if (!S.hConsumer) break;

        return true;
    } while (0);

    LinuxKmsgLogConsumerImpl_LibFini();
    return false;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LinuxKmsgLogConsumer, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(LinuxKmsgLogConsumer) LTLIBRARY_DEFINITION;
