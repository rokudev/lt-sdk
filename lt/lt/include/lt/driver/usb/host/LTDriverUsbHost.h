/*******************************************************************************
 * <lt/driver/usb/host/LTDriverUsbHost.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Defines standard LTObject API for USB host controller drivers
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_USB_HOST_LTDRIVERUSBHOST_H
#define LT_INCLUDE_LT_DRIVER_USB_HOST_LTDRIVERUSBHOST_H

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/device/usb/LTDeviceUsbStd.h>

LT_EXTERN_C_BEGIN

typedef_LTENUM_SIZED(LTDriverUsbHostTransferResult, u8){
    kLTDriverUsbHostTransferResult_None = 0,   /* Transfer not started. This is not a valid status */
    kLTDriverUsbHostTransferResult_Success,    /* Transfer completed. Data successfully sent or received */
    kLTDriverUsbHostTransferResult_Error,      /* Transfer completed. Error occurred during transfer */
    kLTDriverUsbHostTransferResult_NakTimeout, /* Transfer completed. Timeout occurred during transfer */
    kLTDriverUsbHostTransferResult_NotStarted, /* Transfer not started. Input arguments given to transfer were invalid
                                                  or all hardware FIFOs are busy */
};

typedef void (LTDriverUsbHost_TransferCompleteCallback)(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSentOrReceived, void *clientData);
/**< Callback when a transfer has completed, either successfully or with an error
 *
 * @param deviceAddress Address of the device for the completed transaction
 * @param endpointNumber Endpoint the transaction took place on
 * @param result Reason why the transfer completed
 * @param bytesSendOrReceived Number of bytes successfully sent (OUT) or received (IN) during the transfer
 * @param clientData Client data to pass alongside callback
 */

typedef void LT_ISR_SAFE (LTDriverUsbHost_OnDeviceAttachedOrDetachedCallback)(bool attached, u8 deviceAddress, void *clientData);
/**< Callback when the hardware detects a new device is attached or an old device is detached
 *
 * @param attached True when device attached, False when device detached
 * @param deviceAddress 0 when device attached (default address), else, contains address of the detached device
 * @param clientData Client data to pass alongside callback
 */

/**< LTDriverUSBHost is a generic API for driving USB host controller hardware. With this interface, all four USB
 * transfer types can be implemented in the LTDevice layer.
 *
 * It is up to the LTDevice layer to serialize the usage of this API and ensure only one transfer to a specific address
 * and endpoint is occurring at a time. A transfer (token packet, data packet, handshake packet) is not considered
 * complete until the callback is called. Callbacks are used since there is only one expected user for this API
 * (LTDeviceUsbHost).
 *
 * NOTE: While this API makes reference to "transfers", it is probably more accurate to call them "transactions" at this
 * layer, as transfers are composed of multiple transactions, while a transaction is a single series of packets
 */
typedef_LTObject(LTDriverUsbHost, 1) {
    void (*Enable)(LTOThread *thread);
    /**< Enable USB host mode
     * @param thread Thread driver procs and callbacks should run on, typically will be the same as the thread managing LTDeviceUsbHost.
     */

    void (*Disable)(void);
    /**< Disable USB host mode */

    void (*SetOnDeviceAttachedOrDetached)(LTDriverUsbHost_OnDeviceAttachedOrDetachedCallback *attachDetachCallback, void *clientData);
    /**< Set a callback to be called when a USB device is attached or detached from the host controller
     * hardware. It is then up to the LTDevice layer to configure the USB device and keep track of the active USB
     * topology.
     *
     * @param attachDetachCallback Callback when device attach or detach is detected by the hardware
     * @param clientData Client data to pass alongside callback
     */

    void (*UnsetOnDeviceAttachedOrDetached)(LTDriverUsbHost_OnDeviceAttachedOrDetachedCallback *attachDetachCallback);
    /**< Remove the callback set in SetOnDeviceAttachedOrDetached
     *
     * @param attachDetachCallback Previously set callback to remove
     */

    void (*ResetBus)(void);
    /**< Pulse a reset signal on the USB bus. Used during LTDevice layer USB bus enumeration. */

    void (*ConfigureControlEndpoint)(u8 deviceAddress, u8 maxControlEndpointSize);
    /**< Set the maximum packet size of the control endpoint (endpoint 0) for an addressed USB device. This information
     * is typically obtained from reading the device descriptor during bus enumeration.
     *
     * From USB 2.0 Spec 5.5.3
     * "The allowable maximum control transfer data payload sizes for full-speed devices is 8, 16, 32, or 64 bytes; for
     * high-speed devices, it is 64 bytes and for low-speed devices, it is 8 bytes"
     *
     * @param deviceAddress The address of the USB device to configure
     * @param maxControlEndpointSize The maximum packet size the control endpoint for this device allows
     */

    void (*ConfigureEndpoint)(u8 deviceAddress, LTUsbEndpointDescriptor *usbEndpointDescriptor);
    /**< Configure an endpoint using the endpoint descriptor. This may include writing to registers that track the
     * maximum packet size of the endpoint, the direction, the transfer type, etc.
     *
     * @param deviceAddress The address of the USB device to configure
     * @param usbEndpointDescriptor Standard endpoint descriptor of the endpoint to configure. The descriptor includes
     * information on which endpoint is being configured, so that information does not need to be a separate parameter.
     */

    void (*TransferSetup)(u8 deviceAddress, LTUsbDeviceRequest *setupPacket, LTDriverUsbHost_TransferCompleteCallback completeCb, void *clientData);
    /**< Perform a USB transfer to write a setup packet to the provided address's control endpoint (endpoint 0)
     * This transfer performs the following steps:
     * 1. Token packet with a PID of SETUP is sent
     * 2. Data packet containing the 8-byte setup packet is sent
     * 3. Handshake packet is received from target device
     * 4. completeCb is called with the result.
     *    Note that completeCb will be called earlier if there is a failure before the handshake packet is received.
     *
     * @note This is only used for starting control transfers (Setup stage)
     *
     * @param deviceAddress Host-assigned address for an attached and configured device. It is up to the LTDevice layer
     * to choose an address and configure attached devices with an address. Values 0 through 127 are valid addresses,
     * with the address 0 being reserved for unconfigured devices.
     * @param endpointNumber Endpoint on a device the driver should direct transfers to. Endpoint numbers are defined by
     * the connected device and must be configured by the LTDevice before using. Values 0 through 15 are valid endpoint
     * numbers, with endpoint 0 being reserved for unconfigured devices.
     * @param setupPacket 8-byte setup packet to be sent to initiate a control transfer
     * @param completeCb Callback where result will be reported
     * @param clientData Client data to pass alongside callback
     */

    void (*TransferOut)(u8 deviceAddress, u8 endpointNumber, u8 *buf, u32 bufLen, LTDriverUsbHost_TransferCompleteCallback completeCb, void *clientData);
    /**< Perform a USB transfer to write data to the provided address and endpoint.
     *
     * This transfer performs the following steps:
     * 1. Token packet with a PID of OUT is sent
     * 2. Data packet containing contents of buf is loaded and sent
     * 3. Handshake packet is received from target device
     * 4. Steps 1-3 are repeated until all data is sent, and completeCb is called with the final result and total data
     * successfully sent. Note that completeCb will be called earlier if there is a failure before the handshake packet
     * is received.
     *
     * @note This is used in control OUT transfers (Data stage when bufLen > 0; Status stage when bufLen == 0), bulk OUT
     * transfers, interrupt OUT transfers, and isochronous OUT transfers.
     * LTDeviceUsbHost manages the protocol while LTDriverUsbHost focuses on manipulating the hardware to send packets
     *
     * @param deviceAddress Host-assigned address for an attached and configured device. It is up to the LTDevice layer
     * to choose an address and configure attached devices with an address. Values 0 through 127 are valid addresses,
     * with the address 0 being reserved for unconfigured devices.
     * @param endpointNumber Endpoint on a device the driver should direct transfers to. Endpoint numbers are defined by
     * the connected device and must be configured by the LTDevice before using. Values 0 through 15 are valid endpoint
     * numbers, with endpoint 0 being reserved for unconfigured devices.
     * @param buf Data to be sent
     * @param bufLen Length of data to be sent from buf
     * @param completeCb Callback where result will be reported
     * @param clientData Client data to pass alongside callback
     */

    void (*TransferIn)(u8 deviceAddress, u8 endpointNumber, u8 *buf, u32 bufLen, LTDriverUsbHost_TransferCompleteCallback completeCb, void *clientData);
    /**< Perform a USB transfer to read data from the provided address and endpoint.
     * This transfer performs the following steps:
     * 1. Token packet with a PID of IN is sent
     * 2. Data packet is received from device, and read into buf
     * 3. Handshake packet is sent by host to acknowledge successful receipt of data
     * 4. Steps 1-3 are repeated until all available data is read, the provided buffer is full, or an error occurred.
     * completeCb is called with the final result and total count of data successfully read. Note that completeCb will
     * be called earlier if there is a failure before the handshake packet is received.
     *
     * @note This is used in control IN transfers (Data stage when bufLen > 0; Status stage when bufLen == 0), bulk IN
     * transfers, interrupt IN transfers, and isochronous OUT transfers
     * LTDeviceUsbHost manages the protocol while LTDriverUsbHost focuses on manipulating the hardware to receive
     * packets
     *
     * @param deviceAddress Host-assigned address for an attached and configured device. It is up to the LTDevice layer
     * to choose an address and configure attached devices with an address. Values 0 through 127 are valid addresses,
     * with the address 0 being reserved for unconfigured devices.
     * @param endpointNumber Endpoint on a device the driver should direct transfers to. Endpoint numbers are defined by
     * the connected device and must be configured by the LTDevice before using. Values 0 through 15 are valid endpoint
     * numbers, with endpoint 0 being reserved for unconfigured devices.
     * @param buf Buffer to hold received data
     * @param bufLen Length of buffer to hold data. This will be maximum length of data that can be read. If received
     * data exceeds this length, it will be dropped.
     * @param completeCb Callback where result will be reported
     * @param clientData Client data to pass alongside callback
     */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_USB_HOST_LTDRIVERUSBHOST_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Jul-25   aurelian    created
 */
