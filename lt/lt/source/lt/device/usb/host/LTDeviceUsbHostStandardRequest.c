/*******************************************************************************
 * <lt/device/usb/host/LTDeviceUsbHostStandardRequest.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/usb/host/LTDeviceUsbHost.h>

DEFINE_LTLOG_SECTION("usbstdreq");

#define USBSTDREQ_DO_LOG     0
#if     USBSTDREQ_DO_LOG
#define DLOG                        LTLOG
#else
#define DLOG                        LTLOG_LOGNULL
#endif

enum {
    kMaxValidDeviceAddress = 127,
};

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceUsbHostStandardRequest, LTDeviceUsbHostStandardRequestImpl) {
    LTDeviceUsbHost *usbHost;
} LTOBJECT_API;

/*  __________________
 *  Object private API
 */

/*  _________________
 *  Object public API
 */

static bool LTDeviceUsbHostStandardRequestImpl_GetDescriptor(LTDeviceUsbHostStandardRequestImpl *stdReq, u8 deviceAddress, u8 descriptorType, u8 descriptorIndex, u8 *descriptorBuf, u32 descriptorLength, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    DLOG("get.desc", "devAddr:%u descType:%u descIdx:%u len:%u", deviceAddress, descriptorType, descriptorIndex, descriptorLength);

    // Perform a control transfer for a standard GET_DESCRIPTOR request
    LTUsbDeviceRequest setupPacket = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_DeviceToHost | kLTDeviceUsbStd_RequestType_Type_Standard
                         | kLTDeviceUsbStd_RequestType_Recipient_Device,
        .bRequest = kLTDeviceUsbStd_Request_GetDescriptor,
        /* Descriptor Type (high byte) and Descriptor Index (low byte) */
        .wValue = (descriptorType << 8) | (descriptorIndex),
        /* Zero or Language ID */
        .wIndex = 0,
        /* Descriptor Length */
        .wLength = descriptorLength,
    };

    // Contents of setupPacket will be copied
    return stdReq->usbHost->API->ControlTransfer(deviceAddress, &setupPacket, descriptorBuf, descriptorLength, completeCb, clientData, synchronousModeTimeout);
}

static bool LTDeviceUsbHostStandardRequestImpl_SetAddress(LTDeviceUsbHostStandardRequestImpl *stdReq, u8 deviceAddressDestination, u8 deviceAddressToSet, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    DLOG("set.addr", "dest:%u addrToSet:%u", deviceAddressDestination, deviceAddressToSet);

    if (deviceAddressToSet > kMaxValidDeviceAddress) {
        LTLOG_YELLOWALERT("addr.inv", "%lu exceeds max address of %lu", LT_Pu32(deviceAddressToSet), LT_Pu32(kMaxValidDeviceAddress));
        return false;
    }

    // Perform a control transfer for a standard SET_ADDRESS request
    LTUsbDeviceRequest setupPacket = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_HostToDevice | kLTDeviceUsbStd_RequestType_Type_Standard
                         | kLTDeviceUsbStd_RequestType_Recipient_Device,
        .bRequest = kLTDeviceUsbStd_Request_SetAddress,
        /* Device address */
        .wValue = deviceAddressToSet,
        /* Zero */
        .wIndex = 0,
        /* Zero */
        .wLength = 0,
    };

    // Contents of setupPacket will be copied. No data payload.
    return stdReq->usbHost->API->ControlTransfer(deviceAddressDestination, &setupPacket, NULL, 0, completeCb, clientData, synchronousModeTimeout);
}

static bool LTDeviceUsbHostStandardRequestImpl_SetConfiguration(LTDeviceUsbHostStandardRequestImpl *stdReq, u8 deviceAddress, u8 configVal, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    DLOG("set.config", "devAddr:%u config:%u", deviceAddress, configVal);

    // Perform a control transfer for a standard SET_ADDRESS request
    LTUsbDeviceRequest setupPacket = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_HostToDevice | kLTDeviceUsbStd_RequestType_Type_Standard
                         | kLTDeviceUsbStd_RequestType_Recipient_Device,
        .bRequest = kLTDeviceUsbStd_Request_SetConfig,
        /* Configuration value */
        .wValue = configVal,
        /* Zero */
        .wIndex = 0,
        /* Zero */
        .wLength = 0,
    };

    // Contents of setupPacket will be copied. No data payload.
    return stdReq->usbHost->API->ControlTransfer(deviceAddress, &setupPacket, NULL, 0, completeCb, clientData, synchronousModeTimeout);
}

/*  __________________________________
 *  Object constructor and destructor
 */

static bool LTDeviceUsbHostStandardRequestImpl_ConstructObject(LTDeviceUsbHostStandardRequestImpl *stdReq) {
    stdReq->usbHost = lt_createobject(LTDeviceUsbHost);
    if (!stdReq->usbHost) return false;
    return true;
}

static void LTDeviceUsbHostStandardRequestImpl_DestructObject(LTDeviceUsbHostStandardRequestImpl *stdReq) {
    lt_destroyobject(stdReq->usbHost);
}

/*  _____________________
 *  Object API definition
 */

define_LTObjectImplPublic(LTDeviceUsbHostStandardRequest, LTDeviceUsbHostStandardRequestImpl,
    GetDescriptor,
    SetAddress,
    SetConfiguration,
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jul-25   aurelian    created
 */
