/*******************************************************************************
 * <lt/device/usb/LTDeviceUsbClient.h> LTDeviceUsbClient
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCLIENT_H
#define LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCLIENT_H

#include <lt/LTTypes.h>
#include <lt/device/usb/LTDeviceUsbStd.h>

LT_EXTERN_C_BEGIN

typedef void LT_ISR_SAFE (*LTUsbDeviceConfigOkHandler)(bool ready, void *clientData);
    /**<
     * @brief Callback handler for the configuration status of the CDC device.
     *
     * The driver will call this to signal when the client device has been configured or disconnected by the host.
     *
     * @param ready True if the device is configured and ready for IO, false otherwise
     * @param clientData extra callback data
     */

typedef void (*LTUsbBufferReleaseProc)(void *data);
typedef struct {
    u8                    *data;
    u32                    size;
    u32                    processed;
    LTUsbBufferReleaseProc releaseProc;
} LTUsbIOBuffer;

typedef bool LT_ISR_SAFE (*LTUsbDeviceRequestHandler)(void *clientData, LTDeviceUsbStd_ControlStage stage, LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf);
    /**<
     * @brief Handler for device requests.
     *
     * The Idle stage should be ignored.
     *
     * In the Setup stage, validation should be done on the request and errors signalled via the return value.
     * If the request has a data stage, a sufficiently large buffer should be configured to ioBuf at this stage.
     * If the request direction is Out (device to host), then ioBuf should contain the response data.
     *
     * In the Data stage, if the request direction is In (host to device), ioBuf will contain the read data.
     *
     * In the Status stage, ioBuf still contains the data but will be released imminently.
     *
     * @param stage the current Control Transfer Stage
     * @param stage the original setup request
     * @param stage the in/out buffer for the data stage
     *
     * @returns false if the requests could not be handled
     */

typedef void LT_ISR_SAFE (*LTUsbEndpointIOReadyHandler)(void *clientData);


typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceUsbClient, 1) LTLIBRARY_EMPTY_INTERFACE;

typedef_LTLIBRARY_INTERFACE(ILTDriverUsbClientDeviceUnit, 1) {

    u8 (*GetNumEndpoints)(LTDeviceUnit device);
        /**<
         * @brief Get the number of endpoints supported by the device.
         *
         * @returns the number of endpoints, including the control endpoint
         */

    u16 (*GetEndpointMaxPacketSize)(LTDeviceUnit device, u8 endpoint);
        /**<
         * @brief Get the largest packet size an endpoint can support.
         *
         * This usually is the HW FIFO size.
         * The wMaxPacketSize of the corresponding LTUsbEndpointDescriptor
         * should be configured to be at most this number.
         *
         * @param endpoint the endpoint number
         *
         * @returns the maximum packet size of this endpoint
         */

    void (*SetDeviceRequestHandler)(LTDeviceUnit device, LTUsbDeviceRequestHandler handler, void *clientData);
        /**<
         * @brief Set the handler for device requests for this device.
         *
         * This handler needs to implement all the request types required by the USB spec,
         * relevant to its class, and the GET_DESCRIPTOR standard request.
         *
         * @param handler the class control request handler for this device class
         */

    bool (*ConfigureEndpoint)(LTDeviceUnit device, LTUsbEndpointDescriptor const *descriptor, LTUsbEndpointIOReadyHandler handler, void *clientData);
        /**<
         * @brief ...
         *
         * @param handler ...
         *
         * @returns true if successful
         */

    bool (*Enable)(LTDeviceUnit device, LTUsbDeviceConfigOkHandler configCallback, void *clientData);
        /**<
         * @brief Enable this device.
         *
         * All handlers and endpoint buffers need to be set when calling this.
         *
         * @returns true if successful
         */

    void (*Disable)(LTDeviceUnit device);
        /**<
         * @brief Enable this device.
         */

    s32 (*Read)(LTDeviceUnit device, u8 endpoint, u8 *buffer, u32 size);
    s32 (*Write)(LTDeviceUnit device, u8 endpoint, u8 const *buffer, u32 size);
        /**<
         * @brief Writes buffer data and sends it to the host
         * 
         * Loads buffer data into the FIFO and then sends all data stored in the FIFO, including data loaded from a previous FifoOnlyWrite()
         * 
         * @returns bytes written (0 if unsuccessful)
         */
    s32 (*FifoOnlyWrite)(LTDeviceUnit device, u8 endpoint, u8 const *buffer, u32 size);
        /**<
         * @brief Writes buffer data to buffer but does not send it to the host
         * 
         * Loads buffer data into the FIFO but does not send it to the host. 
         * Useful for multiple smaller writes without having to copy them all into one buffer.
         * 
         * @returns bytes written (0 if unsuccessful)
         */
    void (*SendNewPacket)(LTDeviceUnit device, u8 endpoint);
        /**<
         * @brief Send data currently stored in endpoint FIFO to host. FIFO is emptied in the process
         */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_USB_LTDEVICEUSBCLIENT_H */
