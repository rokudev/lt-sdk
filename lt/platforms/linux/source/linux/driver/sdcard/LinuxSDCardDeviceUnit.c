/*******************************************************************************
 * platforms/linux/source/linux/driver/sdcard/LinuxSDCardDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <linux/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lt/LT.h>
#include "LinuxSDCardUnit.h"

#define SDCARD_PATH "/dev/mmcblk0"
#define SDCARD_PART_PATH SDCARD_PATH "p1"
#define MOUNTPOINT "/sdcard"

// String length constants for safe strncmp usage
#define SDCARD_PATH_LEN 12          // strlen("/dev/mmcblk0")
#define SDCARD_PART_PATH_LEN 14     // strlen("/dev/mmcblk0p1")
#define MOUNTPOINT_LEN 7            // strlen("/sdcard")
#define VFAT_LEN 4                  // strlen("vfat")
#define EXFAT_LEN 5                 // strlen("exfat")

#define DEFAULT_SDCARD_DEVICE_LTHANDLE ((LTHandle)0)

DEFINE_LTLOG_SECTION("linux.sdcard.unit");

/****************************************
 * Static variables
 ***************************************/
static const ILTSDCardDeviceUnit s_ILTSDCardDeviceUnit; // forward decl

static ILTEvent  * s_iEvent;
static ILTThread * s_iThread;

static LTThread s_hPollThread = LTHANDLE_INVALID;
static LTEvent  s_hEvent = LTHANDLE_INVALID;
static LTMutex *s_SDCardOpMutex = NULL; // Mutex protects Mount/Unmount/Format operations

// Static buffer for mount/fsck commands (max needed: ~50 chars)
static char s_mntCmd[64];

// Shared between the poll thread and app thread; access protected by s_SDCardOpMutex
static LTDeviceSDCard_SDCardInfo s_previousPollInfo;

static LTDeviceSDCard_SDCardInfo GetSDCardInfo(LTHandle hUnit);

static void NotifySDCardEvent(LTDeviceUnit hUnit) {
    LTDeviceSDCard_SDCardInfo info = GetSDCardInfo(hUnit);
    LTDeviceSDCard_SDCardInfo *infoToSend = lt_memdup(&info, sizeof(LTDeviceSDCard_SDCardInfo));
    if (!infoToSend) return; // could not malloc
    // infoToSend will be freed in DispatchComplete
    s_iEvent->NotifyEvent(s_hEvent, infoToSend);
}

LTDeviceUnit LinuxSDCardDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hUnit = LT_GetCore()->CreateHandle((LTInterface*) &s_ILTSDCardDeviceUnit, sizeof(void*));
    return hUnit;
};

static void LinuxSDCardDeviceUnit_OnDestroyHandle(LTHandle hUnit) {
    LT_UNUSED(hUnit);
}

static void OnSDCardEvent(LTDeviceUnit hUnit, LTDeviceSDCard_OnSDCardEventProc * pCallback, void * pClientData) {
    LT_UNUSED(hUnit);
    // NOTE: Call immediately so we always get state through this mechanism
    s_iEvent->RegisterForEvent(s_hEvent, (void*) pCallback, NULL, pClientData, false);
}

static void NoSDCardEvent(LTDeviceUnit hUnit, LTDeviceSDCard_OnSDCardEventProc * pCallback) {
    LT_UNUSED(hUnit);
    s_iEvent->UnregisterFromEvent(s_hEvent, (void*) pCallback);
}

static LTDeviceSDCard_SDCardInfo GetSDCardInfo(LTHandle hUnit) {
    LT_UNUSED(hUnit);
    LTDeviceSDCard_SDCardInfo info = {};

    // Check presence
    {
        struct stat statbuf = {};
        int ret = stat(SDCARD_PATH, &statbuf);
        info.present = (ret == 0);
    }

    // Check if mounted
    if (info.present) {
        FILE *f = setmntent("/proc/mounts", "r");
        if (!f) {
            return info;
        }

        char buf[100];

        struct mntent ent = {};
        while (getmntent_r(f, &ent, buf, sizeof(buf))) {
            // Check for both partition and whole device mounts
            if (strncmp(ent.mnt_fsname, SDCARD_PART_PATH, SDCARD_PART_PATH_LEN) != 0 &&
                strncmp(ent.mnt_fsname, SDCARD_PATH, SDCARD_PATH_LEN) != 0) continue;

            if (strncmp(ent.mnt_dir, MOUNTPOINT, MOUNTPOINT_LEN) != 0) {
                endmntent(f);
                LTLOG_YELLOWALERT("mnt_dir", "Device is mounted but at incorrect mountpoint");
                return info;
            }

            info.mounted = true;
            if (strncmp(ent.mnt_type, "vfat", VFAT_LEN) == 0) {
                info.fstype = LTDeviceSDCard_Format_FAT32;
            } else if (strncmp(ent.mnt_type, "exfat", EXFAT_LEN) == 0) {
                info.fstype = LTDeviceSDCard_Format_EXFAT;
            } else {
                info.fstype = LTDeviceSDCard_Format_UNKNOWN;
            }
        }

        endmntent(f);
    }

    // Check filesystem stats
    if (info.mounted) {
        struct statvfs statvfsbuf = {};
        int ret = statvfs(MOUNTPOINT, &statvfsbuf);
        if (ret < 0) {
            // statvfs can fail transiently under I/O pressure (e.g. disk full).
            // The card is still physically present (stat() succeeded above) and
            // still mounted (found in /proc/mounts above), so preserve those
            // flags. Only mark capacity/available as unknown.
            LTLOG_SERVER("statvfs", "statvfs failed: %s", strerror(errno));
            info.capacity  = 0;
            info.available = 0;
        } else {
            info.capacity = statvfsbuf.f_bsize * (u64) statvfsbuf.f_blocks;
            info.available = statvfsbuf.f_bsize * (u64) statvfsbuf.f_bfree;
        }
    }

    return info;
}

// Resolve which device path to use and try mounting with filesystem fallbacks.
// Returns 0 on success, non-zero on failure.
// When devicePath is non-NULL, mount that device directly.
// When NULL, resolve the device path internally.
static int TryMount(const char *devicePath) {
    struct stat st = {0};

    // Create the mount point directory if it does not exist
    if (stat(MOUNTPOINT, &st) == -1) {
        if (mkdir(MOUNTPOINT, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) { // 0755
            LTLOG_YELLOWALERT("mkdir.fail", "Failed to create mount point directory %s: %s", MOUNTPOINT, strerror(errno));
            return -1;
        }
    }

    if (!devicePath) {
        devicePath = (stat(SDCARD_PART_PATH, &st) == -1) ? SDCARD_PATH : SDCARD_PART_PATH;
    }

    // Try mounting with auto-detect, then vfat, then exfat
    snprintf(s_mntCmd, sizeof(s_mntCmd), "mount %s %s 2>/dev/null", devicePath, MOUNTPOINT);
    int ret = system(s_mntCmd);
    if (ret != 0) {
        snprintf(s_mntCmd, sizeof(s_mntCmd), "mount -t vfat %s %s 2>/dev/null", devicePath, MOUNTPOINT);
        ret = system(s_mntCmd);
        if (ret != 0) {
            snprintf(s_mntCmd, sizeof(s_mntCmd), "mount -t exfat %s %s 2>/dev/null", devicePath, MOUNTPOINT);
            ret = system(s_mntCmd);
        }
    }
    return ret;
}

// fsck exit codes: 0=clean, 1=corrected, 2=corrected+reboot-suggested.
// Codes < 4 indicate the filesystem is usable. system() returns a
// wait-status word, not the raw exit code, so decode with WEXITSTATUS.
static bool FsckSucceeded(int status) {
    return WIFEXITED(status) && WEXITSTATUS(status) < 4;
}

// Decode system() wait-status into a human-readable description.
static const char *FsckExitDesc(int status) {
    if (status == -1)        return "system() failed";
    if (WIFSIGNALED(status)) return "killed by signal";
    if (WIFEXITED(status))   return "exit";
    return "unknown";
}

// Full mount with pre-mount fsck — used on card insertion and boot.
// Runs fsck before mounting to repair corruption that the kernel would
// otherwise silently ignore (e.g. broken cluster chains, cross-linked
// clusters, free count mismatch). May block for several seconds on
// large or corrupt cards.
static bool Mount_Private(LTDeviceUnit hUnit) {
    struct stat st = {0};
    const char *devicePath = (stat(SDCARD_PART_PATH, &st) == -1) ? SDCARD_PATH : SDCARD_PART_PATH;

    // Run fsck before mount — the kernel mounts FAT32 even with cluster
    // chain corruption, so the old mount-then-fsck-on-failure approach
    // never triggered fsck for the corruption types that matter.
    snprintf(s_mntCmd, sizeof(s_mntCmd), "fsck.vfat -a %s 2>/dev/null", devicePath);
    int fsckRet = system(s_mntCmd);
    if (!FsckSucceeded(fsckRet)) {
        snprintf(s_mntCmd, sizeof(s_mntCmd), "fsck.exfat -a %s 2>/dev/null", devicePath);
        fsckRet = system(s_mntCmd);
    }
    if (!FsckSucceeded(fsckRet)) {
        LTLOG_YELLOWALERT("mnt.fsck.fail", "fsck failed on %s (%s %d), skipping mount",
            devicePath, FsckExitDesc(fsckRet),
            WIFEXITED(fsckRet) ? WEXITSTATUS(fsckRet) : -1);
        return false;
    }

    int ret = TryMount(devicePath);
    if (ret != 0) {
        LTLOG_YELLOWALERT("mnt.fail", "Failed to mount %s", devicePath);
        return false;
    }

    s_previousPollInfo.mounted = true;
    NotifySDCardEvent(hUnit);
    return true;
}

// Quick mount without fsck — used by the poll thread for remount recovery
// to avoid blocking the poll thread with potentially long fsck operations.
static bool MountQuick_Private(LTDeviceUnit hUnit) {
    int ret = TryMount(NULL);
    if (ret != 0) {
        LTLOG("mnt.quick.fail", "Quick remount failed");
        return false;
    }
    s_previousPollInfo.mounted = true;
    NotifySDCardEvent(hUnit);
    return true;
}

static bool Mount(LTDeviceUnit hUnit) {
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    bool success = Mount_Private(hUnit);
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    return success;
}

static bool MountQuick(LTDeviceUnit hUnit) {
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    bool success = MountQuick_Private(hUnit);
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    return success;
}

static void Unmount(LTDeviceUnit hUnit) {
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    // Always attempt to unmount with a lazy unmount, so it will succeed even if sd card was physically removed
    int ret = umount2(MOUNTPOINT, MNT_DETACH);
    if (ret == 0) {
        s_previousPollInfo.mounted = false;
        NotifySDCardEvent(hUnit);
    }
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
}

static bool RunFdisk(const char *cmd, int size) {
    FILE *f = fopen("/tmp/fdisk.cmd", "w");
    if (!f) return false;
    int count = fwrite(cmd, 1, size, f);
    fclose(f);
    if (count != size) return false;

    int ret = system("fdisk " SDCARD_PATH " < /tmp/fdisk.cmd");
    if (ret != 0) return false;

    int fd = open(SDCARD_PATH, O_RDONLY);
    if (fd == -1) return false;
    ret = ioctl(fd, BLKRRPART, 0);
    close(fd);

    return (ret == 0);
}

static bool Format(LTHandle hUnit, LTDeviceSDCard_FormatOptions options) {
    LTDeviceSDCard_SDCardInfo info = GetSDCardInfo(hUnit);
    // SD card must be present
    if (!info.present) return false;
    // SD card should be unmounted in order to format
    if (info.mounted) return false;

    bool success = false;
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    switch(options.format) {
    case LTDeviceSDCard_Format_EXFAT: {
        const char fdisk_cmd[] = "o\nn\np\n1\n \n \n \nt\n7\np\nw";
        RunFdisk(fdisk_cmd, sizeof(fdisk_cmd));
        int ret = system("mkfs.exfat " SDCARD_PART_PATH);
        if (ret != 0) goto out;
        sync();
    } break;

    case LTDeviceSDCard_Format_FAT32: {
        const char fdisk_cmd[] = "o\nn\np\n1\n \n \n \nt\nc\np\nw";
        RunFdisk(fdisk_cmd, sizeof(fdisk_cmd));
        int ret = system("mkfs.vfat " SDCARD_PART_PATH);
        if (ret != 0) goto out;
        sync();
    } break;

    default:
        goto out;
    }

    // Remount the SD card after formatting
    success = Mount_Private(hUnit);

out:
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    return success;
}

static void DispatchSDCardEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTDeviceSDCard_OnSDCardEventProc *pCallback = pEventProc;
    LTDeviceSDCard_SDCardInfo *info = LTArgs_pointerAt(0, pEventArgs);
    pCallback(info, pEventProcClientData);
}

static void DispatchSDCardEventComplete(LTEvent hEvent, LTArgs *pEventArgs) {
    LT_UNUSED(hEvent);
    LTDeviceSDCard_SDCardInfo *info = LTArgs_pointerAt(0, pEventArgs);
    lt_free(info);
}

static void SDCardPoll(void *pClientData) {
    LT_UNUSED(pClientData);

    /* Read previous state under lock (consistent snapshot), then call GetSDCardInfo
     * outside the lock — stat/getmntent/statvfs can block on a struggling card and
     * would otherwise delay app-thread Mount/Unmount. Write new state back under lock. */
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    bool previousPresentState = s_previousPollInfo.present;
    bool previousMountedState = s_previousPollInfo.mounted;
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);

    LTDeviceSDCard_SDCardInfo currentInfo = GetSDCardInfo(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    bool currentPresentState = currentInfo.present;
    bool currentMountedState = currentInfo.mounted;

    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    s_previousPollInfo = currentInfo;
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);

    /* Case 0: No state change, so no action required */
    if (previousPresentState == currentPresentState &&
        previousMountedState == currentMountedState) return;

    /* Case 1: SD card was physically removed -> Unmount()
     * Removed == state change from (present) -> (not present) */
    if (previousPresentState && !currentPresentState) {
        Unmount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        return;
    }

    /* Case 2: SD card was inserted -> try Mount()
     * Inserted == state change from (not present) -> (present) */
    if (!previousPresentState && currentPresentState) {
        bool mountSuccess = Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        // If mount fails, log and move on
        if (!mountSuccess) LTLOG_YELLOWALERT("mnt.fail", NULL);
        return;
    }

    /* Case 3: Card is present but filesystem dropped out (e.g. I/O error,
     * disk full stress) -> attempt quick remount first, then fall back to
     * full mount with fsck if the filesystem is dirty from the forced unmount. */
    if (currentPresentState && previousMountedState && !currentMountedState) {
        LTLOG_SERVER("mnt.recover", "Card present but filesystem unmounted, attempting remount");
        bool mountSuccess = MountQuick(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        if (!mountSuccess) {
            LTLOG_SERVER("mnt.recover.fsck", "Quick remount failed, attempting fsck + mount");
            mountSuccess = Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        }
        if (!mountSuccess) LTLOG_YELLOWALERT("mnt.recover.fail", NULL);
        return;
    }
    /* Case 4: Card became mounted (e.g. external mount or recovery succeeded)
     * -> notify listeners of the new state */
    if (currentPresentState && !previousMountedState && currentMountedState) {
        NotifySDCardEvent(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        return;
    }
}

static bool SDCardPollThreadStart(void) {
    static const LTArgsDescriptor args = { 1, { kLTArgType_pointer } };
    s_hEvent = LT_GetCore()->CreateEvent(&args, DispatchSDCardEventCB, DispatchSDCardEventComplete, NULL, NULL);
    return (s_hEvent != LTHANDLE_INVALID);
}

static void SDCardPollThreadStop(void) {
    LT_GetCore()->Destroy(s_hEvent);
    s_hEvent = LTHANDLE_INVALID;
}

bool LinuxSDCardDeviceUnit_Init(void) {
    s_iEvent  = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    s_hPollThread = LT_GetCore()->CreateThread("SDCardPoll");
    if (s_hPollThread == LTHANDLE_INVALID) return false;

    s_SDCardOpMutex = lt_createobject(LTMutex);
    if (!s_SDCardOpMutex) return false;

    s_previousPollInfo = GetSDCardInfo(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    if (s_previousPollInfo.present && !s_previousPollInfo.mounted) {
        Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        // Reset mounted flag so the first poll tick fires Case 4 after
        // listeners have registered.  Mount_Private sets mounted=true
        // and fires NotifySDCardEvent, but no listeners exist yet at
        // Init time (they register later in LocalRecordingController_Init).
        // Clearing the flag here makes the first poll see false->true,
        // triggering Case 4 which re-notifies with listeners present.
        s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
        s_previousPollInfo.mounted = false; /* poll thread not yet started; mutex for pattern consistency */
        s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    }

    s_iThread->Start(s_hPollThread, SDCardPollThreadStart, SDCardPollThreadStop);
    s_iThread->SetTimer(s_hPollThread, LTTime_Milliseconds(500), SDCardPoll, NULL, NULL);

    return true;
}

void LinuxSDCardDeviceUnit_Fini(void) {
    if (s_hPollThread != LTHANDLE_INVALID) {
        s_iThread->Terminate(s_hPollThread);
        s_iThread->WaitUntilFinished(s_hPollThread, LTTime_Infinite());
        s_iThread->Destroy(s_hPollThread);
        s_hPollThread = LTHANDLE_INVALID;
    }

    lt_destroyobject(s_SDCardOpMutex);
    Unmount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
}

define_LTLIBRARY_INTERFACE(ILTSDCardDeviceUnit, LinuxSDCardDeviceUnit_OnDestroyHandle)
    .OnSDCardEvent = OnSDCardEvent,
    .NoSDCardEvent = NoSDCardEvent,
    .GetSDCardInfo = GetSDCardInfo,
    .Mount         = Mount,
    .Unmount       = Unmount,
    .Format        = Format,
LTLIBRARY_DEFINITION;
