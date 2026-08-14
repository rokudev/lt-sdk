/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32s3/Esp32s3FlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "lt/core/LTCore.h"
#include "lt/device/flash/LTDeviceFlash.h"

#include "Esp32s3SPIFlash.h"
#include "Esp32s3FlashDeviceUnit.h"
#include "Esp32s3SPIFlashCache.h"

DEFINE_LTLOG_SECTION("flash");

enum {
    kPrimaryPartitionTableOffset = 0x11000,
    kBackupPartitionTableOffset  = 0x12000,
    kWriteQuantum                = 32
};

typedef struct {
    LTMutex                      * mutex;
    const Esp32s3SPIFlash_Chip   * pChip;
    u32                            nNumSectors;
} FlashInfo;

// for creating device units
static const ILTFlashDeviceUnit    s_ILTFlashDeviceUnit;
static FlashInfo                   s_flashInfo;

#define LOCK_FLASH_MUTEX() if (!LT_GetCore()->InsideInterruptContext()) { s_flashInfo.mutex->API->Lock(s_flashInfo.mutex); }
#define UNLOCK_FLASH_MUTEX() if (!LT_GetCore()->InsideInterruptContext()) { s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex); }

/*******************************************************************************
 * Init
*******************************************************************************/
bool Esp32s3FlashDeviceUnit_Initialize(void) {
    // the chip description comes from the ROM, so there is no chip name to pass
    s_flashInfo.pChip = Esp32s3SPIFlash_Init(NULL);
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
void Esp32s3FlashDeviceUnit_Finalize(void) {
    if (s_flashInfo.mutex) {
        lt_destroyobject(s_flashInfo.mutex);
        s_flashInfo.mutex = NULL;
    }
}

/*******************************************************************************
 * Create device handle
*******************************************************************************/
LTDeviceUnit Esp32s3FlashDeviceUnit_CreateHandle(void) {
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

static u32 Esp32s3FlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    lt_memcpy(flashIDToSet, &s_flashInfo.pChip->deviceId, sizeof(s_flashInfo.pChip->deviceId));
    return sizeof(s_flashInfo.pChip->deviceId);
}

static u32 Esp32s3FlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->chipSize;
}

static u32 Esp32s3FlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.nNumSectors;
}

static u32 Esp32s3FlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->sectorSize;
}

static u32 Esp32s3FlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    return (nSectorNumber < Esp32s3FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? s_flashInfo.pChip->sectorSize * nSectorNumber : 0;
}

static u32 Esp32s3FlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    LT_UNUSED(hFlashDevice);
    u32 nSectorNumber = nByteOffset / s_flashInfo.pChip->sectorSize;
    return (nSectorNumber < Esp32s3FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? nSectorNumber : 0;
}

static bool Esp32s3FlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    *pByteOffset = bGetPrimary ? kPrimaryPartitionTableOffset : kBackupPartitionTableOffset;
    return true;
}

static bool Esp32s3FlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * nAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    return Esp32s3SPIFlashCache_BusAddressToByteOffset(nAddress, pByteOffset);
}

static u16 Esp32s3FlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool Esp32s3FlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_EraseChip();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= Esp32s3FlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  Esp32s3FlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  Esp32s3FlashDeviceUnit_GetNumSectors(hDevice)) return false;
    bool bSuccess = true;
    LOCK_FLASH_MUTEX();
    for (; bSuccess && nNumSectors; ++nFirstSector, --nNumSectors) bSuccess = Esp32s3SPIFlash_EraseSector(nFirstSector);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32s3SPIFlash_WriteProtect() : Esp32s3SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32s3SPIFlash_WriteProtect() : Esp32s3SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_Read(nByteOffset, pBuff, nNumBytes, Esp32s3SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_Write(nByteOffset, pBuff, nNumBytes, Esp32s3SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_ReadRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_Read(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32s3FlashDeviceUnit_WriteRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32s3SPIFlash_Write(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit)
    .GetFlashID                 = Esp32s3FlashDeviceUnit_GetFlashID,
    .GetNumBytes                = Esp32s3FlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = Esp32s3FlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = Esp32s3FlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset   = Esp32s3FlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber   = Esp32s3FlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset    = Esp32s3FlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset     = Esp32s3FlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum            = Esp32s3FlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                = Esp32s3FlashDeviceUnit_EraseDevice,
    .EraseSectors               = Esp32s3FlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = Esp32s3FlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = Esp32s3FlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = Esp32s3FlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = Esp32s3FlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = Esp32s3FlashDeviceUnit_ReadBytes,
    .WriteBytes                 = Esp32s3FlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = Esp32s3FlashDeviceUnit_ReadRawBytes,
    .WriteRawBytes              = Esp32s3FlashDeviceUnit_WriteRawBytes,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 */
