/******************************************************************************
 * Builder.c                                                      Image Builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "Image.h"
#include "Builder.h"

/* Eliminate macros that interfere with lt stdlib functions on some platforms: */
#undef memcpy
#undef memset

#include "lt/core/LTCore.h"
#include "lt/device/ota/LTDeviceOta.h"
#include "lt/utility/byteops/LTUtilityByteOps.h"

enum {
    kAuthTagLen          = AES128_GCM_MAX_TAG_LENGTH,
};

static int WriteSHA2DigestToFile(char * pBaseFilename, char * pName, u8 * pDigest) {
    static FILE * pFile = NULL;
    if (pBaseFilename == NULL || pName == NULL || pDigest == NULL) {
        if (pFile != NULL) {
            fclose(pFile);
            pFile = NULL;
        }
        return 0;
    } else if (pFile == NULL) {
        char filename[1000];
        strcpy(filename, pBaseFilename);
        strcat(filename, ".sha256");
        pFile = fopen(filename, "w");
        if (pFile == NULL) {
            return -1;
        }
    }
    for (unsigned nIdx = 0; nIdx < kCryptoSHA2DigestLength; nIdx++) {
        fprintf(pFile, "%02x", pDigest[nIdx]);
    }
    fprintf(pFile, "  %s\n", pName);
    return 0;
}

static int WriteVersionToFile(char * pBaseFilename, char * pName, char * pVersion) {
    static FILE * pFile = NULL;
    if (pBaseFilename == NULL || pName == NULL || pVersion == NULL) {
        if (pFile != NULL) {
            fclose(pFile);
            pFile = NULL;
        }
        return 0;
    } else if (pFile == NULL) {
        char filename[1000];
        strcpy(filename, pBaseFilename);
        strcat(filename, ".version");
        pFile = fopen(filename, "w");
        if (pFile == NULL) {
            return -1;
        }
    }
    fprintf(pFile, "%s %s\n", pName, pVersion);
    return 0;
}

/* Check an image (e.g.: size) */
int BuilderCheckImage(int nAreaIdx, char * pIFilename) {
    int nRtn = -1;
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        char * pFullPath;
        struct stat stat;
        nRtn = ImageFindAndStatFile(pIFilename, &pFullPath, &stat);
        if (nRtn) return nRtn;
        free(pFullPath);
        u32 nSize = (u32)stat.st_size;
        if (nSize <= pArea->nMaxSize) {
            float pctFree = 100.0 * (float)(pArea->nMaxSize - nSize) / (float)pArea->nMaxSize;
            printf("    [%08X] bytes, [%08X] free (%2.1f%%)\n", nSize, pArea->nMaxSize - nSize, pctFree);
        } else {
            printf("    Error, area overflow (0x%08x > 0x%08x)\n", nSize, pArea->nMaxSize);
            return -EFBIG;
        }
    }
    return nRtn;
}

int BuilderCreateFullImage(char * pOFilename) {
    if (pOFilename == NULL) pOFilename = "LTManufacturingFlashImageAll.bin";
    Area * pAreaAll = ImageGetAreaByName("all");
    if (!pAreaAll) return -1;
    Image imageAll;
    printf("Building Flash Image\n");
    if (ImageCreateBlank(&imageAll, pAreaAll->nMaxSize) < 0) return -1;
    unsigned nNumAreas = ImageGetNumAreas();
    
    /* Allocate storage for area digests */
    u8 (*areaDigests)[kCryptoSHA2DigestLength] = malloc((nNumAreas - 1) * kCryptoSHA2DigestLength);
    if (!areaDigests) {
        ImageFree(&imageAll);
        return -1;
    }
    unsigned nDigestCount = 0;
    
    for (unsigned nArea = 1; nArea < nNumAreas; nArea++) {
        Area * pArea = ImageGetArea(nArea);
        assert(pArea);
        /* Skip images without input files */
        if (pArea->filename[0] == '\0') continue;
        printf("\nArea %u: %s [%08X][%08X]\n", pArea->nAreaNum, pArea->name,
                                               pArea->nOffset,  pArea->nMaxSize);
        printf("    File: %s\n", pArea->filename);
        /* Read in an image and check size */
        Image image;
        if (ImageCreateFromFile(&image, pArea->filename) < 0) {
            free(areaDigests);
            ImageFree(&imageAll);
            return -1;
        }
        if (image.nSize > pArea->nMaxSize) {
            printf("    Error, area overflow (0x%08x > 0x%08x)\n", image.nSize, pArea->nMaxSize);
            ImageFree(&image);
            free(areaDigests);
            ImageFree(&imageAll);
            return -EFBIG;
        }
        float pctFree = 100.0 * (float)(pArea->nMaxSize - image.nSize) / (float)pArea->nMaxSize;
        printf("    [%08X] bytes, [%08X] free (%2.1f%%)\n", image.nSize, pArea->nMaxSize - image.nSize, pctFree);
        /* Copy image into all */
        lt_memcpy(imageAll.pData + pArea->nOffset, image.pData, image.nSize);
        
        /* Store digest for later writing */
        lt_memcpy(areaDigests[nDigestCount], image.digest, kCryptoSHA2DigestLength);
        nDigestCount++;
        
        ImageFree(&image);
        printf("    Done\n");
    }
    printf("\nArea all: [%08X][%08X]\n", 0, imageAll.nSize);
    if (ImageWriteToFile(&imageAll, pOFilename) < 0) {
        printf("Write Failed\n");
        free(areaDigests);
        ImageFree(&imageAll);
        return -1;
    }
    printf("Writing [%08X] bytes\n", imageAll.nSize);
    if (ImageCalcChecksums(&imageAll) < 0) {
        free(areaDigests);
        ImageFree(&imageAll);
        return -1;
    }
    
    /* Extract filename from full path */
    char *pFilename = strrchr(pOFilename, '/');
    if (pFilename == NULL) {
        pFilename = pOFilename; /* No path separator found, use as is */
    } else {
        pFilename++; /* Skip the '/' character */
    }
    
    /* Write summary digest first */
    if (WriteSHA2DigestToFile(pOFilename, pFilename, imageAll.digest) < 0) {
        free(areaDigests);
        ImageFree(&imageAll);
        return -1;
    }
    
    /* Now write individual area digests */
    unsigned nDigestIdx = 0;
    for (unsigned nArea = 1; nArea < nNumAreas; nArea++) {
        Area * pArea = ImageGetArea(nArea);
        assert(pArea);
        /* Skip images without input files */
        if (pArea->filename[0] == '\0') continue;
        
        if (WriteSHA2DigestToFile(pOFilename, pArea->name, areaDigests[nDigestIdx]) < 0) {
            free(areaDigests);
            ImageFree(&imageAll);
            return -1;
        }
        nDigestIdx++;
    }
    
    free(areaDigests);
    ImageFree(&imageAll);
    /* Finalize digest file */
    if (WriteSHA2DigestToFile(NULL, NULL, NULL) < 0) {
        return -1;
    }
    return 0;
}

static bool
LoadRootFirmwareEncryptionKey(const char *filename, u8 rfek[AES128_KEY_LENGTH], u8 seed[kLTDeviceOta_ImageHeader_SeedLen]) {
    bool bResult = false;
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open key file: %s\n", filename);
        return false;
    }
    do {
        if (fread(seed, 1, kLTDeviceOta_ImageHeader_SeedLen, file) != kLTDeviceOta_ImageHeader_SeedLen) break;
        if (fread(rfek, 1, AES128_KEY_LENGTH,                   file) != AES128_KEY_LENGTH)                   break;
        bResult = true;
    } while(0);
    fclose(file);
    return bResult;
}

static bool
DeriveSecret(LTSystemCrypto *pCrypto, const u8 key[AES128_KEY_LENGTH], u8 *secret, u32 secretLen, const char *prefix, u8 *data, u32 dataLen) {
    bool bResult = false;
    LT_HMAC_SHA256_CTX *hmacCtx = NULL;
    u8 hmacBuf[SHA256_HASH_LENGTH];
    do {
        hmacCtx = pCrypto->CreateSeqHMACSHA256(key, AES128_KEY_LENGTH);
        if (!hmacCtx) break;
        if (prefix) {
            if (pCrypto->UpdateSeqHMACSHA256(hmacCtx, (u8*)prefix, lt_strlen(prefix)) != kLTSystemCrypto_Result_Ok) break;
        }
        if (pCrypto->UpdateSeqHMACSHA256(hmacCtx, data, dataLen) != kLTSystemCrypto_Result_Ok) break;
        if (pCrypto->FinishSeqHMACSHA256(hmacCtx, hmacBuf) != kLTSystemCrypto_Result_Ok) break;
        lt_memcpy(secret, hmacBuf, secretLen);
        bResult = true;
    } while(0);
    pCrypto->DestroySeqHMACSHA256(hmacCtx);

    return bResult;
}

static bool
DeriveEncryptionParams(LTSystemCrypto *pCrypto, u8 rfek[AES128_KEY_LENGTH], u8 nonce[kLTDeviceOta_ImageHeader_NonceLen], u8 fek[AES128_KEY_LENGTH], u8 initialIv[AES128_GCM_IV_LENGTH]) {
    if (!DeriveSecret(pCrypto, rfek, fek,       AES128_KEY_LENGTH,    "fek", nonce, kLTDeviceOta_ImageHeader_NonceLen)) return false;
    if (!DeriveSecret(pCrypto, rfek, initialIv, AES128_GCM_IV_LENGTH, "fiv", nonce, kLTDeviceOta_ImageHeader_NonceLen)) return false;
    return true;
}

static void
MakeBlockIv(u32 blockIdx, u8 initialIv[AES128_GCM_IV_LENGTH], u8 blockIv[AES128_GCM_IV_LENGTH]) {
    lt_memcpy(blockIv, initialIv, AES128_GCM_IV_LENGTH);
    blockIv[0] ^= ((blockIdx >> 0) & 0xFF);
    blockIv[1] ^= ((blockIdx >> 8) & 0xFF);
    blockIv[2] ^= ((blockIdx >> 16) & 0xFF);
    blockIv[3] ^= ((blockIdx >> 24) & 0xFF);
}

int BuilderCreateUpdateImage(char       *pOFilename,
                             char       *pIFilename,
                             char       *pKeyFilename,
                             char       *pVersion,
                             const char *pPlatformArgs,
                             u32         plaintextBlockSize) {
    if (pOFilename == NULL) pOFilename = "LTSystemUpdateImage.bin";
    printf("Building System Update Image\n");
    int ret = -1;
    Image imageFirmware = { 0 };
    Image imageUpdate   = { 0 };
    LTSystemCrypto *pCrypto = NULL;
    do {
        if (ImageCreateFromFile(&imageFirmware, pIFilename) < 0) break;

        if (pPlatformArgs && pPlatformArgs[0] != '\0') {
            if (strcmp(pPlatformArgs, "rcu") == 0) {
                typedef struct __attribute__((packed)) {
                    u32  magic_number;              /* "OTA", last byte is protocol version number */
                    u32  payload_size;              /* total length of payload portion */
                    char logical_partition_name[4]; /* e.g.:  "fw" or "rdat" */
                    char version[14];               /* e.g.:  "rtn.3000" */
                    char platform_id;               /* TBD */
                    char security_version;          /* 0 - unsecured, 1 - DVT2, 2 - MP */
                    u8   sha256_digest[kCryptoSHA2DigestLength];
                } OTAHeader;

                enum {
                    kRCU_Magic_OTA = 0x0041544F,
                };

                LT_SIZE updateImageSize = sizeof(OTAHeader) + imageFirmware.nSize;
                if (ImageCreateBlank(&imageUpdate, updateImageSize) < 0) break;

                OTAHeader *header    = (OTAHeader *)imageUpdate.pData;
                header->magic_number = kRCU_Magic_OTA;
                header->payload_size = imageFirmware.nSize;
                lt_strncpyTerm(header->logical_partition_name, "fw", sizeof(header->logical_partition_name));
                lt_strncpyTerm(header->version, pVersion, sizeof(header->version));
                lt_memcpy(header->sha256_digest, imageFirmware.digest, kCryptoSHA2DigestLength);
                lt_memcpy(imageUpdate.pData + sizeof(OTAHeader), imageFirmware.pData, imageFirmware.nSize);
            } else {
                printf("Bad argument -x\n");
                return -EINVAL;
            }
        } else {
            // clang-format off

            if (plaintextBlockSize == 0) plaintextBlockSize = 4096;
            u32 ciphertextBlockSize = plaintextBlockSize + kAuthTagLen;
            u32 blockCount = ((imageFirmware.nSize + (plaintextBlockSize - 1)) & ~(plaintextBlockSize - 1)) / plaintextBlockSize;
            LT_SIZE updateImageSize = sizeof(LTDeviceOta_ImageHeader) + imageFirmware.nSize  + (kAuthTagLen * blockCount);
            // clang-format on
            if (ImageCreateBlank(&imageUpdate, updateImageSize) < 0) break;

            pCrypto = lt_openlibrary(LTSystemCrypto);
            if (!pCrypto) break;

            u8 seed[4];
            u8 rfek[AES128_KEY_LENGTH];
            if (!LoadRootFirmwareEncryptionKey(pKeyFilename, rfek, seed)) break;

            LTDeviceOta_ImageHeader *header = (LTDeviceOta_ImageHeader *)imageUpdate.pData;
            header->magic                   = kLTDeviceOta_ImageHeader_Magic;
            lt_memcpy(header->seed, seed, sizeof(header->seed));
            pCrypto->GenRandomBytes(header->nonce, sizeof(header->nonce));

            lt_strncpyTerm(header->encrypted.version, pVersion, sizeof(header->encrypted.version));
            header->encrypted.blockCount    = blockCount;
            header->encrypted.blockSize     = ciphertextBlockSize;
            header->encrypted.plaintextSize = imageFirmware.nSize;
            lt_memcpy(header->encrypted.plaintextHash, imageFirmware.digest, kCryptoSHA2DigestLength);

            u8 fek[AES128_KEY_LENGTH];
            u8 initialIv[AES128_GCM_IV_LENGTH];
            if (!DeriveEncryptionParams(pCrypto, rfek, header->nonce, fek, initialIv)) break;
            pCrypto->EncryptAES128GCM(fek,
                                      initialIv,
                                      NULL,
                                      0,
                                      (u8 *)&header->encrypted,
                                      sizeof(header->encrypted),
                                      (u8 *)&header->encrypted,
                                      header->authTag,
                                      sizeof(header->authTag));

            u8     *plaintextData      = imageFirmware.pData;
            LT_SIZE plaintextRemaining = imageFirmware.nSize;
            u8     *ciphertextData     = imageUpdate.pData + sizeof(LTDeviceOta_ImageHeader);
            for (u32 blockIdx = 0; blockIdx < blockCount; ++blockIdx) {
                LT_ASSERT(plaintextRemaining > 0);

                LT_SIZE blockSize = (plaintextRemaining < plaintextBlockSize) ? plaintextRemaining
                                                                              : plaintextBlockSize;
                u8     *authTag   = ciphertextData + blockSize;
                u8      blockIv[AES128_GCM_IV_LENGTH];
                MakeBlockIv(blockIdx + 1, initialIv, blockIv);
                pCrypto->EncryptAES128GCM(fek,
                                          blockIv,
                                          NULL,
                                          0,
                                          plaintextData,
                                          blockSize,
                                          ciphertextData,
                                          authTag,
                                          kAuthTagLen);

                ciphertextData     += blockSize + kAuthTagLen;
                plaintextData      += blockSize;
                plaintextRemaining -= blockSize;
            }
        }

        printf("Writing system update image to %s\n", pOFilename);
        if (ImageWriteToFile(&imageUpdate, pOFilename) < 0) {
            printf("Image Write Failed\n");
            break;
        }
        printf("Writing system update image version to %s.version\n", pOFilename);
        if (WriteVersionToFile(pOFilename, pOFilename, pVersion) < 0) {
            printf("Version Write Failed\n");
            break;
        }
        ret = 0;
    } while (0);
    if (pCrypto) lt_closelibrary(pCrypto);
    ImageFree(&imageFirmware);
    ImageFree(&imageUpdate);
    return ret;
}

int BuilderCreatePartitionTable(char * pOFilename) {
    u32 nMaxAreaSize;
    int nArea = ImageGetNextAreaIndexByType(kImageType_PartitionTable, 0);
    if (nArea > 0) {
        Area * pArea = ImageGetArea(nArea);
        assert(pArea);
        nMaxAreaSize = pArea->nMaxSize;
        if (!pOFilename) pOFilename = pArea->filename;
    } else {
        printf("Partition table not defined, assuming size is 1 sector\n");
        nMaxAreaSize = ImageGetBlockSize();
    }
    unsigned nNumAreas = ImageGetNumAreas();
    /* Image size is header, table entries and CRC */
    u32 nImageSize = sizeof(LTDeviceFlash_PartitionTableHeader);
    nImageSize += (nNumAreas - 1) * sizeof(LTDeviceFlash_PartitionEntry);
    nImageSize += sizeof(u32);
    if (nImageSize > nMaxAreaSize) {
        printf("Partition table does not fit in provided area\n");
        return -1;
    }
    Image image;
    int nRtn = ImageCreateBlank(&image, nImageSize);
    if (nRtn < 0) return nRtn;
    LTDeviceFlash_PartitionTableHeader * pHeader = (LTDeviceFlash_PartitionTableHeader *)image.pData;
    pHeader->nMagic = kLTDeviceFlash_Magic_PartitionTable;
    /* Do not include "all" area */
    pHeader->nNumPartitions = nNumAreas - 1;
    /* Size of all area = 2^(nDeviceSize + 6) bytes */
    pHeader->nDeviceSize = 25 - __builtin_clz(ImageGetArea(0)->nMaxSize);
    pHeader->nTableVersion = ImageGetTableVersionNumber();
    lt_memset(pHeader->nRsvd, 0, sizeof(pHeader->nRsvd));
    LTDeviceFlash_PartitionEntry * pEntry = (LTDeviceFlash_PartitionEntry *)(pHeader + 1);
    for (unsigned nArea = 1; nArea < nNumAreas; nArea++) {
        Area * pArea = ImageGetArea(nArea);
        assert(pArea);
        lt_strncpyTerm(pEntry[nArea - 1].name, pArea->name, sizeof(pEntry[nArea].name));
        lt_strncpyTerm(pEntry[nArea - 1].type, pArea->type, sizeof(pEntry[nArea].type));
        pEntry[nArea - 1].nByteOffset = pArea->nOffset;
        pEntry[nArea - 1].nNumSectors = pArea->nMaxSize / ImageGetBlockSize();
        pEntry[nArea - 1].flags       = pArea->flags;
    }
    u32 nCRC = 0;
    LTUtilityByteOps * utilityByteOps = lt_openlibrary(LTUtilityByteOps);
    utilityByteOps->Crc32(pHeader, nImageSize - sizeof(u32), &nCRC);
    lt_closelibrary(utilityByteOps);
    /* TODO: write nCRC in byte ordering of target architecture */
    *((u32 *)&image.pData[nImageSize - sizeof(u32)]) = nCRC;
    if (pOFilename == NULL || pOFilename[0] == '\0') {
        pOFilename = "LTPartitionTable.bin";
    }
    printf("Writing partition table to %s\n", pOFilename);
    if (ImageWriteToFile(&image, pOFilename) < 0) {
        printf("Write Failed\n");
        ImageFree(&image);
        return -1;
    }
    ImageFree(&image);
    return 0;
}

int BuilderCreateOTAData(char * pOFilename, bool bIsInitial, bool bIsGood) {
    PingPongOta_PartitionInformation otainfo = {
        .nMagic = kPingPongOta_UpdatePartition_Magic,
        .nCount = 0,
        .ver    = "000.10E00000A",
        .type   = kPingPongOtaPartitionType_Customer,
        .rsvd   = { 0, 0, 0 },
        .nCRC   = 0
    };
    if (pOFilename == NULL || pOFilename[0] == '\0') {
        pOFilename = "LTSystemUpdateDataInitial.bin";
    }
    if (bIsInitial) otainfo.type = kPingPongOtaPartitionType_Manufacturing;
    LTUtilityByteOps * utilityByteOps = lt_openlibrary(LTUtilityByteOps);
    utilityByteOps->Crc32((u8 *)&otainfo, LT_OFFSET_OF(PingPongOta_PartitionInformation, nCRC), &otainfo.nCRC);
    lt_closelibrary(utilityByteOps);
    Image image;
    if (ImageCreateBlank(&image, 2 * ImageGetBlockSize()) < 0) return -1;
    lt_memcpy(image.pData, &otainfo, sizeof(PingPongOta_PartitionInformation));
    if (bIsGood) {
        PingPongOtaPartitionStatus * pStatus = (PingPongOtaPartitionStatus *)(image.pData + kPingPongOta_StatusOffset);
        *pStatus = kPingPongOtaPartitionStatus_Good;
    }
    printf("Writing ota data to %s\n", pOFilename);
    if (ImageWriteToFile(&image, pOFilename) < 0) {
        printf("Write Failed\n");
        ImageFree(&image);
        return -1;
    }
    ImageFree(&image);
    return 0;
}

