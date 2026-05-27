/*******************************************************************************
 * <lt/media/mp4/recorder/LTMediaMP4Recorder.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMP4RECORDER_H
#define LT_INCLUDE_LT_MEDIA_LTMEDIAMP4RECORDER_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/utility/buffer/LTBuffer.h>
#include <lt/system/fs/LTFile.h>
#include <lt/media/mp4/LTMediaMP4.h>
#include <lt/device/media/LTDeviceMedia.h>

LT_EXTERN_C_BEGIN

typedef struct {
    struct {
        bool enabled;  // True to enable audio track in the mp4.
        bool events;   // True to enable metadata events on every audio sample written.
    } audio;
    struct {
        bool enabled;  // True to enable video track in the mp4.
        bool preroll;  // True to enable the use of preroll for the video. Preroll must also be enabled by the library.
        bool events;   // True to enable metadata events on every video sample written.
    } video;
    // Stop writing to the mp4 file upon encountering a write error. A metadata event will be sent out.
    bool stopOnWriteError;

    // Stop writing to the mp4 file after reaching a certain size, in bytes. A metadata event will be sent out. This
    // does not account for the size of the [moov] container appended to the mp4 when finalizing the file.
    LT_SIZE maxSize;

    // Stop writing to the mp4 file if available system RAM is measured to go below this amount. Note that available
    // system RAM is polled and measured at a fixed interval rather than on every data write into the mp4, which will
    // create a degree of inprecision around the configured value
    LT_SIZE minAvailableSystemMemory;
} LTMediaMP4Recording_Config;

typedef enum {
    kMetadataType_Audio,
    kMetadataType_Video,

    // These events stop writing to the mp4 file
    kMetadataType_WriteErrorStop,
    kMetadataType_MaxSize,
    kMetadataType_MinAvailableSystemMemory,
} LTMediaMP4Recording_MetadataType;

typedef struct {
    u64 nTimestamp;  // This field is only valid for audio and video metadata events
    u32 nAudioSamples;
    u32 nVideoSamples;
    u32 nFileSize;
} LTMediaMP4Recording_Metadata;

typedef struct LTMediaMP4Recording LTMediaMP4Recording;

typedef void (LTMediaMP4Recording_OnMetadataEventProc)(LTMediaMP4Recording *recording, LTMediaMP4Recording_MetadataType dataType, LTMediaMP4Recording_Metadata *data, void *clientData);
/**< Event callback type for LTMediaMP4Recording->OnMetadataEvent()
 *
 * @param recording mp4 recording the event is originating from
 * @param dataType the type of most recently added to the mp4, either audio or video
 * @param data the metadata associated with the most recent audio or video data added to the mp4
 * @param clientData client data provided to the event callback during event registration
 * @see OnMetadataEvent()
 */

typedef_LTLIBRARY_ROOT_INTERFACE(LTMediaMP4Recorder, 1){
    /* === Application level API === */

    LTMediaMP4Recording *(*CreateRecording)(LTFile *file, LTMediaMP4Recording_Config config);
    /**< Create and initialize an MP4 recording object with specified config and backing file.
     * This function will fail if the library's daemon thread has not been started yet.
     * @see Start()
     *
     * @param file LTFile to write the mp4 into
     * @param config configuration for the mp4 recording, such as enabling/disabling audio or whether or not to use video preroll
     * @return LTMediaMP4Recording MP4 recording object or NULL if there was an error initializing the recording
     */

    /* === System level API === */

    bool (*Start)(bool enablePreroll);
    /**< System-level API. Start the daemon thread that manages the recording and video writing logic of this library
     *
     * @param enablePreroll true to enable the preroll buffer--a ring buffer of audio and video data that is used prepend data to videos to allow videos to "start" from the past
     * @return true if the daemon thread has been started already or was successfully started
     * @note The application-level API above will NOT work unless the daemon thread of the library is running.
     * @note Mutex-protected.
    */

    void (*Stop)(void);
    /**< System-level API. Stop the daemon thread.
     *
     * This will cancel all ongoing recordings. However, it is up to the owners of the recordings to free/destroy the
     * recordings.
     *
     * @note Mutex-protected.
     */
} LTLIBRARY_INTERFACE;

typedef_LTObject(LTMediaMP4Recording, 1) {

    bool (*Init)(LTMediaMP4Recording *recording, LTFile *file, LTMediaMP4Recording_Config config);
    /**< Initialize the mp4 recording object. Clients should call CreateRecording() instead of directly using this function.
     * @see CreateRecording()
     *
     * @param recording The recording to initialize
     * @param file The LTFile the mp4 should be written into
     * @param config Configuration options for the mp4 file
     */

    void (*Start)(LTMediaMP4Recording *recording);
    /**< Begin receiving and saving video and audio data into an mp4 recording, depending on initial configuration.
     * Internally, this will attach the given mp4 recording to the audio/video pipeline controlled by the library's
     * daemon thread.
     *
     * @note The recording will start asynchronously. This means data will not immediately be written to the recording
     * after calling LTMediaMP4Recording->Start().
     *
     * @param recording The recording to start
     */

    void (*OnMetadataEvent)(LTMediaMP4Recording *recording, LTMediaMP4Recording_OnMetadataEventProc *eventProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData);
    /**< Register to receive metadata of the mp4 based on the configuration applied by CreateRecording(). Clients must
     * opt into the events they wish to receive via LTMediaMP4Recording_Config. This metadata includes values such as
     * the timestamp of the latest video frame or audio sample added to the mp4, or the current size of the mp4 file.
     * Events are only sent for active/ongoing recordings.
     *
     * @param recording The recording to receive metadata events from
     * @param eventProc Event callback to be called when event is dispatched
     * @param clientDataReleaseProc Release proc for client supplied data
     * @param clientData Client supplied data to be passed alongside the event callback when event is dispatched
     * @see NoMetadataEvent
     */

    void (*NoMetadataEvent)(LTMediaMP4Recording *recording, LTMediaMP4Recording_OnMetadataEventProc *eventProc);
    /**< Unregister from metadata events on a recording
     *
     * @param recording The recording to stop receiving metadata events from
     * @param eventProc Event callback to unregister
     * @see OnMetadataEvent
     */

    void (*Cancel)(LTMediaMP4Recording *recording);
    /**< Immediately end mp4 recording without finalizing. The provided LTFile will be left as-is with whatever data was
     * written to the file it in before calling Cancel(). Internally, this will remove the given mp4 recording from the
     * audio/video pipeline controlled by the library's daemon thread.
     *
     * @note The recording will be ended synchronously.
     * @param recording The recording to cancel
     */

    // LTMediaMP4_Stats (*GetStats)(LTMediaMP4Recording *recording);
    /*< Get the current stats of the mp4 recording. TODO: should be synchronous */

    LTMediaMP4_Stats (*Finish)(LTMediaMP4Recording *recording);
    /*< Immediately end mp4 recording and finalize mp4 format. The result will be a valid mp4 stored at the provided
     * LTFile. Internally, this will remove the given mp4 recording from the audio/video pipeline controlled by the
     * library's daemon thread.
     *
     * @note The recording will be ended synchronously.
     * @param recording The recording to end and finalize
     */

    LTFile *(*GetFile)(LTMediaMP4Recording *recording);
    /*< Retrieve the backing file the recording object was created with.
     *
     * If the recording is backed by an LTBuffer, this function will return NULL
     * and instead GetBuffer() should be used to retrieve the LTBuffer.
     *
     * @note It is not recommended to modify the file while the recording is in progress, as the MP4 recorder thread
     * will be actively writing into the file and moving the read/write pointer of the file.
     *
     * @param recording An initialized recording to retrieve the file from
     * @returns LTFile the backing file that was originally provided in Init()
     */

     LTBuffer *(*GetBuffer)(LTMediaMP4Recording *recording);
     /*< Retrieve an LTBuffer that can be used for sequential/streaming access
      *  to the backing file used for the recording.
      *
      * Not all specialisations of LTMediaMP4Recording support buffer access,
      * and will return NULL if there is no available buffer.
      *
      * @note As with GetFile() this function should not be called while the recording is in progress.
      *
      * @param recording An initialized recording to retrieve the file from
      * @returns LTBuffer for streaming access to the recording
      */
 } LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMP4RECORDER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-May-24   aurelian    created
 */