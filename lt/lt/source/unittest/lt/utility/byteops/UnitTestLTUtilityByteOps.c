/*******************************************************************************
 * source/unittest/lt/utility/UnitTestLTUtilityByteOps.c
 *    __  __      _ __ ______          __  __  ________  ____  _ ___ __
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ / / / /_(_) (_) /___  __
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / / / / __/ / / / __/ / / /
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /_/ / /_/ / / / /_/ /_/ /
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\__/_/_/_/\__/\__, /
 *                                                                   /____/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <tilt/JiltEngine.h>

/*____________
 / #defines */
#define NUM_RANDOM_BYTES                  4096
#define RANDOMBYTES_DUMP_PER_LINE         64
#define NUM_UUIDS                         (NUM_RANDOM_BYTES/16)

/*____________________
 / Static variables */
static JiltEngine       * s_engine;
static LTCore           * s_pCore;
static LTUtilityByteOps * s_pUtilityByteOps = NULL;
static u32                s_nNumRandomBytes = 0;
static u32                s_nUUIDS          = 0;

u32 s_base64EncodeStringBufferSize = 0;
u32 s_base64DecodeDataBufferSize   = 0;

u8   * s_pRandomBytes       = NULL;
char * s_pEncodedStringBuff = NULL;
u8   * s_pDecodedBytes      = NULL;

/*____________________________________________________
 / UnitTestLTUtilityByteOps static helper functions */
static const char * TimeString(LTTime time) {
    static char buf[32];
    lt_snprintf(buf, sizeof(buf), "[%03u.%06u]",
                (u32)LTTime_GetSeconds(time),
                (u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(time)));
    return buf;  /* works because unit test is single threaded and runs one at a time (don't run simultaneously from network shell and console shell) */
}

static void PrintLineOfBytes(Tilt * tilt, u32 offset, u32 count, u8 * pBytes) {
    static char buf[132];
    char * pBuf = buf;
    u32 nCountOrig = count;
    u8 nybble;
    pBytes += offset;
    while (count--) {
        nybble = (*pBytes >> 4) & 0x0F;
        if (nybble < 10) {
            *pBuf++ = '0' + nybble;
        } else {
            *pBuf++ = 'a' + (nybble - 10);
        }
        nybble = *pBytes & 0x0F;
        if (nybble < 10) {
            *pBuf++ = '0' + nybble;
        } else {
            *pBuf++ = 'a' + (nybble - 10);
        }
        pBytes++;
    }
    *pBuf = 0;
    TILT_DEBUG(tilt, "%04lX-%04lX: %s", offset, offset + (nCountOrig - 1), buf);
}

/*__________________
 / Test functions */

static void BeforeAllTests(Tilt * tilt) {
    s_pCore = LT_GetCore();
    s_pUtilityByteOps = lt_openlibrary(LTUtilityByteOps);
    TILT_ASSERT_TRUE(tilt, s_pUtilityByteOps, "Cannot open library under test!");

    /* Get memReducer property from TILT interface */
    const char *memReducerParam       = tilt->API->GetProperty("memReducer", "1");
    const u32   memoryReductionFactor = lt_strtou32(memReducerParam, NULL, 10);
    s_nNumRandomBytes                 = NUM_RANDOM_BYTES / memoryReductionFactor;
    s_nUUIDS                          = NUM_UUIDS / memoryReductionFactor;

    s_base64EncodeStringBufferSize = s_pUtilityByteOps->GetBase64EncodeBufferRequirement(s_nNumRandomBytes);
    s_base64DecodeDataBufferSize   = s_pUtilityByteOps->GetBase64DecodeBufferRequirement(s_base64EncodeStringBufferSize - 1);

    s_pRandomBytes       = lt_malloc(s_nNumRandomBytes);
    s_pEncodedStringBuff = lt_malloc(s_base64EncodeStringBufferSize);
    s_pDecodedBytes      = lt_malloc(s_base64DecodeDataBufferSize);
    if (!(s_pRandomBytes && s_pEncodedStringBuff && s_pDecodedBytes)) {
        TILT_REPORT_FAILURE(tilt, "Allocation error");
        // AfterAllTests() will always run, so cleanup will happen there
        return;
    }

    TILT_INFO(tilt, "Generating %d random bytes", s_nNumRandomBytes);
    LTTime t = s_pCore->GetKernelTime();
    s_pUtilityByteOps->GenRandomBytes(s_pRandomBytes, s_nNumRandomBytes);
    t = LTTime_Subtract(s_pCore->GetKernelTime(), t);
    TILT_INFO(tilt, "Generated %d random bytes in %s seconds", s_nNumRandomBytes, TimeString(t));
    TILT_DEBUG(tilt, "Your random bytes, Sire:");
    for (u32 nOffset  = 0; nOffset <= (s_nNumRandomBytes - RANDOMBYTES_DUMP_PER_LINE);
        nOffset     += RANDOMBYTES_DUMP_PER_LINE) {
        PrintLineOfBytes(tilt, nOffset, RANDOMBYTES_DUMP_PER_LINE, s_pRandomBytes);
    }
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    if (s_pRandomBytes) {
        lt_free(s_pRandomBytes);
        s_pRandomBytes = NULL;
    }
    if (s_pEncodedStringBuff) {
        lt_free(s_pEncodedStringBuff);
        s_pEncodedStringBuff = NULL;
    }
    if (s_pDecodedBytes) {
        lt_free(s_pDecodedBytes);
        s_pDecodedBytes = NULL;
    }
    lt_closelibrary(s_pUtilityByteOps);
}

static void TestBase64Encode(Tilt * tilt) {
    TILT_INFO(tilt, "%d random bytes requires %lu Base64 encode string buffer bytes and %lu Base64 decode data buffer bytes",
                 s_nNumRandomBytes, LT_Pu32(s_base64EncodeStringBufferSize), LT_Pu32(s_base64DecodeDataBufferSize));
    TILT_INFO(tilt, "Base64 encoding %d random bytes", s_nNumRandomBytes);
    LTTime t = s_pCore->GetKernelTime();
    u32 strLen = s_pUtilityByteOps->Base64Encode(s_pRandomBytes, s_nNumRandomBytes, s_pEncodedStringBuff, s_base64EncodeStringBufferSize);
    t = LTTime_Subtract(s_pCore->GetKernelTime(), t);
    TILT_INFO(tilt, "Encoded %d random bytes into base64 string of length %lu in %s seconds",
                 s_nNumRandomBytes, LT_Pu32(strLen), TimeString(t));
    TILT_INFO(tilt, "Sanity checking base64 encoded string length");
    TILT_ASSERT_FALSE(tilt, strLen == 0, "String length can't be zero");
    TILT_ASSERT_FALSE(tilt, strLen != (s_base64EncodeStringBufferSize - 1), "Bad encoded length");
    TILT_ASSERT_FALSE(tilt, 0 != *(s_pEncodedStringBuff + strLen), "Bad string termination");
    TILT_ASSERT_FALSE(tilt, lt_strlen(s_pEncodedStringBuff) != strLen, "Bad encoded length");
}

static void TestBase64EncodeValue(Tilt * tilt) {
    static u8 testBytes[] = {
        0xf6, 0x24, 0x65, 0x1e, 0x3f, 0x4a, 0x5e, 0x97, 0x1e, 0x87, 0x4a, 0xae, 0x38, 0x47, 0x9c, 0x7f,
        0x33, 0x64, 0x3c, 0x34, 0xc9, 0xe9, 0x28, 0x4e, 0x96, 0x6c, 0x14, 0x17, 0xc1, 0x19, 0x4d, 0xc3,
        0xe4, 0x53, 0xab, 0x29, 0x93, 0x66, 0x5d, 0x87, 0x2f, 0x3e, 0xbe, 0x38, 0xb4, 0xce, 0x1c, 0xdd,
        0x26, 0xf6, 0xd5, 0x87, 0x03, 0x76, 0xd2, 0x5c, 0xcf, 0x95, 0xc4, 0xba, 0xb6, 0xe3, 0xff, 0x11,
        0x9f, 0xbb, 0x16, 0xa3, 0x90, 0xe5, 0x1d, 0x7a, 0x31, 0x87, 0x2d, 0x42, 0x1c, 0x19, 0x8a, 0x4c,
        0x9a, 0x09, 0xa1, 0x72, 0x40, 0xeb, 0x5b, 0xc0, 0x16, 0x1b, 0xee, 0x97, 0xde, 0x8b, 0xd4, 0xed,
        0x5e, 0xe2, 0x1b, 0xca, 0xbe, 0x1e, 0x2c, 0x22, 0x62, 0x9f, 0x40, 0x49, 0xf5, 0x31, 0x70, 0x37,
        0x86, 0x32, 0x9b, 0xb0, 0xa8, 0x47, 0xda, 0x16, 0x4e, 0xe5
    };
    char encodedString[165];
    char *expectedString = "9iRlHj9KXpceh0quOEecfzNkPDTJ6ShOlmwUF8EZTcPkU6spk2Zdhy8+vji0zhzdJvbVhwN20lzPlcS6tuP/EZ+7FqOQ5R16MYctQhwZikyaCaFyQOtbwBYb7pfei9TtXuIbyr4eLCJin0BJ9TFwN4Yym7CoR9oWTuU=";
    u32 strLen = s_pUtilityByteOps->Base64Encode(testBytes, sizeof(testBytes), encodedString, sizeof(encodedString));

    TILT_INFO(tilt, "Sanity checking base64 encoded string length");
    TILT_ASSERT_FALSE(tilt, strLen != 164, "Bad encoded length");
    TILT_ASSERT_FALSE(tilt, 0 != *(encodedString + strLen), "Bad string termination");
    TILT_ASSERT_FALSE(tilt, lt_strlen(encodedString) != strLen, "Bad encoded length");
    TILT_ASSERT_FALSE(tilt, lt_strcmp(encodedString, expectedString) != 0, "Bad encoded value");
}

static void TestBase64Decode(Tilt * tilt) {
    TILT_INFO(tilt, "Decoding base64 encoded string");
    u32 strLen = s_pUtilityByteOps->Base64Encode(s_pRandomBytes, s_nNumRandomBytes, s_pEncodedStringBuff, s_base64EncodeStringBufferSize);
    LTTime t = s_pCore->GetKernelTime();
    u32 nDecodedBytes = s_pUtilityByteOps->Base64Decode(s_pEncodedStringBuff, strLen, s_pDecodedBytes, s_base64DecodeDataBufferSize);
    t = LTTime_Subtract(s_pCore->GetKernelTime(), t);
    TILT_INFO(tilt, "Decoded %lu bytes from base64 string of len %lu in %s seconds",
                 LT_Pu32(nDecodedBytes), LT_Pu32(strLen), TimeString(t));
    TILT_INFO(tilt, "Comparing base64 encoded/decoded buffer with original random bytes");
    TILT_ASSERT_FALSE(tilt, nDecodedBytes != s_nNumRandomBytes, "Bad decoded byte count");
    TILT_ASSERT_FALSE(tilt, 0 != lt_memcmp(s_pRandomBytes, s_pDecodedBytes, s_nNumRandomBytes), "Bad decoded bytes");
}

static void TestBase64DecodeOverflow(Tilt * tilt) {
    /* Ensure that decoding into buffers of the exact required length is safe against buffer overflows. */

    struct TestEncoding {
        const char *encodedString;
        u32 expectedDecodeLen;
    };
    /* Base 64 decoding decodes blocks of 4 base 64 encoded characters into 3 bytes at a time. Decoding incomplete
     * bytes is not permitted so there are only 3 cases to cover. */
    struct TestEncoding overflowTestCases[3] = {
        /* Case 1: Decoding to n≅0mod3 bytes; 6 * 8 = 48 bits of valid data, should decode to 6 bytes of valid data */
        { .encodedString = "lLz5UVjI", .expectedDecodeLen = 6 },
        /* Case 2: Decoding to n≅2mod3 bytes; 6 * 7 = 42 bits of valid data, should decode to 5 bytes of valid data */
        { .encodedString = "nHnXfzo=", .expectedDecodeLen = 5 },
        /* Case 3: Decoding to n≅1mod3 bytes; 6 * 6 = 36 bits of valid data, should decode to 4 bytes of valid data */
        { .encodedString = "qNb7UA==", .expectedDecodeLen = 4 },
    };

    TILT_INFO(tilt, "Decoding base64 encoded strings to test against buffer overflow");
    /* Overflow of 0 is common, so use 0xff as test value for data integrity. None of the test strings should
     * decode to any '0xff' bytes. */
    u8 canaryValue = 0xff;
    for (u32 caseNum = 0; caseNum < sizeof(overflowTestCases)/sizeof(overflowTestCases[0]); caseNum++) {
        /* Reset decode buffer using canary value. */
        u8 decodeBuffer[8];
        lt_memset(decodeBuffer, canaryValue, sizeof(decodeBuffer));
        struct TestEncoding currCase = overflowTestCases[caseNum];

        /* Perform decode and check claimed decode length is correct. */
        u32 nDecodedBytes = s_pUtilityByteOps->Base64Decode(currCase.encodedString, lt_strlen(currCase.encodedString), decodeBuffer, currCase.expectedDecodeLen); // Claim that decodeBuffer only has the minimum space for the decoding
        TILT_EXPECT_TRUE(tilt, nDecodedBytes == currCase.expectedDecodeLen, "Expected decode len: %lu, actual: %lu, test case %lu", LT_Pu32(currCase.expectedDecodeLen), LT_Pu32(nDecodedBytes), LT_Pu32(caseNum));

        /* Check decoded output did not overflow expected length. */
        bool bOverflow = false;
        for (u32 byteIndex = currCase.expectedDecodeLen; byteIndex < sizeof(decodeBuffer); byteIndex++) {
            if (decodeBuffer[byteIndex] != canaryValue) { bOverflow = true; }
        }
        TILT_EXPECT_FALSE(tilt, bOverflow, "Decode buffer overflowed during test case %lu", LT_Pu32(caseNum));
    }
}

static void TestUUID(Tilt * tilt) {
    TILT_INFO(tilt, "Generating %d UUIDs", s_nUUIDS);
    u32 i = s_nUUIDS;
    u8 * pUUID = s_pRandomBytes;
    LTTime t = s_pCore->GetKernelTime();
    while (i--) {
        s_pUtilityByteOps->GenUUID(pUUID);
        pUUID += 16;
    }
    t = LTTime_Subtract(s_pCore->GetKernelTime(), t);
    TILT_INFO(tilt, "Generated %d UUIDs in %s seconds", s_nUUIDS, TimeString(t));
    TILT_INFO(tilt, "Converting UUIDs to string and back");
    TILT_DEBUG(tilt, "Your UUIDs, Sire:");
    for (i = 1, pUUID = s_pRandomBytes; i <= s_nUUIDS; i++, pUUID += 16) {
        TILT_ASSERT_TRUE(tilt, s_pUtilityByteOps->UUIDToString(pUUID, s_pEncodedStringBuff, 37), "Bad UUID->String");
        TILT_DEBUG(tilt, "UUID %3lu: %s", LT_Pu32(i), s_pEncodedStringBuff);
        TILT_ASSERT_TRUE(tilt, s_pUtilityByteOps->StringToUUID(s_pEncodedStringBuff, s_pDecodedBytes), "Bad String->UUID");
        TILT_ASSERT_TRUE(tilt, 0 == lt_memcmp(pUUID, s_pDecodedBytes, 16), "Bad decoded bytes");
    }
}

static void TestCRC32(Tilt * tilt) {
    u8 data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    u32 crc = 0;
    s_pUtilityByteOps->Crc32(data, sizeof(data), &crc);
    TILT_ASSERT_TRUE(tilt, crc == 0x2520577b, "Bad CRC-32");
    crc = 0;
    s_pUtilityByteOps->Crc32(data, 3, &crc);
    s_pUtilityByteOps->Crc32(data + 3, sizeof(data) - 3, &crc);
    TILT_ASSERT_TRUE(tilt, crc == 0x2520577b, "Bad CRC-32");
}

/*__________________________
 / Library initialization */

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestBase64Encode,         "Base64Encode",         "Base64 encode length",           0 },
    { TestBase64EncodeValue,    "Base64EncodeValue",    "Base64 encode value",            0 },
    { TestBase64Decode,         "Base64Decode",         "Base64 decode length and value", 0 },
    { TestBase64DecodeOverflow, "Base64DecodeOverflow", "Base64 decode overflow test",    0 },
    { TestUUID,                 "UUID",                 "UUID creation and conversion",   0 },
    { TestCRC32,                "CRC32",                "CRC-32 calculation",             0 },
};

static int UnitTestLTUtilityByteOpsImpl_Run(int argc, const char **argv) {
    /* Set test properties and stack size */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilityByteOpsImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityByteOpsImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

/*__________________________________
 / Library root interface binding */

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityByteOps, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityByteOps, UnitTestLTUtilityByteOpsImpl_Run, 1536) LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 */
