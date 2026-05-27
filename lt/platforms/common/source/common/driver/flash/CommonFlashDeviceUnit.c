/*******************************************************************************
 * platforms/common/source/common/driver/flash/CommonFlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include "CommonFlashDeviceUnit.h"

/*_______________________________________________
 / file CommonFlashDeviceUnit.c private types */
typedef struct {
    /* NumSectors and SectorSize are constant after initialization */
    u32 nNumSectors;
    u32 nSectorSize;
    /* Mutex required for write protect and reference counts */
    LTMutex *mutex;
    u32 nRefCount;
} FlashInfo;

static FlashInfo s_flashInfo;

static FlashInfo * GetFlashInfoPtr(LTDeviceUnit hFlashDevice) {
    return hFlashDevice ? &s_flashInfo : NULL;
}

void CommonFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex = lt_createobject(LTMutex);
    s_flashInfo.nNumSectors = 0;
    s_flashInfo.nSectorSize = 4096; /* arbitrary */
    s_flashInfo.nRefCount = 0;
}

/*___________________________________________________
 / CommonFlashDeviceUnit Handle Creation function */
static const ILTFlashDeviceUnit s_ILTFlashDeviceUnit;

LTDeviceUnit
CommonFlashDeviceUnit_CreateHandle(void) {
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
 / CommonFlashDeviceUnit ILTFlashDeviceUnit public interface functions */
static u32
CommonFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(flashIDToSet);
    return 0;
}

static u32  CommonFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize * pFlashInfo->nNumSectors;
}

static u32  CommonFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nNumSectors;
}

static u32  CommonFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize;
}

static u32  CommonFlashDeviceUnit_SectorNumberToByteAddress(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? pFlashInfo->nSectorSize * nSectorNumber : 0;
}

static u32  CommonFlashDeviceUnit_ByteAddressToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteAddress) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u32 nSectorNumber = nByteAddress / pFlashInfo->nSectorSize;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? nSectorNumber : 0;
}

static bool CommonFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    /* Perform the whole flash erase */
    return false;
}

static bool CommonFlashDeviceUnit_EraseSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo || nSectorNumber >= pFlashInfo->nNumSectors) return false;
    return false;
}

static bool CommonFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= CommonFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  CommonFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  CommonFlashDeviceUnit_GetNumSectors(hDevice)) return false;
    for (; nNumSectors; ++nFirstSector, --nNumSectors)
        if (!CommonFlashDeviceUnit_EraseSector(hDevice, nFirstSector)) return false;
    return true;
}

static bool CommonFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return false; /* write protection not currently supported */
}

static bool CommonFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    return false; /* write protection not currently supported */
}

static bool CommonFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(bWriteProtect);
    return false; /* can't write protect the whole flash on this part */
}

static bool CommonFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LT_UNUSED(bWriteProtect);
    return false;
}

static bool CommonFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteAddress, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nByteAddress);
    LT_UNUSED(nNumBytes);
    LT_UNUSED(pBuff);
    return false;
}

static bool CommonFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteAddress, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nByteAddress);
    LT_UNUSED(nNumBytes);
    LT_UNUSED(pBuff);
    return false;
}

/*_________________________________________________________________________
 / CommonFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void
CommonFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
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
 / CommonFlashDeviceUnit ILTFlashDeviceUnit Interface Definition */
define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, CommonFlashDeviceUnit_OnDestroyHandle) {
    .GetFlashID                 = CommonFlashDeviceUnit_GetFlashID,
    .GetNumBytes                = CommonFlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = CommonFlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = CommonFlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteAddress  = CommonFlashDeviceUnit_SectorNumberToByteAddress,
    .ByteAddressToSectorNumber  = CommonFlashDeviceUnit_ByteAddressToSectorNumber,
    .EraseDevice                = CommonFlashDeviceUnit_EraseDevice,
    .EraseSectors               = CommonFlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = CommonFlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = CommonFlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = CommonFlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = CommonFlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = CommonFlashDeviceUnit_ReadBytes,
    .WriteBytes                 = CommonFlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = CommonFlashDeviceUnit_ReadBytes,
    .WriteRawBytes              = CommonFlashDeviceUnit_WriteBytes,
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  11-Nov-20   augustus    created
 *  11-Jan-20   augustus    created again for bifurcated platform root
 */
