/******************************************************************************
 * Assets.c                                               Offline Asset Builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <errno.h>

#include "lt/LTTypes.h"
#include "lt/core/LTStdlib.h"
#include "lt/utility/jsonparser/LTUtilityJsonParser.h"

#include "Image.h"
#include "Assets.h"

/* NOTE: Until libraries become mockable singleton objects we cannot easily
 *       use the existing LTAsset code to generate assets here. This is
 *       because LTProductConfig and the Flash Driver would need to be
 *       mocked to support this. For now we manually create offline assets
 *       directly in their raw format. */

enum {
    kMaxSmallStorageLimit    = 1024,       /* Maximum allowed small-storage limit in bytes */
    kUsingSmallStorage       = 0xffffffff, /* Value indicates when small-storage is used */
    kSettingsMagic_ReadWrite = 0x30544553, /* Settings partition magic number */
    kWriteQuantum            = 32,         /* Write quantum for writing asset metadata */
};

typedef struct {
    u32           sizeInBytes;       /* Asset size (or kUsingSmallStorage) */
    union {
        u8        data[0];           /* small-storage data */
        struct {
            u32   crc;               /* CRC of bulk-storage data */
            u16   blocks[0];         /* ordered block list for asset partition data */
        };
    };
} AssetEntry;

typedef struct {
    u16           firstBulkSector;   /* First bulk sector in the asset partition */
    u16           numBulkSectors;    /* Number of bulk sectors in the asset partition */
    u8            blockMask[0];      /* bit-vector of allocated blocks */
} AllocBlocks;

static Area     *s_pArea        = NULL;
static LTString  s_assetName    = NULL;
static Image     s_assetsImage;

static u32       s_numDirectorySectors = 0;
static u32       s_smallStorageLimit   = 0;
static u32       s_bulkOffset          = 0;
static u32       s_settingsOffset      = 0;
static u32       s_nBlockSize          = 0;

static int AddBinarySetting(const char *pKey, const u8 *pValue, u16 nValueSize) {
    u8 *pData = s_assetsImage.pData + s_settingsOffset;
    pData[0]  = 'b';
    pData[1]  = lt_strlen(pKey);
    lt_memcpy(pData + 2, pKey, pData[1]);
    lt_memcpy(pData + 2 + pData[1], (u8 *)&nValueSize, sizeof(u16));
    lt_memcpy(pData + 4 + pData[1], pValue, nValueSize);
    s_settingsOffset += 4 + pData[1] + nValueSize;
    if (s_settingsOffset >= s_numDirectorySectors * s_nBlockSize / 2) {
        printf("Settings data exceeds available space in assets image\n");
        return -1;
    }
    return 0;
}

static int AddAsset(const char *pName, const char *pFilename) {
    Image image;
    if (ImageCreateFromFile(&image, pFilename) < 0) {
        return -1;
    }
    printf("  Adding '/%s/%s' from '%s', size %lu\n", s_pArea->name, pName, pFilename, LT_Pu32(image.nSize));
    int nStatus = 0;
    AssetEntry *pEntry = NULL;
    if (image.nSize > s_smallStorageLimit) {
        if (s_bulkOffset + image.nSize <= s_assetsImage.nSize) {
            /* Copy image into asset bulk-storage */
            lt_memcpy(s_assetsImage.pData + s_bulkOffset, image.pData, image.nSize);
            u32 numBlocks = (image.nSize + s_nBlockSize - 1) / s_nBlockSize;
            pEntry = lt_malloc(sizeof(AssetEntry) + numBlocks * sizeof(u16));
            if (!pEntry) {
                nStatus = -ENOMEM;
                goto BAIL;
            }
            pEntry->sizeInBytes = image.nSize;
            pEntry->crc         = image.nCRC;
            for (u32 b = 0; b < numBlocks; b++) {
                pEntry->blocks[b] = (s_bulkOffset / s_nBlockSize) - s_numDirectorySectors + b;
            }
            if (AddBinarySetting(pName, (u8 *)pEntry, sizeof(AssetEntry) + numBlocks * sizeof(u16)) != 0) {
                nStatus = -ENOSPC;
                goto BAIL;
            }
            lt_free(pEntry);
            /* Move offset and align up to next whole block */
            s_bulkOffset += image.nSize;
            s_bulkOffset  = (s_bulkOffset + s_nBlockSize - 1) & ~(s_nBlockSize - 1);
        } else {
            printf("Asset '%s' exceeds available space in assets image\n", pName);
            nStatus = -ENOSPC;
        }
    } else {
        /* Add asset to small-storage */
        pEntry = lt_malloc(sizeof(u32) + image.nSize);
        if (!pEntry) {
            nStatus = -ENOMEM;
            goto BAIL;
        }
        pEntry->sizeInBytes = kUsingSmallStorage;
        lt_memcpy(pEntry->data, image.pData, image.nSize);
        if (AddBinarySetting(pName, (u8 *)pEntry, sizeof(u32) + image.nSize) != 0) {
            nStatus = -ENOSPC;
            goto BAIL;
        }
    }
BAIL:
    lt_free(pEntry);
    ImageFree(&image);
    return nStatus;
}

static int AddAllocBlocks(void) {
    u32 numBulkSectors = (s_pArea->nMaxSize / s_nBlockSize) - s_numDirectorySectors;
    u32 numBlocksUsed  =      (s_bulkOffset / s_nBlockSize) - s_numDirectorySectors;
    printf("  Adding '/%s/' allocation block table, %lu blocks used out of %lu\n",
              s_pArea->name, LT_Pu32(numBlocksUsed), LT_Pu32(numBulkSectors));
    AllocBlocks *pBlocks = lt_malloc(sizeof(AllocBlocks) + ((numBulkSectors + 7) >> 3));
    if (!pBlocks) return -ENOMEM;
    pBlocks->firstBulkSector = (s_pArea->nOffset / s_nBlockSize) + s_numDirectorySectors;
    pBlocks->numBulkSectors  = numBulkSectors;
    lt_memset(pBlocks->blockMask, 0, (numBulkSectors + 7) >> 3);
    for (u32 b = 0; b < numBlocksUsed; b++) {
        pBlocks->blockMask[b >> 3] |= (1 << (b & 7));
    }
    if (AddBinarySetting("", (u8 *)pBlocks, sizeof(AllocBlocks) + ((numBulkSectors + 7) >> 3)) != 0) {
        lt_free(pBlocks);
        return -ENOSPC;
    }
    lt_free(pBlocks);
    return 0;
}

static bool AssetsJsonParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData) {
    enum { kTopLevel, kInAssets, kGotAsset };
    static u32 state = kTopLevel;
    static bool bInAssetOfInterest = false;
    switch (state) {
    case kTopLevel:
        if (key) {
            if (value->type == kLTUtilityJsonParser_ValueType_Object) {
                bInAssetOfInterest = false;
                if (lt_strcmp(key, s_pArea->name) == 0) bInAssetOfInterest = true;
                state = kInAssets;
            } else if (value->type != kLTUtilityJsonParser_ValueType_ObjectExit) return false;
        }
        break;

    case kInAssets:
        if (value->type == kLTUtilityJsonParser_ValueType_ObjectExit) {
            state = kTopLevel;
        } else if (value->type == kLTUtilityJsonParser_ValueType_Object) {
            if (bInAssetOfInterest) ltstring_set(&s_assetName, key);
            state = kGotAsset;
        } else return false;
        break;

    case kGotAsset:
        if (value->type == kLTUtilityJsonParser_ValueType_ObjectExit) {
            state = kInAssets;
        } else if (value->type == kLTUtilityJsonParser_ValueType_String) {
            if (lt_strcmp(key, "file") == 0 && bInAssetOfInterest) {
                if (AddAsset(s_assetName, value->string) < 0) return false;
            }
        } else return false;
        break;

    default:
        return false;
    }

    return true;
}

int AssetsCreateImage(int nAreaIdx, char *pIFilename, char *pOFilename) {
    if (ImageIsDeviceLittleEndian() == false || ImageIsHostLittleEndian() == false) {
        printf("This command only supports little-endian systems\n");
        return -1;
    }
    char *pInputFilename  = "LTAssetConfig.json";
    char *pOutputFilename = "LTAssets.bin";
    if (pIFilename) pInputFilename  = pIFilename;
    if (pOFilename) pOutputFilename = pOFilename;
    if (nAreaIdx < 0) {
        s_pArea = ImageGetAreaByName("assets");
    } else {
        s_pArea = ImageGetArea(nAreaIdx);
    }
    if (!s_pArea) {
        printf("Cannot find area\n");
        return -1;
    }
    if (ImageGetWriteQuantum() > kWriteQuantum) {
        printf("Write quantum %u is larger than the maximum %u\n",
                  ImageGetWriteQuantum(), kWriteQuantum);
        return -1;
    }
    s_nBlockSize = ImageGetBlockSize();
    if (lt_strcmp(s_pArea->type, "assets") != 0) {
        printf("Area %s is not an assets partition (type = %s)\n", s_pArea->name, s_pArea->type);
        return -1;
    }
    /* Read product config to determine asset configuration values and terminate with a null byte */
    Image imageJson;
    if (ImageCreateFromFile(&imageJson, "LTProductConfig.json") < 0) {
        printf("Failed to open LTProductConfig.json\n");
        return -1;
    }
    ImageAppendFromBuffer(&imageJson, (u8 *)"\0", 1);
    /* Create parser and read configuration values */
    LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);
    if (!parser) {
        printf("Failed to create Json parser\n");
        ImageFree(&imageJson);
        return -1;
    }
    {
        LTUtilityJsonParser_Value value = { .integer = 0 };
        LTString jsonPath = NULL;
        ltstring_format(&jsonPath, "/config/lib/LTSystemFS/assets/%s/NumDirectorySectors", s_pArea->name);
        parser->API->GetValue(parser, (char *)imageJson.pData, jsonPath, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_Integer || value.integer < 0) value.integer = 0;
        s_numDirectorySectors = value.integer;
        ltstring_format(&jsonPath, "/config/lib/LTSystemFS/assets/%s/SmallStorageLimit", s_pArea->name);
        parser->API->GetValue(parser, (char *)imageJson.pData, jsonPath, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_Integer || value.integer < 0) value.integer = 0;
        s_smallStorageLimit = value.integer;
        if (s_smallStorageLimit > kMaxSmallStorageLimit) s_smallStorageLimit = kMaxSmallStorageLimit;
        ltstring_destroy(jsonPath);
    }
    ImageFree(&imageJson);
    if (s_numDirectorySectors == 0 || s_numDirectorySectors > (s_pArea->nMaxSize / s_nBlockSize)) {
        /* Invalid asset configuration values */
        printf("Invalid asset configuration values: NumDirectorySectors = %lu, SmallStorageLimit = %lu\n",
                  LT_Pu32(s_numDirectorySectors), LT_Pu32(s_smallStorageLimit));
        lt_destroyobject(parser);
        return -1;
    }
    /* Read in assets Json file and terminate with a null byte */
    if (ImageCreateFromFile(&imageJson, pInputFilename) < 0) {
        lt_destroyobject(parser);
        return -1;
    }
    ImageAppendFromBuffer(&imageJson, (u8 *)"\0", 1);
    if (!parser->API->ValidateJson(parser, (char *)imageJson.pData)) {
        printf("Invalid JSON detected in (%s)\n", pInputFilename);
        ImageFree(&imageJson);
        lt_destroyobject(parser);
        return -1;
    }

    printf("Creating assets for area '%s' in %s\n", s_pArea->name, pOutputFilename);

    /* Initialize assets buffer */
    if (ImageCreateBlank(&s_assetsImage, s_pArea->nMaxSize) < 0) {
        printf("Failed to create assets image buffer\n");
        ImageFree(&imageJson);
        lt_destroyobject(parser);
        ltstring_destroy(s_assetName);
        goto BAIL;
    }
    s_bulkOffset = s_numDirectorySectors * s_nBlockSize;
    /* Initialize settings data */
    *((u32 *)(s_assetsImage.pData + 0)) = kSettingsMagic_ReadWrite;
    *((u32 *)(s_assetsImage.pData + 4)) = 0;
    s_settingsOffset = kWriteQuantum;
    /* Parse Json file and create assets */
    if (!parser->API->ParseJson(parser, (char *)imageJson.pData, AssetsJsonParseCallback, NULL)) {
        printf("JSON parse failed for %s\n", pInputFilename);
        goto BAIL;
    }
    if (ltstring_isempty(s_assetName)) {
        printf("No assets defined for area '%s' in %s\n", s_pArea->name, pInputFilename);
        goto BAIL;
    }
    ImageFree(&imageJson);
    lt_destroyobject(parser);
    /* Add allocation block table */
    if (AddAllocBlocks() < 0) {
        printf("Failed to add allocation block table\n");
        goto BAIL;
    }
    /* Terminate settings and create image to calculate CRC32 */
    s_assetsImage.pData[s_settingsOffset++] = 0xff;
    Image dummyImage;
    if (ImageCreateFromBuffer(&dummyImage, s_assetsImage.pData + kWriteQuantum,
                                                     s_settingsOffset - kWriteQuantum) < 0) {
        printf("Failed to create dummy image for CRC calculation\n");
        goto BAIL;
    }
    ImageFree(&dummyImage);
    /* Add CRC to settings */
    s_assetsImage.pData[s_settingsOffset + 0] = (dummyImage.nCRC >>  0) & 0xff;
    s_assetsImage.pData[s_settingsOffset + 1] = (dummyImage.nCRC >>  8) & 0xff;
    s_assetsImage.pData[s_settingsOffset + 2] = (dummyImage.nCRC >> 16) & 0xff;
    s_assetsImage.pData[s_settingsOffset + 3] = (dummyImage.nCRC >> 24) & 0xff;
    s_settingsOffset += sizeof(u32);
    if (s_settingsOffset >= s_numDirectorySectors * s_nBlockSize / 2) {
        printf("Settings data exceeds available space in assets image\n");
        return -1;
    }
    /* Truncate and write assets to output file */
    if (ImageResize(&s_assetsImage, s_bulkOffset, 0) < 0) {
        printf("Failed to resize assets image\n");
        goto BAIL;
    }
    if (ImageWriteToFile(&s_assetsImage, pOutputFilename) < 0) {
        printf("Failed to write assets to %s\n", pOutputFilename);
        goto BAIL;
    }
    ltstring_destroy(s_assetName);
    return 0;
BAIL:
    ImageFree(&s_assetsImage);
    ImageFree(&imageJson);
    lt_destroyobject(parser);
    ltstring_destroy(s_assetName);
    return -1;
}
