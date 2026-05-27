/*******************************************************************************
 * <lt/device/media/LTDeviceMedia.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Device Library for media capture.
 *
 ******************************************************************************/
/** @file LTDeviceMedia.h header for public interface class LTMediaSource
 */

#ifndef LT_INCLUDE_LT_DEVICE_MEDIA_LTDEVICEMEDIA_H
#define LT_INCLUDE_LT_DEVICE_MEDIA_LTDEVICEMEDIA_H

#include <lt/core/LTTime.h>
#include <lt/system/ipc/buffermanager/LTIPCBufferManager.h>

LT_EXTERN_C_BEGIN

typedef LTHandle LTMediaSource;
typedef LTHandle LTMediaSink;

typedef_LTENUM_SIZED(LTMediaKind, u32) {
    kLTMediaKind_Unknown,
    kLTMediaKind_Image_HD,
    kLTMediaKind_Image_SD,
    kLTMediaKind_Video_HD,
    kLTMediaKind_Video_SD,
    kLTMediaKind_Audio,
    kLTMediaKind_Motion,
};

typedef_LTENUM_SIZED(LTMediaEncoding, u32) {
    kLTMediaEncoding_Unknown,
    kLTMediaEncoding_Raw,
    kLTMediaEncoding_Jpeg,
    kLTMediaEncoding_H264,
    kLTMediaEncoding_PCMU,
    kLTMediaEncoding_PCMA,
    kLTMediaEncoding_PCM,
    kLTMediaEncoding_Opus,
    kLTMediaEncoding_Motion,
};

LT_INLINE const char *
LTMediaEncodingToString(LTMediaEncoding nEncoding) {
    switch (nEncoding) {
        case kLTMediaEncoding_Raw:     return "raw";
        case kLTMediaEncoding_Jpeg:    return "JPEG";
        case kLTMediaEncoding_H264:    return "H264";
        case kLTMediaEncoding_PCMU:    return "PCMU";
        case kLTMediaEncoding_PCMA:    return "PCMA";
        case kLTMediaEncoding_PCM:     return "PCM";
        case kLTMediaEncoding_Opus:    return "opus";
        case kLTMediaEncoding_Motion:  return "motion";
        default:
        case kLTMediaEncoding_Unknown: return "unknown";
    }
}

typedef enum {
    kLTMediaModeChangeEvent_FrameSourceSwitchingToDay,
    kLTMediaModeChangeEvent_FrameSourceSwitchingToNight,
    kLTMediaModeChangeEvent_FrameSourceSwitchComplete,
    kLTMediaModeChangeEvent_IRLEDSwitchingOff,
    kLTMediaModeChangeEvent_IRLEDSwitchingOn,
} LTMediaModeChangeEvent;

typedef enum {
    kLTMediaNalTypePFrame = 1,
    kLTMediaNalTypeIFrame = 5,
    kLTMediaNalTypeSei = 6,
    kLTMediaNalTypeSps = 7,
    kLTMediaNalTypePps = 8,
} LTMediaNalType;

typedef struct {
    u32 nClockRate;
    u8  nProfile;
    u8  nConstraints;
    u8  nLevel;
    u8  nPacketizationMode;
    u16 nWidth;
    u16 nHeight;
} LTMediaEncodingParams_H264;

typedef struct {
    u32 nSampleRate;
} LTMediaEncodingParams_G711;

typedef struct {
    u32 nSampleRate;
    u32 nChannels;
    u32 nBitsPerSample;
} LTMediaEncodingParams_PCM;

typedef struct {
    u32 nSampleRate;
    u32 nChannels;
} LTMediaEncodingParams_Opus;

typedef struct {
    LTMediaKind     nKind;
    LTMediaEncoding nEncoding;
    union {
        LTMediaEncodingParams_H264 h264;
        LTMediaEncodingParams_G711 g711;
        LTMediaEncodingParams_Opus opus;
        LTMediaEncodingParams_PCM  pcm;
    } params;
} LTMediaFormat;

typedef enum {
    kLTMediaEvent_Data,
    kLTMediaEvent_Error,
    kLTMediaEvent_RemoteBuffer,
} LTMediaEvent;

typedef struct {
    // Media data source can either be a raw local buffer or an IPC buffer handle.
    // The relevant buffer source is discriminated by the LTMediaEvent type.
    union {
        u8 *pData;                  // #kLTMediaEvent_Data: pointer to the media data buffer
        LTBufferID ipcBufferID;     // #kLTMediaEvent_RemoteBuffer: opaque ID for the IPC buffer
    };
    u32  nDataLen;
    u64  nTimestamp;                // Frame timestamp (microseconds)
    LTTime nTimestampDelta;         // Time since previous frame
    u8   ret;
    bool bEndOfFrame;
    union {
        struct {
            bool bNightVisionActive;
        } video;
        struct {
            u32  nEncodedSamples;
            u8   pcm_max_dB8;
        } audio;
    } meta;
} LTMediaData;

typedef struct {
    u32 width;
    u32 height;
} LTMediaResolution;

typedef struct {
    u32 x1;
    u32 y1;
    u32 x2;
    u32 y2;
} LTMediaRect;

typedef enum {
    LTMediaCameraFlags_None = 0,
    LTMediaCameraFlags_NightVision  = 1 << 0,   /**< Night vision mode is enabled */
    LTMediaCameraFlags_IsoSettled   = 1 << 1,   /**< Auto-exposure has settled */
    LTMediaCameraFlags_Snapshot     = 1 << 2,   /**< Snapshot was requested around this frame */
} LTMediaCameraFlags;

/* Camera Sensor enums */
typedef_LTENUM_SIZED(LTMediaNightVisionMode, u32) {
    /* The first three modes are aligned with the cloud settings */
    kLTMediaDayNight_Force_Day,
    kLTMediaDayNight_Force_Night,
    kLTMediaDayNight_Auto,
    /* Remaining modes are internal to the camera */
    kLTMediaDayNight_Auto_NightSample,
    kLTMediaDayNight_Debug_Manual, /* TODO: This can be removed in production, only used to prevent daynight switch proc from overriding custom manual daynight ISP tuning */
};

typedef_LTENUM_SIZED(LTMediaNightVisionCondition, u32) {
    kLTMediaNightVision_FromDusk = 0,
    kLTMediaNightVision_FromDark = 1,
    kLTMediaNightVision_NumConditions = 2
};

typedef struct {
    LTMediaCameraFlags flags;
    u32 exposureValue;  /**< exposure value*/
    u32 analogGain;     /**< Analog gain */
    u32 digitalGain;    /**< Digital gain */
    u32 totalIso;       /**< Combined exposure value including gain */
    u16 redGain;        /**< White balance red gain factor (filtered) */
    u16 redGainRaw;     /**< White balance red gain factor (raw) */
    u16 redGainBuf;     /**< White balance red gain factor (buffered) */
    u16 blueGain;       /**< White balance blue gain factor (filtered) */
    u16 blueGainRaw;    /**< White balance blue gain factor (raw) */
    u16 blueGainBuf;    /**< White balance blue gain factor (buffered) */
} LTMediaCameraSensor;
/**< Camera sensor parameters, primarily for internal debugging.
 *
 *   The live parameters from the camera sensor can be retrieved via the GetProperty API
 *   call with property name "camera.sensor". SetProperty calls to the same name are not
 *   currently supported, although if they were not all of these parameters can be set.
 *
 *   The meaning of these values is sensor hardware-dependent. Different platforms may
 *   not support all of the available parameters.
*/

typedef struct {
    u16 pipelineEnabled;    /**< bitfield of pipeline enables, indexed by LTMediaEncoding enum */
    u32 rawEventCount;
    u32 h264EventCount;
    u32 motionRefCount;
    u32 motionEventCount;
    u32 jpegRefCount;
    u32 jpegEventCount;
} LTMediaVideoPipelineStats;

typedef void (LTMediaSource_OnMediaEventProc)(LTMediaEvent event, void *eventData, void * pClientData);
/**< callback for media stream events.
 *
 *   This callback is called to provide the client with media stream events.
 *
 *   @param hSource the stream instance.
 *   @param event the media stream event type which has occurred.
 *   @param eventData (optional) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *                    The following #event types and corresponding #eventData types are supported:
 *                         kLTMediaEvent_Data:   LTMediaData
 *                         kLTMediaEvent_Error:  None
 *   @param pClientData the client data that was specified when the callback was registered.
 */

typedef void (LTMediaSource_OnModeChangeEventProc)(LTMediaModeChangeEvent event, void *eventData, void * pClientData);
/**< callback for mode change events.
 *
 *   This callback is called to provide the client with mode change events specific to the underlying media source.
 *   Not all media source will support all (or possibly any) mode change events.
 *
 *   @param hSource the stream instance.
 *   @param event the mode change event type which has occurred.
 *   @param eventData (optional) data associated with the event. The data type is dependent on event type as specified by the %event% parameter.
 *   @param pClientData the client data that was specified when the callback was registered.
 */

 /*  _________________________
     Library Root Interface */
 typedef_LTLIBRARY_INTERFACE(ILTDriverMedia, 1) {
/** ___________________________________
  *   @section LTDriverMedia Library
  *   * @brief a library for opening media input and output devices.
  *   *
  *   * LTDriverMedia provides a simple interface for opening media input/output devices.
  \______________________________________________________________________________________*/

    bool (* SupportsFormat)(LTMediaFormat * pFormat);
        /**< checks if the media driver supports the given format.
         *
         *   @param pFormat the media format.
         *   @return true if the driver supports the provided format, else false.
         */

    bool (* GetProperty)(const char *name, void *value);
        /**< gets a media property by name.
         *
         *   @param name the property to be set.
         *   @param value the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    bool (* SetProperty)(const char *name, const void *value);
        /**< sets a media property by name.
         *
         *   @param name the property to be set.
         *   @param args the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    LTMediaSource (* OpenSource)(LTMediaFormat * pFormat);
        /**< opens a media source which supports the given encoding and parameters.
         *
         *   Attempts to open and configure the source with the given parameters. If
         *   the parameters are acceptable to the source, it will be configured
         *   as specified and the source handle returned.
         *   The parameter string is formatted as:
         *      "param1=value1;param2=value2"
         *   where the specific parameters and values that are appropriate depend
         *   on the format.
         *
         *   @param pFormat the media data format that the source should provide.
         *   @return LTMediaSource handle if the configuration is supported, else 0.
         */

    LTMediaSink (* OpenSink)(LTMediaFormat * pFormat);
        /**< opens a media sink which supports the given encoding and parameters.
         *
         *   Attempts to open and configure the sink with the given parameters. If
         *   the parameters are acceptable to the sink, it will be configured
         *   as specified and the sink handle returned.
         *   The parameter string is formatted as:
         *      "param1=value1;param2=value2"
         *   where the specific parameters and values that are appropriate depend
         *   on the format.
         *
         *   @param pFormat the media data format that the sink should accept.
         *   @return LTMediaSink handle if the configuration is supported, else 0.
         */
} LTLIBRARY_INTERFACE; /**< LTMediaSource Library ROOT INTERFACE */

typedef_LTLIBRARY_INTERFACE(ILTMediaSource, 1) {
/** ___________________________________
  *   @section ILTMediaSource Interface
  *   * @brief an interface for media capture.
  *   *
  *   * ILTMediaSource provides a simple interface interacting with media input devices.
  \______________________________________________________________________________________*/

    LTMediaEncoding (* GetMediaEncoding)(LTMediaSource hSource);
        /**< gets the type of media that this source provides.
         *
         *   @param hSource the handle of the media source.
         *   @return the media type that this source provides.
         */

    void (* OnMediaEvent)(LTMediaSource hSource,
                          LTMediaSource_OnMediaEventProc * pCallback,
                          void * pClientData);
        /**< registers the callback for media stream events.
         *
         *   @param hSource the handle of the media source.
         *   @param pCallback the callback.
         *   @param pClientData the client provided data to be delivered in the callback.
         */

    void (* NoMediaEvent)(LTMediaSource hSource,
                          LTMediaSource_OnMediaEventProc * pCallback);
        /**< unregisters the callback for media stream events.
         *
         *   @param hSource the handle of the media source.
         *   @param pCallback the callback.
         */

    void (* OnModeChangeEvent)(LTMediaSource hSource,
                               LTMediaSource_OnModeChangeEventProc * pCallback,
                               void * pClientData);
        /**< registers the callback for mode change events.
         *
         *   @param hSource the handle of the media source.
         *   @param pCallback the callback.
         *   @param pClientData the client provided data to be delivered in the callback.
         */

    void (* NoModeChangeEvent)(LTMediaSource hSource,
                               LTMediaSource_OnModeChangeEventProc * pCallback);
        /**< unregisters the callback for mode change events.
         *
         *   @param hSource the handle of the media source.
         *   @param pCallback the callback.
         */

    void (* Start)(LTMediaSource hSource);
        /**< begins processing/output of media data from the source.
         *
         *   @param hSource the handle of the media source.
         */

    void (* Stop)(LTMediaSource hSource);
        /**< ends processing/output of media data from the source.
         *
         *   @param hSource the handle of the media source.
         */

    void (* Trigger)(LTMediaSource hSource);
        /**< triggers output of an individual frame. This function only applies to sources of kind kLTMediaKind_Image_HD/kLTMediaKind_Image_SD.
         *
         *   @param hSource the handle of the media source.
         */

    void (* Refresh)(LTMediaSource hSource);
        /**< clears out old media data and restarts the media source.
         *
         *   @param hSource the handle of the media source.
         */

    void (* SetTargetBitrate)(LTMediaSource hSource, u32 nTargetBitrate);
        /**< sets the media data bitrate.
         *
         *   @param hSource the handle of the media source.
         *   @param nTargetBitrate the target media bitrate in bytes/sec.
         */

    bool (* SetAeComp)(LTMediaSource hSource, int comp);
        /**< sets the exposure compensation.
         *
         *   @param hSource the handle of the media source.
         *   @param comp    the exposure compensation. The range is [0, 255], and the default is 128.
         *   @return  true on success, false on fail.
         */

    void (* SetGopLength)(LTMediaSource hSource, u32 nGopLength);
        /**< sets the media data gop length.
         *
         *   @param hSource the handle of the media source.
         *   @param nGopLength the target gop length.
         */

} LTLIBRARY_INTERFACE;

typedef_LTLIBRARY_INTERFACE(ILTMediaSink, 1) {
/** ___________________________________
  *   @section ILTMediaSink Interface
  *   * @brief an interface for media output.
  *   *
  *   * ILTMediaSink provides a simple interface interacting with media output devices.
  \______________________________________________________________________________________*/

    LTMediaEncoding (* GetMediaEncoding)(LTMediaSink hSink);
        /**< gets the type of media that this sink receives.
         *
         *   @param hSink the handle of the media sink.
         *   @return the media type that this sink receives.
         */

    bool (* HandleMediaData)(LTMediaSink hSink, LTMediaData * pPacket);
        /**< handles incoming media data to be processed/output by the sink.
         *
         *   @param hSink the handle of the media sink.
         *   @param pPacket the incoming media data packet.
         *
         *   @return true if success
         */

    void (* Start)(LTMediaSink hSink);
        /**< begins processing/output of media data from the sink.
         *
         *   @param hSink the handle of the media sink.
         */

    void (* Stop)(LTMediaSink hSink, bool discardQueue);
        /**< ends processing/output of media data from the sink.
         *
         *   @param hSink the handle of the media sink.
         *   @param discardQueue If true, discard any queued data.
         *                       If false, wait for queued data to be processed.
         */

    LT_SIZE (* GetMaxFrameByteSize)(LTMediaSink hSink);
        /**< Get maximum frame size in bytes from the sink.
         *
         *   Maximum amount of data to be sent with each call to
         *   HandleMediaData()
         *
         *   @param hSink the handle of the media sink.
         *
         *   @return (LT_Size) Maximum frame size in bytes.
         */

    u32 (* GetAvailableFrameBuffers)(LTMediaSink hSink);
        /**< Get available cache space in number of frames on the sink.
         *
         *   @param hSink the handle of the media sink.
         *
         *   @return Number of frames that can be cached.
         */
} LTLIBRARY_INTERFACE;

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceMedia, 1) {

    bool (* SupportsFormat)(LTMediaFormat * pFormat);
        /**< checks if the media driver supports the given format.
         *
         *   @param pFormat the media format.
         *   @return true if the driver supports the provided format, else false.
         */

    bool (* GetProperty)(const char *name, void *value);
        /**< gets a media property by name.
         *
         *   @param name the property to be retrieved.
         *   @param value the buffer for the property value to be retrieved into.
         *   @return true if the driver supports the property, else false.
         */

    bool (* SetProperty)(const char *name, const void *value);
        /**< sets a media property by name.
         *
         *   @param name the property to be set.
         *   @param value the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    LTMediaSource (* OpenSource)(LTMediaFormat * pFormat);
        /**< opens a media source which supports the given encoding and parameters.
         *
         *   Attempts to open and configure the source with the given parameters. If
         *   the parameters are acceptable to the source, it will be configured
         *   as specified and the source handle returned.
         *   The parameter string is formatted as:
         *      "param1=value1;param2=value2"
         *   where the specific parameters and values that are appropriate depend
         *   on the format.
         *
         *   @param pFormat the media data format that the source should provide.
         *   @return LTMediaSource handle if the configuration is supported, else 0.
         */

    LTMediaSink (* OpenSink)(LTMediaFormat * pFormat);
        /**< opens a media sink which supports the given encoding and parameters.
         *
         *   Attempts to open and configure the sink with the given parameters. If
         *   the parameters are acceptable to the sink, it will be configured
         *   as specified and the sink handle returned.
         *   The parameter string is formatted as:
         *      "param1=value1;param2=value2"
         *   where the specific parameters and values that are appropriate depend
         *   on the format.
         *
         *   @param pFormat the media data format that the sink should accept.
         *   @return LTMediaSink handle if the configuration is supported, else 0.
         */

} LTLIBRARY_INTERFACE;

typedef_LTObject(LTMediaNaluSei, 1) {
/** ___________________________________
 *   @section LTMediaNaluSei Object
 *   * @brief an interface to build Supplemental Enhancement Information NAL Units.
 *   *
 *   * To allow the camera to embed metadata (e.g. debug information) in the video stream
 *   * SEI NALUs can be built and inserted into the video stream by the video driver. This
 *   * object provides functionality to add SEI message fragments and return the finished
 *   * NALU binary data.
 \______________________________________________________________________________________*/

    bool (* PrepareMessageFrag)(LTMediaNaluSei *sei, u8 *pUUID, u32 nPayloadLen, void *pPayload);
        /**< Prepare a new fragment to add to the SEI NALU.
         *
         *   The caller-supplied payload buffer pointer must remain valid until
         *   GetEncodedNalu() is called.
         *
         *   It is an error to call this function after calling GetEncodedNalu().
         *
         *   @param sei the SEI NAL unit instance.
         *   @param pUUID the UUID of the message fragment to add to the NALU.
         *   @param nPayloadLen the number of bytes in the message payload.
         *   @param pPayload pointer to the message payload.
         *   @return true if fragment was added to the SEI
         */

    u32 (* GetEncodedNalu)(LTMediaNaluSei *sei, u8 **pPayload);
        /**< Build the output NALU from message fragments.
         *
         *   After this function has been called, the source payload pointers are no
         *   longer required by the object, and can be freed/reused by the caller.
         *   The returned NALU payload pointer remains valid until the object is
         *   destroyed.
         *
         *   @param sei the SEI NAL unit instance.
         *   @param pPayload pointer to location to store pointer to NALU payload (can be NULL).
         *   @return The number of bytes in the NAL Unit
         */

    void (* Discard)(LTMediaNaluSei *sei);
        /**< Discard SEI contents without destroying object.
         *
         *   This function allows an object to be reused rather than creating a
         *   new object. For example a single object can be created for a video
         *   stream and then rebuilt between frames (or as often as needed).
         *
         *   @param sei the SEI NAL unit instance.
         */

} LTOBJECT_API;


LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_MEDIA_LTDEVICEMEDIA_H
