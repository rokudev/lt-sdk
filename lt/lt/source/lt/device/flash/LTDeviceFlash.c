/*******************************************************************************
 * lt/source/lt/device/flash/LTDeviceFlash.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/device/config/LTDeviceConfig.h>

static LTDriverLibrary * s_pLibFlashDriver     = NULL;
static u32               s_nNumTotalFlashUnits = 0;

typedef struct {
    ILTFlashDeviceUnit * iFlashUnit;
    LTDeviceUnit         hFlashUnit;
    u32                  nReadOffset;
    bool                 bUsingPrimary;
    u8                   nNumPartitions;
    u8                   nTableVersion;
    u8                   nPartition;
} ReadContext;

static bool InitReadContext(LTDeviceUnit hFlashUnit, ReadContext * pContext) {
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (!iFlashUnit) return false;
    pContext->iFlashUnit    = iFlashUnit;
    pContext->hFlashUnit    = hFlashUnit;
    pContext->bUsingPrimary = true;
    for (u32 i = 0; i < 2; i++) {
        if (iFlashUnit->GetPartitionTableOffset(hFlashUnit, &pContext->nReadOffset, pContext->bUsingPrimary)) {
            u32 nMagic;
            if (!iFlashUnit->ReadBytes(hFlashUnit, pContext->nReadOffset, sizeof(nMagic), (u8 *)&nMagic)) return false;
            if (nMagic == kLTDeviceFlash_Magic_PartitionTable) {
                pContext->nReadOffset += sizeof(u32);
                u8 buf[3];
                if (!iFlashUnit->ReadBytes(hFlashUnit, pContext->nReadOffset, sizeof(buf), buf)) return false;
                pContext->nNumPartitions = buf[0];
                pContext->nTableVersion  = buf[2];
                pContext->nPartition     = 0;
                pContext->nReadOffset   += 2 * sizeof(u32);
                return true;
            }
        }
        pContext->bUsingPrimary = false;
    }
    return false;
}

static bool GetNextPartition(ReadContext * pContext, LTDeviceFlash_Partition * pPartition) {
    ILTFlashDeviceUnit * iFlashUnit = pContext->iFlashUnit;
    if (pContext->nPartition++ >= pContext->nNumPartitions) return false;
    if (!iFlashUnit->ReadBytes(pContext->hFlashUnit, pContext->nReadOffset, sizeof(LTDeviceFlash_PartitionEntry), (u8 *)&pPartition->entry))
        return false;
    pContext->nReadOffset    += sizeof(LTDeviceFlash_PartitionEntry);
    pPartition->nWriteQuantum = iFlashUnit->GetWriteQuantum(pContext->hFlashUnit);
    return true;
}

/*____________________________________________
 / LTDeviceFlashImpl library initialization */
static bool LTDeviceFlashImpl_LibInit(void) {
    bool bSuccess = false;
    s_pLibFlashDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceFlash", 0);
    if (s_pLibFlashDriver) {
        s_nNumTotalFlashUnits = s_pLibFlashDriver->GetNumDeviceUnits();
        if (s_nNumTotalFlashUnits) bSuccess = true;
        else {
            LT_GetCore()->CloseLibrary((LTLibrary *)s_pLibFlashDriver);
            s_pLibFlashDriver = NULL;
        }
    }
    return bSuccess;
}

static void LTDeviceFlashImpl_LibFini(void) {
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pLibFlashDriver);
    s_pLibFlashDriver     = NULL;
    s_nNumTotalFlashUnits = 0;
}

/*________________________________________________________
 / LTDeviceFlashImpl library public interface functions */
static u32
LTDeviceFlashImpl_GetNumDeviceUnits(void) {
    return s_nNumTotalFlashUnits;
}

static LTDeviceUnit
LTDeviceFlashImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LTDeviceUnit hFlashUnit = 0;
    if (nDeviceUnitNum < s_nNumTotalFlashUnits) {
        hFlashUnit = s_pLibFlashDriver->CreateDeviceUnitHandle(nDeviceUnitNum);
    }
    return hFlashUnit;
}

static bool
LTDeviceFlashImpl_GetPartition(LTDeviceUnit hFlashUnit, const char * pPartitionName, LTDeviceFlash_Partition * pPartition) {
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (!iFlashUnit) return false;
    if (iFlashUnit->GetPartition) {
        return iFlashUnit->GetPartition(hFlashUnit, pPartitionName, pPartition);
    }
    // Fallback implementation: enumerate partitions until we find the named one
    ReadContext context;
    if (InitReadContext(hFlashUnit, &context) == false) return false;
    while (GetNextPartition(&context, pPartition)) {
        if (lt_strncmp(pPartition->entry.name, pPartitionName, kLTDeviceFlash_MaxNameSize - 1) == 0) return true;
    }

    return false;
}

/* Its important that this function be located in flash for platforms that are eXecute-In-Place. */
static bool
LTDeviceFlashImpl_GetActiveFirmwarePartition(LTDeviceUnit hFlashUnit, LTDeviceFlash_Partition * pPartition) {
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (!iFlashUnit) return false;
    if (iFlashUnit->GetActiveFirmwarePartition) {
        return iFlashUnit->GetActiveFirmwarePartition(hFlashUnit, pPartition);
    } 
    // Fallback implementation: determine current execution address and find partition containing it
    ReadContext context;
    if (InitReadContext(hFlashUnit, &context) == false) return false;
    u32 nByteOffset;
    /* Determine flash byte offset of this function and use it to determine the active partition */
    if (context.iFlashUnit->BusAddressToByteOffset(hFlashUnit, (void *)LTDeviceFlashImpl_GetActiveFirmwarePartition, &nByteOffset)) {
        while (GetNextPartition(&context, pPartition)) {
            u32 nEndOffset = pPartition->entry.nByteOffset + context.iFlashUnit->GetBytesPerSector(hFlashUnit) * pPartition->entry.nNumSectors;
            if (nByteOffset >= pPartition->entry.nByteOffset && nByteOffset < nEndOffset) return true;
        }
    }
    return false;
}

static bool
LTDeviceFlashImpl_ErasePartition(LTDeviceUnit hFlashUnit, const char * pPartitionName) {

    LTDeviceFlash_Partition partition;
    if (!LTDeviceFlashImpl_GetPartition(hFlashUnit, pPartitionName, &partition)) return false;
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    return iFlashUnit->EraseSectors(hFlashUnit, iFlashUnit->ByteOffsetToSectorNumber(hFlashUnit, partition.entry.nByteOffset),
                                                    partition.entry.nNumSectors);
}

static bool
LTDeviceFlashImpl_EnumeratePartitions(LTDeviceUnit hFlashUnit, LTDeviceFlash_PartitionEnumProc * pCallback, void * pClientData) {
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (!iFlashUnit) return false;
    if (iFlashUnit->EnumeratePartitions) {
        return iFlashUnit->EnumeratePartitions(hFlashUnit, pCallback, pClientData);
    }
    // Fallback implementation: enumerate partitions from partition table
    ReadContext context;
    if (InitReadContext(hFlashUnit, &context) == false) return false;
    LTDeviceFlash_Partition partition;
    while (GetNextPartition(&context, &partition)) {
        if ((*pCallback)(context.nNumPartitions, &partition, pClientData) == false) return false;
    }
    return true;
}

static bool
LTDeviceFlashImpl_GetPartitionTableInfo(LTDeviceUnit hFlashUnit, u8 * pVersionNumber, bool * pUsingPrimary) {
    ILTFlashDeviceUnit * iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, hFlashUnit);
    if (!iFlashUnit) return false;
    if (iFlashUnit->GetPartitionTableInfo) {
        return iFlashUnit->GetPartitionTableInfo(hFlashUnit, pVersionNumber, pUsingPrimary);
    }
    // Fallback implementation: get partition table info from partition table
    ReadContext context;
    if (InitReadContext(hFlashUnit, &context) == false) return false;
    *pVersionNumber = context.nTableVersion;
    *pUsingPrimary  = context.bUsingPrimary;
    return true;
}

/*____________________________________________________
 / LTDeviceFlashImpl library root interface binding */
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFlash)
    .GetPartition               = LTDeviceFlashImpl_GetPartition,
    .GetActiveFirmwarePartition = LTDeviceFlashImpl_GetActiveFirmwarePartition,
    .ErasePartition             = LTDeviceFlashImpl_ErasePartition,
    .EnumeratePartitions        = LTDeviceFlashImpl_EnumeratePartitions,
    .GetPartitionTableInfo      = LTDeviceFlashImpl_GetPartitionTableInfo
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 *  07-Nov-23   augustus    added ErasePartition
 */
