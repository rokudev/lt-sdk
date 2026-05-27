/*******************************************************************************
 * <lt/media/mp4/LTMediaMP4.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Library for managing MP4 containers. Supports a single H264 stream and an
 * optional audio stream, which will be added if audio samples are added.
 *****************************************************************************/

#ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMP4_H
#define LT_INCLUDE_LT_MEDIA_LTMEDIAMP4_H

#include <lt/core/LTCore.h>
#include <lt/system/fs/LTFile.h>

LT_EXTERN_C_BEGIN

typedef struct LTMediaMP4_Builder LTMediaMP4_Builder;

typedef struct {
    u32 nBytesPerSample;
    u32 nSamplesPerSec;
} LTMediaMP4_AudioConfig;

typedef struct {
    u32 nAudioSamples;
    u32 nVideoSamples;
    u32 nSize;
    u32 nDurationMillis;
} LTMediaMP4_Stats;

typedef enum {
    kLTMediaMP4Streamer_Done,
    kLTMediaMP4Streamer_Error,
} LTMediaMP4StreamerEvent;

typedef u32 (LTMediaMP4_WriteCallback)(LTMediaMP4_Builder *builder, const u8 * bytes, u32 length, bool isStart, const char * containerName, void * clientData);
/**< Event callback type for LTMediaMP4_Builder->writeCallback
 *
 * @param builder the mp4 builder that will use the function
 * @param bytes pointer to data that needs to be written
 * @param length number of bytes written
 * @param isStart true indicates the current write is the start of the container, false otherwise
 * @param containerName the current container
 * 
 * @returns amount of bytes written
 */

typedef u32 (LTMediaMP4_InsertCallback)(LTMediaMP4_Builder *builder, u32 position, const u8 * data, u32 length, const char * containerName, void * clientData);
/**< Event callback type for LTMediaMP4_Builder->insertCallback
 *
 * @param builder the mp4 builder that will use the function
 * @param position the position in the file that the data needs to be written into
 * @param data pointer to the data that needs to be written into the new position
 * @param length the number of bytes
 * @param containerName the current container
 * 
 * @returns amount of bytes written
 * @note: This function should move the pointer back to its original position once the 
 *        data has been inserted into the position of choice
 */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTMediaMP4, 1);
struct LTMediaMP4Api {
    INHERIT_LIBRARY_BASE;

    LTMediaMP4_Builder* (*CreateBuilder)(LTMediaMP4_AudioConfig config, LTFile *outputFile);
    /**< Create MP4 container file and open it for streaming audio and video
     * samples.
     * 
     * @param outputFile an OPENED LTFile. If LTFile has not been opened, the file will be opened with bCreate=true.
     * 
     * @return an allocated LTMediaMP4_Builder object
     *
     * @note that the container will not be playable before the metadata has
     * been appended to it by the Finalize() step. 
     */
    
    LTMediaMP4_Builder* (*CreateGenericBuilder)(LTMediaMP4_AudioConfig config, LTMediaMP4_WriteCallback * writeCallback, LTMediaMP4_InsertCallback * insertCallback, void * clientData);
    /**< Create a generic MP4 builder that will write using writeCallback and seek using seekCallback. It has no
     * knowledge of the underlying implementation of the callbacks 
     * 
     * @param config an audio config
     * @param writeCallback a function pointer for generic write function
     * @param insertCallback a function pointer for generic insert function
     * @param clientData a pointer to any other arguments the user needs 
     * 
     * @return an allocated LTMediaMP4_Builder object
     */

    bool (*AddH264StreamData)(LTMediaMP4_Builder * builder, u8 * bytes, u32 length);
    /**< Append data from an H264 NAL unit stream in a way that builds a valid mp4. 
     *  
     * Compared to AddNALUnit(), this API will track and persist sps and pps NAL units that
     * have been passed in. It will re-add the latest sps and pps NAL units 
     * before every P-Frame and I-Frame.
     * 
     * @return true if write succeeded
     */

    bool (*AddNALUnit)(LTMediaMP4_Builder * builder, u8 * bytes, u32 length);
    /**< Append an H264 NAL unit. 
     * 
     * @return true if write succeeded
     */

    bool (*ExtendLastH264Frame)(LTMediaMP4_Builder * builder, u32 frames);
    /**< Extend the duration of the most recent H264 frame 
     *
     * In playback, this means the extended frame will be shown 
     * for more than a single frame. This is used to compensate 
     * for differences in capture times between two frames in order
     * to better represent the intended video.
     * 
     * TODO: Should a similar API be available for audio samples?
     *
     * @param frames number of frames to extend the last frame by
     * @return true if write succeeded
     */

    bool (*AddAudioSamples)(LTMediaMP4_Builder * builder, u8 * bytes, u32 length);
    /**< Append audio samples. 
     * 
     * @return true if write succeeded
     */

    u32 (*GetCurrentAudioSampleCount)(LTMediaMP4_Builder * builder);
    /**< Get the current number of audio samples in the mp4 */

    u32 (*GetCurrentVideoSampleCount)(LTMediaMP4_Builder * builder);
    /**< Get the current number of video samples/frames in the mp4 */

    u32 (*GetCurrentSize)(LTMediaMP4_Builder * builder);
    /**< Get the current size of the mp4 file. */

    u32 (*GetCurrentVideoDurationMillis)(LTMediaMP4_Builder * builder);
    /**< Get the duration of the video track. */

    LTMediaMP4_Stats (*Finish)(LTMediaMP4_Builder * builder);
    /**< Finalize MP4 file by writing the metadata to the end and freeing the builder object.
     * 
     * Will call close() on the file in order to flush and finalize written data. The builder
     * will no longer be able to used in any of the above API methods as it will be freed.
     * 
     * @return LTMediaMP4_Stats final stats for the completed mp4
     */

    void (*FreeBuilder)(LTMediaMP4_Builder * builder);
    /**< Free the builder object without finalizing the MP4 file
     *
     * Abort building the MP4 container and free the builder object. This will
     * leave the MP4 file corrupt and no recovery is possible.
     */
};

typedef_LTObject(LTMediaMP4Streamer, 1) {

    bool (*Start)(LTMediaMP4Streamer *stream, LTMediaMP4_AudioConfig config);
    /**< Initialize the builder as well as input the config and allocate the starting buffer
     * 
     * @param stream an initialized LTMediaMP4Streamer object.
     * @param config the audio config of the video being recorded
     * 
     * @return true if the function started correctly
     */

    bool (*Finish)(LTMediaMP4Streamer *stream);
    /**< End the upload process, send up all remaining data left in the buffer queue, free all lists
     * 
     * @param stream an initialized LTMediaMP4Streamer object.
     * 
     * @return true if LTMediaMP4Streamer finished correctly
     */

    void (*SetURL)(LTMediaMP4Streamer *stream, const char *url);
    /**< Sets the URL of the Streamer object
     * 
     * @param stream an initialized LTMediaMP4Streamer object.
     * @param url a string that contains the url to upload to
     */

    LTMediaMP4_Builder * (*GetBuilder)(LTMediaMP4Streamer *stream);
    /**< returns the builder in the streamer object
     * 
     * @param stream an initialized LTMediaMP4Streamer object.
     *
     * @return LTMediaMP4_Builder * builder within the streamer object
     */
    
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_MEDIA_LTMEDIAMP4_H */