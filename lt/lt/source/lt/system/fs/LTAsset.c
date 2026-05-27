/*******************************************************************************
 * lt/system/fs/LTAsset.c                                       LTAsset PseudoFS
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTList.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/system/settings/LTSystemSettings.h>

#include <lt/system/fs/LTFile.h>

/** @file LTAsset.c
 *  @brief LTAsset: A Pseudofilesystem for Asset Management.
 *
 *  LTAsset is a pseudofilesystem designed for reading and writing assets. It supports
 *    two distinct mechanisms to accommodate assets of varying sizes:
 *  - Small-storage: For assets smaller than the configurable small-storage limit.
 *  - Bulk-storage: For larger assets stored in the bulk section of the asset partition.
 *
 *  Limitations
 *  - No wear-leveling support: The pseudofilesystem does not include wear-leveling, meaning
 *     write cycles are limited to the number of erase cycles given in the flash datasheet.
 *  - Limited API functionality: Only basic operations such as read, write, delete and
 *     seek are supported (seek is limited to read operations only). The full LTFile API is
 *     not implemented.
 *  - No subdirectories: Subdirectories are not supported internally. However, subdirectory
 *     naming conventions ARE allowed, for example, "/assets/dir1/dir2/myasset" is a valid
 *     asset name.
 *
 *  Asset Partitions and Naming Requirements
 *    Asset names must adhere to the following format: "/<partition>/<asset_name>". Here
 *    <partition> represents the name of the asset partition. Typically, a product will have
 *    a single LT partition named "assets" of type "assets". Example of valid asset names
 *    include:
 *  - "/assets/llm"
 *  - "/assets/llm/file.1" (if a directory-like organization is desired)
 *    NOTE: The root name "/<partition>/" is reserved for internal use and cannot be used
 *    as an asset name.
 *
 *  Product Configuration:
 *   Asset partitions are configured in the Product Config using the following keys:
 *  - /config/lib/LTSystemFS/assets/<partition>/NumDirectorySectors: Defines the number of
 *    sectors allocated for asset partition metadata. Both asset metadata and small-storage
 *    data are stored at the beginning of the asset partition in LTSystemSettings format. This
 *    size must be a multiple of two, as it includes both ping and pong areas.
 *  - /config/lib/LTSystemFS/assets/<partition>/SmallStorageLimit: Specifies the maximum size
 *    (in bytes) for small-storage assets within the asset partition. Small assets are stored
 *    directly in the directory sectors and don't use the bulk area. The maximum allowed small
 *    storage limit is 1024 bytes.
 *
 *  Asset Partition Optimization Notes:
 *  - Small Assets: If many small assets are expected, increase the number of directory
 *      sectors accordingly.
 *  - Small Storage Limit: Adjust the small-storage limit to a value that improves storage
 *      efficiency.
 *  - Overhead of bulk assets: Bulk assets use a multiple of sectors no matter what their size
 *      is. Therefore the maximum overhead of all bulk assets is the sector size (minus one)
 *      multiplied by the number of assets in the partition.
 *  - Overhead of small-storage: Small-storage assets are stored in ping and pong, therefore
 *      their storage requirement is effectively double their size.
 */

/*
 *  Possible future improvements:
 *   1. Instead of reallocating all blocks for an asset first reuse its existing
 *        blocks and add blocks when needed.
 */

enum {
    kMaxSmallStorageLimit = 1024,       /* Maximum allowed small-storage limit in bytes */
    kUsingSmallStorage    = 0xffffffff, /* Value indicates when small-storage is used */
    kIncrementalBlocks    = 16,         /* Incremental allocation block count, power of 2 */
    kWriteBufSize         = 64,         /* Write buffer size for bulk assets, power of 2 */
};

typedef_LTENUM_SIZED(OpStatus, u8) {
    kOpStatus_NotOpen,
    kOpStatus_Open,
    kOpStatus_OpenRead,
    kOpStatus_OpenWrite,
    kOpStatus_Error
};

typedef struct {
    u32           sizeInBytes;       /* Asset size (or kUsingSmallStorage), temp for CRC calc */
    union {
        u8        data[0];           /* small-storage data */
        struct {
            u32   crc;               /* CRC of bulk-storage data */
            u16   blocks[0];         /* ordered block list for asset partition data */
        };
    };
} AssetEntry;

LT_STATIC_ASSERT_SIZE_32_64(AssetEntry, 8, 8);

typedef struct {
    u16           firstBulkSector;   /* First bulk sector in the asset partition */
    u16           numBulkSectors;    /* Number of bulk sectors in the asset partition */
    u8            blockMask[0];      /* bit-vector of allocated blocks */
} AllocBlocks;

LT_STATIC_ASSERT_SIZE_32_64(AllocBlocks, 4, 4);

typedef struct {
    AllocBlocks  *allocBlocks;       /* allocBlocks structure for the asset partition */
    u32           refCount;          /* Reference count for partition info data */
    u16           firstBulkSector;   /* First bulk sector in the asset partition */
    u16           numBulkSectors;    /* Number of bulk sectors in the asset partition */
    u16           smallStorageLimit; /* Upper limit for small-storage in the asset partition */
} PartitionInfo;

DEFINE_LTLOG_SECTION("asset");

static LTMutex            *s_mutex;
static LTUtilityByteOps   *s_byteOps;
static LTSystemSettings   *s_settings;

static LTDeviceFlash      *s_deviceFlash;
static LTDeviceUnit        s_hFlashUnit;
static ILTFlashDeviceUnit *s_iFlashUnit;

static LTList              s_openAssets;
static u16                 s_sectorSize;

/*_______________________
  LTAsset private data */
typedef_LTObjectImpl(LTFile, Asset) {
    LTList_Node       node;           /* Linked list node for open assets */
    LTString          name;           /* Asset name (e.g.: "/assets/myasset") */
    PartitionInfo    *info;           /* Info for the partition where the asset lives */
    AssetEntry       *entry;          /* Current asset entry */
    u8               *writeBuf;       /* Write buffer for the asset (if needed) */
    u32               sizeInBytes;    /* Size of the asset in bytes (0 if not set) */
    u32               position;       /* Current position in the asset */
    u16               writeBufOffset; /* Write buffer offset */
    OpStatus          status;         /* Current operation status */
} LTOBJECT_API;

/*__________________________________
  LTAsset private functions */

static Asset *FindOpenAssetByName(const char *name) {
    for (LTList_Node *node = s_openAssets.pNext; node != &s_openAssets; node = node->pNext) {
        Asset *asset = LT_CONTAINER_OF(node, Asset, node);
        if (lt_strcmp(asset->name, name) == 0) return asset;
    }
    return NULL;
}

static bool SetPartitionInfoOnAsset(Asset *asset) {
    if (asset->info) return true;
    if (asset->name[0] != '/') return false;
    char *ch = lt_strchr(asset->name + 1, '/');
    if (ch == NULL) return false;
    char chSave = ch[1];
    ch[1] = '\0';
    /* Reference info if it exists on another asset */
    PartitionInfo *info = NULL;
    for (LTList_Node *node = s_openAssets.pNext; node != &s_openAssets; node = node->pNext) {
        Asset *otherAsset = LT_CONTAINER_OF(node, Asset, node);
        if (otherAsset->info && (lt_strncmp(asset->name, otherAsset->name, lt_strlen(asset->name)) == 0)) {
            info = otherAsset->info;
            info->refCount++;
            break;
        }
    }
    ch[1] = chSave;
    if (!info) {
        /* If it doesn't exist then determine partition info */
        LTDeviceFlash_Partition partition;
        ch[0] = '\0';
        if (s_deviceFlash->GetPartition(s_hFlashUnit, asset->name + 1, &partition)) {
            /* Check write quantum against write buffer */
            LT_ASSERT(partition.nWriteQuantum <= kWriteBufSize);
            /* Read asset sector size from ProductConfig */
            LTProductConfig *productConfig = lt_openlibrary(LTProductConfig);
            if (productConfig) {
                info = lt_malloc(sizeof(PartitionInfo));
                if (info) {
                    /* Read from config/assets/<partition>/NumDirectorySectors -and- SmallStorageLimit */
                    u32 section = productConfig->GetLibraryConfigSection("LTSystemFS");
                    LTString key = NULL;
                    ltstring_format(&key, "assets/%s/NumDirectorySectors", asset->name + 1);
                    info->firstBulkSector = (u16)productConfig->ReadInteger(section, key);
                    ltstring_format(&key, "assets/%s/SmallStorageLimit", asset->name + 1);
                    info->smallStorageLimit = (u16)productConfig->ReadInteger(section, key);
                    if (info->smallStorageLimit > kMaxSmallStorageLimit) {
                        info->smallStorageLimit = kMaxSmallStorageLimit;
                    }
                    ltstring_destroy(key);
                    if (info->firstBulkSector) {
                        info->allocBlocks     = NULL;
                        info->refCount        = 1;
                        info->numBulkSectors  = partition.entry.nNumSectors - info->firstBulkSector;
                        info->firstBulkSector = (partition.entry.nByteOffset / s_sectorSize) + info->firstBulkSector;
                    } else {
                        LTLOG_YELLOWALERT("spi", "Missing configs for asset partition %s\n", asset->name);
                        lt_free(info);
                        info = NULL;
                    }
                }
                lt_closelibrary(productConfig);
            }
        }
        ch[0] = '/';
    }
    asset->info = info;
    return asset->info != NULL;
}

static void RemovePartitionInfoFromAsset(Asset *asset) {
    PartitionInfo *info = asset->info;
    if (info == NULL) return;
    if (--info->refCount == 0) {
        if (info->allocBlocks) {
            lt_free(info->allocBlocks);
        }
        lt_free(info);
    }
    /* Important to set this to NULL in ALL cases */
    asset->info = NULL;
}

/* Read AssetEntry structure from settings (NB: returns allocated data). */
static AssetEntry *ReadAssetEntry(const char *name, u32 *entrySize) {
    *entrySize = LT_U32_MAX;
    AssetEntry *entry = NULL;
    if (s_settings->GetBinaryValue(name, NULL, entrySize)) {
        entry = lt_malloc(*entrySize);
        if (entry && !s_settings->GetBinaryValue(name, (u8 *)entry, entrySize)) {
            lt_free(entry);
            return NULL;
        }
    }
    return entry;
}

/* Read AllocBlocks structure or create one if it doesn't exist (returns allocated data). */
static AllocBlocks *SetAllocBlocksOnAsset(Asset *asset) {
    if (asset->info->allocBlocks) return asset->info->allocBlocks;
    if (asset->name[0] != '/') return NULL;
    char *ch = lt_strchr(asset->name + 1, '/');
    if (ch == NULL) return NULL;
    char chSave = *(++ch);
    *ch = '\0';
    AllocBlocks *allocBlocks = NULL;
    u32 allocBlocksSize = LT_U32_MAX;
    if (s_settings->GetBinaryValue(asset->name, NULL, &allocBlocksSize)) {
        allocBlocks = lt_malloc(allocBlocksSize);
        if (allocBlocks) {
            if (!s_settings->GetBinaryValue(asset->name, (u8 *)allocBlocks, &allocBlocksSize)) {
                lt_free(allocBlocks);
                allocBlocks = NULL;
            }
            /* TODO: One could erase the whole settings partition here to recover and
                     then create an empty AllocBlocks structure (instead of assert). */
            LT_ASSERT(allocBlocks->firstBulkSector == asset->info->firstBulkSector);
            LT_ASSERT(allocBlocks->numBulkSectors  == asset->info->numBulkSectors);
        }
    } else {
        /* Create an empty AllocBlocks */
        u16 maskSize = (asset->info->numBulkSectors + 7) >> 3;
        allocBlocks = lt_malloc(sizeof(AllocBlocks) + maskSize);
        if (allocBlocks) {
            allocBlocks->firstBulkSector = asset->info->firstBulkSector;
            allocBlocks->numBulkSectors  = asset->info->numBulkSectors;
            lt_memset(allocBlocks->blockMask, 0, maskSize);
        }
    }
    *ch = chSave;
    asset->info->allocBlocks = allocBlocks;
    return allocBlocks;
}

static void WriteAllocBlocks(Asset *asset) {
    char *ch = lt_strchr(asset->name + 1, '/');
    char chSave = ch[1];
    ch[1] = '\0';
    u32 allocBlocksSize = sizeof(AllocBlocks) + ((asset->info->numBulkSectors + 7) >> 3);
    s_settings->SetBinaryValue(asset->name, (u8 *)asset->info->allocBlocks, allocBlocksSize);
    ch[1] = chSave;
}

static bool FlushSettings(char *name) {
    if (name[0] != '/') return false;
    char *ch = lt_strchr(name + 1, '/');
    if (ch == NULL) return false;
    char chSave = *(++ch);
    *ch = '\0';
    s_settings->Flush(name);
    *ch = chSave;
    return true;
}

static void DeleteAsset(Asset *asset, u32 entrySize) {
    AssetEntry *entry = asset->entry;
    if (entry->sizeInBytes != kUsingSmallStorage) {
        AllocBlocks *allocBlocks = asset->info->allocBlocks;
        /* Update the free block mask with freed blocks */
        for (u32 i = 0; i < (entrySize - sizeof(AssetEntry)) >> 1; i++) {
            u16 block = entry->blocks[i];
            u8 maskByte = allocBlocks->blockMask[block >> 3];
            maskByte &= ~(1 << (block & 0x7));
            allocBlocks->blockMask[block >> 3] = maskByte;
        }
        /* Save the updated free block mask */
        WriteAllocBlocks(asset);
    }
    s_settings->DeleteSetting(asset->name);
    if (entry->sizeInBytes != kUsingSmallStorage) {
        /* Flush after every delete of bulk assets to preserve consistency */
        FlushSettings(asset->name);
    }
}

/* See if any blocks in the mask byte are free. */
static s32 FindFreeBlockInMaskByte(Asset *asset, u16 maskIdx, u8 maskByte) {
    s32 block = -1;
    /* Check each bit in the byte */
    for (u8 b = 0; b < 8; b++) {
        if ((maskByte & (1 << b)) == 0) {
            /* Found a potentially free block */
            block = (maskIdx << 3) + b;
            if (block >= (s32)asset->info->numBulkSectors) return -1;
            /* Check if this block is allocated to this asset */
            LTList_Node *node = s_openAssets.pNext;
            for (; node != &s_openAssets; node = node->pNext) {
                Asset *otherAsset = LT_CONTAINER_OF(node, Asset, node);
                if (otherAsset->status == kOpStatus_OpenWrite && otherAsset->entry &&
                                                       otherAsset->entry->sizeInBytes != kUsingSmallStorage) {
                    /* Skip if this other asset is in a different asset partition */
                    if (lt_strncmp(asset->name, otherAsset->name, lt_strlen(asset->name)) != 0) {
                        continue;
                    }
                    for (u32 i = 0; i < (otherAsset->position + s_sectorSize - 1) / s_sectorSize; i++) {
                        if (otherAsset->entry->blocks[i] == block) {
                            block = -1;
                            break;
                        }
                    }
                }
            }
            if (block >= 0) break;
        }
    }
    return block;
}

static s32 FindFreeBlockAndErase(Asset *asset, u32 lastBlock) {
    char *ch = lt_strchr(asset->name + 1, '/');
    char chSave = *(++ch);
    *ch = '\0';
    AllocBlocks *allocBlocks = asset->info->allocBlocks;
    s32 block = -1;
    /* Search beyond existing block first */
    for (u16 maskIdx = lastBlock >> 3; maskIdx < (asset->info->numBulkSectors >> 3); maskIdx++) {
        u8 maskByte = allocBlocks->blockMask[maskIdx];
        if (maskByte == 0xff) continue;
        block = FindFreeBlockInMaskByte(asset, maskIdx, maskByte);
        if (block >= 0) break;
    }
    if (block < 0) {
        /* Continue search from 0 */
        for (u16 maskIdx = 0; maskIdx < lastBlock >> 3; maskIdx++) {
            u8 maskByte = allocBlocks->blockMask[maskIdx];
            if (maskByte == 0xff) continue;
            block = FindFreeBlockInMaskByte(asset, maskIdx, maskByte);
            if (block >= 0) break;
        }
    }
    *ch = chSave;
    if (block >= 0) {
        /* Found a free block, erase it */
        s_iFlashUnit->EraseSectors(s_hFlashUnit, asset->info->firstBulkSector + block, 1);
    } else {
        LTLOG_YELLOWALERT("ffb", "No free block found for %s\n", asset->name);
    }
    return block;
}

static bool WriteFlash(Asset *asset, u32 offset, u32 numBytesLeft, const u8 *data) {
    s_byteOps->Crc32(data, numBytesLeft, &asset->entry->sizeInBytes);
    if (asset->writeBufOffset > 0 || numBytesLeft < kWriteBufSize) {
        if (!asset->writeBuf) {
            asset->writeBuf = lt_malloc(kWriteBufSize);
            if (!asset->writeBuf) return false;
        }
        u32 numBytesToCopy = kWriteBufSize - asset->writeBufOffset;
        if (numBytesLeft < numBytesToCopy) {
            /* Transfer to incomplete write buffer and return */
            lt_memcpy(asset->writeBuf + asset->writeBufOffset, data, numBytesLeft);
            asset->writeBufOffset += numBytesLeft;
            return true;
        }
        /* Flash the complete write buffer and account for it */
        lt_memcpy(asset->writeBuf + asset->writeBufOffset, data, numBytesToCopy);
        if (!s_iFlashUnit->WriteBytes(s_hFlashUnit, offset - asset->writeBufOffset,
                                                                kWriteBufSize, asset->writeBuf)) return false;
        asset->writeBufOffset = 0;
        offset       += numBytesToCopy;
        data         += numBytesToCopy;
        numBytesLeft -= numBytesToCopy;
        /* Data and offset should now be aligned to kWriteBufSize */
    }
    if (numBytesLeft) {
        /* Write as much aligned data to flash as possible */
        u32 numBytesToWrite = numBytesLeft & ~(kWriteBufSize - 1);
        if (numBytesToWrite > 0) {
            if (!s_iFlashUnit->WriteBytes(s_hFlashUnit, offset, numBytesToWrite, data)) return false;
            data         += numBytesToWrite;
            numBytesLeft -= numBytesToWrite;
        }
    }
    if (numBytesLeft) {
        if (!asset->writeBuf) {
            asset->writeBuf = lt_malloc(kWriteBufSize);
            if (!asset->writeBuf) return false;
        }
        /* Copy remaining data to write buffer (should be less than kWriteBufSize bytes left). */
        lt_memcpy(asset->writeBuf, data, numBytesLeft);
        asset->writeBufOffset = numBytesLeft;
    }
    return true;
}

/*__________________________
  LTAsset public API */

static void Asset_SetName(Asset *asset, const char *name) {
    s_mutex->API->Lock(s_mutex);
    if (asset->status != kOpStatus_NotOpen) {
        s_mutex->API->Unlock(s_mutex);
        return;
    }
    /* Name cannot have an empty leaf name (e.g.: "/assets/") which is reserved for the free list. */
    if (name == NULL || name[lt_strlen(name) - 1] == '/') {
        ltstring_destroy(asset->name);
        asset->name = NULL;
    } else {
        ltstring_set(&asset->name, name);
    }
    s_mutex->API->Unlock(s_mutex);
}

static bool Asset_Exists(Asset *asset) {
    bool exists = false;
    s_mutex->API->Lock(s_mutex);
    Asset *openAsset = FindOpenAssetByName(asset->name);
    if (openAsset) {
        exists = true;
    } else {
        u32 entrySize = LT_U32_MAX;
        exists = s_settings->GetBinaryValue(asset->name, NULL, &entrySize);
    }
    s_mutex->API->Unlock(s_mutex);
    return exists;
}

static bool Asset_Delete(Asset *asset, bool recursive) {
    LT_UNUSED(recursive);
    bool success = false;
    s_mutex->API->Lock(s_mutex);
    Asset *openAsset = FindOpenAssetByName(asset->name);
    if (openAsset) goto BAIL;
    u32 entrySize;
    asset->entry = ReadAssetEntry(asset->name, &entrySize);
    if (asset->entry == NULL) goto BAIL;
    if (asset->entry->sizeInBytes != kUsingSmallStorage) {
        if (!SetPartitionInfoOnAsset(asset)) goto BAIL;
        AllocBlocks *allocBlocks = SetAllocBlocksOnAsset(asset);
        if (allocBlocks == NULL) goto BAIL;
    }
    DeleteAsset(asset, entrySize);
    if (asset->entry->sizeInBytes != kUsingSmallStorage) {
        RemovePartitionInfoFromAsset(asset);
    }
    success = true;
BAIL:
    lt_free(asset->entry);
    asset->entry = NULL;
    s_mutex->API->Unlock(s_mutex);
    return success;
}

static u64 Asset_GetSize(Asset *asset) {
    u32 sizeInBytes = 0;
    s_mutex->API->Lock(s_mutex);
    Asset *openAsset = FindOpenAssetByName(asset->name);
    if (openAsset) {
        if (openAsset->status == kOpStatus_Open || openAsset->status == kOpStatus_OpenRead) {
            sizeInBytes = openAsset->sizeInBytes;
        }
    } else {
        u32 entrySize;
        AssetEntry *entry = ReadAssetEntry(asset->name, &entrySize);
        if (entry) {
            /* Determine size of asset */
            if (entry->sizeInBytes == kUsingSmallStorage) {
                sizeInBytes = entrySize - sizeof(u32);
            } else {
                sizeInBytes = entry->sizeInBytes;
            }
            lt_free(entry);
        }
    }
    s_mutex->API->Unlock(s_mutex);
    return (u64)sizeInBytes;
}

static void Asset_SeekToPosition(Asset *asset, u64 position) {
    s_mutex->API->Lock(s_mutex);
    if (asset->status == kOpStatus_NotOpen || asset->status == kOpStatus_OpenWrite) {
        s_mutex->API->Unlock(s_mutex);
        return;
    }
    asset->position = (u32)position;
    s_mutex->API->Unlock(s_mutex);
    return;
}

static bool Asset_Open(Asset *asset, bool create) {
    LT_UNUSED(create);
    s_mutex->API->Lock(s_mutex);
    /* Better not be open and better have a flash partition */
    if (asset->status != kOpStatus_NotOpen || ltstring_isempty(asset->name)) {
        goto BAIL;
    }
    Asset *openAsset = FindOpenAssetByName(asset->name);
    if (openAsset || !SetPartitionInfoOnAsset(asset)) {
        goto BAIL;
    }
    /* Find existing asset entry (if it exists) */
    u32 entrySize;
    asset->entry = ReadAssetEntry(asset->name, &entrySize);
    if (asset->entry) {
        /* Determine size of asset */
        if (asset->entry->sizeInBytes == kUsingSmallStorage) {
            asset->sizeInBytes = entrySize - sizeof(u32);
        } else {
            asset->sizeInBytes = asset->entry->sizeInBytes;
        }
    }
    asset->writeBufOffset = 0;
    asset->position       = 0;
    asset->status         = kOpStatus_Open;
    LTList_AddTail(&s_openAssets, &asset->node);
    s_mutex->API->Unlock(s_mutex);
    return true;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    return false;
}

static u32 Asset_Read(Asset *asset, u32 numBytes, u8 *buffer) {
    s_mutex->API->Lock(s_mutex);
    if (asset->status == kOpStatus_Open) {
        asset->status = kOpStatus_OpenRead;
    } else if (asset->status != kOpStatus_OpenRead) {
        asset->status = kOpStatus_Error;
        goto BAIL;
    }
    /* Does file exist and is it big enough? */
    u32 origPosition = asset->position;
    if (asset->entry == NULL || origPosition + numBytes > asset->sizeInBytes) {
        asset->status = kOpStatus_Error;
        goto BAIL;
    }
    if (asset->entry->sizeInBytes == kUsingSmallStorage) {
        /* Read from small-storage */
        u32 bytesToRead = asset->sizeInBytes - origPosition;
        if (numBytes < bytesToRead) bytesToRead = numBytes;
        lt_memcpy(buffer, asset->entry->data + origPosition, bytesToRead);
        asset->position += bytesToRead;
    } else {
        /* Read from bulk-storage */
        u32 blockNum    = origPosition / s_sectorSize;
        u32 bulkStart   = asset->info->firstBulkSector * s_sectorSize;
        u32 offset      = bulkStart + (origPosition % s_sectorSize);
        u32 bytesToRead = s_sectorSize - (origPosition % s_sectorSize);
        while (numBytes > 0) {
            offset += asset->entry->blocks[blockNum] * s_sectorSize;
            /* Read until end of sector or numBytes whichever is smaller */
            if (numBytes < bytesToRead) bytesToRead = numBytes;
            if (!s_iFlashUnit->ReadBytes(s_hFlashUnit, offset, bytesToRead, buffer)) {
                asset->position = origPosition;
                asset->status   = kOpStatus_Error;
                goto BAIL;
            }
            buffer          += bytesToRead;
            numBytes        -= bytesToRead;
            asset->position += bytesToRead;
            blockNum        += 1;
            offset           = bulkStart;
            bytesToRead      = s_sectorSize;
        }
    }
    u32 numBytesRead = asset->position - origPosition;
    s_mutex->API->Unlock(s_mutex);
    return numBytesRead;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    return 0;
}

static u32 Asset_Write(Asset *asset, u32 numBytes, const u8 *buffer) {
    u32 numBytesWritten = 0;
    s_mutex->API->Lock(s_mutex);
    if (asset->status == kOpStatus_Open) {
        asset->status = kOpStatus_OpenWrite;
        AllocBlocks *allocBlocks = SetAllocBlocksOnAsset(asset);
        if (allocBlocks == NULL) {
            asset->status = kOpStatus_Error;
            goto BAIL;
        }
        if (asset->entry) {
            /* Delete asset (if it exists) on first write operation */
            u32 numBlocks = (asset->sizeInBytes + s_sectorSize - 1) / s_sectorSize;
            DeleteAsset(asset, sizeof(AssetEntry) + numBlocks * sizeof(u16));
        }
        u32 entrySize = sizeof(u32) + (kIncrementalBlocks * sizeof(u16));
        if (entrySize < asset->info->smallStorageLimit) {
            entrySize = asset->info->smallStorageLimit;
        }
        entrySize += sizeof(u32);
        AssetEntry *newEntry = lt_realloc(asset->entry, entrySize);
        if (newEntry) {
            /* Start entry assuming it is small-storage */
            asset->entry              = newEntry;
            asset->entry->sizeInBytes = kUsingSmallStorage;
        } else {
            asset->status = kOpStatus_Error;
            goto BAIL;
        }
    } else if (asset->status != kOpStatus_OpenWrite) {
        asset->status = kOpStatus_Error;
        goto BAIL;
    }
    /* Write data to asset, first using small-storage */
    u32 origPosition = asset->position;
    if (asset->position < asset->info->smallStorageLimit) {
        u32 numBytesToWrite = asset->info->smallStorageLimit - asset->position;
        if (numBytes < numBytesToWrite) numBytesToWrite = numBytes;
        lt_memcpy(asset->entry->data + asset->position, buffer, numBytesToWrite);
        asset->position += numBytesToWrite;
        buffer          += numBytesToWrite;
        numBytes        -= numBytesToWrite;
    }
    if (numBytes > 0) {
        if (asset->entry->sizeInBytes == kUsingSmallStorage) {
            /* Convert to bulk-storage */
            asset->entry->sizeInBytes = 0;
            s32 block = FindFreeBlockAndErase(asset, 0);
            if (block < 0 || block >= (s32)asset->info->numBulkSectors) {
                asset->status = kOpStatus_Error;
                goto BAIL;
            }
            u32 offset = (asset->info->firstBulkSector + block) * s_sectorSize;
            if (!WriteFlash(asset, offset, asset->info->smallStorageLimit, asset->entry->data)) {
                asset->status = kOpStatus_Error;
                goto BAIL;
            }
            /* Set block number AFTER write to avoid data clobber */
            asset->entry->blocks[0] = (u16)block;
        }
        /* Write to bulk-storage */
        u32 blockNum     = asset->position / s_sectorSize;
        u32 bulkStart    = asset->info->firstBulkSector * s_sectorSize;
        u32 offset       = bulkStart + (asset->position % s_sectorSize);
        u32 bytesToWrite = s_sectorSize - (asset->position % s_sectorSize);
        while (numBytes > 0) {
            if ((asset->position & (s_sectorSize - 1)) == 0) {
                s32 block = FindFreeBlockAndErase(asset, asset->entry->blocks[blockNum - 1]);
                if (block < 0) {
                    asset->status = kOpStatus_Error;
                    goto BAIL;
                }
                if ((blockNum & (kIncrementalBlocks - 1)) == 0) {
                    u32 newEntrySize = sizeof(AssetEntry) + ((blockNum + kIncrementalBlocks) * sizeof(u16));
                    AssetEntry *newEntry = lt_realloc(asset->entry, newEntrySize);
                    if (newEntry == NULL) {
                        asset->status = kOpStatus_Error;
                        goto BAIL;
                    }
                    asset->entry = newEntry;
                }
                asset->entry->blocks[blockNum] = (u16)block;
            }
            if (asset->entry->blocks[blockNum] >= asset->info->numBulkSectors) {
                asset->status = kOpStatus_Error;
                goto BAIL;
            }
            offset += asset->entry->blocks[blockNum] * s_sectorSize;
            if (numBytes < bytesToWrite) bytesToWrite = numBytes;
            if (!WriteFlash(asset, offset, bytesToWrite, buffer)) {
                asset->status = kOpStatus_Error;
                goto BAIL;
            }
            buffer          += bytesToWrite;
            numBytes        -= bytesToWrite;
            asset->position += bytesToWrite;
            blockNum        += 1;
            offset           = bulkStart;
            bytesToWrite     = s_sectorSize;
        }
    }
    numBytesWritten = asset->position - origPosition;
BAIL:
    s_mutex->API->Unlock(s_mutex);
    return numBytesWritten;
}

static void Asset_Close(Asset *asset) {
    s_mutex->API->Lock(s_mutex);
    if (asset->status == kOpStatus_OpenWrite) {
        /* Finalize write operation */
        if (asset->writeBufOffset > 0) {
            /* Write remaining data in the write buffer */
            u32 position = asset->position - asset->writeBufOffset;
            u32 block    = asset->entry->blocks[position / s_sectorSize];
            if (block < asset->info->numBulkSectors) {
                u32 offset = (asset->info->firstBulkSector + block) * s_sectorSize;
                offset    += position % s_sectorSize;
                /* Do not call WriteFlash() here, just drain the remaining bytes */
                if (!s_iFlashUnit->WriteBytes(s_hFlashUnit, offset, kWriteBufSize, asset->writeBuf)) {
                    asset->status = kOpStatus_Error;
                }
            } else {
                asset->status = kOpStatus_Error;
            }
        }
    }
    if (asset->status == kOpStatus_OpenWrite) {
        /* Calculate entry size */
        u32 entrySize = sizeof(AssetEntry);
        if (asset->position <= asset->info->smallStorageLimit) {
            asset->entry->sizeInBytes = kUsingSmallStorage;
            /* Subtract off CRC field */
            entrySize += asset->position - sizeof(u32);
        } else {
            /* Copy temp CRC value to actual CRC and set sizeinBytes to actual size. */
            asset->entry->crc         = asset->entry->sizeInBytes;
            asset->entry->sizeInBytes = asset->position;
            entrySize += ((asset->position + s_sectorSize - 1) / s_sectorSize) * sizeof(u16);
        }
        if (s_settings->SetBinaryValue(asset->name, (u8 *)asset->entry, entrySize) &&
                asset->entry->sizeInBytes != kUsingSmallStorage) {
            /* Update allocated block mask */
            AllocBlocks *allocBlocks = asset->info->allocBlocks;
            for (u32 i = 0; i < (entrySize - sizeof(AssetEntry)) >> 1; i++) {
                u16 block = asset->entry->blocks[i];
                u8 maskByte = allocBlocks->blockMask[block >> 3] | (1 << (block & 0x7));
                allocBlocks->blockMask[block >> 3] = maskByte;
            }
            WriteAllocBlocks(asset);
        }
    }
    if (asset->status != kOpStatus_NotOpen) {
        RemovePartitionInfoFromAsset(asset);
        asset->status = kOpStatus_NotOpen;
        LTList_Remove(&asset->node);
        lt_free(asset->entry);
        asset->entry = NULL;
        lt_free(asset->writeBuf);
        asset->writeBuf = NULL;
    }
    s_mutex->API->Unlock(s_mutex);
}

static bool Asset_ConstructObject(Asset *asset) {
    asset->status = kOpStatus_NotOpen;
    return true;
}

static void Asset_DestructObject(Asset *asset) {
    s_mutex->API->Lock(s_mutex);
    LTList_Remove(&asset->node);
    RemovePartitionInfoFromAsset(asset);
    s_mutex->API->Unlock(s_mutex);
    lt_free(asset->entry);
    lt_free(asset->writeBuf);
    ltstring_destroy(asset->name);
}

/*_______________________
  LTAsset LTObject API */
define_LTObjectImplPublic(LTFile, Asset,
    SetName,
    Exists,
    Delete,
    GetSize,
    SeekToPosition,
    Open,
    Read,
    Write,
    Close
);

/*_________________________________________
  LTSystemFSAsset Root interface binding */

static void LTSystemFSAsset_LibFini(void) {
    s_iFlashUnit->Destroy(s_hFlashUnit);
    lt_closelibrary(s_deviceFlash);
    lt_closelibrary(s_settings);
    lt_closelibrary(s_byteOps);
    lt_destroyobject(s_mutex);
}

static bool LTSystemFSAsset_LibInit(void) {
    s_mutex    = lt_createobject(LTMutex);
    s_byteOps  = lt_openlibrary(LTUtilityByteOps);
    s_settings = lt_openlibrary(LTSystemSettings);
    if (s_mutex == NULL || s_byteOps == NULL || s_settings == NULL) {
        goto BAIL;
    }
    s_deviceFlash = lt_openlibrary(LTDeviceFlash);
    if (s_deviceFlash) {
        s_hFlashUnit = s_deviceFlash->CreateDeviceUnitHandle(0);
        if (s_hFlashUnit) s_iFlashUnit = lt_gethandleinterface(ILTFlashDeviceUnit, s_hFlashUnit);
    }
    if (s_deviceFlash == NULL || s_iFlashUnit == NULL) {
        if (s_iFlashUnit) s_iFlashUnit->Destroy(s_hFlashUnit);
        s_iFlashUnit = NULL;
        lt_closelibrary(s_deviceFlash);
        goto BAIL;
    }
    s_sectorSize = s_iFlashUnit->GetBytesPerSector(s_hFlashUnit);
    LTList_Init(&s_openAssets);
    return true;
BAIL:
    lt_closelibrary(s_settings);
    lt_closelibrary(s_byteOps);
    lt_destroyobject(s_mutex);
    return false;
}

define_LTObjectLibrary(1, LTSystemFSAsset_LibInit, LTSystemFSAsset_LibFini);

