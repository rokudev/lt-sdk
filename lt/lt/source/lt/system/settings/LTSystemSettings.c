/*******************************************************************************
 * lt/source/lt/system/settings/LTSystemSettings.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/device/watchdog/LTDeviceWatchdog.h>
#include <lt/system/settings/LTSystemSettings.h>

/*
 * LT System Settings
 *
 * Introduction
 *   LTSystemSettings is used for the persistent storage of small data items in the form
 *   of (key, value) pairs. Settings allows storage of integer, string and binary data.
 *   Settings is designed to minimize RAM usage since settings will only be retrieved on
 *   demand.
 *
 * Flush
 *   Flash is written at most every kFlushTimeoutInSeconds seconds. Any settings change
 *   will reset this timer. This helps prevent excessive re-writes of flash and simplifies
 *   the API.
 *
 * Write Cache
 *   A write cache is maintained in RAM for each settings section. This cache maintains
 *   the list of changes to be performed on data in persistent storage. Cache operations
 *   include: add/replace, delete and delete by key prefix. To minimize RAM, the write cache
 *   only stores the latest change to a given key/value pair--it will not retain the entire
 *   transaction history.
 *
 * Ping/Pong
 *   Settings use ping/pong storage to provide a fallback in case one side is corrupted. A
 *   counter is maintained on each side, where the side with the higher count value is used.
 *   The counter is written only after the section contents are validated.
 */

// Possible improvements:
//  1. If there are no changes (such as deletes that do nothing) then do not write settings.
//    Option 1 -> two pass flush.
//    Option 2 -> check if settings changed every time on writes, unless a write is already in the queue.
//  2. Detect too many writes and back-off.

#define LTSYSTEMSETTINGS_LOGGER_SECTION     "lt.system.settings"
#define LTSYSTEMSETTINGS_THREAD_STACKSIZE   (1000)

#ifdef LT_BIGENDIAN
#define IS_BIGENDIAN    1
#else
#define IS_BIGENDIAN    0
#endif

/* Align value up to nearest boundary (NOTE: mask = alignment - 1) */
#define ALIGN32(val, mask)        (((u32)(val) + (mask)) & ~((u32)(mask)))

enum {
    kNameSeparator                   = '/',   /**< Name separator for sections and optionally suffixes */
    kMaxSuffixLen                    = 255,   /**< Maximum suffix length, not including null-terminator */
    kCopyBufferSize                  = 128,   /**< Size of flash->flash copy buffer */
    kFlushTimeoutInSeconds           = 5,     /**< Delay between last settings write and write to flash */
    kMinWriteQuantum                 = 32,    /**< Minimum write size and alignment in bytes */
    kMaxWriteQuantum                 = 128,   /**< Maximum write size and alignment in bytes */
    kFlushReportInterval             = 16384, /**< Report once for this many write cycles (power-of-2) */
    kSettingsThreadFidelityInSeconds = 30     /**< Required thread response fidelity for the settings thread */
};

/*********************
 * Settings sections */
typedef u8 SectionType;
enum SectionType {
    kSectionType_ReadWrite,      /**< Writeable partition with built-in redundancy */
};

/* Section State */
typedef u8 SectionState;
enum SectionState {
    /* NB: The order of these states is important for state decoding (see below) */
    kSectionState_Unknown = 0,   /**< State not determined yet */
    kSectionState_FatalError,    /**< Fatal error encountered, do not retry */
    kSectionState_Error,         /**< Error occurred, can retry */
    kSectionState_AppearsBlank,  /**< Section appears blank */
    kSectionState_PingActive,    /**< Section good, use ping */
    kSectionState_PongActive,    /**< Section good, use pong */
};

/* State decoding */
#define HAS_READABLE_FLASH(pR)   ((pR)->state >= kSectionState_PingActive)
#define HAS_WRITEABLE_FLASH(pR)  ((pR)->state >= kSectionState_Error)
#define IS_REGION_READABLE(pR)   (NOT USED, assumed 1)
#define IS_REGION_WRITEABLE(pR)  ((pR)->state > kSectionState_FatalError)

/* Process type */
typedef enum ProcessType {
    kProcessType_Normal,        /**< Normal processing (e.g.: flush) */
    kProcessType_ExplicitWrite, /**< Explicit flush */
    kProcessType_Final          /**< Final processing--free resources */
} ProcessType;

/* Operation status */
typedef u8 OpStatus;
enum OpStatus {
    kOpStatus_OK = 0,           /**< Operation worked */
    kOpStatus_OK_NoMatch,       /**< Operation worked but no match */
    kOpStatus_DeviceReadError,  /**< Device returned read error */
    kOpStatus_DeviceEraseError, /**< Device returned erase error */
    kOpStatus_DeviceWriteError, /**< Device returned write error */
    kOpStatus_WriteOverflow,    /**< Write overflowed section */
    kOpStatus_ReadOverflow,     /**< Read overflowed section */
    kOpStatus_VerifyError,      /**< Data verification failure */
    kOpStatus_InvalidState,     /**< Invalid state for operation */
};

/* Settings data types */
typedef u8 CacheDataType;
enum CacheDataType {
    kCacheDataType_Integer       = kLTSystemSettingsDataType_Integer,
    kCacheDataType_String        = kLTSystemSettingsDataType_String,
    kCacheDataType_Binary        = kLTSystemSettingsDataType_Binary,
    kCacheDataType_Deleted       = kLTSystemSettingsDataType_Deleted,
    kCacheDataType_DeletedPrefix = 'p'
};

/* Setting Section Magic Number */
typedef u32 SettingsMagic;
enum SettingsMagic {
    kSettingsMagic_ReadWrite    = 0x30544553,
};

/* Flash Setting Type */
typedef u8 FlashDataType;
enum FlashDataType {
    kFlashDataType_String       = kCacheDataType_String,  /**< Strings */
    kFlashDataType_Binary       = kCacheDataType_Binary,  /**< Binary blobs */
    kFlashDataType_EndOfList    = 0xff,                   /**< End of Settings */
    /* Integer types */
    kFlashDataType_Integer8     = 0x01,  /**< Packed 8-bit integer */
    kFlashDataType_Integer16    = 0x02,  /**< Packed 16-bit integer */
    kFlashDataType_Integer32    = 0x04,  /**< Packed 32-bit integer */
    kFlashDataType_Integer64    = 0x08,  /**< Signed 64-bit integer */
    /* Masks for integer types */
    kFlashDataType_IntSizeMask  = 0x0f,  /**< Mask for integer size */
    kFlashDataType_IntNegMask   = 0x10,  /**< Mask for integer negation */
    /* Integer type max */
    kFlashDataType_IntTypeMax   = kFlashDataType_IntNegMask + kFlashDataType_Integer64,
                                         /**< Used to differentiate int from non-int types */
};

/* Settings match return value */
typedef enum SettingsMatch {
    kSettingsMatch_Full,         /**< Found setting/delete with exact name match */
    kSettingsMatch_Prefix,       /**< Found setting/delete with matching prefix */
    kSettingsMatch_DeletePrefix, /**< Found delete prefix with prefix or exact match */
    kSettingsMatch_EndOfList,    /**< At end of list */
} SettingsMatch;

/* A Section of Settings */
typedef struct {
    LTList_Node         node;
    SectionType         type;
    SectionState        state;
    u8                  nPrefixLen;
    u8                  nSizeInSectors;
    union {
        struct {
            LTList      writeCache;
            u32         nStartOffset;
            u16         nWriteQuantum;
        };
    };
    char                prefix[0];
} SettingsSection;

/* Setting Cached in Memory */
typedef struct {
    LTList_Node         node;
    CacheDataType       type;
    union {
        s64             nInteger;
        struct {
            u8        * pValue;
            u32         nSizeInBytes;
        };
    };
    char                suffix[0];
} CachedSetting;

/* Flash Read State */
typedef struct {
    SettingsSection   * pSection;
    u32                 nReadOffset;
    u32                 nEndOffset;
    struct {
        FlashDataType   type;
        u8              nNameLength;
    } hdr;
} ReadState;

/* Flash Write State */
typedef struct {
    SettingsSection   * pSection;
    u8                * pBuffer;
    u32                 nBufferIdx;
    u32                 nWriteOffset;
    u32                 nEndOffset;
    u32                 nCrc;
} WriteState;

DEFINE_LTLOG_SECTION(LTSYSTEMSETTINGS_LOGGER_SECTION);

/* The default settings section always exists, even if it is just cached memory */
static SettingsSection        s_defaultSection;

static LTMutex              * s_mutex   = NULL;
static LTHandle               s_hThread = 0;
static ILTThread            * s_iThread = NULL;

static LTDeviceFlash        * s_pDeviceFlash = NULL;
static LTDeviceUnit           s_hFlashUnit   = 0;
static ILTFlashDeviceUnit   * s_iFlashUnit   = NULL;

static LTUtilityByteOps     * s_pUtilityByteOps = NULL;
static LTDeviceWatchdog     * s_pDeviceWatchdog = NULL;

static void SettingsFlushProc(void *);

/* Find settings section using key prefix and then advancing *ppKey to key suffix */
static SettingsSection *
FindSettingsSection(const char ** ppKey) {
    if (*ppKey == NULL) return NULL;
    if ((*ppKey)[0] != kNameSeparator) return &s_defaultSection;
    for (LTList_Node * pNode = s_defaultSection.node.pNext;
            pNode != &s_defaultSection.node; pNode = pNode->pNext) {
        SettingsSection * pSection = LT_CONTAINER_OF(pNode, SettingsSection, node);
        if ((lt_strncmp(pSection->prefix, *ppKey, pSection->nPrefixLen) == 0)) {
            *ppKey += pSection->nPrefixLen;
            return pSection;
        }
    }
    return NULL;
}

static bool
ReplaceCachedSettingValue(CachedSetting * pSetting, CacheDataType type,
                             const u8 * pValue, u32 nSizeInBytes) {

    bool bOldTypeIsBlob = (pSetting->type == kCacheDataType_String) ||
                              (pSetting->type == kCacheDataType_Binary);
    bool bNewTypeIsBlob = (type == kCacheDataType_String) ||
                              (type == kCacheDataType_Binary);
    if (bOldTypeIsBlob) {
        if (bNewTypeIsBlob) {
            void * pValue = lt_realloc(pSetting->pValue, nSizeInBytes);
            if (!pValue) return false;
            pSetting->pValue = pValue;
        } else {
            lt_free(pSetting->pValue);
        }
    } else if (bNewTypeIsBlob) {
        void * pValue = lt_malloc(nSizeInBytes);
        if (!pValue) return false;
        pSetting->pValue = pValue;
    }
    pSetting->type = type;
    switch (pSetting->type) {
    case kCacheDataType_Integer:
        pSetting->nInteger = *((s64 *)pValue);
        break;
    case kCacheDataType_String:
    case kCacheDataType_Binary:
        lt_memcpy(pSetting->pValue, pValue, nSizeInBytes);
        pSetting->nSizeInBytes = nSizeInBytes;
        break;
    default:
        break;
    }
    return true;
}

static void
UnlinkAndFreeCachedSetting(LTList_Node * pNode) {
    LTList_Remove(pNode);
    CachedSetting * pSetting = LT_CONTAINER_OF(pNode, CachedSetting, node);
    if (pSetting->type == kCacheDataType_String ||
           pSetting->type == kCacheDataType_Binary) {
        lt_free(pSetting->pValue);
    }
    lt_free(pSetting);
}

static CachedSetting *
CreateCachedSetting(const char * pSuffix, CacheDataType type, const u8 * pValue,
                       u32 nSizeInBytes) {
    LT_SIZE nSuffixLen = lt_strlen(pSuffix);
    if (nSuffixLen > kMaxSuffixLen) return NULL;
    CachedSetting * pSetting =
                        (CachedSetting *)lt_malloc(sizeof(CachedSetting) + nSuffixLen + 1);
    if (!pSetting) return NULL;
    lt_strncpyTerm(pSetting->suffix, pSuffix, nSuffixLen + 1);
    pSetting->type = type;
    switch (type) {
    case kCacheDataType_Integer:
        pSetting->nInteger = *((s64 *)pValue);
        break;
    case kCacheDataType_String:
    case kCacheDataType_Binary:
        pSetting->pValue = (u8 *)lt_malloc(nSizeInBytes);
        if (pSetting->pValue == NULL) {
            lt_free(pSetting);
            return NULL;
        }
        lt_memcpy(pSetting->pValue, pValue, nSizeInBytes);
        pSetting->nSizeInBytes = nSizeInBytes;
        break;
    default:
        break;
    }
    return pSetting;
}

static SettingsMatch
MatchNextCachedSetting(SettingsSection * pSection, LTList_Node ** ppNode, const char * pName) {
    SettingsMatch match = kSettingsMatch_EndOfList;
    for (; *ppNode != &pSection->writeCache; *ppNode = (*ppNode)->pPrev) {
        CachedSetting * pSetting = LT_CONTAINER_OF(*ppNode, CachedSetting, node);
        for (u32 nIdx = 0; match == kSettingsMatch_EndOfList; nIdx++) {
            if (pName[nIdx] == '\0') {
                if (pSetting->suffix[nIdx] == '\0') {
                    if (pSetting->type == kCacheDataType_DeletedPrefix)
                        match = kSettingsMatch_DeletePrefix;
                    else
                        match = kSettingsMatch_Full;
                } else match = kSettingsMatch_Prefix;
            } else if (pSetting->suffix[nIdx] == '\0') {
                if (pSetting->type == kCacheDataType_DeletedPrefix)
                    match = kSettingsMatch_DeletePrefix;
                else break;
            } else if (pName[nIdx] != pSetting->suffix[nIdx]) break;
        }
        if (match != kSettingsMatch_EndOfList) break;
    }
    return match;
}

/* Write cache management
 *   In each case, walk write cache from tail to head:
 *     1. Adding a write to cache
 *       a. On matching write or delete, replace setting, stop
 *       b. On end of queue or deletion prefix match, add setting to tail, stop
 *     2. Adding a deletion to cache
 *       a. On matching write or delete, replace setting, stop
 *       b. On matching deletion prefix, stop
 *       c. On end of queue, add deletion to tail, stop
 *     3. Adding a deletion prefix
 *       a. If matched prefix, stop.
 *       b. If encountered matching write or delete, remove it, continue.
 *       c. On end of queue, add prefix to tail, stop  */
static bool
SetValue(const char * pKey, CacheDataType type, const u8 * pValue, u32 nSizeInBytes) {
    const char * pSuffix = pKey;
    s_mutex->API->Lock(s_mutex);
    SettingsSection * pSection = FindSettingsSection(&pSuffix);
    if (!pSection || !pValue) goto BAIL;
    if (!IS_REGION_WRITEABLE(pSection)) goto BAIL;
    LTList_Node * pNode = pSection->writeCache.pPrev;
    SettingsMatch match;
    do {
        match = MatchNextCachedSetting(pSection, &pNode, pSuffix);
        LTList_Node * pNodeSave = pNode->pPrev;
        switch (match) {
        case kSettingsMatch_DeletePrefix:
            if (type == kCacheDataType_Deleted || type == kCacheDataType_DeletedPrefix) {
                match = kSettingsMatch_EndOfList;
                break;
            }
            /* Fallthrough */
        case kSettingsMatch_EndOfList: {
            CachedSetting * pSetting = CreateCachedSetting(pSuffix, type, pValue, nSizeInBytes);
            if (!pSetting) goto BAIL;
            LTList_AddTail(&pSection->writeCache, &pSetting->node);
            match = kSettingsMatch_EndOfList;
            break;
        }
        case kSettingsMatch_Full:
            if (type != kCacheDataType_DeletedPrefix) {
                if (!ReplaceCachedSettingValue(LT_CONTAINER_OF(pNode, CachedSetting, node), type,
                                                  pValue, nSizeInBytes)) goto BAIL;
                match = kSettingsMatch_EndOfList;
            } else {
                /* Node name exactly matches delete prefix */
                UnlinkAndFreeCachedSetting(pNode);
            }
            break;
        case kSettingsMatch_Prefix:
            if (type == kCacheDataType_DeletedPrefix) {
                UnlinkAndFreeCachedSetting(pNode);
            }
            break;
        }
        pNode = pNodeSave;
    } while (match != kSettingsMatch_EndOfList);
    s_mutex->API->Unlock(s_mutex);
    if (s_iThread) {
        s_iThread->SetTimer(s_hThread, LTTime_Seconds(kFlushTimeoutInSeconds), &SettingsFlushProc, NULL, NULL);
    }
    return true;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    return false;
}

/* Read from flash, while checking for overflow and errors, advancing offset */
static OpStatus
ReadFromFlash(ReadState * pState, u8 * pData, u32 nSizeInBytes) {
    if (pState->nReadOffset + nSizeInBytes > pState->nEndOffset)
        return kOpStatus_ReadOverflow;
    if (!s_iFlashUnit->ReadBytes(s_hFlashUnit, pState->nReadOffset, nSizeInBytes, (u8 *)pData))
        return kOpStatus_DeviceReadError;
    pState->nReadOffset += nSizeInBytes;
    return kOpStatus_OK;
}

/* Match the next setting in flash using name or name prefix
 *   If full name match, leave read offset at start of data,
 *   If partial name match, leave read state in name just after matched part. */
static OpStatus
MatchNextSettingInFlash(ReadState * pState, const char * pName, bool bMatchPrefix) {
    while (1) {
        OpStatus status = ReadFromFlash(pState, (u8 *)&pState->hdr, sizeof(pState->hdr));
        if (status) return status;
        if (pState->hdr.type == kFlashDataType_EndOfList) return kOpStatus_OK_NoMatch;
        /* Attempt for complete or (optionally) partial match */
        u32 nIdx = 0;
        while (1) {
            if (pName[nIdx] == '\0' && (bMatchPrefix || nIdx == pState->hdr.nNameLength)) return kOpStatus_OK;
            if (pName[nIdx] == '\0' || nIdx == pState->hdr.nNameLength) break;
            u8 nCh;
            status = ReadFromFlash(pState, &nCh, sizeof(nCh));
            if (status) return status;
            if (pName[nIdx++] != nCh) break;
        }
        /* Skip rest of name and advance to next setting */
        pState->nReadOffset += (pState->hdr.nNameLength - nIdx);
        if (pState->hdr.type <= kFlashDataType_IntTypeMax) {
            pState->nReadOffset += (pState->hdr.type & kFlashDataType_IntSizeMask);
        } else {
            u16 nSizeInBytes;
            status = ReadFromFlash(pState, (u8 *)&nSizeInBytes, sizeof(nSizeInBytes));
            if (status) return status;
            pState->nReadOffset += nSizeInBytes;
        }
    }
}

/* Read the latest setting value from either cache or flash */
static bool
GetValue(const char * pKey, CacheDataType type, u8 * pValue, u32 * pSizeInBytes) {
    const char * pSuffix = pKey;
    s_mutex->API->Lock(s_mutex);
    SettingsSection * pSection = FindSettingsSection(&pSuffix);
    if (!pSection) {
        s_mutex->API->Unlock(s_mutex);
        return false;
    }
    /* First look for data in write cache */
    LTList_Node * pNode = pSection->writeCache.pPrev;
    SettingsMatch match;
    OpStatus status = kOpStatus_OK;
    while (1) {
        match = MatchNextCachedSetting(pSection, &pNode, pSuffix);
        if (match != kSettingsMatch_Prefix) break;
        pNode = pNode->pPrev;
    }
    if (match != kSettingsMatch_EndOfList) {
        CachedSetting * pSetting = LT_CONTAINER_OF(pNode, CachedSetting, node);
        /* NOTE: This check catches delete, delete prefix AND type mismatch */
        if (pSetting->type != type) goto BAIL;
        /* Read setting from write cache */
        if (type == kCacheDataType_Integer) {
            if (pValue) *((s64 *)pValue) = pSetting->nInteger;
        } else if (type == kCacheDataType_String) {
            if (pValue) {
                LTString *pString = (LTString *)pValue;
                u16 currentCap = 0;
                if (*pString) {
                    currentCap = LT_GetCore()->GetAllocSize(*pString);
                    LT_ASSERT(currentCap);
                }
                if (pSetting->nSizeInBytes > currentCap) {
                    LTString pTemp = lt_realloc(*pString, pSetting->nSizeInBytes);
                    if (!pTemp) goto BAIL;
                    *pString = pTemp;
                }
                lt_memcpy(*pString, pSetting->pValue, pSetting->nSizeInBytes);
            }
        } else {
            if (!pSizeInBytes) goto BAIL;
            u32 nSizeInBytes = *pSizeInBytes;
            *pSizeInBytes = pSetting->nSizeInBytes;
            pSizeInBytes = NULL;
            if (pSetting->nSizeInBytes > nSizeInBytes) goto BAIL;
            if (pValue) lt_memcpy(pValue, pSetting->pValue, pSetting->nSizeInBytes);
        }
    } else {
        if (!HAS_READABLE_FLASH(pSection)) goto BAIL;
        /* Read setting from flash */
        ReadState readState = {
            .pSection    = pSection,
            .nReadOffset = pSection->nStartOffset
        };
        u32 nAreaSize = pSection->nSizeInSectors * s_iFlashUnit->GetBytesPerSector(s_hFlashUnit);
        if (pSection->state == kSectionState_PongActive) readState.nReadOffset += nAreaSize;
        readState.nEndOffset = readState.nReadOffset + nAreaSize - 1;
        readState.nReadOffset += sizeof(SettingsMagic) + sizeof(u32);
        readState.nReadOffset = ALIGN32(readState.nReadOffset, pSection->nWriteQuantum - 1);
        status = MatchNextSettingInFlash(&readState, pSuffix, false);
        if (status) goto BAIL;
        if (readState.hdr.type <= kFlashDataType_IntTypeMax) {
            if (type != kCacheDataType_Integer) goto BAIL;
            /* Read and unpack integer */
            if (pValue) {
                bool bNegate = readState.hdr.type & kFlashDataType_IntNegMask;
                if (bNegate) readState.hdr.type -= kFlashDataType_IntNegMask;
                *((s64 *)pValue) = 0;
                pValue += IS_BIGENDIAN * (sizeof(s64) - readState.hdr.type);
                status = ReadFromFlash(&readState, (u8 *)pValue, readState.hdr.type);
                if (status) goto BAIL;
                if (bNegate) *((s64 *)pValue) = -*((s64 *)pValue);
            }
        } else {
            if (type != readState.hdr.type) goto BAIL;
            /* Read blob */
            u16 nBlobSizeInBytes;
            status = ReadFromFlash(&readState, (u8 *)&nBlobSizeInBytes, sizeof(nBlobSizeInBytes));
            if (status) goto BAIL;
            if (type == kCacheDataType_String) {
                if (pValue) {
                    LTString *pString = (LTString *)pValue;
                    u16 currentCap = 0;
                    if (*pString) {
                        currentCap = LT_GetCore()->GetAllocSize(*pString);
                        LT_ASSERT(currentCap);
                    }
                    if (nBlobSizeInBytes + 1 > currentCap) {
                        LTString pTemp = lt_realloc(*pString, nBlobSizeInBytes + 1);
                        if (!pTemp) goto BAIL;
                        *pString = pTemp;
                    }
                    status = ReadFromFlash(&readState, (u8 *)*pString, nBlobSizeInBytes);
                    if (status) goto BAIL;
                    (*pString)[nBlobSizeInBytes] = '\0';
                }
            } else {
                if (!pSizeInBytes) goto BAIL;
                u32 nSizeInBytes = *pSizeInBytes;
                *pSizeInBytes = nBlobSizeInBytes;
                pSizeInBytes = NULL;
                if (nBlobSizeInBytes > nSizeInBytes) goto BAIL;
                if (pValue) {
                    status = ReadFromFlash(&readState, pValue, nBlobSizeInBytes);
                    if (status) goto BAIL;
                }
            }
        }
    }
    s_mutex->API->Unlock(s_mutex);
    return true;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    if (status > kOpStatus_OK_NoMatch) LTLOG_YELLOWALERT("get", "Read failure, reason %u", status);
    if (pSizeInBytes) *pSizeInBytes = 0;
    return false;
}

/* Most integers do not require 64 bits of flash storage */
static FlashDataType
PackInteger(void * pBuffer, s64 nValue) {
    bool bNegate = (nValue < 0);
    if (bNegate) nValue = -nValue;
    FlashDataType nSize = kFlashDataType_Integer32;
    if (nValue < 0x10000) {
        if (nValue < 0x100) nSize = kFlashDataType_Integer8;
        else nSize = kFlashDataType_Integer16;
    } else if (nValue >= 0x100000000) nSize = kFlashDataType_Integer64;
    lt_memcpy(pBuffer, (u8 *)&nValue, nSize);
    return (bNegate ? kFlashDataType_IntNegMask | nSize : nSize);
}

/* Write to flash, while checking for overflow and errors, advancing offset, calculating CRC,
 *   whilst maintaining the required write quantum. */
static OpStatus
WriteToFlash(WriteState * pState, const u8 * pData, u32 nSizeInBytes) {
    if (pState->nWriteOffset + nSizeInBytes > pState->nEndOffset)
        return kOpStatus_WriteOverflow;
    s_pUtilityByteOps->Crc32(pData, nSizeInBytes, &pState->nCrc);
    while (nSizeInBytes > 0) {
        u32 nBytesToCopy = kMaxWriteQuantum - pState->nBufferIdx;
        if (nSizeInBytes < nBytesToCopy) nBytesToCopy = nSizeInBytes;
        nSizeInBytes -= nBytesToCopy;
        lt_memcpy(pState->pBuffer + pState->nBufferIdx, pData, nBytesToCopy);
        pData += nBytesToCopy;
        pState->nBufferIdx += nBytesToCopy;
        if (pState->nBufferIdx == kMaxWriteQuantum) {
            pState->nBufferIdx = 0;
            if (!s_iFlashUnit->WriteBytes(s_hFlashUnit, pState->nWriteOffset, kMaxWriteQuantum, pState->pBuffer))
                return kOpStatus_DeviceWriteError;
            pState->nWriteOffset += kMaxWriteQuantum;
        }
    }
    return kOpStatus_OK;
}

/* Pad and flush final sub-quantum sized buffer to flash */
static OpStatus
FinalWriteToFlash(WriteState * pState) {
    /* Note: overflow has already been checked */
    /* Note: CRC has already been written -- No need to maintain CRC */
    if (pState->nBufferIdx > 0) {
        lt_memset(pState->pBuffer + pState->nBufferIdx, 0xff, kMaxWriteQuantum - pState->nBufferIdx);
        if (!s_iFlashUnit->WriteBytes(s_hFlashUnit, pState->nWriteOffset, kMaxWriteQuantum, pState->pBuffer))
            return kOpStatus_DeviceWriteError;
        /* Account for last actual byte, not full quantum */
        pState->nWriteOffset += pState->nBufferIdx;
    }
    return kOpStatus_OK;
}

/* Flash Section Header format
 *
 *   Settings Section Header Ping (or Pong):
 *     +---------+----------+
 *     |  Magic* | Counter  |
 *     |   u32   |   s32    |
 *     +---------+----------+
 *        * Magic number written after section contents validated
 *
 * Individual setting format
 *   Settings are stored sequentially after header in a packed arrangement.
 *   NOTE: First setting is aligned to write quantum.
 *
 *   String / Binary:
 *     +----+----+----------+------+---------------------+
 *     |type|len1|   name   | len2 |       value         |
 *     | u8 | u8 |   len1   |  u16 |        len2         |
 *     +----+----+----------+------+---------------------+
 *
 *   Packed Integer:
 *     +----+----+----------+----------------------+
 *     |type|len1|   name   |     packed value*    |
 *     | u8 | u8 |   len1   | u8 / u16 / u32 / u64 |
 *     +----+----+----------+----------------------+
 *         * Integer size/sign is encoded in type
 *
 *   End Of Settings List:
 *     +----+---------------------+
 *     |type|         CRC         |
 *     | 255|         u32         |
 *     +----+----------------------   */
/* Sync write cache to flash for a given section */
static OpStatus
FlushSectionToFlash(SettingsSection * pSection, char * pScratch) {
    OpStatus status;
    /* Determine read and write offsets */
    WriteState writeState = {
        .pSection     = pSection,
        .pBuffer      = (u8 *)pScratch + kMaxSuffixLen + 1,
        .nBufferIdx   = 0,
        .nWriteOffset = pSection->nStartOffset + sizeof(SettingsMagic),
        .nCrc         = 0
    };
    ReadState readState = {
        .pSection     = pSection,
        .nReadOffset  = writeState.nWriteOffset
    };
    u32 nAreaSize = pSection->nSizeInSectors * s_iFlashUnit->GetBytesPerSector(s_hFlashUnit);
    bool bReadOtherArea = false;
    switch (pSection->state) {
    case kSectionState_PingActive:
        writeState.nWriteOffset += nAreaSize;
        bReadOtherArea = true;
        break;
    case kSectionState_PongActive:
        bReadOtherArea = true;
        /* fallthrough */
    default:
        /* Do not read other area if blank or error */
        readState.nReadOffset += nAreaSize;
        break;
    case kSectionState_Unknown:
    case kSectionState_FatalError:
        return kOpStatus_InvalidState;
    }
    writeState.nEndOffset = writeState.nWriteOffset + nAreaSize - sizeof(SettingsMagic) - 1;
    readState.nEndOffset  = readState.nReadOffset + nAreaSize - sizeof(SettingsMagic) - 1;
    u32 nSizeInSectors    = pSection->nSizeInSectors;
    u32 nStartSector      = s_iFlashUnit->ByteOffsetToSectorNumber(s_hFlashUnit, writeState.nWriteOffset);
    /* Erase write section */
    if (!s_iFlashUnit->EraseSectors(s_hFlashUnit, nStartSector, nSizeInSectors))
        return kOpStatus_DeviceEraseError;
    /* If read area is valid, read and increment count, then write it */
    u32 nCount = 0;
    if (bReadOtherArea) {
        if (!s_iFlashUnit->ReadRawBytes(s_hFlashUnit, readState.nReadOffset, sizeof(nCount), (u8 *)&nCount))
            return kOpStatus_DeviceReadError;
        if ((++nCount & (kFlushReportInterval - 1)) == 0) {
            if (pSection == &s_defaultSection) {
                LTLOG_SERVER("flush.cyc", "Write Cycles: %lu", LT_Pu32(nCount));
            } else {
                LTLOG_SERVER("flush.cyc.other", "%s: Write Cycles: %lu", pSection->prefix, LT_Pu32(nCount));
            }
        }
    }
    if (!s_iFlashUnit->WriteRawBytes(s_hFlashUnit, writeState.nWriteOffset, sizeof(nCount), (u8 *)&nCount))
        return kOpStatus_DeviceWriteError;
    /* Verify magic number of erased region is erased and check write of counter value */
    if (!s_iFlashUnit->ReadRawBytes(s_hFlashUnit, writeState.nWriteOffset - sizeof(SettingsMagic), 2*sizeof(u32), (u8 *)pScratch))
        return kOpStatus_DeviceReadError;
    if (((u32 *)pScratch)[0] != 0xffffffff) return kOpStatus_DeviceEraseError;
    if (((u32 *)pScratch)[1] != nCount) return kOpStatus_VerifyError;
    readState.nReadOffset   += sizeof(u32);
    writeState.nWriteOffset += sizeof(u32);
    writeState.nWriteOffset = ALIGN32(writeState.nWriteOffset, pSection->nWriteQuantum - 1);
    u32 nBytesToCheck = writeState.nWriteOffset;
    if (bReadOtherArea) {
        readState.nReadOffset = ALIGN32(readState.nReadOffset, pSection->nWriteQuantum - 1);
        /* Read current settings while reconciling against cache */
        while (1) {
            OpStatus status = MatchNextSettingInFlash(&readState, "", true);
            if (status == kOpStatus_OK_NoMatch) break;
            else if (status) return status;
            /* Read name and look for match in write cache */
            status = ReadFromFlash(&readState, (u8 *)pScratch, readState.hdr.nNameLength);
            if (status) return status;
            pScratch[readState.hdr.nNameLength] = '\0';
            LTList_Node * pNode = pSection->writeCache.pPrev;
            SettingsMatch match;
            do {
                match = MatchNextCachedSetting(pSection, &pNode, pScratch);
                switch (match) {
                case kSettingsMatch_Prefix:
                    /* Prefix -> Retry match */
                    break;
                case kSettingsMatch_Full:
                case kSettingsMatch_DeletePrefix:
                    /* Full -> Advance and replace with new setting later
                     * Delete/DeletePrefix -> Advance, ignoring current value */
                    if (readState.hdr.type <= kFlashDataType_IntTypeMax) {
                        readState.nReadOffset += (readState.hdr.type & kFlashDataType_IntSizeMask);
                    } else {
                        u16 nSizeInBytes;
                        status = ReadFromFlash(&readState, (u8 *)&nSizeInBytes, sizeof(nSizeInBytes));
                        if (status) return status;
                        readState.nReadOffset += nSizeInBytes;
                    }
                    break;
                case kSettingsMatch_EndOfList:
                    /* Not matched -> Copy/Advance */
                    status = WriteToFlash(&writeState, (u8 *)&readState.hdr, sizeof(readState.hdr));
                    if (status) return status;
                    status = WriteToFlash(&writeState, (u8 *)pScratch, readState.hdr.nNameLength);
                    if (status) return status;
                    if (readState.hdr.type <= kFlashDataType_IntTypeMax) {
                        u8 buffer[sizeof(s64)];
                        status = ReadFromFlash(&readState, buffer,
                                                 readState.hdr.type & kFlashDataType_IntSizeMask);
                        if (status) return status;
                        status = WriteToFlash(&writeState, buffer,
                                                 readState.hdr.type & kFlashDataType_IntSizeMask);
                        if (status) return status;
                    } else {
                        u16 nSizeInBytes;
                        status = ReadFromFlash(&readState, (u8 *)&nSizeInBytes, sizeof(u16));
                        if (status) return status;
                        status = WriteToFlash(&writeState, (u8 *)&nSizeInBytes, sizeof(u16));
                        if (status) return status;
                        for (u32 nIdx = 0; nIdx < nSizeInBytes;) {
                            u32 nBytesToCopy = kCopyBufferSize;
                            if (nSizeInBytes - nIdx < kCopyBufferSize)
                                nBytesToCopy = nSizeInBytes - nIdx;
                            status = ReadFromFlash(&readState, (u8 *)pScratch, nBytesToCopy);
                            if (status) return status;
                            status = WriteToFlash(&writeState, (u8 *)pScratch, nBytesToCopy);
                            if (status) return status;
                            nIdx += nBytesToCopy;
                        }
                    }
                    break;
                }
                pNode = pNode->pPrev;
            } while (match == kSettingsMatch_Prefix);
        }
    }
    /* Write modified settings from cache to flash */
    LTList_Node * pNode = pSection->writeCache.pNext;
    for (; pNode != &pSection->writeCache; pNode = pNode->pNext) {
        CachedSetting * pSetting = LT_CONTAINER_OF(pNode, CachedSetting, node);
        switch (pSetting->type) {
        case kCacheDataType_Integer: {
            u8 nTmp[10];
            nTmp[0] = PackInteger(&nTmp[2], pSetting->nInteger);
            nTmp[1] = lt_strlen(pSetting->suffix);
            status = WriteToFlash(&writeState, nTmp, 2*sizeof(u8));
            if (status) return status;
            status = WriteToFlash(&writeState, (u8 *)pSetting->suffix, nTmp[1]);
            if (status) return status;
            status = WriteToFlash(&writeState, &nTmp[2], nTmp[0] & kFlashDataType_IntSizeMask);
            if (status) return status;
            break;
        }
        case kCacheDataType_String:
        case kCacheDataType_Binary: {
            u8 nTmp[2] = { pSetting->type, lt_strlen(pSetting->suffix) };
            status = WriteToFlash(&writeState, nTmp, sizeof(nTmp));
            if (status) return status;
            status = WriteToFlash(&writeState, (u8 *)pSetting->suffix, nTmp[1]);
            if (status) return status;
            u16 nSizeInBytes = pSetting->nSizeInBytes;
            if (pSetting->type == kCacheDataType_String) nSizeInBytes--;
            status = WriteToFlash(&writeState, (u8 *)&nSizeInBytes, sizeof(u16));
            if (status) return status;
            status = WriteToFlash(&writeState, pSetting->pValue, nSizeInBytes);
            if (status) return status;
            break;
        }
        default:
            break;
        }
    }
    /* Write end of list marker followed by CRC */
    u8 nTmp = kFlashDataType_EndOfList;
    status = WriteToFlash(&writeState, &nTmp, sizeof(nTmp));
    if (status) return status;
    u32 nTmpCrc = writeState.nCrc;
    status = WriteToFlash(&writeState, (u8 *)&nTmpCrc, sizeof(u32));
    if (status) return status;
    status = FinalWriteToFlash(&writeState);
    if (status) return status;
    /* Reset offsets and validate written data */
    nBytesToCheck = writeState.nWriteOffset - nBytesToCheck;
    readState.nReadOffset = pSection->nStartOffset + sizeof(SettingsMagic) + sizeof(u32);
    writeState.nWriteOffset = pSection->nStartOffset;
    if (pSection->state == kSectionState_PingActive) {
        readState.nReadOffset += nAreaSize;
        writeState.nWriteOffset += nAreaSize;
    }
    readState.nReadOffset = ALIGN32(readState.nReadOffset, pSection->nWriteQuantum - 1);
    /* We know overflow is not possible any more */
    writeState.nEndOffset = 0xffffffff;
    readState.nEndOffset  = 0xffffffff;
    u32 nCrc = 0;
    for (u32 nIdx = 0; nIdx < nBytesToCheck;) {
        u32 nBytesToRead = kCopyBufferSize;
        if (nBytesToCheck - nIdx < kCopyBufferSize) nBytesToRead = nBytesToCheck - nIdx;
        status = ReadFromFlash(&readState, (u8 *)pScratch, nBytesToRead);
        if (status) return status;
        s_pUtilityByteOps->Crc32(pScratch, nBytesToRead, &nCrc);
        nIdx += nBytesToRead;
    }
    if (nCrc != writeState.nCrc) return kOpStatus_VerifyError;
    /* Write and check magic */
    u32 nMagic = kSettingsMagic_ReadWrite;
    if (!s_iFlashUnit->WriteRawBytes(s_hFlashUnit, writeState.nWriteOffset, sizeof(nMagic), (u8 *)&nMagic))
        return kOpStatus_DeviceWriteError;
    if (!s_iFlashUnit->ReadRawBytes(s_hFlashUnit, writeState.nWriteOffset, sizeof(nMagic), (u8 *)pScratch))
        return kOpStatus_DeviceReadError;
    if (*((u32 *)pScratch) != nMagic) return kOpStatus_VerifyError;
    /* Toggle state */
    if (pSection->state == kSectionState_PingActive) pSection->state = kSectionState_PongActive;
    else pSection->state = kSectionState_PingActive;
    return kOpStatus_OK;
}

static void
DeleteWriteCache(SettingsSection * pSection, bool bJustRemoveDeletes) {
    LTList_Node * pNode = pSection->writeCache.pNext;
    for (LTList_Node * pSave; pNode != &pSection->writeCache; pNode = pSave) {
        pSave = pNode->pNext;
        CachedSetting * pSetting = LT_CONTAINER_OF(pNode, CachedSetting, node);
        if (!bJustRemoveDeletes || pSetting->type == kCacheDataType_Deleted ||
               pSetting->type == kCacheDataType_DeletedPrefix)
            UnlinkAndFreeCachedSetting(pNode);
    }
}

static bool
ProcessSection(SettingsSection * pSection, ProcessType ptype, char * pScratch, char * pName) {
    if (LTList_IsEmpty(&pSection->writeCache)) return true;
    if (!pScratch || !HAS_WRITEABLE_FLASH(pSection)) {
        return false;
    }
    OpStatus status = FlushSectionToFlash(pSection, pScratch);
    if (status) LTLOG_YELLOWALERT("proc.flush", "Could not flush %s, reason %lu", pName, LT_Pu32(status));
    if (status == kOpStatus_OK || ptype == kProcessType_Final) {
        DeleteWriteCache(pSection, false);
    }
    return (status == kOpStatus_OK);
}

static bool
FlushSettingsToFlash(ProcessType ptype) {
    LT_ASSERT(kMaxSuffixLen > kCopyBufferSize);
    /* Kill timer if set */
    s_iThread->KillTimer(s_hThread, &SettingsFlushProc, NULL);
    bool bSuccess = true;
    char * pScratch = (char *)lt_malloc(kMaxWriteQuantum + kMaxSuffixLen + 1);
    if (!pScratch) LTLOG_YELLOWALERT("flush.oom", "Could not allocate buffer");
    /* If above allocation fails, it is okay to continue (providing potential memory pressure relief) */
    s_mutex->API->Lock(s_mutex);
    if (!ProcessSection(&s_defaultSection, ptype, pScratch, "settings")) bSuccess = false;
    LTList_Node * pNode = s_defaultSection.node.pNext;
    for (; pNode != &s_defaultSection.node; pNode = pNode->pNext) {
        SettingsSection * pSection = LT_CONTAINER_OF(pNode, SettingsSection, node);
        if (!ProcessSection(pSection, ptype, pScratch, pSection->prefix)) bSuccess = false;
    }
    s_mutex->API->Unlock(s_mutex);
    if (pScratch) lt_free(pScratch);
    if (bSuccess) {
        LTLOG_DEBUG("flush.success", NULL);
    }
    return bSuccess;
}

/* Determine flash section state */
static SectionState
DetermineFlashSectionState(SettingsSection * pSection) {
    if (!s_iFlashUnit) return kSectionState_FatalError;
    SectionState state = kSectionState_Error;
    u32 nOffset = pSection->nStartOffset;
    s32 nPing[2], nPong[2];
    bool bReadSuccess = s_iFlashUnit->ReadRawBytes(s_hFlashUnit, nOffset, sizeof(nPing), (u8 *)nPing);
    if (bReadSuccess) {
        if (nPing[0] == -1 && nPing[1] == -1) state = kSectionState_AppearsBlank;
        nOffset += pSection->nSizeInSectors * s_iFlashUnit->GetBytesPerSector(s_hFlashUnit);
        bReadSuccess = s_iFlashUnit->ReadRawBytes(s_hFlashUnit, nOffset, sizeof(nPong), (u8 *)nPong);
        if (bReadSuccess) {
            if (state != kSectionState_AppearsBlank || nPong[0] != -1 || nPong[1] != -1) {
                /* Invalidate ping/pong if magic doesn't match */
                if (nPing[0] != kSettingsMagic_ReadWrite) nPing[1] = -1;
                if (nPong[0] != kSettingsMagic_ReadWrite) nPong[1] = -1;
                if (nPong[1] > nPing[1]) state = kSectionState_PongActive;
                else if (nPing[1] != -1 || nPong[1] != -1) state = kSectionState_PingActive;
                /* else state is Error */
            } /* else state is AppearsBlank */
        }
    }
    if (!bReadSuccess) {
        LTLOG_YELLOWALERT("det.state", "Read failure, reason %u", kOpStatus_DeviceReadError);
        state = kSectionState_FatalError;
    }
    return state;
}

/* Find existing section or allocate a new unconfigured settings section */
static SettingsSection *
FindOrAddSettingsSection(const char * pSectionPrefix, SectionType type, bool bAddIfNotExist) {
    u32 nPrefixLen = lt_strlen(pSectionPrefix);
    if (nPrefixLen > 64) return NULL;
    const char * pSuffix = pSectionPrefix;
    SettingsSection * pSection = FindSettingsSection(&pSuffix);
    if (!pSection) {
        if (!bAddIfNotExist) return NULL;
        pSection = (SettingsSection *)lt_malloc(sizeof(SettingsSection) + nPrefixLen + 1);
        if (!pSection) return NULL;
        lt_strncpyTerm(pSection->prefix, pSectionPrefix, nPrefixLen + 1);
        pSection->nPrefixLen = nPrefixLen;
        pSection->type = type;
        LTList_Init(&pSection->writeCache);
        LTList_AddTail(&s_defaultSection.node, &pSection->node);
    }
    return pSection;
}

/*_______________
 / ThreadProcs */

static void
SettingsFlushProc(void * pClientData) {
    FlushSettingsToFlash(pClientData ? kProcessType_Final : kProcessType_Normal);
}

static void
MyOnRebootNotifyEventProc(void * pClientData) {
    LT_UNUSED(pClientData);
    SettingsFlushProc((void *)1);
}

static bool
EnumeratePartitionsCallback(u32 nNumPartitions, const LTDeviceFlash_Partition * pPartition, void * pClientData) {
    LT_UNUSED(nNumPartitions);
    bool * pFoundSettings = (bool *)pClientData;
    if (lt_strcmp(pPartition->entry.type, "settings") == 0 || lt_strcmp(pPartition->entry.type, "assets") == 0) {
        LT_ASSERT(pPartition->nWriteQuantum <= kMaxWriteQuantum);
        SettingsSection * pSection = &s_defaultSection;
        if (lt_strcmp(pPartition->entry.name, "settings") == 0) {
            *pFoundSettings = true;
        } else {
            enum { kPrefixSize = 20 };
            char prefix[kPrefixSize] = "/";
            u32 nLen = lt_strncpyTerm(prefix + 1, pPartition->entry.name, kPrefixSize - 2);
            lt_strncpyTerm(prefix + nLen + 1, "/", kPrefixSize - nLen - 1);
            pSection = FindOrAddSettingsSection(prefix, kSectionType_ReadWrite, true);
            if (!pSection) {
                LTLOG_YELLOWALERT("init.section", "Can't add section %s", prefix);
                return true;
            }
        }
        pSection->nWriteQuantum = kMinWriteQuantum;
        if (pPartition->nWriteQuantum > kMinWriteQuantum)
            pSection->nWriteQuantum = pPartition->nWriteQuantum;
        pSection->nStartOffset = pPartition->entry.nByteOffset;
        /* NOTE: Size in sectors is for BOTH ping and pong area */
        pSection->nSizeInSectors = 0;
        if (pPartition->entry.type[0] == 'a') {
            /* Read asset sector size from ProductConfig */
            LTProductConfig * pProductConfig = lt_openlibrary(LTProductConfig);
            if (pProductConfig) {
                /* Read from LTSystemFS/assets/<partition>/NumDirectorySectors */
                u32 section = pProductConfig->GetLibraryConfigSection("LTSystemFS");
                LTString key = NULL;
                ltstring_format(&key, "assets/%s/NumDirectorySectors", pPartition->entry.name);
                pSection->nSizeInSectors = (u32)pProductConfig->ReadInteger(section, key) / 2;
                ltstring_destroy(key);
                if (pSection->nSizeInSectors == 0) {
                    LTLOG_YELLOWALERT("init.size", "Too small for section %s, using 2 sectors", pSection->prefix);
                    pSection->nSizeInSectors = 1;  /* 2 / 2 = 1 */
                }
                lt_closelibrary(pProductConfig);
            }
        } else {
            pSection->nSizeInSectors = pPartition->entry.nNumSectors / 2;
        }
        LT_ASSERT(pSection->nSizeInSectors > 0);
        pSection->state = DetermineFlashSectionState(pSection);
        if (pSection->state <= kSectionState_Error) {
            if (pSection == &s_defaultSection) {
                LTLOG_YELLOWALERT("init.state", "bad initial state: %lu", LT_Pu32(pSection->state));
            } else {
                LTLOG_YELLOWALERT("init.state.other", "%s: bad initial state: %lu", pSection->prefix, LT_Pu32(pSection->state));
            }
        }
    }
    return true;
}

static bool
ThreadInitProc(void) {
    /* Lock mutex to prevent LibInit() thread from running until this is finished */
    s_mutex->API->Lock(s_mutex);
    s_pDeviceFlash = (LTDeviceFlash *)LT_GetCore()->OpenLibrary("LTDeviceFlash");
    if (s_pDeviceFlash) {
        s_hFlashUnit = s_pDeviceFlash->CreateDeviceUnitHandle(0);
        if (s_hFlashUnit) s_iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashUnit);
    }
    if (s_iFlashUnit) {
        bool bFoundSettings = false;
        s_pDeviceFlash->EnumeratePartitions(s_hFlashUnit, EnumeratePartitionsCallback, &bFoundSettings);
        if (!bFoundSettings)
            LTLOG_YELLOWALERT("init.part", "Can't find settings partition");
    } else {
        LTLOG_YELLOWALERT("init.flash.du", "Can't access flash Device Unit");
    }
    s_pDeviceWatchdog = lt_openlibrary(LTDeviceWatchdog);
    if (s_pDeviceWatchdog) {
        s_pDeviceWatchdog->OnRebootNotify(&MyOnRebootNotifyEventProc, NULL, NULL);
        s_pDeviceWatchdog->WatchThread(LTTime_Seconds(kSettingsThreadFidelityInSeconds), true);
    }
    s_mutex->API->Unlock(s_mutex);
    s_iThread->SetPriority(s_iThread->GetCurrentThread(), kLTThread_PriorityLowest);
    return true;
}

static void
ThreadExitProc(void) {
    /* Ensure unwritten settings are flushed and memory is released */
    if (s_pDeviceWatchdog) {
        s_pDeviceWatchdog->NoRebootNotify(&MyOnRebootNotifyEventProc);
        lt_closelibrary(s_pDeviceWatchdog);
        s_pDeviceWatchdog = NULL;
    }
    SettingsFlushProc((void *)1);
    if (s_iFlashUnit) s_iFlashUnit->Destroy(s_hFlashUnit);
    s_iFlashUnit = NULL;
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pDeviceFlash);
    s_pDeviceFlash = NULL;
    LTLOG_VERBOSE("fini.thread", "thread exit");
}

/*_______________________
 / Library API Methods */

static bool
LTSystemSettings_GetIntegerValue(const char * pKey, s64 * pValueToSet) {
    return GetValue(pKey, kCacheDataType_Integer, (u8 *)pValueToSet, NULL);
}

static bool
LTSystemSettings_SetIntegerValue(const char * pKey, s64 nValue) {
    return SetValue(pKey, kCacheDataType_Integer, (u8 *)&nValue, sizeof(s64));
}

static bool
LTSystemSettings_GetStringValue(const char * pKey, LTString * ltstring) {
    return GetValue(pKey, kCacheDataType_String, (u8 *)ltstring, NULL);
}

static bool
LTSystemSettings_SetStringValue(const char * pKey, const char * pValue) {
    return SetValue(pKey, kCacheDataType_String, (u8 *)pValue, lt_strlen(pValue) + 1);
}

static bool
LTSystemSettings_GetBinaryValue(const char * pKey, u8 * pValueToSet, u32 * pSizeInBytes) {
    return GetValue(pKey, kCacheDataType_Binary, pValueToSet, pSizeInBytes);
}

static bool
LTSystemSettings_SetBinaryValue(const char * pKey, const u8 * pValue, u32 nSizeInBytes) {
    return SetValue(pKey, kCacheDataType_Binary, pValue, nSizeInBytes);
}

static bool
LTSystemSettings_DeleteSetting(const char * pKey) {
    return SetValue(pKey, kCacheDataType_Deleted, (u8 *)pKey, 0);
}

static bool
LTSystemSettings_DeleteSettingsWithPrefix(const char * pKeyPrefix) {
    return SetValue(pKeyPrefix, kCacheDataType_DeletedPrefix, (u8 *)pKeyPrefix, 0);
}

static bool
LTSystemSettings_EnumerateSettingsWithPrefix(const char * pKeyPrefix, LTSystemSettings_EnumProc * pCallback, void * pClientData) {
    const char * pSuffix = pKeyPrefix;
    s_mutex->API->Lock(s_mutex);
    SettingsSection * pSection = FindSettingsSection(&pSuffix);
    if (!pSection) {
        s_mutex->API->Unlock(s_mutex);
        return false;
    }
    char * pScratch = lt_malloc(kMaxSuffixLen + 1);
    if (!pScratch) {
        s_mutex->API->Unlock(s_mutex);
        return false;
    }
    /* Save end of write cache node _before_ running callbacks */
    LTList_Node * pNode = pSection->writeCache.pPrev;
    OpStatus status = kOpStatus_OK;
    u32 nSuffixLen = lt_strlen(pSuffix);
    if (HAS_READABLE_FLASH(pSection)) {
        /* Find matching entries in flash that aren't in write cache */
        ReadState readState = {
            .pSection    = pSection,
            .nReadOffset = pSection->nStartOffset
        };
        u32 nAreaSize = pSection->nSizeInSectors * s_iFlashUnit->GetBytesPerSector(s_hFlashUnit);
        if (pSection->state == kSectionState_PongActive) readState.nReadOffset += nAreaSize;
        readState.nEndOffset = readState.nReadOffset + nAreaSize - 1;
        readState.nReadOffset += sizeof(SettingsMagic) + sizeof(u32);
        readState.nReadOffset = ALIGN32(readState.nReadOffset, pSection->nWriteQuantum - 1);
        lt_strncpyTerm(pScratch, pSuffix, nSuffixLen + 1);
        while (1) {
            status = MatchNextSettingInFlash(&readState, pSuffix, true);
            if (status == kOpStatus_OK_NoMatch) break;
            else if (status) goto BAIL;
            /* Read remainder of name and look for match in write cache */
            status = ReadFromFlash(&readState, (u8 *)pScratch + nSuffixLen,
                                      readState.hdr.nNameLength - nSuffixLen);
            if (status) goto BAIL;
            pScratch[readState.hdr.nNameLength] = '\0';
            LTList_Node * pNode = pSection->writeCache.pPrev;
            SettingsMatch match;
            do {
                match = MatchNextCachedSetting(pSection, &pNode, pScratch);
                if (match == kSettingsMatch_EndOfList) {
                   /* No match in cache -> call callback */
                   u32 nSizeInBytes = pSection->nPrefixLen + readState.hdr.nNameLength + 1;
                   char * pKeyCopy = (char *)lt_malloc(nSizeInBytes);
                   if (!pKeyCopy) goto BAIL;
                   lt_strncpyTerm(pKeyCopy, pSection->prefix, nSizeInBytes);
                   lt_strncpyTerm(pKeyCopy + pSection->nPrefixLen, pScratch,
                                     nSizeInBytes - pSection->nPrefixLen);
                   CacheDataType type = kCacheDataType_Integer;
                   if (readState.hdr.type == kFlashDataType_String) type = kCacheDataType_String;
                   else if (readState.hdr.type == kFlashDataType_Binary) type = kCacheDataType_Binary;
                   bool bContinue = (*pCallback)(pKeyCopy, pKeyCopy + pSection->nPrefixLen + nSuffixLen, type, pClientData);
                   lt_free(pKeyCopy);
                   if (!bContinue) goto BAIL;
                }
                pNode = pNode->pPrev;
            } while (match == kSettingsMatch_Prefix);
            if (readState.hdr.type <= kFlashDataType_IntTypeMax) {
                readState.nReadOffset += (readState.hdr.type & kFlashDataType_IntSizeMask);
            } else {
                u16 nSizeInBytes;
                status = ReadFromFlash(&readState, (u8 *)&nSizeInBytes, sizeof(nSizeInBytes));
                if (status) goto BAIL;
                readState.nReadOffset += nSizeInBytes;
            }
        }
    }
    /* Look for matching entries from write cache */
    SettingsMatch match;
    while (1) {
        match = MatchNextCachedSetting(pSection, &pNode, pSuffix);
        if (match == kSettingsMatch_EndOfList) break;
        if (match == kSettingsMatch_Full || match == kSettingsMatch_Prefix) {
            CachedSetting * pSetting = LT_CONTAINER_OF(pNode, CachedSetting, node);
            if (pSetting->type == kCacheDataType_Integer ||
                   pSetting->type == kCacheDataType_String ||
                   pSetting->type == kCacheDataType_Binary) {
                u32 nSizeInBytes = pSection->nPrefixLen + lt_strlen(pSetting->suffix) + 1;
                char * pKeyCopy = (char *)lt_malloc(nSizeInBytes);
                if (!pKeyCopy) goto BAIL;
                lt_strncpyTerm(pKeyCopy, pSection->prefix, nSizeInBytes);
                lt_strncpyTerm(pKeyCopy + pSection->nPrefixLen, pSetting->suffix,
                                  nSizeInBytes - pSection->nPrefixLen);
                bool bContinue = (*pCallback)(pKeyCopy, pKeyCopy + pSection->nPrefixLen + nSuffixLen, pSetting->type, pClientData);
                lt_free(pKeyCopy);
                if (!bContinue) goto BAIL;
            }
        }
        pNode = pNode->pPrev;
    }
    s_mutex->API->Unlock(s_mutex);
    lt_free(pScratch);
    return true;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    lt_free(pScratch);
    if (status > kOpStatus_OK_NoMatch) LTLOG_YELLOWALERT("enum", "Read failure, reason %u", status);
    return false;
}

static bool
LTSystemSettings_EraseSection(const char * pSectionPrefix) {
    if (!pSectionPrefix) return false;
    const char * pSuffix = pSectionPrefix;
    bool bSuccess = false;
    s_mutex->API->Lock(s_mutex);
    SettingsSection * pSection = FindSettingsSection(&pSuffix);
    if (!pSection) goto BAIL;
    /* Delete everything from write cache */
    DeleteWriteCache(pSection, false);
    bSuccess = true;
    /* Erase Section */
    u32 nSizeInSectors = pSection->nSizeInSectors;
    nSizeInSectors <<= 1;
    u32 nStartSector = s_iFlashUnit->ByteOffsetToSectorNumber(s_hFlashUnit, pSection->nStartOffset);
    if (!s_iFlashUnit->EraseSectors(s_hFlashUnit, nStartSector, nSizeInSectors)) {
        bSuccess = false;
        pSection->state = kSectionState_FatalError;
        LTLOG_YELLOWALERT("erase.fail", "Erase failed");
        goto BAIL;
    }
    /* Re-determine section state */
    pSection->state = DetermineFlashSectionState(pSection);
    if (pSection->state != kSectionState_AppearsBlank) {
        bSuccess = false;
        LTLOG_YELLOWALERT("erase.fail.bl", "Erase failed, state: %lu", LT_Pu32(pSection->state));
    }
BAIL:
    s_mutex->API->Unlock(s_mutex);
    return bSuccess;
}

static bool
LTSystemSettings_Flush(const char * pSectionPrefix) {
    bool bSuccess = false;
    if (pSectionPrefix == NULL) {
        bSuccess = FlushSettingsToFlash(kProcessType_Normal);
    } else {
        s_mutex->API->Lock(s_mutex);
        const char * pSuffix = pSectionPrefix;
        SettingsSection * pSection = FindSettingsSection(&pSuffix);
        if (pSection) {
            /* Scratch is used for write buffer and name storage */
            char * pScratch = (char *)lt_malloc(kMaxWriteQuantum + kMaxSuffixLen + 1);
            bSuccess = ProcessSection(pSection, kProcessType_ExplicitWrite, pScratch, pSection->prefix);
            if (pScratch) lt_free(pScratch);
        }
        s_mutex->API->Unlock(s_mutex);
    }
    return bSuccess;
}

/*___________________________________________
 / Library initialization and finalization */
static bool
LTSystemSettingsImpl_LibInit(void) {
    LTCore * pCore = LT_GetCore();
    s_pUtilityByteOps = lt_openlibrary(LTUtilityByteOps);
    if (!s_pUtilityByteOps || !s_pUtilityByteOps) goto BAIL;
    s_mutex = lt_createobject(LTMutex);
    if (!s_mutex) goto BAIL;
    s_hThread = pCore->CreateThread("Settings");
    if (!s_hThread) goto BAIL;
    s_iThread = lt_gethandleinterface(ILTThread, s_hThread);
    if (!s_iThread) goto BAIL;

    /* Initialize default section */
    s_defaultSection.type = kSectionType_ReadWrite;
    s_defaultSection.nPrefixLen = 0;
    LTList_Init(&s_defaultSection.node);
    LTList_Init(&s_defaultSection.writeCache);

    /* start thread; max priority allows thread InitProc to run before LibInit finishes */
    s_iThread->SetStackSize(s_hThread, LTSYSTEMSETTINGS_THREAD_STACKSIZE);
    s_iThread->SetPriority(s_hThread, kLTThread_PriorityHighest);
    s_iThread->Start(s_hThread, &ThreadInitProc, &ThreadExitProc);

    /* Lock and Unlock mutex in case thread InitProc blocks */
    s_mutex->API->Lock(s_mutex);
    s_mutex->API->Unlock(s_mutex);
    return true;

BAIL:
    lt_closelibrary(s_pUtilityByteOps);
    lt_destroyobject(s_mutex);
    if (s_hThread) {
        pCore->DestroyHandle(s_hThread);
    }
    s_pUtilityByteOps = NULL;
    s_mutex = NULL;
    s_hThread = 0;
    s_iThread = NULL;
    return false;
}

static void
LTSystemSettingsImpl_LibFini(void) {
    /* Destroy thread (wait for termination before continuing) */
    s_iThread->Destroy(s_hThread);
    s_iThread = NULL;
    /* Destroy section list */
    LTList_Node * pNode = s_defaultSection.node.pNext;
    for (LTList_Node * pNext; pNode != &s_defaultSection.node; pNode = pNext) {
        pNext = pNode->pNext;
        SettingsSection *section = LT_CONTAINER_OF(pNode, SettingsSection, node);
        DeleteWriteCache(section, false);
        lt_free(section);
    }
    lt_closelibrary(s_pUtilityByteOps);
    lt_destroyobject(s_mutex);
    s_pUtilityByteOps = NULL;
    s_mutex = NULL;
}

/*_______________________________________________________
 / LTSystemSettingsImpl library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemSettings)
    .GetIntegerValue             = &LTSystemSettings_GetIntegerValue,
    .SetIntegerValue             = &LTSystemSettings_SetIntegerValue,
    .GetStringValue              = &LTSystemSettings_GetStringValue,
    .SetStringValue              = &LTSystemSettings_SetStringValue,
    .GetBinaryValue              = &LTSystemSettings_GetBinaryValue,
    .SetBinaryValue              = &LTSystemSettings_SetBinaryValue,
    .DeleteSetting               = &LTSystemSettings_DeleteSetting,
    .DeleteSettingsWithPrefix    = &LTSystemSettings_DeleteSettingsWithPrefix,
    .EnumerateSettingsWithPrefix = &LTSystemSettings_EnumerateSettingsWithPrefix,
    .EraseSection                = &LTSystemSettings_EraseSection,
    .Flush                       = &LTSystemSettings_Flush,
LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Mar-23   augustus    use the watchdog's notify reboot event to flush
 */
