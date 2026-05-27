/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32FlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "lt/core/LTCore.h"
#include "lt/device/flash/LTDeviceFlash.h"

#include "Esp32SPIFlash.h"
#include "Esp32FlashDeviceUnit.h"
#include "Esp32SPIFlashCache.h"

DEFINE_LTLOG_SECTION("flash");

enum {
    kPrimaryPartitionTableOffset = 0x11000,
    kBackupPartitionTableOffset  = 0x12000,
    kWriteQuantum                = 32
};

typedef struct {
    LTMutex                      * mutex;
    const Esp32SPIFlash_Chip     * pChip;
    u32                            nNumSectors;
} FlashInfo;

// FIXME: use the Chip Device ID to detect the flash type
static const char                * s_pChipName    = NULL;

// for creating device units
static const ILTFlashDeviceUnit    s_ILTFlashDeviceUnit;
static FlashInfo                   s_flashInfo;

#define LOCK_FLASH_MUTEX() if (!LT_GetCore()->InsideInterruptContext()) { s_flashInfo.mutex->API->Lock(s_flashInfo.mutex); }
#define UNLOCK_FLASH_MUTEX() if (!LT_GetCore()->InsideInterruptContext()) { s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex); }

/*******************************************************************************
 * Init
*******************************************************************************/
bool Esp32FlashDeviceUnit_Initialize(void) {
    s_flashInfo.pChip = Esp32SPIFlash_Init(s_pChipName);
    if (s_flashInfo.pChip == NULL) {
        LTLOG_REDALERT("fail.flash.init", "Failed to initialize the flash driver");
    } else {
        s_flashInfo.mutex      = lt_createobject(LTMutex);
        s_flashInfo.nNumSectors = s_flashInfo.pChip->chipSize / s_flashInfo.pChip->sectorSize;
#ifdef LT_DEBUG
        LTLOG_DEBUG("chip", "ID: %06lX (%lu KiB)", LT_Pu32(s_flashInfo.pChip->deviceId), LT_Pu32(s_flashInfo.pChip->chipSize >> 10));
#endif
    }
    return (s_flashInfo.pChip != NULL);
}

/*******************************************************************************
 * Finalize
*******************************************************************************/
void Esp32FlashDeviceUnit_Finalize(void) {
    if (s_flashInfo.mutex) {
        lt_destroyobject(s_flashInfo.mutex);
        s_flashInfo.mutex = NULL;
    }
}

/*******************************************************************************
 * Create device handle
*******************************************************************************/
LTDeviceUnit Esp32FlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(LTDeviceUnit));
    if (hFlashDevice) {
        // handle private data is not used, so just set it to the handle
        u32 *pPrivateData = (u32 *)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        if (pPrivateData) {
            *pPrivateData = hFlashDevice;
            LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, pPrivateData);
        }
    }
    return hFlashDevice;
}

/*******************************************************************************
 * Implementation functions
*******************************************************************************/

static u32 Esp32FlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    lt_memcpy(flashIDToSet, &s_flashInfo.pChip->deviceId, sizeof(s_flashInfo.pChip->deviceId));
    return sizeof(s_flashInfo.pChip->deviceId);
}

static u32 Esp32FlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->chipSize;
}

static u32 Esp32FlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.nNumSectors;
}

static u32 Esp32FlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->sectorSize;
}

static u32 Esp32FlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    return (nSectorNumber < Esp32FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? s_flashInfo.pChip->sectorSize * nSectorNumber : 0;
}

static u32 Esp32FlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    LT_UNUSED(hFlashDevice);
    u32 nSectorNumber = nByteOffset / s_flashInfo.pChip->sectorSize;
    return (nSectorNumber < Esp32FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? nSectorNumber : 0;
}

static bool Esp32FlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    *pByteOffset = bGetPrimary ? kPrimaryPartitionTableOffset : kBackupPartitionTableOffset;
    return true;
}

static bool Esp32FlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * nAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    return Esp32SPIFlashCache_BusAddressToByteOffset(nAddress, pByteOffset);
}

static u16 Esp32FlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool Esp32FlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_EraseChip();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= Esp32FlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  Esp32FlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  Esp32FlashDeviceUnit_GetNumSectors(hDevice)) return false;
    bool bSuccess = true;
    LOCK_FLASH_MUTEX();
    for (; bSuccess && nNumSectors; ++nFirstSector, --nNumSectors) bSuccess = Esp32SPIFlash_EraseSector(nFirstSector);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32SPIFlash_WriteProtect() : Esp32SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32SPIFlash_WriteProtect() : Esp32SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_Read(nByteOffset, pBuff, nNumBytes, Esp32SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_Write(nByteOffset, pBuff, nNumBytes, Esp32SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_ReadRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_Read(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32FlashDeviceUnit_WriteRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32SPIFlash_Write(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit)
    .GetFlashID                 = Esp32FlashDeviceUnit_GetFlashID,
    .GetNumBytes                = Esp32FlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = Esp32FlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = Esp32FlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset   = Esp32FlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber   = Esp32FlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset    = Esp32FlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset     = Esp32FlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum            = Esp32FlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                = Esp32FlashDeviceUnit_EraseDevice,
    .EraseSectors               = Esp32FlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = Esp32FlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = Esp32FlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = Esp32FlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = Esp32FlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = Esp32FlashDeviceUnit_ReadBytes,
    .WriteBytes                 = Esp32FlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = Esp32FlashDeviceUnit_ReadRawBytes,
    .WriteRawBytes              = Esp32FlashDeviceUnit_WriteRawBytes,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-May-22   vitellius   created
 */
