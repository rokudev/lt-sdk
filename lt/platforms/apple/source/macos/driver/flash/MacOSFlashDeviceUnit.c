/*******************************************************************************
 * platforms/macos/source/macos/driver/flash/MacOSFlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "MacOSFlashDeviceUnit.h"

enum {
    kPartByteOffset = 0x1000,  /**< LT Partition table offset */
    kFw0ByteOffset  = 0xA000,  /**< LT Partition table offset */
    kWriteQuantum   = 1        /**< No constraint */
};

typedef struct {
    /* NumSectors and SectorSize are constant after initialization */
    u32 nNumSectors;
    u32 nSectorSize;
    /* Mutex required for write protect and reference counts */
    LTMutex *mutex;
    u32 nRefCount;
} FlashInfo;

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
static char *s_pFlashFilename = NULL;
static FILE *s_pFlashFILE     = NULL;

static const char kflashFileNameExtension[] = ".flash.bin";

static FlashInfo *GetFlashInfoPtr(LTDeviceUnit hFlashDevice) { return hFlashDevice ? &s_flashInfo : NULL; }

void MacOSFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex = lt_createobject(LTMutex);
    s_flashInfo.nNumSectors = 512;  /* arbitrary */
    s_flashInfo.nSectorSize = 4096; /* arbitrary */
    s_flashInfo.nRefCount = 0;
    if (!s_pFlashFilename) {
        char * pFlashPath = getenv("LT_FLASH_FILE");
        if (pFlashPath) {
            s_pFlashFilename = (char *)lt_malloc(lt_strlen(pFlashPath) + sizeof(kflashFileNameExtension));
            if (s_pFlashFilename) {
                lt_strncpyTerm(s_pFlashFilename, pFlashPath, lt_strlen(pFlashPath) + 1);
                LT_SIZE nLen = lt_strlen(s_pFlashFilename);
                lt_strncpyTerm(s_pFlashFilename + nLen, kflashFileNameExtension, lt_strlen(kflashFileNameExtension) + 1);
            }
        } else {
            u32 nPathSize = 0;
            _NSGetExecutablePath(NULL, &nPathSize);
            s_pFlashFilename = (char *)lt_malloc(nPathSize + sizeof(kflashFileNameExtension));
            if (s_pFlashFilename) {
                _NSGetExecutablePath(s_pFlashFilename, &nPathSize);
                LT_SIZE nLen = lt_strlen(s_pFlashFilename);
                lt_strncpyTerm(s_pFlashFilename + nLen, kflashFileNameExtension, nPathSize + sizeof(kflashFileNameExtension) - nLen);
            }
        }
    }
}

void MacOSFlashDeviceUnit_Finalize(void) { lt_destroyobject(s_flashInfo.mutex); lt_free(s_pFlashFilename); s_pFlashFilename = NULL; }

/*________________________________________________________________________
 / MacOSFlashDeviceUnit ILTFlashDeviceUnit public interface functions */
static u32 MacOSFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(flashIDToSet);
    return 0;
}

static u32 MacOSFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize * pFlashInfo->nNumSectors;
}

static u32 MacOSFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nNumSectors;
}

static u32 MacOSFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize;
}

static u32 MacOSFlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? pFlashInfo->nSectorSize * nSectorNumber : 0;
}

static u32 MacOSFlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u32 nSectorNumber = nByteOffset / pFlashInfo->nSectorSize;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? nSectorNumber : 0;
}

static bool MacOSFlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashUnit, u32 *pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashUnit);
    if (!bGetPrimary) return false;
    *pByteOffset = kPartByteOffset;
    return true;
}

static bool MacOSFlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashUnit, void *pAddress, u32 *pByteOffset) {
    LT_UNUSED(hFlashUnit); LT_UNUSED(pAddress);
    /* hack to allow LTSystemUpdate to load */
    *pByteOffset = kFw0ByteOffset;
    return true;
}

static u16 MacOSFlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashUnit) { LT_UNUSED(hFlashUnit); return 1; }

static bool MacOSFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u64 nFlashSize = pFlashInfo->nNumSectors * pFlashInfo->nSectorSize;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_pFlashFILE, 0, SEEK_SET)) goto done;
    for (u32 i = 0; i < nFlashSize; ++i) {
        u8 emptyByte = 0xff;
        if (1 != fwrite(&emptyByte, sizeof(u8), 1, s_pFlashFILE)) goto done;
    }
    fflush(s_pFlashFILE);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool MacOSFlashDeviceUnit_EraseSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || nSectorNumber >= pFlashInfo->nNumSectors) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_pFlashFILE, nSectorNumber * pFlashInfo->nSectorSize, SEEK_SET)) goto done;
    for (u32 i = 0; i < pFlashInfo->nSectorSize; ++i) {
        u8 emptyByte = 0xff;
        if (1 != fwrite(&emptyByte, sizeof(u8), 1, s_pFlashFILE)) goto done;
    }
    fflush(s_pFlashFILE);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool MacOSFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= MacOSFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  MacOSFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  MacOSFlashDeviceUnit_GetNumSectors(hDevice)) return false;
    for (; nNumSectors; ++nFirstSector, --nNumSectors)
        if (!MacOSFlashDeviceUnit_EraseSector(hDevice, nFirstSector)) return false;
    return true;
}

static bool MacOSFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice); return false;
}

static bool MacOSFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice); LT_UNUSED(nSectorNumber); return false;
}

static bool MacOSFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice); LT_UNUSED(bWriteProtect); return false;
}

static bool MacOSFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice); LT_UNUSED(nSectorNumber); LT_UNUSED(bWriteProtect); return false;
}

static bool MacOSFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 *pBuff) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors *pFlashInfo->nSectorSize)) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_pFlashFILE, nByteOffset, SEEK_SET))               goto done;
    if (nNumBytes != fread(pBuff, sizeof(u8), nNumBytes, s_pFlashFILE)) goto done;
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool MacOSFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 *pBuff) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors *pFlashInfo->nSectorSize)) return false;
    bool bResult = false;
    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    if (-1 == fseek(s_pFlashFILE, nByteOffset, SEEK_SET))                goto done;
    if (nNumBytes != fwrite(pBuff, sizeof(u8), nNumBytes, s_pFlashFILE)) goto done;
    fflush(s_pFlashFILE);
    bResult = true;
done:
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

/*_________________________________________________________________________
 / MacOSFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void
MacOSFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
    FlashInfo *pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (pFlashInfo) {
        pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
        if (0 == --pFlashInfo->nRefCount) {
            fflush(s_pFlashFILE);
            fclose(s_pFlashFILE);
            s_pFlashFILE = NULL;
        }
        pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    }
}

/*__________________________________________________________
 / MacOSFlashDeviceUnit ILTFlashDeviceUnit Interface Definition */
define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, MacOSFlashDeviceUnit_OnDestroyHandle)
    .GetFlashID                 = MacOSFlashDeviceUnit_GetFlashID,
    .GetNumBytes                = MacOSFlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = MacOSFlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = MacOSFlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset   = MacOSFlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber   = MacOSFlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset    = MacOSFlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset     = MacOSFlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum            = MacOSFlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                = MacOSFlashDeviceUnit_EraseDevice,
    .EraseSectors               = MacOSFlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = MacOSFlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = MacOSFlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = MacOSFlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = MacOSFlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = MacOSFlashDeviceUnit_ReadBytes,
    .WriteBytes                 = MacOSFlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = MacOSFlashDeviceUnit_ReadBytes,
    .WriteRawBytes              = MacOSFlashDeviceUnit_WriteBytes,
LTLIBRARY_DEFINITION;

/*________________________________________
 / MacOSFlashDeviceUnit Handle Creation */

LTDeviceUnit MacOSFlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(FlashInfo *));
    if (hFlashDevice) {
        /* got a handle back with enough room to store a pointer to our data */
        FlashInfo ** ppFlashInfo = (FlashInfo **)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        *ppFlashInfo = &s_flashInfo;
        LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, ppFlashInfo);
        s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
        if (1 == ++s_flashInfo.nRefCount) {
            s_pFlashFILE = fopen(s_pFlashFilename, "r+");
            if (NULL == s_pFlashFILE) {
                s_pFlashFILE = fopen(s_pFlashFilename, "w+");
                if (NULL == s_pFlashFILE) {
                    perror(s_pFlashFilename);
                    LT_GetCore()->DebugBreak();;
                }
            }
            s64 nFlashSize = s_flashInfo.nNumSectors * s_flashInfo.nSectorSize;
            fseek(s_pFlashFILE, 0, SEEK_END);
            long int nCurSize = ftell(s_pFlashFILE);
            while (nCurSize < nFlashSize) {
                u8 emptyByte = 0xff;
                fwrite(&emptyByte, sizeof(u8), 1, s_pFlashFILE);
                ++nCurSize;
            }
            /* Write the partition table if one isn't already written */
            u32 nMagic = 0;
            fseek(s_pFlashFILE, kPartByteOffset, SEEK_SET);
            int nRtn = fread((u8 *)&nMagic, sizeof(nMagic), 1, s_pFlashFILE);
            if (nRtn == 1 && nMagic != kLTDeviceFlash_Magic_PartitionTable) {
                fseek(s_pFlashFILE, kPartByteOffset, SEEK_SET);
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
                fwrite((u8 *)&header, sizeof(header), 1, s_pFlashFILE);
                fwrite((u8 *)&s_partitions, sizeof(s_partitions), 1, s_pFlashFILE);
                fwrite((u8 *)&nCRC, sizeof(nCRC), 1, s_pFlashFILE);
            }
            fflush(s_pFlashFILE);
        }
        s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
    }
    return hFlashDevice;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-20   augustus    created
 *  11-Jan-20   augustus    created again for bifurcated platform root
 *  02-Jul-24   constantine copied from LinuxFlashDeviceUnitClearFile.c
 */
