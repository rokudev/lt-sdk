/*******************************************************************************
 * LTSystemSettings Unit Test
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/system/settings/LTSystemSettings.h>

#include <tilt/JiltEngine.h>

static JiltEngine       * s_engine;
static LTSystemSettings * s_pSettings = NULL;

enum { kTestArrayIdxCount = 4 };
static int s_nTestCounter[kTestArrayIdxCount];

/* String savers */
#define STR_CANT_SET_VAL       "Can't set value"
#define STR_CANT_GET_VAL       "Can't get value"
#define STR_CANT_GET_KEYS      "Can't get keys"
#define STR_CANT_DELETE        "Can't delete value"
#define STR_CANT_MALLOC        "Can't malloc"
#define STR_SHOULDNT_CONVERT   "Shouldn't convert types"
#define STR_UNEXPECTED_VAL     "Unexpected value"
#define STR_VAL_NOT_DELETED    "Value not deleted"
#define STR_TYPE_MISMATCH      "Type mismatch"
#define STR_KEY_SHOULDNT_EXIST "Key shouldn't exist"
#define STR_DEL_PREFIX_FAILED  "Delete prefix failed"

static void SetterGetterTest(Tilt * tilt) {
    /* 1. Integer value */
    {
        const char * pName = "test/int";
        s64 nValue = 0;
        s64 nExpValue = 0xba5eba1111abe5ab;
        TILT_ASSERT_TRUE(tilt, s_pSettings->SetIntegerValue(pName, nExpValue), STR_CANT_SET_VAL);
        TILT_ASSERT_TRUE(tilt, s_pSettings->GetIntegerValue(pName, &nValue), STR_CANT_GET_VAL);
        TILT_ASSERT_TRUE(tilt, nValue == nExpValue, STR_UNEXPECTED_VAL);
        LTString pValue = NULL;
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue(pName, &pValue), STR_SHOULDNT_CONVERT);
        TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSetting(pName), STR_CANT_DELETE);
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetIntegerValue(pName, &nValue), STR_VAL_NOT_DELETED);
    }

    /* 2a. C string value */
    {
        const char * pName = "test/cstr";
        const char * pExpValue = "test";
        LTString pValue = NULL;
        TILT_ASSERT_TRUE(tilt, s_pSettings->SetStringValue(pName, pExpValue), STR_CANT_SET_VAL);
        TILT_ASSERT_TRUE(tilt, s_pSettings->GetStringValue(pName, &pValue), STR_CANT_GET_VAL);
        TILT_ASSERT_TRUE(tilt, lt_strcmp(pValue, pExpValue) == 0, STR_UNEXPECTED_VAL);
        s64 nValue;
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetIntegerValue(pName, &nValue), STR_SHOULDNT_CONVERT);
        TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSetting(pName), STR_CANT_DELETE);
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue(pName, &pValue), STR_VAL_NOT_DELETED);
        ltstring_destroy(pValue);
    }

    /* 2b. LT String value */
    {
        LTString pName     = ltstring_create("test/ltstr");
        LTString pExpValue = ltstring_create("test_value");
        LTString pValue    = ltstring_create("xyzzy");
        TILT_ASSERT_TRUE(tilt, s_pSettings->SetStringValue(pName, pExpValue), STR_CANT_SET_VAL);
        TILT_ASSERT_TRUE(tilt, s_pSettings->GetStringValue(pName, &pValue), STR_CANT_GET_VAL);
        TILT_ASSERT_TRUE(tilt, lt_strcmp(pValue, pExpValue) == 0, STR_UNEXPECTED_VAL);
        TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSetting(pName), STR_CANT_DELETE);
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue(pName, &pValue), STR_VAL_NOT_DELETED);
        ltstring_destroy(pName);
        ltstring_destroy(pExpValue);
        ltstring_destroy(pValue);
    }

    /* 3. Binary value */
    {
        const u32 nValueSize = 128;
        LTString pName = ltstring_create("test/ltbin");
        u8 * pExpValue = lt_malloc(2 * nValueSize);
        u8 * pValue = pExpValue + nValueSize;
        TILT_ASSERT_TRUE(tilt, pExpValue, STR_CANT_MALLOC);
        for (u32 nIx = 0; nIx < nValueSize; nIx++) pExpValue[nIx] = nIx;
        TILT_ASSERT_TRUE(tilt, s_pSettings->SetBinaryValue(pName, pExpValue, nValueSize), STR_CANT_SET_VAL);
        u32 nSizeInBytes = nValueSize;
        TILT_ASSERT_TRUE(tilt, s_pSettings->GetBinaryValue(pName, pValue, &nSizeInBytes), STR_CANT_GET_VAL);
        TILT_ASSERT_TRUE(tilt, nSizeInBytes == nValueSize, "Bad value size");
        for (u32 nIx = 0; nIx < nValueSize; nIx++) {
            TILT_ASSERT_TRUE(tilt, pValue[nIx] == pExpValue[nIx], STR_UNEXPECTED_VAL);
        }
        nSizeInBytes = nValueSize - 1;
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetBinaryValue(pName, pValue, &nSizeInBytes), "Can't get what doesn't fit");
        TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSetting(pName), STR_CANT_DELETE);
        TILT_ASSERT_FALSE(tilt, s_pSettings->GetBinaryValue(pName, pValue, &nSizeInBytes), STR_VAL_NOT_DELETED);
        ltstring_destroy(pName);
        lt_free(pExpValue);
    }
}

static bool AddCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    LT_UNUSED(pKey); LT_UNUSED(type);
    Tilt * tilt = (Tilt *)pClientData;
    if (lt_strcmp(pKeySuffix, "integer") == 0) {
        if (!s_pSettings->SetStringValue("test/prefix/string3", "again")) {
            TILT_REPORT_FAILURE(tilt, STR_CANT_SET_VAL);
        }
    }
    if (lt_strcmp(pKeySuffix, "string3") == 0) {
        TILT_REPORT_FAILURE(tilt, "Should not enumerate");
    }
    return true;
}

static bool DeleteCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    LT_UNUSED(pKeySuffix); LT_UNUSED(type);
    Tilt * tilt = (Tilt *)pClientData;
    if (!s_pSettings->DeleteSetting(pKey)) {
        TILT_REPORT_FAILURE(tilt, "Can't delete %s", pKey);
    }
    return true;
}

static bool TerminateCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    LT_UNUSED(pKey); LT_UNUSED(type); LT_UNUSED(pClientData);
    if (lt_strcmp(pKeySuffix, "string1") == 0) return false;
    return true;
}

static bool ReadCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    Tilt * tilt = (Tilt *)pClientData;
    LTString pString = ltstring_create("test.ltbin");
    if (lt_strcmp("test/prefix/string1", pKey) == 0) {
        if (lt_strcmp("string1", pKeySuffix) != 0) TILT_REPORT_FAILURE(tilt, "Bad suffix");
        if (s_pSettings->GetStringValue(pKey, &pString) == false) TILT_REPORT_FAILURE(tilt, STR_CANT_GET_VAL);
        if (type != kLTSystemSettingsDataType_String) TILT_REPORT_FAILURE(tilt, STR_TYPE_MISMATCH);
        if (lt_strcmp(pString, "hello") != 0) TILT_REPORT_FAILURE(tilt, STR_UNEXPECTED_VAL);
        s_nTestCounter[0]--;
    } else if (lt_strcmp("test/prefix/string2", pKey) == 0) {
        if (lt_strcmp("string2", pKeySuffix) != 0) TILT_REPORT_FAILURE(tilt, "Bad suffix");
        if (s_pSettings->GetStringValue(pKey, &pString) == false) TILT_REPORT_FAILURE(tilt, STR_CANT_GET_VAL);
        if (type != kLTSystemSettingsDataType_String) TILT_REPORT_FAILURE(tilt, STR_TYPE_MISMATCH);
        if (lt_strcmp(pString, "world") != 0) TILT_REPORT_FAILURE(tilt, STR_UNEXPECTED_VAL);
        s_nTestCounter[1]--;
    } else if (lt_strcmp("test/prefix/integer", pKey) == 0) {
        if (lt_strcmp("integer", pKeySuffix) != 0) TILT_REPORT_FAILURE(tilt, "Bad suffix");
        s64 nTemp = 0;
        if (s_pSettings->GetIntegerValue(pKey, &nTemp) == false) TILT_REPORT_FAILURE(tilt, STR_CANT_GET_VAL);
        if (type != kLTSystemSettingsDataType_Integer) TILT_REPORT_FAILURE(tilt, STR_TYPE_MISMATCH);
        if (nTemp != 37) TILT_REPORT_FAILURE(tilt, STR_UNEXPECTED_VAL);
        s_nTestCounter[2]--;
    } else if (lt_strcmp("test/prefix/string3", pKey) == 0) {
        if (lt_strcmp("string3", pKeySuffix) != 0) TILT_REPORT_FAILURE(tilt, "Bad suffix");
        if (s_pSettings->GetStringValue(pKey, &pString) == false) TILT_REPORT_FAILURE(tilt, STR_CANT_GET_VAL);
        if (type != kLTSystemSettingsDataType_String) TILT_REPORT_FAILURE(tilt, STR_TYPE_MISMATCH);
        if (lt_strcmp(pString, "again") != 0) TILT_REPORT_FAILURE(tilt, STR_UNEXPECTED_VAL);
        s_nTestCounter[3]--;
    } else {
        TILT_REPORT_FAILURE(tilt, "Unknown setting found %s", pKey);
    }
    ltstring_destroy(pString);
    return true;
}

static void EnumerateTest(Tilt * tilt) {
    s_pSettings->SetStringValue("test/prefix/string1", "hello");
    s_pSettings->SetStringValue("test/prefix/string2", "world");
    s_pSettings->SetIntegerValue("test/prefix/integer", 37);
    s_pSettings->SetIntegerValue("test/prefixinteger2", 237);

    /* 1. Value read */
    for (u32 nIdx = 0; nIdx < kTestArrayIdxCount - 1; nIdx++) s_nTestCounter[nIdx] = 1;
    s_nTestCounter[kTestArrayIdxCount - 1] = 0;
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", ReadCallback, (void *)tilt), STR_CANT_GET_KEYS);
    for (u32 nIdx = 0; nIdx < kTestArrayIdxCount; nIdx++) {
        TILT_ASSERT_TRUE(tilt, s_nTestCounter[nIdx] == 0, "Enumerate failed");
        /* Set test array check for next test */
        s_nTestCounter[nIdx] = 1;
    }

    /* 2. Early termination */
    TILT_ASSERT_FALSE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", TerminateCallback, (void *)tilt), "Can't terminate");

    /* 3. Add during enumeration */
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", AddCallback, (void *)tilt), STR_CANT_GET_KEYS);
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", ReadCallback, (void *)tilt), STR_CANT_GET_KEYS);
    for (u32 nIdx = 0; nIdx < kTestArrayIdxCount; nIdx++) {
        TILT_ASSERT_TRUE(tilt, s_nTestCounter[nIdx] == 0, "Add failed");
        s_nTestCounter[nIdx] = 0;
    }

    /* 4. Delete during enumeration */
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", DeleteCallback, (void *)tilt), STR_CANT_GET_KEYS);
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("test/prefix/", ReadCallback, (void *)tilt), STR_CANT_GET_KEYS);
    for (u32 nIdx = 0; nIdx < kTestArrayIdxCount; nIdx++) {
        TILT_ASSERT_TRUE(tilt, s_nTestCounter[nIdx] == 0, STR_VAL_NOT_DELETED);
    }
    LTString pString = NULL;
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/prefix/string1", &pString), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/prefix/string2", &pString), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/prefix/string3", &pString), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/prefix/integer", &pString), STR_KEY_SHOULDNT_EXIST);
    ltstring_destroy(pString);

    /* 5. Check the setting with non-matching prefix */
    s64 nTemp;
    TILT_ASSERT_TRUE(tilt, s_pSettings->GetIntegerValue("test/prefixinteger2", &nTemp), STR_CANT_GET_VAL);
    TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSetting("test/prefixinteger2"), STR_VAL_NOT_DELETED);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetIntegerValue("test/prefixinteger2", &nTemp), STR_VAL_NOT_DELETED);
}

/* Write Cache Tests (including delete by prefix) */
static void WriteCacheTest(Tilt * tilt) {
    LTString pValue = NULL;
    s_pSettings->SetStringValue("test/cache/", "test");
    s_pSettings->SetStringValue("test/cache/one", "one");
    s_pSettings->SetStringValue("test/cacher", "test");
    s_pSettings->SetStringValue("test/cash", "five");
    s_pSettings->SetStringValue("test/cache/two", "two");
    s_pSettings->SetStringValue("test/cache/three", "three");
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cache/cstr", &pValue), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_TRUE(tilt, s_pSettings->GetStringValue("test/cache/", &pValue), STR_CANT_GET_VAL);
    TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSettingsWithPrefix("test/cache/"), STR_DEL_PREFIX_FAILED);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cache/", &pValue), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cache/two", &pValue), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_TRUE(tilt, s_pSettings->GetStringValue("test/cacher", &pValue), STR_CANT_GET_VAL);
    TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSettingsWithPrefix("test/cacher"), STR_DEL_PREFIX_FAILED);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cacher", &pValue), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_TRUE(tilt, s_pSettings->GetStringValue("test/cash", &pValue), STR_CANT_GET_VAL);
    TILT_ASSERT_TRUE(tilt, s_pSettings->DeleteSettingsWithPrefix("test/cash"), STR_DEL_PREFIX_FAILED);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cash", &pValue), STR_KEY_SHOULDNT_EXIST);
    ltstring_destroy(pValue);
    TILT_INFO(tilt, "6 second sleep to flush settings");
    ILTThread * pIThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    pIThread->Sleep(LTTime_Seconds(6));
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cacher", &pValue), STR_KEY_SHOULDNT_EXIST);
    TILT_ASSERT_FALSE(tilt, s_pSettings->GetStringValue("test/cash", &pValue), STR_KEY_SHOULDNT_EXIST);
}

static bool EraseCallback(const char * pKey, const char * pKeySuffix, LTSystemSettingsDataType type, void * pClientData) {
    LT_UNUSED(pKey); LT_UNUSED(pKeySuffix); LT_UNUSED(type); LT_UNUSED(pClientData);
    return false;
}

/* Extended test (test is normally disabled in automation to prevent excessive flash wear, test also takes a fair amount of time). */
static void ExtendedTest(Tilt * tilt) {
    const char *extendedStr = tilt->API->GetProperty("extended", "false");
    if (lt_strcmp(extendedStr, "true") != 0) {
        TILT_REPORT_CANNOT_RUN(tilt, "Extended test disabled");
        return;
    }

    ILTThread * pIThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    TILT_ASSERT_TRUE(tilt, pIThread, "Can't get interface");

    /* 0. Erase Settings */
    TILT_ASSERT_TRUE(tilt, s_pSettings->EraseSection(""), "Can't erase section");
    TILT_ASSERT_TRUE(tilt, s_pSettings->EnumerateSettingsWithPrefix("", EraseCallback, NULL), STR_KEY_SHOULDNT_EXIST);

    /* 1. Write Settings */
    const u32 nValueSize = 128;
    u8 * pExpBinary = lt_malloc(2 * nValueSize);
    TILT_ASSERT_TRUE(tilt, pExpBinary, STR_CANT_MALLOC);
    u8 * pBinary = pExpBinary + nValueSize;
    for (u32 nIx = 0; nIx < nValueSize; nIx++) pExpBinary[nIx] = nIx;
    TILT_EXPECT_TRUE(tilt, s_pSettings->SetBinaryValue("test/extended/binary", pExpBinary, nValueSize), STR_CANT_SET_VAL);
    s64 nExpValue = 0x0ca1191111911ac0;
    TILT_EXPECT_TRUE(tilt, s_pSettings->SetIntegerValue("test/extended/int", nExpValue), STR_CANT_SET_VAL);
    const char * pExpString = "my_string";
    TILT_EXPECT_TRUE(tilt, s_pSettings->SetStringValue("test/extended/string", pExpString), STR_CANT_SET_VAL);
    TILT_EXPECT_TRUE(tilt, s_pSettings->Flush(NULL), "flush");

    /* 2. Verify Settings */
    u32 nSizeInBytes = nValueSize;
    TILT_EXPECT_TRUE(tilt, s_pSettings->GetBinaryValue("test/extended/binary", pBinary, &nSizeInBytes), STR_CANT_GET_VAL);
    s64 nTemp;
    TILT_EXPECT_TRUE(tilt, s_pSettings->GetIntegerValue("test/extended/int", &nTemp), STR_CANT_GET_VAL);
    TILT_EXPECT_TRUE(tilt, nTemp == nExpValue, STR_UNEXPECTED_VAL);
    LTString pString = ltstring_create("xyzzy");
    TILT_EXPECT_TRUE(tilt, s_pSettings->GetStringValue("test/extended/string", &pString), STR_CANT_GET_VAL);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(pString, pExpString) == 0, STR_UNEXPECTED_VAL);

    /* 3. Write More Settings */
    nExpValue++;
    TILT_EXPECT_TRUE(tilt, s_pSettings->SetIntegerValue("test/extended/int", nExpValue), STR_CANT_SET_VAL);

    /* 4. Verify More Settings */
    TILT_EXPECT_TRUE(tilt, s_pSettings->GetIntegerValue("test/extended/int", &nTemp), STR_CANT_GET_VAL);
    TILT_EXPECT_TRUE(tilt, nTemp == nExpValue, STR_UNEXPECTED_VAL);
    TILT_INFO(tilt, "6 second sleep");
    pIThread->Sleep(LTTime_Seconds(6));

    /* 5. Delete Settings */
    TILT_EXPECT_TRUE(tilt, s_pSettings->DeleteSettingsWithPrefix("test/extended"), STR_DEL_PREFIX_FAILED);
    TILT_INFO(tilt, "6 second sleep");
    pIThread->Sleep(LTTime_Seconds(6));

    /* 6. Verify deletion */
    TILT_EXPECT_FALSE(tilt, s_pSettings->GetBinaryValue("test/extended/binary", pBinary, &nSizeInBytes), STR_KEY_SHOULDNT_EXIST);
    TILT_EXPECT_FALSE(tilt, s_pSettings->GetIntegerValue("test/extended/int", &nTemp), STR_KEY_SHOULDNT_EXIST);
    TILT_EXPECT_FALSE(tilt, s_pSettings->GetStringValue("test/extended/string", &pString), STR_KEY_SHOULDNT_EXIST);

    ltstring_destroy(pString);
    lt_free(pExpBinary);
}

static void BeforeAllTestsHook(Tilt *tilt) {
    s_pSettings = (LTSystemSettings *)LT_GetCore()->OpenLibrary("LTSystemSettings");
    TILT_ASSERT_TRUE(tilt, s_pSettings, "Cannot open library under test");
}

static void AfterAllTestsHook(Tilt *tilt) {
    LT_UNUSED(tilt);
    LT_GetCore()->CloseLibrary((LTLibrary *)s_pSettings);
}

static int UnitTestLTSystemSettingsImpl_Run(int argc, const char **argv) {
    static const TiltEngineTest s_tests[] = {
        { SetterGetterTest, "Set/Get/Delete",    "Test Set/Get/Delete",       0 },
        { EnumerateTest,    "Enumerate",         "Test Enumerate API",        0 },
        { WriteCacheTest,   "Write Cache",       "Write Cache Focused Tests", 0 },
        { ExtendedTest,     "Extended Test",     "Extended Setting Tests",    kTiltEngineFlag_NoAutomation },
    };

    const TiltEngineTestHooks hooks = { BeforeAllTestsHook, AfterAllTestsHook, NULL, NULL };
    
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemSettingsImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemSettingsImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemSettings, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemSettings, UnitTestLTSystemSettingsImpl_Run, 1536) LTLIBRARY_DEFINITION;
