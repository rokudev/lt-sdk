/*******************************************************************************
 * platforms/st/source/st/driver/flash/STFlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include "STFlashDeviceUnit.h"

enum {
    kPrimaryPartitionTableOffset = 0x1000,
    kBackupPartitionTableOffset  = 0x2000,
    kWriteQuantum                = 1
};

/*__________________________________________
 / file STFlashDeviceUnit.c private types */
typedef struct {
    /* NumSectors and SectorSize are constant after initialization */
    u32 nNumSectors;
    u32 nSectorSize;
    /* Mutex required for write protect and reference counts */
    LTMutex * mutex;
    u32 nRefCount;
} FlashInfo;

static FlashInfo s_flashInfo;

static FlashInfo * GetFlashInfoPtr(LTDeviceUnit hFlashDevice) {
    return hFlashDevice ? &s_flashInfo : NULL;
}

void STFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex = lt_createobject(LTMutex);
    s_flashInfo.nNumSectors = 0;
    s_flashInfo.nSectorSize = 4096; /* arbitrary */
    s_flashInfo.nRefCount = 0;
}

/*___________________________________________________
 / STFlashDeviceUnit Handle Creation function */
static const ILTFlashDeviceUnit s_ILTFlashDeviceUnit;

LTDeviceUnit
STFlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(FlashInfo *));
    if (hFlashDevice) {
        /* got a handle back with enough room to store a pointer to our data */
        FlashInfo ** ppFlashInfo = (FlashInfo **)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        if (ppFlashInfo) {
            *ppFlashInfo = &s_flashInfo;
            s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
            if (1 == ++s_flashInfo.nRefCount) {
                /* first client, perform first client init, perhaps waking up the chip from low power mode */
            }
            s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
            LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, ppFlashInfo);
        }
    }
    return hFlashDevice;
}

/*________________________________________________________________________
 / STFlashDeviceUnit ILTFlashDeviceUnit public interface functions */
static u32
STFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(flashIDToSet);
    return 0;
}

static u32  STFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize * pFlashInfo->nNumSectors;
}

static u32  STFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nNumSectors;
}

static u32  STFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize;
}

static u32 STFlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    return (nSectorNumber < STFlashDeviceUnit_GetNumSectors(hFlashDevice)) ? s_flashInfo.nSectorSize * nSectorNumber : 0;
}

static u32 STFlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    LT_UNUSED(hFlashDevice);
    u32 nSectorNumber = nByteOffset / s_flashInfo.nSectorSize;
    return (nSectorNumber < STFlashDeviceUnit_GetNumSectors(hFlashDevice)) ? nSectorNumber : 0;
}

static bool STFlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    *pByteOffset = bGetPrimary ? kPrimaryPartitionTableOffset : kBackupPartitionTableOffset;
    return true;
}

static bool STFlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * nAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nAddress);
    LT_UNUSED(pByteOffset);
    return false;
}

static u16 STFlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool STFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    /* Perform the whole flash erase */
    return false;
}

static bool STFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= STFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  STFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  STFlashDeviceUnit_GetNumSectors(hDevice)) return false;
    bool bSuccess = false;
    //s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
    //for (; bSuccess && nNumSectors; ++nFirstSector, --nNumSectors) bSuccess = Esp32SPIFlash_EraseSector(nFirstSector);
    //s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
    return bSuccess;
}

static bool STFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return false; /* write protection not currently supported */
}

static bool STFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    return false; /* write protection not currently supported */
}

static bool STFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(bWriteProtect);
    return false; /* can't write protect the whole flash on this part */
}

static bool STFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LT_UNUSED(bWriteProtect);
    return false;
}

static bool STFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteAddress, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nByteAddress);
    LT_UNUSED(nNumBytes);
    LT_UNUSED(pBuff);
    return false;
}

static bool STFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteAddress, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nByteAddress);
    LT_UNUSED(nNumBytes);
    LT_UNUSED(pBuff);
    return false;
}

/*_________________________________________________________________________
 / STFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void
STFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (pFlashInfo) {
        pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
        if (0 == --pFlashInfo->nRefCount) {
            /* free some resource - maybe turn off the flash chip to save power */
            /* NOTE: Cannot turn off XIP flash */
        }
        pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    }
}

/*__________________________________________________________
 / STFlashDeviceUnit ILTFlashDeviceUnit Interface Definition */
define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, STFlashDeviceUnit_OnDestroyHandle) 

    .GetFlashID                 = STFlashDeviceUnit_GetFlashID,
    .GetNumBytes                = STFlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = STFlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = STFlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset   = STFlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber   = STFlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset    = STFlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset     = STFlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum            = STFlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                = STFlashDeviceUnit_EraseDevice,
    .EraseSectors               = STFlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = STFlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = STFlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = STFlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = STFlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = STFlashDeviceUnit_ReadBytes,
    .WriteBytes                 = STFlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = STFlashDeviceUnit_ReadBytes,
    .WriteRawBytes              = STFlashDeviceUnit_WriteBytes,

LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Feb-26   augustus    created
 */
