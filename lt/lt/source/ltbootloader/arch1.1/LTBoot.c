/*******************************************************************************
 * LTBoot.c                                                        LT Bootloader
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTBoot.h"
#include "LTBootDriver.h"

#include "lt/device/ota/LTDeviceOta.h"

static LTBootInfo s_info = { .nBootIdx = 0xff, .usePrimaryTable = true };

/* This bitwise CRC implementation is compact. While it is not very efficient, the
 * payloads used by the bootloader are relatively small and therefore speed is not
 * normally an issue. This function has weak linkage and may be overridden in the
 * LTBootDriver implementation if desired. */
LTBOOT_WEAK void LTBootDriver_Crc32(u8 * pData, u32 nSizeInBytes, u32 * pCrc) {
    u32 nCrc = *pCrc ^ 0xffffffff;
    const u32 nBitReversedPoly = 0xedb88320;
    for (u32 nByte = 0; nByte < nSizeInBytes; nByte++) {
        nCrc ^= pData[nByte];
        for (u32 nBit = 0; nBit < 8; nBit++) {
            nCrc = (nCrc >> 1) ^ (nBitReversedPoly & -(nCrc & 0x1));
        }
    }
    *pCrc = nCrc ^ 0xffffffff;
}

bool LTBoot_EnumeratePartitions(LTBoot_PartitionEnumCallback * pCallback, void * pClientData) {
    u32 nReadOffset;
    if (!LTBootDriverFlash_GetPartitionTableOffset(&nReadOffset, s_info.usePrimaryTable)) return false;
    nReadOffset += sizeof(LTDeviceFlash_PartitionTableHeader);
    LTDeviceFlash_PartitionEntry partition;
    for (u32 nPart = 0; nPart < s_info.nNumPartitions; nPart++) {
       if (!LTBootDriverFlash_ReadBytes(nReadOffset, sizeof(partition), (u8 *)&partition)) return false;
       if ((*pCallback)(&partition, pClientData) == false) return false;
       nReadOffset += sizeof(LTDeviceFlash_PartitionEntry);
    }
    return true;
}

static bool ReadPartitionTableCallback(const LTDeviceFlash_PartitionEntry * pPartition, void * pClientData) {
    LT_UNUSED(pClientData);
    if (LTBootPlatform_strncmp(pPartition->type, "fw", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        u32 nFw = s_info.nNumFwPartitions;
        if (nFw < kLTBootNumFirmwarePartitions) {
            s_info.fw[nFw].nByteOffset  = pPartition->nByteOffset;
            s_info.fw[nFw].nSizeInBytes = pPartition->nNumSectors * s_info.nSectorSize;
        }
        s_info.nNumFwPartitions++;
    } else if (LTBootPlatform_strncmp(pPartition->type, "otainfo", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        s_info.otainfo.nByteOffset  = pPartition->nByteOffset;
        s_info.otainfo.nSizeInBytes = kLTBootNumFirmwarePartitions * s_info.nSectorSize;
    } else if (LTBootPlatform_strncmp(pPartition->type, "ltat", kLTDeviceFlash_MaxTypeSize - 1) == 0) {
        s_info.ltat.nByteOffset  = pPartition->nByteOffset;
        s_info.ltat.nSizeInBytes = s_info.nSectorSize;
    }
    return true;
}

static bool LTBoot_ReadPartitionTable(bool bUsePrimary) {
    u32 nReadOffset;
    if (!LTBootDriverFlash_GetPartitionTableOffset(&nReadOffset, bUsePrimary)) return false;
    u32 nCalcCrc = 0;
    {   /* Read and check header */
        LTDeviceFlash_PartitionTableHeader hdr;
        if (!LTBootDriverFlash_ReadBytes(nReadOffset, sizeof(hdr), (u8 *)&hdr)) return false;
        if (hdr.nMagic != kLTDeviceFlash_Magic_PartitionTable) return false;
        LTBootDriver_Crc32((u8 *)&hdr, sizeof(hdr), &nCalcCrc);
        nReadOffset += sizeof(hdr);
        s_info.nNumPartitions = hdr.nNumPartitions;
    }
    {   /* Read partitions, check CRC */
        LTDeviceFlash_PartitionEntry partition;
        for (u32 nPart = 0; nPart < s_info.nNumPartitions; nPart++) {
            if (!LTBootDriverFlash_ReadBytes(nReadOffset, sizeof(partition), (u8 *)&partition)) return false;
            LTBootDriver_Crc32((u8 *)&partition, sizeof(partition), &nCalcCrc);
            nReadOffset += sizeof(partition);
        }
        u32 nReadCrc;
        /* CRC should be stored in the native byte-ordering */
        if (!LTBootDriverFlash_ReadBytes(nReadOffset, sizeof(nReadCrc), (u8 *)&nReadCrc)) return false;
        if (nReadCrc != nCalcCrc) return false;
    }
    s_info.usePrimaryTable = bUsePrimary;
    s_info.nNumFwPartitions = 0;
    s_info.nSectorSize = LTBootDriverFlash_GetBytesPerSector();
    return LTBoot_EnumeratePartitions(ReadPartitionTableCallback, NULL);
}

LTBootSecurityCheck LTBoot_CheckLTATClaims(LTSecurityClaimMask mask) {
    if (!mask || s_info.ltat.nSizeInBytes != s_info.nSectorSize)
        return kLTBootSecurityCheck_Fail;
    volatile LTBootSecurityCheck nCheck = kLTBootDeviceIDLength - kLTBootSecurityCheck_Pass + ~mask;
    /* Check magic number, ID and claim bit mask */
    {
        u32 nReadOffset = s_info.ltat.nByteOffset;
        LTSecurityLTATHeader hdr = { 0 };
        if (!LTBootDriverFlash_ReadBytes(nReadOffset, sizeof(hdr), (u8 *)&hdr))
            return kLTBootSecurityCheck_Fail;
        if (hdr.nMagic != kLTSecurity_Magic_LTAT) return kLTBootSecurityCheck_Fail;
        u8 deviceID[kLTBootDeviceIDLength];
        if (!LTBootDriver_GetDeviceID(deviceID)) return kLTBootSecurityCheck_Fail;
        nCheck += LTBootDriver_TRNGDelay();
        for (u32 ix = 0; ix < kLTBootDeviceIDLength; ix++, nCheck--) {
            if (hdr.id[ix] != deviceID[ix]) return kLTBootSecurityCheck_Fail;
        }
        nCheck -= ~(hdr.mask[0] & mask);
    }
    /* Check signature even if mask doesn't match to allow subsequent processing of other claims */
    nCheck += LTBootDriver_AuthenticateLTAT(&s_info);
    return nCheck;
}

static bool LTBoot_CheckOTAPartition(u32 nBootIdx) {
    if (s_info.fw[nBootIdx].nSizeInBytes == 0) {
        LTBootPlatform_printf("OTA%d fw partition not found\n", nBootIdx);
        return false;
    }
    return LTBootDriver_CheckOTAPartition(&s_info, nBootIdx);
}

static bool LTBoot_SelectBootPartition(LTBootReason reason) {
    /* Check that OTA information is the minimum size... */
    u32 nBytesPerSector = LTBootDriverFlash_GetBytesPerSector();
    if (s_info.otainfo.nSizeInBytes < kLTBootNumFirmwarePartitions * nBytesPerSector) {
        LTBootPlatform_printf("otainfo partition too small or not found\n");
        return false;
    }

    /* Read OTA information from flash */
    PingPongOta_PartitionInformation otainfo[kLTBootNumFirmwarePartitions];
    PingPongOtaPartitionStatus otastatus[kLTBootNumFirmwarePartitions];
    {
        if (!LTBootDriverFlash_ReadBytes(s_info.otainfo.nByteOffset,
                                            sizeof(PingPongOta_PartitionInformation), (u8 *)&otainfo[0])) return false;
        if (!LTBootDriverFlash_ReadBytes(s_info.otainfo.nByteOffset + nBytesPerSector,
                                            sizeof(PingPongOta_PartitionInformation), (u8 *)&otainfo[1])) return false;

        /* Read partition status from flash */
        u32 statusOffset = s_info.otainfo.nByteOffset + kPingPongOta_StatusOffset;
        if (!LTBootDriverFlash_ReadBytes(statusOffset, sizeof(PingPongOtaPartitionStatus), (u8 *)&otastatus[0]))
            return false;
        statusOffset += nBytesPerSector;
        if (!LTBootDriverFlash_ReadBytes(statusOffset, sizeof(PingPongOtaPartitionStatus), (u8 *)&otastatus[1]))
            return false;
    }

    /* OTA Partition Selection:
     *   NB: if magic number or CRC are invalid then treat count as -3 or -2 (respectively).
     *       else if (u32)nCount > 0x00ffffff then its invalid and treat count as -1.
     *
     *                  Count 1
     *               -1    N   N+1
     *      Count 0 +-------------
     *        -1    | 0    1    1
     *         N    | 0    0    1     (In general, partition with the higher count wins)
     *        N+1   | 0    0    0                    */
    s32 nCount[kLTBootNumFirmwarePartitions] = { -1, -1 };
    u32 nBootIdx = 0;
    {
        /* CRC / Magic Number check of OTA data */
        u32 nCalcCrc = 0;
        LTBootDriver_Crc32((u8 *)&otainfo[0], LT_OFFSET_OF(PingPongOta_PartitionInformation, nCRC), &nCalcCrc);
        if (otainfo[0].nMagic != kPingPongOta_UpdatePartition_Magic) nCount[0] = -3;
        else if (nCalcCrc != otainfo[0].nCRC) nCount[0] = -2;
        nCalcCrc = 0;
        LTBootDriver_Crc32((u8 *)&otainfo[1], LT_OFFSET_OF(PingPongOta_PartitionInformation, nCRC), &nCalcCrc);
        if (otainfo[1].nMagic != kPingPongOta_UpdatePartition_Magic) nCount[1] = -3;
        else if (nCalcCrc != otainfo[1].nCRC) nCount[1] = -2;

        const u32 nCountThresh = 0x00ffffff;
        if (nCount[0] == -1 && otainfo[0].nCount < nCountThresh) nCount[0] = (s32)otainfo[0].nCount;
        if (nCount[1] == -1 && otainfo[1].nCount < nCountThresh) nCount[1] = (s32)otainfo[1].nCount;
        if (nCount[1] > nCount[0]) nBootIdx = 1;
        LTBootPlatform_printf("OTA%u C:%d:%d ", 0, otainfo[0].nCount, nCount[0]);
        LTBootPlatform_printf("OTA%u C:%d:%d ", 1, otainfo[1].nCount, nCount[1]);
    }
    PingPongOtaPartitionType   type   = kPingPongOtaPartitionType_Manufacturing;
    PingPongOtaPartitionStatus status = kPingPongOtaPartitionStatus_Unknown;
    bool bTryFailover = true;
    bool bValid = false;
    {
        /* Primary area logic
         *   NB: Do not failover FROM a manufacturing partition.
         *       Attempt failover if watchdog reset occurred on an unvalidated partition. */
        if (nCount[nBootIdx] >= 0) {
            type   = otainfo[nBootIdx].type;
            status = otastatus[nBootIdx];
        }
        if (status == kPingPongOtaPartitionStatus_Good) {
            LTBootPlatform_printf("OTA%u T:%u S:G\n", nBootIdx, type);
        } else {
            LTBootPlatform_printf("OTA%u T:%u S:%x\n", nBootIdx, type, status);
        }
        if (type == kPingPongOtaPartitionType_Manufacturing) {
            bValid = LTBoot_CheckOTAPartition(nBootIdx);
            bTryFailover = false;
        } else if (status == kPingPongOtaPartitionStatus_Good || reason != kLTBootReason_WatchdogReset) {
            bValid = LTBoot_CheckOTAPartition(nBootIdx);
        }
    }
    if (!bValid) {
        /* Failover area logic
         *   NB: Do not failover TO a manufacturing partition. */
        nBootIdx = !nBootIdx;
        type     = kPingPongOtaPartitionType_Manufacturing;
        status   = kPingPongOtaPartitionStatus_Unknown;
        if (nCount[nBootIdx] >= 0) {
            type   = otainfo[nBootIdx].type;
            status = otastatus[nBootIdx];
        }
        if (status == kPingPongOtaPartitionStatus_Good) {
            LTBootPlatform_printf("OTA%u T:%u S:G\n", nBootIdx, type);
        } else {
            LTBootPlatform_printf("OTA%u T:%u S:%x\n", nBootIdx, type, status);
        }
        if (type == kPingPongOtaPartitionType_Manufacturing) {
            /* In this case retry customer-facing partition on watchdog reset */
            if (reason == kLTBootReason_WatchdogReset) nBootIdx = !nBootIdx;
            else bTryFailover = false;
        }
        if (bTryFailover) {
            bValid = LTBoot_CheckOTAPartition(nBootIdx);
        }
    }
    if (bValid) {
        s_info.bootReason = reason;
        s_info.nBootIdx   = nBootIdx;
        s_info.isBootStatusGood = (status == kPingPongOtaPartitionStatus_Good);
        LTBootPlatform_printf("OTA%d [%08x][%08x]\n", nBootIdx, s_info.fw[nBootIdx].nByteOffset,
                                                        s_info.fw[nBootIdx].nSizeInBytes);
    }
    return bValid;
}

u32 LTBoot_ProcessApplicationHeader(const LTApplicationHeader * pHeader, u32 magic, u32 sigBlockSize) {
    enum { kSecCheckPass = 0x7331ed0c };
    volatile u32 bootCheck = kSecCheckPass - 10*kLTBootSecurityCheck_Pass;
    u32 nImageSizeOnFlash = sizeof(LTApplicationHeader);
    if (pHeader->sigOffset != kLTApplicationSigOffset_None) {
        if (pHeader->sigOffset >= pHeader->imageSize) bootCheck += kLTBootSecurityCheck_Pass;
        nImageSizeOnFlash += pHeader->sigOffset + sigBlockSize;
    } else {
        bootCheck += kLTBootSecurityCheck_Pass;
        nImageSizeOnFlash += pHeader->imageSize;
    }
    if (nImageSizeOnFlash < s_info.fw[s_info.nBootIdx].nSizeInBytes) bootCheck += kLTBootSecurityCheck_Pass;
    /* Check reserved header fields */
    if (pHeader->magic == magic) bootCheck += kLTBootSecurityCheck_Pass;
    for (u32 nIx = 0; nIx < 4; nIx++) {
        if (pHeader->rsvd0[nIx] == 0) bootCheck += kLTBootSecurityCheck_Pass;
    }
    for (u32 nIx = 0; nIx < 3; nIx++) {
        if (pHeader->rsvd1[nIx] == 0) bootCheck += kLTBootSecurityCheck_Pass;
    }
    LTBootPlatform_SecurityAssert(bootCheck == kSecCheckPass);
    return nImageSizeOnFlash;
}

void LTBoot_Run(LTBootReason reason, const char * pReason) {
    enum { kSecCheckPass = 0xed0c7331 };
    /* Chip and bootloader security */
    volatile u32 bootCheck = kSecCheckPass + 3*kLTBootSecurityCheck_Pass;
    bootCheck -= LTBootDriver_TRNGDelay();
    bootCheck -= LTBootDriver_CheckChipSecurity();
    bootCheck -= LTBootDriver_CheckBootloaderSecurity();
    LTBootPlatform_SecurityAssert(bootCheck == kSecCheckPass);
    /* Application boot */
    const char * pVer = LTBootDriver_GetVersion();
    LTBootPlatform_printf("\n<<<------ LTBoot Version %s Running (Boot Reason %u", pVer ? pVer : "X", reason);
    if (pReason) LTBootPlatform_printf(": %s", pReason);
    LTBootPlatform_printf(") ------>>>\n\n");
    /* Read primary partition table and fill in boot info, using backup table if necessary */
    bool bTableRead = LTBoot_ReadPartitionTable(true);
    if (!bTableRead) {
        LTBootPlatform_printf("Trying backup table.\n");
        bTableRead = LTBoot_ReadPartitionTable(false);
    }
    if (bTableRead) {
        if (LTBoot_SelectBootPartition(reason) && s_info.nBootIdx <= 1) {
            volatile u32 appCheck = kSecCheckPass + 2*kLTBootSecurityCheck_Pass;
            appCheck -= LTBootDriver_TRNGDelay();
            appCheck -= LTBootDriver_LoadAndAuthenticateApplication(&s_info);
            LTBootPlatform_SecurityAssert(bootCheck == kSecCheckPass);
            LTBootPlatform_SecurityAssert(appCheck == kSecCheckPass);
            LTBootDriver_RunApplication(&s_info);
            /* ONLY REACHABLE upon failure */
            LTBootPlatform_printf("No bootable app found!\n");
        } else {
            LTBootPlatform_printf("Can't select boot partition!\n");
        }
    } else {
        LTBootPlatform_printf("Error reading partition table!\n");
    }
    LTBootPlatform_SecurityAbort();
    while (1);
    /* UNREACHABLE */
}

