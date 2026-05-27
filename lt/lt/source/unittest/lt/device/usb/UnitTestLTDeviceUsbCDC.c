/*******************************************************************************
 * LTDeviceUsbCDC Unit Test
 *
 * Test the USB-CDC implementation by sending data and reading it back.
 *
 * This set requires that the remote endpoint is started and echoing all the data.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/usb/LTDeviceUsbCDC.h>
#include <lt/core/LTCore.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <tilt/JiltEngine.h>

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static struct Statics {
    LTUtilityByteOps    *byteOps;
    Tilt                *tilt;
    const char          *currentTest;

    LTDeviceUsbCDC      *usbCDC;
    u32                  fifoSize;

    u8                  *testData;
    u32                  testDataSize;
    u32                  testDataWritten;
    u8                  *testBuffer;
    u32                  testBufferRead;

    u32                  maxWriteSize;
    u32                  maxReadSize;
} S;

static JiltEngine *s_engine;

/*******************************************************************************
 * Callback Functions
 ******************************************************************************/

static void BufferDiff(u8* buffer0, u8* buffer1, u32 offset, u32 size) {
    u32 max_diffs = 10;
    TILT_INFO(S.tilt, "offset write                 read");
    while (size > 4) {
        if (lt_memcmp(buffer0 + offset, buffer1 + offset, 4)) {
            if (! max_diffs--) {
                TILT_INFO(S.tilt, "...");
                return;
            }
            TILT_INFO(S.tilt, "%5d: 0x%02x 0x%02x 0x%02x 0x%02x | 0x%02x 0x%02x 0x%02x 0x%02x",
                offset,
                (unsigned)buffer0[offset], (unsigned)buffer0[offset+1], (unsigned)buffer0[offset+2], (unsigned)buffer0[offset+3],
                (unsigned)buffer1[offset], (unsigned)buffer1[offset+1], (unsigned)buffer1[offset+2], (unsigned)buffer1[offset+3]);
            offset += 4;
            size -= 4;
        }
    }

    if (size && lt_memcmp(buffer0 + offset, buffer1 + offset, size)) {
        if (size == 3) {
            TILT_INFO(S.tilt, "%5d: 0x%02x 0x%02x 0x%02x      | 0x%02x 0x%02x 0x%02x",
                offset,
                (unsigned)buffer0[offset], (unsigned)buffer0[offset+1], (unsigned)buffer0[offset+2],
                (unsigned)buffer1[offset], (unsigned)buffer1[offset+1], (unsigned)buffer1[offset+2]);
        } else if (size == 2) {
            TILT_INFO(S.tilt, "%5d: 0x%02x 0x%02x           | 0x%02x 0x%02x",
                offset,
                (unsigned)buffer0[offset], (unsigned)buffer0[offset+1],
                (unsigned)buffer1[offset], (unsigned)buffer1[offset+1]);
        } else if (size == 1) {
            TILT_INFO(S.tilt, "%5d: 0x%02x               | 0x%02x",
                offset,
                (unsigned)buffer0[offset],
                (unsigned)buffer1[offset]);
        }
    }
}

static bool GenerateTestData(u32 size) {
    S.testDataSize = size;
    S.testData = lt_malloc(S.testDataSize);
    if (!S.testData) {
        return false;
    }

    S.byteOps->GenRandomBytes(S.testData, size);
    return true;
}

static void Cleanup(void) {
    if (S.testData) {
        TILT_INFO(S.tilt, "cleaning up CDC device");
        S.usbCDC->API->Stop(S.usbCDC);

        lt_free(S.testData);
        S.testData = NULL;
    }
    if (S.testBuffer) {
        lt_free(S.testBuffer);
        S.testBuffer = NULL;
    }
}

static void Finish(void) {
    Cleanup();
    s_engine->API->SignalTestCompletion(s_engine, S.currentTest);
}

static void TimeoutCleanup(Tilt *tilt, void *clientData) {
    LT_UNUSED(tilt);
    LT_UNUSED(clientData);
    Cleanup();
}

static void WriteHandler(void *clientData) {
    LT_UNUSED(clientData);
    if (!S.testData) {
        // stray callback after cleanup
        return;
    }

    if (S.testDataWritten == S.testDataSize) {
        TILT_INFO(S.tilt, "Write already complete");
        return;
    }

    while (S.testDataWritten < S.testDataSize) {
        LT_SIZE writeSize = S.testDataSize - S.testDataWritten;
        // clamp down if requested
        if (S.maxWriteSize && writeSize > S.maxWriteSize) {
            writeSize = S.maxWriteSize;
        }
        s32 written = S.usbCDC->API->Write(S.usbCDC, S.testData + S.testDataWritten, writeSize);
        if (written < 0) {
            TILT_REPORT_FAILURE(S.tilt, "Cannot write to CDC device");
            Finish();
            return;
        } else if (written == 0) {
            TILT_INFO(S.tilt, "Write test data: busy");
            // wait a bit, as the interrupt has a tendency to fire non-stop and starve the CPU
            LT_GetCore()->GetCurrentThreadObject()->API->Sleep(LTTime_Milliseconds(10));
        } else if  (written < 4) {
            TILT_INFO(S.tilt, "Write test data: %d", (int)written);
        } else {
            TILT_INFO(S.tilt, "Write test data: %d: %02x%02x%02x%02x",
                (int)written,
                (u8)S.testData[S.testDataWritten], (u8)S.testData[S.testDataWritten+1], (u8)S.testData[S.testDataWritten+2], (u8)S.testData[S.testDataWritten+3]);
        }

        S.testDataWritten += (u32)written;
    }

    TILT_EXPECT_FALSE(S.tilt, S.testDataWritten > S.testDataSize, "Wrote too much");
    if (S.testDataWritten > S.testDataSize) {
        Finish();
    }

}

static void ReadHandler(void *clientData) {
    LT_UNUSED(clientData);
    if (!S.testData) {
        // stray callback after cleanup
        return;
    }

    if (S.testBufferRead == S.testDataSize) {
        TILT_INFO(S.tilt, "Read already complete");
        return;
    }

    if (!S.testBuffer) {
        TILT_INFO(S.tilt, "First read, allocating buffer");
        S.testBuffer = lt_malloc(S.testDataSize);
        S.testBufferRead = 0;
    }

    while (S.testBufferRead < S.testDataSize) {
        LT_SIZE readeSize = S.testDataSize - S.testBufferRead;
        // clamp down if requested
        if (S.maxReadSize && readeSize > S.maxReadSize) {
            readeSize = S.maxReadSize;
        }
        s32 read = S.usbCDC->API->Read(S.usbCDC, S.testBuffer + S.testBufferRead, readeSize);
        if (read < 0) {
            TILT_REPORT_FAILURE(S.tilt, "Cannot read from CDC device");
            Finish();
            return;
        } else if (read == 0) {
            TILT_INFO(S.tilt, "Read test data: busy");
            // wait a bit, as the interrupt has a tendency to fire non-stop and starve the CPU
            LT_GetCore()->GetCurrentThreadObject()->API->Sleep(LTTime_Milliseconds(10));
        } else if  (read < 4) {
            TILT_INFO(S.tilt, "Read test data: %d", (int)read);
        } else {
            TILT_INFO(S.tilt, "Read test data: %d: %02x%02x%02x%02x",
                (int)read,
                (u8)S.testBuffer[S.testBufferRead], (u8)S.testBuffer[S.testBufferRead+1], (u8)S.testBuffer[S.testBufferRead+2], (u8)S.testBuffer[S.testBufferRead+3]);
        }

        if (lt_memcmp(S.testData + S.testBufferRead, S.testBuffer + S.testBufferRead, (u32)read)) {
            TILT_REPORT_FAILURE(S.tilt, "Corrupted read from CDC device after read offset %u read size %u", (unsigned)S.testBufferRead, (unsigned)read);
            BufferDiff(S.testData, S.testBuffer, S.testBufferRead, read);
            // carry on and finish the test
        }

        S.testBufferRead += (u32)read;
    }

    if (S.testBufferRead > S.testDataSize) {
        TILT_REPORT_FAILURE(S.tilt, "Read too much");
        Finish();
        return;
    }

    if (S.testBufferRead == S.testDataSize) {
        TILT_INFO(S.tilt, "Last packet received");
        Finish();
    }
}

static void ErrorHandler(void *clientData) {
    LT_UNUSED(clientData);
    if (!S.testData) {
        // stray callback after cleanup
        return;
    }

    TILT_REPORT_FAILURE(S.tilt, "USB error");
    Finish();
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

static void TestIO(Tilt *tilt) {
    S.currentTest = "TestIO";
    S.testData = (u8*)lt_strdup("Hello World!\n");
    S.testDataSize = lt_strlen((char*)S.testData);

    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Start(S.usbCDC), "Cannot start CDC device");

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);
}

static void TestIOLarge(Tilt *tilt) {
    S.currentTest = "TestIOLarge";

    TILT_ASSERT_TRUE(tilt, GenerateTestData(S.fifoSize * 16), "Cannot generate test data");
    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Start(S.usbCDC), "Cannot start CDC device");

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);
}

static void TestShortWrites(Tilt *tilt) {
    S.currentTest = "TestShortWrites";

    S.maxWriteSize = S.fifoSize/5;
    TILT_ASSERT_TRUE(tilt, S.maxWriteSize > 0, "Write FIFO too small: %u", S.fifoSize);
    TILT_INFO(tilt, "Testing with write size %u", S.maxWriteSize);
    TILT_ASSERT_TRUE(tilt, GenerateTestData(S.fifoSize * 2), "Cannot generate test data");
    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Start(S.usbCDC), "Cannot start CDC device");

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);
}

static void TestShortReads(Tilt *tilt) {
    S.currentTest = "TestShortReads";

    S.maxReadSize = S.fifoSize/5;
    TILT_ASSERT_TRUE(tilt, S.maxReadSize > 0, "Read FIFO too small: %u", S.fifoSize);
    TILT_INFO(tilt, "Testing with read size %u", S.maxReadSize);
    TILT_ASSERT_TRUE(tilt, GenerateTestData(S.fifoSize * 2), "Cannot generate test data");
    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Start(S.usbCDC), "Cannot start CDC device");

    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(60), TimeoutCleanup, (void*)tilt);

    WriteHandler(NULL);
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics) {};
    S.tilt = tilt;
    S.byteOps = lt_openlibrary(LTUtilityByteOps);
    TILT_EXPECT_TRUE(tilt, S.byteOps != NULL, "Cannot open LTUtilityByteOps");

    S.usbCDC = lt_createobject(LTDeviceUsbCDC);

    TILT_ASSERT_TRUE(tilt, S.usbCDC != NULL, "Cannot create CDC device");

    TILT_ASSERT_TRUE(tilt, S.usbCDC->API->Init(S.usbCDC, "LTMailbox", LT_GetCore()->GetCurrentThreadObject(), ReadHandler,
                                                  WriteHandler, ErrorHandler, NULL),
                                                  "Cannot init CDC device");
    S.fifoSize = S.usbCDC->API->GetMaxWriteSize(S.usbCDC);
    TILT_ASSERT_TRUE(tilt, S.fifoSize > 0, "Cannot get read FIFO size");

    // We need to let the endpoint configure the tty as raw before doing any IO.
    // Otherwise we get responses from the tty not the endpoint.
    LT_GetCore()->GetCurrentThreadObject()->API->Sleep(LTTime_Seconds(1));

    // The echo service on the host signals readiness by transmitting a single byte.
    TILT_INFO(tilt, "Waiting for CDC host echo service to be ready");
    u32 retries = 10;
    while (retries--) {
        u8 out = 0;
        s32 read = S.usbCDC->API->Read(S.usbCDC, &out, 1);
        if (read == 1) {
            TILT_INFO(tilt, "CDC host echo service ready");
            return;
        }
        LT_GetCore()->GetCurrentThreadObject()->API->Sleep(LTTime_Milliseconds(100));
    }
    TILT_REPORT_FAILURE(S.tilt, "CDC host echo service not ready before timeout");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);

    if (S.usbCDC) {
        lt_destroyobject(S.usbCDC);
        S.usbCDC = NULL;
    }

    if (S.byteOps) {
        lt_closelibrary(S.byteOps);
    }
}

static void BeforeTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    S.testDataSize = 0;
    S.testDataWritten = 0;
    S.testBufferRead = 0;

    S.maxWriteSize = 0;
    S.maxReadSize = 0;
}

static void AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    Cleanup();
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
    .BeforeTest     = BeforeTest,
    .AfterTest      = AfterTest
};

static const TiltEngineTest s_tests[] = {
    { TestIO,          "TestIO",           "Read and write device",                          0 },
    { TestIOLarge,     "TestIOLarge",      "Read and write large amounts of data to device", 0 },
    { TestShortWrites, "TestShortWrites",  "Writes smaller than FIFO size",                  0 },
    { TestShortReads,  "TestShortReads",   "Reads smaller than FIFO size",                   0 },
};

static int UnitTestLTDeviceUsbCDCImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceUsbCDCImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceUsbCDCImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUsbCDC, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceUsbCDC, UnitTestLTDeviceUsbCDCImpl_Run, 1536) LTLIBRARY_DEFINITION;
