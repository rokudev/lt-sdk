/*******************************************************************************
 * source/unittest/lt/iot/migration/UnitTestLTMediaPipeline.c
 *    __  __      _ __ ______          __
 *   / / / /___  (_) //_  __/__  _____/ /_
 *  / / / / __ \/ / __// / / _ \/ ___/ __/
 * / /_/ / / / / / /_ / / /  __(__  ) /_/
 * \____/_/ /_/_/\__//_/  \___/____/\__/
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>

#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/pipeline/LTMediaPipeline.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include <tilt/JiltEngine.h>

typedef void (*TestDataFiller)(u32 sampleIndex, u8 *data, LT_SIZE dataLen);

enum {
    kTestBufferSize     = 200 * 1024,
    kTestBufferCount    = 10,
    kTestOutputPeriodMs = 20,
    kMaxPerSinkFramesInFlight = 8,
    kSampleSize8        = 1,
    kSampleSize16       = 2,
    kSampleSize32       = 4,
    kTestPipelineSampleRate = 16000,
    // Output needs to be gated to test real world output progress, but we don't want CI to wait for real
    kSpeedupFactor      = 4,
    kDecoderCtxMagic    = 0x12345678,
};

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static struct {
    LTCore          *pCore;
    ILTThread       *iThread;
    LTMediaPipeline *pipeline;
    LTMutex         *testmutex;
    Tilt            *tilt;
    u8              *testResultBuffer;
    LT_SIZE          testResultBufferSize;
    LT_SIZE          testResultBufferIdx;
    u32              outputCount;
    u32              outputFlushCount;
    u32              outputBufferLevel;
    u32              decodeAdd1Count;
    u32              decodeAdd1ReleaseCount;
} S;

static JiltEngine *s_engine;

/*******************************************************************************
 * Callback Functions
 ******************************************************************************/
static bool OutputFrameHandler(const u8 *data, LT_SIZE dataLen) {
    S.testmutex->API->Lock(S.testmutex);
    if (data) {
        if (S.outputBufferLevel >= kSpeedupFactor) {
            S.outputBufferLevel = 0;
            S.testmutex->API->Unlock(S.testmutex);
            return false;
        }
        S.outputBufferLevel++;
        S.outputCount++;
        if (S.testResultBuffer) {
            if (S.testResultBufferIdx + dataLen < S.testResultBufferSize) {
                lt_memcpy(S.testResultBuffer + S.testResultBufferIdx, data, dataLen);
            }
            // Still increment the index so we know we blew pass the end and by how much
            S.testResultBufferIdx += dataLen;
        }
    } else {
        S.outputFlushCount++;
    }
    S.testmutex->API->Unlock(S.testmutex);
    return true;
}

static void DecodeFrameAdd1(void *pDecoderCtx, LTMediaData *pPacket, u8 **decodedData, LT_SIZE *decodedDataLen) {
    // Test decoding function that adds 1 to each sample
    LT_ASSERT(S.tilt != NULL);
    TILT_EXPECT_TRUE(S.tilt, pDecoderCtx == (void *)kDecoderCtxMagic,  "Invalid decoder context at decode");
    S.testmutex->API->Lock(S.testmutex);
    S.decodeAdd1Count++;
    S.testmutex->API->Unlock(S.testmutex);
    *decodedData = lt_malloc(pPacket->nDataLen);
    LT_ASSERT(*decodedData);
    *decodedDataLen = pPacket->nDataLen;
    for (u32 i = 0; i < pPacket->nDataLen / kSampleSize16; i++) {
        ((s16 *)*decodedData)[i] = ((s16 *)pPacket->pData)[i] + 1;
    }
}

static void DecodeFrameAdd1Release(void *pDecoderCtx, u8 *decodedData, LT_SIZE decodedDataLen) {
    LT_UNUSED(decodedDataLen);
    LT_ASSERT(S.tilt != NULL);
    TILT_EXPECT_TRUE(S.tilt, pDecoderCtx == (void *)kDecoderCtxMagic,  "Invalid decoder context at release");
    S.testmutex->API->Lock(S.testmutex);
    S.decodeAdd1ReleaseCount++;
    S.testmutex->API->Unlock(S.testmutex);
    lt_free(decodedData);
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
s16 GetTestSample(u32 sampleIndex) {
    return LT_S16_MIN + sampleIndex;
}
s16 GetTestSample256(u32 sampleIndex) {
    s8 sample = LT_S8_MIN + sampleIndex;
    return (s16)sample;
}

void FillTestSampleBuffer(u32 sampleIndex,
                          u8 *pBuffer,
                          LT_SIZE bufferSize) {
    for (LT_SIZE i = 0; i < bufferSize / kSampleSize16; i++) {
        ((s16 *)pBuffer)[i] = GetTestSample(sampleIndex + i);
    }
}

void FillTestSampleBuffer2To1DownSample(u32 sampleIndex,
                                        u8 *pBuffer,
                                        LT_SIZE bufferSize) {
    for (LT_SIZE i = 0; i < bufferSize / kSampleSize16; i++) {
        ((s16 *)pBuffer)[i] = GetTestSample((sampleIndex + i) * 2);
    }
}

void FillTestSampleBuffer2To1DownSample256(u32 sampleIndex,
                                        u8 *pBuffer,
                                        LT_SIZE bufferSize) {
    for (LT_SIZE i = 0; i < bufferSize / kSampleSize16; i++) {
        ((s16 *)pBuffer)[i] = GetTestSample256((sampleIndex + i) * 2);
    }
}

LTMediaData *CreateLTMediaData(LT_SIZE dataLen) {
    LTMediaData *pMediaData;
    pMediaData = lt_malloc(sizeof(LTMediaData) + dataLen);
    LT_ASSERT(pMediaData);
    pMediaData->pData = (u8 *)&(pMediaData[1]);
    pMediaData->nDataLen = dataLen;
    pMediaData->bEndOfFrame = false;
    return pMediaData;
}

static void WriteTestDataToProducer(TestDataFiller pfnDataFiller,
                                    LTMediaPipelineProducer *pProducer,
                                    u32 frameSampleSize,
                                    LT_SIZE totalSamplesToWrite) {
    LT_SIZE samplesWritten = 0;
    bool done = false;

    do {
        LTMediaData *pMediaData = CreateLTMediaData(frameSampleSize * kSampleSize16);
        pfnDataFiller(samplesWritten, pMediaData->pData, pMediaData->nDataLen);
        samplesWritten += frameSampleSize;
        if (samplesWritten >= totalSamplesToWrite) {
            pMediaData->bEndOfFrame = true;
            done = true;
        }
        while (!S.pipeline->API->QueueMediaData(pProducer, pMediaData)) {
            S.iThread->Sleep(LTTime_Milliseconds(kTestOutputPeriodMs));
        }
    } while (!done);
}

// This function is used by the decode, src and mix test. It writes data to 2 producers simultaneously
static void WriteTestDataMixSrc(LTMediaPipelineProducer *pProducer1, LTMediaPipelineProducer *pProducer2) {
    LT_SIZE producer1SamplesWritten = 0;
    LT_SIZE producer2SamplesWritten = 0;
    LT_SIZE totalSamplesToWrite1 = LT_S16_MAX - LT_S16_MIN + 1;
    LT_SIZE totalSamplesToWrite2 = (LT_S16_MAX - LT_S16_MIN + 1) / 2;
    u32 frameSampleSize1 = kTestPipelineSampleRate * kTestOutputPeriodMs / 1000;
    u32 frameSampleSize2 = (kTestPipelineSampleRate * kTestOutputPeriodMs / 1000) / 2;
    bool producer1Done = false;
    bool producer2Done = false;

    do {
        // Producer 1
        while (!producer1Done) {
            LT_SIZE samplesToWrite = ((totalSamplesToWrite1 - producer1SamplesWritten) > frameSampleSize1) ?
                                     frameSampleSize1 :
                                     (totalSamplesToWrite1 - producer1SamplesWritten);
            LTMediaData *pMediaData = CreateLTMediaData(samplesToWrite * kSampleSize16);
            FillTestSampleBuffer(producer1SamplesWritten, pMediaData->pData, pMediaData->nDataLen);
            pMediaData->bEndOfFrame = (samplesToWrite < frameSampleSize1);
            if (!S.pipeline->API->QueueMediaData(pProducer1, pMediaData)) {
                // Queue is full, discard the data and try again later
                lt_free(pMediaData);
                break;
            }

            // Update position
            producer1SamplesWritten += samplesToWrite;
            if (samplesToWrite < frameSampleSize1) {
                producer1Done = true;
            }
        }

        // Producer 2
        while (!producer2Done) {
            LT_SIZE samplesToWrite = ((totalSamplesToWrite2 - producer2SamplesWritten) > frameSampleSize2) ?
                                     frameSampleSize2 :
                                     (totalSamplesToWrite2 - producer2SamplesWritten);
            LTMediaData *pMediaData = CreateLTMediaData(samplesToWrite * kSampleSize16);
            FillTestSampleBuffer2To1DownSample(producer2SamplesWritten, pMediaData->pData, pMediaData->nDataLen);
            pMediaData->bEndOfFrame = (samplesToWrite < frameSampleSize1);
            if (!S.pipeline->API->QueueMediaData(pProducer2, pMediaData)) {
                // Queue is full, discard the data and try again later
                lt_free(pMediaData);
                break;
            }

            // Update position
            producer2SamplesWritten += samplesToWrite;
            if (samplesToWrite < frameSampleSize2) {
                producer2Done = true;
            }
        }

        // Both producers filled. Wait a little.
        S.iThread->Sleep(LTTime_Milliseconds(kTestOutputPeriodMs));
    } while (!producer1Done || !producer2Done);
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/
static void TestPipelineCreateDestroy(Tilt * tilt) {
    S.pipeline = lt_createobject(LTMediaPipeline);
    TILT_ASSERT_TRUE(tilt, S.pipeline != NULL, "Cannot create pipeline");
    S.pipeline->API->Stop(S.pipeline); // Test stop on non-started pipeline
    lt_destroyobject(S.pipeline);
    S.pipeline = NULL;
}

static void TestPipelineEmptyRun(Tilt * tilt) {
    bool res;
    u32 expectedFlushCount;
    S.outputCount = 0;
    S.outputFlushCount = 0;
    S.pipeline = lt_createobject(LTMediaPipeline);
    TILT_ASSERT_TRUE(tilt, S.pipeline != NULL, "Cannot create pipeline");
    res = S.pipeline->API->Start(S.pipeline, kTestPipelineSampleRate, kSampleSize16, kTestOutputPeriodMs, kTestBufferCount, OutputFrameHandler);
    if (!res) {
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        TILT_REPORT_FAILURE(tilt, "Cannot start pipeline");
        return;
    }

    // Sleep for 200ms to allow for pipeline start up
    S.iThread->Sleep(LTTime_Milliseconds(200));
    expectedFlushCount = (200 / kTestOutputPeriodMs) ;

    S.pipeline->API->Stop(S.pipeline);

    TILT_EXPECT_TRUE(tilt, S.outputCount == 0, "Output callback called on empty pipeline");
    TILT_EXPECT_TRUE(tilt, S.outputFlushCount >= expectedFlushCount -1 && S.outputFlushCount <= expectedFlushCount + 1,
                     "Wrong number of flushing calls, expected %d+-1, got %d", expectedFlushCount, S.outputFlushCount);

    // Test pipeline restart
    S.outputCount = 0;
    S.outputFlushCount = 0;
    res = S.pipeline->API->Start(S.pipeline, kTestPipelineSampleRate, kSampleSize16, kTestOutputPeriodMs, kTestBufferCount, OutputFrameHandler);
    TILT_ASSERT_TRUE(tilt, res, "Cannot restart pipeline");

    // Sleep for 200ms to allow for pipeline start up
    S.iThread->Sleep(LTTime_Milliseconds(200));
    expectedFlushCount = (200/kTestOutputPeriodMs) ;

    S.pipeline->API->Stop(S.pipeline);

    TILT_EXPECT_TRUE(tilt, S.outputCount == 0, "Output callback called on empty pipeline after restart");
    TILT_EXPECT_TRUE(tilt, S.outputFlushCount >= expectedFlushCount -1 && S.outputFlushCount <= expectedFlushCount + 1,
                     "Wrong number of flushing calls after restart, expected %d+-1, got %d", expectedFlushCount, S.outputFlushCount);

    lt_destroyobject(S.pipeline);
    S.pipeline = NULL;
}

static void TestPipelineBasic(Tilt * tilt) {
    bool res;
    u32 frameSampleSize = kTestPipelineSampleRate * kTestOutputPeriodMs / 1000;
    u32 totalTestSamplesCount = LT_S16_MAX - LT_S16_MIN + 1;
    u32 expectedOutputCallCount = (totalTestSamplesCount / frameSampleSize);
    if (totalTestSamplesCount % frameSampleSize) {
        expectedOutputCallCount++;
    }
    S.outputCount = 0;
    S.outputFlushCount = 0;
    S.outputBufferLevel = 0;

    S.pipeline = lt_createobject(LTMediaPipeline);
    TILT_ASSERT_TRUE(tilt, S.pipeline != NULL, "Cannot create pipeline");
    res = S.pipeline->API->Start(S.pipeline, kTestPipelineSampleRate, kSampleSize16, kTestOutputPeriodMs, kTestBufferCount, OutputFrameHandler);
    TILT_EXPECT_TRUE(tilt, res, "Cannot start pipeline");
    if (!res) {
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        return;
    }

    // Allocate a test buffer for catching the output
    S.testResultBuffer = lt_malloc(kTestBufferSize);
    LT_ASSERT(S.testResultBuffer);
    S.testResultBufferSize = kTestBufferSize;
    S.testResultBufferIdx = 0;
    lt_memset(S.testResultBuffer, 0, kTestBufferSize);

    // Register producer 1
    LTMediaPipelineProducer *pProducer1 = S.pipeline->API->CreateProducer(kTestPipelineSampleRate, kMaxPerSinkFramesInFlight, NULL, NULL, NULL);

    if (!pProducer1) {
        lt_free(S.testResultBuffer);
        S.testResultBuffer = NULL;
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        TILT_REPORT_FAILURE(tilt, "Cannot create producer 1");
        return;
    }

    S.pipeline->API->RegisterProducer(S.pipeline, pProducer1);

    // Proceed to write test data to the producer.
    WriteTestDataToProducer(FillTestSampleBuffer , pProducer1, frameSampleSize, totalTestSamplesCount);

    // Wait for the producer to drain
    S.pipeline->API->WaitUntilProducerEmpty(pProducer1, kTestOutputPeriodMs);

    // Wait for the pipeline to flush
    S.testmutex->API->Lock(S.testmutex);
    S.outputFlushCount = 0;
    while (!S.outputFlushCount) {
        S.testmutex->API->Unlock(S.testmutex);
        S.iThread->Sleep(LTTime_Milliseconds(kTestOutputPeriodMs));
        S.testmutex->API->Lock(S.testmutex);
    }
    S.testmutex->API->Unlock(S.testmutex);

    S.pipeline->API->UnregisterProducer(S.pipeline, pProducer1);
    S.pipeline->API->DestroyProducer(pProducer1);

    // Pipeline is done
    S.pipeline->API->Stop(S.pipeline);

    // Verify the output
    LT_SIZE expectedOutputSize = expectedOutputCallCount * frameSampleSize * kSampleSize16;
    TILT_EXPECT_TRUE(tilt, S.outputCount == expectedOutputCallCount, "Too many Output callbacks, expected %d, got %d", expectedOutputCallCount, S.outputCount);
    TILT_EXPECT_TRUE(tilt, S.outputFlushCount == 1, "Wrong number of flushing calls, expected 1, got %d", S.outputFlushCount);
    TILT_EXPECT_TRUE(tilt, S.testResultBufferIdx == expectedOutputSize, "Incorrect data length from pipeline, expected %d, got %d", expectedOutputSize, S.testResultBufferIdx);

    for (u32 i = 0; i < totalTestSamplesCount; i++) {
        s16 expectedDataPoint = GetTestSample(i);
        if (((s16 *)S.testResultBuffer)[i] != expectedDataPoint) {
            TILT_REPORT_FAILURE(tilt, "Incorrect data from pipeline at index %d, expected 0x%04X, got 0x%04X", i, expectedDataPoint, ((s16 *)S.testResultBuffer)[i]);
        }
    }

    // Clean up
    lt_free(S.testResultBuffer);
    S.testResultBuffer = NULL;
    lt_destroyobject(S.pipeline);
    S.pipeline = NULL;
}

static void TestPipelineSRC(Tilt * tilt) {
    bool res;
    u32 pipeLineFrameSampleSize = kTestPipelineSampleRate * kTestOutputPeriodMs / 1000;
    u32 inputframeSampleSize = pipeLineFrameSampleSize / 2; // Starting data is at 8000Hz.
    u32 framesToTest = 5;
    u32 totalInputTestSamples = inputframeSampleSize * framesToTest;
    u32 totalOutputTestSamples = pipeLineFrameSampleSize * framesToTest;
    u32 expectedOutputCallCount = (totalOutputTestSamples / pipeLineFrameSampleSize);
    if ((totalOutputTestSamples * 2) % pipeLineFrameSampleSize) {
        expectedOutputCallCount++;
    }
    S.outputCount = 0;
    S.outputFlushCount = 0;
    S.outputBufferLevel = 0;

    S.pipeline = lt_createobject(LTMediaPipeline);
    TILT_ASSERT_TRUE(tilt, S.pipeline != NULL, "Cannot create pipeline");

    res = S.pipeline->API->Start(S.pipeline, kTestPipelineSampleRate, kSampleSize16, kTestOutputPeriodMs, kTestBufferCount, OutputFrameHandler);
    TILT_EXPECT_TRUE(tilt, res, "Cannot start pipeline");
    if (!res) {
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        return;
    }

    // Allocate a test buffer for catching the output
    S.testResultBuffer = lt_malloc(kTestBufferSize);
    LT_ASSERT(S.testResultBuffer);
    S.testResultBufferSize = kTestBufferSize;
    S.testResultBufferIdx = 0;
    lt_memset(S.testResultBuffer, 0, kTestBufferSize);

    // Register producer 1
    LTMediaPipelineProducer *pProducer1 = S.pipeline->API->CreateProducer(8000, kMaxPerSinkFramesInFlight, NULL, NULL, NULL);
    TILT_EXPECT_TRUE(tilt, pProducer1 != NULL,  "Cannot create producer 1");
    if (!pProducer1) {
        lt_free(S.testResultBuffer);
        S.testResultBuffer = NULL;
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        return;
    }
    S.pipeline->API->RegisterProducer(S.pipeline, pProducer1);

    // Proceed to write test data to the producer.
    WriteTestDataToProducer(FillTestSampleBuffer2To1DownSample256 , pProducer1, inputframeSampleSize, totalInputTestSamples);

    // Wait for the producer to drain
    S.pipeline->API->WaitUntilProducerEmpty(pProducer1, kTestOutputPeriodMs);

    // Wait for the pipeline to flush
    S.testmutex->API->Lock(S.testmutex);
    S.outputFlushCount = 0;
    while (!S.outputFlushCount) {
        S.testmutex->API->Unlock(S.testmutex);
        S.iThread->Sleep(LTTime_Milliseconds(kTestOutputPeriodMs));
        S.testmutex->API->Lock(S.testmutex);
    }
    S.testmutex->API->Unlock(S.testmutex);

    S.pipeline->API->UnregisterProducer(S.pipeline, pProducer1);
    S.pipeline->API->DestroyProducer(pProducer1);
    // Pipeline is done
    S.pipeline->API->Stop(S.pipeline);

    // Verify the output
    LT_SIZE expectedOutputSize = expectedOutputCallCount * pipeLineFrameSampleSize * kSampleSize16;
    TILT_EXPECT_TRUE(tilt, S.outputCount == expectedOutputCallCount, "Too many Output callbacks, expected %d, got %d", expectedOutputCallCount, S.outputCount);
    TILT_EXPECT_TRUE(tilt, S.outputFlushCount == 1, "Wrong number of flushing calls, expected 1, got %d", S.outputFlushCount);
    TILT_EXPECT_TRUE(tilt, S.testResultBufferIdx == expectedOutputSize, "Incorrect data length from pipeline, expected %d, got %d", expectedOutputSize, S.testResultBufferIdx);

    for (u32 i = 0; i < totalOutputTestSamples; i++) {
        s16 expectedDataPoint;
        if (i == 0) {
            expectedDataPoint = (GetTestSample256(i) + 0) / 2;
        } else if (i & 1) {
            expectedDataPoint = GetTestSample256(i - 1);
        } else {
            expectedDataPoint = (GetTestSample256(i - 2) + GetTestSample256(i))/2;
        }
        if (((s16 *)S.testResultBuffer)[i] != expectedDataPoint) {
            TILT_REPORT_FAILURE(tilt, "Incorrect data from pipeline at index %d, expected %d, got %d", i, expectedDataPoint, ((s16 *)S.testResultBuffer)[i]);
        }
    }

    // Clean up
    lt_free(S.testResultBuffer);
    S.testResultBuffer = NULL;
    lt_destroyobject(S.pipeline);
    S.pipeline = NULL;
}


static void TestPipelineDecodeMix(Tilt * tilt) {
    bool res;
    u32 frameSampleSize = kTestPipelineSampleRate * kTestOutputPeriodMs / 1000;
    u32 totalTestSamplesCount = LT_S16_MAX - LT_S16_MIN + 1;
    u32 expectedOutputCallCount;
    LT_SIZE expectedOutputSize;

    expectedOutputCallCount = (totalTestSamplesCount / frameSampleSize);
    if (totalTestSamplesCount % frameSampleSize) {
        expectedOutputCallCount++;
    }
    expectedOutputSize = expectedOutputCallCount * frameSampleSize * kSampleSize16;

    S.outputCount = 0;
    S.outputFlushCount = 0;
    S.decodeAdd1Count = 0;
    S.decodeAdd1ReleaseCount = 0;
    S.outputBufferLevel = 0;

    S.pipeline = lt_createobject(LTMediaPipeline);
    TILT_ASSERT_TRUE(tilt, S.pipeline != NULL, "Cannot create pipeline");

    res = S.pipeline->API->Start(S.pipeline, kTestPipelineSampleRate, kSampleSize16, kTestOutputPeriodMs, kTestBufferCount, OutputFrameHandler);
    TILT_EXPECT_TRUE(tilt, res, "Cannot start pipeline");
    if (!res) {
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        return;
    }

    // Allocate a test buffer for catching the output
    S.testResultBuffer = lt_malloc(kTestBufferSize);
    LT_ASSERT(S.testResultBuffer);
    S.testResultBufferSize = kTestBufferSize;
    S.testResultBufferIdx = 0;
    lt_memset(S.testResultBuffer, 0, kTestBufferSize);

    S.tilt = tilt;
    LTMediaPipelineProducer *pProducer1 = S.pipeline->API->CreateProducer(kTestPipelineSampleRate, kMaxPerSinkFramesInFlight, (void *)kDecoderCtxMagic ,DecodeFrameAdd1, DecodeFrameAdd1Release);
    LTMediaPipelineProducer *pProducer2 = S.pipeline->API->CreateProducer(8000, kMaxPerSinkFramesInFlight, NULL, NULL, NULL);
    TILT_EXPECT_TRUE(tilt, pProducer1 != NULL,  "Cannot create producer 1");
    TILT_EXPECT_TRUE(tilt, pProducer2 != NULL,  "Cannot create producer 2");
    if (!pProducer1 || !pProducer2) {
        S.pipeline->API->DestroyProducer(pProducer1);
        S.pipeline->API->DestroyProducer(pProducer2);
        lt_free(S.testResultBuffer);
        S.testResultBuffer = NULL;
        lt_destroyobject(S.pipeline);
        S.pipeline = NULL;
        S.tilt = NULL;
        return;
    }

    // Register producers
    S.pipeline->API->RegisterProducer(S.pipeline, pProducer1);
    S.pipeline->API->RegisterProducer(S.pipeline, pProducer2);

    // Proceed to write test data to both producers.
    WriteTestDataMixSrc(pProducer1, pProducer2);

    // Wait for the producers to drain
    S.pipeline->API->WaitUntilProducerEmpty(pProducer1, kTestOutputPeriodMs);

    // Ensure producer 2 is done by now
    S.pipeline->API->WaitUntilProducerEmpty(pProducer2, kTestOutputPeriodMs);

    // Wait for the pipeline to flush
    S.testmutex->API->Lock(S.testmutex);
    S.outputFlushCount = 0;
    while (!S.outputFlushCount) {
        S.testmutex->API->Unlock(S.testmutex);
        S.iThread->Sleep(LTTime_Milliseconds(kTestOutputPeriodMs));
        S.testmutex->API->Lock(S.testmutex);
    }
    S.testmutex->API->Unlock(S.testmutex);

    S.pipeline->API->UnregisterProducer(S.pipeline, pProducer1);
    S.pipeline->API->DestroyProducer(pProducer1);
    S.pipeline->API->UnregisterProducer(S.pipeline, pProducer2);
    S.pipeline->API->DestroyProducer(pProducer2);
    // Pipeline is done
    S.pipeline->API->Stop(S.pipeline);

    // Verify the output
    TILT_EXPECT_TRUE(tilt, S.outputCount == expectedOutputCallCount, "Too many Output callbacks, expected %d, got %d", expectedOutputCallCount, S.outputCount);
    TILT_EXPECT_TRUE(tilt, S.testResultBufferIdx == expectedOutputSize, "Incorrect data length from pipeline, expected %d, got %d", expectedOutputSize, S.testResultBufferIdx);
    TILT_EXPECT_TRUE(tilt, S.decodeAdd1Count == expectedOutputCallCount, "Wrong number of decode calls, expected %d, got %d", expectedOutputCallCount, S.decodeAdd1Count);
    TILT_EXPECT_TRUE(tilt, S.decodeAdd1ReleaseCount == S.decodeAdd1Count, "Number of decoder releases does not match decoder calls. Expected %d, got %d", S.decodeAdd1Count, S.decodeAdd1ReleaseCount);

    for (u32 i = 0; i < totalTestSamplesCount; i++) {
        s16 prod1datapoint = GetTestSample(i) + 1;
        s16 prod2datapoint; // SRC extrapolated data
        if (i == 0) {
            prod2datapoint = (GetTestSample(i) + 0) / 2;
        } else if (i & 1) {
            prod2datapoint = GetTestSample(i - 1);
        } else {
            prod2datapoint = (GetTestSample(i - 2) + GetTestSample(i))/2;
        }
        s32 unclippedDataPoint = prod1datapoint + prod2datapoint;
        s16 expectedDataPoint;
        if (unclippedDataPoint > LT_S16_MAX) {
            expectedDataPoint = LT_S16_MAX;
        } else if (unclippedDataPoint < LT_S16_MIN) {
            expectedDataPoint = LT_S16_MIN;
        } else {
            expectedDataPoint = unclippedDataPoint;
        }

        if (((s16 *)S.testResultBuffer)[i] != expectedDataPoint) {
            TILT_REPORT_FAILURE(tilt, "Incorrect data from pipeline at index %d, expected %d, got %d", i, expectedDataPoint, ((s16 *)S.testResultBuffer)[i]);
        }
    }

    // Clean up
    lt_free(S.testResultBuffer);
    S.testResultBuffer = NULL;
    lt_destroyobject(S.pipeline);
    S.pipeline = NULL;
    S.tilt = NULL;
}

static void BeforeAllTests(Tilt *tilt) {
    S.pCore = LT_GetCore();
    S.iThread = lt_getlibraryinterface(ILTThread, S.pCore);

    S.testmutex = lt_createobject(LTMutex);
    TILT_EXPECT_TRUE(tilt, S.testmutex != NULL, "Cannot create mutex");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(S.testmutex);
}

static const TiltEngineTest s_tests[] = {
    { TestPipelineCreateDestroy,    "TestPipelineCreateDestroy",    "Create destroy test",      0 },
    { TestPipelineEmptyRun,         "TestPipelineEmptyRun",         "Run pipeline empty",       0 },
    { TestPipelineBasic,            "TestPipelineBasic",            "Basic tests",              0 },
    { TestPipelineSRC,              "TestPipelineSRC",              "8KHz to 16KHz conversion", 0 },
    { TestPipelineDecodeMix,        "TestPipelineDecodeMix",        "Decode SRC Mix tests",     0 },
};

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static int UnitTestLTMediaPipelineImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTMediaPipelineImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTMediaPipelineImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaPipeline, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTMediaPipeline, UnitTestLTMediaPipelineImpl_Run, 1536) LTLIBRARY_DEFINITION;
