/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32p4/Esp32p4FlashDeviceUnit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "lt/core/LTCore.h"
#include "lt/device/flash/LTDeviceFlash.h"

#include "Esp32p4SPIFlash.h"
#include "Esp32p4FlashDeviceUnit.h"
#include "Esp32p4SPIFlashCache.h"

DEFINE_LTLOG_SECTION("flash");

enum {
    kPrimaryPartitionTableOffset = 0x11000,
    kBackupPartitionTableOffset  = 0x12000,
    kWriteQuantum                = 32
};

typedef struct {
    LTMutex                      * mutex;
    const Esp32p4SPIFlash_Chip   * pChip;
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
bool Esp32p4FlashDeviceUnit_Initialize(void) {
    // the chip description comes from the ROM, so there is no chip name to pass
    s_flashInfo.pChip = Esp32p4SPIFlash_Init(NULL);
    if (s_flashInfo.pChip == NULL) {
        LTLOG_REDALERT("fail.flash.init", "Failed to initialize the flash driver");
    } else {
        s_flashInfo.mutex      = lt_createobject(LTMutex);
        s_flashInfo.nNumSectors = s_flashInfo.pChip->chipSize / s_flashInfo.pChip->sectorSize;
#ifdef LT_DEBUG
        LTLOG_DEBUG("chip", "ID: %06lX (%lu KiB)", LT_Pu32(s_flashInfo.pChip->deviceId), LT_Pu32(s_flashInfo.pChip->chipSize >> 10));
#endif
    }
    return (s_flashInfo.mutex && s_flashInfo.pChip != NULL);
}

/*******************************************************************************
 * Finalize
*******************************************************************************/
void Esp32p4FlashDeviceUnit_Finalize(void) {
    if (s_flashInfo.mutex) {
        lt_destroyobject(s_flashInfo.mutex);
        s_flashInfo.mutex = NULL;
    }
}

/*******************************************************************************
 * Create device handle
*******************************************************************************/
LTDeviceUnit Esp32p4FlashDeviceUnit_CreateHandle(void) {
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

static u32 Esp32p4FlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    lt_memcpy(flashIDToSet, &s_flashInfo.pChip->deviceId, sizeof(s_flashInfo.pChip->deviceId));
    return sizeof(s_flashInfo.pChip->deviceId);
}

static u32 Esp32p4FlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->chipSize;
}

static u32 Esp32p4FlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.nNumSectors;
}

static u32 Esp32p4FlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return s_flashInfo.pChip->sectorSize;
}

static u32 Esp32p4FlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    return (nSectorNumber < Esp32p4FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? s_flashInfo.pChip->sectorSize * nSectorNumber : 0;
}

static u32 Esp32p4FlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    LT_UNUSED(hFlashDevice);
    u32 nSectorNumber = nByteOffset / s_flashInfo.pChip->sectorSize;
    return (nSectorNumber < Esp32p4FlashDeviceUnit_GetNumSectors(hFlashDevice)) ? nSectorNumber : 0;
}

static bool Esp32p4FlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    *pByteOffset = bGetPrimary ? kPrimaryPartitionTableOffset : kBackupPartitionTableOffset;
    return true;
}

static bool Esp32p4FlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * nAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    return Esp32p4SPIFlashCache_BusAddressToByteOffset(nAddress, pByteOffset);
}

static u16 Esp32p4FlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool Esp32p4FlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_EraseChip();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= Esp32p4FlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  Esp32p4FlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  Esp32p4FlashDeviceUnit_GetNumSectors(hDevice)) return false;
    bool bSuccess = true;
    LOCK_FLASH_MUTEX();
    for (; bSuccess && nNumSectors; ++nFirstSector, --nNumSectors) bSuccess = Esp32p4SPIFlash_EraseSector(nFirstSector);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_IsLocked();
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32p4SPIFlash_WriteProtect() : Esp32p4SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LOCK_FLASH_MUTEX();
    bool bSuccess = (bWriteProtect ? Esp32p4SPIFlash_WriteProtect() : Esp32p4SPIFlash_WriteUnprotect());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_Read(nByteOffset, pBuff, nNumBytes, Esp32p4SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_Write(nByteOffset, pBuff, nNumBytes, Esp32p4SPIFlash_IsEncryptionEnabled());
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_ReadRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_Read(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

static bool Esp32p4FlashDeviceUnit_WriteRawBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    LOCK_FLASH_MUTEX();
    bool bSuccess = Esp32p4SPIFlash_Write(nByteOffset, pBuff, nNumBytes, false);
    UNLOCK_FLASH_MUTEX();
    return bSuccess;
}

define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit)
    .GetFlashID                 = Esp32p4FlashDeviceUnit_GetFlashID,
    .GetNumBytes                = Esp32p4FlashDeviceUnit_GetNumBytes,
    .GetNumSectors              = Esp32p4FlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector          = Esp32p4FlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset   = Esp32p4FlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber   = Esp32p4FlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset    = Esp32p4FlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset     = Esp32p4FlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum            = Esp32p4FlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                = Esp32p4FlashDeviceUnit_EraseDevice,
    .EraseSectors               = Esp32p4FlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected     = Esp32p4FlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected     = Esp32p4FlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice         = Esp32p4FlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector         = Esp32p4FlashDeviceUnit_WriteProtectSector,
    .ReadBytes                  = Esp32p4FlashDeviceUnit_ReadBytes,
    .WriteBytes                 = Esp32p4FlashDeviceUnit_WriteBytes,
    .ReadRawBytes               = Esp32p4FlashDeviceUnit_ReadRawBytes,
    .WriteRawBytes              = Esp32p4FlashDeviceUnit_WriteRawBytes,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created, from the esp32c3 driver
 */
