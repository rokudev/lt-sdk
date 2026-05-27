/******************************************************************************
 * lt/source/lt/system/crashdump/LTSystemCrashdump.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCrashdump.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/system/logger/LTSystemLogger.h>

#include <lt/system/crashdump/LTSystemCrashdump.h>

/*******************************************************************************
 * libraries
*******************************************************************************/

DEFINE_LTLOG_SECTION("crashdump");

#define SHELL_CMDS   0

#define SHELL_LIBS

#define LIB_OPEN(lib, libptr) pLibName = #lib; if (NULL == (libptr = lt_openlibrary(lib))) break;

enum {
    kFlashWriteSize            = 32,  /**< Flash write alignment buffer size. */

    kCrashdumpPolicy_Replace   = 0,   /**< Replace existing crashdump. */
    kCrashdumpPolicy_Keep      = 1,   /**< Retain crashdump until sent to cloud. */
};

typedef u8 FlashWriteState;
enum {
    kFlashWriteState_Armed     = 0,   /**< Flash writes are allowed but have not started. */
    kFlashWriteState_Started   = 1,   /**< Flash writes are in progress. */
    kFlashWriteState_Disabled  = 2,   /**< Flash Writes have ended and/or have been disabled. */
};

typedef struct {
    LT_SIZE offset;      /**< Partition byte offset in flash */
    u32     numSectors;  /**< Number of sectors in partition */
} Partition;

static LTDeviceFlash          *s_pDeviceFlash     = NULL;
static LTSystemLogger         *s_pSystemLogger    = NULL;
static ILTFlashDeviceUnit     *s_iFlashDeviceUnit = NULL;
static Partition               s_crashPartition   = {0, 0};
static Partition               s_logPartition     = {0, 0};
static LTDeviceUnit            s_hFlashDeviceUnitNormalContext;
static LTDeviceUnit            s_hFlashDeviceUnitInterruptContext;

static u8                      s_flashWriteBuffer[kFlashWriteSize];

static FlashWriteState         s_flashWriteState = kFlashWriteState_Armed;
static u8                      s_policy = kCrashdumpPolicy_Replace;

static LT_SIZE                 s_nCoreLen; /* content size in crashdump partition */
static LT_SIZE                 s_nLogLen;  /* content size in log partition (0 if absent) */
static char                    s_version[32];

 /* if we already checked for core, use the current s_nCoreLen+s_nLogLen instead of reading flash */
static bool bCoreReady = false;

static LT_SIZE LTSystemCrashdump_GetChunk(void *pBuffer, LT_SIZE nBufferSize, LT_SIZE nOffset) {
    if ((!pBuffer) || (!nBufferSize) || (nOffset + nBufferSize > s_nCoreLen + s_nLogLen)) return false;
    LT_SIZE nReadFromCrash = 0;
    LT_SIZE nReadFromLog = 0;
    if (nOffset < s_nCoreLen) {
        nReadFromCrash = LT_MIN(nBufferSize, s_nCoreLen - nOffset);
        if (!s_iFlashDeviceUnit->ReadBytes(s_hFlashDeviceUnitNormalContext, s_crashPartition.offset + nOffset, nReadFromCrash, (u8 *)pBuffer)) return 0;
        nOffset = s_nCoreLen;
    }
    if (s_logPartition.numSectors > 0 && nReadFromCrash < nBufferSize) {
        nReadFromLog = LT_MIN(nBufferSize - nReadFromCrash, s_nLogLen - (nOffset - s_nCoreLen));
        /* we get here if there is enough bytes in 'crashdump' or we are only reading from 'log' */
        if (!s_iFlashDeviceUnit->ReadBytes(s_hFlashDeviceUnitNormalContext, s_logPartition.offset + nOffset - s_nCoreLen, nReadFromLog, (u8 *)pBuffer + nReadFromCrash))
            return 0;
    }
    return nReadFromCrash + nReadFromLog;
}

static bool EraseCrashdumpPartition(LTDeviceUnit hDeviceUnit) {
    /* Erase the entire flash area for dumps */
    u32 nSector = s_iFlashDeviceUnit->ByteOffsetToSectorNumber(hDeviceUnit, s_crashPartition.offset);
    return s_iFlashDeviceUnit->EraseSectors(hDeviceUnit, nSector, s_crashPartition.numSectors);
}

static bool EraseLogBackupPartition(LTDeviceUnit hDeviceUnit) {
    if (s_logPartition.numSectors == 0) return true; /* No-op if log partition absent */
    /* Erase the entire flash area for log backup */
    u32 nSector = s_iFlashDeviceUnit->ByteOffsetToSectorNumber(hDeviceUnit, s_logPartition.offset);
    return s_iFlashDeviceUnit->EraseSectors(hDeviceUnit, nSector, s_logPartition.numSectors);
}

static LT_SIZE GetSize(LTDeviceUnit hDeviceUnit) LT_ISR_SAFE {
    if (bCoreReady) return (s_nCoreLen + s_nLogLen);
    s_nCoreLen = s_nLogLen = 0;
    LTCrashdumpHeader dumpHeader;
    LTLogBackupHeader backupHeader;
    if (!s_iFlashDeviceUnit->ReadBytes(hDeviceUnit, s_crashPartition.offset, sizeof(dumpHeader), (u8 *)&dumpHeader)) {
        if (!LT_GetCore()->InsideInterruptContext()) LTLOG_SERVER("fail.crashdump.read", "part");
        return 0;
    }
    if (dumpHeader.magic != kLTCrashdump_Partition_Magic) return 0;
    if (dumpHeader.numContexts == 0) {
        if (!LT_GetCore()->InsideInterruptContext())
            LTLOG_SERVER("fail.nocontexts", NULL);
        return 0;
    }
    /* Crashdump is encrypted, and it's possible but rare that four 0xFF bytes are decrypted into the
     * valid magic number. We'll read raw bytes into logbackup header (because we will still need
     * dumpHeader content) and check if the first eight bytes are 0xFF. If that happens, we exit
     * under the assumption that while the magic number's correct encryption is all Fs, the next four
     * bytes of Fs make it more likely it's an empty partition. */
    if (!s_iFlashDeviceUnit->ReadRawBytes(hDeviceUnit, s_crashPartition.offset, sizeof(backupHeader), (u8 *)&backupHeader)) return 0;
    if (*((u32 *)&backupHeader) == 0xFFFFFFFFU && *((u32 *)&backupHeader + 1) == 0xFFFFFFFFU) {
        return 0;
    }

    if (s_logPartition.numSectors > 0) {
        if (!s_iFlashDeviceUnit->ReadBytes(hDeviceUnit, s_logPartition.offset, sizeof(backupHeader), (u8 *)&backupHeader)) {
            if (!LT_GetCore()->InsideInterruptContext()) LTLOG_SERVER("fail.log.read", "part");
            return 0;
        } else if (backupHeader.magic != kLTLogBackup_Partition_Magic) {
            if (!LT_GetCore()->InsideInterruptContext()) LTLOG_DEBUG("fail.log.format", "magic 0x%lx", LT_Pu32(backupHeader.magic));
            /* It's OK if we don't have log data */
            s_nLogLen = 0;
        } else {
            /* log size doesn't include header */
            s_nLogLen  = backupHeader.logSize + sizeof(LTLogBackupHeader);
        }
    }
    s_nCoreLen = dumpHeader.totalSize;
    lt_memcpy(s_version, dumpHeader.swVersion, 32);
    bCoreReady = true;
    return (s_nCoreLen + s_nLogLen);
}

static LT_SIZE LTSystemCrashdump_GetSize(void) LT_ISR_SAFE {
    return GetSize(s_hFlashDeviceUnitNormalContext);
}

static const char * LTSystemCrashdump_GetVersion(void) {
    if (!bCoreReady) {
        if (!LTSystemCrashdump_GetSize()) return NULL;
    }
    return s_version;
}

static bool EraseAll(LTDeviceUnit hDeviceUnit) {
    bCoreReady = false;
    s_nCoreLen = s_nLogLen = 0;
    lt_memset(s_version, 0, sizeof(s_version));
    bool bCrashErase = EraseCrashdumpPartition(hDeviceUnit);
    bool bLogErase   = EraseLogBackupPartition(hDeviceUnit);
    return (bCrashErase && bLogErase);
}

static bool LTSystemCrashdump_Erase(void) {
    return EraseAll(s_hFlashDeviceUnitNormalContext);
}

/**************************************************************************************************
 * Crashdump and log backup writing code below is executed at the time of the crash.
 */
static void WriteLogBackup(const u8 * pBuffer, LT_SIZE bufSize) LT_ISR_SAFE {
    if (s_logPartition.numSectors == 0) return; /* Log partition absent: skip */
    if (!EraseLogBackupPartition(s_hFlashDeviceUnitInterruptContext) || !pBuffer || !bufSize) return;
    u32 nPartitionSize = s_logPartition.numSectors * s_iFlashDeviceUnit->GetBytesPerSector(s_hFlashDeviceUnitInterruptContext);
    if (nPartitionSize < bufSize + sizeof(LTLogBackupHeader)) {
        bufSize = nPartitionSize - sizeof(LTLogBackupHeader);
    }

    LT_STATIC_ASSERT(sizeof(LTLogBackupHeader) == kFlashWriteSize, "Incorrect size for LTLogBackupHeader");
    lt_memset(s_flashWriteBuffer, 0xFF, kFlashWriteSize);
    LTLogBackupHeader *backupHeader = (LTLogBackupHeader *)&s_flashWriteBuffer;
    backupHeader->magic = kLTLogBackup_Partition_Magic;
    backupHeader->logSize = bufSize;
    backupHeader->logBackupUTCTimestamp = LT_GetCore()->GetClockTimeUTC();
    if (!s_iFlashDeviceUnit->WriteBytes(s_hFlashDeviceUnitInterruptContext, s_logPartition.offset, kFlashWriteSize, s_flashWriteBuffer)) {
        return;
    }
    LT_SIZE nOffset = 0;
    LT_SIZE logBaseAddress = s_logPartition.offset + kFlashWriteSize;
    while (bufSize >= kFlashWriteSize) {
        if (!s_iFlashDeviceUnit->WriteBytes(s_hFlashDeviceUnitInterruptContext, logBaseAddress + nOffset, kFlashWriteSize, pBuffer + nOffset)) {
            return;
        }
        else {
            nOffset += kFlashWriteSize;
            bufSize -= kFlashWriteSize;
        }
    }
    lt_memset(s_flashWriteBuffer + bufSize, 0xFF, kFlashWriteSize - bufSize);
    lt_memcpy(s_flashWriteBuffer, pBuffer + nOffset, bufSize);
    s_iFlashDeviceUnit->WriteBytes(s_hFlashDeviceUnitInterruptContext, logBaseAddress + nOffset, kFlashWriteSize, s_flashWriteBuffer);
}

static void LTSystemCrashdump_WriteCrashdump(const u8 * pBuffer, LT_SIZE bufSize, LT_SIZE * pSpaceAvail) LT_ISR_SAFE {
    /* The next index in s_flashWriteBuffer available to write to, and also the number of bytes waiting to be written to flash. */
    static u8 s_bufferIdx = 0;
    bool bBeginLogBackup = false;
    u32 nPartitionSize = s_crashPartition.numSectors * s_iFlashDeviceUnit->GetBytesPerSector(s_hFlashDeviceUnitInterruptContext);
    if (s_flashWriteState == kFlashWriteState_Disabled)
        goto FAIL;
    if (s_flashWriteState == kFlashWriteState_Armed) {
        if (LTSystemCrashdump_GetSize()) {
            if (s_policy == kCrashdumpPolicy_Keep || !EraseAll(s_hFlashDeviceUnitInterruptContext))
                goto FAIL;
        }
        /* Initialize write for subsequent calls */
        *pSpaceAvail = nPartitionSize;
        s_flashWriteState = kFlashWriteState_Started;
    }
    else {
        /* Write to flash in kFlashWriteSize increments via alignment buffer */
        while (1) {
            u32 nBytesToCopy = kFlashWriteSize - s_bufferIdx;
            if (bufSize == 0) {
                bBeginLogBackup = true;
                /* Finalize write, padding/flushing as required */
                if (s_bufferIdx == 0) break;
                lt_memset(s_flashWriteBuffer + s_bufferIdx, 0xff, nBytesToCopy);
                s_bufferIdx = kFlashWriteSize;
                s_flashWriteState = kFlashWriteState_Disabled;
            }
            else {
                /* Copy as much as possible into alignment buffer */
                if (bufSize < nBytesToCopy) nBytesToCopy = bufSize;
                lt_memcpy(s_flashWriteBuffer + s_bufferIdx, pBuffer, nBytesToCopy);
                s_bufferIdx += nBytesToCopy;
                bufSize -= nBytesToCopy;
                pBuffer += nBytesToCopy;
            }
            if (*pSpaceAvail < nBytesToCopy) goto FAIL;
            *pSpaceAvail -= nBytesToCopy;
            if (s_bufferIdx == kFlashWriteSize) {
                LT_SIZE nOffset = s_crashPartition.offset + nPartitionSize - *pSpaceAvail - kFlashWriteSize;
                if (!s_iFlashDeviceUnit->WriteBytes(s_hFlashDeviceUnitInterruptContext, nOffset, kFlashWriteSize, s_flashWriteBuffer))
                    goto FAIL;
                s_bufferIdx = 0;
            }
            if (bufSize == 0) break;
        }
    }
    if (bBeginLogBackup && s_logPartition.numSectors > 0) {
        /* Core FaultHandler is done. We dump to "log" all the logs in the internal buffer. */
        /* reusing buffer pointer and size */
        s_pSystemLogger->GetInternalBuffer(&pBuffer, &bufSize);
        WriteLogBackup(pBuffer, bufSize);
    }
    return;
FAIL:
    *pSpaceAvail = 0;
    s_flashWriteState = kFlashWriteState_Disabled;
}

/*_________________________________
_/ LibFini and LibInit functions */
static void LTSystemCrashdumpImpl_LibFini(void) {
    lt_memset(s_version, 0, 32);
    bCoreReady = false;
    LT_GetCore()->RegisterCrashdumpWriteCallback(NULL);
    lt_destroyhandle(s_hFlashDeviceUnitNormalContext);
    lt_destroyhandle(s_hFlashDeviceUnitInterruptContext);
    lt_closelibrary(s_pSystemLogger);
    lt_closelibrary(s_pDeviceFlash);
}

static bool LTSystemCrashdumpImpl_LibInit(void) {
    const char *pLibName = NULL;
    do {
        LIB_OPEN(LTDeviceFlash, s_pDeviceFlash);
        if (!(s_hFlashDeviceUnitNormalContext    = s_pDeviceFlash->CreateDeviceUnitHandle(0))) break;
        if (!(s_hFlashDeviceUnitInterruptContext = s_pDeviceFlash->CreateDeviceUnitHandle(1))) break;
        /* This, and other parts of this Library, assume that both Device Units use the same interface: */
        s_iFlashDeviceUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashDeviceUnitInterruptContext);
        LTDeviceFlash_Partition crashPartition, logPartition;
        if (!s_pDeviceFlash->GetPartition(s_hFlashDeviceUnitNormalContext, "crashdump", &crashPartition)) {
            LTLOG_REDALERT("no.crashdump.partition", NULL);
            break;
        }
        s_crashPartition.offset = crashPartition.entry.nByteOffset;
        s_crashPartition.numSectors = crashPartition.entry.nNumSectors;
        
        if (s_pDeviceFlash->GetPartition(s_hFlashDeviceUnitNormalContext, "log", &logPartition)) {
            s_logPartition.offset = logPartition.entry.nByteOffset;
            s_logPartition.numSectors = logPartition.entry.nNumSectors;
            LIB_OPEN(LTSystemLogger, s_pSystemLogger);
        } else {
            s_logPartition.offset = 0;
            s_logPartition.numSectors = 0;
        }
        LT_GetCore()->RegisterCrashdumpWriteCallback(&LTSystemCrashdump_WriteCrashdump);
        pLibName = NULL;
    }
    while (false);
    lt_memset(s_version, 0, 32);
    if (pLibName) {
        LTSystemCrashdumpImpl_LibFini();
        return false;
    };
    return true;
}

/*___________________________________________
 / LTSystemCrashdump library interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemCrashdump) {
    .Erase      = &LTSystemCrashdump_Erase,
    .GetSize    = &LTSystemCrashdump_GetSize,
    .GetVersion = &LTSystemCrashdump_GetVersion,
    .GetChunk   = &LTSystemCrashdump_GetChunk
} LTLIBRARY_DEFINITION;
