/*******************************************************************************
 * platforms/linux/source/linux/driver/flash/LinuxFlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include "LinuxFlashDeviceUnit.h"

enum {
    kPartByteOffset = 0x1000,  /**< LT Partition table offset */
    kFw0ByteOffset  = 0xA000,  /**< LT Partition table offset */
    kWriteQuantum   = 1        /**< No constraint */
};

/*_______________________________________________
 / file LinuxFlashDeviceUnit.c private types */
typedef struct {
    /* NumSectors and SectorSize are constant after initialization */
    u32 nNumSectors;
    u32 nSectorSize;
    /* Mutex required for write protect and reference counts */
    LTMutex *mutex;
    u32 nRefCount;
} FlashInfo;

/*________________________________________________
 / file LinuxFlashDeviceUnit.c static variables */

static const LTDeviceFlash_PartitionEntry s_partitions[] = {
    { "ltat",      "ltat",     0x000000,        0x01,  0 },
    { "part",      "part",     kPartByteOffset, 0x01,  0 },
    { "ota",       "otainfo",  0x002000,        0x02,  0 },
    { "settings",  "settings", 0x004000,        0x06,  0 },
    { "fw0",       "fw",       kFw0ByteOffset,  0x01,  0 }, // dummy firmware partitions to allow LTSystemUpdate to load
    { "fw1",       "fw",       0x00B000,        0x01,  0 },
    { "crashdump", "misc",     0x00C000,        0x20,  0 },
    { "log",       "log",      0x02C000,        0x0C,  0 },
    { "migration", "misc",     0x038000,        0x0F,  0 },
    { "crypto",    "settings", 0x047000,        0x04,  0 },
    { "prov",      "misc",     0x04B000,        0x01,  0 },
};

static FlashInfo s_flashInfo;
static char s_flashFileName[PATH_MAX];
static FILE * s_flashFile = NULL;

static FlashInfo * GetFlashInfoPtr(LTDeviceUnit hFlashDevice) {
    return hFlashDevice ? &s_flashInfo : NULL;
}

void LinuxFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex = lt_createobject(LTMutex);
    s_flashInfo.nNumSectors = 512;  /* arbitrary */
    s_flashInfo.nSectorSize = 4096; /* arbitrary */
    s_flashInfo.nRefCount = 0;
}

void LinuxFlashDeviceUnit_Finalize() {
    lt_destroyobject(s_flashInfo.mutex);
}

static const char *
LinuxFlashDeviceUnit_GetExecFile(void) {
    static char s_exeFile[PATH_MAX];

    if (s_exeFile[0])
        return s_exeFile;

    ssize_t bytes = readlink("/proc/self/exe", s_exeFile, PATH_MAX);
    if (bytes < 0 || bytes >= (ssize_t)PATH_MAX) {
        s_exeFile[0] = '\0';
        return "a.out";
    }
    s_exeFile[bytes] = '\0';

    return s_exeFile;
}

/*___________________________________________________
 / LinuxFlashDeviceUnit Handle Creation function */
static const ILTFlashDeviceUnit s_ILTFlashDeviceUnit;

LTDeviceUnit
LinuxFlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(FlashInfo *));
    if (hFlashDevice) {
        /* got a handle back with enough room to store a pointer to our data */
        FlashInfo ** ppFlashInfo = (FlashInfo **)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        *ppFlashInfo = &s_flashInfo;
        LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, ppFlashInfo);
        s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
        if (1 == ++s_flashInfo.nRefCount) {
            char * pFlashPath = getenv("LT_FLASH_FILE");
            if (pFlashPath) {
                lt_strncpyTerm(s_flashFileName, pFlashPath, sizeof(s_flashFileName));
            } else {
                lt_strncpyTerm(s_flashFileName, LinuxFlashDeviceUnit_GetExecFile(), sizeof(s_flashFileName));
                LT_SIZE nLen = lt_strlen(s_flashFileName);
                lt_strncpyTerm(s_flashFileName + nLen , ".flash.bin", sizeof(s_flashFileName) - nLen);
            }
            s_flashFile = fopen(s_flashFileName, "r+");
            if (NULL == s_flashFile) {
                s_flashFile = fopen(s_flashFileName, "w+");
                if (NULL == s_flashFile) {
                    perror(s_flashFileName);
                    LT_GetCore()->DebugBreak();;
                }
            }
            s64 nFlashSize = s_flashInfo.nNumSectors * s_flashInfo.nSectorSize;
            fseek(s_flashFile, 0, SEEK_END);
            long int nCurSize = ftell(s_flashFile);
            while (nCurSize < nFlashSize) {
                u8 emptyByte = 0xff;
                fwrite(&emptyByte, sizeof(u8), 1, s_flashFile);
                ++nCurSize;
            }
            /* Write the partition table if one isn't already written */
            u32 nMagic = 0;
            fseek(s_flashFile, kPartByteOffset, SEEK_SET);
            int nRtn = fread((u8 *)&nMagic, sizeof(nMagic), 1, s_flashFile);
            if (nRtn == 1 && nMagic != kLTDeviceFlash_Magic_PartitionTable) {
                fseek(s_flashFile, kPartByteOffset, SEEK_SET);
                LTDeviceFlash_PartitionTableHeader header = {
                    kLTDeviceFlash_Magic_PartitionTable,
                    sizeof(s_partitions) / sizeof(s_partitions[0]),
                    /* Size = 2^(nDeviceSize + 6) bytes */
                    25 - __builtin_clz(s_flashInfo.nNumSectors * s_flashInfo.nSectorSize),
                    0,
                    { 0, 0, 0, 0, 0 }
                };
                u32 nCRC = 0;
                LTUtilityByteOps * pOps = (LTUtilityByteOps *)LT_GetCore()->OpenLibrary("LTUtilityByteOps");
                if (pOps) {
                    pOps->Crc32((u8 *)&header, sizeof(header), &nCRC);
                    pOps->Crc32((u8 *)&s_partitions, sizeof(s_partitions), &nCRC);
                    LT_GetCore()->CloseLibrary((LTLibrary *)pOps);
                }
                fwrite((u8 *)&header, sizeof(header), 1, s_flashFile);
                fwrite((u8 *)&s_partitions, sizeof(s_partitions), 1, s_flashFile);
                fwrite((u8 *)&nCRC, sizeof(nCRC), 1, s_flashFile);
            }
            fflush(s_flashFile);
        }
        s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
    }
    return hFlashDevice;
}

/*________________________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit public interface functions */

static u32
LinuxFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(flashIDToSet);
    return 0;
}

static u32  LinuxFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize * pFlashInfo->nNumSectors;
}

static u32  LinuxFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nNumSectors;
}

static u32  LinuxFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize;
}

static u32  LinuxFlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? pFlashInfo->nSectorSize * nSectorNumber : 0;
}

static u32  LinuxFlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u32 nSectorNumber = nByteOffset / pFlashInfo->nSectorSize;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? nSectorNumber : 0;
}

static bool
LinuxFlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    if (!bGetPrimary) return false;
    *pByteOffset = kPartByteOffset;
    return true;
}

static bool LinuxFlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * pAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(pAddress);
    // hack to allow LTSystemUpdate to load
    *pByteOffset = kFw0ByteOffset;
    return true;
}

static u16 LinuxFlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool LinuxFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u64 nFlashSize = pFlashInfo->nNumSectors * pFlashInfo->nSectorSize;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_flashFile, 0, SEEK_SET)) goto done;
    for (u32 i = 0; i < nFlashSize; ++i) {
        u8 emptyByte = 0xff;
        if (1 != fwrite(&emptyByte, sizeof(u8), 1, s_flashFile)) goto done;
    }
    fflush(s_flashFile);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool LinuxFlashDeviceUnit_EraseSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || nSectorNumber >= pFlashInfo->nNumSectors) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_flashFile, nSectorNumber * pFlashInfo->nSectorSize, SEEK_SET)) goto done;
    for (u32 i = 0; i < pFlashInfo->nSectorSize; ++i) {
        u8 emptyByte = 0xff;
        if (1 != fwrite(&emptyByte, sizeof(u8), 1, s_flashFile)) goto done;
    }
    fflush(s_flashFile);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool LinuxFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= LinuxFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  LinuxFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  LinuxFlashDeviceUnit_GetNumSectors(hDevice)) return false;
    for (; nNumSectors; ++nFirstSector, --nNumSectors)
        if (!LinuxFlashDeviceUnit_EraseSector(hDevice, nFirstSector)) return false;
    return true;
}

static bool LinuxFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return false; /* write protection not currently supported */
}

static bool LinuxFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    return false; /* write protection not currently supported */
}

static bool LinuxFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(bWriteProtect);
    return false; /* can't write protect the whole flash on this part */
}

static bool LinuxFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LT_UNUSED(bWriteProtect);
    return false;
}

static bool LinuxFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors * pFlashInfo->nSectorSize)) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_flashFile, nByteOffset, SEEK_SET))              goto done;
    if (nNumBytes != fread(pBuff, sizeof(u8), nNumBytes, s_flashFile)) goto done;
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool LinuxFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors * pFlashInfo->nSectorSize)) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_flashFile, nByteOffset, SEEK_SET))               goto done;
    if (nNumBytes != fwrite(pBuff, sizeof(u8), nNumBytes, s_flashFile)) goto done;
    fflush(s_flashFile);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

/*_________________________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void
LinuxFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (pFlashInfo) {
        pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
        if (0 == --pFlashInfo->nRefCount) {
            fflush(s_flashFile);
            fclose(s_flashFile);
            s_flashFile = NULL;
        }
        pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    }
}

/*__________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit Interface Definition */
define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, LinuxFlashDeviceUnit_OnDestroyHandle)
    .GetFlashID                  = LinuxFlashDeviceUnit_GetFlashID,
    .GetNumBytes                 = LinuxFlashDeviceUnit_GetNumBytes,
    .GetNumSectors               = LinuxFlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector           = LinuxFlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset    = LinuxFlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber    = LinuxFlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset     = LinuxFlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset      = LinuxFlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum             = LinuxFlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                 = LinuxFlashDeviceUnit_EraseDevice,
    .EraseSectors                = LinuxFlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected      = LinuxFlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected      = LinuxFlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice          = LinuxFlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector          = LinuxFlashDeviceUnit_WriteProtectSector,
    .ReadBytes                   = LinuxFlashDeviceUnit_ReadBytes,
    .WriteBytes                  = LinuxFlashDeviceUnit_WriteBytes,
    .ReadRawBytes                = LinuxFlashDeviceUnit_ReadBytes,
    .WriteRawBytes               = LinuxFlashDeviceUnit_WriteBytes,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-20   augustus    created
 *  11-Jan-20   augustus    created again for bifurcated platform root
 *  16-Feb-24   augustus    recovered for minimal platform
 *  04-Nov-24   augustus    added EraseSectors
 */
