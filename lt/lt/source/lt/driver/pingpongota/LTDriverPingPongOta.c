/******************************************************************************
 * lt/source/lt/driver/pingpongota/LTDriverPingPongOta.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/device/ota/LTDeviceOta.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("pp.ota");
#define LTDRIVERPINGPONGOTA_DO_DLOG     0
#if     LTDRIVERPINGPONGOTA_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

static LTCore           *s_core   = NULL;
static LTDeviceFlash    *s_flash  = NULL;
static LTUtilityByteOps *s_byte   = NULL;
static LTSystemCrypto   *s_crypto = NULL;

static ILTFlashDeviceUnit              *s_flashDeviceUnit   = NULL;
static LTDeviceFlash_Partition          s_newFwPartition;
static LTDeviceFlash_Partition          s_otaInfoPartition;
static LTDeviceUnit                     s_hFlashUnit        = 0;

static PingPongOta_PartitionInformation s_curPartInfo;
static PingPongOtaPartitionStatus       s_curPartStatus;
static u32                              s_curFwPartitionIdx;

static bool LTDriverPingPongOta_Init(void) {
    do {
        s_hFlashUnit = s_flash->CreateDeviceUnitHandle(0);
        s_flashDeviceUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashUnit);

        /* Determine active firmware partition  */
        LTDeviceFlash_Partition activeFwPartition;
        if (!s_flash->GetActiveFirmwarePartition(s_hFlashUnit, &activeFwPartition)) {
            LTLOG_REDALERT("actfw.part.err", "Failed to get active firmware partition");
            break;
        }
        const char *updatePartitionName = NULL;
        if (lt_strcmp(activeFwPartition.entry.name, "fw0") == 0) {
            s_curFwPartitionIdx = 0;
            updatePartitionName = "fw1";
        } else if (lt_strcmp(activeFwPartition.entry.name, "fw1") == 0) {
            s_curFwPartitionIdx = 1;
            updatePartitionName = "fw0";
        } else {
            LTLOG_REDALERT("actfw.part.badname", "Invalid firmware partition name: %s", activeFwPartition.entry.name);
            break;
        }

        /* Lookup update partition */
        if (!s_flash->GetPartition(s_hFlashUnit, updatePartitionName, &s_newFwPartition)) {
            LTLOG_REDALERT("upd.part.abs", "Device does not contain %s partition", updatePartitionName);
            break;
        }

        /* Read firmware ota info */
        if (!s_flash->GetPartition(s_hFlashUnit, "ota", &s_otaInfoPartition)) {
            LTLOG_REDALERT("otai.part.abs", "Device does not contain ota partition");
            break;
        }
        u32 sectorSize = s_flashDeviceUnit->GetBytesPerSector(s_hFlashUnit);
        u32 otaInfoOffset = s_otaInfoPartition.entry.nByteOffset + (sectorSize * s_curFwPartitionIdx);
        if (!s_flashDeviceUnit->ReadBytes(s_hFlashUnit, otaInfoOffset, sizeof(s_curPartInfo), (u8*)&s_curPartInfo)) {
            LTLOG_REDALERT("otai.rd.err", "Failed to read active firmware ota info");
            break;
        }

        /* Status is located at a fixed offset after partition information */
        if (!s_flashDeviceUnit->ReadBytes(s_hFlashUnit, otaInfoOffset + kPingPongOta_StatusOffset, sizeof(s_curPartStatus), (u8*)&s_curPartStatus)) {
            LTLOG_REDALERT("otai.rd.err", "Failed to read active firmware ota status");
            break;
        }

        LTLOG_DEBUG("finit.ok", "Partition '%s' selected to receive updated firmware", updatePartitionName);
        return true;
    } while (0);

    // fail
    lt_destroyhandle(s_hFlashUnit);
    s_hFlashUnit = 0;
    return false;
}

static bool LTDriverPingPongOta_IsValidated(void) {
    return s_curPartStatus == kPingPongOtaPartitionStatus_Good;
}

static bool LTDriverPingPongOta_CheckStorage(u32 imageSize) {
    return imageSize <= (s_newFwPartition.entry.nNumSectors * s_flashDeviceUnit->GetBytesPerSector(s_hFlashUnit));
}

static bool LTDriverPingPongOta_PrepareStorage(u32 imageSize) {
    u32 sectorSize   = s_flashDeviceUnit->GetBytesPerSector(s_hFlashUnit);
    u32 nFirstSector = s_flashDeviceUnit->ByteOffsetToSectorNumber(s_hFlashUnit, s_newFwPartition.entry.nByteOffset);
    u32 nNumSectors  = (imageSize + sectorSize - 1) / sectorSize;
    LTLOG("prep.storage.start", "%lu/%lu Bytes, %08lX sectors @ %08lX",
                                LT_Pu32(imageSize), LT_Pu32(s_newFwPartition.entry.nNumSectors * sectorSize),
                                LT_Pu32(nNumSectors), LT_Pu32(nFirstSector));
    bool bSuccess = s_flashDeviceUnit->EraseSectors(s_hFlashUnit, nFirstSector, nNumSectors);
    LTLOG("prep.storage.done", NULL);
    return bSuccess;
}

static bool LTDriverPingPongOta_SaveBlock(const u8 *data, u32 dataLen, u32 offsetToSave) {
    DLOG("save.blc", "%0lx %lld", LT_Pu32(offsetToSave), LT_Ps64(LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime())));
    u32 writeAddr = s_newFwPartition.entry.nByteOffset + offsetToSave;
    return s_flashDeviceUnit->WriteBytes(s_hFlashUnit, writeAddr, dataLen, data);
}

static bool LTDriverPingPongOta_VerifyImage(u32 imageSize, const u8 imageHash[SHA256_HASH_LENGTH]) {
    DLOG("verify.img.start", "%lld", LT_Ps64(LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime())));
    bool verified = false;
    u32 nFlashAddr = s_newFwPartition.entry.nByteOffset;
    u32 nBytesRemaining = imageSize;
    u8 *readBuf = lt_malloc(kPingPongOta_FirmwareBufLen);
    if (!readBuf) {
        LTLOG_REDALERT("fl.vfy.oom", "Oom flash readback");
        return false;
    }

    LT_SHA256_CTX *flashHash = s_crypto->CreateSeqSHA256();
    if (!flashHash) {
        LTLOG_REDALERT("fl.vfy.oom", "Oom sha ctx");
        goto done;
    }
    while (nBytesRemaining > 0) {
        u32 nBytesToRead = nBytesRemaining > kPingPongOta_FirmwareBufLen ? kPingPongOta_FirmwareBufLen : nBytesRemaining;
        if (!s_flashDeviceUnit->ReadBytes(s_hFlashUnit, nFlashAddr, nBytesToRead, readBuf)) {
            LTLOG_REDALERT("fl.vfy.rdfl.err", "Failed to read flash");
            goto done;
        }
        nFlashAddr += nBytesToRead;
        nBytesRemaining -= nBytesToRead;
        s_crypto->UpdateSeqSHA256(flashHash, readBuf, nBytesToRead);
    }
    u8 fwHash[SHA256_HASH_LENGTH];
    s_crypto->FinishSeqSHA256(flashHash, fwHash);

    if (lt_memcmp(fwHash, imageHash, SHA256_HASH_LENGTH) == 0) {
        LTLOG_DEBUG("fl.vfy.ok", "SWUP image hash comparison ok");
        verified = true;
    } else {
        LTLOG_REDALERT("fl.vfy.hash", "SWUP image hash comparison failed");
    }

done:
    lt_free(readBuf);
    s_crypto->DestroySeqSHA256(flashHash);
    DLOG("verify.img.done", "%lld", LT_Ps64(LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime())));
    return verified;
}

static bool LTDriverPingPongOta_ApplyUpdate(const char *version) {
    u32 sectorSize = s_flashDeviceUnit->GetBytesPerSector(s_hFlashUnit);
    u32 updatePartitionIdx = !s_curFwPartitionIdx;
    u32 otaInfoAddr = s_otaInfoPartition.entry.nByteOffset + (sectorSize * updatePartitionIdx);
    u32 otaInfoSector = otaInfoAddr / sectorSize;

    if (!s_flashDeviceUnit->EraseSectors(s_hFlashUnit, otaInfoSector, 1)) {
        LTLOG_REDALERT("otai.wrt.err", "Failed to erase update partition info");
        return false;
    }

    /* Start with blank buffer that meets write size requirements and then write over buffer */
    u32 writeMask = s_otaInfoPartition.nWriteQuantum - 1;
    u32 alignedSize = (sizeof(PingPongOta_PartitionInformation) + writeMask) & ~writeMask;
    u8 updateInfoBuffer[alignedSize];
    lt_memset(updateInfoBuffer, 0xff, alignedSize);

    PingPongOta_PartitionInformation * info = (PingPongOta_PartitionInformation *)updateInfoBuffer;
    if (lt_strlen(version) > sizeof(info->ver)) {
        LTLOG_REDALERT("otai.wrt.err", "Version length overflow");
        return false;
    }
    lt_memset(info, 0x00, sizeof(PingPongOta_PartitionInformation));  // zero bytes
    info->nMagic = kPingPongOta_UpdatePartition_Magic;
    info->nCount = s_curPartInfo.nCount + 1;
    info->type   = kPingPongOtaPartitionType_Customer;
    lt_strncpyTerm(info->ver, version, sizeof(info->ver));
    s_byte->Crc32(updateInfoBuffer, LT_OFFSET_OF(PingPongOta_PartitionInformation, nCRC), &info->nCRC);

    if (!s_flashDeviceUnit->WriteBytes(s_hFlashUnit, otaInfoAddr, alignedSize, updateInfoBuffer)) {
        LTLOG_REDALERT("otai.wrt.err", "Failed to write update partition info");
        return false;
    }

    return true;
}

static void LTDriverPingPongOta_Complete(void) {
    return;
}

static bool LTDriverPingPongOta_MarkValidated(void) {
    u32 sectorSize = s_flashDeviceUnit->GetBytesPerSector(s_hFlashUnit);
    u32 otaInfoAddr = s_otaInfoPartition.entry.nByteOffset + (sectorSize * s_curFwPartitionIdx);

    /* Start with a blank buffer that meets flash write size requirements and write Good marker */
    u32 writeQuantum = s_otaInfoPartition.nWriteQuantum;
    /* The write quantum is a power-of-2 value that must accommodate the location of the status marker */
    LT_ASSERT((writeQuantum & (writeQuantum - 1)) == 0 && writeQuantum <= kPingPongOta_StatusOffset);
    u32 alignedSize = sizeof(PingPongOtaPartitionStatus);
    if (alignedSize < writeQuantum) alignedSize = writeQuantum;
    u8 statusBuffer[alignedSize];
    lt_memset(statusBuffer, 0xff, alignedSize);
    *((PingPongOtaPartitionStatus *)statusBuffer) = kPingPongOtaPartitionStatus_Good;

    /* Status is located at fixed offset after partition information */
    if (!s_flashDeviceUnit->WriteBytes(s_hFlashUnit, otaInfoAddr + kPingPongOta_StatusOffset, alignedSize, statusBuffer)) {
        LTLOG_REDALERT("otai.wrt.err", "Failed to write firmware status");
        return false;
    }

    s_curPartStatus = kPingPongOtaPartitionStatus_Good;
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static ILTOta s_ILTOta;

static u32 LTDriverPingPongOtaImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTDeviceUnit LTDriverPingPongOtaImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)&s_ILTOta, 1);
}

static void ShutdownOta(void) {
    lt_closelibrary(s_crypto);
    s_crypto = NULL;
    lt_closelibrary(s_byte);
    s_byte = NULL;
    lt_destroyhandle(s_hFlashUnit);
    s_hFlashUnit = 0;
    lt_closelibrary(s_flash);
    s_flash = NULL;
    s_core = NULL;
}

static bool LTDriverPingPongOtaImpl_LibInit(void) {
    do {
        s_core = LT_GetCore();
        s_flash = lt_openlibrary(LTDeviceFlash);
        if (!s_flash) break;
        s_byte = lt_openlibrary(LTUtilityByteOps);
        if (!s_byte) break;
        s_crypto = lt_openlibrary(LTSystemCrypto);
        if (!s_crypto) break;
        return true;
    } while (0);

    ShutdownOta();
    LTLOG_YELLOWALERT("error", "fail to init");
    return false;
}

static void LTDriverPingPongOtaImpl_LibFini(void) {
    ShutdownOta();
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceOta, LTDriverPingPongOta);

define_LTLIBRARY_INTERFACE(ILTOta) {
    .Init            = &LTDriverPingPongOta_Init,
    .IsValidated     = &LTDriverPingPongOta_IsValidated,
    .CheckStorage    = &LTDriverPingPongOta_CheckStorage,
    .PrepareStorage  = &LTDriverPingPongOta_PrepareStorage,
    .SaveBlock       = &LTDriverPingPongOta_SaveBlock,
    .VerifyImage     = &LTDriverPingPongOta_VerifyImage,
    .ApplyUpdate     = &LTDriverPingPongOta_ApplyUpdate,
    .Complete        = &LTDriverPingPongOta_Complete,
    .MarkValidated   = &LTDriverPingPongOta_MarkValidated,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverPingPongOta, (ILTOta))

/**********************************************************************************
 *  LOG
 **********************************************************************************
 *  07-Aug-23   gallienus   created
 */
