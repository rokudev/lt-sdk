/******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/buffer/LTBuffer.h>
#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

static const char *teststring
    = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et "
      "dolore magna aliqua.";

void Test_WriteRead(Tilt *tilt) {
    u32          testsize = lt_strlen(teststring) + 1;
    LTBuffer    *buffer   = lt_createobject(LTBuffer);
    LTBufferSpan span     = {(u8 *)teststring, testsize};
    LTBufferSpan written  = buffer->API->Write(buffer, span);
    TILT_ASSERT_TRUE(tilt, written.size == testsize, "Write failed");
    TILT_ASSERT_TRUE(tilt, written.data == (u8 *)teststring, "Write span output data is wrong");
    u8 *data          = lt_malloc(testsize);
    span.data         = data;
    span.size         = testsize;
    LTBufferSpan read = buffer->API->Read(buffer, span);
    TILT_ASSERT_TRUE(tilt, read.size == testsize, "Read failed");
    TILT_ASSERT_TRUE(tilt, read.data == data, "Read span output data is wrong");
    TILT_ASSERT_TRUE(tilt, lt_strcmp((const char *)span.data, teststring) == 0, "Read doesn't match write");
    lt_destroyobject(buffer);
    lt_free(data);
}

void Test_StartWriteRead(Tilt *tilt) {
    u32            testsize = lt_strlen(teststring) + 1;
    LTBuffer      *buffer   = lt_createobject(LTBuffer);
    LTBufferAccess write    = buffer->API->StartWrite(buffer, testsize);
    TILT_ASSERT_TRUE(tilt, write.span.size == testsize, "Wrong size span");
    lt_memcpy(write.span.data, teststring, testsize);
    buffer->API->FinishWrite(buffer, write);
    LTBufferAccess read = buffer->API->StartRead(buffer, testsize);
    TILT_ASSERT_TRUE(tilt, read.span.size == testsize, "Write size span");
    TILT_ASSERT_TRUE(tilt, lt_strcmp(teststring, (const char *)read.span.data) == 0, "Read doesn't match write");
    buffer->API->FinishRead(buffer, read);
    lt_destroyobject(buffer);
}

static const TiltEngineTest s_tests[] = {
    { Test_WriteRead,      "WriteRead",      "Write and read from buffer",        0 },
    { Test_StartWriteRead, "StartWriteRead", "Direct write and read from buffer", 0 },
};

static int UnitTestLTUtilityBufferImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTUtilityBufferImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityBufferImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityBuffer, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityBuffer, UnitTestLTUtilityBufferImpl_Run, 1536) LTLIBRARY_DEFINITION;
