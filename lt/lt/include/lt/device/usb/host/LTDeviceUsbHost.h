/*******************************************************************************
 * <lt/device/usb/host/LTDeviceUsbHost.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Defines device API for USB host controller
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_USB_HOST_LTDEVICEUSBHOST_H
#define LT_INCLUDE_LT_DEVICE_USB_HOST_LTDEVICEUSBHOST_H

#include <lt/LTTypes.h>
#include <lt/core/LTTime.h>
#include <lt/device/usb/LTDeviceUsbStd.h>

typedef enum {
    kLTDeviceUsbHostTransferResult_Complete = 0,
    kLTDeviceUsbHostTransferResult_Error,
} LTDeviceUsbHostTransferResult;

typedef void (LTDeviceUsbHost_OnTransferCompleteCb)(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
/**< Callback when a transfer has completed. This callback will be queued to the caller and take place in the context of the caller.
 * This has slightly different meanings depending on the transfer type, briefly summarized below. Please see the
 * corresponding transfer API function for more details on each transfer type's usage of this callback.
 * 1. Control Transfer - Control transfer has gone through all stages (setup/data/handshake) and has completed
 * sending/receiving data
 * 2. Bulk Transfer - All data has been sent or read from bulk transfer payload
 * 3. Interrupt Transfer - Data has been polled and read once, or data has been sent
 * 4. Isochronous Transfer - All data has been sent or read from the iso transfer payload
 *
 * @param result End result of the transfer, whether it completed successfully or ended with an error
 * @param buf Buffer of data originally passed to the transfer function. Contains either the data that was sent or was
 * read in the transfer
 * @param bufLen Length of buf. If amount of data available to be read exceeded this size, all excess data will have
 * been dropped.
 * @param bytesSentOrReceived Total number of bytes sent from buf or read into buf. This value may equal but never
 * exceed bufLen. For control transfers, this is specifically the number of bytes sent/received during the DATA stage.
 * @param clientData Additional data passed alongside callback
 */

typedef void (LTDeviceUsbHost_OnDeviceStatus)(u8 deviceAddress, bool connected, const LTUsbConfigDescriptor *configDescriptor, void *clientData);
/**< LTEvent callback when a connected device is ready to be used or when a device has disconnected. The provided const
 * pointers to data for a given device address will remain valid until the next disconnect event for the device address,
 * so be wary if caching these data pointers. Internally, this is guaranteed by not clearing the cached descriptor
 * data for a device address until all registrants have completed their LTEvent callback (and the LTEvent's
 * DispatchCompleteProc is subsequently fired).
 *
 * @param deviceAddress Host-assigned address of the connected or disconnected device
 * @param connected True if the device is connected, False if the device was disconnected
 * @param configDescriptor Full configuration descriptor of the device when connected is True. Pointer will be NULL when
 * connected is False. Pointer is valid until device is disconnected.
 * @param clientData Client data passed with event registration
 */

typedef bool (LTDeviceUsbHost_EnumerateConfigCb)(LTDeviceUsbStd_DescriptorType descriptorType, const LTUsbUnknownDescriptor *descriptor, void *clientData);
/**< Callback invoked for each descriptor found within a provided USB configuration descriptor.
 * The callback is called sequentially for each descriptor (interface association, interface, endpoint, etc.) contained
 * within the configuration descriptor hierarchy.
 *
 * @param descriptorType Descriptor type of the current descriptor being enumerated. This is the same as reading bDescriptorType from the descriptor itself, but it has been conveniently cast to an enum here.
 * @param descriptor Pointer to a buffer containing the raw descriptor data. The exact format depends on the descriptor type, but the common header will always be available.
 * For example, a descriptorType of "kLTDeviceUSBStd_DescriptorType_Interface" will correspond with the "LTUsbInterfaceDescriptor" struct
 * @param clientData Client data passed to EnumerateConfigDescriptor
 * @return True to continue enumeration to the next descriptor, False to stop enumeration early
 */

LT_EXTERN_C_BEGIN

/**< LTDeviceUsbHost API
 * This is an LTDevice-level API intended for use by multiple client libraries who need to interface with connected USB
 * devices/interfaces.
 *
 * For example, LTDeviceUsbHost can be used to implement support for a USB device class. That device class library can
 * then be used to implement an LTDriver for another LTDevice-layer interface. It might look like the following:
 *
 *      ak3918xDriverUsbHost - Platform dependent implementation
 *          | implements
 *      LTDeviceUsbHost - Platform independent LT library for the host-side implementation of the USB protocol
 *          | used in
 *      LTDeviceUsbHostClassAudio - Platform-independent LT library for the host-side implementation of the USB audio class
 *          | used in
 *      UsbDriverMediaAudio - Usb-specific implementation of LTDeviceMedia
 *          | implements
 *      LTDeviceMedia - Platform independent LT library for working with audio and video data
 *
 * IMPORTANT: Synchronous vs Asynchronous API usage
 * All transfer functions have a "synchronousModeTimeout" parameter.
 * When set to NULL, the transfer will execute asynchronously and completeCb will be queued to the caller when the transfer ends.
 * When non-null, this value is the amount of time the caller will be block and wait for a result.
 * - When a result arrives, the caller will directly call completeCb and the API function will return true
 * - If the timeout is reached without a result, the API function will return false.
 */
typedef_LTObject(LTDeviceUsbHost, 1) {
    /* Transfers */

    bool (*ControlTransfer)(u8 deviceAddress, LTUsbDeviceRequest *request, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< Perform a control transfer to a USB device/function at the specified address.
     *
     * @param deviceAddress Address of USB device/function to communicate with
     * @param request Payload for the SETUP packet of the control transfer
     * @param buf Buffer to store the data being sent or read from the control transfer
     * @param bufLen Length of buf. If data to be read cannot fit into the buffer, a bufLen amount of data will be read
     * into buf and the rest of the data will be dropped.
     * @param completeCb Callback to be called when control transfer completes
     * @param clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if transfer is successfully queued, false otherwise
     */

    bool (*BulkTransfer)(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< Perform a bulk transfer in either the IN or OUT direction and receive a callback when buf has been fully sent
     * or all available data has been read into buf.
     *
     * # OUT: Sending Data
     * A bufLen amount of data will be sent to the destination and a callback will be received once all data in buf has
     * been sent.
     *
     * Internally, buf will be automatically split into as many packets as needed to be sent over bus. If needed, this
     * includes a zero-length packet to indicate to the destination that the transfer has completed.
     *
     * # IN: Receiving Data
     * Up to a buflen amount of data will be read from the destination and a callback will be received once all
     * available data has been read. The amount of data that was read and copied into buf will be indicated in the
     * callback and will not exceed buflen. Any amount of data that exceeds buflen will be dropped.
     *
     * Internally, reading will only read one packet's worth of data before calling the callback.
     *
     * @param deviceAddress Address of USB device/function to communicate with
     * @param endpointAddress Combination of endpoint number and transfer direction. This should match the
     * endpointAddress provided by the associated endpoint descriptor.
     * @param buf Buffer to store the data being sent or read
     * @param bufLen Length of buf. If data to be read cannot fit into the buffer, a bufLen amount of data will be read
     * into buf and the rest of the data will be dropped.
     * @param completeCb Callback to be called when transfer completes
     * @param clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if transfer is successfully queued, false otherwise
     */

    bool (*InterruptTransfer)(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< Perform an interrupt transfer in either the IN or OUT direction and receive a callback when buf has been fully
     * sent or any available data has been read into buf.
     *
     * # OUT: Sending Data
     * A bufLen amount of data will be sent to the destination and a callback will be received once all data in buf has
     * been sent.
     *
     * Internally, the rate at which data is sent out will abide by the bInterval specified in its associated endpoint
     * descriptor. Data will also not necessary be sent immediately, it will only be sent on the next available
     * interval. Generally, interrupt transfers will not be used to move large amounts of data.
     *
     * # IN: Receiving Data
     * Up to a buflen amount of data will be read from the destination and a callback will be received once all
     * available data has been read. The amount of data that was read and copied into buf will be indicated in the
     * callback and will not exceed buflen. Any amount of data that exceeds buflen will be dropped.
     *
     * Internally, the destination is automatically polled until there is data available to be read. As soon as data is
     * available, it will be read into buf and the callback will be invoked. The data may not entirely fill bufLen.
     *
     * @important To continue polling for more interrupt IN transfers from the destination, simply call
     * InterruptTransfer again (either during or after the previous completion callback) to re-initate the automatic
     * polling process.
     *
     * @param deviceAddress Address of USB device/function to communicate with
     * @param endpointAddress Combination of endpoint number and transfer direction. This should match the
     * endpointAddress provided by the associated endpoint descriptor.
     * @param buf Buffer to store the data being sent or read
     * @param bufLen Length of buf. If data to be read cannot fit into the buffer, a bufLen amount of data will be read
     * into buf and the rest of the data will be dropped.
     * @param completeCb Callback to be called when transfer completes
     * @param clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if transfer is successfully queued, false otherwise
     */

    bool (*IsochronousTransfer)(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< Perform an isochronous transfer in either the IN or OUT direction and receive a callback when buf has been
     * fully sent or available data has been read into buf.
     *
     * # OUT: Sending Data
     * A bufLen amount of data will be sent to the destination and a callback will be received once all data in buf has
     * been sent.
     *
     * Internally, packets will be sent out at the interval specified in the associated endpoint descriptor. buf will be
     * automatically split into as many packets as needed to be sent over bus, which may involve multiple send
     * intervals. Since each isochronous transfer is technically defined as one send interval, this means the API may
     * perform multiple "isochronous transfers" internally for every call of this API.
     *
     * # IN: Receiving Data
     * Up to a buflen amount of data will be read from the destination and a callback will be received once all
     * available data has been read. The amount of data that was read and copied into buf will be indicated in the
     * callback and will not exceed buflen. Any amount of data that exceeds buflen will be dropped.
     *
     * The completion callback will be called as soon as any data is read.
     *
     * @important To continue receiving or sending data in a timely manner, IsochronousTransfer should be called again
     * as soon as the client is ready to receive or send more data. This can be done inside the completion callback of
     * the previous transfer or afterwards.
     *
     * @param deviceAddress Address of USB device/function to communicate with
     * @param endpointAddress Combination of endpoint number and transfer direction. This should match the
     * endpointAddress provided by the associated endpoint descriptor.
     * @param buf Buffer to store the data being sent or read
     * @param bufLen Length of buf. If data to be read cannot fit into the buffer, a bufLen amount of data will be read
     * into buf and the rest of the data will be dropped.
     * @param completeCb Callback to be called when transfer completes
     * @param clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if transfer is successfully queued, false otherwise
     */

    /* Device interfaces */

    void (*OnDeviceStatus)(LTDeviceUsbHost_OnDeviceStatus *onDeviceStatus, bool notifyCurrentlyConnected, void *clientData);
    /**< Register for an LTEvent callback when an interface or interface association of a specific type is connected or
     * disconnected from the USB topology.
     *
     * @param onDeviceStatus LTEvent callback when a USB device has been connected or disconnected from the USB
     * topology. If this was a connection, the callback will contain descriptor data clients can use to determine if the
     * new device matches their use case.
     * @param notifyCurrentlyConnected True to receive one LTEvent callback for each currently connected device upon
     * registration, in addition to receiving notifications for subsequent device disconnects and reconnects.
     * @param clientData Data to pass alongside LTEvent callback
     */

    void (*NoDeviceStatus)(LTDeviceUsbHost_OnDeviceStatus *onDeviceStatus);
    /**< Unregister LTEvent callback for when a device is connected or disconnected
     *
     * @param onDeviceStatus Previously registered LTEvent callback used in OnDeviceStatus
     */

    bool (*EnumerateConfigDescriptor)(const LTUsbConfigDescriptor *configDescriptor, LTDeviceUsbHost_EnumerateConfigCb *callback, void *clientData);
    /**< Parse and enumerate all descriptors contained within a "full" USB configuration descriptor (USB configuration
     * descriptor header followed by the rest of the available descriptors). The full configuration descriptor will
     * typically be obtained from the OnDeviceStatus event, and this function will be subsequently used to determine the
     * capabilities of a newly connected device.
     *
     * The enumeration proceeds sequentially through the descriptor hierarchy in the order they appear in the
     * configuration descriptor. The callback can terminate enumeration early by returning false.
     *
     * @param configDescriptor Pointer to buffer with the configuration descriptor to enumerate. This should be a
     * complete configuration descriptor that includes all associated sub-descriptors.
     * @param callback Function to be called for each descriptor found within the configuration descriptor
     * @param clientData Data to pass to the callback function
     * @return True if enumeration ran to complete (all descriptors enumerated), false if enumeration was terminated
     * early (callback returned false or error)
     */
} LTOBJECT_API;

/**< Standard Requests defined by USB 2.0 Spec 9.4
 * Standard requests are pre-defined payloads and responses used during control transfers that every USB device must support. This
 * provides a known baseline for the host to communicate and learn about connected USB devices.
 */
typedef_LTObject(LTDeviceUsbHostStandardRequest, 1) {
    bool (*GetDescriptor)(LTDeviceUsbHostStandardRequest *stdReq, u8 deviceAddress, u8 descriptorType, u8 descriptorIndex, u8 *descriptorBuf, u32 descriptorLength, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< See USB 2.0 Spec 9.4.3. This is standard device request with bRequest == GET_DESCRIPTOR. This is used to
     * read standard device, configuration, endpoint, and string descriptors from a USB device.
     *
     * @param deviceAddress Address of the USB function to retrieve a descriptor from
     * @param descriptorType Type of descriptor to retrieve (wValue). See LTDeviceUsbStd_DescriptorType.
     * @param descriptorBuf Buffer to store the descriptor data that was read
     * @param descriptorLength Length of descriptorBuf
     * @param completeCb Callback to be called when standard request completes
     * @prama clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if request is successfully queued, false otherwise
     */

    bool (*SetAddress)(LTDeviceUsbHostStandardRequest *stdReq, u8 deviceAddressDestination, u8 deviceAddressToSet, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< See USB 2.0 Spec 9.4.6. This is standard device request with bRequest == SET_ADDRESS. This is used to set the
     * address of a connected USB device during bus enumeration.
     *
     * @param deviceAddressDestination Address of the USB function that needs to be assigned a new address. This is typically 0 (0 is the address used for unconfigured USB functions).
     * @param deviceAddressToSet Address to assign to the USB function. This is any value from 1-127, selected by the USB host
     * @param completeCb Callback to be called when standard request completes
     * @prama clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if request is successfully queued, false otherwise
     */

    bool (*SetConfiguration)(LTDeviceUsbHostStandardRequest *stdReq, u8 deviceAddress, u8 configVal, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout);
    /**< See USB 2.0 Spec 9.4.7. This is standard device request with bRequest == SET_CONFIGURATION. This is used to set
     * the configuration (operating mode) of the USB device at the end of bus enumeration
     *
     * @param deviceAddress Address of the USB function to configure
     * @param configVal Index of one of the configurations the USB function listed in its device descriptor. If there is
     * only one configuration, this value would be 0.
     * @param completeCb Callback to be called when standard request completes
     * @prama clientData Data to pass alongside completion callback
     * @param synchronousModeTimeout Use API in synchronous mode when non-NULL, asynchronous mode when NULL. See topmost doc comment of the LTDeviceUsbHost API for more details.
     * @return True if request is successfully queued, false otherwise
     */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_USB_HOST_LTDEVICEUSBHOST_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Jul-25   aurelian    created
 */