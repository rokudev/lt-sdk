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
#include <lt/core/LTStdlib.h>
#include "STFlashDeviceUnit.h"

#include "stm32h7xx_hal.h"

DEFINE_LTLOG_SECTION("st.flash");

/*******************************************************************************
 * STM32H755 internal flash layout:
 *   Bank 1: 0x08000000 - 0x080FFFFF  (1 MB, 8 sectors x 128 KB)
 *   Bank 2: 0x08100000 - 0x081FFFFF  (1 MB, 8 sectors x 128 KB)
 *   Total:  2 MB, 16 sectors of 128 KB each
 *
 * The HAL flash word is 256 bits (32 bytes), so writes must be 32-byte aligned.
 * HAL_FLASH_Program() writes one flash word (32 bytes) at a time.
 ******************************************************************************/

enum {
    kFlashBank1Base         = 0x08000000UL,
    kFlashBank2Base         = 0x08100000UL,
    kFlashBankSize          = 0x00100000UL,   /* 1 MB per bank */
    kSectorSizeBytes        = 0x00020000UL,   /* 128 KB per sector */
    kSectorsPerBank         = 8,
    kTotalSectors           = 16,             /* 2 banks x 8 sectors */
    kTotalFlashBytes        = 0x00200000UL,   /* 2 MB */
    kFlashWordBytes         = 32,             /* HAL flash word = 256 bits = 32 bytes */
    kWriteQuantum           = kFlashWordBytes,
    kPrimaryPartitionTableOffset = 0x00080000UL, /* sector 4 */
    kBackupPartitionTableOffset  = 0x000A0000UL, /* sector 5 */
    kHALTimeout             = 50000U          /* ms, erase of full chip can take ~40s */
};

typedef struct {
    LTMutex * mutex;
    u32       nRefCount;
} FlashInfo;

static FlashInfo s_flashInfo;

static FlashInfo * GetFlashInfoPtr(LTDeviceUnit hFlashDevice) {
    return hFlashDevice ? &s_flashInfo : NULL;
}

/*******************************************************************************
 * Map a linear sector number (0-15) to bank + HAL sector number (0-7)
 ******************************************************************************/
static void SectorToHAL(u32 nSectorNumber, u32 * pBank, u32 * pHALSector) {
    if (nSectorNumber < kSectorsPerBank) {
        *pBank      = FLASH_BANK_1;
        *pHALSector = nSectorNumber;
    } else {
        *pBank      = FLASH_BANK_2;
        *pHALSector = nSectorNumber - kSectorsPerBank;
    }
}

/*******************************************************************************
 * Map a linear byte offset to an absolute flash address
 ******************************************************************************/
static u32 ByteOffsetToAddress(u32 nByteOffset) {
    if (nByteOffset < kFlashBankSize) {
        return kFlashBank1Base + nByteOffset;
    }
    return kFlashBank2Base + (nByteOffset - kFlashBankSize);
}

/*******************************************************************************
 * Map an absolute flash address to a linear byte offset
 ******************************************************************************/
static bool AddressToByteOffset(u32 nAddress, u32 * pByteOffset) {
    if (nAddress >= kFlashBank1Base && nAddress < (kFlashBank1Base + kFlashBankSize)) {
        *pByteOffset = nAddress - kFlashBank1Base;
        return true;
    }
    if (nAddress >= kFlashBank2Base && nAddress < (kFlashBank2Base + kFlashBankSize)) {
        *pByteOffset = kFlashBankSize + (nAddress - kFlashBank2Base);
        return true;
    }
    return false;
}

/*******************************************************************************
 * Init / Finalize
 ******************************************************************************/
void STFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex    = lt_createobject(LTMutex);
    s_flashInfo.nRefCount = 0;
}

/*___________________________________________________
 / STFlashDeviceUnit Handle Creation function */
static const ILTFlashDeviceUnit s_ILTFlashDeviceUnit;

LTDeviceUnit STFlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(FlashInfo *));
    if (hFlashDevice) {
        FlashInfo ** ppFlashInfo = (FlashInfo **)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        if (ppFlashInfo) {
            *ppFlashInfo = &s_flashInfo;
            s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
            ++s_flashInfo.nRefCount;
            s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
            LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, ppFlashInfo);
        }
    }
    return hFlashDevice;
}

/*******************************************************************************
 * ILTFlashDeviceUnit interface implementation
 ******************************************************************************/

static u32 STFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    /* STM32H755 does not expose a JEDEC-style flash ID; return the UID instead */
    u32 uid = HAL_GetUIDw0();
    lt_memcpy(flashIDToSet, &uid, sizeof(uid));
    return sizeof(uid);
}

static u32 STFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kTotalFlashBytes;
}

static u32 STFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kTotalSectors;
}

static u32 STFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kSectorSizeBytes;
}

static u32 STFlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    return (nSectorNumber < kTotalSectors) ? kSectorSizeBytes * nSectorNumber : 0;
}

static u32 STFlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    LT_UNUSED(hFlashDevice);
    u32 nSectorNumber = nByteOffset / kSectorSizeBytes;
    return (nSectorNumber < kTotalSectors) ? nSectorNumber : 0;
}

static bool STFlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    *pByteOffset = bGetPrimary ? kPrimaryPartitionTableOffset : kBackupPartitionTableOffset;
    return true;
}

static bool STFlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * nAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    return AddressToByteOffset((u32)nAddress, pByteOffset);
}

static u16 STFlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool STFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef eraseInit;
    eraseInit.TypeErase    = FLASH_TYPEERASE_MASSERASE;
    eraseInit.Banks        = FLASH_BANK_BOTH;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7V - 3.6V */

    u32 sectorError = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    HAL_FLASH_Lock();

    s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);

    if (status != HAL_OK) {
        LTLOG_REDALERT("fail.erase.device", "Mass erase failed, HAL error %08lX", LT_Pu32(HAL_FLASH_GetError()));
    }
    return (status == HAL_OK);
}

static bool STFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= STFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  STFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  STFlashDeviceUnit_GetNumSectors(hDevice)) return false;

    s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);

    HAL_FLASH_Unlock();

    bool bSuccess = true;
    u32 sector = nFirstSector;
    u32 remaining = nNumSectors;

    /* Erase contiguous runs within each bank separately, as the HAL requires
     * that all sectors in a single call belong to the same bank.             */
    while (bSuccess && remaining > 0) {
        u32 bank, halSector;
        SectorToHAL(sector, &bank, &halSector);

        /* Count how many consecutive sectors remain in this bank */
        u32 sectorsThisBank = (bank == FLASH_BANK_1)
            ? (kSectorsPerBank - halSector)
            : (kSectorsPerBank - halSector);
        if (sectorsThisBank > remaining) sectorsThisBank = remaining;

        FLASH_EraseInitTypeDef eraseInit;
        eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
        eraseInit.Banks        = bank;
        eraseInit.Sector       = halSector;
        eraseInit.NbSectors    = sectorsThisBank;
        eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        u32 sectorError = 0;
        HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
        if (status != HAL_OK) {
            LTLOG_REDALERT("fail.erase.sector", "Sector erase failed at sector %lu, HAL error %08lX",
                LT_Pu32(sector), LT_Pu32(HAL_FLASH_GetError()));
            bSuccess = false;
        }
        sector    += sectorsThisBank;
        remaining -= sectorsThisBank;
    }

    HAL_FLASH_Lock();
    s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);

    return bSuccess;
}

static bool STFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    /* Read WRP option bytes for both banks */
    FLASH_OBProgramInitTypeDef ob;
    ob.Banks = FLASH_BANK_1;
    HAL_FLASHEx_OBGetConfig(&ob);
    if (ob.WRPState == OB_WRPSTATE_ENABLE) return true;
    ob.Banks = FLASH_BANK_2;
    HAL_FLASHEx_OBGetConfig(&ob);
    return (ob.WRPState == OB_WRPSTATE_ENABLE);
}

static bool STFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    u32 bank, halSector;
    SectorToHAL(nSectorNumber, &bank, &halSector);
    FLASH_OBProgramInitTypeDef ob;
    ob.Banks = bank;
    HAL_FLASHEx_OBGetConfig(&ob);
    return (ob.WRPState == OB_WRPSTATE_ENABLE) && (ob.WRPSector & (1u << halSector));
}

static bool STFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    /* Per-sector WRP requires option byte programming — not supported here */
    LT_UNUSED(bWriteProtect);
    return false;
}

static bool STFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LT_UNUSED(bWriteProtect);
    return false;
}

/*******************************************************************************
 * ReadBytes - flash is directly memory-mapped, so a simple memcpy suffices
 ******************************************************************************/
static bool STFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    if (nByteOffset + nNumBytes > kTotalFlashBytes) return false;
    u32 nAddress = ByteOffsetToAddress(nByteOffset);
    lt_memcpy(pBuff, (const void *)nAddress, nNumBytes);
    return true;
}

/*******************************************************************************
 * WriteBytes - writes nNumBytes from pBuff to flash at nByteOffset.
 *
 * The STM32H7 requires writes to be 32-byte (256-bit flash word) aligned. When
 * nByteOffset or nNumBytes are not 32-byte multiples, we read-modify-write the
 * partial boundary flash words using a local 32-byte staging buffer.
 ******************************************************************************/
static bool STFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    LT_UNUSED(hFlashDevice);
    if (!nNumBytes) return true;
    if (nByteOffset + nNumBytes > kTotalFlashBytes) return false;

    s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);

    HAL_FLASH_Unlock();

    bool bSuccess = true;
    u32 offset = nByteOffset;
    u32 remaining = nNumBytes;
    const u8 * pSrc = pBuff;

    /* --- Handle leading partial flash word --- */
    u32 leadBytes = offset & (kFlashWordBytes - 1);
    if (leadBytes != 0) {
        u32 wordBase = offset - leadBytes;
        u32 wordAddr = ByteOffsetToAddress(wordBase);

        u8 staging[kFlashWordBytes];
        lt_memcpy(staging, (const void *)wordAddr, kFlashWordBytes);

        u32 copyBytes = kFlashWordBytes - leadBytes;
        if (copyBytes > remaining) copyBytes = remaining;
        lt_memcpy(staging + leadBytes, pSrc, copyBytes);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, wordAddr, (u32)staging) != HAL_OK) {
            LTLOG_REDALERT("fail.write.lead", "Flash write failed at 0x%08lX", LT_Pu32(wordAddr));
            bSuccess = false;
        }

        offset    += copyBytes;
        pSrc      += copyBytes;
        remaining -= copyBytes;
    }

    /* --- Write aligned full flash words --- */
    while (bSuccess && remaining >= kFlashWordBytes) {
        u32 wordAddr = ByteOffsetToAddress(offset);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, wordAddr, (u32)pSrc) != HAL_OK) {
            LTLOG_REDALERT("fail.write.word", "Flash write failed at 0x%08lX", LT_Pu32(wordAddr));
            bSuccess = false;
        }

        offset    += kFlashWordBytes;
        pSrc      += kFlashWordBytes;
        remaining -= kFlashWordBytes;
    }

    /* --- Handle trailing partial flash word --- */
    if (bSuccess && remaining > 0) {
        u32 wordAddr = ByteOffsetToAddress(offset);

        u8 staging[kFlashWordBytes];
        lt_memcpy(staging, (const void *)wordAddr, kFlashWordBytes);
        lt_memcpy(staging, pSrc, remaining);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, wordAddr, (u32)staging) != HAL_OK) {
            LTLOG_REDALERT("fail.write.trail", "Flash write failed at 0x%08lX", LT_Pu32(wordAddr));
            bSuccess = false;
        }
    }

    HAL_FLASH_Lock();
    s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);

    return bSuccess;
}

/*_________________________________________________________________________
 / STFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void STFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (pFlashInfo) {
        pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
        --pFlashInfo->nRefCount;
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
 *  23-Feb-26   augustus   created (stub)
 *  09-Jul-26   augustus   implemented using STM32H7xx HAL flash API
 */
