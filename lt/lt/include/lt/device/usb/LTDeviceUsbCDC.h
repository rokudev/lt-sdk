/*******************************************************************************
 * <lt/device/usb/LTDeviceUsbCDC.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/** @file LTDeviceUsbCDC.h header for LT USB CDC
  */

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCDC_H_
#define ROKU_LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCDC_H_

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/device/usb/LTDeviceUsbStd.h>

LT_EXTERN_C_BEGIN

typedef_LTObject(LTDeviceUsbCDC, 1) {

    bool (*Init)(LTDeviceUsbCDC *device, const char *portName, LTOThread *thread, LTThread_TaskProc readReadyCallback, LTThread_TaskProc writeReadyCallback, LTThread_TaskProc errorCallback, void *clientData);
    /**<
     * @brief Initialize the USB CDC device with a specified thread for callbacks
     *
     * This will start the USB hardware and allow enumeration by the host.
     * The thread specified to this function will be used for all callbacks.
     *
     * Initialize this object as a specialization in order to indicate if USB CDC should be operated in USB client mode or host mode. For example,
     *    // Initialize in USB client mode
     *    LTDeviceUsbCDC *myUsbCdc = lt_createobject(LTDeviceUsbCDC);
     *    // Initialize in USB host mode
     *    LTDeviceUsbCDC *myUsbCdc = lt_createobject_typed(LTDeviceUsbCDC, LTDeviceUsbCDCHostMode);
     *
     * @important USB CDC can only be active in EITHER client mode or host mode. Both modes cannot be running simultaneously for a single USB bus.
     *
     * @param[in] portNum: The serial port number.
     * @param[in] thread: the thread to run callbacks on.
     * @param[in] readReadyCallback: Called when data is available to read.
     * @param[in] writeReadyCallback: Called when the device is ready to write data.
     * @param[in] errorCallback: Called when an error happens. Further IO is not possible on this device, and it should be stopped.
     * @param[in] clientData: The client's data supplied to all callbacks.
     */

    bool (*Start)(LTDeviceUsbCDC *device);
    /**<
     * @brief Start the USB CDC device
     *
     * In device mode, this will start the USB hardware and allow enumeration by the host.

     * In host mode, this will automatically start communication with the first connected CDC interface it finds.
     * Internally, this also includes managing connection/disconnection of the CDC interface and polling for available data to
     * read.
     *
     * @param[in] device: USB CDC device instance to start
     */

    void (*Stop)(LTDeviceUsbCDC *device);
    /**<
     * @brief Stop the USB CDC device.
     *
     * @param[in] device: USB CDC device to be stopped.
     */

    u32 (*GetMaxWriteSize)(LTDeviceUsbCDC *device);
    /**<
     * @brief Returns the maximum number of bytes that a call to Write() will consume.
     *
     * @param[in] device: USB CDC device
     * @return The maximum number of bytes that can be consumed by Write().
     */

    u32 (*GetMaxReadSize)(LTDeviceUsbCDC *device);
    /**<
     * @brief Returns the maximum number of bytes that a call to Read() will return.
     *
     * @param[in] device: USB CDC device
     * @return The maximum number of bytes that can be returned by Read().
     */

    s32 (*Write)(LTDeviceUsbCDC *device, void const *data, LT_SIZE size);
    /**<
     * @brief Write bytes to a USB CDC device
     *
     * @param[in] device: USB CDC device
     * @param[in] data: pointer to data to be written
     * @param[in] size: number of bytes to write
     * @return The number of bytes written. Zero means busy device. Negative indicates an error (see log)
     */

    s32 (*Read)(LTDeviceUsbCDC *device, void *data, LT_SIZE size);
    /**<
     * @brief Read bytes from a USB CDC device
     *
     * If size is less than GetMaxReadSize(), then Read() should be called repeatedly until it returns 0.
     * Otherwise readReadyCallback might not be called before the entire packet is read.
     *
     * @param[in] device: USB CDC device
     * @param[out] data: pointer to buffer to hold data that is read
     * @param[in] size: number of bytes to read
     * @return number of bytes actually read, or zero, or negative for error
     */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #define ROKU_LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCDC_H_ */
