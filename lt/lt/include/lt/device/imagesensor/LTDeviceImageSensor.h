/******************************************************************************
 * <lt/device/imagesensor/LTDeviceImageSensor.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/**
 * @defgroup ltdevice_imagesensor LTDeviceImageSensor
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for image sensor devices.
 */

#ifndef LT_INCLUDE_LT_DEVICE_IMAGESENSOR_LTDEVICEIMAGESENSOR_H
#define LT_INCLUDE_LT_DEVICE_IMAGESENSOR_LTDEVICEIMAGESENSOR_H

#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

typedef u32 LTImageSensorEvent;
enum LTImageSensorEvent {
    kLTImageSensorEvent_PowerOn,
    kLTImageSensorEvent_PowerOnFailed,
};

typedef u32 LTImageSensorAttribute;
enum LTImageSensorAttribute {
    kLTImageSensorAttribute_Id,
    kLTImageSensorAttribute_FrameRate,
    kLTImageSensorAttribute_Crop,
    kLTImageSensorAttribute_FlipVerticalHorizontal,
    kLTImageSensorAttribute_AGain,
    kLTImageSensorAttribute_DGain,
    kLTImageSensorAttribute_Exposure,
    kLTImageSensorAttribute_ExposureForFps,
    kLTImageSensorAttribute_Sleep,
    kLTImageSensorAttribute_TuningData,
    kLTImageSensorAttribute_AeTable,
    kLTImageSensorAttribute_Status,
    /* Pixel format — value is LTImageSensorPixelFormat */
    kLTImageSensorAttribute_PixelFormat,
    /* Output frame size index — value is LTImageSensorFrameSize */
    kLTImageSensorAttribute_FrameSize,
    /* Output frame resolution (width × height) — value is LTImageSensorFrameResolution */
    kLTImageSensorAttribute_FrameResolution,
    /* JPEG compression quality 0–63 (lower = higher quality) */
    kLTImageSensorAttribute_JpegQuality,
    /* Brightness -2..+2 */
    kLTImageSensorAttribute_Brightness,
    /* Contrast -2..+2 */
    kLTImageSensorAttribute_Contrast,
    /* Saturation -2..+2 */
    kLTImageSensorAttribute_Saturation,
    /* Special effect — value is LTImageSensorSpecialEffect */
    kLTImageSensorAttribute_SpecialEffect,
    /* White-balance mode — value is LTImageSensorWBMode */
    kLTImageSensorAttribute_WhiteBalanceMode,
    /* AE level -2..+2 */
    kLTImageSensorAttribute_AeLevel,
    /* Auto-gain control enable (bool) */
    kLTImageSensorAttribute_AutoGainControl,
    /* Auto-exposure control enable (bool) */
    kLTImageSensorAttribute_AutoExposureControl,
    /* Auto-white-balance enable (bool) */
    kLTImageSensorAttribute_AutoWhiteBalance,
    /* AWB gain enable (bool) */
    kLTImageSensorAttribute_AwbGain,
    /* Gain ceiling — value is LTImageSensorGainCeiling */
    kLTImageSensorAttribute_GainCeiling,
    /* Bad-pixel correction enable (bool) */
    kLTImageSensorAttribute_BadPixelCorrection,
    /* White-pixel correction enable (bool) */
    kLTImageSensorAttribute_WhitePixelCorrection,
    /* Raw gamma enable (bool) */
    kLTImageSensorAttribute_RawGamma,
    /* Lens correction enable (bool) */
    kLTImageSensorAttribute_LensCorrection,
    /* Downsize+crop window enable (bool) */
    kLTImageSensorAttribute_DownsizeCropWindow,
    /* Colorbar test pattern enable (bool) */
    kLTImageSensorAttribute_Colorbar,
    /* AEC2 (night-mode auto-exposure) enable (bool) */
    kLTImageSensorAttribute_Aec2,
    /* Raw register access — value is LTImageSensorRegAccess */
    kLTImageSensorAttribute_Register,
    /* Sharpness level — sensor-defined range, typically -3..+3 */
    kLTImageSensorAttribute_Sharpness,
    /* Noise reduction level — sensor-defined range, typically 0..8; 0 = off */
    kLTImageSensorAttribute_Denoise,
    /* Pixel binning enable (bool) — halves resolution, improves low-light SNR */
    kLTImageSensorAttribute_Binning,
};

typedef u32 LTImageSensorPixelFormat;
enum LTImageSensorPixelFormat {
    kLTImageSensorPixelFormat_RGB565,
    kLTImageSensorPixelFormat_YUV422,
    kLTImageSensorPixelFormat_Grayscale,
    kLTImageSensorPixelFormat_JPEG,
};

typedef u32 LTImageSensorFrameSize;
enum LTImageSensorFrameSize {
    kLTImageSensorFrameSize_96x96,
    kLTImageSensorFrameSize_160x120,    /* QQVGA */
    kLTImageSensorFrameSize_176x144,    /* QCIF  */
    kLTImageSensorFrameSize_240x176,    /* HQVGA */
    kLTImageSensorFrameSize_240x240,
    kLTImageSensorFrameSize_320x240,    /* QVGA  */
    kLTImageSensorFrameSize_400x296,    /* CIF   */
    kLTImageSensorFrameSize_480x320,    /* HVGA  */
    kLTImageSensorFrameSize_640x480,    /* VGA   */
    kLTImageSensorFrameSize_800x600,    /* SVGA  */
    kLTImageSensorFrameSize_1024x768,   /* XGA   */
    kLTImageSensorFrameSize_1280x720,   /* HD    */
    kLTImageSensorFrameSize_1280x1024,  /* SXGA  */
    kLTImageSensorFrameSize_1600x1200,  /* UXGA  */
    kLTImageSensorFrameSize_2048x1536,  /* QXGA  — 3MP sensors (OV3660, OV5640) */
    kLTImageSensorFrameSize_Count,
};

typedef struct {
    u16 width;
    u16 height;
} LTImageSensorFrameResolution;

typedef u32 LTImageSensorSpecialEffect;
enum LTImageSensorSpecialEffect {
    kLTImageSensorSpecialEffect_None,
    kLTImageSensorSpecialEffect_Negative,
    kLTImageSensorSpecialEffect_BlackAndWhite,
    kLTImageSensorSpecialEffect_Reddish,
    kLTImageSensorSpecialEffect_Greenish,
    kLTImageSensorSpecialEffect_Blue,
    kLTImageSensorSpecialEffect_Retro,
};

typedef u32 LTImageSensorWBMode;
enum LTImageSensorWBMode {
    kLTImageSensorWBMode_Auto,
    kLTImageSensorWBMode_Sunny,
    kLTImageSensorWBMode_Cloudy,
    kLTImageSensorWBMode_Office,
    kLTImageSensorWBMode_Home,
};

typedef u32 LTImageSensorGainCeiling;
enum LTImageSensorGainCeiling {
    kLTImageSensorGainCeiling_2x,
    kLTImageSensorGainCeiling_4x,
    kLTImageSensorGainCeiling_8x,
    kLTImageSensorGainCeiling_16x,
    kLTImageSensorGainCeiling_32x,
    kLTImageSensorGainCeiling_64x,
    kLTImageSensorGainCeiling_128x,
};

typedef struct {
    u16  reg;   /* register address — full 16-bit for sensors with 16-bit addressing (OV3660, OV5640);
                   for banked 8-bit sensors (OV2640) the high byte selects the bank */
    u16  mask;
    u16  value;
} LTImageSensorRegAccess;

typedef struct {
    int left;
    int top;
    int width;
    int height;
} LTImageSensorCropRect;

typedef struct {
    u32 numerator;
    u32 denominator;
} LTImageSensorCropFract;

typedef struct {
    LTImageSensorCropRect  bounds;
    LTImageSensorCropRect  defrect;
    LTImageSensorCropFract pixelaspect;
} LTImageSensorCropCap;

typedef struct LTImageSensorAttributeParams {
    const void *in;  // point to the buffer of input bytes
    u32 inLen;       // length of input bytes
    void *out;       // point to the buffer of output bytes
    u32 outLen;      // length of output bytes
} LTImageSensorAttributeParams;

typedef void (LTDeviceImageSensor_OnEventProc)(LTImageSensorEvent event, void *pClientData);
/**< callback for image sensor events.
 *
 *   This callback is called to provide the client with image sensor events.
 *
 *   @param event the image sensor event type which has occurred.
 *   @param pClientData the client data that was specified when the callback was registered.
 */

/**
 * @brief The API for controlling image sensor devices.
 */
typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceImageSensor, 1) {

    void (* OnImageSensorEvent)(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc, void *clientData);
        /**< registers the callback for image sensor events.
         *
         *   @param hUnit the handle of the image sensor.
         *   @param eventProc the callback.
         *   @param clientData the client provided data to be delivered in the callback.
         */

    void (* NoImageSensorEvent)(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc);
        /**< unregisters the callback for image sensor events.
         *
         *   @param hUnit the handle of the image sensor.
         *   @param eventProc the callback.
         */

    void (* PowerOn)(LTDeviceUnit hUnit);
        /**< starts the asynchronous sensor power on process.
         *
         *   The power on process is asynchronous. When complete, status will be communicated via the image sensor event.
         *
         *   @param hUnit the handle of the image sensor.
         */

    void (* PowerOff)(LTDeviceUnit hUnit);
        /**< starts the asynchronous sensor power off process.
         *
         *   The power off process is asynchronous. When complete, status will be communicated via the image sensor event.
         *
         *   @param hUnit the handle of the image sensor.
         */

    bool (* SetAttribute)(LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *attrValue);
    bool (* GetAttribute)(LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *attrValue);
} LTLIBRARY_INTERFACE;

#ifndef DOXY_SKIP
typedef_LTLIBRARY_INTERFACE(ILTDriverImageSensor, 1) {
    void (* OnImageSensorEvent)(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc, void *clientData);
        /**< registers the callback for image sensor events.
         *
         *   @param hUnit the handle of the image sensor.
         *   @param eventProc the callback.
         *   @param clientData the client provided data to be delivered in the callback.
         */

    void (* NoImageSensorEvent)(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc);
        /**< unregisters the callback for image sensor events.
         *
         *   @param hUnit the handle of the image sensor.
         *   @param eventProc the callback.
         */

    void (* PowerOn)(LTDeviceUnit hUnit);
        /**< starts the asynchronous sensor power on process.
         *
         *   The power on process is asynchronous. When complete, status will be communicated via the image sensor event.
         *
         *   @param hUnit the handle of the image sensor.
         */

    void (* PowerOff)(LTDeviceUnit hUnit);
        /**< starts the asynchronous sensor power off process.
         *
         *   The power off process is asynchronous. When complete, status will be communicated via the image sensor event.
         *
         *   @param hUnit the handle of the image sensor.
         */

    bool (* SetAttribute)(LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *attrValue);
    bool (* GetAttribute)(LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *attrValue);

} LTLIBRARY_INTERFACE;
#endif /* #ifndef DOXY_SKIP */

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_IMAGESENSOR_LTDEVICEIMAGESENSOR_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Jan-24   trajan      created
 */
