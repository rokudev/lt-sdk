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

// Static buffer for mount commands (max needed: ~48 chars)
static char s_mntCmd[64];

// Only the polling thread should read or modify this value
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
            // Assume sd card is unmounted if statvfs fails
            LTLOG("statvfs", "statvfs failed: %s", strerror(errno));
            info = (LTDeviceSDCard_SDCardInfo){};
        } else {
            info.capacity = statvfsbuf.f_bsize * (u64) statvfsbuf.f_blocks;
            info.available = statvfsbuf.f_bsize * (u64) statvfsbuf.f_bfree;
        }
    }

    return info;
}

static bool Mount_Private(LTDeviceUnit hUnit) {
    // Create the mount point directory if it doesn't exist
    struct stat st = {0};
    if (stat(MOUNTPOINT, &st) == -1) {
        if (mkdir(MOUNTPOINT, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) { // 0755
            LTLOG_YELLOWALERT("mkdir.fail", "Failed to create mount point directory %s: %s", MOUNTPOINT, strerror(errno));
            return false;
        }
    }
    
    // Try to mount - check if partition exists first
    int ret = -1;
    const char* devicePath;
    
    if (stat(SDCARD_PART_PATH, &st) == -1) {
        // No partition, try mounting whole device
        devicePath = SDCARD_PATH;
    } else {
        // Partition exists, try mounting it
        devicePath = SDCARD_PART_PATH;
    }
    
    // Common mounting logic with filesystem fallbacks
    snprintf(s_mntCmd, sizeof(s_mntCmd), "mount %s %s 2>/dev/null", devicePath, MOUNTPOINT);
    ret = system(s_mntCmd);
    if (ret != 0) {
        snprintf(s_mntCmd, sizeof(s_mntCmd), "mount -t vfat %s %s 2>/dev/null", devicePath, MOUNTPOINT);
        ret = system(s_mntCmd);
        if (ret != 0) {
            snprintf(s_mntCmd, sizeof(s_mntCmd), "mount -t exfat %s %s 2>/dev/null", devicePath, MOUNTPOINT);
            ret = system(s_mntCmd);
        }
    }
    
    if (ret != 0) {
        LTLOG_YELLOWALERT("mnt.fail", "Failed to mount %s", devicePath);
        return false;
    }
    
    // Notify on successful mount
    NotifySDCardEvent(hUnit);
    return true;
}

static bool Mount(LTDeviceUnit hUnit) {
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    bool success = Mount_Private(hUnit);
    s_SDCardOpMutex->API->Unlock(s_SDCardOpMutex);
    return success;
}

static void Unmount(LTDeviceUnit hUnit) {
    s_SDCardOpMutex->API->Lock(s_SDCardOpMutex);
    // Always attempt to unmount with a lazy unmount, so it will succeed even if sd card was physically removed
    int ret = umount2(MOUNTPOINT, MNT_DETACH);
    if (ret == 0) NotifySDCardEvent(hUnit);
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

    /* Read previous SD card state and update */
    bool previousPresentState = s_previousPollInfo.present;
    LTDeviceSDCard_SDCardInfo currentInfo = GetSDCardInfo(DEFAULT_SDCARD_DEVICE_LTHANDLE);
    bool currentPresentState = currentInfo.present;
    s_previousPollInfo = currentInfo;

    /* Case 0: No state change, so no action required */
    if (previousPresentState == currentPresentState) return;

    /* Case 1: SD card was inserted -> try Mount()
     * Inserted == state change from (not present) -> (present) */
    if (currentPresentState == true) {
        bool mountSuccess = Mount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
        // If mount fails, log and move on
        if (!mountSuccess) LTLOG_YELLOWALERT("mnt.fail", NULL);
        return;
    }

    /* Case 2: SD card was removed -> Unmount() 
     * Removed == state change from (present) -> (not present) */
    if (currentPresentState == false) {
        Unmount(DEFAULT_SDCARD_DEVICE_LTHANDLE);
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
