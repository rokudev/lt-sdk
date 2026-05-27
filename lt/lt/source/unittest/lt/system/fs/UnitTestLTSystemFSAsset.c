/*******************************************************************************
 * UnitTestLTSystemFSAsset.c                        LTAsset Pseudo-FS Unit Tests
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/system/settings/LTSystemSettings.h>
#include <tilt/JiltEngine.h>

#include <lt/system/fs/LTFile.h>

enum {
    kNumAssets = 2,
    kNumPRNGs  = 2
};

static LTFile *s_asset[kNumAssets];
static u32     s_prng[kNumPRNGs];
static u64     s_seed;
static bool    s_extendedTests;

static JiltEngine    *s_engine;

static void OpenCloseTests(Tilt *tilt) {
    u8 buffer[100];
    /* Opens with ill-formed names */
    {
        s_asset[0]->API->SetName(s_asset[0], "test_asset");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        s_asset[0]->API->SetName(s_asset[0], "/bad/test_asset");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        s_asset[0]->API->SetName(s_asset[0], "//test_asset");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        s_asset[0]->API->SetName(s_asset[0], "assets/");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        s_asset[0]->API->SetName(s_asset[0], "/assets");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        /* Test with reserved (empty) leaf-name */
        s_asset[0]->API->SetName(s_asset[0], "/assets/");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
    }
    /* Calling open twice on same asset object / then double close */
    {
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_assetX");
        TILT_EXPECT_TRUE(tilt,  s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt,  s_asset[0]->API->Read(s_asset[0], 1, buffer) == 0, "Succeeded in reading from non-existent asset");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Succeeded in opening asset");
        s_asset[0]->API->Close(s_asset[0]);
        s_asset[0]->API->Close(s_asset[0]);
    }
    /* Opening same asset twice on two asset objects / destroy object without close */
    {
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_assetX");
        s_asset[1]->API->SetName(s_asset[1], "/assets/test_assetX");
        TILT_EXPECT_TRUE(tilt,  s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_FALSE(tilt, s_asset[1]->API->Open(s_asset[1], true), "Succeeded in opening asset");
        /* Destroy without close (then recreate object for next test) */
        lt_destroyobject(s_asset[0]);
        s_asset[0] = lt_createobject_typed(LTFile, Asset);
        TILT_ASSERT_TRUE(tilt, s_asset[0] != NULL, "Failed to create asset object");
    }
    /* Opening two different assets / close */
    {
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_assetX");
        s_asset[1]->API->SetName(s_asset[1], "/assets/test_assetY");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[1]->API->Open(s_asset[1], true), "Failed to open asset");
        s_asset[0]->API->Close(s_asset[0]);
        s_asset[1]->API->Close(s_asset[1]);
    }
}

static void ReadWriteDeleteTests(Tilt *tilt) {
    /* Small asset write/read/overwrite/read */
    for (u32 iter = 0; iter < 2; iter++) {
        enum { kBufferSize = 33, kNumWrites = 2, kSmallAssetSize = kNumWrites * kBufferSize };
        u8 buffer[kBufferSize];
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], sizeof(buffer), buffer) == 0, "Read should return zero");
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer, kBufferSize);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], sizeof(buffer), buffer) == sizeof(buffer), "Failed to write to asset");
        }
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->GetSize(s_asset[0]) == kSmallAssetSize, "Wrong asset size");
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->GetSize(s_asset[0]) == kSmallAssetSize, "Wrong asset size");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should exist");
        u8 checkBuffer[kBufferSize];
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[1], checkBuffer, kBufferSize);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], sizeof(buffer), buffer) == sizeof(buffer), "Failed to read from asset");
            for (u32 j = 0; j < kBufferSize; j++) {
                TILT_EXPECT_TRUE(tilt, buffer[j] == checkBuffer[j], "Read data mismatch at %u", i*kBufferSize + j);
            }
        }
        s_asset[0]->API->Close(s_asset[0]);
    }
    /* Large asset write/read/overwrite/read */
    for (u32 iter = 0; iter < 2; iter++) {
        enum { kBufferSize = 17, kNumWrites = 529, kLargeAssetSize = kNumWrites * kBufferSize };
        u8 buffer[kBufferSize];
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset2");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer, kBufferSize);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], kBufferSize, buffer) == kBufferSize, "Failed to write asset");
        }
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->GetSize(s_asset[0]) == kLargeAssetSize, "Wrong asset size");
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->GetSize(s_asset[0]) == kLargeAssetSize, "Wrong asset size");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should exist");
        u8 checkBuffer[kBufferSize];
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[1], checkBuffer, kBufferSize);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], kBufferSize, buffer) == kBufferSize, "Failed to read asset");
            for (u32 j = 0; j < kBufferSize; j++) {
                TILT_EXPECT_TRUE(tilt, buffer[j] == checkBuffer[j], "Read data mismatch at %u", i*kBufferSize + j);
            }
        }
        s_asset[0]->API->Close(s_asset[0]);
    }
    /* Delete test assets */
    {
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should exist");
        s_asset[0]->API->Delete(s_asset[0], false);
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should not exist after deletion");
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset2");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should exist");
        s_asset[0]->API->Delete(s_asset[0], false);
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should not exist after deletion");
    }
}

static void MultipleBulkAssetTests(Tilt *tilt) {
    /* Write/Read multiple bulk assets */
    {
        enum { kNumWrites = 529 };
        enum { kBufferSize1 = 17, kLargeAssetSize1 = kNumWrites * kBufferSize1 };
        enum { kBufferSize2 = 33, kLargeAssetSize2 = kNumWrites * kBufferSize2 };
        u8 buffer1[kBufferSize1];
        u8 buffer2[kBufferSize2];
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset3");
        s_asset[1]->API->SetName(s_asset[1],  "/assets/test_asset4");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[1]->API->Open(s_asset[1], true), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer1, kBufferSize1);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], kBufferSize1, buffer1) == kBufferSize1, "Failed to write first asset");
            tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer2, kBufferSize2);
            TILT_EXPECT_TRUE(tilt, s_asset[1]->API->Write(s_asset[1], kBufferSize2, buffer2) == kBufferSize2, "Failed to write second asset");
        }
        s_asset[0]->API->Close(s_asset[0]);
        s_asset[1]->API->Close(s_asset[1]);
        u8 checkBuffer1[kBufferSize1];
        u8 checkBuffer2[kBufferSize2];
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[1]->API->Open(s_asset[1], true), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[1], checkBuffer1, kBufferSize1);
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], kBufferSize1, buffer1) == kBufferSize1, "Failed to read asset");
            tilt->API->PrngGenerateRandomBytes(s_prng[1], checkBuffer2, kBufferSize2);
            TILT_EXPECT_TRUE(tilt, s_asset[1]->API->Read(s_asset[1], kBufferSize2, buffer2) == kBufferSize2, "Failed to read asset");
            for (u32 j = 0; j < kBufferSize1; j++) {
                TILT_EXPECT_TRUE(tilt, buffer1[j] == checkBuffer1[j], "Read data mismatch at %u", i*kBufferSize1 + j);
            }
            for (u32 j = 0; j < kBufferSize2; j++) {
                TILT_EXPECT_TRUE(tilt, buffer2[j] == checkBuffer2[j], "Read data mismatch at %u", i*kBufferSize2 + j);
            }
        }
        s_asset[0]->API->Close(s_asset[0]);
        s_asset[1]->API->Close(s_asset[1]);
        /* Clean up */
        s_asset[0]->API->Delete(s_asset[0], false);
        s_asset[1]->API->Delete(s_asset[1], false);
    }
}

static void SeekTests(Tilt *tilt) {
    /* Seek read position on small asset */
    {
        enum { kBufferSize = 13, kNumWrites = 20, kSmallAssetSize = kNumWrites * kBufferSize };
        u8 buffer[kBufferSize];
        for (u32 i = 0; i < kBufferSize; i++) {
            buffer[i] = i;
        }
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset3");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], kBufferSize, buffer) == kBufferSize, "Failed to write asset");
        }
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        for (u32 i = 0; i < 10; i++) {
            u32 seekPos = tilt->API->PrngGenerateRandomNumber(s_prng[0], kSmallAssetSize - 1);
            s_asset[0]->API->SeekToPosition(s_asset[0], seekPos);
            u8 byte;
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], 1, &byte) == 1, "Failed to read asset");
            TILT_EXPECT_TRUE(tilt, byte == (seekPos % kBufferSize), "Read data mismatch at position %u", seekPos);
        }
        s_asset[0]->API->Close(s_asset[0]);
    }
    /* Seek read position on large asset */
    {
        enum { kBufferSize = 17, kNumWrites = 529, kLargeAssetSize = kNumWrites * kBufferSize };
        u8 buffer[kBufferSize];
        for (u32 i = 0; i < kBufferSize; i++) {
            buffer[i] = i;
        }
        s_asset[0]->API->SetName(s_asset[0], "/assets/test_asset3");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        for (u32 i = 0; i < kNumWrites; i++) {
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], kBufferSize, buffer) == kBufferSize, "Failed to write asset");
        }
        s_asset[0]->API->Close(s_asset[0]);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        for (u32 i = 0; i < 100; i++) {
            u32 seekPos = tilt->API->PrngGenerateRandomNumber(s_prng[0], kLargeAssetSize - 1);
            s_asset[0]->API->SeekToPosition(s_asset[0], seekPos);
            u8 byte;
            TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Read(s_asset[0], 1, &byte) == 1, "Failed to read asset");
            TILT_EXPECT_TRUE(tilt, byte == (seekPos % kBufferSize), "Read data mismatch at position %u", seekPos);
        }
        s_asset[0]->API->Close(s_asset[0]);
    }
    /* Clean up */
    {
        s_asset[0]->API->Delete(s_asset[0], false);
    }
}

static void OtherTests(Tilt *tilt) {
    /* Exists corner cases */
    {
        s_asset[0]->API->SetName(s_asset[0], NULL);
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Exists(s_asset[0]), "This asset should not exist");
        s_asset[0]->API->SetName(s_asset[0], "/assets/");
        TILT_EXPECT_FALSE(tilt, s_asset[0]->API->Exists(s_asset[0]), "This asset should not exist");
    }
    /* Exists after open */
    {
        s_asset[0]->API->SetName(s_asset[0], "/assets/test");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], false), "Failed to open asset");
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Exists(s_asset[0]), "Asset should exist");
        s_asset[0]->API->Close(s_asset[0]);
    }
}

static void ExtendedTests(Tilt *tilt) {
    if (!s_extendedTests) {
        TILT_REPORT_CANNOT_RUN(tilt, "Skipping extended tests");
        return;
    }
    enum { kBufferSize = 100, kNumWrites = 300 };
    u8 buffer[kBufferSize];
    u32 numAssets = 0;
    LTString name = NULL;
    bool success = true;
    /* Create multiple assets until we run out of space */
    do {
        ltstring_format(&name, "/assets/test_extended_%u", numAssets);
        s_asset[0]->API->SetName(s_asset[0], name);
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset %u", numAssets);
        for (u32 i = 0; i < kNumWrites; i++) {
            tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer, sizeof(buffer));
            success = s_asset[0]->API->Write(s_asset[0], sizeof(buffer), buffer) == sizeof(buffer);
            if (!success) break;
        }
        s_asset[0]->API->Close(s_asset[0]);
        if (success) numAssets++;
    } while (success == true);
    /* Delete asset in the middle */
    ltstring_format(&name, "/assets/test_extended_%u", numAssets / 2);
    s_asset[0]->API->SetName(s_asset[0], name);
    s_asset[0]->API->Delete(s_asset[0], false);
    TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Open(s_asset[0], true), "Failed to open asset %u", numAssets);
    for (u32 i = 0; i < kNumWrites; i++) {
        tilt->API->PrngGenerateRandomBytes(s_prng[0], buffer, sizeof(buffer));
        TILT_EXPECT_TRUE(tilt, s_asset[0]->API->Write(s_asset[0], sizeof(buffer), buffer) == sizeof(buffer), "Failed to write to asset");
    }
    s_asset[0]->API->Close(s_asset[0]);
    for (u32 i = 0; i < numAssets; i++) {
        ltstring_format(&name, "/assets/test_extended_%u", i);
        s_asset[0]->API->SetName(s_asset[0], name);
        s_asset[0]->API->Delete(s_asset[0], false);
    }
    ltstring_destroy(name);
}

void BeforeAllTestsHook(Tilt *tilt) {
    for (u32 i = 0; i < kNumAssets; i++) {
        s_asset[i] = lt_createobject_typed(LTFile, Asset);
        TILT_ASSERT_TRUE(tilt, s_asset[i] != NULL, "Failed to create asset object");
    }
    const char *seedStr = tilt->API->GetProperty("seed", "0xba5eba11");
    s_seed = lt_strtou64(seedStr, NULL, 16);
    TILT_INFO(tilt, "Using PRNG seed: 0x%llx", LT_Pu64(s_seed));
    /* Create identical PRNGs */
    for (u32 i = 0; i < kNumPRNGs; i++) {
        s_prng[i] = tilt->API->PrngCreate(s_seed, 0);
    }
    /* Extended tests flag */
    const char *extendedTestsStr = tilt->API->GetProperty("extended", "false");
    s_extendedTests = false;
    if (lt_strcmp(extendedTestsStr, "true") == 0) {
        s_extendedTests = true;
    }
}

void AfterAllTestsHook(Tilt *tilt) {
    for (u32 i = 0; i < kNumAssets; i++) {
        lt_destroyobject(s_asset[i]);
    }
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    TILT_ASSERT_TRUE(tilt, settings != NULL, "Failed to open LTSystemSettings library");
    settings->Flush(NULL);
    lt_closelibrary(settings);
}

static int UnitTestLTSystemFSAssetImpl_Run(int argc, const char **argv) {
    static const TiltEngineTest s_tests[] = {
        { OpenCloseTests,         "Open/Close",        "Open/Close Asset Tests",        0 },
        { ReadWriteDeleteTests,   "Read/Write/Delete", "Read/Write/Delete Asset Tests", 0 },
        { MultipleBulkAssetTests, "Multiple Bulk",     "Multiple Bulk Asset Test",      0 },
        { SeekTests,              "Seek",              "Seek Tests",                    0 },
        { OtherTests,             "Other",             "Other Asset Tests",             0 },
        { ExtendedTests,          "Extended",          "Extended Asset Tests",          0 },
    };

    const TiltEngineTestHooks hooks = { BeforeAllTestsHook, AfterAllTestsHook, NULL, NULL };

    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemFSAssetImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemFSAssetImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemFSAsset, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemFSAsset, UnitTestLTSystemFSAssetImpl_Run, 1536) LTLIBRARY_DEFINITION;
