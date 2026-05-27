/******************************************************************************
 * platforms/linux/source/linux/system/crashdump/LinuxCrashdump.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include <lt/system/crashdump/LTSystemCrashdump.h>

/*******************************************************************************
 * libraries
*******************************************************************************/

DEFINE_LTLOG_SECTION("crashdump");

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTSystemCrashdump, (LTSystemCrypto) (LTUtilityByteOps));

typedef struct {
    LT_SIZE  nBytes;
    char     sDevice[PATH_MAX];
    u8       uCoreHash[SHA256_HASH_LENGTH];
    LT_SIZE  nElfCoreBytes;
    int      deviceFd;
} CoreData;

static const char coreInfoPath[] = "/configs/coreinfo.txt";

static struct Statics {
    LTMutex        *coreDataMutex;
    CoreData       *pCoreData;
    char            version[16];
} S;

static bool AllocateBuffer(char **ppBuffer, LT_SIZE *pBufferSize) {
    const LT_SIZE minBufferSize = 4096;
    const LT_SIZE maxBufferSize = 65536;

    for (LT_SIZE bufferSize = maxBufferSize; bufferSize >= minBufferSize; bufferSize /= 2) {
        char *pBuffer = lt_malloc(bufferSize);
        if (pBuffer) {
            *ppBuffer = pBuffer;
            *pBufferSize = bufferSize;
            return true;
        }
    }

    *ppBuffer = NULL;
    *pBufferSize = 0;
    return false;
}

static void DestroyCoreData(CoreData **ppCoreData) {
    CoreData *pCoreData = *ppCoreData;
    if (!pCoreData) return;
    lt_memset(S.version, 0, sizeof(S.version));
    *ppCoreData = NULL;

    if (pCoreData->deviceFd != -1) {
        close(pCoreData->deviceFd);
        pCoreData->deviceFd = -1;
    }
    lt_free(pCoreData);
}

static CoreData *ParseCoreInfo(char *buffer, LT_SIZE bufferSize) {
    CoreData *pCoreData = NULL, *retVal = NULL;
    FILE *coreInfoFile = NULL;

    do {
         pCoreData = lt_malloc(sizeof(CoreData));
         if (!pCoreData) break;
         lt_memset(pCoreData, 0, sizeof(CoreData));
         pCoreData->deviceFd = -1;

         coreInfoFile = fopen(coreInfoPath, "r");
         if (!coreInfoFile) break;

         // Parse core information textfile which consists of lines
         // of the form "key=value".
         bool validSha256 = false;
         while (fgets(buffer, bufferSize, coreInfoFile) != NULL) {
             char *value = lt_strchr(buffer, '=');
             if (!value) continue;
             *value++ = '\0';

             LT_SIZE valueLength = lt_strlen(value);
             if (valueLength > 0 && value[valueLength - 1] == '\n') {
                 value[--valueLength] = '\0';
             }
             if (!valueLength) continue;

             const char *key = buffer;
             if (lt_strcmp(key, "CORE_DEV") == 0) {
                 lt_strncpyTerm(pCoreData->sDevice, value,
                                sizeof(pCoreData->sDevice));
             } else if (lt_strcmp(key, "CORE_BYTES") == 0) {
                 pCoreData->nBytes = lt_strtou64(value, NULL, 10);
             } else if (lt_strcmp(key, "CORE_SHA256") == 0) {
                 LTUtilityByteOps *byteOps = LT_GetLTUtilityByteOps();
                 validSha256 = byteOps->HexDecode(value,
                                                  lt_strlen(value),
                                                  pCoreData->uCoreHash,
                                                  sizeof(pCoreData->uCoreHash)) == SHA256_HASH_LENGTH;
             } else if (lt_strcmp(key, "LT_SOFTWARE_VERSION") == 0) {
                 lt_strncpyTerm(S.version, value, sizeof(S.version));
             } else if (lt_strcmp(key, "ELF_CORE_BYTES") == 0) {
                 pCoreData->nElfCoreBytes = lt_strtou64(value, NULL, 10);
             }
         }
         if (ferror(coreInfoFile)) break;

         // Check whether the core information file contained all required keys.
         if (lt_strncmp(pCoreData->sDevice, "/dev/", 5) != 0) break;
         if (pCoreData->nBytes == 0) break;
         if (!validSha256) break;
         if (lt_strlen(S.version) != 13) break;

         retVal = pCoreData;
         pCoreData = NULL;
    } while (false);

    if (coreInfoFile) fclose(coreInfoFile);
    DestroyCoreData(&pCoreData);

    return retVal;
}

static bool VerifyCoreData(CoreData *pCoreData, char *buffer, LT_SIZE bufferSize) {
    LTSystemCrypto *pCrypto = LT_GetLTSystemCrypto();
    LT_SHA256_CTX *pHashCtx = NULL;
    bool retVal = false;

    do {
        pHashCtx = pCrypto->CreateSeqSHA256();
        if (!pHashCtx) break;

        // It would be much nicer to mmap() the core dump instead of reading
        // it one chunk at a time. However the core dump resides on a physical
        // flash partition which can contain bad blocks. And read errors
        // within a memory mapped area are reported as a "Bus error" via
        // a signal that would crash the LT process.
        LT_SIZE remainder = pCoreData->nBytes;
        while (remainder > 0) {
            ssize_t chunkSize = LT_MIN(remainder, bufferSize);
            if (read(pCoreData->deviceFd, buffer, chunkSize) != chunkSize) break;
            if (pCrypto->UpdateSeqSHA256(pHashCtx,
                                         (const u8 *)buffer,
                                         chunkSize) != kLTSystemCrypto_Result_Ok) break;
            remainder -= chunkSize;
        }
        if (remainder) break;

        u8 coreHash[SHA256_HASH_LENGTH];
        if (pCrypto->FinishSeqSHA256(pHashCtx,
                                     coreHash) != kLTSystemCrypto_Result_Ok) break;

        retVal = (lt_memcmp(coreHash, pCoreData->uCoreHash, sizeof(coreHash)) == 0);
    } while (false);

    pCrypto->DestroySeqSHA256(pHashCtx);

    return retVal;
}

static CoreData *GetCoreData(void) {
    LT_SIZE bufferSize;
    char *buffer = NULL;
    CoreData *pCoreData = NULL;
    bool coreValid = false;

    do {
        if (!AllocateBuffer(&buffer, &bufferSize)) break;

        pCoreData = ParseCoreInfo(buffer, bufferSize);
        if (!pCoreData) break;

        pCoreData->deviceFd = open(pCoreData->sDevice, O_RDONLY);
        if (pCoreData->deviceFd == -1) break;

        struct stat statBuf;
        if (fstat(pCoreData->deviceFd, &statBuf) != 0) break;
        if (!S_ISCHR(statBuf.st_mode)) break;

        if (!VerifyCoreData(pCoreData, buffer, bufferSize)) {
            LTLOG_SERVER("core.verify.fail", "Core hash verification failed");
            break;
        }

        coreValid = true;
    } while (false);

    if (!coreValid) {
        DestroyCoreData(&pCoreData);
        unlink(coreInfoPath);
    }
    lt_free(buffer);

    return pCoreData;
}

static bool LinuxCrashdump_CrashdumpErase(void) {
    if (!S.pCoreData) return true;

    S.coreDataMutex->API->Lock(S.coreDataMutex);
    DestroyCoreData(&S.pCoreData);
    S.coreDataMutex->API->Unlock(S.coreDataMutex);

    return (unlink(coreInfoPath) == 0 || errno == ENOENT);
}

static LT_SIZE LinuxCrashdump_CrashdumpGetSize(void) {
    LT_SIZE retVal = 0;
    S.coreDataMutex->API->Lock(S.coreDataMutex);
    if (S.pCoreData != NULL) {
        retVal = S.pCoreData->nBytes;
        LTLOG_SERVER("core.size", "Available core size: %llu/%llu B",
                     LT_Pu64(S.pCoreData->nElfCoreBytes),
                     LT_Pu64(S.pCoreData->nBytes));
    }
    S.coreDataMutex->API->Unlock(S.coreDataMutex);
    return retVal;
}

static const char * LinuxCrashdump_CrashdumpGetVersion(void) {
    const char *vers = NULL;
    S.coreDataMutex->API->Lock(S.coreDataMutex);
    if (S.pCoreData) vers = S.version;
    S.coreDataMutex->API->Unlock(S.coreDataMutex);
    return vers;
}

static LT_SIZE LinuxCrashdump_CrashdumpGetChunk(void *pBuffer, LT_SIZE nBufferSize, LT_SIZE nOffset) {
    S.coreDataMutex->API->Lock(S.coreDataMutex);
    ssize_t inBytes = pread(S.pCoreData->deviceFd, pBuffer, nBufferSize, nOffset);
    S.coreDataMutex->API->Unlock(S.coreDataMutex);
    if (inBytes <= 0) {
        const char *reason = (inBytes == 0) ? "end of file" : strerror(errno);
        LTLOG_SERVER("core.upload.read.fail", "Failed to read core data: %s", reason);
        return 0;
    } else return inBytes;
}

static void LTSystemCrashdumpImpl_LibFini(void) {
    DestroyCoreData(&S.pCoreData);

    lt_destroyobject(S.coreDataMutex);
    S.coreDataMutex = NULL;
}

static bool LTSystemCrashdumpImpl_LibInit(void) {
    do {
        S.coreDataMutex = lt_createobject(LTMutex);
        if (!S.coreDataMutex) break;

        S.pCoreData = GetCoreData();
        if (S.pCoreData) {
            LTLOG_SERVER("core.found",
                         "Found %llu/%llu bytes core dump in partition '%s'",
                         LT_Pu64(S.pCoreData->nElfCoreBytes),
                         LT_Pu64(S.pCoreData->nBytes),
                         S.pCoreData->sDevice);
        }

        return true;
    } while (false);

    LTSystemCrashdumpImpl_LibFini();
    return false;
}

/*___________________________________________
 / LTSystemCrashdump library interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemCrashdump,)

    .Erase        = &LinuxCrashdump_CrashdumpErase,
    .GetSize      = &LinuxCrashdump_CrashdumpGetSize,
    .GetVersion   = &LinuxCrashdump_CrashdumpGetVersion,
    .GetChunk     = &LinuxCrashdump_CrashdumpGetChunk,

LTLIBRARY_DEFINITION;
