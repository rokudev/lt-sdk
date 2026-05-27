/*******************************************************************************
 * lt/source/media/mp4/LTMediaMP4Recording.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/core/LTArray.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/mp4/recorder/LTMediaMP4Recorder.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/utility/buffer/LTRing.h>

DEFINE_LTLOG_SECTION("mp4.rec")

#define P(...)

/* _______________________________
   LTMediaMP4Recorder Constants */

enum {
    // Audio config
    kDefaultAudioSampleRate     = 8000,  // Audio samples per second, can be overridden from LTProductConfig
    kBytesPerAudioSample        = 1,     // Bytes per audio sample
    kDefaultAudioEncoding       = kLTMediaEncoding_PCMU,
    // Video config
    kDefaultMsecPerFrame        = 50,    // 50 microseconds per video frame
    kDefaultH264Profile         = 77,    // Main profile
    kDefaultVideoEncoding       = kLTMediaEncoding_H264,
    // Breakpoints for padding audio/video data
    kMinimumDroppedSamplesToPad = 100,   // Minimum number of dropped audio samples to compensate for
    kDroppedFrameMsecMargin     = 30,    // Minimum time between video frames to compensate for

    kDefaultGopLength           = 200,   // I-frame every 10 seconds based on 20 fps
    kMaxPrerollDuration         = 10000, // Maximum preroll duration in milliseconds (1 GoP duration when gopLength 200 and fps 20), can be overridden from LTProductConfig

    kSystemMemoryPollIntervalSeconds = 2,  // 2 seconds
};

static u32     s_maxPrerollDuration       = kMaxPrerollDuration;      // Set by value defined in LTProductConfig, if available
static u32     s_audioSampleRate          = kDefaultAudioSampleRate;  // Set by value defined in LTProductConfig, if available
static u32     s_cameraMsecPerFrame       = kDefaultMsecPerFrame;     // Set by LibInit
static bool    s_enablePreroll            = false;                    // Set by Recorder->Start()
static LT_SIZE s_cachedAvailableSystemRAM = LT_SIZE_MAX;

// As a point of reference, one GOP+audio data (1920x1080 H264 video stream, 8000Hz PCMU audio) can consume upwards of 500k, especially when there is a lot of video movement. There should be enough room for 2 GOPs + audio data
#define PREROLL_BUFFER_SIZE 1048576  // 1MB static buffer (2^20 bytes). This buffer must be a power of two.
#define PREROLL_SIZE        (PREROLL_BUFFER_SIZE / sizeof(AudioVideoData))  // sizeof(AudioVideoData) must be a power of two.

static const char settingsKeyGopLength[] = "mp4recorder/goplength";

/* _____________________________________
   LTMediaMP4Recorder Private Structs */

typedef enum {
    kWriteResult_None = 0,
    kWriteResult_Fail,
    kWriteResult_Success,
    kWriteResult_Skip,
} WriteResult;

typedef enum {
    kAudioUnit = kMetadataType_Audio,  // Audio
    kNalUnit   = kMetadataType_Video,  // Video
} MediaType;

typedef struct AudioVideoData {
    u64    timestamp;       // 8 bytes      // LTMediaData timestamp, in microseconds
    LTTime timestampDelta;  // 8 bytes      // LTMediaData timestampDelta
    u64    padding;         // 8 bytes      // Padding to bring struct to 32 bytes
    u32    dataLen;         // 4 bytes
    u16    elementLen;      // 2 bytes      // How many elements this struct + data uses in the preroll buffer. See CalculatedNeededElements for more detail.
    u8     type;            // 1 byte       // MediaType: Audio, NAL, or filler
    bool   hasNext;         // 1 byte       // Whether another data exists at AudioVideoData + (elementLen - 1)
    u8     data[];          // 0 bytes
} AudioVideoData;
LT_STATIC_ASSERT_SIZE_32_64(AudioVideoData, 32, 32);
/** Structure used for the elements of the preroll ring buffer. Its size must be a power of two in order to divide the
 * power of two backing buffer into power of two elements for the ring buffer. */

typedef struct {
    MediaType       type;
    u64             dataTimestamp;
    AudioVideoData *ref;
} PrerollDataReference;
/** Pointers to data in the preroll ring buffer may become stale when old data is evicted to make room for new data in
 * the ring buffer. By storing the timestamp externally, the validity of the data pointer can be checked w/o needing to
 * access the data itself */

typedef struct {
    LTRingBuffer            ring;                   // Ring buffer
    u64                     audioTimestampCutoff;   // Timestamp of most recently evicted audio data from the ring buffer. Audio data with
                                                    // a timestamp less than this cutoff is no longer in the ring buffer
    u64                     videoTimestampCutoff;   // Timestamp of most recently evicted video data from the ring buffer. Video data with
                                                    // a timestamp less than this cutoff is no longer in the ring buffer
    AudioVideoData         *lastData;               // Most recent data inserted in ring buffer. This is guaranteed to be valid.
    PrerollDataReference    lastVideoDataIDR;       // Most recent sps/pps/Iframe triplet, starting from the sps nal
    PrerollDataReference    lastLastVideoDataIDR;   // Second most recent sps/pps/Iframe triplet, starting from the sps nal
} PrerollRing;
/** The "preroll" stores the most recent video and audio data in a static ring buffer. This historical data allows
 * videos to be started from a point _before_ the present. This is useful for cases such as capturing the entirety of a
 * motion detection event.
 *
 * Note that audio and video timestamp cutoffs are tracked individually since their timestamp data is only accurate
 * relative to themselves. Comparison of timestamps between the two data types can be inaccurate since it is
 * possible for audio and video data to arrive in the ring buffer slightly out-of-order relative to each other (such as
 * due to event proc timing). */

/* _____________________________________________________
   LTMediaMP4Recorder Shared Interfaces and Libraries */

static LTDeviceMedia        *s_DeviceMedia        = NULL;
static LTMediaMP4           *s_MediaMP4           = NULL;
static LTUtilityByteOps     *s_ByteOps            = NULL;
static LTSystemSettings     *s_Settings           = NULL;

// Library-level thread that controls the mp4 recording pipeline
static LTMutex              *s_StartStopMutex     = NULL;
static LTOThread            *s_RecorderThread     = NULL;

// Mutex-protected linked list of all ongoing recordings. This allows a single recording pipeline and preroll to be
// shared between multiple recordings.
static LTMutex              *s_RecordingListMutex = NULL;
static LTArray              *s_RecordingList      = NULL;

// Shared video and audio sources
static LTMediaSource         s_hVideoSource       = LTHANDLE_INVALID;
static ILTMediaSource       *s_iVideoSource       = NULL;
static LTMediaSource         s_hAudioSource       = LTHANDLE_INVALID;
static ILTMediaSource       *s_iAudioSource       = NULL;

// Preroll and backing buffer
static AudioVideoData        s_prerollBacking[PREROLL_SIZE];
static PrerollRing           s_preroll;

static LTMediaMP4Streamer * s_streamer = NULL; // TODO: work on having multiple streams at a time

/* ___________________________________________
   LTMediaMP4RecordingImpl Member Variables */

typedef_LTObjectImpl(LTMediaMP4Recording, LTMediaMP4RecordingImpl) {
    u8      internalUuid[LT_UUID_BYTE_LEN];  // UUID to track individual recordings in the global recording list
    LTTime  lastAudioDuration;               // For calculating if filler needs to be added between audio samples
    LTTime  lastAudioTimestamp;              // For calculating if filler needs to be added between audio samples
    u64     lastVideoTimestamp;              // For calculating if filler needs to be added between video samples
    LTEvent hMetadataEvent;                  // Metadata event for the recording
    LTFile *file;                            // File the mp4 is written to
    LTMediaMP4_Builder        *builder;      // MP4 builder that writes into the file
    LTMediaMP4Recording_Config config;       // Configuration options for the recording
    PrerollDataReference lastAddedData;      // The most recent data that was added to the recording. Used for building
                                             // recordings from the preroll.
    bool                 started;            // Whether or not the mp4 has begun to receive video data
    bool                 stopped;            // A configured stop condition was reached, blocking all further writes to the mp4
}
LTOBJECT_API;

/* _____________________________________________________
   LTMediaMP4RecordingImpl Metadata Event Dispatchers */

static const LTArgsDescriptor s_MetadataEventArgs = {
    3,
    {kLTArgType_pointer, kLTArgType_u32, kLTArgType_pointer}
};

static void DispatchMetadataEvent(LTEvent hEvent, void *proc, LTArgs *eventArgs, void *clientData) {
    LT_UNUSED(hEvent);
    (*(LTMediaMP4Recording_OnMetadataEventProc *)proc)((LTMediaMP4Recording *)LTArgs_pointerAt(0, eventArgs),
                                                       (LTMediaMP4Recording_MetadataType)LTArgs_u32At(1, eventArgs),
                                                       (LTMediaMP4Recording_Metadata *)LTArgs_pointerAt(2, eventArgs),
                                                       clientData);
}

static void DispatchMetadataEventComplete(LTEvent hEvent, LTArgs *eventArgs) {
    LT_UNUSED(hEvent);
    lt_free(LTArgs_pointerAt(2, eventArgs));  // Free LTMediaMP4Recording_Metadata
}

static void NotifyMetadataEvent(LTMediaMP4RecordingImpl         *recording,
                                LTMediaMP4Recording_MetadataType dataType,
                                u64                           nTimestamp) {
    /* Send event out to clients! */
    LTMediaMP4Recording_Metadata *data = lt_malloc(sizeof(LTMediaMP4Recording_Metadata));
    if (!data) return;
    data->nTimestamp      = nTimestamp;
    data->nAudioSamples   = s_MediaMP4->GetCurrentAudioSampleCount(recording->builder);
    data->nVideoSamples   = s_MediaMP4->GetCurrentVideoSampleCount(recording->builder);
    data->nFileSize       = s_MediaMP4->GetCurrentSize(recording->builder);

    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, recording->hMetadataEvent);
    iEvent->NotifyEvent(recording->hMetadataEvent, recording, dataType, data);
}

static void NotifyConfiguredEvents(LTMediaMP4RecordingImpl *recording, WriteResult lastWriteResult, MediaType type, u64 timestamp) {
    /* If enabled, notify write error and stop recording */
    if (recording->config.stopOnWriteError && lastWriteResult == kWriteResult_Fail){
        recording->stopped = true;
        NotifyMetadataEvent(recording, kMetadataType_WriteErrorStop, 0);
    }
    /* If enabled && currentSize > maxSize, notify max size and stop recording */
    else if (recording->config.maxSize > 0 && s_MediaMP4->GetCurrentSize(recording->builder) > recording->config.maxSize) {
        recording->stopped = true;
        NotifyMetadataEvent(recording, kMetadataType_MaxSize, 0);
    }
    /* If enabled && availableRAM < minAvailable, notify low available system memory and stop recording */
    else if (s_cachedAvailableSystemRAM < recording->config.minAvailableSystemMemory) {
        recording->stopped = true;
        NotifyMetadataEvent(recording, kMetadataType_MinAvailableSystemMemory, 0);
    }
    /* If enabled, notify audio write */
    else if (recording->config.audio.events && type == kAudioUnit) {
        NotifyMetadataEvent(recording, kMetadataType_Audio, timestamp);
    }
    /* If enabled, notify video write */
    else if (recording->config.video.events && type == kNalUnit) {
        NotifyMetadataEvent(recording, kMetadataType_Video, timestamp);
    }
}

/* ____________________________________________________
   LTMediaMP4RecordingImpl Public API Implementation */

static bool RemoveRecordingFromList(LTMediaMP4RecordingImpl *recording) {
    /* Lock and remove the recording from list based on uuid */
    bool success = false;
    s_RecordingListMutex->API->Lock(s_RecordingListMutex);
    for (u32 i = 0; i < s_RecordingList->API->GetCount(s_RecordingList); i++) {
        LTMediaMP4RecordingImpl *current = s_RecordingList->API->Get(s_RecordingList, i, NULL);
        if (lt_memcmp(current->internalUuid, recording->internalUuid, LT_UUID_BYTE_LEN) == 0) {
            s_RecordingList->API->Remove(s_RecordingList, i);
            success = true;
            break;
        }
    }
    s_RecordingListMutex->API->Unlock(s_RecordingListMutex);
    return success;
}

static bool LTMediaMP4RecordingImpl_Init(LTMediaMP4RecordingImpl   *recording,
                                         LTFile                    *file,
                                         LTMediaMP4Recording_Config config) {
    do {
        s_ByteOps->GenUUID(recording->internalUuid);
        recording->file   = file;
        recording->config = config;

        recording->hMetadataEvent = LT_GetCore()->CreateEvent(&s_MetadataEventArgs,
                                                              DispatchMetadataEvent,
                                                              DispatchMetadataEventComplete,
                                                              NULL,
                                                              NULL);
        if (!recording->hMetadataEvent) break;

        LTMediaMP4_AudioConfig audioConfig = {
            .nBytesPerSample = kBytesPerAudioSample,
            .nSamplesPerSec = s_audioSampleRate,
        };

        if (!file) {
            s_streamer = lt_createobject(LTMediaMP4Streamer); // TODO: make separate function for Mp4Stream
            if (!s_streamer) { break; }
            s_streamer->API->Start(s_streamer, audioConfig);
            recording->builder = s_streamer->API->GetBuilder(s_streamer);
        } else {
            recording->builder = s_MediaMP4->CreateBuilder(audioConfig, recording->file);
        }

        if (!recording->builder) break;

        return true;
    } while (false);

    lt_destroyhandle(recording->hMetadataEvent);
    return false;
}

static void LTMediaMP4RecordingImpl_Start(LTMediaMP4RecordingImpl *recording) {
    /* Lock and add recording to the list to asynchronously start recording */
    s_RecordingListMutex->API->Lock(s_RecordingListMutex);
    s_RecordingList->API->Append(s_RecordingList, recording);
    s_RecordingListMutex->API->Unlock(s_RecordingListMutex);
}

static void LTMediaMP4RecordingImpl_OnMetadataEvent(LTMediaMP4RecordingImpl                 *recording,
                                                    LTMediaMP4Recording_OnMetadataEventProc *eventProc,
                                                    LTThread_ClientDataReleaseProc          *clientDataReleaseProc,
                                                    void                                    *clientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, recording->hMetadataEvent);
    iEvent->RegisterForEvent(recording->hMetadataEvent, eventProc, clientDataReleaseProc, clientData, false);
}

static void LTMediaMP4RecordingImpl_NoMetadataEvent(LTMediaMP4RecordingImpl                 *recording,
                                                    LTMediaMP4Recording_OnMetadataEventProc *eventProc) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, recording->hMetadataEvent);
    iEvent->UnregisterFromEvent(recording->hMetadataEvent, eventProc);
}

static void LTMediaMP4RecordingImpl_Cancel(LTMediaMP4RecordingImpl *recording) {
    /* Synchronously remove recording from list and cancel the mp4 */
    if (RemoveRecordingFromList(recording)) {
        s_MediaMP4->FreeBuilder(recording->builder);
        recording->builder = NULL;
    }
}

static LTMediaMP4_Stats LTMediaMP4RecordingImpl_Finish(LTMediaMP4RecordingImpl *recording) {
    /* Synchronously remove recording from list and finalize the mp4 */
    if (RemoveRecordingFromList(recording)) {
        if (s_streamer) {
            s_streamer->API->Finish(s_streamer);
            return (LTMediaMP4_Stats){};
        } else {
            LTMediaMP4_Stats stats = s_MediaMP4->Finish(recording->builder); // Finish() frees the builder
            recording->builder = NULL;
            return stats;
        }
    }
    return (LTMediaMP4_Stats){};
}

static LTFile *LTMediaMP4RecordingImpl_GetFile(LTMediaMP4RecordingImpl *recording) {
    return recording->file;
}

static LTBuffer *LTMediaMP4RecordingImpl_GetBuffer(LTMediaMP4Recording *recording) {
    // Not supported in this specialisation of the recorder
    LT_UNUSED(recording);
    return NULL;
}


/* _____________________________________________________
   LTMediaMP4RecordingImpl Constructor and Destructor */

static void LTMediaMP4RecordingImpl_DestructObject(LTMediaMP4RecordingImpl *recording) {
    lt_destroyhandle(recording->hMetadataEvent);
    LTMediaMP4RecordingImpl_Cancel(recording);
}

static bool LTMediaMP4RecordingImpl_ConstructObject(LTMediaMP4RecordingImpl *recording) {
    LT_UNUSED(recording);
    return true;
}

/* _________________________________________
   LTMediaMP4RecordingImpl API Definition */

define_LTObjectImplPublic(LTMediaMP4Recording, LTMediaMP4RecordingImpl,
    Init,
    Start,
    OnMetadataEvent,
    NoMetadataEvent,
    Cancel,
    Finish,
    GetFile,
    GetBuffer,
);

/* ____________________________________________________
   LTMediaMP4Recorder Preroll Ring Buffer Management */

static LTMediaNalType GetNalType(u8 *data) {
    /* Get the type of a NAL unit while accounting for the start code */
    const u8       startCode[4] = {0, 0, 0, 1};
    LTMediaNalType nalType      = lt_memcmp(data, startCode, 4) == 0 ? data[4] & 0x1F : data[0] & 0x1F;
    return nalType;
}

static bool IsReferenceValid(PrerollDataReference *reference) {
    if (reference->ref == NULL) return false;
    /* If the timestamp of the reference is older than the oldest unit in the ring buffer, the reference is no longer
     * valid. Audio and video units are checked separately since their timestamps are only relative to themselves. */
    u64 timestampCutoff = reference->type == kAudioUnit ? s_preroll.audioTimestampCutoff
                                                        : s_preroll.videoTimestampCutoff;
    // Special case: If the cutoff is zero, all timestamps are valid. This accounts for the case if driver sends a
    // timestamp starting at zero.
    return reference->dataTimestamp > timestampCutoff || timestampCutoff == 0;
}

static AudioVideoData *GetNextData(AudioVideoData *data) {
    /* Retrieve the next data entry in the ring buffer */
    AudioVideoData *next              = NULL;
    u32             elementsUntilNext = data->elementLen;
    if (data->hasNext) {
        if (LTRingCalculateIndex(&s_preroll.ring, data) + elementsUntilNext == PREROLL_SIZE) {
            // Next will wrap around the ring, back to absolute index 0
            next = LTRingAt(&s_preroll.ring, 0);
        } else {
            // Next is contiguous and will not wrap
            next = data + elementsUntilNext;
        }
    }
    return next;  // NULL indicates data has no next entry in the ring buffer
}

static PrerollDataReference GetNextReference(PrerollDataReference *currentRef) {
    /* Advance to the next valid entry in the linked list */
    AudioVideoData *next = NULL;
    if (IsReferenceValid(currentRef)) {
        // Current ref is within the valid time range, advance and update
        next = GetNextData(currentRef->ref);
    } else {
        // Current is not within valid time range. Pick the next closest valid data by checking from the oldest
        // available (lastlastSps) to the newest (lastData)
        if (IsReferenceValid(&s_preroll.lastLastVideoDataIDR) &&
                currentRef->dataTimestamp < s_preroll.lastLastVideoDataIDR.dataTimestamp &&
                LTTime_IsLessThan(LT_GetCore()->GetKernelTime(),
                    LTTime_Add(LTTime_Microseconds(s_preroll.lastLastVideoDataIDR.dataTimestamp), LTTime_Milliseconds(s_maxPrerollDuration)))) {
            next = s_preroll.lastLastVideoDataIDR.ref;
            P("skip.to", "lastlastsps");
        } else if (IsReferenceValid(&s_preroll.lastVideoDataIDR) && currentRef->dataTimestamp < s_preroll.lastVideoDataIDR.dataTimestamp) {
            next = s_preroll.lastVideoDataIDR.ref;
            P("skip.to", "lastsps");
        }
        // Final fallback: the most recent data inserted into the ring buffer
        else if (s_preroll.lastData != NULL && currentRef->dataTimestamp < s_preroll.lastData->timestamp) {
            next = s_preroll.lastData;
            P("skip.to", "current");
        } else {
            P("skip.to", "none");
        }
    }

    // If next is NULL, return ref == NULL to signify no valid next could be found.
    return next != NULL ? (PrerollDataReference){.type = next->type, .dataTimestamp = next->timestamp, .ref = next}
                        : (PrerollDataReference){.ref = NULL};
}

static u32 CalculateNeededElements(u32 dataLen, LT_SIZE sizeOfElement) {
    u32 neededElements  = 0;
    neededElements     += 1;                            // Data header
    neededElements     += dataLen / sizeOfElement + 1;  // Number of elements needed to store the raw audio/video data, plus
                                                        // padding for remainder bytes. Note that 1 element will be wasted
                                                        // if dataLen divides evenly into sizeOfElement.
    return neededElements;
}

/* Returns FALSE if tail data failed to be freed */
static bool FreeTail(PrerollRing *preroll) {
    /* Move the tail past the oldest entry in the ring buffer, returning it to the writer. Also update cutoffs. */
    AudioVideoData *oldestData = LTRingPeek(&preroll->ring, 0);
    if (oldestData->type == kAudioUnit)
        preroll->audioTimestampCutoff = oldestData->timestamp;
    else if (oldestData->type == kNalUnit)
        preroll->videoTimestampCutoff = oldestData->timestamp;
    u8 *consumedData = LTRingConsume(&preroll->ring, oldestData->elementLen);
    return consumedData != NULL;  // Non-null value == successful consume
}

/* Returns FALSE if head needed to be wrapped around but failed to do so */
static bool TryMoveHeadOverWrapAround(PrerollRing *preroll) {
    u32 contigCap    = LTRingGetContiguousCapacity(&preroll->ring);
    u32 nonContigCap = LTRingGetCapacity(&preroll->ring);
    /* If contiguous capacity is less than the total capacity, it indicates the write head is near the wrap-around point
     * of the ring buffer and must be moved in order to access more contiguous storage space */
    if (contigCap < nonContigCap) {
        // Move write head to the wrap around point by extending the last data
        if (!preroll->lastData) return false;  // No last data to extend
        AudioVideoData *extension = LTRingReserve(&preroll->ring, contigCap);
        if (!extension) return false;
        preroll->lastData->elementLen += contigCap;
    }
    return true;
}

/* Returns FALSE if write head cannot reserve enough room for neededElements */
static bool WriteHead(PrerollRing *preroll, u32 neededElements, LTMediaData *data, MediaType type) {
    /* Assume enough contiguous room exists in the ring buffer. Save the newest data! */
    AudioVideoData *newestData = LTRingReserve(&preroll->ring, neededElements);
    if (!newestData) return false;
    newestData->elementLen     = neededElements;
    newestData->type           = type;
    newestData->timestamp      = data->nTimestamp;
    newestData->timestampDelta = data->nTimestampDelta;
    newestData->hasNext        = false;
    newestData->dataLen        = data->nDataLen;
    // newestData->data points to the block of memory directly after the current struct. Assumes neededElements was
    // computed properly and has enough room to store data->pData
    lt_memcpy(newestData->data, data->pData, data->nDataLen);

    /* Update the ring buffer linked list */
    if (preroll->lastData) preroll->lastData->hasNext = true;
    preroll->lastData = newestData;

    /* Track oldest IDR frames */
    if (type == kNalUnit && GetNalType(newestData->data) == kLTMediaNalTypeSps) {
        // Copy last into lastlast
        preroll->lastLastVideoDataIDR = preroll->lastVideoDataIDR;

        // Save the most recent sps
        preroll->lastVideoDataIDR.type          = kNalUnit;
        preroll->lastVideoDataIDR.dataTimestamp = newestData->timestamp;
        preroll->lastVideoDataIDR.ref           = newestData;
    }

    return true;
}

/* Returns FALSE if data was unable to be saved to the preroll */
static bool SaveAudioVideoDataToPreroll(LTMediaData *data, MediaType type) {
    u32 neededElements = CalculateNeededElements(data->nDataLen, sizeof(AudioVideoData));
    if (neededElements >= PREROLL_SIZE) return false;  // Data is too big for the ring buffer, do not try to store

    /* Free contiguous space in the ring buffer if needed */
    bool success = false;
    while (LTRingGetContiguousCapacity(&s_preroll.ring) < neededElements) {
        // Free 1 data entry
        success = FreeTail(&s_preroll);
        if (!success) break;

        // If enough room has been created, break
        if (LTRingGetContiguousCapacity(&s_preroll.ring) >= neededElements) break;

        // Else, check if write head needs to be moved over the wrap-around
        success = TryMoveHeadOverWrapAround(&s_preroll);
        if (!success) break;

        // Repeat until enough contiguous capacity exists
    }

    /* Enough contiguous space has been created, write the new data into the ring buffer */
    success = WriteHead(&s_preroll, neededElements, data, type);
    return success;
}

/* __________________________________
   LTMediaMP4Recorder MP4 Building */

static LTTime CalculateAudioDataDuration(u32 audioDataLength) {
    /* Time per sample x Number of samples (assumes 1 byte of data == 1 sample) */
    LTTime timePerSample = LTTime_Divide(LTTime_Seconds(1), s_audioSampleRate);
    return LTTime_Multiply(timePerSample, audioDataLength);
}

static WriteResult FillAudioBetweenSamplesInMp4(LTMediaMP4RecordingImpl *recording, u64 newAudioTimestamp) {
    LTTime timeBetweenAudio  = LTTime_Subtract(LTTime_Microseconds(newAudioTimestamp), recording->lastAudioTimestamp);
    LTTime lastAudioDuration = recording->lastAudioDuration;

    if (LTTime_IsGreaterThan(timeBetweenAudio, lastAudioDuration)) {
        LTTime timePerSample = LTTime_Divide(LTTime_Seconds(1), s_audioSampleRate);
        LTTime durationToPad = LTTime_Subtract(timeBetweenAudio, lastAudioDuration);
        u64    samplesToPad  = LTTime_GetMicroseconds(durationToPad) / LTTime_GetMicroseconds(timePerSample);

        if (samplesToPad < kMinimumDroppedSamplesToPad) return kWriteResult_None;
        if (samplesToPad > s_audioSampleRate) {
            // If over 1s of audio is trying to be filled, something else has likely gone wrong,
            // such as integer overflow with above values or audio pipeline failure
            LTLOG_DEBUG("aud.fill.excessive", NULL);
            return kWriteResult_None;
        }

        // Create silent audio samples to insert
        u64 bytesToPad     = samplesToPad * kBytesPerAudioSample;
        u8 *paddingSamples = lt_malloc(bytesToPad);
        if (!paddingSamples) {
            LTLOG_DEBUG("aud.fill.OOM", NULL);
            return kWriteResult_None;
        }
        lt_memset(paddingSamples, ~0, bytesToPad);  // Set to 0xFF for silent PCMU audio sample

        bool writeSuccess = s_MediaMP4->AddAudioSamples(recording->builder, paddingSamples, bytesToPad);
        lt_free(paddingSamples);
        return writeSuccess ? kWriteResult_Success : kWriteResult_Fail;
    }
    return false;
}

static WriteResult WriteVideo(LTMediaMP4RecordingImpl *recording, u8 *data, u32 dataLen, LTTime delta) {
    /* Video data must start on an sps+pps+IDR frame */
    if (s_MediaMP4->GetCurrentVideoSampleCount(recording->builder) == 0 && !recording->started) {
        LTMediaNalType nalType = GetNalType(data);
        if (nalType == kLTMediaNalTypeSps) {
            // Mark the recording as started once the first sps nal is written, which marks the start of an IDR frame
            // (sps+pps+IDR frame). However, the video sample count will not increment until the actual IDR frame
            // is written, which is why this flag is needed.
            recording->started = true;
        } else {
            return kWriteResult_None;
        }
    }

    /* If time between samples is a significant* amount greater than 50ms (1/20 second), a video frame must have been
     * dropped from the 20fps video. To compensate, extend the duration of the last frame by the number of dropped
     * frames, rounded down.
     *
     * If >1 second of video is missing, something has likely gone wrong. Assume the frame delta is inaccurate and skip extending the last frame.
     *
     * *NOTE: Currently, adding an extra frame will add 50ms to the video, as the video is a fixed 20fps.
     *        So, the margin should not be too small, or else the video will be extended too often and stray from the
     *        real duration.
     *
     * TODO: To more accurately compensate for late frames, could the video be upscaled to a higher frame rate?
     *       Or is there something else that can be done with the mp4 format?
     *       Or, this library can keep track of an "accrued delay debt" and add a frame once enough late frame debt is
     *       built up? */
    if (LTTime_IsGreaterThan(delta, LTTime_Milliseconds(s_cameraMsecPerFrame + kDroppedFrameMsecMargin))
        && LTTime_IsLessThan(delta, LTTime_Seconds(1))) {
        u32 droppedFrames = ((u32)LTTime_GetMilliseconds(delta)) / s_cameraMsecPerFrame;
        if (droppedFrames > 0) {
            P("extend", "Extend last frame by: %lu frames (%lu delta)", LT_Pu32(droppedFrames), LT_Pu32(LTTime_GetMilliseconds(delta)));
            s_MediaMP4->ExtendLastH264Frame(recording->builder, droppedFrames);
        }
    }

    /* Write the new video data and return the result */
    bool writeSuccess = s_MediaMP4->AddH264StreamData(recording->builder, data, dataLen);
    return writeSuccess ? kWriteResult_Success : kWriteResult_Fail;
}

static WriteResult WriteAudio(LTMediaMP4RecordingImpl *recording, u8 *data, u32 dataLen, u64 timestamp) {
    /* If recording has a video track, only insert audio samples after the first video frame */
    if (recording->config.video.enabled && s_MediaMP4->GetCurrentVideoSampleCount(recording->builder) == 0) {
        return kWriteResult_None;
    }

    /* If time between audio samples is greater than a threshold, add extra audio samples to pad the time discrepancy.
     * No need to pad if this is the first audio sample being added to the recording.
     *
     * TODO: Should extra samples be added align the start of the audio track with the start of the video track? The
     * timing difference seems to be minimal, so this is low priority */
    if (s_MediaMP4->GetCurrentAudioSampleCount(recording->builder) > 0
        && !LTTime_IsZero(recording->lastAudioTimestamp)) {
        FillAudioBetweenSamplesInMp4(recording, timestamp);
    }

    /* Write the current audio data */
    bool writeSuccess = s_MediaMP4->AddAudioSamples(recording->builder, data, dataLen);

    /* Keep track of the audio data that was written */
    if (writeSuccess) {
        recording->lastAudioTimestamp = LTTime_Microseconds(timestamp);
        recording->lastAudioDuration  = CalculateAudioDataDuration(dataLen);
    }
    return writeSuccess ? kWriteResult_Success : kWriteResult_Fail;
}

static WriteResult AddDataToMP4(LTMediaMP4RecordingImpl *recording,
                                AudioVideoData          *currentData,
                                u8                      *nonContiguousData) {
    u8 *data = nonContiguousData != NULL ? nonContiguousData : currentData->data;
    /* Add video data */
    if (currentData->type == kNalUnit) {
        if (!recording->config.video.enabled) return kWriteResult_Skip;
        // P("add.video", "%llu | video: %lu", LT_Pu64(currentData->timestamp), LT_Pu32(currentData->dataLen));
        return WriteVideo(recording, data, currentData->dataLen, currentData->timestampDelta);
    }
    /* Add audio data */
    else if (currentData->type == kAudioUnit) {
        if (!recording->config.audio.enabled) return kWriteResult_Skip;
        // P("add.audio", "%llu | audio: %lu", LT_Pu64(currentSample->timestamp), LT_Pu32(currentSample->len));
        return WriteAudio(recording, data, currentData->dataLen, currentData->timestamp);
    }
    return kWriteResult_None;
}

static WriteResult BuildMP4FromPreroll(LTMediaMP4RecordingImpl *recording) {
    /* Write data from the preroll until the head of the preroll (the present) is reached.
     * TODO: Should this be performed over time instead of all in one go? If this operation is too heavy, it MAY slow
     * down the video pipeline for all consumers */
    WriteResult result = kWriteResult_None;
    do {
        // Obtain the next valid consecutive data
        PrerollDataReference nextData = GetNextReference(&recording->lastAddedData);
        // Valid next does not exist (current == head of preroll). Skip writing to the mp4 and return the last result.
        if (nextData.ref == NULL) break;
        // Valid data exists. Write and update.
        result = AddDataToMP4(recording, nextData.ref, NULL);
        if (result == kWriteResult_Success || result == kWriteResult_Skip) recording->lastAddedData = nextData;
        else if (result == kWriteResult_Fail) break;
    } while (recording->lastAddedData.ref != NULL);
    return result;
}

static void BuildMP4(LTMediaMP4RecordingImpl *recording, LTMediaData *data, MediaType type) {
    /* Recording has reached a configured stop condition, end writes */
    if (recording->stopped) return;
    /* Video is not enabled for this recording, don't act on NAL units */
    if (type == kNalUnit && !recording->config.video.enabled) return;
    /* Audio is not enabled for this recording, don't act on audio units */
    if (type == kAudioUnit && !recording->config.audio.enabled) return;
    /* Pipeline preroll is not enabled but video wants preroll, invalid condition */
    if (!s_enablePreroll && recording->config.video.preroll) return;

    WriteResult result = kWriteResult_None;

    /* Current video is configured to use preroll. Add in data from the preroll */
    if (recording->config.video.preroll) {
        result = BuildMP4FromPreroll(recording);
        // At this point, recording will have traversed to the head of the preroll and added the latest sample, so
        // data->nTimestamp will be accurate.
    }
    /* Video does not use preroll. Directly add in the newest data */
    else {
        // Wrap the video data in the struct in order to use AddSampleToMp4. No need to malloc and duplicate the data.
        AudioVideoData latestSample = {
            .dataLen        = data->nDataLen,
            .timestamp      = data->nTimestamp,
            .timestampDelta = data->nTimestampDelta,
            .type           = type,
        };
        // Noncontiguous data is passed in separately
        result = AddDataToMP4(recording, &latestSample, data->pData);
    }

    /* Notify metadata for this recording */
    NotifyConfiguredEvents(recording, result, type, data->nTimestamp);
}

/* ________________________________________________
   LTMediaMP4Recorder Video and Audio Event Proc */

static void VideoCallback(LTMediaEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Error) return;

    LTMediaData *data = eventData;
    if (s_enablePreroll) SaveAudioVideoDataToPreroll(data, kNalUnit);

    s_RecordingListMutex->API->Lock(s_RecordingListMutex);
    for (u32 i = 0; i < s_RecordingList->API->GetCount(s_RecordingList); i++) {
        LTMediaMP4RecordingImpl *current = s_RecordingList->API->Get(s_RecordingList, i, NULL);
        if (!current) continue;
        BuildMP4(current, data, kNalUnit);
    }
    s_RecordingListMutex->API->Unlock(s_RecordingListMutex);
}

static void AudioCallback(LTMediaEvent event, void *eventData, void *pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Error) return;

    LTMediaData *data = eventData;
    if (s_enablePreroll) SaveAudioVideoDataToPreroll(data, kAudioUnit);

    s_RecordingListMutex->API->Lock(s_RecordingListMutex);
    for (u32 i = 0; i < s_RecordingList->API->GetCount(s_RecordingList); i++) {
        LTMediaMP4RecordingImpl *current = s_RecordingList->API->Get(s_RecordingList, i, NULL);
        if (!current) continue;
        BuildMP4(current, data, kAudioUnit);
    }
    s_RecordingListMutex->API->Unlock(s_RecordingListMutex);
}

/* __________________________________________________
   LTMediaMP4Recorder Available System Memory Poll */

static void SetCachedAvailableSystemMemory(void *clientData) {
    LT_UNUSED(clientData);
    s_cachedAvailableSystemRAM = LT_GetCore()->GetAvailableSystemRAM();
}

/* _____________________________________________
   LTMediaMP4Recorder Camera Thread Init/Fini */

static void ApplyProductConfig(void) {
    LTProductConfig *productConfig = NULL;
    do {
        /* Read library section */
        productConfig = lt_openlibrary(LTProductConfig);
        if (!productConfig) break;
        u32 section = productConfig->GetLibraryConfigSection("LTMediaMP4Recorder");
        if (!section) break;
        /* Apply config if available */
        u32 sampleRate = (u32)productConfig->ReadInteger(section, "AudioSampleRate");
        if (sampleRate) s_audioSampleRate = sampleRate;
        u32 maxPreroll = (u32)productConfig->ReadInteger(section, "MaxPrerollDuration");
        if (maxPreroll) s_maxPrerollDuration = maxPreroll;

    } while (false);
    lt_closelibrary(productConfig); // NULL OK
}

static void EndVideoAudioSources(void) {
    /* Close video source */
    if (s_hVideoSource) {
        s_iVideoSource->NoMediaEvent(s_hVideoSource, VideoCallback);
        s_iVideoSource->Stop(s_hVideoSource);
        lt_destroyhandle(s_hVideoSource);
        s_hVideoSource = LTHANDLE_INVALID;
        s_iVideoSource = NULL;
    }

    /* Close audio source */
    if (s_hAudioSource) {
        s_iAudioSource->NoMediaEvent(s_hAudioSource, AudioCallback);
        s_iAudioSource->Stop(s_hAudioSource);
        lt_destroyhandle(s_hAudioSource);
        s_hAudioSource = LTHANDLE_INVALID;
        s_iAudioSource = NULL;
    }

    /* End system memory poll */
    s_RecorderThread->API->KillTimer(s_RecorderThread, SetCachedAvailableSystemMemory, NULL);
}

static bool CameraThreadInit(void) {
    P("init", "Camera thread init");

    do {
        /* Read and apply values defined in the ProductConfig */
        ApplyProductConfig();

        /* Initialize preroll ring buffer */
        s_preroll = (PrerollRing){};
        LTRingInit(&s_preroll.ring, s_prerollBacking, PREROLL_SIZE);

        /* Start the shared video source */
        LTMediaFormat videoFormat = {
            .nKind                = kLTMediaKind_Video_HD,
            .nEncoding            = kDefaultVideoEncoding,
            .params.h264.nProfile = kDefaultH264Profile,
        };
        s_hVideoSource = s_DeviceMedia->OpenSource(&videoFormat);
        if (!s_hVideoSource) {
            LTLOG_YELLOWALERT("init.vsrc", "Unable to create video source");
            break;
        }
        s_iVideoSource = lt_gethandleinterface(ILTMediaSource, s_hVideoSource);
        s_iVideoSource->OnMediaEvent(s_hVideoSource, VideoCallback, NULL);
        s_iVideoSource->Start(s_hVideoSource);
        s64 value = 0;
        s_Settings->GetIntegerValue(settingsKeyGopLength, &value);
        s_iVideoSource->SetGopLength(
                s_hVideoSource,
                value > 0 ? (u32)value : kDefaultGopLength);

        /* Start the shared audio source */
        LTMediaFormat audioFormat = {
            .nKind                   = kLTMediaKind_Audio,
            .nEncoding               = kDefaultAudioEncoding,
            .params.g711.nSampleRate = s_audioSampleRate,
        };
        s_hAudioSource = s_DeviceMedia->OpenSource(&audioFormat);
        if (!s_hAudioSource) {
            LTLOG_YELLOWALERT("init.asrc", "Unable to create audio source");
            break;
        }
        s_iAudioSource = lt_gethandleinterface(ILTMediaSource, s_hAudioSource);
        s_iAudioSource->OnMediaEvent(s_hAudioSource, AudioCallback, NULL);
        s_iAudioSource->Start(s_hAudioSource);

        s_DeviceMedia->GetProperty("camera.msec", &s_cameraMsecPerFrame);

        /* Start the system memory polling timer. Polling and caching system memory at a fixed interval is less
         * intensive than polling system memory on every video/audio data write */
        s_RecorderThread->API->SetTimer(s_RecorderThread, LTTime_Seconds(kSystemMemoryPollIntervalSeconds), SetCachedAvailableSystemMemory, NULL, NULL);

        return true;
    } while (false);

    EndVideoAudioSources();
    return false;
}

static void CameraThreadExit(void) {
    P("exit", "Camera thread exit");
    EndVideoAudioSources();
}

/* _______________________________________________
   LTMediaMP4Recorder Public API Implementation */

static LTMediaMP4Recording *LTMediaMP4Recorder_CreateRecording(LTFile *file, LTMediaMP4Recording_Config config) {
    /* Recorder thread not started, don't create recording */
    if (!s_RecorderThread) return NULL;

    /* Create and initialize recording */
    LTMediaMP4Recording *recording = lt_createobject(LTMediaMP4Recording);
    if (!recording) return NULL;
    if (!recording->API->Init(recording, file, config)) {
        lt_destroyobject(recording);
        return NULL;
    } else {
        return recording;
    }
}

static void EndOngoingVideos(void) {
    /* Remove and cancel all recordings in list */
    s_RecordingListMutex->API->Lock(s_RecordingListMutex);
    while (s_RecordingList->API->GetCount(s_RecordingList)) {
        LTMediaMP4Recording *recording = s_RecordingList->API->Get(s_RecordingList, 0, NULL);
        s_RecordingList->API->Remove(s_RecordingList, 0);
        recording->API->Cancel(recording);
    }
    s_RecordingListMutex->API->Unlock(s_RecordingListMutex);
}

static void LTMediaMP4Recorder_Stop(void) {
    EndOngoingVideos();

    /* Terminate the recorder thread if it exists */
    s_StartStopMutex->API->Lock(s_StartStopMutex);
    if (s_RecorderThread) {
        s_RecorderThread->API->Terminate(s_RecorderThread);
        s_RecorderThread->API->WaitUntilFinished(s_RecorderThread, LTTime_Infinite());
        lt_destroyobject(s_RecorderThread);
        s_RecorderThread = NULL;
    }
    s_StartStopMutex->API->Unlock(s_StartStopMutex);
}

static bool LTMediaMP4Recorder_Start(bool enablePreroll) {
    s_StartStopMutex->API->Lock(s_StartStopMutex);
    /* Recorder thread already exists */
    if (s_RecorderThread) {
        s_StartStopMutex->API->Unlock(s_StartStopMutex);
        return true;
    }

    /* Else, create and start the thread */
    s_enablePreroll  = enablePreroll;
    s_RecorderThread = lt_createobject(LTOThread);
    if (!s_RecorderThread) {
        s_StartStopMutex->API->Unlock(s_StartStopMutex);
        return false;
    }
    s_RecorderThread->API->SetStackSize(s_RecorderThread, 2048);  // TODO: trim thread stack size
    s_RecorderThread->API->Start(s_RecorderThread, "LTMediaMP4Recorder", &CameraThreadInit, &CameraThreadExit);

    s_StartStopMutex->API->Unlock(s_StartStopMutex);
    return true;
}

/* ____________________________________________
   LTMediaMP4Recorder Library Root Interface */

static void LTMediaMP4RecorderImpl_LibFini(void) {
    /* Stop recording pipeline and preroll */
    LTMediaMP4Recorder_Stop();
    lt_destroyobject(s_StartStopMutex);
    s_StartStopMutex = NULL;

    EndOngoingVideos();
    lt_destroyobject(s_RecordingList);
    s_RecordingList = NULL;
    lt_destroyobject(s_RecordingListMutex);
    s_RecordingListMutex = NULL;

    /* Close libraries */
    lt_closelibrary(s_Settings);
    lt_closelibrary(s_DeviceMedia);
    lt_closelibrary(s_MediaMP4);
    lt_closelibrary(s_ByteOps);
}

static bool LTMediaMP4RecorderImpl_LibInit(void) {
    do {
        /* Libraries */
        s_DeviceMedia = lt_openlibrary(LTDeviceMedia);
        if (!s_DeviceMedia) break;
        s_MediaMP4 = lt_openlibrary(LTMediaMP4);
        if (!s_MediaMP4) break;
        s_ByteOps = lt_openlibrary(LTUtilityByteOps);
        if (!s_ByteOps) break;
        s_Settings = lt_openlibrary(LTSystemSettings);
        if (!s_Settings) break;

        /* List to manage ongoing recordings */
        s_RecordingListMutex = lt_createobject(LTMutex);
        if (!s_RecordingListMutex) break;
        s_RecordingList = lt_createobject_typed(LTArray, List);
        if (!s_RecordingList) break;

        /* Mutex for controlling library start/stop */
        s_StartStopMutex = lt_createobject(LTMutex);
        if (!s_StartStopMutex) break;

        return true;
    } while (false);

    LTMediaMP4RecorderImpl_LibFini();
    return false;
}

define_LTLIBRARY_ROOT_INTERFACE(LTMediaMP4Recorder)
    .CreateRecording    = LTMediaMP4Recorder_CreateRecording,
    .Start              = LTMediaMP4Recorder_Start,
    .Stop               = LTMediaMP4Recorder_Stop,
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTMediaMP4Recorder,
    (LTMediaMP4RecordingImpl)
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-May-24   aurelian    created
 *  22-Jul-24   decius      added streaming function
 */
