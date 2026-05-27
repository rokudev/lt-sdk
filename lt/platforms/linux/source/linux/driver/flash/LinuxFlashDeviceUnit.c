/*******************************************************************************
 * platforms/linux/source/linux/driver/flash/LinuxFlashDeviceUnit.c
 *
 * This driver code provides Flash-like functionality to LT. The read,
 * write, erase-chip and erase-sector are provided with the goal of storing
 * data, so behaviours of physical flash such as delays and writes behaving like
 * NOR are not replicated. I.e. Writing 0xFF to a 0x00 location will work
 * without erase first.
 *
 * This driver is designed for use cases where the full flash representation
 * in raw binary format is not needed and physical storage is limited.
 *
 * Due to compression, this driver requires heap memory for plain and compression
 * buffers. The memory requirement is 3 times the virtual sector size. It can be
 * reduced by reducing the virtual sector size at the cost of more file system
 * entries.
 *
 * Sparseness: Only non-empty flash sectors are stored. One sector per file at
 * file system location specified by LT_FLASH_PATH env variable.
 *
 * Run-length encoding is implemented to conserve storage space as sectors will
 * be mostly filled with 0xFF (specified by macro EMTPY_BYTE). This can be turned
 * off via preprocessor macro "SF_PERFORM_RLE".
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#include <lt/device/flash/LTDeviceFlash.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include "LinuxFlashDeviceUnit.h"

#if !defined(SF_PERFORM_RLE)
#define SF_PERFORM_RLE                         (true)
#endif

#if !defined(SF_SUPPORT_ENCRYPTION)
#define SF_SUPPORT_ENCRYPTION                         (true)
#endif

#if SF_SUPPORT_ENCRYPTION
#define FLASH_KEY_NONCE_LENGTH                 (16)
#endif

DEFINE_LTLOG_SECTION("linux.flash");

#define USE_DLOG    0
#if USE_DLOG
  #define DLOG LTLOG_DEBUG
#else
  #define DLOG LTLOG_LOGNULL
#endif

enum {
    kPartByteOffset = 0x1000,       /**< LT Partition table offset */
    kFw0ByteOffset  = 0xA000,       /**< LT Partition table offset */
    kWriteQuantum   = 1,            /**< No constraint */
    VIRTUAL_SECTOR_COUNT = 512,     /**< Number of sectors in this virtual flash */
    VIRTUAL_SECTOR_SIZE = 4096,     /**< Number of sectors in this virtual flash */
    INVALID_SECTOR  = 0xFFFFFFFF,   /**< Invalid valur for sector number */
    EMTPY_BYTE      = 0xFF,         /**< Value of a byte after erase */
};

/*_______________________________________________
 / file LinuxFlashDeviceUnit.c private types */
typedef struct {
    /* NumSectors and SectorSize are constant after initialization */
    u32 nNumSectors;
    u32 nSectorSize;
    /* Mutex required for write protect and reference counts */
    LTMutex *mutex;
    u32 nRefCount;
} FlashInfo;

#if SF_SUPPORT_ENCRYPTION
typedef union {
    struct {
        u8 key1[AES128_KEY_LENGTH];
        u8 key2[AES128_KEY_LENGTH];
    };
    u8 S256[SHA256_HASH_LENGTH];
} EncryptionKey;

typedef struct {
    LTDriverCryptoAes128Xts *aesXts;
    EncryptionKey eKey;
} CryptoCTX;
#endif //SF_SUPPORT_ENCRYPTION


/*________________________________________________
 / file LinuxFlashDeviceUnit.c static variables */

static const LTDeviceFlash_PartitionEntry s_partitions[] = {
    { "ltat",      "ltat",     0x000000,        0x01,  0 },
    { "part",      "part",     kPartByteOffset, 0x01,  0 },
    { "ota",       "otainfo",  0x002000,        0x02,  0 },
    { "settings",  "settings", 0x004000,        0x06,  0 },
    { "fw0",       "fw",       kFw0ByteOffset,  0x01,  0 }, // dummy firmware partitions to allow IotServiceUpdate to load
    { "fw1",       "fw",       0x00B000,        0x01,  0 },
    { "crashdump", "misc",     0x00C000,        0x20,  0 },
    { "log",       "log",      0x02C000,        0x0C,  0 },
    { "migration", "misc",     0x038000,        0x0F,  0 },
    { "crypto",    "settings", 0x047000,        0x04,  0 },
    { "prov",      "misc",     0x04B000,        0x01,  0 },
};

static const char *s_DefaultFlashPath = "/tmp/ltflash";

static const char * s_pFlashPath = NULL;
static FlashInfo s_flashInfo;
static u32 s_SectorBufferSectorNum = INVALID_SECTOR;
static u8 * s_SectorBuffer = NULL;
static u32 s_SectorBufferSize;

static bool WriteToFile(const char *filename, const u8 *buffer, LT_SIZE length) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
    if (fd < 0) {
        LTLOG_REDALERT("open.w.create", "Failed to create file %s", filename);
        return false;
    }

    int writeCount = write(fd, buffer, length);
    fsync(fd);
    close(fd);
    if ((u32)writeCount == length) return true;

    LTLOG_REDALERT("file.wr", "%s: write returned unexpected value. Expected %u, returned %d", filename, FLASH_KEY_NONCE_LENGTH, writeCount);
    remove(filename);
    return false;
}

#if SF_PERFORM_RLE
// Buffer for RLE compressed data. Size needs to be at least 150% of uncompressed buffer. 2X for simplicity.
static u8 * s_RLEBuffer = NULL;

static u32 RLE_Compress(u8 *pSrc, u8 *pDst, u32 srcLength) {
    // Compressed segment format: [escape 0x1B (byte), length (byte), repeated value].
    // compressed segment length 0 and 1 are special values. 0 -> decompresses to 0x1B. 1-> decompresses to [0x1B, 0x1B].
    // In case of compressed segment length = 0 or 1, no repeated value byte follows, such compressed segment is only 2 bytes long.
    u32 compressedSize = 0;
    u8 rCount = 0;
    u8 escape = 0x1B;
    u8 prev = EMTPY_BYTE;

    if (!srcLength) return 0;

    // Loop runs 1 last time after all data is processed to flush the count.
    for (u32 idx = 0; (idx < srcLength + 1); idx++) {
        if (idx == 0) {
            prev = pSrc[idx];
            rCount = 1;
            continue;
        }
        if (idx < srcLength && pSrc[idx] == prev && rCount < 0xFF) {
            rCount++;
        } else {
            if (rCount >= 3) {
                pDst[compressedSize++] = escape;
                pDst[compressedSize++] = rCount;
                pDst[compressedSize++] = prev;
            } else {
                while (rCount--) {
                    if (prev == escape) {
                        pDst[compressedSize++] = escape;
                        pDst[compressedSize++] = rCount; // 0 = 1x 0x1B, 1 = 2x 0x1B
                        break;
                    } else {
                        pDst[compressedSize++] = prev;
                    }
                }
            }
            if (idx >= srcLength) break;
            prev = pSrc[idx];
            rCount = 1;
        }
    }
    return compressedSize;
}

static u32 RLE_Decompress(u8 *pSrc, u8 *pDst, u32 srcLength, u32 dstLength) {
    u8 rCount = 0;
    u8 escape = 0x1B;
    u32 decompressed_size = 0;
    for (u32 idx = 0; idx < srcLength; idx++) {
        if (pSrc[idx] == escape) {
            if (idx + 2 >= srcLength) return 0; // Overflow
            idx++;
            rCount = pSrc[idx];
            if (rCount >= 3) {
                if (decompressed_size + rCount > dstLength) return 0; // Overflow
                idx++;
                while (rCount--) pDst[decompressed_size++] = pSrc[idx];
            } else {
                if (decompressed_size + 2 > dstLength) return 0; // Overflow
                pDst[decompressed_size++] = escape;
                if (rCount == 1) pDst[decompressed_size++] = escape;
            }
        } else {
            if (decompressed_size + 1 > dstLength) return 0; // Overflow
            pDst[decompressed_size++] = pSrc[idx];
        }
    }
    return decompressed_size;
}
#endif // SF_PERFORM_RLE

#if SF_SUPPORT_ENCRYPTION
static CryptoCTX *s_pCryptCTX;

static char *MakeFullPath(char *filename) {
    LT_SIZE fullLen = lt_strlen(s_pFlashPath) + lt_strlen(filename) + 2; // 1 slash, 1 EOL
    char *fullPath = lt_malloc(fullLen);
    if (!fullPath) return NULL;
    lt_snprintf(fullPath, fullLen, "%s/%s", s_pFlashPath, filename);
    return fullPath;
}

static char *MakeSectorFullPath(u32 sectorNum) {
    LT_SIZE fullLen = lt_strlen(s_pFlashPath) + 16 + 2; // For sake of simplicity, allow 16 characters for <sector num>.bin
    char *fullPath = lt_malloc(fullLen);
    if (!fullPath) return NULL;
    lt_snprintf(fullPath, fullLen, "%s/%u.sector", s_pFlashPath, sectorNum);
    return fullPath;
}

static bool GetEncryptionNonce(u8 *nonce) {
    bool ret = false;
    char *nonceFilePath = MakeFullPath("nonce.bin");
    if (nonceFilePath) {
        do {
            int nf = open(nonceFilePath, O_RDONLY);
            if (nf >= 0) {
                int readCount = read(nf, nonce, FLASH_KEY_NONCE_LENGTH);
                close(nf);

                if ((u32)readCount == FLASH_KEY_NONCE_LENGTH) {
                    ret = true;
                    break;
                }
                LTLOG_REDALERT("nonce.read.fail", "read returned unexpected value. Expected %u, returned %d", FLASH_KEY_NONCE_LENGTH, readCount);
            }

            // Generate a new nonce
            LTDriverCryptoRandom *random = lt_createobject_typed(LTDriverCryptoRandom, LTSoftwareCryptoRandom);
            if (!random) break;
            ret = random->API->GenRandomBytes(nonce, FLASH_KEY_NONCE_LENGTH);
            lt_destroyobject(random);
            if (!ret) {
                LTLOG_REDALERT("nonce.genfailed", "Failed to generate nonce");
                break;
            }

            // Save nonce to file
            ret = WriteToFile(nonceFilePath, nonce, FLASH_KEY_NONCE_LENGTH);
            if (!ret) {
                LTLOG_REDALERT("nonce.w.failed", "Failed to store nonce");
            }
        } while (0);
        lt_free(nonceFilePath);
    }
    return ret;
}

static bool GenerateEncryptionKey(CryptoCTX *ctx) {
    bool ret = false;
    u8 nonce[FLASH_KEY_NONCE_LENGTH];
    do {
        if (!GetEncryptionNonce(nonce)) break;
        LTDriverCrypto_KeyReference aesKey1 = {
            .secureKeyType = kLTDriverCrypto_SecureKeyType_Provision,
            .keyType = kLTDriverCrypto_KeyType_Key128,
            .provisionId = kLTDriverCrypto_ProvisionId_AesKey1
        };
        LTSecureCryptoHmacSha256 *hmacCrypto = lt_createobject(LTSecureCryptoHmacSha256);
        if (!hmacCrypto) break;
        LTSystemCryptoResult hmacRet = hmacCrypto->API->GenHmac(&aesKey1, nonce, FLASH_KEY_NONCE_LENGTH, ctx->eKey.S256);
        lt_destroyobject(hmacCrypto);
        if (hmacRet == kLTSystemCrypto_Result_Ok) {
            ret = true;
        } else {
            LTLOG_REDALERT("hmacsha256.err", "HMACS256 Failed with code %x", hmacRet);
        }
    } while (0);

    lt_memset(nonce, 0, FLASH_KEY_NONCE_LENGTH);
    return ret;
}

static void DeInitCryptoCTX(CryptoCTX *ctx) {
    if (ctx) {
        lt_destroyobject(ctx->aesXts);
        lt_memset(ctx, 0, sizeof(CryptoCTX));
        lt_free(ctx);
    }
}

static bool InitCryptoCTX(CryptoCTX **ctx) {
    *ctx = NULL;

    CryptoCTX *pCTX = (CryptoCTX *)lt_malloc(sizeof(CryptoCTX));

    if (!pCTX) return false;
    pCTX->aesXts = lt_createobject_typed(LTDriverCryptoAes128Xts, LTSoftwareCryptoAes128Xts);
    if (!pCTX->aesXts) {
        LTLOG_REDALERT("aesxts.open.err", NULL);
        DeInitCryptoCTX(pCTX);
        return false;
    }

    // Generate key1 and key2
    if (GenerateEncryptionKey(pCTX)) {
        *ctx = pCTX;
        return true;
    } else {
        LTLOG_REDALERT("crypto.genkey.err", "Failed to generate keys");
    }

    DeInitCryptoCTX(pCTX);
    return false;
}

static bool EncryptDecyptBuffer(CryptoCTX *ctx,
                                bool encrypt,
                                u32 sectorNum,
                                u8 *pData,
                                LT_SIZE length) {
    LTSystemCryptoResult ret;
    u32 iv[AES128_XTS_IV_LENGTH / 4];
    // AES-XTS-PLAIN64 uses sector number in lower 64 bits of tweak
    // Upper 64 bits are zero'd
    lt_memset(iv, 0, AES128_XTS_IV_LENGTH);
    iv[0] = LT_LE32(sectorNum);

    if (encrypt){
        ret = ctx->aesXts->API->Encrypt(ctx->eKey.key1, ctx->eKey.key2, (u8*)iv, pData, length, pData);
    } else {
        ret = ctx->aesXts->API->Decrypt(ctx->eKey.key1, ctx->eKey.key2, (u8*)iv, pData, length, pData);
    }

    if (ret != kLTSystemCrypto_Result_Ok) {
        LTLOG_REDALERT("enc.failed", "Encryption failed with code 0x%X", ret);
        return false;
    }
    return true;
}

static bool EncryptBuffer(  CryptoCTX *ctx,
                            u32 sectorNum,
                            u8 *pData,
                            LT_SIZE length) {
    return EncryptDecyptBuffer(ctx, true, sectorNum, pData, length);
}

static bool DecryptBuffer(  CryptoCTX *ctx,
                            u32 sectorNum,
                            u8 *pData,
                            LT_SIZE length) {
    return EncryptDecyptBuffer(ctx, false, sectorNum, pData, length);
}
#endif

static void sReadSecFileToBuffer(u32 nSectorNumber) {
    /* find sector in file.*/
    bool readSuccess = false;
    u8 * p_readBuffer = s_SectorBuffer;
    u32 readSize = s_SectorBufferSize;
#if SF_PERFORM_RLE
    p_readBuffer = s_RLEBuffer;
    readSize = s_SectorBufferSize * 2;
#endif /* SF_PERFORM_RLE */

    /* nSectorNumber is just used for filename. Validation not necessary. */
    s_SectorBufferSectorNum = nSectorNumber;

    char *fullFilePath = MakeSectorFullPath(nSectorNumber);
    if (fullFilePath) {
        int sf = open(fullFilePath, O_RDONLY);
        if (sf >= 0) {
            int readRes = read(sf, p_readBuffer, readSize);

            if (readRes < 0 || readRes > (int)readSize) {
                LTLOG_REDALERT("read.failed", "File read failed");
            } else {
                u32 readCount = (u32)readRes;
#if SF_SUPPORT_ENCRYPTION
                // Note: XTS requires plain text of at least 1 AES block long. i.e. 16 bytes.
                // Even the most compressed and most sparse of 4K sector will satisfy this requirement. (At least 48 bytes)
                // Care must be taken to ensure this requirement is met after future architectural changes.
                DecryptBuffer(s_pCryptCTX, s_SectorBufferSectorNum, p_readBuffer, readCount);
#endif //SF_SUPPORT_ENCRYPTION

#if SF_PERFORM_RLE
                readCount = RLE_Decompress(s_RLEBuffer, s_SectorBuffer, readCount, s_SectorBufferSize);
#endif //SF_PERFORM_RLE
                if (readCount == s_SectorBufferSize) {
                    readSuccess = true;
                } else {
                    LTLOG_REDALERT("read.invfile", "Unexpected read size. Expected %u, read %u bytes", s_SectorBufferSize, readCount);
                }
            }
            close(sf);
        }
        // else sf < 0, assume file does not exist - Sector empty
        if (!readSuccess)  {
            lt_memset(s_SectorBuffer, EMTPY_BYTE, s_SectorBufferSize);
            remove(fullFilePath);
        }
        lt_free(fullFilePath);
    } else {
        LTLOG_REDALERT("rd.fn.err", "Failed to create file path");
    }
}

static bool sWriteSecBuffertoFile(void) {
    //find sector in file.

    u8 * p_writeBuffer = s_SectorBuffer;
    u32 writeSize = s_SectorBufferSize;

    bool isBlank = true;
    for (u32 i = 0; i < s_SectorBufferSize; i++) {
        if (s_SectorBuffer[i] != EMTPY_BYTE) {
            isBlank = false;
            break;
        }
    }

    char *fullFilePath = MakeSectorFullPath(s_SectorBufferSectorNum);
    bool ret = false;
    if (fullFilePath) {
        if (isBlank) {
            // Blank sector, treat as erased.
            remove(fullFilePath);
            ret = true;
        } else {
#if SF_PERFORM_RLE
            p_writeBuffer = s_RLEBuffer;
            writeSize = RLE_Compress(s_SectorBuffer, s_RLEBuffer, s_SectorBufferSize);
#endif // SF_PERFORM_RLE

#if SF_SUPPORT_ENCRYPTION
            // Note: XTS requires plain text of at least 1 AES block long. i.e. 16 bytes.
            // Even the most compressed and most sparse of 4K sector will satisfy this requirement. (At least 48 bytes)
            // Care must be taken to ensure this requirement is met after future architectural changes.
            EncryptBuffer(s_pCryptCTX, s_SectorBufferSectorNum, p_writeBuffer, writeSize);
#endif //SF_SUPPORT_ENCRYPTION

            ret = WriteToFile(fullFilePath, p_writeBuffer, writeSize);
        }
        lt_free(fullFilePath);
    } else {
        LTLOG_REDALERT("rd.fn.err", "Failed to create file path");
    }
    return ret;
}

static FlashInfo * GetFlashInfoPtr(LTDeviceUnit hFlashDevice) {
    return hFlashDevice ? &s_flashInfo : NULL;
}

void LinuxFlashDeviceUnit_Initialize(void) {
    s_flashInfo.mutex = lt_createobject(LTMutex);
    s_flashInfo.nNumSectors = VIRTUAL_SECTOR_COUNT;
    s_flashInfo.nSectorSize = VIRTUAL_SECTOR_SIZE;
    s_flashInfo.nRefCount = 0;

    s_pFlashPath = getenv("LT_FLASH_PATH");
    if (!s_pFlashPath) s_pFlashPath = s_DefaultFlashPath;

    /* Create Flash directory if it does not exist */
    struct stat st;
    if (stat(s_pFlashPath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        LTLOG("create", "Creating flash path: %s", s_pFlashPath);
        // To keep the code simple, only try to create to top level directory.
        // If the entire path needs to be created, please do so manually.
        // or ensure the directory exists in skeleton fs.
        if (mkdir(s_pFlashPath, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
            LTLOG_REDALERT("nopath", "Cannot create flash path: %s", s_pFlashPath);
            LT_GetCore()->DebugBreak();
        }
    }

    // Sector buffer
    s_SectorBuffer = lt_malloc(s_flashInfo.nSectorSize);
    if (!s_SectorBuffer) LT_GetCore()->DebugBreak();
    s_SectorBufferSize = s_flashInfo.nSectorSize;

#if SF_PERFORM_RLE
    // Run-length encoding buffer
    s_RLEBuffer = lt_malloc(s_SectorBufferSize * 2);
    if (!s_RLEBuffer) LT_GetCore()->DebugBreak();
#endif //SF_PERFORM_RLE

#if SF_SUPPORT_ENCRYPTION
    if (!InitCryptoCTX(&s_pCryptCTX)) LT_GetCore()->DebugBreak();
#endif
}

void LinuxFlashDeviceUnit_Finalize() {
    lt_destroyobject(s_flashInfo.mutex);
    s_flashInfo.mutex = NULL;
    if (s_SectorBuffer) {
        lt_free(s_SectorBuffer);
        s_SectorBuffer = NULL;
    }
    if (s_RLEBuffer) {
        lt_free(s_RLEBuffer);
        s_RLEBuffer = NULL;
    }

#if SF_SUPPORT_ENCRYPTION
    DeInitCryptoCTX(s_pCryptCTX);
    s_pCryptCTX = NULL;
#endif
}

/*___________________________________________________
 / LinuxFlashDeviceUnit Handle Creation function */
static const ILTFlashDeviceUnit s_ILTFlashDeviceUnit;

LTDeviceUnit
LinuxFlashDeviceUnit_CreateHandle(void) {
    LTDeviceUnit hFlashDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTFlashDeviceUnit, sizeof(FlashInfo *));
    if (hFlashDevice) {
        /* got a handle back with enough room to store a pointer to our data */
        FlashInfo ** ppFlashInfo = (FlashInfo **)LT_GetCore()->ReserveHandlePrivateData(hFlashDevice);
        *ppFlashInfo = &s_flashInfo;
        LT_GetCore()->ReleaseHandlePrivateData(hFlashDevice, ppFlashInfo);

        s_flashInfo.mutex->API->Lock(s_flashInfo.mutex);
        s_flashInfo.nRefCount++;

        /* Write the partition table if one isn't already written */
        u32 nMagic = 0;
        u32 partSectorNum = kPartByteOffset/s_flashInfo.nSectorSize;
        u32 partSectorOffset = kPartByteOffset - (partSectorNum * s_flashInfo.nSectorSize);
        sReadSecFileToBuffer(partSectorNum);
        memcpy((u8*)&nMagic, s_SectorBuffer + partSectorOffset, sizeof(nMagic));
        if (nMagic != kLTDeviceFlash_Magic_PartitionTable) {
            LTLOG_YELLOWALERT("format", "Formatting flash");
            u32 writeOffset = partSectorOffset;
            LTDeviceFlash_PartitionTableHeader header = {
                kLTDeviceFlash_Magic_PartitionTable,
                sizeof(s_partitions) / sizeof(s_partitions[0]),
                /* Size = 2^(nDeviceSize + 6) bytes */
                25 - __builtin_clz(s_flashInfo.nNumSectors * s_flashInfo.nSectorSize),
                0,
                { 0, 0, 0, 0, 0 }
            };
            u32 nCRC = 0;
            LTUtilityByteOps * pOps = (LTUtilityByteOps *)LT_GetCore()->OpenLibrary("LTUtilityByteOps");
            if (pOps) {
                pOps->Crc32((u8 *)&header, sizeof(header), &nCRC);
                pOps->Crc32((u8 *)&s_partitions, sizeof(s_partitions), &nCRC);
                LT_GetCore()->CloseLibrary((LTLibrary *)pOps);
            }

            memcpy(s_SectorBuffer + writeOffset, (u8 *)&header, sizeof(header));
            writeOffset += sizeof(header);
            memcpy(s_SectorBuffer + writeOffset, (u8 *)&s_partitions, sizeof(s_partitions));
            writeOffset += sizeof(s_partitions);
            memcpy(s_SectorBuffer + writeOffset, (u8 *)&nCRC, sizeof(nCRC));
            sWriteSecBuffertoFile();
        }
        s_flashInfo.mutex->API->Unlock(s_flashInfo.mutex);
    }
    return hFlashDevice;
}

/*________________________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit public interface functions */

static u32
LinuxFlashDeviceUnit_GetFlashID(LTDeviceUnit hFlashDevice, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(flashIDToSet);
    return 0;
}

static u32  LinuxFlashDeviceUnit_GetNumBytes(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize * pFlashInfo->nNumSectors;
}

static u32  LinuxFlashDeviceUnit_GetNumSectors(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nNumSectors;
}

static u32  LinuxFlashDeviceUnit_GetBytesPerSector(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return pFlashInfo->nSectorSize;
}

static u32  LinuxFlashDeviceUnit_SectorNumberToByteOffset(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? pFlashInfo->nSectorSize * nSectorNumber : 0;
}

static u32  LinuxFlashDeviceUnit_ByteOffsetToSectorNumber(LTDeviceUnit hFlashDevice, u32 nByteOffset) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (!pFlashInfo) return false;
    u32 nSectorNumber = nByteOffset / pFlashInfo->nSectorSize;
    return (nSectorNumber < pFlashInfo->nNumSectors) ? nSectorNumber : 0;
}

static bool
LinuxFlashDeviceUnit_GetPartitionTableOffset(LTDeviceUnit hFlashDevice, u32 * pByteOffset, bool bGetPrimary) {
    LT_UNUSED(hFlashDevice);
    if (!bGetPrimary) return false;
    *pByteOffset = kPartByteOffset;
    return true;
}

static bool LinuxFlashDeviceUnit_BusAddressToByteOffset(LTDeviceUnit hFlashDevice, void * pAddress, u32 * pByteOffset) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(pAddress);
    // hack to allow IotServiceUpdate to load
    *pByteOffset = kFw0ByteOffset;
    return true;
}

static u16 LinuxFlashDeviceUnit_GetWriteQuantum(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return kWriteQuantum;
}

static bool LinuxFlashDeviceUnit_EraseDevice(LTDeviceUnit hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    struct dirent *file_entry;
    bool bResult = false;

    if (!pFlashInfo) return false;

    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    DIR* flashDir = opendir(s_pFlashPath);
    if (flashDir) {
        while ((file_entry = readdir(flashDir)) != NULL) {
            if (file_entry->d_type == DT_REG) {
                char *fullFilePath = MakeFullPath(file_entry->d_name);
                if (fullFilePath) {
                    int r = remove(fullFilePath);
                    if (r != 0) {
                        LTLOG_REDALERT("erase.rm.failed", "Remove Error %d: %s.", r, fullFilePath);
                    } else {
                        DLOG("erase.rm", "Removed %s", fullFilePath);
                        bResult = true;
                    }
                    lt_free(fullFilePath);
                } else {
                    LTLOG_REDALERT("erase.dev.fn.err", "Failed to create file path");
                    bResult = true;
                }
            }
        }
        closedir(flashDir);
    } else {
        LTLOG_REDALERT("erase.nopath", "Cannot open %s", s_pFlashPath);
    }
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool LinuxFlashDeviceUnit_EraseSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    bool bResult = true;

    DLOG("erasesec", "Erasing sector %u.", nSectorNumber);

    if (!pFlashInfo || nSectorNumber >= pFlashInfo->nNumSectors) return false;

    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    char *fullFilePath = MakeSectorFullPath(nSectorNumber);
    if (fullFilePath) {
        if (access(fullFilePath, F_OK) == 0) {
            int r = remove(fullFilePath);
            if (r != 0) {
                LTLOG_REDALERT("erasesec.rm.failed", "Remove Error %d: %s.", r, fullFilePath);
                bResult = false;
            } else {
                DLOG("erasesec.rm", "Removed sector %u - %s", nSectorNumber, fullFilePath);
            }
        } else {
            DLOG("erasesec.nop", "Sector not in file.");
        }
        lt_free(fullFilePath);
    } else {
        LTLOG_REDALERT("erase.sect.fn.err", "Failed to create file path");
        bResult = true;
    }

    // If sector already in buffer, fill it with EMTPY_BYTE too.
    if (s_SectorBufferSectorNum == nSectorNumber) {
        lt_memset(s_SectorBuffer, EMTPY_BYTE, s_SectorBufferSize);
    }
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

static bool LinuxFlashDeviceUnit_EraseSectors(LTDeviceUnit hDevice, u32 nFirstSector, u32 nNumSectors) {
    if (!nNumSectors) return true;
    if (   nFirstSector               >= LinuxFlashDeviceUnit_GetNumSectors(hDevice)
        || nNumSectors                >  LinuxFlashDeviceUnit_GetNumSectors(hDevice)
        || nFirstSector + nNumSectors >  LinuxFlashDeviceUnit_GetNumSectors(hDevice)) return false;
    for (; nNumSectors; ++nFirstSector, --nNumSectors)
        if (!LinuxFlashDeviceUnit_EraseSector(hDevice, nFirstSector)) return false;
    return true;
}

static bool LinuxFlashDeviceUnit_IsDeviceWriteProtected(LTDeviceUnit hFlashDevice) {
    LT_UNUSED(hFlashDevice);
    return false; /* write protection not currently supported */
}

static bool LinuxFlashDeviceUnit_IsSectorWriteProtected(LTDeviceUnit hFlashDevice, u32 nSectorNumber) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    return false; /* write protection not currently supported */
}

static bool LinuxFlashDeviceUnit_WriteProtectDevice(LTDeviceUnit hFlashDevice, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(bWriteProtect);
    return false; /* can't write protect the whole flash on this part */
}

static bool LinuxFlashDeviceUnit_WriteProtectSector(LTDeviceUnit hFlashDevice, u32 nSectorNumber, bool bWriteProtect) {
    LT_UNUSED(hFlashDevice);
    LT_UNUSED(nSectorNumber);
    LT_UNUSED(bWriteProtect);
    return false;
}

static bool LinuxFlashDeviceUnit_ReadBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, u8 * pBuff) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    u32 bytesRemaining = nNumBytes;
    u32 readOffset = nByteOffset;
    u8 *p_Dst = pBuff;

    DLOG("flash.rd", "Read %08X - %08X", nByteOffset, nByteOffset + nNumBytes);
    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors * pFlashInfo->nSectorSize)) return false;
    if (!s_SectorBuffer) { LT_GetCore()->DebugBreak(); }

    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    while ( bytesRemaining ) {
        u32 sectorNum = readOffset/pFlashInfo->nSectorSize;
        u32 nextSectorOffset = (sectorNum + 1) *  pFlashInfo->nSectorSize;
        u32 sectorBytesFromOffset = nextSectorOffset - readOffset;
        u32 sectorBytesToRead = (bytesRemaining >= sectorBytesFromOffset) ? sectorBytesFromOffset : bytesRemaining;
        u32 offsetInSector = readOffset - (sectorNum * pFlashInfo->nSectorSize);

        if (s_SectorBufferSectorNum != sectorNum) {
            sReadSecFileToBuffer(sectorNum);
            DLOG("readsec", "read sec %u", sectorNum);
        }

        DLOG("read", "offsetInSector %u, bytes to read %u", offsetInSector, sectorBytesToRead);
        lt_memcpy(p_Dst, s_SectorBuffer + offsetInSector, sectorBytesToRead);

        // Update counters
        p_Dst += sectorBytesToRead;
        readOffset += sectorBytesToRead;
        bytesRemaining -= sectorBytesToRead;
    }
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return true;
}

static bool LinuxFlashDeviceUnit_WriteBytes(LTDeviceUnit hFlashDevice, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    u32 bytesRemaining = nNumBytes;
    u32 writeOffset = nByteOffset;
    bool bResult = true;
    const u8 * pSrc = pBuff;

    DLOG("flash.wr", "Write %08X - %08X", nByteOffset, nByteOffset + nNumBytes);

    if (!pFlashInfo || (nByteOffset + nNumBytes) > (pFlashInfo->nNumSectors * pFlashInfo->nSectorSize)) return false;

    pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
    while ( bytesRemaining ) {
        u32 sectorNum = writeOffset/pFlashInfo->nSectorSize;
        u32 nextSectorOffset = (sectorNum + 1) *  pFlashInfo->nSectorSize;
        u32 sectorBytesFromOffset = nextSectorOffset - writeOffset;
        u32 sectorBytesToWrite = (bytesRemaining >= sectorBytesFromOffset) ? sectorBytesFromOffset : bytesRemaining;
        u32 offsetInSector = writeOffset - (sectorNum * pFlashInfo->nSectorSize);

        // If partial sector write, and target sector is not in memory, read sector file to buffer first
        if (    sectorBytesToWrite != pFlashInfo->nSectorSize &&
                s_SectorBufferSectorNum != sectorNum) {
            sReadSecFileToBuffer(sectorNum);
        }

        // Copy the write data to buffer
        lt_memcpy(s_SectorBuffer + offsetInSector, pSrc, sectorBytesToWrite);

        // Write sector buffer to file
        if (!sWriteSecBuffertoFile()) {
            bResult = false;
            break;
        }

        // Update counters
        pSrc += sectorBytesToWrite;
        writeOffset += sectorBytesToWrite;
        bytesRemaining -= sectorBytesToWrite;
    }
    pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    return bResult;
}

/*_________________________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit interface binding functions */
static void
LinuxFlashDeviceUnit_OnDestroyHandle(LTHandle hFlashDevice) {
    FlashInfo * pFlashInfo = GetFlashInfoPtr(hFlashDevice);
    if (pFlashInfo) {
        pFlashInfo->mutex->API->Lock(pFlashInfo->mutex);
        if (0 == --pFlashInfo->nRefCount) s_SectorBufferSectorNum = INVALID_SECTOR;
        pFlashInfo->mutex->API->Unlock(pFlashInfo->mutex);
    }
}

/*__________________________________________________________
 / LinuxFlashDeviceUnit ILTFlashDeviceUnit Interface Definition */
define_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, LinuxFlashDeviceUnit_OnDestroyHandle)
    .GetFlashID                  = LinuxFlashDeviceUnit_GetFlashID,
    .GetNumBytes                 = LinuxFlashDeviceUnit_GetNumBytes,
    .GetNumSectors               = LinuxFlashDeviceUnit_GetNumSectors,
    .GetBytesPerSector           = LinuxFlashDeviceUnit_GetBytesPerSector,
    .SectorNumberToByteOffset    = LinuxFlashDeviceUnit_SectorNumberToByteOffset,
    .ByteOffsetToSectorNumber    = LinuxFlashDeviceUnit_ByteOffsetToSectorNumber,
    .GetPartitionTableOffset     = LinuxFlashDeviceUnit_GetPartitionTableOffset,
    .BusAddressToByteOffset      = LinuxFlashDeviceUnit_BusAddressToByteOffset,
    .GetWriteQuantum             = LinuxFlashDeviceUnit_GetWriteQuantum,
    .EraseDevice                 = LinuxFlashDeviceUnit_EraseDevice,
    .EraseSectors                = LinuxFlashDeviceUnit_EraseSectors,
    .IsDeviceWriteProtected      = LinuxFlashDeviceUnit_IsDeviceWriteProtected,
    .IsSectorWriteProtected      = LinuxFlashDeviceUnit_IsSectorWriteProtected,
    .WriteProtectDevice          = LinuxFlashDeviceUnit_WriteProtectDevice,
    .WriteProtectSector          = LinuxFlashDeviceUnit_WriteProtectSector,
    .ReadBytes                   = LinuxFlashDeviceUnit_ReadBytes,
    .WriteBytes                  = LinuxFlashDeviceUnit_WriteBytes,
    .ReadRawBytes                = LinuxFlashDeviceUnit_ReadBytes,
    .WriteRawBytes               = LinuxFlashDeviceUnit_WriteBytes,
LTLIBRARY_DEFINITION;
