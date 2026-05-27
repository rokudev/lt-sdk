/******************************************************************************
 * platforms/Linux/source/linux/driver/ota/LinuxDriverOta.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *****************************************************************************/

#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <lt/core/LTCore.h>
#include <lt/device/ota/LTDeviceOta.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>

#include <LinuxCloseFrom.h>

DEFINE_LTLOG_SECTION("linux.ota");

static struct Statics {
    LTDeviceConfig   *pDeviceConfig;
    LTSystemCrypto   *pCrypto;
    LTDeviceWatchdog *pWatchdog;
    const char       *pApplyCommand;
    char             otaFilename[32];
    int              otaFd;
} S;

enum {
    kLinuxDriverOta_ApplyUpdateTimeout = 600,
};

static void CloseOtaFile(void) {
    if (S.otaFd != -1) {
        close(S.otaFd);
        S.otaFd = -1;
    }
}

static void RemoveOtaFile(void) {
    CloseOtaFile();
    if (lt_strlen(S.otaFilename) > 0) {
        unlink(S.otaFilename);
        lt_memset(S.otaFilename, 0, sizeof(S.otaFilename));
    }
}

static bool LinuxDriverOta_Init(void) {
    return true;
}

static bool LinuxDriverOta_IsValidated(void) {
    return true;
}

static bool LinuxDriverOta_CheckStorage(u32 imageSize) {
    LT_UNUSED(imageSize);
    return true;
}

static bool LinuxDriverOta_PrepareStorage(u32 imageSize) {
    LT_UNUSED(imageSize);
    RemoveOtaFile();

    // Create a *new* temporary file which allows reading and writing.
    lt_strncpyTerm(S.otaFilename, "/tmp/ota-XXXXXX", sizeof(S.otaFilename));
    S.otaFd = mkstemp(S.otaFilename);

    return (S.otaFd != -1);
}

static bool LinuxDriverOta_SaveBlock(const u8 *data, u32 dataLen, u32 offsetToSave) {
    LTLOG_DEBUG("save", "%02X%02X len %lu offset %lu", data[0], data[1], LT_Pu32(dataLen), LT_Pu32(offsetToSave));
    if (S.otaFd == -1) return false;

    ssize_t wlen = pwrite(S.otaFd, data, dataLen, offsetToSave);
    if (wlen == -1) {
        LTLOG_REDALERT("save.err", "pwrite(): %d", errno);
        return false;
    }
    if (wlen != (ssize_t)dataLen) {
        LTLOG_REDALERT("save.trunc", "write %ld of %lu", LT_Ps32(wlen), LT_Pu32(dataLen));
        return false;
    }
    return true;
}

static bool LinuxDriverOta_VerifyImage(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]) {
    // This occurs when image downloading just completes, and the file is still open for verification.
    if (S.otaFd == -1) return false;

    struct stat statbuf;
    if (fstat(S.otaFd, &statbuf) == -1) {
        LTLOG_REDALERT("vfy.err", "fstat(): %d", errno);
        return false;
    }
    if (statbuf.st_size != (off_t)imageSize) {
        LTLOG_REDALERT("vfy.err", "image size mismatch: %lld != %lu",
                       LT_Ps64(statbuf.st_size), LT_Pu32(imageSize));
        return false;
    }

    void *image = mmap(NULL, imageSize, PROT_READ, MAP_PRIVATE, S.otaFd, 0);
    if (image == MAP_FAILED) {
        LTLOG_REDALERT("vfy.err", "mmap(): %d", errno);
        return false;
    }

    u8 fwHash[SHA256_HASH_LENGTH];
    S.pCrypto->GenDigestSHA256(image, imageSize, fwHash);

    bool verified = false;
    if (lt_memcmp(fwHash, imageHash, SHA256_HASH_LENGTH) == 0) {
        LTLOG_DEBUG("vfy.ok", "SWUP image hash ok");
        verified = true;
    } else {
        LTLOG_REDALERT("vfy.err", "SWUP image hash failed");
    }

    munmap(image, imageSize);
    CloseOtaFile();
    return verified;
}

static void LinuxDriverOta_ApplyUpdate_ChildProcess(void) {
    LT_ASSERT(S.pApplyCommand);

    closefrom(STDERR_FILENO + 1);

    int devnull = open(_PATH_DEVNULL, O_RDONLY);
    if (devnull != -1) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }

    execlp(S.pApplyCommand, S.pApplyCommand, "-v", S.otaFilename, NULL);
}

static bool LinuxDriverOta_ApplyUpdate(const char *version) {
    LT_UNUSED(version);

    if (!S.pApplyCommand) return true;

    S.pWatchdog->ResetTimer();

    pid_t child = fork();
    switch (child) {
    case -1:
        LTLOG_REDALERT("apply.err", "fork(): %d", errno);
        return false;

    case 0:
        LinuxDriverOta_ApplyUpdate_ChildProcess();
        _exit(EX_OSERR);
    }

    int status, wret;
    LTCore *pCore = LT_GetCore();
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, pCore);
    LTTime endTime = pCore->GetKernelTime();
    LTTime_AddTo(endTime, LTTime_Seconds(kLinuxDriverOta_ApplyUpdateTimeout));
    bool done = false;
    for (LTTime now = pCore->GetKernelTime();
         LTTime_IsLessThanOrEqual(now, endTime);
         now = pCore->GetKernelTime()) {
        wret = waitpid(child, &status, WNOHANG);
        if (wret == child) {
            done = true;
            break;
        }
        if (wret != 0) {
            int error = errno;
            LTLOG_REDALERT("apply.err", "waitpid(): %d", error);
            return false;
        }
        if (LTTime_GetSeconds(now) % 10 == 0) LTLOG("waiting.apply.update", NULL);
        S.pWatchdog->ResetTimer();
        iThread->Sleep(LTTime_Seconds(1));
    }

    if (!done) {
        LTLOG_REDALERT("apply.oot", "apply command ran out of time");
        kill(child, SIGKILL);
        return false;
    }

    if (!WIFEXITED(status)) {
        LTLOG_REDALERT("apply.crash", "apply command terminated abnormally");
        return false;
    }

    int exit_code = WEXITSTATUS(status);
    if (exit_code != 0) {
        LTLOG_REDALERT("apply.fail", "apply command failed with exit code %d", exit_code);
        return false;
    }

    return true;
}

static void LinuxDriverOta_Complete(void) {
    RemoveOtaFile();
}

static bool LinuxDriverOta_MarkValidated(void) {
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTOta s_ILTOta;

static u32 LinuxDriverOtaImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit LinuxDriverOtaImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTOta, 1);
}

static void LinuxDriverOtaImpl_LibFini(void) {
    RemoveOtaFile();
    lt_closelibrary(S.pWatchdog);
    S.pWatchdog = NULL;
    lt_closelibrary(S.pCrypto);
    S.pCrypto = NULL;
    S.pApplyCommand = NULL;
    lt_closelibrary(S.pDeviceConfig);
    S.pDeviceConfig = NULL;
}

static bool LinuxDriverOtaImpl_LibInit(void) {
    S = (struct Statics) {
        .otaFd = -1
    };

    do {
        S.pDeviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!S.pDeviceConfig) break;

        S.pCrypto = lt_openlibrary(LTSystemCrypto);
        if (!S.pCrypto) break;

        u32 driverSection = S.pDeviceConfig->GetDriverSection("LTDeviceOta",
                                                              "LinuxDriverOta");
        if (driverSection) {
            S.pApplyCommand = S.pDeviceConfig->ReadString(driverSection,
                                                          "apply_command");
        }

        S.pWatchdog = lt_openlibrary(LTDeviceWatchdog);
        if (!S.pWatchdog) break;

        return true;
    } while (0);

    LinuxDriverOtaImpl_LibFini();
    LTLOG_YELLOWALERT("error", "fail to init");
    return false;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceOta, LinuxDriverOta);

define_LTLIBRARY_INTERFACE(ILTOta) {
    .Init            = &LinuxDriverOta_Init,
    .IsValidated     = &LinuxDriverOta_IsValidated,
    .CheckStorage    = &LinuxDriverOta_CheckStorage,
    .PrepareStorage  = &LinuxDriverOta_PrepareStorage,
    .SaveBlock       = &LinuxDriverOta_SaveBlock,
    .VerifyImage     = &LinuxDriverOta_VerifyImage,
    .ApplyUpdate     = &LinuxDriverOta_ApplyUpdate,
    .MarkValidated   = &LinuxDriverOta_MarkValidated,
    .Complete        = & LinuxDriverOta_Complete,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LinuxDriverOta, (ILTOta))

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *  07-Aug-23   gallienus   created
 */
