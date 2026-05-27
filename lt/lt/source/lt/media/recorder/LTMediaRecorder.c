/*******************************************************************************
 * lt/source/media/recorder/LTMediaRecorder.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/recorder/LTMediaRecorder.h>
#include <lt/media/mp4/recorder/LTMediaMP4Recorder.h>
#include <lt/media/mp4/LTMediaMP4.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/system/timezone/LTSystemTimeZone.h>

DEFINE_LTLOG_SECTION("media.rec")

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTMediaRecorder,
    (LTDeviceMedia)
    (LTMediaMP4)
    (LTSystemTimeZone)
    (LTMediaMP4Recorder)
);

enum {
    kDefaultGopLength = 200,   // I-frame every 10 seconds based on 20 fps
};

typedef enum {
    kStopReason_Success,
    kStopReason_Failure,
} StopReason;

typedef struct {
    bool audioEnabled;

    struct {
        LTEvent              hEvent;        // notify clients timelapse is complete
        LTFile              *file;          // file for timelapse
        LTMediaRecorder_Snapshot *snapshot; // thumbnail
        LTTime               lastIFrameTimestamp; // Kernel time
        LTTime               interval;
        LTTime               endAtTimestamp; // Kernel time
        LTTime               startTimeEpoch;
        LTTime               endTimeEpoch;
    } timelapse;

    struct {
        LTEvent              hEvent;        // notify clients a continuous recording chunk is complete
        LTFile              *file;          // file for continuous recording
        LTMediaMP4Recording *recording;
        LTFile              *localSaveDir;  // base directory to save continuous recordings to
        u64                  nextMinute;
        LTTime               startTimeEpoch;
        u64                  lastFrameTimestamp;
    } record;

} CameraState;

static LTThread              s_hCameraThread    = 0;
static LTMutex              *s_StartStopMutex   = NULL;
static ILTThread            *s_iThread          = NULL;
static LTSystemSettings     *s_Settings         = NULL;

static LTMediaSource         s_VideoSource; // For timelapse ONLY
static ILTMediaSource       *s_iVideoSource;
static LTMediaResolution     s_videoResolution;
static LTMediaSource         s_SnapshotSource;
static ILTMediaSource       *s_iSnapshotSource;
static LTMediaResolution     s_snapshotResolution;

static CameraState           s_camState;

static const char settingsKeyGopLength[] = "recorder/goplength";

/*_______________________
  Forward declarations */
static void StopRecordTimelapse(StopReason reason);

/*____________________________________
  Shared file object implementation */
typedef_LTObjectImpl(LTSharedFile, LTSharedFileImpl) {
    LTFile      *fileToShare;
    LTString     fileToShareName;
    LTMutex     *mutex;
    u32          referenceCount;
    bool         deleteFileOnRelease;
} LTOBJECT_API;

static bool LTSharedFileImpl_ConstructObject(LTSharedFileImpl *sharedFile) {
    sharedFile->mutex = lt_createobject(LTMutex);
    if (!sharedFile->mutex) return false;
    return true;
}

static void LTSharedFileImpl_DestructObject(LTSharedFileImpl *sharedFile) {
    lt_destroyobject(sharedFile->mutex);
    sharedFile->mutex = NULL;
    ltstring_destroy(sharedFile->fileToShareName);
}

static void LTSharedFileImpl_Init(LTSharedFileImpl *sharedFile, LTFile *fileToShare, bool deleteFileOnRelease) {
    sharedFile->fileToShare         = fileToShare;
    sharedFile->deleteFileOnRelease = deleteFileOnRelease;

    sharedFile->fileToShareName = ltstring_create("");
    sharedFile->fileToShare->API->GetName(sharedFile->fileToShare, &sharedFile->fileToShareName);
}

static LTLinkFile *LTSharedFileImpl_Reserve(LTSharedFileImpl *sharedFile){
    sharedFile->mutex->API->Lock(sharedFile->mutex);
    ++sharedFile->referenceCount;
    sharedFile->mutex->API->Unlock(sharedFile->mutex);

    // Create a new LTFile using the name of the shared video file
    LTLinkFile *linkFile = lt_createobject(LTFile); // malloc
    linkFile->API->SetName(linkFile, sharedFile->fileToShareName);
    return linkFile;
}

static void LTSharedFileImpl_Release(LTSharedFileImpl *sharedFile, LTLinkFile *linkFile) {
    // Always close and destroy the linkFile
    if (linkFile) {
        lt_destroyobject(linkFile);
    }

    // Decrement refcount and destroy the file if refcount==0
    // This will always be executed on the last Reserve/Release pair
    sharedFile->mutex->API->Lock(sharedFile->mutex);
    --sharedFile->referenceCount;

    if (sharedFile->fileToShare && sharedFile->referenceCount <= 0) {
        // Delete and destroy the underlying LTFile
        if (sharedFile->deleteFileOnRelease) {
            LTFile *file = sharedFile->fileToShare;
            file->API->Delete(file, false);
            lt_destroyobject(file);
            LTLOG("sf.del", "Deleted: %s", sharedFile->fileToShareName);
        }

        // Log success
        LTLOG_DEBUG("sf.end", "Deleting shared file around: %s", sharedFile->fileToShareName);

        // Destroy this very SharedFile object if refcount hits 0
        sharedFile->mutex->API->Unlock(sharedFile->mutex);
        lt_destroyobject(sharedFile);
        return;
    }
    sharedFile->mutex->API->Unlock(sharedFile->mutex);
}

define_LTObjectImplPublic(LTSharedFile, LTSharedFileImpl,
    Init,
    Reserve,
    Release
);

/*_________________________________
  Library private implementation */

static LTMediaRecorder_Video *MallocVideoWrapper(LTSharedFile *sharedFile, LTTime duration, LTTime startTimeEpoch, LTTime endTimeEpoch){
    LTMediaRecorder_Video *video = lt_malloc(sizeof(LTMediaRecorder_Video));
    if (!video) return NULL;
    video->sharedFile = sharedFile;
    video->resolution = s_videoResolution;
    video->duration = duration;
    video->startTimeEpoch = startTimeEpoch;
    video->endTimeEpoch = endTimeEpoch;
    return video;
}

static void NotifyVideoFinished(LTEvent                   hVideoFinishedEvent,
                                LTFile                   *videoFile,
                                LTTime                    duration,
                                LTTime                    startTimeEpoch,
                                LTTime                    endTimeEpoch,
                                LTMediaRecorder_Snapshot *snapshot) {
    /* Wrap the video file in a sharedfile and increment the refcount.
     * The shared file will be released/destroyed in DispatchVideoFinishedEventComplete.
     * The linkfile can be discarded since it will not be used. */
    LTSharedFile *sharedFile = NULL;
    if (videoFile) {
        sharedFile = lt_createobject(LTSharedFile);
        sharedFile->API->Init(sharedFile, videoFile, false);
        LTLinkFile *linkFile = sharedFile->API->Reserve(sharedFile);
        lt_destroyobject(linkFile);
    }

    /* Wrap the rest of the data */
    LTMediaRecorder_Video *video = MallocVideoWrapper(sharedFile, duration, startTimeEpoch, endTimeEpoch);
    if (!video) {
        // Wrapper failed to malloc, release the shared file and do not send out event
        if (sharedFile) sharedFile->API->Release(sharedFile, NULL);
        return;
    }

    /* Send out the event. */
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, hVideoFinishedEvent);
    iEvent->NotifyEvent(hVideoFinishedEvent, video, snapshot);
}

typedef struct TimelapseArgs {
    LTString  filename;
    LTTime    startTimeEpoch;
    LTTime    endTimeEpoch;
    LTTime    interval;
} TimelapseArgs;

static void StartTimelapseProc(void *data) {
    TimelapseArgs *args = data;

    // If there is currently an ongoing timelapse, end it before starting a new one
    if (s_camState.timelapse.file) {
        StopRecordTimelapse(kStopReason_Success);
    }

    s_camState.timelapse.interval = args->interval;
    s_camState.timelapse.lastIFrameTimestamp   = LTTime_Zero();
    s_camState.timelapse.startTimeEpoch = args->startTimeEpoch;
    s_camState.timelapse.endTimeEpoch = args->endTimeEpoch;
    LTTime duration = LTTime_Subtract(args->endTimeEpoch, args->startTimeEpoch);
    s_camState.timelapse.endAtTimestamp = LTTime_Add(LT_GetCore()->GetKernelTime(), duration);

    s_camState.timelapse.file = lt_createobject(LTFile);
    LTFile *file = s_camState.timelapse.file;
    file->API->SetName(file, args->filename);
    if (!file->API->MakeDirectory(file, true)){
       LTLOG_YELLOWALERT("tl.mkdir.err", "unable to make directory for: %s", args->filename);
       return;
    }
    if (file->API->Exists(file)){
        // If the timelapse h264 file already exists for the given start time, continue that file
        if (!file->API->Open(file, false)) {
            LTLOG_YELLOWALERT("open.tl.err", "Unable to open path \"%s\"", args->filename);
            return;
        }
    } else {
        if (!file->API->Open(file, true)) {
            LTLOG_YELLOWALERT("open.tl.create.err", "Unable to open path \"%s\"", args->filename);
            return;
        }
    }

    file->API->SeekToPosition(file, file->API->GetSize(file));
    LTLOG_SERVER("open.tl", "Started timelapse (%llums interval): %s", LT_Ps64(LTTime_GetMilliseconds(s_camState.timelapse.interval)), args->filename);

    // Grab a snapshot to act as the timelapse thumbnail
    // If snapshot was continued, then this will grab a new thumbnail snapshot for the timelapse
    s_iSnapshotSource->Trigger(s_SnapshotSource);
}
static void StartTimelapseReleaseProc(LTThread_ReleaseReason releaseReason, void *data) {
    LT_UNUSED(releaseReason);
    TimelapseArgs *args = data;
    ltstring_destroy(args->filename);
    lt_free(args);
}
static void EndTimelapseProc(void *data){
    LT_UNUSED(data);
    StopRecordTimelapse(kStopReason_Success);
}

static u32 TokenizeH264(u8 *buf, u32 bufsize) {
    LT_ASSERT(bufsize > 4);
    LT_ASSERT(buf[0] == 0);
    LT_ASSERT(buf[1] == 0);
    LT_ASSERT(buf[2] == 0);
    LT_ASSERT(buf[3] == 1);

    for (u32 idx = 4; bufsize-idx >= 4; idx++) {
        if (buf[idx+0] == 0 && buf[idx+1] == 0 && buf[idx+2] == 0 && buf[idx+3] == 1) {
            return idx;
        }
    }

    return bufsize;
}

// Only converts .h264 files that consist ONLY of sps/pps/sei/IFrames
static LTMediaMP4_Stats ConvertH264TimelapseFileToMP4(LTFile *inputFile, LTFile *outputFile) {
    LTMediaMP4_Builder * builder = LT_GetLTMediaMP4()->CreateBuilder((LTMediaMP4_AudioConfig) {}, outputFile);

    u32 inputSize = inputFile->API->GetSize(inputFile);
    u32 readOffset = 0;
    u32 nSize = 2048;
    u8 * buf = lt_malloc(nSize);

    while (readOffset < inputSize) {
        inputFile->API->SeekToPosition(inputFile, readOffset);
        u32 bytesRead = inputFile->API->Read(inputFile, nSize, buf);
        u32 idx = TokenizeH264(buf, bytesRead);

        if (idx == bytesRead && readOffset + bytesRead < inputSize) {
            // Buffer too small to hold NAL frame. Increase buffer size.
            nSize += 2048;
            buf = lt_realloc(buf, nSize);
            LTLOG_DEBUG("convert.realloc", "Increase buffer size to %d", nSize);
            continue;
        }

        // Failed to write NAL unit, abort
        if (!LT_GetLTMediaMP4()->AddNALUnit(builder, buf, idx)) {
            LT_GetLTMediaMP4()->FreeBuilder(builder);
            lt_free(buf);
            return (LTMediaMP4_Stats){};  // Propagate error up with empty stats
        }
        readOffset += idx;
    }

    LT_ASSERT(readOffset == inputSize);

    lt_free(buf);

    return LT_GetLTMediaMP4()->Finish(builder);
}

static LTMediaMP4_Stats ConvertTimelapse(LTFile *fileToConvert) {
    LTMediaMP4_Stats videoStats = (LTMediaMP4_Stats){};

    /* Name the .mp4 file destination */
    LTString convertedFilename = ltstring_create("");
    fileToConvert->API->GetName(fileToConvert, &convertedFilename);
    /* Replace .h264 extension with .mp4 */
    lt_strncpyTerm(convertedFilename + lt_strlen(convertedFilename) - 4, "mp4", 4);

    /* Convert .h264 file to a .mp4 file */
    LTFile *outputFile = lt_createobject(LTFile);
    outputFile->API->SetName(outputFile, convertedFilename);
    do {
        // Create the mp4 file
        if (!outputFile->API->Open(outputFile, true)) {
            LTLOG_YELLOWALERT("convert.output.err",
                              "Unable to open path \"%s\"",
                              convertedFilename);
            break;
        }
        // Output converted the h264 file to defined location
        videoStats = ConvertH264TimelapseFileToMP4(fileToConvert, outputFile);  // Will Close() outputFile
        if (videoStats.nVideoSamples == 0) break;
        // Delete old file and set to converted file
        fileToConvert->API->Delete(fileToConvert, false);
        fileToConvert->API->SetName(fileToConvert, convertedFilename);
    } while (false);
    lt_destroyobject(outputFile);

    ltstring_destroy(convertedFilename);
    return videoStats;
}

static void StopRecordTimelapse(StopReason reason) {
    if (!s_camState.timelapse.file) return;

    /* Convert the h264 timelapse file to an mp4 */
    LTLOG_DEBUG("tl.mp4.begin", "Timelapse done. Converting to MP4.");
    // If stop was due to failure, skip converting
    LTMediaMP4_Stats timelapseVideoStats = (reason == kStopReason_Failure)
                                               ? (LTMediaMP4_Stats){}
                                               : ConvertTimelapse(s_camState.timelapse.file);
    if (timelapseVideoStats.nVideoSamples == 0) {
        LTLOG_YELLOWALERT("tl.mp4.err", "Conversion failed");
        // Delete and destroy the .h264 or mp4 file that failed to convert.
        s_camState.timelapse.file->API->Delete(s_camState.timelapse.file, false);
        lt_destroyobject(s_camState.timelapse.file);
        s_camState.timelapse.file = NULL;
    } else {
        LTLOG_DEBUG("tl.mp4.end", "Finished Converting timelapse to mp4.");
    }

    /* If s_camstate.timelapse.file is NULL, video->SharedFile=NULL will be sent out to clients. */
    NotifyVideoFinished(s_camState.timelapse.hEvent,
                        s_camState.timelapse.file,
                        LTTime_Milliseconds(timelapseVideoStats.nDurationMillis),
                        s_camState.timelapse.startTimeEpoch,
                        s_camState.timelapse.endTimeEpoch,
                        s_camState.timelapse.snapshot);

    // Notify event will pass the malloc'd snapshot pointer out,
    // so the static reference to it can now be set to NULL.
    // The snapshot pointer will be freed in DispatchVideoFinishedEventComplete.
    s_camState.timelapse.snapshot = NULL;

    /* Cleanup, regardless of success or error.
     * File has deleted or wrapped and sent out to clients, so OK to destroy */
    lt_destroyobject(s_camState.timelapse.file);
    s_camState.timelapse.file = NULL;
}

static void ContinuousRecordingNextMinute(void);

static void OnContinuousRecordingMetadata(LTMediaMP4Recording *recording, LTMediaMP4Recording_MetadataType dataType, LTMediaMP4Recording_Metadata *data, void *clientData) {
    LT_UNUSED(recording);

    if (dataType == kMetadataType_Video) {
        // Always track the latest video frame timestamp so nextMinute can be accurately calculated
        s_camState.record.lastFrameTimestamp = data->nTimestamp;
        // End and start next recording on the provided minute boundary
        u64 nextMinute = *(u64 *)clientData;
        if (data->nTimestamp > nextMinute) {
            ContinuousRecordingNextMinute();
        }
    } else if (dataType == kMetadataType_WriteErrorStop) {
        LTLOG("contrec.abort", "Failed to write continuous recording, abort");
        if (s_camState.record.recording) s_camState.record.recording->API->Cancel(s_camState.record.recording);
        lt_destroyobject(s_camState.record.recording);
        s_camState.record.recording = NULL;
        lt_destroyobject(s_camState.record.file);
        s_camState.record.file = NULL;
    }
}

static void ContinuousRecordingNextMinute(void) {
    // Finish the previous minute chunk, if it exists
    if (s_camState.record.recording) {
        // will close() previous open file
        LTMediaMP4_Stats videoStats = s_camState.record.recording->API->Finish(s_camState.record.recording);
        lt_destroyobject(s_camState.record.recording);
        s_camState.record.recording = NULL;

        // Notify clients continuous recording minute has finished. Continuous recording has no snapshot.
        NotifyVideoFinished(s_camState.record.hEvent,
                            s_camState.record.file,
                            LTTime_Milliseconds(videoStats.nDurationMillis),
                            s_camState.record.startTimeEpoch,
                            LT_GetLTSystemTimeZone()
                                ->ClockTimeLocalToUTC(LT_GetLTSystemTimeZone()->GetClockTimeLocal(NULL), NULL),
                            NULL);
    }

    // localSaveDir will be the base dir for the recording file
    LTFile *saveDir = s_camState.record.localSaveDir;
    LTString baseDirString = ltstring_create("");
    saveDir->API->GetBaseDirectory(saveDir, &baseDirString);

    LTTime localTimestamp = LT_GetLTSystemTimeZone()->GetClockTimeLocal(NULL);
    // If time is UTC start time, then timezone time zone and time have not yet been set
    if (localTimestamp.nNanoseconds <= 0) return;
    LTCalendarTime time;
    LT_GetLTSystemTimeZone()->ClockTimeToCalendarTime(localTimestamp, &time);

    // Save start time
    s_camState.record.startTimeEpoch = LT_GetLTSystemTimeZone()->ClockTimeLocalToUTC(localTimestamp, NULL);

    // wyze format: /record/<year><month><day>/<hour0-23>/<minute0_59>.mp4
    LTString filename = NULL;
    ltstring_format(&filename,
        "/%s/record/" "%04u%02u%02u" "/" "%02u" "/" "%02u" ".mp4",
        baseDirString,
        time.nYear, time.nMonth, time.nDay,
        time.nHour, time.nMinute
    );
    ltstring_destroy(baseDirString);

    // If last frame timestamp is not set, set it to the current kernel time. This assumes video frame timestamps are accurate relative to kernel time.
    if (s_camState.record.lastFrameTimestamp == 0)
        s_camState.record.lastFrameTimestamp = LTTime_GetMicroseconds(LT_GetCore()->GetKernelTime());
    // Start 1 second past to ensure correct behavior on leap seconds
    s_camState.record.nextMinute = s_camState.record.lastFrameTimestamp + (61 - time.nSecond) * LTTime_GetMicroseconds(LTTime_Seconds(1));

    // Reuse the file object
    LTFile *file = s_camState.record.file;
    file->API->SetName(file, filename); // Will copy the string
    if (!file->API->MakeDirectory(file, true)) {
        LTLOG_YELLOWALERT("rec.mkdir.err", "Unable to make directory for path \"%s\"", filename);
        return;
    }
    if (!file->API->Open(file, true)) {
        LTLOG_YELLOWALERT("rec.open.err", "Unable to open path \"%s\"", filename);
        return;
    }

    LTLOG("rec", "Recording continuous to %s\n", filename);
    ltstring_destroy(filename);

    LTMediaMP4Recording_Config config = {.video.enabled = true, .video.preroll = true, .video.events = true, .audio.enabled = s_camState.audioEnabled, .stopOnWriteError = true};
    s_camState.record.recording = LT_GetLTMediaMP4Recorder()->CreateRecording(s_camState.record.file, config);
    s_camState.record.recording->API->OnMetadataEvent(s_camState.record.recording, OnContinuousRecordingMetadata, NULL, &s_camState.record.nextMinute);
    s_camState.record.recording->API->Start(s_camState.record.recording);
}

static void StartContinuousRecordingProc(void *data) {
    LTLOG_DEBUG("rec.start", "begin continuous recording");
    s_camState.record.localSaveDir = data;
    s_camState.record.file = lt_createobject(LTFile);
    s_camState.record.lastFrameTimestamp = 0;
    ContinuousRecordingNextMinute();
}

static void StopContinuousRecordingProc(void *data) {
    LT_UNUSED(data);
    LTLOG_DEBUG("rec.stop", "stop continuous recording");
    if (s_camState.record.recording) s_camState.record.recording->API->Cancel(s_camState.record.recording);
    lt_destroyobject(s_camState.record.recording);
    s_camState.record.recording = NULL;
    s_camState.record.lastFrameTimestamp = 0;

    // Destroy the video file
    if (s_camState.record.file) lt_destroyobject(s_camState.record.file);
    s_camState.record.file = NULL;
    s_camState.record.localSaveDir = NULL;
    lt_destroyobject(s_camState.record.recording);
    s_camState.record.recording = NULL;
}

static void SetRecordingAudioProc(void *data) {
    LTLOG_DEBUG("aud.set", "Record audio: %s", (bool)(data) == 0 ? "false" : "true");
    s_camState.audioEnabled = (bool)(data);
}

// TODO: Time lapse only records I-frames. We're recording a new frame after AT LEAST N seconds.
// Should we compensate for this?
// E.g. I-frame every 2 seconds + 3 second timelapse => 4 second timelapse in practice. We could do 2/4 instead.
// Or should the UI only allow user to set multiple of min frame interval (e.g. 2s, 4s, etc.)
static void BuildTimelapse(LTMediaData *data) {
    if (!s_camState.timelapse.file) return;

    u8 nalType = data->pData[4] & 0x1F;
    if (nalType != kLTMediaNalTypeIFrame && nalType != kLTMediaNalTypeSps && nalType != kLTMediaNalTypePps) return;

    LTFile *file = s_camState.timelapse.file;
    // The first frame written to the file MUST be an sps nal
    if (file->API->GetSize(file) == 0) {
        if (nalType != kLTMediaNalTypeSps) return;
    }

    // If Iframe timestamp > timelapse end, end the timelapse
    if (LTTime_IsGreaterThanOrEqual(LTTime_Microseconds(data->nTimestamp), s_camState.timelapse.endAtTimestamp)) {
        LTLOG("tl.end", "Timelapse ended");
        StopRecordTimelapse(kStopReason_Success);
        return;
    }

    // Check if enough time has passed between sps/pps/Iframe trio
    LTTime elapsedTime = LTTime_Subtract(LTTime_Microseconds(data->nTimestamp), s_camState.timelapse.lastIFrameTimestamp);
    if (LTTime_IsLessThan(elapsedTime, s_camState.timelapse.interval)) return;
    // Only update the time on every IFrame
    if (nalType == kLTMediaNalTypeIFrame) s_camState.timelapse.lastIFrameTimestamp = LTTime_Microseconds(data->nTimestamp);

    // Write data into .h264 file. It will be converted into an mp4 at the end by ConvertH264TimelapseFileToMp4().
    // Enough time has passed, write the Iframe

    /* Write the sps/pps/IFrame */
    u32 ret = file->API->Write(file, data->nDataLen, data->pData);
    if (ret != data->nDataLen) {
        LTLOG("tl.iframe.err", "Timelapse write error");
        StopRecordTimelapse(kStopReason_Failure);
    }

    // LTLOG("tl.iframe", "wrote an sps/pps/IFrame into timelapse h264 (nalType: %u)", nalType);
}

static void VideoCallback(LTMediaEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Error) {
        LTLOG_YELLOWALERT("vid.err", "VideoCallback: Error event");
        return;
    }

    LTMediaData *data = eventData;
    if (data->nDataLen < 5) {
        LTLOG_YELLOWALERT("nal.invl", "Invalid NAL unit of length %lu", LT_Pu32(data->nDataLen));
        return;
    }

    BuildTimelapse(data);
}

static LTMediaRecorder_Snapshot *MallocSnapshot(LTMediaData *data) {
    LTMediaRecorder_Snapshot *snapshot = lt_malloc(sizeof(LTMediaRecorder_Snapshot));
    if (!snapshot) return NULL;
    lt_memset(snapshot, 0, sizeof(LTMediaRecorder_Snapshot));
    snapshot->resolution = s_snapshotResolution;
    snapshot->bNightVisionActive = data->meta.video.bNightVisionActive;
    snapshot->nTimestamp = data->nTimestamp;
    snapshot->nImageLen = data->nDataLen;
    snapshot->pImageData = lt_memdup(data->pData, data->nDataLen);
    snapshot->pMotionROIs = NULL;
    if (!snapshot->pImageData) {
        lt_free(snapshot);
        return NULL;
    }
    return snapshot;
}

// Snapshots can be triggered by audio events, timelapse thumbnail, or short clip thumbnail
static void SnapshotCallback(LTMediaEvent event, void *eventData, void * pClientData) {
    LT_UNUSED(pClientData);
    if (event == kLTMediaEvent_Error) {
        LTLOG_YELLOWALERT("snap.err", "SnapshotCallback: Error event");
        return;
    }

    // If timelapse or shortclip are ongoing and don't currently have a snapshot, save this snapshot
    if (s_camState.timelapse.file && !s_camState.timelapse.snapshot) {
        // Freed when timelapse completes (DispatchVideoFinishedEventComplete)
        LTMediaRecorder_Snapshot *snapshot = MallocSnapshot(eventData);
        s_camState.timelapse.snapshot = snapshot;
    }
}

/*__________________________________
  VideoFinished event dispatchers */
static const LTArgsDescriptor s_VideoFinishedProcArgs  = { 2, {kLTArgType_pointer, kLTArgType_pointer} };
static void DispatchVideoFinishedEvent(LTEvent hEvent, void *proc, LTArgs *eventArgs, void *data) {
    LT_UNUSED(hEvent);
    (*(LTMediaRecorder_VideoFinishedProc *)proc)(
        (LTMediaRecorder_Video *)LTArgs_pointerAt(0, eventArgs),
        (LTMediaRecorder_Snapshot *)LTArgs_pointerAt(1, eventArgs),
        data
    );
}
static void DispatchVideoFinishedEventComplete(LTEvent hEvent, LTArgs *eventArgs) {
    LT_UNUSED(hEvent);
    LTMediaRecorder_Video *video = LTArgs_pointerAt(0, eventArgs);

    // Free the SharedFile that was reserved before event was sent out
    if (video) {
        if (video->sharedFile) {
            // No linkFile since clip/timelapse/continuous all discard their linkFiles
            video->sharedFile->API->Release(video->sharedFile, NULL);
        }
        // Always free the video wrapper
        lt_free(video);
    }

    // Attempt to free thumbnail snapshot if it exists
    // NOTE: There are no motionROIs in the thumbnail snapshot to free
    LTMediaRecorder_Snapshot *snapshot = LTArgs_pointerAt(1, eventArgs);
    if (snapshot) {
        lt_free(snapshot->pImageData);
        lt_free(snapshot);
    }
}

/*_____________________________
  Library API implementation */
static void LTMediaRecorder_StartContinuousRecording(LTFile *dir) {
    s_iThread->QueueTaskProcIfRequired(s_hCameraThread, StartContinuousRecordingProc, NULL, dir);
}
static void LTMediaRecorder_StopContinuousRecording(void) {
    s_iThread->QueueTaskProcIfRequired(s_hCameraThread, StopContinuousRecordingProc, NULL, NULL);
}
static void LTMediaRecorder_OnContinuousRecordingChunkFinished(LTMediaRecorder_VideoFinishedProc *eventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData) {
    ILTEvent *event = lt_gethandleinterface(ILTEvent, s_camState.record.hEvent);
    event->RegisterForEvent(s_camState.record.hEvent, eventProc, clientDataReleaseProc, clientData, false);
}
static void LTMediaRecorder_NoContinuousRecordingChunkFinished(LTMediaRecorder_VideoFinishedProc *eventProc) {
    ILTEvent *event = lt_gethandleinterface(ILTEvent, s_camState.record.hEvent);
    event->UnregisterFromEvent(s_camState.record.hEvent, eventProc);
}

static void LTMediaRecorder_RecordTimelapse(const char *dirname, LTTime startTimeEpoch, LTTime endTimeEpoch, LTTime interval) {
    LTString filename = NULL;
    ltstring_format(&filename, "/%s/time_lapse/time_Task_%lld/record.h264",
        dirname,
        LT_Ps64(LTTime_GetMilliseconds(startTimeEpoch))
    );
    TimelapseArgs *args = lt_malloc(sizeof(TimelapseArgs));
    if (!args) {
        LTLOG_YELLOWALERT("tl.start.OOM", "could not start timelapse");
        return;
    }
    args->filename = filename;
    args->startTimeEpoch = startTimeEpoch;
    args->endTimeEpoch = endTimeEpoch;
    args->interval = interval;
    s_iThread->QueueTaskProc(s_hCameraThread, StartTimelapseProc, StartTimelapseReleaseProc, args);
}
static void LTMediaRecorder_EndTimelapse(void){
    s_iThread->QueueTaskProcIfRequired(s_hCameraThread, EndTimelapseProc, NULL, NULL);
}
static void LTMediaRecorder_OnTimelapseFinished(LTMediaRecorder_VideoFinishedProc *eventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData) {
    ILTEvent *event = lt_gethandleinterface(ILTEvent, s_camState.timelapse.hEvent);
    event->RegisterForEvent(s_camState.timelapse.hEvent, eventProc, clientDataReleaseProc, clientData, false);
}
static void LTMediaRecorder_NoTimelapseFinished(LTMediaRecorder_VideoFinishedProc *eventProc) {
    ILTEvent *event = lt_gethandleinterface(ILTEvent, s_camState.timelapse.hEvent);
    event->UnregisterFromEvent(s_camState.timelapse.hEvent, eventProc);
}

static void LTMediaRecorder_EnableRecordingAudio(void) {
    s_iThread->QueueTaskProcIfRequired(s_hCameraThread, SetRecordingAudioProc, NULL, (void*)true);
}
static void LTMediaRecorder_DisableRecordingAudio(void) {
    s_iThread->QueueTaskProcIfRequired(s_hCameraThread, SetRecordingAudioProc, NULL, (void*)false);
}
/*_________________________________
  Library Start/Stop API */
static bool CameraThreadInit(void) {
    LTLOG("init", "Camera thread init");

    LT_GetLTMediaMP4Recorder()->Start(true);

    // Start and register for the shared video source
    s_VideoSource = LT_GetLTDeviceMedia()->OpenSource(&(LTMediaFormat) {
        .nKind = kLTMediaKind_Video_HD,
        .nEncoding = kLTMediaEncoding_H264,
        .params.h264.nProfile = 77, // Main profile
    });
    if (!s_VideoSource) {
        LTLOG_YELLOWALERT("init.vsrc", "Unable to create video source\n");
        return false;
    }
    s_iVideoSource = lt_gethandleinterface(ILTMediaSource, s_VideoSource);
    s_iVideoSource->OnMediaEvent(s_VideoSource, VideoCallback, NULL);
    s_iVideoSource->Start(s_VideoSource);
    s64 value = 0;
    s_Settings->GetIntegerValue(settingsKeyGopLength, &value);
    s_iVideoSource->SetGopLength(
            s_VideoSource,
            value > 0 ? (u32)value : kDefaultGopLength);
    LT_GetLTDeviceMedia()->GetProperty("resolution.hd", &s_videoResolution);

    // Create and register for the snapshot source
    s_SnapshotSource = LT_GetLTDeviceMedia()->OpenSource(&(LTMediaFormat){
        .nKind = kLTMediaKind_Image_SD,
        .nEncoding = kLTMediaEncoding_Jpeg
    });
    if (!s_SnapshotSource) {
        LTLOG_YELLOWALERT("init.asrc", "Unable to create snapshot source\n");
        return false;
    }
    s_iSnapshotSource = lt_gethandleinterface(ILTMediaSource, s_SnapshotSource);
    s_iSnapshotSource->OnMediaEvent(s_SnapshotSource, SnapshotCallback, NULL);
    LT_GetLTDeviceMedia()->GetProperty("resolution.sd", &s_snapshotResolution);

    return true;
}

static void CameraThreadExit(void) {
    LTLOG("exit", "Camera thread exit");

    LT_GetLTMediaMP4Recorder()->Stop();

    if (s_VideoSource) {
        s_iVideoSource->Stop(s_VideoSource);
        lt_destroyhandle(s_VideoSource);
        s_VideoSource = 0;
        s_iVideoSource = NULL;
    }

    if (s_SnapshotSource) {
        s_iSnapshotSource->Stop(s_SnapshotSource);
        lt_destroyhandle(s_SnapshotSource);
        s_SnapshotSource = 0;
        s_iSnapshotSource = NULL;
    }
}

static void LTMediaRecorder_Stop(void) {
    s_StartStopMutex->API->Lock(s_StartStopMutex);
    if (s_hCameraThread) {
        lt_destroyhandle(s_hCameraThread);
        s_hCameraThread = 0;
    }
    s_StartStopMutex->API->Unlock(s_StartStopMutex);
}

static bool LTMediaRecorder_Start(void) {
    s_StartStopMutex->API->Lock(s_StartStopMutex);

    // If camera thread already exists, return
    if (s_hCameraThread) {
        s_StartStopMutex->API->Unlock(s_StartStopMutex);
        return true;
    }

    // Else, create the new thread
    s_hCameraThread = LT_GetCore()->CreateThread("LTMediaRecorder");
    if (!s_hCameraThread) {
        s_StartStopMutex->API->Unlock(s_StartStopMutex);
        return false;
    }
    s_StartStopMutex->API->Unlock(s_StartStopMutex);

    // Start the thread
    s_iThread->Start(s_hCameraThread, &CameraThreadInit, &CameraThreadExit);
    s_iThread->SetStackSize(s_hCameraThread, 2048); // TODO: can this stack be reduced?

    return true;
}

/*_____________________________________
  Library initialization and cleanup */
static void LTMediaRecorderImpl_LibFini(void) {
    // Camera thread destruction will close video and audio sources
    LTMediaRecorder_Stop();
    s_iThread = NULL;

    lt_closelibrary(s_Settings);
    lt_destroyobject(s_StartStopMutex);
    s_StartStopMutex = NULL;

    lt_destroyhandle(s_camState.record.hEvent);
    lt_destroyhandle(s_camState.timelapse.hEvent);

    s_camState = (CameraState){};
}

static bool LTMediaRecorderImpl_LibInit(void) {
    do {
        s_camState.timelapse.hEvent = LT_GetCore()->CreateEvent(&s_VideoFinishedProcArgs, DispatchVideoFinishedEvent, DispatchVideoFinishedEventComplete, NULL, NULL);
        if (!s_camState.timelapse.hEvent) break;
        s_camState.record.hEvent = LT_GetCore()->CreateEvent(&s_VideoFinishedProcArgs, DispatchVideoFinishedEvent, DispatchVideoFinishedEventComplete, NULL, NULL);
        if (!s_camState.record.hEvent) break;

        s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        if (!s_iThread) break;

        s_StartStopMutex = lt_createobject(LTMutex);
        if (!s_StartStopMutex) break;

        s_Settings = lt_openlibrary(LTSystemSettings);
        if (!s_Settings) break;

        return true;
    } while (false);

    LTMediaRecorderImpl_LibFini();
    return false;
}

/*_____________________________________
  Library interface bindings */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaRecorder)
    .StartContinuousRecording           = LTMediaRecorder_StartContinuousRecording,
    .StopContinuousRecording            = LTMediaRecorder_StopContinuousRecording,
    .OnContinuousRecordingChunkFinished = LTMediaRecorder_OnContinuousRecordingChunkFinished,
    .NoContinuousRecordingChunkFinished = LTMediaRecorder_NoContinuousRecordingChunkFinished,
    .RecordTimelapse                    = LTMediaRecorder_RecordTimelapse,
    .OnTimelapseFinished                = LTMediaRecorder_OnTimelapseFinished,
    .NoTimelapseFinished                = LTMediaRecorder_NoTimelapseFinished,
    .EndTimelapse                       = LTMediaRecorder_EndTimelapse,
    .EnableRecordingAudio               = LTMediaRecorder_EnableRecordingAudio,
    .DisableRecordingAudio              = LTMediaRecorder_DisableRecordingAudio,
    .Start                              = LTMediaRecorder_Start,
    .Stop                               = LTMediaRecorder_Stop,
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTMediaRecorder,
    (LTSharedFileImpl)
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Sep-23   otho        created IndoorCameraSDCard
 *  16-Nov-23   aurelian    renamed/refactored to LTMediaRecorder, added event saving
 *  13-Jun-24   aurelian    moved lower-level recording logic to LTMediaMP4Recorder
 *  10-Jul-24   aurelian    moved event and clip recording logic to MediaRecordingController
*/
