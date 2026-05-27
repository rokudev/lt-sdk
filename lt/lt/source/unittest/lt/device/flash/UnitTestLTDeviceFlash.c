/*******************************************************************************
 * Flash Unit Test
 *
 * Stress test LTDeviceFlash by erasing, writing, and reading to the entire test
 * partition
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
#include <lt/device/flash/LTDeviceFlash.h>

#include <tilt/JiltEngine.h>

static struct S {
    Tilt               *tilt;
    LTDeviceFlash      *flash;
    LTDeviceUnit        hFlashDevice;
    ILTFlashDeviceUnit *iFlashDevice;
    u8                 *writeBuff;
    u8                 *readBuff;
    u8                 *backUpBuff;
} s;

static JiltEngine *s_engine;

/*******************************************************************************
 * Report mismatching data between the write and read buffers:                */
static void ReportMismatch(const u8 *writeBuff, const u8 *readBuff, u32 size) {
    TILT_DEBUG(s.tilt, "Mismatch buffer report:");
    TILT_DEBUG(s.tilt, "Position  Write  Read");
    TILT_DEBUG(s.tilt, "--------  -----  ----");
    char lastMismatch = '\0';
    u32 mismatchCount = 0;
    for (u32 i = 0; i < size; ++i) {
        if (writeBuff[i] != readBuff[i]) {
            if (lastMismatch != readBuff[i]) {
                TILT_DEBUG(s.tilt, "%8u   0x%02x  0x%02x", i, writeBuff[i], readBuff[i]);
                lastMismatch = readBuff[i];
            }
            ++mismatchCount;
        } else {
            if (mismatchCount) {
                TILT_REPORT_FAILURE(s.tilt, "%lu mismatch%s detected", mismatchCount, mismatchCount > 1 ? "es": "");
                mismatchCount = 0;
            }
        }
    }
    if (mismatchCount)   /* check again in case the last ones were a mismatch */
        TILT_REPORT_FAILURE(s.tilt, "%lu mismatch%s detected", mismatchCount, mismatchCount > 1 ? "es": "");
}

/*******************************************************************************
 * Erase and write an entire sector:                                          */
static bool WriteSector(const u32 nSector, const u8 *pBuff) {
    const u32 address = s.iFlashDevice->SectorNumberToByteOffset(s.hFlashDevice, nSector);
    const u32 size = s.iFlashDevice->GetBytesPerSector(s.hFlashDevice);
    if (!s.iFlashDevice->EraseSectors(s.hFlashDevice, nSector, 1)) {
        TILT_REPORT_FAILURE(s.tilt, "failed to erase address 0x%08lX", LT_Pu32(address));
        return false;
    }
    if (!s.iFlashDevice->WriteBytes(s.hFlashDevice, address, size, pBuff)) {
        TILT_REPORT_FAILURE(s.tilt, "failed to write to address 0x%08lX", LT_Pu32(address));
        return false;
    }
    return true;
}

/*******************************************************************************
 * Enumerate partitions in case partition named by user is not available:     */
static bool PartitionEnumProc(u32 nNumPartitions, const LTDeviceFlash_Partition *pPartition, void *pClientData) {
    u32 *pPartitionCount = (u32 *)pClientData;
    if (*pPartitionCount == 0) {
        TILT_WARNING(s.tilt, "%d Available Flash Partitions:", nNumPartitions);
        TILT_WARNING(s.tilt, "%-2s %-12s %-12s %-9s %-5s", "I", "Name", "Type", "Address", "Sectors");
        TILT_WARNING(s.tilt, "%-56.56s", "----------------------------------------------------------------------------");
    }
    TILT_WARNING(s.tilt, "%-2u %-12.12s %-12.12s %08X  %-5d", *pPartitionCount, pPartition->entry.name,
                                                              pPartition->entry.type, pPartition->entry.nByteOffset,
                                                              pPartition->entry.nNumSectors);
    return ++*pPartitionCount < nNumPartitions;
}

/*******************************************************************************
 * Sector corruption warning:                                                 */
static void PrintSectorCorruptionWarning(Tilt *tilt, u32 address) {
    TILT_REPORT_FAILURE(tilt, "Flash sector restore FAILED. Device flash is now corrupted at 0x%08X and will need to be re-flashed", address);
}

/*********************************************************************************
 * Flash read/write test:                                                       */
static void RunFlashTest(Tilt *tilt) {
    s.tilt        = tilt;

    /* Properties:                                            Property name:                   */
    const char *partitionName        = tilt->API->GetProperty("partition",   "" ); /* Required */
    const char *memReducerParam      = tilt->API->GetProperty("memReducer",  "1"); /* Optional */
    const char *nIterationsParam     = tilt->API->GetProperty("nIterations", "2"); /* Optional */

    if (!(s.hFlashDevice = s.flash->CreateDeviceUnitHandle(0))) {
        TILT_REPORT_FAILURE(tilt, "Failed to create device unit");
        goto cleanup;
    }
    if (!(s.iFlashDevice = lt_gethandleinterface(ILTFlashDeviceUnit, s.hFlashDevice))) {
        TILT_REPORT_FAILURE(tilt, "Failed to get the device interface");
        goto cleanup;
    }

    if (!lt_strlen(partitionName)) {
        TILT_REPORT_CANNOT_RUN(tilt, "Must specify a partition name (partition=<name>)");
        goto cleanup;
    }
    LTDeviceFlash_Partition partition;
    if (!s.flash->GetPartition(s.hFlashDevice, partitionName, &partition)) {
        TILT_REPORT_CANNOT_RUN(s.tilt, "\"%s\" test partition not found", partitionName);
        TILT_WARNING(s.tilt, "Please select a valid partition name:");
        u32 partitionCount = 0;
        s.flash->EnumeratePartitions(s.hFlashDevice, PartitionEnumProc, (void *)&partitionCount);
        TILT_EXPECT_TRUE(s.tilt, partitionCount > 0, "No partitions available on device");
        TILT_WARNING(s.tilt, "Flash Test can be destructive in the event of test failure; choose the partition wisely.");
        goto cleanup;
    }

    /* It is not always possible to run this test on a systems with serious memory  constraints. The memReducer
       property can be used to reduce the size of the test buffers to fit within the available memory. */
    const u32 memReducer = lt_strtou32(memReducerParam, NULL, 10);
    const u32 secSize    = s.iFlashDevice->GetBytesPerSector(s.hFlashDevice) / memReducer;
    const u32 totalSize  = secSize * partition.entry.nNumSectors;

    if (   !(s.writeBuff  = lt_malloc(secSize))
        || !(s.readBuff   = lt_malloc(secSize))
        || !(s.backUpBuff = lt_malloc(secSize))) {
        TILT_REPORT_CANNOT_RUN(tilt, "Out of memory");
        goto cleanup;
    }

    TILT_DEBUG(tilt, "Test Partition = \"%s\", Device Sector Size = %d B, Total Size = %d B", partitionName, secSize, totalSize);

    char ch = '!';
    u32 nIterations = lt_strtou32(nIterationsParam, NULL, 10);
    u32 nSector = s.iFlashDevice->ByteOffsetToSectorNumber(s.hFlashDevice, partition.entry.nByteOffset);
    bool bSuccess = true;
    for (u32 i = 0; bSuccess && i < nIterations; ++i) {
        TILT_DEBUG(tilt, "Test Iteration #%lu", LT_Pu32(i));
        lt_memset(s.writeBuff, ch++, secSize);
        if (ch >= '~') ch = '!';
        for (u32 n = 0; bSuccess && n < partition.entry.nNumSectors; ++n) {
            TILT_DEBUG(tilt, "Sector #%lu", LT_Pu32(n));
            lt_memset(s.readBuff, 0, secSize);
            u32 address = s.iFlashDevice->SectorNumberToByteOffset(s.hFlashDevice, nSector + n);
            /* Cache sector to restore after test */
            if (!(bSuccess = s.iFlashDevice->ReadBytes(s.hFlashDevice, address, secSize, s.backUpBuff))) {
                TILT_REPORT_FAILURE(tilt,  "failed to read from address 0x%08lX to backUpBuff", LT_Pu32(address));
                break;
            }
            /* Write test data */
            if (!(bSuccess = WriteSector(nSector + n, s.writeBuff))) {
                TILT_REPORT_FAILURE(tilt, "failed to write to address 0x%08lX", LT_Pu32(address));
                break;
            }
            /* Read test data */
            if (!(bSuccess = s.iFlashDevice->ReadBytes(s.hFlashDevice, address, secSize, s.readBuff))) {
                TILT_REPORT_FAILURE(tilt,  "failed to read from address 0x%08lX", LT_Pu32(address));
                break;
            }
            /* Verify test data */
            bSuccess = lt_memcmp(s.writeBuff, s.readBuff, secSize) == 0;
            if (!bSuccess) {
                ReportMismatch(s.writeBuff, s.readBuff, secSize);
                TILT_REPORT_FAILURE(tilt, "read and write buffers did not match");
                if (!WriteSector(nSector + n, s.backUpBuff))    /* Restore sector */
                    PrintSectorCorruptionWarning(tilt, address);
                break;
            }
            /* Restore sector to pre-test state */
            if (!(bSuccess = WriteSector(nSector + n, s.backUpBuff)))
                PrintSectorCorruptionWarning(tilt, address);
        }
    }

cleanup:
    LT_GetCore()->DestroyHandle(s.hFlashDevice);
    lt_free(s.writeBuff);
    lt_free(s.readBuff);
    lt_free(s.backUpBuff);
    s.hFlashDevice = 0;
    s.tilt         = NULL;
    s.iFlashDevice = NULL;
    s.writeBuff    = NULL;
    s.readBuff     = NULL;
    s.backUpBuff   = NULL;
}

static void BeforeAllTests(Tilt *tilt) {
    s.flash = lt_openlibrary(LTDeviceFlash);
    TILT_EXPECT_TRUE(tilt, s.flash != NULL, "Cannot open LTDeviceFlash");
}

static void AfterAllTests(Tilt *tilt) { LT_UNUSED(tilt); lt_closelibrary(s.flash); s.flash = NULL; }

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { RunFlashTest, "Flash", "Runs the flash test. Warning: test failure can corrupt device flash", 0 },
};

static int UnitTestLTDeviceFlashImpl_Run(int argc, const char **argv) {
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceFlashImpl_LibInit(void) { return (s_engine = lt_createobject(JiltEngine)); }

static void UnitTestLTDeviceFlashImpl_LibFini(void) { lt_destroyobject(s_engine); }

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFlash, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceFlash, UnitTestLTDeviceFlashImpl_Run, 1536) LTLIBRARY_DEFINITION;
