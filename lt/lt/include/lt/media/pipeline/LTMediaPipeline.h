/*******************************************************************************
 * <lt/media/pipeline/LTMediaPipeline.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 *
 ******************************************************************************/
/** @file LTMediaPipeline.h header for LT Media Pipeline
  */

#ifndef LT_INCLUDE_LT_MEDIA_PIPELINE_H
#define LT_INCLUDE_LT_MEDIA_PIPELINE_H

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/utility/buffer/LTRing.h>

LT_EXTERN_C_BEGIN

typedef bool (*LTMediaPipeline_OutputFrameCallback)(const u8 *data, LT_SIZE dataLen);
/**< Callback handler for output from the pipeline. The pipeline will call this
 * to output a data packet at an interval set by Start(). The size of the data packet
 * is determined by the interval, and the sample size and sample rate specified in Start().
 * The return value is treated by the pipeline as flow control. If the callback returns
 * false, the pipeline will wait until the next interval to call the callback again with
 * the same data. The pipeline will continue to call the callback with data as long as
 * this function returns true, and there is data to output.
 * The pipeline will call the callback with NULL data when the pipeline is out of data (zeros).
 * The callee may wish to flush remaining data in hardware buffers when this happens.
 *
 * @param data     Pointer to the data to output. NULL if there is no data to output.
 * @param dataLen  Length of the data to output
 *
 * @return true to accept data from pipeline, false to reject - pipeline will try again later
 */
typedef void (*LTMediaPipeline_DecodeFunction)(void *pDecoderContext, LTMediaData *pPacket, u8 **decodedData, LT_SIZE *decodedDataLen);
/**< Callback handler for decoding a packet, specified as part of a producer. The pipeline
 * will call this to decode a packet from the producer data queue and mix the data in the
 * returned pointer in decodedData to the output. If this function is not specified (NULL), the
 * pipeline will mix the packet data directly to the output.
 *
 * @param pDecoderContext Pointer to the decoder context
 * @param pPacket       Pointer to the packet to decode
 * @param decodedData   Pointer to the decoded data
 * @param decodedDataLen Length of the decoded data
 *
 */
typedef void (*LTMediaPipeline_ReleaseDecodeFunction)(void *pDecoderContext, u8 *decodedData, LT_SIZE decodedDataLen);
/**< Callback handler for releasing the decoded data, specified as part of a producer.
 * If specified, The pipeline will call this with the pointer and data length supplied by LTMediaPipeline_DecodeFunction()
 *
 * @param pDecoderContext Pointer to the decoder context
 * @param decodedData   Pointer to the decoded data, from LTMediaPipeline_DecodeFunction()
 * @param decodedDataLen Length of the decoded data, from LTMediaPipeline_DecodeFunction()
 *
 */

typedef struct {
    LTMutex *decodeQueueMutex;
    u32 nSampleRate;
    u64 pts;
    s16 lastPCMSample;
    LTRingBuffer decodeQueue;
    void *pDecoderContext;
    LTMediaPipeline_DecodeFunction pfnDecodeFunction;
    LTMediaPipeline_ReleaseDecodeFunction pfnDecodeReleaseFunction;
} LTMediaPipelineProducer;

typedef_LTObject(LTMediaPipeline, 1) {
    bool    (*Start)(LTMediaPipeline *pipeline,
                     u32 sampleRate, u32 sampleSize,
                     u32 framePeriodms, u32 pipelineFrameCount,
                     LTMediaPipeline_OutputFrameCallback pfnOutputFrame);
    /**< Start the pipeline.
     * A thread is created to manage the pipeline.
     * The pipeline will call the output callback at the specified frame period
     * and repeat continuously until the output callback returns false. The amount of data supplied to
     * the output callback is determined by the sample rate, sample size, and frame period.
     * pipelineFrameCount defines length of the pipeline.
     *
     * @param pipeline Pointer to the pipeline
     * @param sampleRate Sample rate of the pipeline
     * @param sampleSize Sample byte size
     * @param framePeriodms Frame period in milliseconds
     * @param pipelineFrameCount Length of the pipeline
     * @param pfnOutputFrame Output callback
     *
     * @return true if successful, false if failed.
     */
    void    (*Stop)(LTMediaPipeline *pipeline);
    /**< Stop the pipeline.
     * The pipeline thread is terminated and the pipeline will stop calling the output callback.
     *
     * @param pipeline Pointer to the pipeline
     */
    bool    (*RegisterProducer)(LTMediaPipeline *pipeline, LTMediaPipelineProducer *pProducer);
    /**< Register a producer to the pipeline.
     * The pipeline will start consumeing data from the producer. See LTMediaPipelineProducer for more details.
     *
     * @param pipeline Pointer to the pipeline
     * @param pProducer Pointer to the producer
     *
     * @return true if successful, false if failed.
    */
    void    (*UnregisterProducer)(LTMediaPipeline *pipeline, LTMediaPipelineProducer *pProducer);
    /**< Unregister a producer from the pipeline.
     * The pipeline will stop consumeing data from the producer. See LTMediaPipelineProducer for more details.
     *
     * @param pipeline Pointer to the pipeline
     * @param pProducer Pointer to the producer
    */

    LTMediaPipelineProducer* (*CreateProducer)(u32 sampleRate, u32 queueSize,
                                               void *pDecoderContext,
                                               LTMediaPipeline_DecodeFunction pfnDecodeFunction,
                                               LTMediaPipeline_ReleaseDecodeFunction pfnDecodeReleaseFunction);
    /**< Create a producer
     * This helper function creates a producer with the specified sample rate and queue size.
     * The producer returned is not yet registered to the pipeline.
     *
     * @param sampleRate Sample rate of the producer
     * @param queueSize  Size of the producer queue
     * @param pDecoderContext Pointer to the decoder context
     * @param pfnDecodeFunction Pointer to the decode function
     * @param pfnDecodeReleaseFunction Pointer to the release decode function
     *
     * @return Pointer to the producer, NULL if failed.
    */
    void    (*DestroyProducer)(LTMediaPipelineProducer *pProducer);
    /**< Destroy a producer
     * This helper function destroys a producer created by LTMediaPipeline_CreateProducer().
     * The producer must be unregistered from the pipeline before calling this function.
     *
     * @param pProducer Pointer to the producer
    */

   void (*WaitUntilProducerEmpty)(LTMediaPipelineProducer *pProducer, u32 periodMs);
    /**< Wait until the producer queue is empty
     * This helper function waits until the producer queue is empty.
     * The producer must be registered to the pipeline before calling this function to ensure
     * the queue is being consumed by the pipeline.
     *
     * @param pProducer Pointer to the producer
     * @param periodMs  Period in milliseconds to check the queue
     */

    bool (*QueueMediaData)(LTMediaPipelineProducer *pProducer, LTMediaData *pMediaData);
    /**< Queue media data to the producer
     * This helper function queues media data to the producer.
     *
     * @param pProducer Pointer to the producer
     * @param pMediaData Pointer to the media data
     *
     * @return true if successful, false if producer queue is full.
     */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_PIPELINE_H */
