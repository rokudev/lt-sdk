/******************************************************************************
 * <lt/device/video/LTDeviceVideo.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_VIDEO_LTDEVICEVIDEO_H
#define LT_INCLUDE_LT_DEVICE_VIDEO_LTDEVICEVIDEO_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

// source generating raw image
typedef_LTENUM_SIZED(LTDeviceVideo_Source, s32) {
    kLTDeviceVideo_Source_0      = 0,    // typically, image sensor
    // kLTDeviceVideo_Source_More        // add more sources here
    kLTDeviceVideo_Source_Test   = 127,  // internal test source, if available
    kLTDeviceVideo_Source_None   = -1,   // no source available
};

// channel generating stream
typedef_LTENUM_SIZED(LTDeviceVideo_Channel, s32) {
    // Encoder channels
    kLTDeviceVideo_Channel_H264HD,       // H264 HD streaming channel
    kLTDeviceVideo_Channel_ImageHD,      // Image HD capture channel, for Jpeg
    kLTDeviceVideo_Channel_H264SD,       // H264 SD streaming channel
    kLTDeviceVideo_Channel_ImageSD,      // Image SD capture channel, for Jpeg
    // ISP (encoder-less) channels
    kLTDeviceVideo_Channel_IspHD,        // ISP data HD channel, for HD Yuv420 and MdData
    kLTDeviceVideo_Channel_IspSD,        // ISP data SD channel, for SD Yuv420
    // kLTDeviceVideo_Channel_More       // add more channels here
    kLTDeviceVideo_Channel_Count,
    kLTDeviceVideo_Channel_None   = -1,  // no channel available
};

typedef_LTENUM_SIZED(LTDeviceVideo_Param, s32) {
    // video wise / all channels
    kLTDeviceVideo_Param_ISPTuningMode,  // value = (LTDeviceVideo_ISPTuningMode *), set which daynight tuning data mode is being used
    kLTDeviceVideo_Param_Rotation,       // value = (u32 *),   180(rotate by 180 degrees), 0(no rotation)
    kLTDeviceVideo_Param_FlipVertical,   // value = NULL,      toggles current vertical flip
    kLTDeviceVideo_Param_FlipHorizontal, // value = NULL,      toggles current horizontal flip
    kLTDeviceVideo_Param_Wdr,            // value = (bool *),  turn wdr on/off
    kLTDeviceVideo_Param_AutoExposure,   // value = (bool *),  turn auto exposure on/off
    kLTDeviceVideo_Param_UpdateExposure, // value = (bool *),  request an immediate auto-exposure update, using the LTCameraExposure latest settings
    kLTDeviceVideo_Param_TuningData,     // value = (LTDeviceVideo_TuningData *), must call before enabling ISP.
    kLTDeviceVideo_Param_TuningDataHash, // value = (LTString), Get the SHA-256 hash of the tuning data.
    kLTDeviceVideo_Param_Framerate,
    kLTDeviceVideo_Param_Exposure,
    kLTDeviceVideo_Param_Brightness,
    kLTDeviceVideo_Param_NightVision,    // value = (LTDeviceVideo_NightVision *), get night vision state
    kLTDeviceVideo_Param_NightVisionActive,   // value = (LTDeviceVideo_NightVision *), get night vision state
    kLTDeviceVideo_Param_NightVisionCondition, // value = (NightVisionCondition *), set / get night vision day to night switch condition
    kLTDeviceVideo_Param_Status,         // value = NULL, log video driver status
    // per channel
    kLTDeviceVideo_Param_Bitrate,        // value = (LTDeviceVideo_Bitrate *)
    kLTDeviceVideo_Param_GopLength,      // value = (LTDeviceVideo_Gop *)
    kLTDeviceVideo_Param_OsdLogo,        // value = (LTDeviceVideo_Osd *)
    kLTDeviceVideo_Param_OsdTimestamp,   // value = (LTDeviceVideo_Osd *),  data points to a timestamp string.
    
    // Get resolution
    kLTDeviceVideo_Param_ResolutionHD,   // value = (LTMediaVideoResolution *), get HD resolution of the video device
    kLTDeviceVideo_Param_ResolutionSD,   // value = (LTMediaVideoResolution *), get SD resolution of the video device

    // kLTDeviceVideo_Param_More         // add more parameters here
    kLTDeviceVideo_Param_None = -1,
};

/* ALTA1P-371: To be a true platform-independent enum this should remove references to mode numbers and leave only the
 * targets for day and night mode. Due to convenience and lack of time it is currently being kept this way. */
typedef_LTENUM_SIZED(LTDeviceVideo_ISPTuningMode, s32) {
    kLTDeviceVideo_ISPTuningMode0 = 0,
    kLTDeviceVideo_ISPTuningMode1,
    kLTDeviceVideo_ISPTuningMode2,
    kLTDeviceVideo_ISPTuningMode3,
    // kLTDeviceVideo_ISPTuningMode4,
    // kLTDeviceVideo_ISPTuningMode5,
    // kLTDeviceVideo_ISPTuningMode6,
    // kLTDeviceVideo_ISPTuningMode7,
    kLTDeviceVideo_ISPTuningModeMax,
    kLTDeviceVideo_ISPTuningModeNaturalDay,     // Set/get day (natural) mode chosen by video driver
    kLTDeviceVideo_ISPTuningModeArtificialDay,  // Set/get day (artificial) mode chosen by video driver
    kLTDeviceVideo_ISPTuningModeNight,          // Set/get night mode chosen by video driver
    kLTDeviceVideo_ISPTuningModeNone = -1,
};

typedef_LTENUM_SIZED(LTDeviceVideo_FrameType, s32) {
    kLTDeviceVideo_Frame_I = 0,          // I-Frame
    kLTDeviceVideo_Frame_P,              // P-Frame
    /* Multiple YUV FrameTypes required because both share the ISP channel */
    kLTDeviceVideo_Frame_Yuv420,
    kLTDeviceVideo_Frame_Jpeg,
    kLTDeviceVideo_Frame_MdData,         // motion detection data
    // kLTDeviceVideo_Frame_More,        // add more image types here
    kLTDeviceVideo_Frame_Count,
    kLTDeviceVideo_Frame_None = -1,      // invalid type
};

typedef_LTENUM_SIZED(LTDeviceVideo_Event, u32) {
    kLTDeviceVideo_Event_FrameReady,     // frame is encoded and ready
    kLTDeviceVideo_Event_FrameDrop,      // frame dropped before encoding, for example, encoder buffer is full
    kLTDeviceVideo_Event_FrameFail,      // fail to encode frame
    kLTDeviceVideo_Event_MdDataReady,    // MD Data is ready
};

typedef_LTENUM_SIZED(LTDeviceVideo_NightVision, s32) {
    kLTDeviceVideo_NightVision_Day = 0,
    kLTDeviceVideo_NightVision_Night = 1,
    kLTDeviceVideo_NightVision_Invalid = -1,
};

typedef struct LTDeviceVideo_TuningData {
    const u8 *data;
    u32 dataLen;
} LTDeviceVideo_TuningData;

typedef struct LTDeviceVideo_Bitrate {
    LTDeviceVideo_Channel channel;
    u32 bitrate;                     // bit rate (kbps) set to encoder
    u32 stat;                        // bit rate (kbps) stat by encoder
} LTDeviceVideo_Bitrate;

typedef struct LTDeviceVideo_Gop {
    LTDeviceVideo_Channel channel;
    u32 length;                   // gop length set to encoder
} LTDeviceVideo_Gop;

typedef struct LTDeviceVideo_Osd {
    LTDeviceVideo_Channel channel;
    void *data;
    bool bEnable;
} LTDeviceVideo_Osd;

typedef struct LTDeviceVideo_VideoData {
    u8    *address;
    u32    length;
    LTTime time;
    u32    sequence;
    LTDeviceVideo_FrameType type;
} LTDeviceVideo_VideoData;

typedef void (LTDeviceVideo_EventProc)(LTDeviceVideo_Channel channel, LTDeviceVideo_Event event, LTDeviceVideo_VideoData *videoData, void *clientData);

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceVideo, 1);

struct LTDeviceVideo {

    INHERIT_DEVICE_LIBRARY_BASE

    bool (*Enable)(LTDeviceVideo_Source source);
    void (*Disable)(LTDeviceVideo_Source source);
    /**< Enable/disable video source. Must be called before start streaming.
     *   @param source  image source
     *   @return  true on success, false on failure
     *   @note  source should be interpreted by the driver. For example, for ak3918x video driver, 0 is mipi sensor.
     */

    bool (*Start)(LTDeviceVideo_Channel channel);
    void (*Stop)(LTDeviceVideo_Channel channel);
    /**< Start/stop a video channel.
     *   @param   channel  video channel
     *   @return  true on success, false on failure
     */

    void (*Pause)(void);
    void (*Resume)(void);
    /**< Pause/resume streams. After streaming starts, pause and resume all streams. Stream driver resources are not de-allocated.
     *   @param   channel  video channel
     *   @return  true on success, false on failure
     */

    void (*OnVideoEvent)(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc, void *clientData);
    void (*NoVideoEvent)(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc);
    /**< Subscribe/unscribe to video events */

    void (*ReleaseVideoData)(LTDeviceVideo_Channel channel, LTDeviceVideo_VideoData *videoData);
    /**< MUST release the video data in eventProc. */

    bool (*Capture)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type);
    s32  (*CaptureSingle)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 destMaxLen);
    s32  (*CaptureCrop)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 cropWidth, u32 cropHeight, u32 centerX, u32 centerY);
    /**< Capture: asynchronously capture a frame from a channel. Use OnVideoEvent to receive the frame.
     *   CaptureSingle: a convenient blocking API to capture a frame from a channel.
     *   CaptureCrop: capture a cropped region of a frame directly from the hardware buffer.
     *   @param channel     channel to capture fame
     *   @param type        frame type
     *   @param destBuf     destination buffer
     *   @param destMaxLen  max length of destination buffer
     *   @param cropWidth   width of the crop region (for CaptureCrop)
     *   @param cropHeight  height of the crop region (for CaptureCrop)
     *   @param centerX     x-coordinate of the center of the crop region (for CaptureCrop)
     *   @param centerY     y-coordinate of the center of the crop region (for CaptureCrop)
     *   @return  true if the channel can capture the frame type (and frame is captured for CaptureSingle), otherwise false.
     *   @note CaptureSingle returns the length of the frame or a negative error code.
     *   @note For YUV420 capture, you cannot currently call Capture directly, you must use CaptureSingle.
     */

    void (*RequestIdrFrame)(LTDeviceVideo_Channel channel);
    /**< Request an IDR frame.
     *   @param channel  the video channel to produce an IDR frame
     *   @note  Silently discard errors, for example, when the channel is not streaming, or the channel does not support streaming.
     */

    void (*Poll)(LTDeviceVideo_Channel channel);
    /**< Poll an encoder channel to get a frame. Use OnVideoEvent to receive the frame.
     *   @param channel  the encoder channel to encode a frame
     */

    bool (*GetParam)(LTDeviceVideo_Param param, void *value);
    bool (*SetParam)(LTDeviceVideo_Param param, const void *value);
    /**< Get/set a video parameter
     *   @param param  the parameter to get
     *   @param value  the value to set
     *   @return  true if the driver supports the parameter, else false.
     *   @note  The set operation may be asynchronous, depending on driver implementation.
     */
};

// This interface is only for video device to call. Apps shall only call video device APIs.
typedef_LTLIBRARY_INTERFACE(ILTVideo, 1) {
    bool (*Enable)(LTDeviceVideo_Source source);
    void (*Disable)(LTDeviceVideo_Source source);
    bool (*Start)(LTDeviceVideo_Channel channel);
    void (*Stop)(LTDeviceVideo_Channel channel);
    void (*Pause)(void);
    void (*Resume)(void);
    void (*OnVideoEvent)(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc, void *clientData);
    void (*NoVideoEvent)(LTDeviceVideo_Channel channel, LTDeviceVideo_EventProc *eventProc);
    void (*ReleaseVideoData)(LTDeviceVideo_Channel channel, LTDeviceVideo_VideoData *videoData);
    bool (*Capture)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type);
    s32  (*CaptureSingle)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 destMaxLen);
    s32  (*CaptureCrop)(LTDeviceVideo_Channel channel, LTDeviceVideo_FrameType type, u8 *destBuf, u32 cropWidth, u32 cropHeight, u32 centerX, u32 centerY);
    void (*RequestIdrFrame)(LTDeviceVideo_Channel channel);
    void (*Poll)(LTDeviceVideo_Channel channel);
    bool (*GetParam)(LTDeviceVideo_Param param, void *value);
    bool (*SetParam)(LTDeviceVideo_Param param, const void *value);
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif // LT_INCLUDE_LT_DEVICE_VIDEO_LTDEVICEVIDEO_H

/* Example operation sequences to stream or capture
 *
 * void EventProc(LTDeviceVideo_Channel channel, LTDeviceVideo_Event event, VideoData *videoData, void *clientData) {
 *     ...
 *     video->ReleaseVideoData(channel, videoData);
 * }
 *
 * void Stream(void) {
 *     video->Enable(kLTDeviceVideo_Source_0);
 *     ...
 *     video->Start(kLTDeviceVideo_Channel_H264HD);
 *     video->OnVideoEvent(kLTDeviceVideo_Channel_H264HD, EventProc, NULL);
 *     ...
 *     // wait until stream is no more needed;
 *     ...
 *     video->NoVideoEvent(kLTDeviceVideo_Channel_H264HD, EventProc);
 *     video->Stop(kLTDeviceVideo_Channel_H264HD);
 *     ...
 *     video->Disable(kLTDeviceVideo_Source_0);
 * }
 *
 * void Capture(void) {
 *     video->Enable(kLTDeviceVideo_Source_0);
 *     ...
 *     video->Start(kLTDeviceVideo_Channel_ImageHD);
 *     video->OnVideoEvent(kLTDeviceVideo_Channel_ImageHD, EventProc, NULL);
 *     video->CaptureSingle(kLTDeviceVideo_Channel_ImageHD, kLTDeviceVideo_Frame_Jpeg);
 *     ...
 *     // wait until capture is done;
 *     ...
 *     video->NoVideoEvent(kLTDeviceVideo_Channel_ImageHD, EventProc);
 *     video->Stop(kLTDeviceVideo_Channel_ImageHD);
 *     ...
 *     video->Disable(kLTDeviceVideo_Source_0);
 * }
 */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  12-Feb-24   gallienus   created
 */
