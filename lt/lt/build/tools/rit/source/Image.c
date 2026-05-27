/******************************************************************************
 * Image.c                                                        Image Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lt/LTTypes.h"
#include "lt/system/crypto/LTSystemCrypto.h"
#include "lt/utility/byteops/LTUtilityByteOps.h"

#include "json.h"
#include "Image.h"

#ifdef LT_LITTLEENDIAN
#define IS_HOST_LITTLEENDIAN  1
#else
#define IS_HOST_LITTLEENDIAN  0
#endif

/* Dulcified variables */
static LTSystemCrypto   * s_systemCrypto = NULL;
static LTUtilityByteOps * s_utilityByteOps = NULL;

/* Search Paths */
static char   s_searchPaths[512] = ".";

/* Platform Config */
static char   s_platformName[32];
static char   s_deviceType[32];
static char   s_deviceFamily[32];
static bool   s_bDeviceIsLittleEndian = true;
static bool   s_bEndianSpecified = false;

/* Flash Config */
static char   s_flasherConfigFilename[128] = "LTFlasher.json";
static u32    s_nBlockSize = 0;
static u32    s_nNumBlocks = 0;
static u8     s_nTableVersion = 0;
static u32    s_nWriteQuantum = 1;
static char   s_defaultPartition[16];

/* Flash Areas (partitions) */
#define MAX_AREAS                128
static Area   s_areas[MAX_AREAS];

/* Start at 1 since the "all" area is implicitly area 0 */
static unsigned s_nNumAreas = 1;

/* JSON5 parser error strings */
static const char * s_pParserErrorString[] = {
    "OK",
    "Expected comma or closing bracket",
    "Expected colon",
    "Expected opening quote",
    "Invalid string escape sequence",
    "Invalid number format",
    "Invalid value",
    "Premature end of buffer",
    "Invalid string",
    "Allocator failed",
    "Unexpected trailing characters",
    "Unknown error"
};

static bool ParseJSONPlatform(struct json_object_s * pObj, char * pConfigFilename) {
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_string: {
            struct json_string_s * pStr = pElm->value->payload;
            if (strcmp(pElm->name->string, "name") == 0) {
                lt_strncpyTerm(s_platformName, pStr->string, sizeof(s_platformName));
            } else if (strcmp(pElm->name->string, "device") == 0) {
                lt_strncpyTerm(s_deviceType, pStr->string, sizeof(s_deviceType));
            } else if (strcmp(pElm->name->string, "family") == 0) {
                lt_strncpyTerm(s_deviceFamily, pStr->string, sizeof(s_deviceFamily));
            } else if (strcmp(pElm->name->string, "endian") == 0) {
                if (strcmp(pStr->string, "little") == 0) {
                    s_bDeviceIsLittleEndian = true;
                    s_bEndianSpecified = true;
                } else if (strcmp(pStr->string, "big") == 0) {
                    s_bDeviceIsLittleEndian = false;
                    s_bEndianSpecified = true;
                } else {
                    printf("Config Error: Invalid endian, choices are little or big.\n");
                    return false;
                }
            } else if (strcmp(pElm->name->string, "flasher_config_file") == 0) {
                lt_strncpyTerm(s_flasherConfigFilename, pStr->string, sizeof(s_flasherConfigFilename));
            } else printf("Config Warning: Ignoring unknown string: %s\n", pElm->name->string);
            break;
        }
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    return true;
}

static bool ParseJSONFlash(struct json_object_s * pObj) {
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_string: {
            struct json_string_s * pStr = pElm->value->payload;
            if (strcmp(pElm->name->string, "default_partition") == 0) {
                lt_strncpyTerm(s_defaultPartition, pStr->string, sizeof(s_defaultPartition));
            } else printf("Config Warning: Ignoring unknown string: %s\n", pElm->name->string);
            break;
        }
        case json_type_number: {
            struct json_number_s * pNum = pElm->value->payload;
            s64 nValue = strtoll(pNum->number, NULL, 0);
            if (strcmp(pElm->name->string, "block_size") == 0) {
                s_nBlockSize = (u32)nValue;
                LT_ASSERT((s_nBlockSize & (s_nBlockSize - 1)) == 0);
            } else if (strcmp(pElm->name->string, "num_blocks") == 0) {
                s_nNumBlocks = (u32)nValue;
                LT_ASSERT((s_nNumBlocks & (s_nNumBlocks - 1)) == 0);
            } else if (strcmp(pElm->name->string, "table_version") == 0) {
                s_nTableVersion = (u32)nValue;
            } else if (strcmp(pElm->name->string, "write_quantum") == 0) {
                s_nWriteQuantum = (u32)nValue;
            } else printf("Config Warning: Ignoring unknown number: %s\n", pElm->name->string);
            break;
        }
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    return true;
}

static bool ParseJSONPartition(struct json_object_s * pObj, const char * pName, u32 nAreaNum) {
    Area * pArea = &s_areas[nAreaNum];
    /* Copy name and defaults */
    lt_strncpyTerm(pArea->name, pName, sizeof(pArea->name));
    pArea->filename[0]       = '\0';
    pArea->type[0]           = '\0';
    pArea->nType             = kImageType_Misc;
    pArea->nAreaNum          = nAreaNum;
    pArea->bMustFlashAll     = false;
    pArea->bExcludeFromAreas = false;
    pArea->flags             = 0;
    /* Mandatory parameter checks */
    bool bFoundOffset        = false;
    bool bFoundSize          = false;
    /* Parse fields */
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_string: {
            struct json_string_s * pStr = pElm->value->payload;
            if (strcmp(pElm->name->string, "type") == 0) {
                lt_strncpyTerm(pArea->type, pStr->string, sizeof(pArea->type));
                if (strcmp(pStr->string, "raw") == 0) {
                    pArea->nType = kImageType_Raw;
                } else if (strcmp(pStr->string, "part") == 0) {
                    pArea->nType = kImageType_PartitionTable;
                } else if (strcmp(pStr->string, "boot") == 0) {
                    pArea->nType = kImageType_Boot;
                } else if (strcmp(pStr->string, "otainfo") == 0) {
                    pArea->nType = kImageType_OTAInfo;
                } else if (strcmp(pStr->string, "fw") == 0) {
                    pArea->nType = kImageType_Firmware;
                } else if (strcmp(pStr->string, "fw_mfg") == 0) {
                    pArea->nType = kImageType_FirmwareMfg;
                } else if (strcmp(pStr->string, "settings") == 0) {
                    pArea->nType = kImageType_Settings;
                } else {
                    pArea->nType = kImageType_Misc;
                }
            } else if (strcmp(pElm->name->string, "file") == 0) {
                lt_strncpyTerm(pArea->filename, pStr->string, sizeof(pArea->filename));
            } else printf("Config Warning: Ignoring unknown string: %s\n", pElm->name->string);
            break;
        }
        case json_type_number: {
            struct json_number_s * pNum = pElm->value->payload;
            s64 nValue = strtoll(pNum->number, NULL, 0);
            if (strcmp(pElm->name->string, "offs") == 0) {
                pArea->nOffset = (u32)nValue * s_nBlockSize;
                bFoundOffset = true;
            } else if (strcmp(pElm->name->string, "size") == 0) {
                pArea->nMaxSize = (u32)nValue * s_nBlockSize;
                bFoundSize = true;
            } else printf("Config Warning: Ignoring unknown number: %s\n", pElm->name->string);
            break;
        }
        case json_type_false:
        case json_type_true: {
            if (strcmp(pElm->name->string, "encrypted") == 0) {
                if (pElm->value->type == json_type_true) {
                    pArea->flags |= kLTDeviceFlash_PartitionFlags_Encrypted;
                }
                break;
            }
        }
        /* fallthrough case */
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    if (!bFoundOffset || !bFoundSize) {
        printf("Config Error: Must specify offset and size for area %s\n", pArea->name);
        return false;
    }
    return true;
}

static bool ParseJSONPartitions(struct json_object_s * pObj) {
    if (s_nBlockSize == 0) {
        printf("Config Error: Must specify block_size before partitions in config file\n");
        return false;
    }
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_object: {
            if (!ParseJSONPartition(pElm->value->payload, pElm->name->string, s_nNumAreas)) return false;
            if (s_nNumAreas++ >= MAX_AREAS) {
                printf("Config Error: Exceeded max number of partitions\n");
                return false;
            }
            break;
        }
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    return true;
}

static bool ParseJSONConfig(struct json_object_s * pObj, char * pConfigFilename) {
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_object:
            if (strcmp(pElm->name->string, "platform") == 0) {
                if (!ParseJSONPlatform(pElm->value->payload, pConfigFilename)) return false;
            } else if (strcmp(pElm->name->string, "flash") == 0) {
                if (!ParseJSONFlash(pElm->value->payload)) return false;
            } else if (strcmp(pElm->name->string, "partitions") == 0) {
                if (!ParseJSONPartitions(pElm->value->payload)) return false;
            } else printf("Config Warning: Ignoring unknown object: %s\n", pElm->name->string);
            break;
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    return true;
}

static int ReadConfigFile(char * pConfigFilename) {
    Image image;
    if (ImageCreateFromFile(&image, pConfigFilename) < 0) {
        printf("Cannot open config: %s\n", pConfigFilename);
        return -ENOENT;
    }

    /* Parse as JSON5 with location information */
    struct json_parse_result_s result;
    struct json_value_s * pRoot =
        json_parse_ex(image.pData, image.nSize,
                      json_parse_flags_allow_json5 | json_parse_flags_allow_location_information,
                      NULL, NULL, &result);

    ImageFree(&image);

    if (result.error) {
        printf("Error: %s on line %d of %s, row %d (offset %d)\n",
                  s_pParserErrorString[result.error], (u32)result.error_line_no,
                  pConfigFilename, (u32)result.error_row_no, (u32)result.error_offset);
        return -1;
    }

    LT_ASSERT(pRoot);
    LT_ASSERT(pRoot->type == json_type_object);

    /* Parse Config Tree */
    if (!ParseJSONConfig(pRoot->payload, pConfigFilename)) {
        free(pRoot);
        return -1;
    }
    free(pRoot);

    /* Parameter checking */
    if (strlen(s_platformName) == 0) {
        printf("Must specify platform:name in config file\n");
        return -1;
    }
    if (strlen(s_deviceType) == 0) {
        printf("Must specify platform:device in config file\n");
        return -1;
    }
    if (strlen(s_deviceFamily) == 0) {
        printf("Must specify platform:family in config file\n");
        return -1;
    }
    if (!s_bEndianSpecified) {
        printf("Must specify platform:endian in config file\n");
        return -1;
    }
    if (s_nBlockSize == 0) {
        printf("Must specify flash:block_size in config file\n");
        return -1;
    }
    if (s_nNumBlocks == 0) {
        printf("Must specify flash:num_blocks in config file\n");
        return -1;
    }
    if (strlen(s_defaultPartition) == 0) {
        printf("Must specify flash:default_partition in config file\n");
        return -1;
    }

    /* Initialize "all" area */
    lt_strncpyTerm(s_areas[kImageAreaAllIndex].name, "all", sizeof(s_areas[kImageAreaAllIndex].name));
    lt_strncpyTerm(s_areas[kImageAreaAllIndex].type, "all", sizeof(s_areas[kImageAreaAllIndex].name));
    s_areas[kImageAreaAllIndex].filename[0]       = '\0';
    s_areas[kImageAreaAllIndex].nType             = kImageType_All;
    s_areas[kImageAreaAllIndex].nAreaNum          = 0;
    s_areas[kImageAreaAllIndex].nOffset           = 0;
    s_areas[kImageAreaAllIndex].nMaxSize          = s_nNumBlocks * s_nBlockSize;
    s_areas[kImageAreaAllIndex].bMustFlashAll     = true;
    s_areas[kImageAreaAllIndex].bExcludeFromAreas = true;
    return 0;
}

// Calculate basic 32-bit checksum for image
static u32 CalcChecksum(u32 * pData, u32 size) {
    u32 nChecksum = 0;
    u32 ix;
    for (ix = 0; ix < size/sizeof(u32); ix++) {
        nChecksum += pData[ix];
    }
    u32 size_mod4 = size & 0x3;
    if (size_mod4) {
        u32 tmp = 0xffffffff << (size_mod4 * 8);
        u8 * pData8 = (u8 *) &pData[ix];
        for (u32 iy = 0; iy < size_mod4; iy++) {
            tmp += (pData8[iy] << (iy * 8));
        }
        nChecksum += tmp;
    }
    return nChecksum;
}

// For digests calculated over multiple chunks, pass NULL as digest argument
//   up until the last chunk.  Otherwise the complete digest is calculated.
static int CalcSHA2Digest(u8 * pDigest, u8 * pInput, u32 nSize) {
    static LT_SHA256_CTX *pContext = NULL;
    if (!pContext) {
        pContext = s_systemCrypto->CreateSeqSHA256();
        if (!pContext) return -1;
    }
    if (kLTSystemCrypto_Result_Ok != s_systemCrypto->UpdateSeqSHA256(pContext, pInput, nSize)) {
        s_systemCrypto->DestroySeqSHA256(pContext);
        pContext = NULL;
        return -1;
    }
    if (pDigest) {
        LTSystemCryptoResult result = s_systemCrypto->FinishSeqSHA256(pContext, pDigest);
        s_systemCrypto->DestroySeqSHA256(pContext);
        pContext = NULL;
        if (kLTSystemCrypto_Result_Ok != result) return -1;
    }
    return 0;
}

/* Obtain number of areas */
unsigned ImageGetNumAreas(void) {
    return s_nNumAreas;
}

/* Get the default area name (usually firmware area) */
char * ImageGetDefaultAreaName(void) {
    return s_defaultPartition;
}

/* Get an area using index */
Area * ImageGetArea(int nAreaIdx) {
    if (nAreaIdx < 0 || nAreaIdx > s_nNumAreas) return NULL;
    return &s_areas[nAreaIdx];
}

Area * ImageGetAreaByName(char * pName) {
    if (!pName) return NULL;
    for (unsigned a = 0; a < s_nNumAreas; a++) {
        if (strcmp(s_areas[a].name, pName) == 0) {
            return &s_areas[a];
        }
    }
    return NULL;
}

int ImageGetAreaIndexByName(char * pName) {
    if (!pName) return -1;
    for (unsigned a = 0; a < s_nNumAreas; a++) {
        if (strcmp(s_areas[a].name, pName) == 0) {
            return a;
        }
    }
    return -1;
}

int ImageGetNextAreaIndexByType(ImageType type, int nStartAreaIdx) {
    for (unsigned a = nStartAreaIdx; a < s_nNumAreas; a++) {
        if (s_areas[a].nType == type) {
            return a;
        }
    }
    return -1;
}

static bool ParseJSONFlasherConfig(struct json_object_s * pObj, Image * pFlasherImage, const char * pDeviceVariant) {
    struct json_object_element_s * pElm = pObj->start;
    for (pElm = pObj->start; pElm != NULL; pElm = pElm->next) {
        switch (pElm->value->type) {
        case json_type_object: {
            if (strcmp(pElm->name->string, "flasher") == 0) {
                return ParseJSONFlasherConfig(pElm->value->payload, pFlasherImage, pDeviceVariant);
            } else if (strcmp(pElm->name->string, pDeviceVariant) == 0) {
                return ParseJSONFlasherConfig(pElm->value->payload, pFlasherImage, pDeviceVariant);
            }
            break;
        }
        case json_type_string: {
            struct json_string_s * pStr = pElm->value->payload;
            if (strcmp(pElm->name->string, "image_b") == 0) {
                u32 nLength = (pStr->string_size * 3) >> 2;
                if (ImageCreateBlank(pFlasherImage, nLength) < 0) return false;
                nLength = s_utilityByteOps->Base64Decode((const char *)pStr->string, pStr->string_size, pFlasherImage->pData, nLength);
                if (0 == nLength) {
                    printf("Binary (base64) parse error on line %d, aborting...\n",  (int)((struct json_value_ex_s *)pElm->value)->line_no);
                    return false;
                }
                pFlasherImage->nSize = nLength;
            } else printf("Config Warning: Ignoring unknown string: %s\n", pElm->name->string);
            break;
        }
        default:
            printf("Config Warning: Ignoring unknown element: %s\n", pElm->name->string);
            break;
        }
    }
    return true;
}

int ImageGetFlasherImage(Image * pFlasherImage, const char * pDeviceVariant) {
    pFlasherImage->nSize = 0;
    if (strlen(s_flasherConfigFilename) == 0) return -1;

    Image image;
    if (ImageCreateFromFile(&image, s_flasherConfigFilename) < 0) {
        printf("Cannot open flasher config: %s\n", s_flasherConfigFilename);
        return -ENOENT;
    }

    /* Parse as JSON5 with location information */
    struct json_parse_result_s result;
    struct json_value_s * pRoot =
        json_parse_ex(image.pData, image.nSize,
                      json_parse_flags_allow_json5 | json_parse_flags_allow_location_information,
                      NULL, NULL, &result);

    ImageFree(&image);

    if (result.error) {
        printf("Error: %s on line %d of %s, row %d (offset %d)\n",
                  s_pParserErrorString[result.error], (u32)result.error_line_no,
                  s_flasherConfigFilename, (u32)result.error_row_no, (u32)result.error_offset);
        return -1;
    }

    LT_ASSERT(pRoot);
    LT_ASSERT(pRoot->type == json_type_object);

    /* Parse Flasher Config File */
    if (!ParseJSONFlasherConfig(pRoot->payload, pFlasherImage, pDeviceVariant)) {
        free(pRoot);
        return -1;
    }
    free(pRoot);
    if (pFlasherImage->nSize == 0) return -1;
    return 0;
}

u32 ImageGetBlockSize() {
    return s_nBlockSize;
}

u8 ImageGetTableVersionNumber() {
    return s_nTableVersion;
}

u32 ImageGetWriteQuantum() {
    return s_nWriteQuantum;
}

bool ImageIsDeviceLittleEndian(void) {
    return s_bDeviceIsLittleEndian;
}

bool ImageIsHostLittleEndian(void) {
    return IS_HOST_LITTLEENDIAN;
}

int ImageList(void) {
    printf("Name             Type             ");
    printf("[ Offset ][  Size  ]       Size  Flags\n");
    printf("-----------------------------------");
    printf("----------------------------------------\n");
    for (u32 a = 0; a < s_nNumAreas; a++) {
            printf("%-16s %-16s [%08x][%08x] %10u %08X %4s\n",
                       s_areas[a].name, s_areas[a].type,
                       s_areas[a].nOffset, s_areas[a].nMaxSize, s_areas[a].nMaxSize,
                       s_areas[a].flags, s_areas[a].bExcludeFromAreas ? "" : "(P)");
    }
    printf("\n(P) ->  Area included in partition table\n");
    printf("Default Area: %s\n", s_defaultPartition);
    return 0;
}

u16 ImageHostToDeviceU16(u16 * pOut, u16 nIn) {
    u16 nOut = nIn;
    if (s_bDeviceIsLittleEndian != IS_HOST_LITTLEENDIAN) {
        nOut  = LT_SWAP16(nIn);
    }
    if (pOut) *pOut = nOut;
    return nOut;
}

u16 ImageDeviceToHostU16(u16 * pOut, u16 nIn) {
    return ImageHostToDeviceU16(pOut, nIn);
}

u32 ImageHostToDeviceU32(u32 * pOut, u32 nIn) {
    u32 nOut = nIn;
    if (s_bDeviceIsLittleEndian != IS_HOST_LITTLEENDIAN) {
        nOut  = LT_SWAP32(nIn);
    }
    if (pOut) *pOut = nOut;
    return nOut;
}

u32 ImageDeviceToHostU32(u32 * pOut, u32 nIn) {
    return ImageHostToDeviceU32(pOut, nIn);
}

const char * ImageGetDeviceType(void) {
    return (const char *)s_deviceType;
}

const char * ImageGetDeviceFamily(void) {
    return (const char *)s_deviceFamily;
}

/* Create a blank image */
int ImageCreateBlank(Image * pImage, u32 nSize) {
    pImage->pData = (u8 *)malloc(nSize);
    if (pImage->pData == NULL) {
        printf("Memory allocation error, aborting...\n");
        return -1;
    }
    memset(pImage->pData, 0xff, nSize);
    pImage->nSize = nSize;
    return ImageCalcChecksums(pImage);
}

/* Create an image from a buffer */
int ImageCreateFromBuffer(Image * pImage, u8 * pData, u32 nSize) {
    pImage->pData = (u8 *)malloc(nSize);
    if (pImage->pData == NULL) {
        printf("Memory allocation error, aborting...\n");
        return -ENOMEM;
    }
    memcpy(pImage->pData, pData, nSize);
    pImage->nSize = nSize;
    return ImageCalcChecksums(pImage);
}

/* Find a file and stat it, NB: callee must free *pFullPath */
int ImageFindAndStatFile(const char * pFilename, char ** ppFullPath, struct stat * pStat) {
    char * pFullPath;
    if (pFilename[0] == '/') {
         /* Absolute path, no search */
         pFullPath = (char *)malloc(strlen(pFilename) + 1);
         if (!pFullPath) {
             return -ENOMEM;
         }
         strcpy(pFullPath, pFilename);
         if (stat(pFullPath, pStat) < 0) {
            printf("Cannot stat %s, aborting...\n", pFilename);
            return -ENOENT;
         }
    } else {
        /* Relative path, search for file */
        char * pPaths = (char *)malloc(strlen(s_searchPaths) + 1);
        if (!pPaths) return -ENOMEM;
        strcpy(pPaths, s_searchPaths);
        char * pTemp;
        char * pPath = strtok_r(pPaths, ":", &pTemp);
        while (pPath != NULL) {
            pFullPath = (char *)malloc(strlen(pPath) + strlen(pFilename) + 2);
            if (!pFullPath) {
                free(pPaths);
                return -ENOMEM;
            }
            strcpy(pFullPath, pPath);
            strcat(pFullPath, "/");
            strcat(pFullPath, pFilename);
            if (stat(pFullPath, pStat) >= 0) break;
            free(pFullPath);
            pPath = strtok_r(NULL, ":", &pTemp);
        }
        free(pPaths);
        if (pPath == NULL) {
            printf("Cannot stat %s, aborting...\n", pFilename);
            return -ENOENT;
        }
    }
    *ppFullPath = pFullPath;
    return 0;
}

static int StatPaths(char * pPaths) {
    char * pPathsTemp = (char *)malloc(strlen(pPaths) + 1);
    if (pPathsTemp == NULL) return -ENOMEM;
    lt_strncpyTerm(pPathsTemp, pPaths, strlen(pPaths) + 1);
    struct stat st;
    char * pTemp;
    char * pPath = strtok_r(pPathsTemp, ":", &pTemp);
    while (pPath != NULL) {
        if (stat(pPath, &st) < 0) {
            printf("Cannot stat %s\n", pPath);
            return -ENOENT;
        }
        pPath = strtok_r(NULL, ":", &pTemp);
    }
    free(pPathsTemp);
    return 0;
}

/* Create an image from a file */
int ImageCreateFromFile(Image * pImage, const char * pFilename) {
    char * pFullPath;
    struct stat stat;
    int nRtn = ImageFindAndStatFile(pFilename, &pFullPath, &stat);
    if (nRtn) return nRtn;
    pImage->pData = NULL;
    off_t nFileSize = stat.st_size;
    pImage->nSize = (u32)nFileSize;
    pImage->pData = (u8 *)malloc(nFileSize);
    if (pImage->pData == NULL) {
        printf("Memory allocation error, aborting...\n");
        free(pFullPath);
        return -ENOMEM;
    }
    memset(pImage->pData, 0xff, nFileSize);
    FILE * pFile = fopen(pFullPath, "rb");
    free(pFullPath);
    if (pFile == NULL) {
        ImageFree(pImage);
        return -1;
    }
    u32 nReadSize = fread(pImage->pData, 1, nFileSize, pFile);
    if (nReadSize != nFileSize) {
        fclose(pFile);
        ImageFree(pImage);
        return -1;
    }
    fclose(pFile);
    return ImageCalcChecksums(pImage);
}

int ImageAppend(Image * pImage, Image * pAppendImage) {
    u8 *pNewData = realloc(pImage->pData, pImage->nSize + pAppendImage->nSize);
    if (!pNewData) return -ENOMEM;
    pImage->pData = pNewData;
    lt_memcpy(pImage->pData + pImage->nSize, pAppendImage->pData, pAppendImage->nSize);
    pImage->nSize += pAppendImage->nSize;
    return ImageCalcChecksums(pImage);
}

int ImageAppendFromBuffer(Image * pImage, u8 * pAppendData, u32 nAppendSize) {
    u8 *pNewData = realloc(pImage->pData, pImage->nSize + nAppendSize);
    if (!pNewData) return -ENOMEM;
    pImage->pData = pNewData;
    lt_memcpy(pImage->pData + pImage->nSize, pAppendData, nAppendSize);
    pImage->nSize += nAppendSize;
    return ImageCalcChecksums(pImage);
}

/* Resize image to a new size (filling with fillByte if larger) */
int ImageResize(Image * pImage, u32 nNewSize, u8 fillByte) {
    if (nNewSize == pImage->nSize) return 0;
    u8 * pNewData = realloc(pImage->pData, nNewSize);
    if (!pNewData) return -ENOMEM;
    pImage->pData = pNewData;
    if (nNewSize > pImage->nSize) {
        lt_memset(pImage->pData + pImage->nSize, fillByte, nNewSize - pImage->nSize);
    }
    pImage->nSize = nNewSize;
    return ImageCalcChecksums(pImage);
}

/* Pad image to end with 0xff */
int ImagePadToEnd(Image * pImage, u32 nAreaIdx) {
    int nRtn = -1;
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        if (pImage->nSize < pArea->nMaxSize) {
            // Resize, pad and recalculate checksums
            u8 * pNewImage = realloc(pImage->pData, pArea->nMaxSize);
            if (pNewImage) {
                pImage->pData = pNewImage;
                memset(pImage->pData + pImage->nSize, 0xff, pArea->nMaxSize - pImage->nSize);
                pImage->nSize = pArea->nMaxSize;
                nRtn = ImageCalcChecksums(pImage);
            } else {
                printf("Memory allocation error, aborting...\n");
                nRtn = -ENOMEM;
            }
        } else if (pImage->nSize == pArea->nMaxSize) {
            nRtn = 0;
        } else {
            printf("Padding an image that is already too big won't work\n");
        }
    }
    return nRtn;
}

int ImagePadToBoundary(Image * pImage, u32 nBoundary) {
    u32 nPaddedSize = nBoundary * ((pImage->nSize + nBoundary - 1) / nBoundary);
    u8 * pNewImage = realloc(pImage->pData, nPaddedSize);
    if (pNewImage) {
        pImage->pData = pNewImage;
        memset(pImage->pData + pImage->nSize, 0xff, nPaddedSize - pImage->nSize);
        pImage->nSize = nPaddedSize;
        return ImageCalcChecksums(pImage);
    } else {
        printf("Memory allocation error, aborting...\n");
        return -ENOMEM;
    }
}

/* Explicit checksum calculation */
int ImageCalcChecksums(Image * pImage) {
    pImage->nChecksum = CalcChecksum((u32 *)pImage->pData, pImage->nSize);
    pImage->nCRC = 0;
    s_utilityByteOps->Crc32(pImage->pData, pImage->nSize, &pImage->nCRC);
    return CalcSHA2Digest(pImage->digest, pImage->pData, pImage->nSize);
}

/* Write an image to a file */
int ImageWriteToFile(Image * pImage, char * pFilename) {
    int nRtn = -1;
    FILE * pFile = fopen(pFilename, "wb");
    if (pFile) {
        u32 nWriteSize = fwrite(pImage->pData, 1, pImage->nSize, pFile);
        if (nWriteSize == pImage->nSize) {
            nRtn = 0;
        }
        fclose(pFile);
    }
    return nRtn;
}

static int SetSearchPath(char * pPaths) {
    int nRtn = 0;
    if (pPaths) {
        lt_strncpyTerm(s_searchPaths, pPaths, sizeof(s_searchPaths));
        nRtn = StatPaths(s_searchPaths);
    } else {
        /* Attempt to build default search path from LT environment */
        char * pPlatformRoot = getenv("LT_PLATFORM_ROOT");
        char * pPlatform     = getenv("LT_PLATFORM");
        char * pProductRoot  = getenv("LT_PRODUCT_ROOT");
        char * pProduct      = getenv("LT_PRODUCT");
        if (pPlatformRoot && pPlatform) {
            /* Create required search path */
            int len = lt_snprintf(s_searchPaths, sizeof(s_searchPaths), "%s/build/platform/%s",
                                     pPlatformRoot, pPlatform);
            if (len < 0 || len >= sizeof(s_searchPaths)) {
                printf("Error creating search paths\n");
                nRtn = -1;
            }
            if (nRtn == 0) {
                /* Check required search path */
                nRtn = StatPaths(s_searchPaths);
                if (nRtn == 0) {
                    if (pProductRoot && pProduct) {
                        len = lt_snprintf(s_searchPaths, sizeof(s_searchPaths),
                                             "%s/build/platform/%s:%s/build/image:%s/build/product/%s:.",
                                             pPlatformRoot, pPlatform, pPlatformRoot, pProductRoot, pProduct);
                    } else {
                        len = lt_snprintf(s_searchPaths, sizeof(s_searchPaths), "%s/build/platform/%s:%s/build/image:.",
                                             pPlatformRoot, pPlatform, pPlatformRoot);
                    }
                    if (len < 0 || len >= sizeof(s_searchPaths)) {
                        printf("Error creating search paths\n");
                        nRtn = -1;
                    }
                }
            }
        } else {
            printf("Warning, LT_PLATFORM_ROOT and LT_PLATFORM not set or -p not specified, using ""%s"" path\n", s_searchPaths);
        }
    }
    return nRtn;
}

/* Release image memory resources */
void ImageFree(Image * pImage) {
    if (pImage->pData) {
        free(pImage->pData);
        pImage->pData = NULL;
    }
}

/* Flash erase helper function */
int ImageNorFlashErase(Image * pImage, Area * pArea, ImageFlashEraseFunc * pEraseSector,
                                                     ImageFlashEraseFunc * pErase32k,
                                                     ImageFlashEraseFunc * pErase64k) {
    enum { k32k = 32768, k64k = 65536 };
    if (!pArea || !pEraseSector) return -1;
    u32 nSectorSize = ImageGetBlockSize();
    u32 nSectors    = pArea->nMaxSize / nSectorSize;
    if (pImage) {
        u32 nImageSectors = (pImage->nSize + nSectorSize - 1) / nSectorSize;
        if (nImageSectors > nSectors) {
            printf("Binary image larger than area, aborting erase...\n");
            return -1;
        }
        nSectors = nImageSectors;
    }
    u32 nOffset           = pArea->nOffset;
    u32 nBytesLeft        = nSectors * nSectorSize;
    u32 nMinBulkEraseSize = 0;
    if (pErase64k) nMinBulkEraseSize = k64k;
    if (pErase32k) nMinBulkEraseSize = k32k;
    while (nBytesLeft > 0) {
        u32 nBytesErased;
        int nRtn;
        if (pErase64k && (nOffset % k64k == 0) && (nBytesLeft >= k64k) ) {
            u32 nBlocks  = nBytesLeft / k64k;
            nBytesErased = nBlocks * k64k;
            nRtn = (pErase64k)(nOffset, nBytesErased, nBlocks);
        } else if (pErase32k && (nOffset % k32k == 0) && (nBytesLeft >= k32k)) {
            u32 nBlocks = nBytesLeft / k32k;
            if (pErase64k && (nBytesLeft >= (k32k + k64k))) {
                nBlocks = 1;
            }
            nBytesErased = nBlocks * k32k;
            nRtn = (pErase32k)(nOffset, nBytesErased, nBlocks);
        } else {
            u32 nBlocks = nBytesLeft / nSectorSize;
            if (nMinBulkEraseSize) {
                u32 nBytesToBoundary = nMinBulkEraseSize - (nOffset & (nMinBulkEraseSize - 1));
                if (nBytesLeft >= nBytesToBoundary + nMinBulkEraseSize) {
                    nBlocks = nBytesToBoundary / nSectorSize;
                }
            }
            nBytesErased = nBlocks * nSectorSize;
            nRtn = (pEraseSector)(nOffset, nBytesErased, nBlocks);
        }
        if (nRtn < 0) return nRtn;
        nOffset    += nBytesErased;
        nBytesLeft -= nBytesErased;
    }
    return 0;
}

/* Initialize Image Library */
int ImageInit(char * pConfigFilename, char * pPaths) {
    s_systemCrypto = lt_openlibrary(LTSystemCrypto);
    s_utilityByteOps = lt_openlibrary(LTUtilityByteOps);
    if (!s_systemCrypto || !s_utilityByteOps) {
        printf("Cannot open required LT libraries\n");
        return -1;
    }
    int nRtn = SetSearchPath(pPaths);
    if (nRtn < 0) return nRtn;
    return ReadConfigFile(pConfigFilename);
}

void ImageFini(void) {
    lt_closelibrary(s_utilityByteOps);
    lt_closelibrary(s_systemCrypto);
}
