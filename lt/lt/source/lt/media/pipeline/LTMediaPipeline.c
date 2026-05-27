/*******************************************************************************
 * source/lt/media/pipeline/LTMediaPipeline.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/media/pipeline/LTMediaPipeline.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/utility/buffer/LTRing.h>
#include "LTMediaPipelineRingQueue.h"


DEFINE_LTLOG_SECTION("pipeline");

/*________________________________________________
  static constants */

enum {
    kPipelineThreadStackSize = 2 * 1024, // 2 KB
};

/*________________________________________________
  forward declarations */
typedef void (*MixFunction)(s16 *dst, s16 *src, LT_SIZE srcLen, s16 prevLastSrcSample);

/*  ______________________________________________
 *  Object private data members */
typedef_LTObjectImpl(LTMediaPipeline, LTMediaPipelineImpl) {
    LTCore                   * pCore;
    LTMediaPipelineRingQueue * outputBuffer;
    ILTThread                * iThread;
    LTThread                   hPipelineThread;
    u32 framePeriodms;
    u32 frameSamples;
    u32 frameByteSize;
    u32 sampleSize;
    u32 sampleRate;
    u32 pipelineBufferFrames;
    u64 pts;      /* Head of buffer timestamp (ms) */
    u64 wrpts;    /* PTS of last mixed sample */
    LTMediaPipeline_OutputFrameCallback pfnOutputFrame;
} LTOBJECT_API;

/*_________________________________
  Library private implementation */
/****************************************************/
/*    Sample rate conversion and Mixing functions   */
/****************************************************/
static void mixsamples(s16 *dst, s16 *src) {
    s32 tmp = (s32)*dst + (s32)*src;
    if (tmp > LT_S16_MAX) tmp = LT_S16_MAX;
    else if (tmp < LT_S16_MIN) tmp = LT_S16_MIN;
    *dst = (s16)tmp;
}

static inline s16 linearInterpolate(s16 y1, s16 y2, u8 mu) {
    s32 y = (s32)y1 * (256 - mu) + (s32)y2 * mu;
    return (s16)(y >> 8);
}

// MixFunction with 1:2 sample rate conversion using linear interpolation
static void linearInterpolate1to2Mix(s16 *dst, s16 *src, LT_SIZE srcLen, s16 prevLastSrcSample) {
    s16 resSample = linearInterpolate(prevLastSrcSample, src[0], 128); // mu = 128 = 0.5, halfway value
    mixsamples(dst++, &resSample);
    mixsamples(dst++, src);

    for (LT_SIZE i = 1; i < srcLen; i++) {
        resSample = linearInterpolate(src[i-1], src[i], 128);
        mixsamples(dst++, &resSample);
        mixsamples(dst++, src + i);
    }
}

// MixFunction with no sample rate conversion
static void noSrcMix(s16 *dst, s16 *src, LT_SIZE srcLen, s16 prevLastSrcSample) {
    LT_UNUSED(prevLastSrcSample);
    for (LT_SIZE i = 0; i < srcLen; i++) {
        mixsamples(dst + i, src + i);
    }
}

/****************************************************/
/*             Pipeline implementation              */
/****************************************************/
// Mix PCM to output buffer
static void mixToOutput(LTMediaPipelineImpl *pCtx, LTMediaPipelineProducer * pProd, u64 sampleStartPTS, s16 *pcmSamples, LT_SIZE pcmSamplesLen) {
    u64 sampleStartOffsetMs = sampleStartPTS - pCtx->pts;
    u64 sampleStartOffset = sampleStartOffsetMs * (pCtx->sampleRate / 1000);
    s16 *dstPCM = pCtx->outputBuffer->API->GetElementFromHead(pCtx->outputBuffer, sampleStartOffset);
    LT_SIZE pcmSrcSamples = (pcmSamplesLen * pCtx->sampleRate) / pProd->nSampleRate;
    MixFunction pfnMixFunction;
    if (pProd->nSampleRate == 8000) {
        pfnMixFunction = linearInterpolate1to2Mix;
    } else {
        pfnMixFunction = noSrcMix;
    }

    // Deal with boundary condition
    LT_SIZE elemToBoundary = pCtx->outputBuffer->API->GetContiguousElements(pCtx->outputBuffer, sampleStartOffset);

    if (pcmSrcSamples > elemToBoundary) {
        LT_SIZE firstPartSamples = elemToBoundary * pProd->nSampleRate / pCtx->sampleRate;
        pfnMixFunction(dstPCM, pcmSamples, firstPartSamples, pProd->lastPCMSample);
        pProd->lastPCMSample = pcmSamples[firstPartSamples - 1];
        dstPCM = pCtx->outputBuffer->API->GetElementFromHead(pCtx->outputBuffer, sampleStartOffset + elemToBoundary);
        pfnMixFunction(dstPCM, pcmSamples + firstPartSamples, pcmSamplesLen - firstPartSamples, pProd->lastPCMSample);
        pProd->lastPCMSample = pcmSamples[pcmSamplesLen - 1];
    } else {
        pfnMixFunction(dstPCM, pcmSamples, pcmSamplesLen, pProd->lastPCMSample);
        pProd->lastPCMSample = (pcmSamples)[pcmSamplesLen - 1];
    }

    // Update producer's PTS
    pProd->pts += (pcmSrcSamples * 1000) / pCtx->sampleRate;

    // Update pipeline's PTS
    if (pCtx->wrpts < pProd->pts) pCtx->wrpts = pProd->pts;
}

// Decode raw frames from each producer and mix to output
static void DecodeFrameTask(void * pClientData) {
    LTMediaPipelineProducer * pProd = pClientData;
    LTMediaPipelineImpl *pCtx;
    LTMediaData **ppDecData;
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    u64 outputBufferSizeMs;

    if (!pProd) return;
    pCtx = iThread->GetThreadSpecificClientData(iThread->GetCurrentThread(), "pipe");
    if (!pCtx) return;

    outputBufferSizeMs = pCtx->pipelineBufferFrames * pCtx->framePeriodms;

    if ((pCtx->pts + outputBufferSizeMs) < pProd->pts) return; // Too early

    if (pProd->decodeQueueMutex->API->TryLock(pProd->decodeQueueMutex)) {
        ppDecData = (LTMediaData**)LTRingConsume(&pProd->decodeQueue, 1);
        while (ppDecData) {
            LTMediaData *pPacket = *ppDecData;
            u64 sampleStartPTS;
            // Update producer's PTS
            if (pProd->pts < pCtx->pts) pProd->pts = pCtx->pts;
            sampleStartPTS = pProd->pts;

            // Decode and mix to output
            if (pProd->pfnDecodeFunction) {
                u8 *decodedData = NULL;
                LT_SIZE decodedDataLen = 0;
                pProd->pfnDecodeFunction(pProd->pDecoderContext, pPacket, &decodedData, &decodedDataLen);
                if (decodedData) {
                    mixToOutput(pCtx, pProd, sampleStartPTS, (s16 *)decodedData, decodedDataLen / 2);
                    if (pProd->pfnDecodeReleaseFunction) {
                        pProd->pfnDecodeReleaseFunction(pProd->pDecoderContext, decodedData, decodedDataLen);
                    }
                }
            } else {
                // No decode function supplied, mix the packet data directly to output
                mixToOutput(pCtx, pProd, sampleStartPTS, (s16 *)pPacket->pData, pPacket->nDataLen / 2);
            }
            lt_free(pPacket);
            if ((pCtx->pts + outputBufferSizeMs) < pProd->pts) break; // Too early
            ppDecData = (LTMediaData**)LTRingConsume(&pProd->decodeQueue, 1);
        }
        pProd->decodeQueueMutex->API->Unlock(pProd->decodeQueueMutex);
    }
}

static void
OutputFrameTask(void * pClientData) {
    LTMediaPipelineImpl *pCtx = pClientData;
    LT_ASSERT(pCtx);

    // No more samples to output for now
    if (pCtx->wrpts <= pCtx->pts) {
        if (pCtx->pfnOutputFrame) pCtx->pfnOutputFrame(NULL, 0);
        return;
    }

    while (pCtx->pts < pCtx->wrpts) {
        if (pCtx->pfnOutputFrame && pCtx->pfnOutputFrame(pCtx->outputBuffer->API->GetElementFromHead(pCtx->outputBuffer, 0), pCtx->frameByteSize)) {
            pCtx->outputBuffer->API->AdvanceHead(pCtx->outputBuffer, pCtx->frameSamples);
            pCtx->pts += pCtx->framePeriodms;
        } else {
            break;
        }
    }
}

static bool
LTMediaPipeline_ThreadInit(void) {
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    LTMediaPipelineImpl * pCtx = iThread->GetThreadSpecificClientData(iThread->GetCurrentThread(), "pipe");
    if (!pCtx) return false;

    iThread->SetTimer(iThread->GetCurrentThread(), LTTime_Milliseconds(pCtx->framePeriodms), OutputFrameTask, NULL, pCtx);
    return true;
}

static bool LTMediaPipelineImpl_Start(LTMediaPipelineImpl *pipeline,
                          u32 sampleRate,
                          u32 sampleSize,
                          u32 framePeriodms,
                          u32 pipelineFrameCount,
                          LTMediaPipeline_OutputFrameCallback pfnOutputFrame) {
    LT_SIZE frameByteSize;
    if (!pipeline) {
        LTLOG_REDALERT("start.err", "Invalid pipeline");
        return false;
    }

    if (pipeline->hPipelineThread != LTHANDLE_INVALID) {
        LTLOG_REDALERT("td.err", "Pipeline thread already running");
        return false;
    }

    frameByteSize = sampleSize * sampleRate * framePeriodms / 1000;
    if (!pipeline->outputBuffer->API->Init(pipeline->outputBuffer, sampleSize, pipelineFrameCount * frameByteSize)) {
        LTLOG_REDALERT("oom", "Unable to allocate ring buffer");
        return false;
    }

    // Create and start the pipeline thread
    pipeline->hPipelineThread = pipeline->pCore->CreateThread("media_pipe");
    if (pipeline->hPipelineThread == LTHANDLE_INVALID) {
        LTLOG_REDALERT("thread.err", "Unable to create pipeline thread");
        pipeline->outputBuffer->API->Destroy(pipeline->outputBuffer);
        return false;
    }

    pipeline->framePeriodms = framePeriodms;
    pipeline->sampleSize = sampleSize;
    pipeline->sampleRate = sampleRate;
    pipeline->frameSamples = (sampleRate * framePeriodms) / 1000;
    pipeline->frameByteSize = frameByteSize;
    pipeline->pipelineBufferFrames = pipelineFrameCount;
    pipeline->pfnOutputFrame = pfnOutputFrame;
    pipeline->pts = 0;
    pipeline->wrpts = 0;

    // Start the pipeline thread
    pipeline->iThread->SetThreadSpecificClientData(pipeline->hPipelineThread, "pipe", NULL, pipeline);
    pipeline->iThread->SetStackSize(pipeline->hPipelineThread, kPipelineThreadStackSize);
    pipeline->iThread->SetPriority(pipeline->hPipelineThread, kLTThread_PriorityDefault+5);
    pipeline->iThread->Start(pipeline->hPipelineThread, LTMediaPipeline_ThreadInit, NULL);
    return true;
}

static void LTMediaPipelineImpl_Stop(LTMediaPipelineImpl *pipeline) {
    if (pipeline->hPipelineThread != LTHANDLE_INVALID) {
        pipeline->iThread->Terminate(pipeline->hPipelineThread);
        pipeline->iThread->WaitUntilFinished(pipeline->hPipelineThread, LTTime_Infinite());
        lt_destroyhandle(pipeline->hPipelineThread);
        pipeline->hPipelineThread = LTHANDLE_INVALID;
        pipeline->outputBuffer->API->Destroy(pipeline->outputBuffer);
    }
}

static bool LTMediaPipelineImpl_RegisterProducer(LTMediaPipelineImpl *pipeline, LTMediaPipelineProducer *pProducer) {
    if (!pProducer) return false;
    if (pipeline->hPipelineThread == LTHANDLE_INVALID) {
        LTLOG_REDALERT("reg.err", "Pipeline not started");
        return false;
    }

    pipeline->iThread->SetTimer(pipeline->hPipelineThread, LTTime_Milliseconds(pipeline->framePeriodms), DecodeFrameTask, NULL, pProducer);
    return true;
}

static void LTMediaPipelineImpl_UnregisterProducer(LTMediaPipelineImpl *pipeline, LTMediaPipelineProducer *pProducer) {
    if (!pProducer) return;
    if (pipeline->hPipelineThread == LTHANDLE_INVALID) {
        LTLOG_REDALERT("unreg.err", "Pipeline not started");
        return;
    }

    // Kill the decoding task
    pipeline->iThread->KillTimer(pipeline->hPipelineThread, DecodeFrameTask, pProducer);
}

static LTMediaPipelineProducer* LTMediaPipelineImpl_CreateProducer(u32 sampleRate, u32 queueSize,
                                                                   void *pDecoderContext,
                                                                   LTMediaPipeline_DecodeFunction pfnDecodeFunction,
                                                                   LTMediaPipeline_ReleaseDecodeFunction pfnDecodeReleaseFunction) {
    LTMediaPipelineProducer *pProd = lt_malloc(sizeof(LTMediaPipelineProducer));
    if (!pProd) return NULL;
    lt_memset(pProd, 0, sizeof(LTMediaPipelineProducer));
    pProd->nSampleRate = sampleRate;
    pProd->pDecoderContext = pDecoderContext;
    pProd->pfnDecodeFunction = pfnDecodeFunction;
    pProd->pfnDecodeReleaseFunction = pfnDecodeReleaseFunction;
    LTMediaData **decodeQueueData = lt_malloc(queueSize * sizeof(LTMediaData *));
    if (!decodeQueueData) {
        lt_free(pProd);
        return NULL;
    }
    LTRingInit(&pProd->decodeQueue, decodeQueueData, queueSize);
    pProd->decodeQueueMutex = lt_createobject(LTMutex);
    if (pProd->decodeQueueMutex == NULL) {
        lt_free(decodeQueueData);
        lt_free(pProd);
        return NULL;
    }
    return pProd;
}

static void LTMediaPipelineImpl_DestroyProducer(LTMediaPipelineProducer *pProducer) {
    if (!pProducer) return;
    if (pProducer->decodeQueueMutex != NULL) {
        lt_destroyobject(pProducer->decodeQueueMutex);
        pProducer->decodeQueueMutex = NULL;
    }
    lt_free(pProducer->decodeQueue.data);
    lt_free(pProducer);
}

static void LTMediaPipelineImpl_WaitUntilProducerEmpty(LTMediaPipelineProducer *pProducer, u32 periodMs) {
    if (!pProducer) return;
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    // Wait for samples to be consumed by pipeline
    pProducer->decodeQueueMutex->API->Lock(pProducer->decodeQueueMutex);
    while (LTRingGetSize(&(pProducer->decodeQueue))) {
        pProducer->decodeQueueMutex->API->Unlock(pProducer->decodeQueueMutex);
        iThread->Sleep(LTTime_Milliseconds(periodMs));
        pProducer->decodeQueueMutex->API->Lock(pProducer->decodeQueueMutex);
    }
    pProducer->decodeQueueMutex->API->Unlock(pProducer->decodeQueueMutex);
}

static bool LTMediaPipelineImpl_QueueMediaData(LTMediaPipelineProducer *pProducer, LTMediaData *pMediaData) {
    bool res = false;
    if (!pProducer || !pMediaData) return false;
    pProducer->decodeQueueMutex->API->Lock(pProducer->decodeQueueMutex);
    LTMediaData **pRingElem = LTRingReserve(&pProducer->decodeQueue, 1);
    if (pRingElem) {
        *pRingElem = pMediaData;
        res = true;
    }
    pProducer->decodeQueueMutex->API->Unlock(pProducer->decodeQueueMutex);
    return res;
}

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTMediaPipelineImpl_DestructObject(LTMediaPipelineImpl *pipeline) {
    LTMediaPipelineImpl_Stop(pipeline);
    lt_destroyobject(pipeline->outputBuffer);
}

static bool LTMediaPipelineImpl_ConstructObject(LTMediaPipelineImpl *pipeline) {
    pipeline->pCore = LT_GetCore();
    pipeline->iThread = lt_getlibraryinterface(ILTThread, pipeline->pCore);
    pipeline->hPipelineThread = LTHANDLE_INVALID;
    pipeline->outputBuffer = lt_createobject(LTMediaPipelineRingQueue);
    if (!pipeline->outputBuffer) {
        pipeline->pCore->ConsolePrint("Unable to create ring queue\n");
        return false;
    }
    return true;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTMediaPipeline, LTMediaPipelineImpl,
    Start,
    Stop,
    RegisterProducer,
    UnregisterProducer,
    CreateProducer,
    DestroyProducer,
    WaitUntilProducerEmpty,
    QueueMediaData,
);

define_LTOBJECT_EXPORTLIBRARY(
    LTMediaPipeline, 1,
    LTMediaPipelineImpl
);

// Name a callback function for checking with the user whether to output a frame
