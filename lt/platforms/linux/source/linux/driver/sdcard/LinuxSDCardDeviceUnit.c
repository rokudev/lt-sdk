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
#include <sys/syscall.h>
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

// stderr of each failed mount command, for the mnt.fail log
static char s_mountOutput[256];

enum {
    kMaxMountAttempts     = 3,      // including the first
    kMountRetryIntervalMs = 20000,
    kUnmountBusyRetries   = 5,      // clean-unmount retries on EBUSY
    kUnmountBusyWaitUs    = 200000,
};

// Mount attempts made so far; only touched on the poll thread
// (or in Init before polling is armed).
static int s_mountAttempt = 0;

// Shared between the poll thread and app thread; access protected by s_SDCardOpMutex
static LTDeviceSDCard_SDCardInfo s_previousPollInfo;

static LTDeviceSDCard_SDCardInfo GetSDCardInfo(LTHandle hUnit);
static void MountRetryTimerProc(void *pClientData);
static void StartMountRetry(int attemptsMade);

static void NotifySDCardEvent(LTDeviceUnit hUnit) {
    if (s_hEvent == LTHANDLE_INVALID) return; // event destroyed at shutdown; nothing would free the payload
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

// Runs "mount [-t type] device mountpoint". On failure appends its stderr,
// tagged with label, to s_mountOutput so every attempt's reason is kept.
static int RunMount(const char *typeArgs, const char *label, const char *devicePath) {
    snprintf(s_mntCmd, sizeof(s_mntCmd), "mount %s%s %s 2>&1", typeArgs, devicePath, MOUNTPOINT);
    FILE *p = popen(s_mntCmd, "r");
    if (!p) return -1;
    char out[128];
    LT_SIZE n = fread(out, 1, sizeof(out) - 1, p);
    out[n] = '\0';
    char drain[64];
    while (fread(drain, 1, sizeof(drain), p) > 0) {}
    if (n > 0 && out[n - 1] == '\n') out[n - 1] = '\0';
    int ret = pclose(p);
    if (ret != 0) {
        LT_SIZE len = strlen(s_mountOutput);
        snprintf(s_mountOutput + len, sizeof(s_mountOutput) - len, "%s%s: %.80s", len ? "; " : "", label,
            out[0] ? out : "no output");
    }
    return ret;
}

// Resolve which device path to use and try mounting with filesystem fallbacks.
// Returns 0 on success, non-zero on failure.
// When devicePath is non-NULL, mount that device directly.
// When NULL, resolve the device path internally.
static int TryMount(const char *devicePath) {
    struct stat st = {0};
    s_mountOutput[0] = '\0';

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
    int ret = RunMount("", "auto", devicePath);
    if (ret != 0) ret = RunMount("-t vfat ", "vfat", devicePath);
    if (ret != 0) ret = RunMount("-t exfat ", "exfat", devicePath);
    return ret;
}

static u64 ReadSysfsU64(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[32];
    u64 v = fgets(buf, sizeof(buf), f) ? (u64)strtoull(buf, NULL, 10) : 0;
    fclose(f);
    return v;
}

static void ReadSysfsString(const char *path, char *out, LT_SIZE size) {
    snprintf(out, size, "?");
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)size, f)) out[strcspn(out, "\n")] = '\0';
    fclose(f);
}

// Logs which card this is, to correlate failures with card make and model.
static void LogCardIdentity(const char *tag) {
    char name[32], manfid[16], oemid[16], date[16];
    ReadSysfsString("/sys/block/mmcblk0/device/name", name, sizeof(name));
    ReadSysfsString("/sys/block/mmcblk0/device/manfid", manfid, sizeof(manfid));
    ReadSysfsString("/sys/block/mmcblk0/device/oemid", oemid, sizeof(oemid));
    ReadSysfsString("/sys/block/mmcblk0/device/date", date, sizeof(date));
    LTLOG_SERVER(tag, "name=%s manfid=%s oemid=%s date=%s sectors=%llu p1start=%llu", name, manfid, oemid, date,
        LT_Pu64(ReadSysfsU64("/sys/block/mmcblk0/size")), LT_Pu64(ReadSysfsU64("/sys/block/mmcblk0/mmcblk0p1/start")));
}

// Reads sector 0 of a device; returns the errno on failure, 0 on success.
static int ReadBootSector(const char *devicePath, u8 *sector, LT_SIZE size) {
    int fd = open(devicePath, O_RDONLY);
    LT_SSIZE n = (fd >= 0) ? read(fd, sector, size) : -1;
    int err = (n < 0) ? errno : EIO;  // a short read has no errno
    if (fd >= 0) close(fd);
    return (n == (LT_SSIZE)size) ? 0 : err;
}

// Logs sector 0 of a device: tells a zeroed boot sector from a damaged filesystem.
static void LogBootSector(const char *devicePath) {
    u8 sector[512];
    int err = ReadBootSector(devicePath, sector, sizeof(sector));
    if (err) {
        LTLOG_SERVER("mnt.bootsector", "%s: read failed (errno %d)", devicePath, err);
        return;
    }
    bool zeroed = true;
    for (LT_SIZE i = 0; i < sizeof(sector) && zeroed; i++) zeroed = (sector[i] == 0);
    LTLOG_SERVER("mnt.bootsector", "%s: %s jmp=%02x sig=%02x%02x", devicePath, zeroed ? "ZEROED" : "nonzero",
        sector[0], sector[510], sector[511]);
}

// mount(8) usually only says "wrong fs type"; the kernel log has the real reason.
static void LogKernelMessages(void) {
    FILE *p = popen("dmesg 2>/dev/null | grep -E 'mmc|FAT-fs|exFAT|exfat' | tail -n 3", "r");
    if (!p) return;
    char buf[256];
    LT_SIZE n = fread(buf, 1, sizeof(buf) - 1, p);
    pclose(p);
    buf[n] = '\0';
    for (LT_SIZE i = 0; i < n; i++) if (buf[i] == '\n') buf[i] = '|';
    LTLOG_SERVER("mnt.kmsg", "%s", n ? buf : "none");
}

// Diagnostics for a card that would not mount.
static void LogMountFailureContext(const char *devicePath) {
    LogCardIdentity("mnt.card.fail");
    LogBootSector(devicePath);
    LogKernelMessages();
}

// Picks the checker for a device. An exFAT boot sector carries "EXFAT   " at
// offset 3 (required by the spec); anything else is checked as FAT, with
// fsck.exfat as the fallback since a damaged boot sector can hide an exFAT card
// whose boot region fsck.exfat restores from its backup.
static void PickFsckTypes(const char *devicePath, const char **primary, const char **fallback) {
    u8 sector[512];
    bool exfat = ReadBootSector(devicePath, sector, sizeof(sector)) == 0 && memcmp(sector + 3, "EXFAT   ", 8) == 0;
    *primary = exfat ? "exfat" : "vfat";
    *fallback = exfat ? NULL : "exfat";
}

static int RunFsck(const char *fsckType, const char *devicePath) {
    snprintf(s_mntCmd, sizeof(s_mntCmd), "fsck.%s -a %s 2>/dev/null", fsckType, devicePath);
    return system(s_mntCmd);
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
    const char *fsckType, *fallbackType;
    PickFsckTypes(devicePath, &fsckType, &fallbackType);
    int fsckRet = RunFsck(fsckType, devicePath);
    if (!FsckSucceeded(fsckRet) && fallbackType) {
        LTLOG_SERVER("mnt.fsck.retry", "fsck.%s failed (%s %d), trying fsck.%s", fsckType, FsckExitDesc(fsckRet),
            WIFEXITED(fsckRet) ? WEXITSTATUS(fsckRet) : -1, fallbackType);
        fsckType = fallbackType;
        fsckRet = RunFsck(fsckType, devicePath);
    }
    if (!FsckSucceeded(fsckRet)) {
        LTLOG_YELLOWALERT("mnt.fsck.fail", "fsck.%s failed on %s (%s %d), skipping mount",
            fsckType, devicePath, FsckExitDesc(fsckRet),
            WIFEXITED(fsckRet) ? WEXITSTATUS(fsckRet) : -1);
        LogMountFailureContext(devicePath);
        return false;
    }
    LTLOG_SERVER("mnt.fsck.ok", "fsck.%s on %s (exit %d)", fsckType, devicePath, WEXITSTATUS(fsckRet));

    int ret = TryMount(devicePath);
    if (ret != 0) {
        LTLOG_YELLOWALERT("mnt.fail", "Failed to mount %s: %s", devicePath,
            s_mountOutput[0] ? s_mountOutput : "unknown");
        LogMountFailureContext(devicePath);
        return false;
    }

    // busybox's mount(8) silently falls back to read-only on EACCES/EROFS
    // instead of failing -- treat that as a mount failure too, so a
    // transiently write-protected card gets retried instead of silently
    // left unusable for recording.
    struct statvfs vfs;
    int vfsRet = statvfs(MOUNTPOINT, &vfs);
    if (vfsRet != 0 || (vfs.f_flag & ST_RDONLY)) {
        // An unverifiable mount (statvfs failed) is treated the same as a
        // confirmed read-only one -- accepting it on faith risks recording
        // silently failing to write.
        if (vfsRet != 0) {
            LTLOG_YELLOWALERT("mnt.fail.statvfs", "Cannot verify %s is writable: %s", devicePath, strerror(errno));
        } else {
            LTLOG_YELLOWALERT("mnt.fail.ro", "Mounted %s read-only, treating as failure", devicePath);
        }
        LogMountFailureContext(devicePath);
        // EINVAL means it's already unmounted, which is fine.
        if (umount2(MOUNTPOINT, MNT_DETACH) != 0 && errno != EINVAL) {
            LTLOG_YELLOWALERT("mnt.fail.ro.umount", "Failed to unmount %s: %s", devicePath, strerror(errno));
        }
        return false;
    }
    LogCardIdentity("mnt.card");

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

// Stops the mount-retry timer so a pending retry can't remount the card after an
// eject or during a format. Callers must not hold s_SDCardOpMutex: KillTimer()
// waits for an in-flight retry, which needs it.
static void CancelMountRetryTimer(void) {
    if (s_hPollThread != LTHANDLE_INVALID) s_iThread->KillTimer(s_hPollThread, MountRetryTimerProc, NULL);
}

static void Unmount(LTDeviceUnit hUnit) {
    CancelMountRetryTimer();
    LTTime start = LT_GetCore()->GetKernelTime();

    // Flush the card's filesystem before unmounting, outside the lock so Mount/
    // Format aren't held up. Skipped if the card is gone (a sync can only fail)
    // or nothing is mounted at MOUNTPOINT (it would sync the root filesystem).
    struct stat mountSt, parentSt;
    if (access(SDCARD_PATH, F_OK) == 0 && stat(MOUNTPOINT, &mountSt) == 0 && stat("/", &parentSt) == 0 &&
        mountSt.st_dev != parentSt.st_dev) {
        int fd = open(MOUNTPOINT, O_RDONLY);
        if (fd >= 0) {
#ifdef SYS_syncfs
            if (syscall(SYS_syncfs, fd) != 0) {
                LTLOG_YELLOWALERT("umount.sync.fail", "syncfs failed: %s", strerror(errno));
            }
#else
            sync();
#endif
            close(fd);
        }
    }
    LTTime synced = LT_GetCore()->GetKernelTime();

    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    // Prefer a clean unmount, waiting briefly for files to be released. A lazy
    // detach leaves dirty data in flight, so the user can pull the card too
    // early; it is only the fallback (and what works if the card was removed).
    int ret = -1, err = 0;
    for (int i = 0; i < kUnmountBusyRetries; i++) {
        ret = umount2(MOUNTPOINT, 0);
        err = (ret == 0) ? 0 : errno;
        if (ret == 0 || err != EBUSY) break;
        usleep(kUnmountBusyWaitUs);
    }
    bool lazy = false;
    if (ret != 0 && err != EINVAL) { // EINVAL: not mounted
        lazy = true;
        ret = umount2(MOUNTPOINT, MNT_DETACH);
    }
    if (ret == 0) {
        s_previousPollInfo.mounted = false;
        NotifySDCardEvent(hUnit);
    }
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);

    LTTime end = LT_GetCore()->GetKernelTime();
    LTLOG_SERVER("umount", "mode=%s result=%d cleanErrno=%d sync=%lldms total=%lldms", lazy ? "lazy" : "clean", ret, err,
        LT_Ps64(LTTime_GetMilliseconds(LTTime_Subtract(synced, start))),
        LT_Ps64(LTTime_GetMilliseconds(LTTime_Subtract(end, start))));
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

// Partitions the card with fdisk, then runs mkfs, logging each step and where
// partition 1 starts before and after (a stale partition node would put the
// filesystem somewhere the new table doesn't point to).
static int PartitionAndMkfs(const char *fdiskCmd, int size, const char *mkfsCmd) {
    u64 startBefore = ReadSysfsU64("/sys/block/mmcblk0/mmcblk0p1/start");
    bool fdiskOk = RunFdisk(fdiskCmd, size);
    u64 startAfter = ReadSysfsU64("/sys/block/mmcblk0/mmcblk0p1/start");
    LTLOG_SERVER("fmt.fdisk", "ok=%d p1start before=%llu after=%llu", fdiskOk,
        LT_Pu64(startBefore), LT_Pu64(startAfter));
    int ret = system(mkfsCmd);
    LTLOG_SERVER("fmt.mkfs", "%s ret=%d", mkfsCmd, ret);
    return ret;
}

static bool Format(LTHandle hUnit, LTDeviceSDCard_FormatOptions options) {
    // Before the checks below: an in-flight retry may still mount the card.
    CancelMountRetryTimer();
    LTDeviceSDCard_SDCardInfo info = GetSDCardInfo(hUnit);
    // SD card must be present
    if (!info.present) return false;
    // SD card should be unmounted in order to format
    if (info.mounted) return false;

    bool success = false;
    bool mountTried = false;
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    switch(options.format) {
    case LTDeviceSDCard_Format_EXFAT: {
        const char fdisk_cmd[] = "o\nn\np\n1\n \n \n \nt\n7\np\nw";
        int ret = PartitionAndMkfs(fdisk_cmd, sizeof(fdisk_cmd), "mkfs.exfat " SDCARD_PART_PATH);
        if (ret != 0) goto out;
        sync();
    } break;

    case LTDeviceSDCard_Format_FAT32: {
        const char fdisk_cmd[] = "o\nn\np\n1\n \n \n \nt\nc\np\nw";
        int ret = PartitionAndMkfs(fdisk_cmd, sizeof(fdisk_cmd), "mkfs.vfat " SDCARD_PART_PATH);
        if (ret != 0) goto out;
        sync();
    } break;

    default:
        goto out;
    }

    // Remount the SD card after formatting
    mountTried = true;
    success = Mount_Private(hUnit);

out:
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    // A failed format leaves the card unmounted with no state change for the
    // poll thread to react to, so retry the mount (the timer was cancelled above).
    if (!success && GetSDCardInfo(hUnit).present) StartMountRetry(mountTried ? 1 : 0);
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

// The poll thread only reacts to state transitions, and "still unmounted" is
// not one, so a failed mount is retried on a timer.
// attemptsMade: mount attempts already made for this problem (0 if none was tried).
static void StartMountRetry(int attemptsMade) {
    s_mountAttempt = attemptsMade;
    s_iThread->SetTimer(s_hPollThread, LTTime_Milliseconds(kMountRetryIntervalMs), MountRetryTimerProc, NULL, NULL);
}

static void StopMountRetry(void) {
    CancelMountRetryTimer();
    s_mountAttempt = 0;
}

static void MountRetryTimerProc(void *pClientData) {
    LT_UNUSED(pClientData);
    s_mountAttempt++;
    LTLOG_SERVER("mnt.retry", "attempt %d/%d", s_mountAttempt, kMaxMountAttempts);

    // Under the lock so a concurrent Format() can't mount the card between the
    // check and Mount_Private(), which would fsck a live filesystem.
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    LTDeviceSDCard_SDCardInfo info = GetSDCardInfo(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    bool done = !info.present || info.mounted || Mount_Private(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);

    if (done) {
        StopMountRetry();
    } else if (s_mountAttempt >= kMaxMountAttempts) {
        // SetTimer() repeats until killed, so stop it once retries are exhausted.
        LTLOG_YELLOWALERT("mnt.give_up", "Giving up after %d attempts", kMaxMountAttempts);
        StopMountRetry();
    }
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
        StopMountRetry();
        Unmount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        return;
    }

    /* Case 2: SD card was inserted -> try Mount()
     * Inserted == state change from (not present) -> (present) */
    if (!previousPresentState && currentPresentState) {
        bool mountSuccess = Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        if (!mountSuccess) {
            LTLOG_YELLOWALERT("mnt.fail", NULL);
            StartMountRetry(1);
        }
        return;
    }

    /* Case 3: Card is present but filesystem dropped out (e.g. I/O error,
     * disk full stress) -> full fsck + mount, retrying on failure. A mount
     * without fsck could let a corrupted filesystem through. */
    if (currentPresentState && previousMountedState && !currentMountedState) {
        LTLOG_SERVER("mnt.recover", "Card present but filesystem unmounted, attempting remount");
        if (!Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE)) {
            LTLOG_YELLOWALERT("mnt.recover.fail", NULL);
            StartMountRetry(1);
        }
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
    bool mountFailed = false;
    if (s_previousPollInfo.present && !s_previousPollInfo.mounted) {
        mountFailed = !Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    }

    s_iThread->Start(s_hPollThread, SDCardPollThreadStart, SDCardPollThreadStop);
    if (mountFailed) StartMountRetry(1);
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

    Unmount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    lt_destroyobject(s_SDCardOpMutex);
}

define_LTLIBRARY_INTERFACE(ILTSDCardDeviceUnit, LinuxSDCardDeviceUnit_OnDestroyHandle)
    .OnSDCardEvent = OnSDCardEvent,
    .NoSDCardEvent = NoSDCardEvent,
    .GetSDCardInfo = GetSDCardInfo,
    .Mount         = Mount,
    .Unmount       = Unmount,
    .Format        = Format,
LTLIBRARY_DEFINITION;
