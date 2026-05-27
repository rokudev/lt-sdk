/*******************************************************************************
 * <lt/media/recorder/LTMediaRecorder.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#ifndef LT_INCLUDE_LT_MEDIA_LTMEDIARECORDER_H
#define LT_INCLUDE_LT_MEDIA_LTMEDIARECORDER_H

#include <lt/system/fs/LTFile.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/media/motiondetection/LTMediaMotionDetection.h>

LT_EXTERN_C_BEGIN

/* LTMediaDetectionObject enum order should be aligned
   with the order of the "saveDetectedObject" setting !! */
typedef enum {
    kLTMediaDetectionObject_None = 0,
    kLTMediaDetectionObject_Motion = 1,
    kLTMediaDetectionObject_Sound,
    kLTMediaDetectionObject_Person,
    kLTMediaDetectionObject_Pet,
    kLTMediaDetectionObject_Package,
    kLTMediaDetectionObject_Vehicle,
    kLTMediaDetectionObject_FireAlarm,
    kLTMediaDetectionObject_COAlarm,
} LTMediaDetectionObject;

typedef struct LTMediaRecorder_Snapshot {
    LTMediaKind nKind;
    LTMediaEncoding nEncoding;
    u64 nTimestamp;
    u32 nImageLen;
    LTMediaResolution resolution;
    u8  *pImageData;
    // Motion-specific values
    bool bNightVisionActive;
    LTMediaMotionDetection_Regions *pMotionROIs;
} LTMediaRecorder_Snapshot;

typedef struct LTSharedFile LTSharedFile; // forward declaration of LTSharedFile object
typedef struct LTMediaRecorder_Video {
    LTSharedFile *sharedFile;
    LTMediaResolution resolution;
    LTTime duration;
    LTTime startTimeEpoch;
    LTTime endTimeEpoch;
} LTMediaRecorder_Video;


typedef void (LTMediaRecorder_VideoFinishedProc)(LTMediaRecorder_Video *video, LTMediaRecorder_Snapshot *snapshot, void *clientData);
/**< callback type for timelapse, continuous recording, video clip events
 *
 * @param video the recorded video that contains a shared file reference to the video, alongside some video statistics.
 * It is NOT possible for video to be NULL. However, video->sharedFile may be NULL, indicating video was unable to be
 * finished.
 * @param snapshot snapshot of a frame of the video, if available. Clients can use this image as the thumbnail of the
 * video. It is possible for snapshot to be NULL.
 * @param clientData
 */

/*______________________
  LTSharedFile Object */
typedef LTFile LTLinkFile;
    /**< Alias typedef for LTFile for use with LTSharedFile.
     *
     * A "LinkFile" is an LTFile that "links" to the same file location as another LTFile.
     * It can be thought of as having multiple unix file descriptors that refer to the same physical underlying file.
     */
typedef_LTObject(LTSharedFile , 1) {
    void            (*Init)(LTSharedFile *sharedFile, LTFile *fileToShare, bool deleteFileOnRelease);
    /**< Initialize the sharedFile object with an LTFile
     *
     * @param sharedFile object created by lt_createobject(LTSharedFile)
     * @param fileToShare LTFile to wrap with LTSharedFile
     * @param deleteFileOnRelease whether or not to call LTFile->API->Delete() and lt_destroyobject() on
     *                            the underlying file (i.e. fileToShare) when the final Release() has been called.
     */
    LTLinkFile  *(*Reserve)(LTSharedFile *sharedFile);
    /**< Reserve the shared video file in order to use it.
     *
     * @param sharedFile struct that acts as an LTFile with a refcount. Clients receive SharedFiles from OnDetectionMediaEvent.
     * @return LTLinkFile that "links" to the shared video file. Pass this as "linkFile" to Release() to free it.
     *         Clients should LTFile->API->Open() this file in order to access the underlying file.
     * @note MUST call Release() later!
     * @see Release()
    */
    void            (*Release)(LTSharedFile *sharedFile, LTLinkFile *linkFile);
    /**< Release the shared video file.
     *
     * The last paired call to Release() will DESTROY the CURRENT LTSharedFile object!
     * ex.
     * Reserve()
     * Reserve()
     * ...
     * Release()
     * Release() // will call lt_destroyobject() on itself!!
     *
     * @param sharedFile struct that acts as an LTFile with a refcount.
     * @param linkFile an LTFile that "links" to the shared video file. This should be the same LTFile received from ReserveEventVideoFile!
     * @note MUST be paired with a previous Reserve() call!
     * @see Reserve()
    */
} LTOBJECT_API;

/*_____________________
  LTMediaRecorder API*/
typedef_LTLIBRARY_ROOT_INTERFACE(LTMediaRecorder, 1){
    /* === Application level API === */

    void (*StartContinuousRecording)(LTFile *dir);
    /**< Start continuous recording. Queues a taskproc to this library's thread.
     *
     * Writes mp4s to <dirname>/record/<year><month><day>/<hour0-23>/<minute0-59>.mp4 in one-minute chunks.
     *
     * @param dir LTFile with a defined base directory name. The base directory string is then copied out and used by this library.
     */
    void (*StopContinuousRecording)(void);
    /**< Stop continuous recording. Queues a taskproc to this library's thread.
     *
     * Ends the current recording chunk.
     */
    void (*OnContinuousRecordingChunkFinished)(LTMediaRecorder_VideoFinishedProc *videoProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData);
    /**< Register to receive an event when a continuous recording video "chunk" is finished (1-minute video). */
    void (*NoContinuousRecordingChunkFinished)(LTMediaRecorder_VideoFinishedProc *videoProc);
    /**< Unregister from continuous recording events */

    void (*RecordTimelapse)(const char *dirname, LTTime startTimeEpoch, LTTime endTimeEpoch, LTTime interval);
    /**< Record a timelapse. Queues a taskproc to this library's thread.
     *
     * Timelapse will begin recording immediately, and will record until (end - start) time elapses.
     * Videos are first recorded to <dirname>/time_lapse/time_Task_<startTimeEpoch>/record.h264, and then converted to mp4 files.
     *
     * If the same parameters are passed in, the timelapse will attempt to append to any existing
     * .h264 files, effectively "continuing" an in-progress timelapse
     *
     * @param dirname name of the directory to write the timelapse into
     * @param startTimeEpoch start time of the timelapse. This value is only used to name the timelapse file
     *                      and calculate the duration of the timelapse. It does NOT determine the actual start time of the timelapse.
     * @param endTimeEpoch end time of the timelapse. Assumed to occur after start time. Used to calculate the duration of the timelapse.
     * @param interval time between frames of the timelapse
     */
    void (*EndTimelapse)(void);
    /**< End the timelapse early. The timelapse will be wrapped up and converted to an mp4. */
    void (*OnTimelapseFinished)(LTMediaRecorder_VideoFinishedProc *videoProc, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData);
    /**< Register to receive an event when a timelapse is finished */
    void (*NoTimelapseFinished)(LTMediaRecorder_VideoFinishedProc *videoProc);
    /**< Unregister from timelapse finished events */

    void (*EnableRecordingAudio) (void);
    /**< Enable recording audio for continuous recordings >*/
    void (*DisableRecordingAudio) (void);
    /**< Disable recording audio for continuous recordings >*/

    /* === System level API === */

    bool (*Start)(void);
    /**< System-level API. Start the daemon thread that manages the recording and video writing logic of this library
     *
     * @return true if the daemon thread has been started already or was successfully started
     * @note The application-level API above will NOT work unless the daemon thread of the library is running.
     * @note Mutex-protected.
    */
    void (*Stop)(void);
    /**< System-level API. Stop the daemon thread.
     *
     * @note Mutex-protected.
    */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif // LT_INCLUDE_LT_MEDIA_LTMEDIARECORDER_H
/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Sep-23   otho        created IndoorCameraSDCard
 *  16-Nov-23   aurelian    renamed/refactored to LTMediaRecorder, added event saving
*/