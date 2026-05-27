/*******************************************************************************
 * <lt/device/usb/host/LTDeviceUsbHost.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/usb/host/LTDeviceUsbHost.h>
#include <lt/driver/usb/host/LTDriverUsbHost.h>
#include <lt/system/usb/LTSystemUsbBusManager.h>

DEFINE_LTLOG_SECTION("usbhost");

#define USBHOST_DO_LOG     0
#if     USBHOST_DO_LOG
#define DLOG                        LTLOG
#else
#define DLOG                        LTLOG_LOGNULL
#endif

#define USBHOST_DO_ISRLOG  0
#if     USBHOST_DO_ISRLOG
#define ISRLOG                      LTLOG_STOMP
#else
#define ISRLOG                      LTLOG_LOGNULL
#endif

/*  _________________________
 *  Private enums and structs
 */

enum {
    kMaxControlEndpointHighSpeedSize = 64,                    /* Device address 0 is reserved for all connected by unconfigured devices */
    kMaxConnectedDevices             = 2,                     /* Maximum number of downstream devices for static allocation */
    kDeviceAddressNew                = 0,                     /* Address 0 reserved for all non-enumerated devices */
    kDeviceAddressStarting           = kDeviceAddressNew + 1, /* Non-zero address */
    kDeviceAddressMax                = kMaxConnectedDevices,  /* Valid device address are from 1 to 127 */
    kMaxEndpointsPerDevice           = 8,                     /* Maximum number of endpoints for each connected device */
    kControlEndpoint                 = 0,                     /* The control endpoint number is always 0 */
};

typedef enum {
    kBusEnumStage_None = 0,
    /* Attach, port status, etc */             /* USB 2.0 Spec 9.1.2, Step 1-3 */
    kBusEnumStage_Reset,                       /* USB 2.0 Spec 9.1.2, Step 4 */
    kBusEnumStage_GetDeviceDescriptor_Partial, /* USB 2.0 Spec 9.1.2, Step 6 (yes, this comes before step 5) */
    kBusEnumStage_SetAddress,                  /* USB 2.0 Spec 9.1.2, Step 5 */
    kBusEnumStage_GetDeviceDescriptor_Full,    /* USB 2.0 Spec 9.1.2, Step 6 (step 6 complete) */
    kBusEnumStage_GetConfigDescriptor_Partial, /* USB 2.0 Spec 9.1.2, Step 7 (begin step 7) */
    kBusEnumStage_GetConfigDescriptor_Full,    /* USB 2.0 Spec 9.1.2, Step 7 (step 7 complete) */
    kBusEnumStage_SetConfiguration,            /* USB 2.0 Spec 9.1.2, Step 8 */
    kBusEnumStage_Complete,
    kBusEnumStage_nStages,
} BusEnumStage;
/* Enum representing each stage of USB host bus enumeration, for use in the bus enumeration state machine */

typedef enum {
    kTransferContextState_Free         = 0, /* Transfer context is not in use */
    kTransferContextState_InUse        = 1, /* Transfer is in progress, context in use */
    kTransferContextState_SyncComplete = 2, /* Transfer is done but still needs callback to be called. Only applies to transfers that are using the synchronous API option */
    kTransferContextState_SyncTimeout  = 3,  /* Synchronous caller timed out, discard any result that eventually arrives */
} TransferContextState;

typedef struct {
    bool                   enumerated;       /* If a device has not been enumerated, the rest of this struct should be ignored */
    u8                     address;          /* Address of the device. This value will match its index in the static topology array */
    u8                     hubAddress;       /* Address of the hub this device is connected to (hub address 0 will be root hub) */
    bool                   isHub;            /* True when device is a hub subclass, false otherwise */
    LTUsbDeviceDescriptor *deviceDescriptor; /* Full device descriptor of the device */
    LTUsbConfigDescriptor *configDescriptor; /* Full config descriptor of the device */
} ConnectedDevice;
/* Structure to represent a device in the USB topology (device that is connected to the USB host and has gone through enumeration) */

typedef struct {
    BusEnumStage           stage;            /* Current stage of bus enumeration */
    ConnectedDevice       *device;           /* Device being enumerated */
} BusEnumContext;
/* Structure to track current state of bus enumeration */

typedef struct {
    /* Callback data */
    LTOThread                            *callerThread;         /* Thread callback will be queued to*/
    LTDeviceUsbHost_OnTransferCompleteCb *completeCb;           /* Callback when transfer completes */
    void                                 *completeCbClientData; /* Data to pass alongside callback */
    u8                                   *buf;                  /* Client buffer, should be valid until transfer completes */
    u32                                   bufLen;               /* Length of client buffer */
    u32                                   bytesSentOrReceived;  /* Total number of bytes sent or received during the transfer */
    LTDeviceUsbHostTransferResult         result;               /* Result of the transfer */

    /* Transfer data */
    u8                             deviceAddress;   /* Address of the destination device for the transfer */
    LTDeviceUsbStd_EndpointAddress endpointAddress; /* Endpoint address of the destination endpoint on the device for the transfer */
    LTUsbDeviceRequest             deviceRequest;   /* For control transfers */
    LTDeviceUsbStd_ControlStage    controlStage;    /* For control transfers */
    LTAtomic                       state;           /* General state of the transfer, see enum TransferContextState */
    bool                           synchronous;     /* Whether callback is directly invoked after busy-waiting or will be queued */
} TransferContext;
/* Structure to represent and track an ongoing USB transfer */

/*  _______
 *  Statics
 */

/* All downstream devices connected to the root hub (host).
 * The index in the array will match the deviceAddress of the member.
 * Address 0 is reserved and unused.
 * NOTE: Bytes can be saved by calculating an offset from address 0 instead of reserving */
static ConnectedDevice s_topology[1 + kMaxConnectedDevices];

/* Only one transaction per endpoint can take place at a time for a device
 * The 1st dimension of the array is the device address. Address 0 is reserved for not-yet-enumerated devices.
 * The 2nd dimension of the array is the endpoint number.
 * NOTE: Bytes can be saved by dedicating address 0 outside of the main pool instead of reserving */
static TransferContext s_transferPool[1 + kMaxConnectedDevices][kMaxEndpointsPerDevice];

static LTSystemUsbBusManager          *s_usbManager     = NULL;  /* Manages control to the USB bus. Will be used to reserve the USB bus for host mode */
static LTDeviceUsbHostStandardRequest *s_stdReq         = NULL;  /* API to standard requests */
static LTDriverUsbHost                *s_driver         = NULL;  /* USB host driver implementation */
static LTOThread                      *s_hostThread     = NULL;  /* Dedicated thread USB host operations and transfers take place on */
static BusEnumContext                  s_busEnumContext = {};    /* State of the current device on the USB bus being enumerated */

static LTArgsDescriptor s_DeviceStatusEventArgs = { 3, {kLTArgType_u8, kLTArgType_u8, kLTArgType_pointer} };
static LTEvent          s_hDeviceStatusEvent    = LTHANDLE_INVALID;

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceUsbHost, LTDeviceUsbHostImpl) {
} LTOBJECT_API;

/*  ____________________
 *  Forward declarations
 */

static void AdvanceControlTransfer(void *clientData);
static bool LTDeviceUsbHostImpl_EnumerateConfigDescriptor(const LTUsbConfigDescriptor *configDescriptor, LTDeviceUsbHost_EnumerateConfigCb *callback, void *clientData);
static void FreeTransfer(TransferContext *transfer);

/*  __________________
 *  Object private API
 */

static void NotifyTransferCompleteProc(void *clientData) {
    DLOG("trans.done", NULL);
    TransferContext *transfer = clientData;

    // Cache the transfer data
    TransferContext transferShallowCopy = *transfer;

    // Return transfer to the pool. This guarantees clients can start another transfer to the same endpoint in their completion callback.
    FreeTransfer(transfer);

    // Notify client their transfer is complete using the cached data
    transferShallowCopy.completeCb(transferShallowCopy.result, transferShallowCopy.buf, transferShallowCopy.bufLen, transferShallowCopy.bytesSentOrReceived, transferShallowCopy.completeCbClientData);
}

static void NotifyCallerTransferComplete(TransferContext *transfer, LTDeviceUsbHostTransferResult result, u32 bytesSentOrReceived) {
    DLOG("notify.done", "result %u", result);
    transfer->result = result;
    transfer->bytesSentOrReceived = bytesSentOrReceived;
    if (transfer->synchronous) {
        // Sync behavior, let original caller directly call their callback with the result
        bool waitingOnCaller = LTAtomic_CompareAndExchange(&transfer->state, kTransferContextState_InUse, kTransferContextState_SyncComplete);
        // Caller timed out earlier and is no longer waiting, discard the result
        if (!waitingOnCaller) FreeTransfer(transfer);
    } else {
        // Async behavior, queue result to caller and move on
        s_hostThread->API->QueueTaskProc(transfer->callerThread, NotifyTransferCompleteProc, NULL, transfer);
    }
}

/* Returns true if transfer completed and callback was called, false if transfer timed out */
static bool WaitForTransferComplete(TransferContext *transfer, const LTTime *timeout) {
    if (!timeout) return false;
    if (!transfer->synchronous) return false;

    // Busy wait and yield as long as result is not available and timeout is not reached
    LTTime timeoutEnd = LTTime_Add(LT_GetCore()->GetKernelTime(), *timeout);
    while (LTAtomic_Load(&transfer->state) != kTransferContextState_SyncComplete
           && LTTime_IsLessThan(LT_GetCore()->GetKernelTime(), timeoutEnd)) {
        LT_GetCore()->GetCurrentThreadObject()->API->Yield();
    }

    // Case 1: Timeout
    bool timeoutNotified = LTAtomic_CompareAndExchange(&transfer->state, kTransferContextState_InUse, kTransferContextState_SyncTimeout);
    if (timeoutNotified) {
        // Library thread is aware that synchronous caller is gone, any eventual response will be discarded
        DLOG("timeout", NULL);
        return false;
    } else {
        // Response became available *just* as caller timed out, move on to happy path
    }

    // Case 2: Result available
    if (LTAtomic_Load(&transfer->state) == kTransferContextState_SyncComplete) {
        // Transfer complete, directly free transfer and call callback
        NotifyTransferCompleteProc(transfer);
        return true;
    }

    // Case 3: Should not occur. Transfer state machine possibly broken.
    LTLOG_YELLOWALERT("wait.unexpected", "Unexpected state: %lu", LT_Pu32(LTAtomic_Load(&transfer->state)));
    return false;
}


static u8 GetNextAvailableTopologyAddress(void) {
    // Skip address 0 when looking for available addresses
    for (u8 i = 1; i < kMaxConnectedDevices; i++) {
        if (!s_topology[i].enumerated) {
            // Available address found
            return i;
        }
    }
    // No available address found
    return 0;
}

static void AddDeviceToTopology(u8 deviceAddress) {
    DLOG("topo.add", "addr %u", deviceAddress);
    s_topology[deviceAddress].enumerated = true;
}

static void RemoveDeviceFromTopology(u8 deviceAddress) {
    DLOG("topo.rem", "addr %u", deviceAddress);
    lt_free(s_topology[deviceAddress].deviceDescriptor);
    lt_free(s_topology[deviceAddress].configDescriptor);
    lt_memset(&s_topology[deviceAddress], 0, sizeof(ConnectedDevice));
}

static ConnectedDevice *GetDeviceFromTopology(u8 deviceAddress) {
    for (u8 i = 0; i < kMaxConnectedDevices; i++) {
        if (s_topology[i].address == deviceAddress) {
            return &s_topology[i];
        }
    }
    return NULL;
}

static void NotifyDeviceStatusEvent(u8 deviceAddress, bool connected) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hDeviceStatusEvent);
    if (connected) {
        ConnectedDevice *device = GetDeviceFromTopology(deviceAddress);
        iEvent->NotifyEvent(s_hDeviceStatusEvent, deviceAddress, connected, device->configDescriptor);
    } else {
        iEvent->NotifyEvent(s_hDeviceStatusEvent, deviceAddress, connected, NULL);
    }
}

static void DispatchDeviceStatusProc(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    (*(LTDeviceUsbHost_OnDeviceStatus *)eventProc)(LTArgs_u8At(0, eventArgs),
                                                   LTArgs_u8At(1, eventArgs),
                                                   LTArgs_pointerAt(2, eventArgs),
                                                   eventProcClientData);
}

static void DispatchDeviceStatusCompleteProc(LTEvent hEvent, LTArgs *eventArgs) {
    LT_UNUSED(hEvent);
    u8 deviceAddress = LTArgs_u8At(0, eventArgs);
    bool connected = LTArgs_u8At(1, eventArgs);
    if (!connected) {
        // All clients have been notified the device has been disconnected, OK to clear data
        RemoveDeviceFromTopology(deviceAddress);
    }
}

static void DispatchDeviceStatusImmediateProc(LTEvent hEvent, void *notifyImmediateEventStateClientData, void *eventProc, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LT_UNUSED(notifyImmediateEventStateClientData);
    for (u8 i = 0; i < kMaxConnectedDevices; i++) {
        ConnectedDevice *device = &s_topology[i];
        if (device->enumerated) {
            (*(LTDeviceUsbHost_OnDeviceStatus *)eventProc)(device->address,
                                                           true,
                                                           device->configDescriptor,
                                                           eventProcClientData);
        }
    }
}

static void DisconnectedProc(void *clientData) {
    u8 deviceAddress = (LT_SIZE)clientData;
    NotifyDeviceStatusEvent(deviceAddress, false);
}

static void PrintDescriptor(LTDeviceUsbStd_DescriptorType descriptorType, const u8 *descriptor) {
    if (!descriptor) {
        LTLOG_YELLOWALERT("desc.print.null", NULL);
        return;
    }

    // Assumes provided descriptor buffer is valid
    switch (descriptorType) {
        case kLTDeviceUsbStd_DescriptorType_Device: {
            LTUsbDeviceDescriptor *deviceDesc = (LTUsbDeviceDescriptor *)descriptor;
            DLOG("desc.device.bLength",         "%u",      deviceDesc->bLength);
            DLOG("desc.device.bDescriptorType", "%u",      deviceDesc->bDescriptorType);
            DLOG("desc.device.bcdUSB",          "0x%04x",  deviceDesc->bcdUSB);
            DLOG("desc.device.bDeviceClass",    "%u",      deviceDesc->bDeviceClass);
            DLOG("desc.device.bDeviceSubClass", "%u",      deviceDesc->bDeviceSubClass);
            DLOG("desc.device.bDeviceProtocol", "%u",      deviceDesc->bDeviceProtocol);
            DLOG("desc.device.bMaxPacketSize0", "%u",      deviceDesc->bMaxPacketSize0);
            DLOG("desc.device.idVendor",        "0x%04x",  deviceDesc->idVendor);
            DLOG("desc.device.idProduct",       "0x%04x",  deviceDesc->idProduct);
            DLOG("desc.device.bcdDevice",       "0x%04x",  deviceDesc->bcdDevice);
            DLOG("desc.device.iManufacturer",   "%u",      deviceDesc->iManufacturer);
            DLOG("desc.device.iProduct",        "%u",      deviceDesc->iProduct);
            DLOG("desc.device.iSerialNumber",   "%u",      deviceDesc->iSerialNumber);
            DLOG("desc.device.bNumConfigurations", "%u",   deviceDesc->bNumConfigurations);
        } break;

        case kLTDeviceUsbStd_DescriptorType_Config: {
            LTUsbConfigDescriptor *configDesc = (LTUsbConfigDescriptor *)descriptor;
            DLOG("desc.config.bLength",            "%u",      configDesc->bLength);
            DLOG("desc.config.bDescriptorType",    "%u",      configDesc->bDescriptorType);
            DLOG("desc.config.wTotalLength",       "%u",      configDesc->wTotalLength);
            DLOG("desc.config.bNumInterfaces",     "%u",      configDesc->bNumInterfaces);
            DLOG("desc.config.bConfigurationValue", "%u",     configDesc->bConfigurationValue);
            DLOG("desc.config.iConfiguration",     "%u",      configDesc->iConfiguration);
            DLOG("desc.config.bmAttributes",       "0x%02x",  configDesc->bmAttributes);
            DLOG("desc.config.bMaxPower",          "%u",      configDesc->bMaxPower);
        } break;

        case kLTDeviceUsbStd_DescriptorType_Interface: {
            LTUsbInterfaceDescriptor *interfaceDesc = (LTUsbInterfaceDescriptor *)descriptor;
            DLOG("desc.if.bLength",            "%u",  interfaceDesc->bLength);
            DLOG("desc.if.bDescriptorType",    "%u",  interfaceDesc->bDescriptorType);
            DLOG("desc.if.bInterfaceNumber",   "%u",  interfaceDesc->bInterfaceNumber);
            DLOG("desc.if.bAlternateSetting",  "%u",  interfaceDesc->bAlternateSetting);
            DLOG("desc.if.bNumEndpoints",      "%u",  interfaceDesc->bNumEndpoints);
            DLOG("desc.if.bInterfaceClass",    "%u",  interfaceDesc->bInterfaceClass);
            DLOG("desc.if.bInterfaceSubClass", "%u",  interfaceDesc->bInterfaceSubClass);
            DLOG("desc.if.bInterfaceProtocol", "%u",  interfaceDesc->bInterfaceProtocol);
            DLOG("desc.if.iInterface",         "%u",  interfaceDesc->iInterface);
        } break;

        case kLTDeviceUsbStd_DescriptorType_Endpoint: {
            LTUsbEndpointDescriptor *endpointDesc = (LTUsbEndpointDescriptor *)descriptor;
            DLOG("desc.ep.bLength",          "%u",      endpointDesc->bLength);
            DLOG("desc.ep.bDescriptorType",  "%u",      endpointDesc->bDescriptorType);
            DLOG("desc.ep.bEndpointAddress", "0x%02x",  endpointDesc->bEndpointAddress);
            DLOG("desc.ep.bmAttributes",     "0x%02x",  endpointDesc->bmAttributes);
            DLOG("desc.ep.wMaxPacketSize",   "%u",      endpointDesc->wMaxPacketSize);
            DLOG("desc.ep.bInterval",        "%u",      endpointDesc->bInterval);

            // Decode endpoint direction and type for more readable log
            const char *direction = (endpointDesc->bEndpointAddress & kLTDeviceUsbStd_EndpointAddress_Dir_Mask) ? "IN" : "OUT";
            u8 epNumber = endpointDesc->bEndpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask;
            u8 epType = endpointDesc->bmAttributes & kLTDeviceUsbStd_Attribute_EPTransfer_Mask;
            const char *epTypeStr[] = {"Control", "Isochronous", "Bulk", "Interrupt"};
            DLOG("desc.ep.decoded", "EP%u %s %s, MaxPkt:%u", epNumber, direction, epTypeStr[epType], endpointDesc->wMaxPacketSize);
        } break;

        case kLTDeviceUsbStd_DescriptorType_InterfaceAssociation: {
            LTUsbInterfaceAssociationDescriptor *iadDesc = (LTUsbInterfaceAssociationDescriptor *)descriptor;
            DLOG("desc.iad.bLength",            "%u",  iadDesc->bLength);
            DLOG("desc.iad.bDescriptorType",    "%u",  iadDesc->bDescriptorType);
            DLOG("desc.iad.bFirstInterface",    "%u",  iadDesc->bFirstInterface);
            DLOG("desc.iad.bInterfaceCount",    "%u",  iadDesc->bInterfaceCount);
            DLOG("desc.iad.bFunctionClass",     "%u",  iadDesc->bFunctionClass);
            DLOG("desc.iad.bFunctionSubClass",  "%u",  iadDesc->bFunctionSubClass);
            DLOG("desc.iad.bFunctionProtocol",  "%u",  iadDesc->bFunctionProtocol);
            DLOG("desc.iad.iFunction",          "%u",  iadDesc->iFunction);
        } break;

        case kLTDeviceUsbStd_DescriptorType_String: {
            LTUsbStringDescriptor *stringDesc = (LTUsbStringDescriptor *)descriptor;
            DLOG("desc.string.bLength",        "%u", stringDesc->bLength);
            DLOG("desc.string.bDescriptorType", "%u", stringDesc->bDescriptorType);
        } break;

        default: {
            // Unknown descriptor, read the guaranteed 2-byte header and move on
            LTUsbUnknownDescriptor *desc = (LTUsbUnknownDescriptor *)descriptor;
            DLOG("desc.unknown.bLength",        "%u", desc->bLength);
            DLOG("desc.unknown.bDescriptorType", "%u", desc->bDescriptorType);
        } break;
    }
}

static bool IsTransferRequestValid(u8 deviceAddress, u8 endpointNumber) {
    // Ensure device address is in bounds
    if (deviceAddress > kMaxConnectedDevices) return false;
    // Ensure endpoint number is in bounds
    if (endpointNumber > kMaxEndpointsPerDevice) return false;
    return true;
}

static TransferContext *MallocTransfer(u8 deviceAddress, u8 endpointNumber) {
    DLOG("malloc.transfer.try", "%u %u", deviceAddress, endpointNumber);
    if (IsTransferRequestValid(deviceAddress, endpointNumber)) {
        // All clear, attempt to reserve this endpoint for a transfer!
        TransferContext *transfer = &s_transferPool[deviceAddress][endpointNumber];
        if (LTAtomic_CompareAndExchange(&transfer->state, kTransferContextState_Free, kTransferContextState_InUse)) return transfer;
    }
    return NULL;
}

static void FreeTransfer(TransferContext *transfer) {
    u8 deviceAddress = transfer->deviceAddress;
    u8 endpointNumber = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask;
    DLOG("free.transfer.try", "%u %u", deviceAddress, endpointNumber);
    if (IsTransferRequestValid(deviceAddress, endpointNumber)) {
        // All clear, attempt to release this transfer back to the pool!
        TransferContext *transfer = &s_transferPool[deviceAddress][endpointNumber];
        *transfer = (TransferContext) {}; // Zero all values
        LTAtomic_Store(&transfer->state, kTransferContextState_Free);
        DLOG("free.transfer", "0x%p", transfer);
    }
    // OK to silently fail
}

/*  ______________________________
 *  Control transfer state machine
 *  See USB 2.0 Spec 8.5.3
 *
 *  Control transfers consist of:
 *  1. Setup stage - Establish what kind of request will take place
 *  2. (optional) Data stage - Send/receive any data associated with the request
 *  3. Status stage - Host or device acknowledges control transfer is complete
 */

static void OnControlTransferCb_SetupDone(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSent, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);
    TransferContext *transfer = clientData;

    if (result != kLTDriverUsbHostTransferResult_Success || bytesSent < sizeof(LTUsbDeviceRequest)) {
        // End the state machine if transaction failed or full device request for was not sent. Setup stage payload is ALWAYS a LTUsbDeviceRequest.
        NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Error, bytesSent);
        return;
    }

    // Move on to STATUS stage if there is no data to send/receive
    if (transfer->deviceRequest.wLength == 0) {
        transfer->controlStage = kLTDeviceUsbStd_ControlStage_Status;
    } else {
        // Move on to DATA stage
        if (kLTDeviceUsbStd_RequestType_Dir_DeviceToHost == (transfer->deviceRequest.bmRequestType & kLTDeviceUsbStd_RequestType_Dir_Mask)) {
            // IN
            transfer->controlStage = kLTDeviceUsbStd_ControlStage_DataIn;
        } else {
            // OUT
            transfer->controlStage = kLTDeviceUsbStd_ControlStage_DataOut;
        }
    }
    // Stage decided, advance the state machine
    s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceControlTransfer, NULL, transfer);
}

static void OnControlTransferCb_DataDone(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSent, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);
    TransferContext *transfer = clientData;
    if (result != kLTDriverUsbHostTransferResult_Success || bytesSent == 0) {
        // End the state machine if transaction failed or no payload was sent
        NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Error, bytesSent);
        return;
    }
    // All data sent or received, move on to status stage
    transfer->controlStage = kLTDeviceUsbStd_ControlStage_Status;
    transfer->bytesSentOrReceived = bytesSent;
    s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceControlTransfer, NULL, transfer);
}


static void OnControlTransferCb_StatusDone(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSent, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);
    // For the following notifications, use the DATA stage's bytesSentOrReceived value if available, since the status
    // stage's value will always be 0. The default value of bytesSentOrReceived is also 0.
    TransferContext *transfer = clientData;
    if (result != kLTDriverUsbHostTransferResult_Success || bytesSent > 0) {
        // End the state machine if transaction failed or unexpected bytes were sent. Status stage is ALWAYS a 0-byte payload.
        NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Error, transfer->bytesSentOrReceived);
        return;
    }
    // Control transfer complete! Queue a notification for the client
    NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Complete, transfer->bytesSentOrReceived);
}

static void AdvanceControlTransfer(void *clientData) {
    const u8 controlEndpoint = 0; // Control endpoint is always 0 for both IN and OUT directions
    TransferContext *transfer = clientData;
    DLOG("ctrl", ">>>>>>>>>> control transfer stage: %lu", LT_Pu32(transfer->controlStage));
    switch (transfer->controlStage) {
        case kLTDeviceUsbStd_ControlStage_Setup:
            s_driver->API->TransferSetup(transfer->deviceAddress, &transfer->deviceRequest, OnControlTransferCb_SetupDone, transfer);
            break;
        case kLTDeviceUsbStd_ControlStage_DataIn:
            s_driver->API->TransferIn(transfer->deviceAddress, controlEndpoint, transfer->buf, transfer->bufLen, OnControlTransferCb_DataDone, transfer);
            break;
        case kLTDeviceUsbStd_ControlStage_DataOut:
            s_driver->API->TransferOut(transfer->deviceAddress, controlEndpoint, transfer->buf, transfer->bufLen, OnControlTransferCb_DataDone, transfer);
            break;
        case kLTDeviceUsbStd_ControlStage_Status:
            if (kLTDeviceUsbStd_RequestType_Dir_DeviceToHost == (transfer->deviceRequest.bmRequestType & kLTDeviceUsbStd_RequestType_Dir_Mask)) {
                // Transfer was IN, host needs to send a zero-length DATA1 status packet to device as an acknowledgement
                s_driver->API->TransferOut(transfer->deviceAddress, controlEndpoint, NULL, 0, OnControlTransferCb_StatusDone, transfer);
            } else {
                // Transfer was OUT, host needs to anticipate a zero-length DATA1 status packet from device (device's acknowledgement)
                s_driver->API->TransferIn(transfer->deviceAddress, controlEndpoint, NULL, 0, OnControlTransferCb_StatusDone, transfer);
            }
            break;
        default:
            break;
    }
}

/*  _____________________________
 *  Bus enumeration state machine
 *  See USB 2.0 Spec - 9.1.2 Bus Enumeration
 *
 *  TODO: Support usb hubs. Currently, this logic only supports one device connecting.
 *  TODO: Error handling in all stages of bus enumeration!!
 */

static void BusEnum_HandleReset(BusEnumContext *ctx);
static void BusEnum_HandleGetDeviceDescriptorPartial(BusEnumContext *ctx);
static void BusEnum_OnGetDeviceDescriptorPartialDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleSetAddress(BusEnumContext *ctx);
static void BusEnum_OnSetAddressDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleGetDeviceDescriptorFull(BusEnumContext *ctx);
static void BusEnum_OnGetDeviceDescriptorFullDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleGetConfigDescriptorPartial(BusEnumContext *ctx);
static void BusEnum_OnGetConfigDescriptorPartialDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleGetConfigDescriptorFull(BusEnumContext *ctx);
static void BusEnum_OnGetConfigDescriptorFullDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleSetConfiguration(BusEnumContext *ctx);
static void BusEnum_OnSetConfigurationDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData);
static void BusEnum_HandleComplete(BusEnumContext *ctx);

typedef void (*BusEnumHandler)(BusEnumContext *ctx);
static const BusEnumHandler s_busEnumHandlers[] = {
    [kBusEnumStage_None]                        = NULL,
    [kBusEnumStage_Reset]                       = BusEnum_HandleReset,
    [kBusEnumStage_GetDeviceDescriptor_Partial] = BusEnum_HandleGetDeviceDescriptorPartial,
    [kBusEnumStage_SetAddress]                  = BusEnum_HandleSetAddress,
    [kBusEnumStage_GetDeviceDescriptor_Full]    = BusEnum_HandleGetDeviceDescriptorFull,
    [kBusEnumStage_GetConfigDescriptor_Partial] = BusEnum_HandleGetConfigDescriptorPartial,
    [kBusEnumStage_GetConfigDescriptor_Full]    = BusEnum_HandleGetConfigDescriptorFull,
    [kBusEnumStage_SetConfiguration]            = BusEnum_HandleSetConfiguration,
    [kBusEnumStage_Complete]                    = BusEnum_HandleComplete,
};

static void AdvanceBusEnumeration(void *clientData) {
    BusEnumContext *ctx = clientData;
    DLOG("busenum", ">>>>>>>>>>>>>>>>>>>>>>> bus enum stage: %lu", LT_Pu32(ctx->stage));

    // Call the handler associated with the enum
    if (ctx->stage < kBusEnumStage_nStages && s_busEnumHandlers[ctx->stage] != NULL) {
        s_busEnumHandlers[ctx->stage](ctx);
    } else {
        LTLOG_YELLOWALERT("busenum.invalid", "Invalid bus enumeration stage: %u", ctx->stage);
    }
}

static void BusEnum_GoToStage(BusEnumStage stage, BusEnumContext *context) {
    s_busEnumContext.stage = stage;
    s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceBusEnumeration, NULL, context);
}

/* _____________________________
 * Bus enumeration state machine
 * <Entry point>
 */

static void LT_ISR_SAFE OnDeviceAttachedOrDetachedCb(bool attached, u8 deviceAddress, void *clientData) {
    /* See usb_bus_enum in Anyka Sky SDK v1.04 */
    LT_UNUSED(clientData);
    if (attached) {
        DLOG("attach", NULL);
        // TODO: When >1 downstream USB device support is needed, read port status from hub class before moving on to reset stage
        BusEnum_GoToStage(kBusEnumStage_Reset, &s_busEnumContext);
    } else {
        // Remove device and any dependent devices after notifying clients
        s_hostThread->API->QueueTaskProc(s_hostThread, DisconnectedProc, NULL, (void *)(LT_SIZE)deviceAddress);
    }
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_Reset
 */

static void BusEnum_HandleReset(BusEnumContext *ctx) {
    s_driver->API->ResetBus();
    BusEnum_GoToStage(kBusEnumStage_GetDeviceDescriptor_Partial, ctx);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_GetDeviceDescriptor_Partial
 */

static void BusEnum_HandleGetDeviceDescriptorPartial(BusEnumContext *ctx) {
    // Prepare storage
    u8 nextAvailableAddress = GetNextAvailableTopologyAddress();
    DLOG("next.addr", "addr %u reserved for this new device", nextAvailableAddress);
    if (nextAvailableAddress > 0) {
        ctx->device = &s_topology[nextAvailableAddress];
        ctx->device->address = nextAvailableAddress;
    } else {
        LTLOG_YELLOWALERT("addr.none", NULL);
        // TODO: Error. No device addresses available, notify client
    }

    // Assume max packet size of control endpoint for USB 2.0 high speed, real packet size will be smaller if using lower USB speed
    ctx->device->deviceDescriptor = lt_malloc(kMaxControlEndpointHighSpeedSize);
    if (!ctx->device->deviceDescriptor) {
        LTLOG_YELLOWALERT("desc.oom", NULL);
        // TODO: Error. Alloc failed, notify client
    }

    // New devices are always at address 0 until address is set by the host. Get the descriptor!
    bool success = s_stdReq->API->GetDescriptor(s_stdReq,
                                                kDeviceAddressNew,
                                                kLTDeviceUsbStd_DescriptorType_Device,
                                                0,
                                                (u8 *)ctx->device->deviceDescriptor,
                                                kMaxControlEndpointHighSpeedSize,
                                                BusEnum_OnGetDeviceDescriptorPartialDone,
                                                ctx,
                                                NULL);

    if (!success) {
        LTLOG_YELLOWALERT("req.getdesc.fail", NULL);
        // TODO: Error. Alloc failed, notify client
    }
}

static void BusEnum_OnGetDeviceDescriptorPartialDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    DLOG("get.descpart.done", NULL);
    BusEnumContext *ctx = clientData;
    LTUsbDeviceDescriptor *descriptor = (LTUsbDeviceDescriptor *)buf;

    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < sizeof(LTUsbUnknownDescriptor)) {
        // Transfer failed or result is too short to be valid
        LTLOG_YELLOWALERT("get.descpart.fail", "bytesReceived %lu", LT_Pu32(bytesSentOrReceived));
        // TODO: Error, notify client
    }

    // Partial descriptor read has informed us what the max packet size of the control endpoint is and length of the descriptor
    s_driver->API->ConfigureControlEndpoint(kDeviceAddressNew, descriptor->bMaxPacketSize0);

    // Ensure descriptor buffer is large enough to hold the total length of the descriptor
    if (bufLen < descriptor->bLength) {
        ctx->device->deviceDescriptor = lt_realloc(ctx->device->deviceDescriptor, descriptor->bLength);
        if (!ctx->device->deviceDescriptor) {
            LTLOG_YELLOWALERT("desc.re.oom", NULL);
            // TODO: Error. Alloc failed, notify client
        }
    }

    // Move on to next stage
    BusEnum_GoToStage(kBusEnumStage_SetAddress, ctx);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_SetAddress
 */

static void BusEnum_HandleSetAddress(BusEnumContext *ctx) {
    bool success = s_stdReq->API->SetAddress(s_stdReq, kDeviceAddressNew, ctx->device->address, BusEnum_OnSetAddressDone, ctx, NULL);
    if (!success) {
        LTLOG_YELLOWALERT("req.setaddr.fail", NULL);
        // TODO: Error. notify client
    }
}

static void BusEnum_OnSetAddressDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    DLOG("set.addr.done", NULL);
    LT_UNUSED(buf);
    LT_UNUSED(bufLen);
    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < bufLen) {
        // Transfer failed or full request payload was not sent
        LTLOG_YELLOWALERT("set.addr.fail", NULL);
        // TODO: Error, notify client
    }
    BusEnum_GoToStage(kBusEnumStage_GetDeviceDescriptor_Full, clientData);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_GetDeviceDescriptor_Full
 */

static void BusEnum_HandleGetDeviceDescriptorFull(BusEnumContext *ctx) {
    // Get device descriptor again, this time with the correct size and address
    bool success = s_stdReq->API->GetDescriptor(s_stdReq,
                                                ctx->device->address,
                                                kLTDeviceUsbStd_DescriptorType_Device,
                                                0,
                                                (u8 *)ctx->device->deviceDescriptor,
                                                ctx->device->deviceDescriptor->bLength,
                                                BusEnum_OnGetDeviceDescriptorFullDone,
                                                ctx,
                                                NULL);
    if (!success) {
        LTLOG_YELLOWALERT("req.getdescfull.fail", NULL);
        // TODO: Error. notify client
    }
}

static void BusEnum_OnGetDeviceDescriptorFullDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(bufLen);
    DLOG("get.descfull.done", NULL);
    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < bufLen) {
        // Transfer failed or full descriptor was not received
        LTLOG_YELLOWALERT("get.descfull.fail", NULL);
        // TODO: Error, notify client
    }
    PrintDescriptor(kLTDeviceUsbStd_DescriptorType_Device, buf);
    BusEnum_GoToStage(kBusEnumStage_GetConfigDescriptor_Partial, clientData);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_GetConfigDescriptor_Partial
 */

static void BusEnum_HandleGetConfigDescriptorPartial(BusEnumContext *ctx) {
    // TODO: Consider reading all available configuration descriptors before selecting one of them. For now, read and select only the first one.

    // Get the first configuration descriptor
    ctx->device->configDescriptor = lt_malloc(sizeof(LTUsbConfigDescriptor));
    if (!ctx->device->configDescriptor) {
        LTLOG_YELLOWALERT("cfg.init.oom", NULL);
        // TODO: Error. Alloc failed, notify client
    }
    bool success = s_stdReq->API->GetDescriptor(s_stdReq,
                                                ctx->device->address,
                                                kLTDeviceUsbStd_DescriptorType_Config,
                                                0,
                                                (u8 *)ctx->device->configDescriptor,
                                                sizeof(LTUsbConfigDescriptor),
                                                BusEnum_OnGetConfigDescriptorPartialDone,
                                                ctx,
                                                NULL);
    if (!success) {
        LTLOG_YELLOWALERT("req.getcfg.fail", NULL);
        // TODO: Error. notify client
    }
}

static void BusEnum_OnGetConfigDescriptorPartialDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    DLOG("get.configpart.done", NULL);
    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < bufLen) {
        // Transfer failed or full descriptor was not received
        LTLOG_YELLOWALERT("get.configpart.fail", NULL);
        // TODO: Error, notify client
    }
    LTUsbConfigDescriptor *configDescriptor = (LTUsbConfigDescriptor *)buf;
    DLOG("get.configpart.wTotalLength", "%lu", LT_Pu32(configDescriptor->wTotalLength));
    BusEnum_GoToStage(kBusEnumStage_GetConfigDescriptor_Full, clientData);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_GetConfigDescriptor_Full
 */

static void BusEnum_HandleGetConfigDescriptorFull(BusEnumContext *ctx) {
    // Get full config descriptor now that total length is known
    u16 wTotalLength = ctx->device->configDescriptor->wTotalLength;
    ctx->device->configDescriptor = lt_realloc(ctx->device->configDescriptor, wTotalLength);
    if (!ctx->device->configDescriptor) {
        LTLOG_YELLOWALERT("cfg.full.oom", NULL);
        // TODO: Error. Alloc failed, notify client
    }
    bool success = s_stdReq->API->GetDescriptor(s_stdReq,
                                                ctx->device->address,
                                                kLTDeviceUsbStd_DescriptorType_Config,
                                                0,
                                                (u8 *)ctx->device->configDescriptor,
                                                wTotalLength,
                                                BusEnum_OnGetConfigDescriptorFullDone,
                                                ctx,
                                                NULL);
    if (!success) {
        LTLOG_YELLOWALERT("req.getcfgfull.fail", NULL);
        // TODO: Error. notify client
    }
}

static bool BusEnum_ProcessDescriptors(LTDeviceUsbStd_DescriptorType descriptorType, const LTUsbUnknownDescriptor *descriptor, void *clientData) {
    BusEnumContext *ctx = clientData;
    // Print every descriptor from the new device
    PrintDescriptor(descriptorType, (u8 *)descriptor);
    switch (descriptorType) {
        case kLTDeviceUsbStd_DescriptorType_Endpoint:
            // Configure the driver to handle each endpoint of the new device
            s_driver->API->ConfigureEndpoint(ctx->device->address, (LTUsbEndpointDescriptor *)(descriptor));
            break;
        default:
            break;
    }
    return true;
}

static void BusEnum_OnGetConfigDescriptorFullDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    /* USB 2.0 Spec 9.4.3
     * "A request for a configuration descriptor returns the configuration descriptor, all interface descriptors, and
     * endpoint descriptors for all of the interfaces in a single request. The first interface descriptor follows the
     * configuration descriptor. The endpoint descriptors for the first interface follow the first interface descriptor.
     * If there are additional interfaces, their interface descriptor and endpoint descriptors follow the first
     * interface’s endpoint descriptors. "
     */
    DLOG("get.configfull.done", NULL);
    BusEnumContext *ctx = clientData;
    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < bufLen) {
        // Transfer failed or full descriptor was not received
        LTLOG_YELLOWALERT("get.configfull.fail", NULL);
        // TODO: Error, notify client
    }

    // First, read the configuration descriptor
    LTUsbConfigDescriptor *configDescriptor = (LTUsbConfigDescriptor *)buf;
    PrintDescriptor(kLTDeviceUsbStd_DescriptorType_Config, buf);

    // Go through all descriptors and configure the driver
    LTDeviceUsbHostImpl_EnumerateConfigDescriptor(configDescriptor, BusEnum_ProcessDescriptors, ctx);

    BusEnum_GoToStage(kBusEnumStage_SetConfiguration, clientData);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_SetConfiguration
 */

static void BusEnum_HandleSetConfiguration(BusEnumContext *ctx) {
    // Set the configuration for the first configuration descriptor
    bool success = s_stdReq->API->SetConfiguration(s_stdReq,
                                                   ctx->device->address,
                                                   ctx->device->configDescriptor->bConfigurationValue,
                                                   BusEnum_OnSetConfigurationDone,
                                                   ctx,
                                                   NULL);
    if (!success) {
        LTLOG_YELLOWALERT("req.setcfg.fail", NULL);
        // TODO: Error. notify client
    }
}

static void BusEnum_OnSetConfigurationDone(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(buf);
    DLOG("set.config.done", NULL);
    if (result != kLTDeviceUsbHostTransferResult_Complete || bytesSentOrReceived < bufLen) {
        // Transfer failed or full payload was not sent
        LTLOG_YELLOWALERT("set.config.fail", NULL);
        // TODO: Error, notify client
    }
    BusEnum_GoToStage(kBusEnumStage_Complete, clientData);
}

/* _____________________________
 * Bus enumeration state machine
 * kBusEnumStage_Complete
 */

static void BusEnum_HandleComplete(BusEnumContext *ctx) {
    // Enumeration done, mark device as done in topology
    AddDeviceToTopology(ctx->device->address);

    // Notify to clients that a new device is connected
    NotifyDeviceStatusEvent(ctx->device->address, true);

    // Clear bus enum context
    s_busEnumContext = (BusEnumContext){};
}

/*  ____________________________
 *  Bulk transfer implementation
 *  See USB 2.0 Spec 8.5.2
 */

static void OnBulkTransferCb_Done(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);

    TransferContext *transfer = clientData;

    // Convert driver result to device result
    LTDeviceUsbHostTransferResult deviceResult = kLTDeviceUsbHostTransferResult_Complete;
    if (result != kLTDriverUsbHostTransferResult_Success) {
        deviceResult = kLTDeviceUsbHostTransferResult_Error;
    }

    DLOG("bulk.done", "result=%u bytes=%u", deviceResult, bytesSentOrReceived);

    // Notify the client that the bulk transfer is complete
    NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Complete, bytesSentOrReceived);
}

static void AdvanceBulkTransfer(void *clientData) {
    TransferContext *transfer = clientData;

    DLOG("bulk.start",
         "addr=%u ep=0x%02x len=%u",
         transfer->deviceAddress,
         transfer->endpointAddress,
         transfer->bufLen);

    // Parse direction and number from address
    u8 endpointDir    = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Dir_Mask;
    u8 endpointNumber = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask;

    if (endpointDir == kLTDeviceUsbStd_EndpointAddress_Dir_In) {
        // Bulk IN transfer - receiving data from device
        s_driver->API->TransferIn(transfer->deviceAddress,
                                  endpointNumber,
                                  transfer->buf,
                                  transfer->bufLen,
                                  OnBulkTransferCb_Done,
                                  transfer);
    } else {
        // Bulk OUT transfer - sending data to device
        s_driver->API->TransferOut(transfer->deviceAddress,
                                   endpointNumber,
                                   transfer->buf,
                                   transfer->bufLen,
                                   OnBulkTransferCb_Done,
                                   transfer);
    }
}

/*  _______________________________
 *  Interrupt transfer implementation
 *  See USB 2.0 Spec 8.5.4
 */

static void OnInterruptTransferCb_Done(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);

    TransferContext *transfer = clientData;

    // Convert driver result to device result
    LTDeviceUsbHostTransferResult deviceResult = kLTDeviceUsbHostTransferResult_Complete;
    if (result != kLTDriverUsbHostTransferResult_Success) {
        deviceResult = kLTDeviceUsbHostTransferResult_Error;
    }

    DLOG("int.done", "result=%u bytes=%u", deviceResult, bytesSentOrReceived);

    // Notify the client that the interrupt transfer is complete
    NotifyCallerTransferComplete(transfer, kLTDeviceUsbHostTransferResult_Complete, bytesSentOrReceived);
}

static void AdvanceInterruptTransfer(void *clientData) {
    TransferContext *transfer = clientData;

    DLOG("int.start",
         "addr=%u ep=0x%02x len=%u",
         transfer->deviceAddress,
         transfer->endpointAddress,
         transfer->bufLen);

    // Parse direction and number from address
    u8 endpointDir    = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Dir_Mask;
    u8 endpointNumber = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask;

    // TODO: Handle SOF-timing/bandwidth considerations here?

    if (endpointDir == kLTDeviceUsbStd_EndpointAddress_Dir_In) {
        // Interrupt IN transfer - poll for data from device
        // The driver will automatically handle polling until data is available
        s_driver->API->TransferIn(transfer->deviceAddress,
                                  endpointNumber,
                                  transfer->buf,
                                  transfer->bufLen,
                                  OnInterruptTransferCb_Done,
                                  transfer);
    } else {
        // Interrupt OUT transfer - send data to device
        // TODO: The driver will handle timing according to endpoint's bInterval?
        s_driver->API->TransferOut(transfer->deviceAddress,
                                   endpointNumber,
                                   transfer->buf,
                                   transfer->bufLen,
                                   OnInterruptTransferCb_Done,
                                   transfer);
    }
}

/*  __________________________________
 *  Isochronous transfer implementation
 *  See USB 2.0 Spec 8.5.5
 */

static void OnIsochronousTransferCb_Done(u8 deviceAddress, u8 endpointNumber, LTDriverUsbHostTransferResult result, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(deviceAddress);
    LT_UNUSED(endpointNumber);

    TransferContext *transfer = clientData;

    // Convert driver result to device result
    LTDeviceUsbHostTransferResult deviceResult = kLTDeviceUsbHostTransferResult_Complete;
    if (result != kLTDriverUsbHostTransferResult_Success) {
        deviceResult = kLTDeviceUsbHostTransferResult_Error;
    }

    DLOG("iso.done", "result=%u bytes=%u", deviceResult, bytesSentOrReceived);

    // Notify the client that the isochronous transfer is complete
    NotifyCallerTransferComplete(transfer,kLTDeviceUsbHostTransferResult_Complete, bytesSentOrReceived);
}

static void AdvanceIsochronousTransfer(void *clientData) {
    TransferContext *transfer = clientData;

    DLOG("iso.start",
         "addr=%u ep=0x%02x len=%u",
         transfer->deviceAddress,
         transfer->endpointAddress,
         transfer->bufLen);

    // Parse direction and number from address
    u8 endpointDir    = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Dir_Mask;
    u8 endpointNumber = transfer->endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask;

    // TODO: Handle SOF-timing/bandwidth considerations here?

    if (endpointDir == kLTDeviceUsbStd_EndpointAddress_Dir_In) {
        // Isochronous IN transfer - receive streaming data from device
        // The callback will be called as soon as any data is available
        s_driver->API->TransferIn(transfer->deviceAddress,
                                  endpointNumber,
                                  transfer->buf,
                                  transfer->bufLen,
                                  OnIsochronousTransferCb_Done,
                                  transfer);
    } else {
        // Isochronous OUT transfer - send streaming data to device
        // TODO: The driver will handle timing according to endpoint's bInterval?
        // Data may be split across multiple intervals if needed
        s_driver->API->TransferOut(transfer->deviceAddress,
                                   endpointNumber,
                                   transfer->buf,
                                   transfer->bufLen,
                                   OnIsochronousTransferCb_Done,
                                   transfer);
    }
}

/*  __________________
 *  Object public API
 */

static bool LTDeviceUsbHostImpl_ControlTransfer(u8 deviceAddress, LTUsbDeviceRequest *request, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    DLOG("ctrl.transfer", NULL);

    // Acquire a transfer struct to keep track of control transfer between stages
    TransferContext *transfer = MallocTransfer(deviceAddress, kControlEndpoint);
    if (transfer) {
        transfer->callerThread         = LT_GetCore()->GetCurrentThreadObject();
        transfer->deviceAddress        = deviceAddress;
        transfer->deviceRequest        = *request;
        transfer->buf                  = buf;
        transfer->bufLen               = bufLen;
        transfer->completeCb           = completeCb;
        transfer->completeCbClientData = clientData;
        transfer->synchronous          = synchronousModeTimeout != NULL;

        // Start the control transfer state machine!
        DLOG("ctrl.transfer.queued", "0x%p", transfer);
        transfer->controlStage = kLTDeviceUsbStd_ControlStage_Setup;
        s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceControlTransfer, NULL, transfer);

        bool success = true;
        // Block and wait for result if needed
        if (transfer->synchronous) {
            success = WaitForTransferComplete(transfer, synchronousModeTimeout);
        }
        return success;
    }

    return false;
}

static bool LTDeviceUsbHostImpl_BulkTransfer(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    TransferContext *transfer = MallocTransfer(deviceAddress, endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask);
    if (transfer) {
        transfer->callerThread         = LT_GetCore()->GetCurrentThreadObject();
        transfer->deviceAddress        = deviceAddress;
        transfer->endpointAddress      = endpointAddress;
        transfer->buf                  = buf;
        transfer->bufLen               = bufLen;
        transfer->completeCb           = completeCb;
        transfer->completeCbClientData = clientData;
        transfer->synchronous          = synchronousModeTimeout != NULL;

        s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceBulkTransfer, NULL, transfer);

        bool success = true;
        // Block and wait for result if needed
        if (transfer->synchronous) {
            success = WaitForTransferComplete(transfer, synchronousModeTimeout);
        }
        return success;
    }
    return false;
}

static bool LTDeviceUsbHostImpl_InterruptTransfer(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    TransferContext *transfer = MallocTransfer(deviceAddress, endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask);
    if (transfer) {
        transfer->callerThread         = LT_GetCore()->GetCurrentThreadObject();
        transfer->deviceAddress        = deviceAddress;
        transfer->endpointAddress      = endpointAddress;
        transfer->buf                  = buf;
        transfer->bufLen               = bufLen;
        transfer->completeCb           = completeCb;
        transfer->completeCbClientData = clientData;
        transfer->synchronous          = synchronousModeTimeout != NULL;

        s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceInterruptTransfer, NULL, transfer);

        bool success = true;
        // Block and wait for result if needed
        if (transfer->synchronous) {
            success = WaitForTransferComplete(transfer, synchronousModeTimeout);
        }
        return success;
    }
    return false;
}

static bool LTDeviceUsbHostImpl_IsochronousTransfer(u8 deviceAddress, LTDeviceUsbStd_EndpointAddress endpointAddress, u8 *buf, u32 bufLen, LTDeviceUsbHost_OnTransferCompleteCb *completeCb, void *clientData, const LTTime *synchronousModeTimeout) {
    TransferContext *transfer = MallocTransfer(deviceAddress, endpointAddress & kLTDeviceUsbStd_EndpointAddress_Number_Mask);
    if (transfer) {
        transfer->callerThread         = LT_GetCore()->GetCurrentThreadObject();
        transfer->deviceAddress        = deviceAddress;
        transfer->endpointAddress      = endpointAddress;
        transfer->buf                  = buf;
        transfer->bufLen               = bufLen;
        transfer->completeCb           = completeCb;
        transfer->completeCbClientData = clientData;
        transfer->synchronous          = synchronousModeTimeout != NULL;

        s_hostThread->API->QueueTaskProc(s_hostThread, AdvanceIsochronousTransfer, NULL, transfer);

        bool success = true;
        // Block and wait for result if needed
        if (transfer->synchronous) {
            success = WaitForTransferComplete(transfer, synchronousModeTimeout);
        }
        return success;
    }
    return false;
}

static void LTDeviceUsbHostImpl_OnDeviceStatus(LTDeviceUsbHost_OnDeviceStatus *onDeviceStatus, bool notifyCurrentlyConnected, void *clientData) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hDeviceStatusEvent);
    iEvent->RegisterForEvent(s_hDeviceStatusEvent, onDeviceStatus, NULL, clientData, notifyCurrentlyConnected);
}

static void LTDeviceUsbHostImpl_NoDeviceStatus(LTDeviceUsbHost_OnDeviceStatus *onDeviceStatus) {
    ILTEvent *iEvent = lt_gethandleinterface(ILTEvent, s_hDeviceStatusEvent);
    iEvent->UnregisterFromEvent(s_hDeviceStatusEvent, onDeviceStatus);
}

static bool LTDeviceUsbHostImpl_EnumerateConfigDescriptor(const LTUsbConfigDescriptor *configDescriptor, LTDeviceUsbHost_EnumerateConfigCb *callback, void *clientData) {
    bool enumerationDone = true;

    // Other descriptors start after the end of the config descriptors
    u32 offset = configDescriptor->bLength;

    // Go through descriptors one by one
    while (offset < configDescriptor->wTotalLength) {
        // Parse and validate common descriptor header
        const u8 descriptorHeaderLength = 2;
        LTUsbUnknownDescriptor *currentDescriptor = (LTUsbUnknownDescriptor*)((u8 *)configDescriptor + offset);
        if (offset + descriptorHeaderLength > configDescriptor->wTotalLength) {
            LTLOG_YELLOWALERT("desc.parse.hdr.invalid", NULL);
            enumerationDone = false;
            break;
        }
        u8 descriptorLength = currentDescriptor->bLength;
        u8 descriptorType   = currentDescriptor->bDescriptorType;

        // Validate descriptor length
        if (descriptorLength < descriptorHeaderLength || offset + descriptorLength > configDescriptor->wTotalLength) {
            LTLOG_YELLOWALERT("desc.parse.error", "Invalid descriptor length %u at offset %lu", descriptorLength, LT_Pu32(offset));
            enumerationDone = false;
            break;
        }

        // Descriptor found, notify via callback
        DLOG("desc.parse", "Type: %u, Length: %u, Offset: %u", descriptorType, descriptorLength, offset);
        bool continueEnumeration = callback(descriptorType, currentDescriptor, clientData);
        if (!continueEnumeration) {
            enumerationDone = false;
            break;
        }

        // Continue to next descriptor
        offset += descriptorLength;
    }

    return enumerationDone;
}

/*  __________________________________
 *  Object constructor and destructor
 */

static bool LTDeviceUsbHostImpl_ConstructObject(LTDeviceUsbHostImpl *usbHost) {
    LT_UNUSED(usbHost);
    return true;
}

static void LTDeviceUsbHostImpl_DestructObject(LTDeviceUsbHostImpl *usbHost) {
    LT_UNUSED(usbHost);
}

/*  __________________________________
 *  Library init and fini
 */

static void OnBusModeChange(bool acquired, void *clientData) {
    LT_UNUSED(clientData);
    if (acquired) {
        s_driver->API->Enable(s_hostThread);
    } else {
        s_driver->API->Disable();
    }
}

static bool ThreadInit(void) {
    // Register for when USB devices are attached/detached
    s_driver->API->SetOnDeviceAttachedOrDetached(OnDeviceAttachedOrDetachedCb, NULL);

    // Reserve the USB bus to host mode, if possible
    s_usbManager = lt_createobject(LTSystemUsbBusManager);
    if (s_usbManager) {
        s_usbManager->API->OnModeChange("host", OnBusModeChange, NULL);
        s_usbManager->API->ChangeMode("host");
    } else {
        // No bus manager on system, directly enable USB driver in host mode
        s_driver->API->Enable(s_hostThread);
    }

    return true;
}

static void ThreadFini(void) {
    lt_destroyobject(s_usbManager);
    s_usbManager = NULL;
}

static void LTDeviceUsbHost_LibFini(void) {
    if (s_driver) s_driver->API->Disable();
    lt_destroyobject(s_driver);     s_driver = NULL;
    lt_destroyobject(s_stdReq);     s_stdReq = NULL;
    lt_destroyhandle(s_hDeviceStatusEvent); s_hDeviceStatusEvent = LTHANDLE_INVALID;
    lt_destroyobject(s_hostThread); s_hostThread = NULL;
    s_busEnumContext = (BusEnumContext){};
    for (u8 i = 0; i < kMaxConnectedDevices; i++) {
        s_topology[i] = (ConnectedDevice){};
    }
}

static bool LTDeviceUsbHost_LibInit(void) {
    const char *driverName = NULL;
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    do {
        // Obtain the driver specialization name
        if (!deviceConfig) {
            LTLOG_YELLOWALERT("f.devc.open", "could not open device config");
            break;
        }
        driverName = deviceConfig->GetDriverAt("LTDeviceUsbHost", 0);
        if (!driverName) {
            LTLOG_YELLOWALERT("f.nodrv", "no driver name found in device config");
            break;
        }

        // Create the driver
        if (NULL == (s_driver = (LTDriverUsbHost *)lt_createobject_named("LTDriverUsbHost", driverName))) {
            LTLOG_YELLOWALERT("f.create.driver.object", "failed to create LTDriverUsbHost object %s", driverName);
            break;
        }

        // Other initialization
        s_stdReq = lt_createobject(LTDeviceUsbHostStandardRequest);
        if (!s_stdReq) {
            LTLOG_YELLOWALERT("f.create.stdreq", NULL);
            break;
        }

        s_hDeviceStatusEvent = LT_GetCore()->CreateEvent(&s_DeviceStatusEventArgs, DispatchDeviceStatusProc, DispatchDeviceStatusCompleteProc, DispatchDeviceStatusImmediateProc, NULL);
        if (!s_hDeviceStatusEvent) {
            LTLOG_YELLOWALERT("f.create.evt", NULL);
            break;
        }

        for (u8 i = 0; i < kMaxConnectedDevices; i++) {
            s_topology[i] = (ConnectedDevice){
                .enumerated = false,
            };
        }

        // Finally, initialize device host thread and kick off driver initialization
        s_hostThread = lt_createobject(LTOThread);
        s_hostThread->API->SetStackSize(s_hostThread, 2048); // TODO: Tune stack size
        s_hostThread->API->Start(s_hostThread, "usbhost", ThreadInit, ThreadFini);

        return true;
    } while (false);

    lt_closelibrary(deviceConfig);
    LTDeviceUsbHost_LibFini();
    return false;
}

/*  __________________________________
 *  Object API definition and library root interface binding
 */

define_LTObjectImplPublic(LTDeviceUsbHost, LTDeviceUsbHostImpl,
    ControlTransfer,
    BulkTransfer,
    InterruptTransfer,
    IsochronousTransfer,
    OnDeviceStatus,
    NoDeviceStatus,
    EnumerateConfigDescriptor,
);

define_LTObjectLibrary(1, LTDeviceUsbHost_LibInit, LTDeviceUsbHost_LibFini);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Jul-25   aurelian    created
 */

