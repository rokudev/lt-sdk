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

#include <lt/LTTypes.h>
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
};

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

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceImageSensor, 1);

/**
 * @brief The API for controlling image sensor devices.
 */
struct LTDeviceImageSensor {

    INHERIT_DEVICE_LIBRARY_BASE

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
};

#ifndef DOXY_SKIP // [
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
#endif // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_IMAGESENSOR_LTDEVICEIMAGESENSOR_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Jan-24   trajan      created
 */
