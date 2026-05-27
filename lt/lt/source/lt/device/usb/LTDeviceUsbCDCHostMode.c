/*******************************************************************************
 * source/lt/device/usb/LTDeviceUsbCDCHostMode.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/usb/LTDeviceUsbCDC.h>
#include <lt/device/usb/LTDeviceUsbCDCStd.h>
#include <lt/device/usb/LTDeviceUsbStd.h>
#include <lt/device/usb/host/LTDeviceUsbHost.h>

#define USBHOSTCDC_DO_LOG     0
#if     USBHOSTCDC_DO_LOG
#define DLOG                        LTLOG
#else
#define DLOG                        LTLOG_LOGNULL
#endif

DEFINE_LTLOG_SECTION("usbcdc.host");

typedef struct LTDeviceUsbCDCHostMode LTDeviceUsbCDCHostMode;

typedef struct {
    s32 result;
    LTDeviceUsbCDCHostMode *device;
} TransferCbData;

typedef struct {
    bool                    complete;        /* True when CDC data below is valid and up to date */
    u8                      deviceAddress;   /* Address of the device with the CDC function to communicate with. When deviceAddress is 0, it means no device is connected */
    u8                      interfaceNumber; /* Interface number of the CDC interface on the device */
    LTUsbEndpointDescriptor endpointACM;     /* Interrupt IN endpoint, for notifications on control/status changes (host initiated and polled) */
    LTUsbEndpointDescriptor endpointBulkIn;  /* Bulk IN endpoint, for RX (host initiated and polled) */
    LTUsbEndpointDescriptor endpointBulkOut; /* Bulk OUT endpoint, for TX (host initiated) */

    bool inCommInterface; /* Only used while parsing cdc descriptor */
    bool inDataInterface; /* Only used while parsing cdc descriptor */
} CdcEndpointData;

/*______________________________
  Object private data members */

static LTDeviceUsbCDCLineCoding s_defaultLineCoding = {
    .dwDTERate   = 115200, /* Data terminal rate */
    .bCharFormat = 0,      /* 0 == 1 stop bit */
    .bParityType = 0,      /* 0 == None */
    .bDataBits   = 8,      /* 8 data bits */
};

typedef_LTObjectImpl(LTDeviceUsbCDC, LTDeviceUsbCDCHostMode) {
    LTDeviceUsbHost   *usbHost;            /* Initialized in Construct and never modified until destruct; no mutex protection needed */

    LTMutex           *mutex;              /* All member values are mutex protected since they may be accessed by the caller's thread or this library's thread */
    LTOThread         *thread;             /* Callback task procs queued to this thread */
    const char        *portName;           /* String */
    LTThread_TaskProc *readReadyCallback;  /* Bulk IN polling indicates when downstream device has data to send to host */
    LTThread_TaskProc *writeReadyCallback; /* Bulk OUT is always ready since it is host-initiated transfer */
    LTThread_TaskProc *errorCallback;      /* For notifying any _non-recoverable_ error */
    void              *clientData;

    LTOThread      *hostModeThread;              /* Used to detect when a device is connected/disconnected and polls for read data from device */
    u8             *readBuffer;                  /* Buffer to hold the available read data. This is the data clients receive when they call Read() after a read ready notification.
                                                    This buffer is created when a valid CDC device is first connected, and destroyed when a CDC device is disconnected or Stop() is called. */
    u32             readBufferBytesReadByClient; /* Number of bytes client has read so far out of the read buffer. More data will be polled into the buffer once bytesRead == readBufferSize */
    u32             readBufferBytesAvailable;    /* Number of valid bytes available to read from readBuffer */
    bool            started;                     /* True when Start() has been called */
    CdcEndpointData cdcData;                     /* Contains all information needed to communicate with a connected CDC function */
}
LTOBJECT_API;

static const LTTime     kUsbTransferTimeout = LTTimeInitializer_Seconds(2);

/*_______________________
  Forward declarations */

static void LTDeviceUsbCDCHostMode_Stop(LTDeviceUsbCDCHostMode *device);
static void StartPollingForReadData(LTDeviceUsbCDCHostMode *device);
static void HostModeThread_OnUsbHostDeviceStatus(u8 deviceAddress, bool connected, const LTUsbConfigDescriptor *config, void *clientData);
static void HostModeThread_OnDisconnect(LTDeviceUsbCDCHostMode *device);
static bool HostModeThread_EnumerateConfigForCdcInterfaceCb(LTDeviceUsbStd_DescriptorType descriptorType, const LTUsbUnknownDescriptor *descriptor, void *clientData);
static void HostModeThread_InitializeCdcProc(void *clientData);

/*________________________
  Private API functions */

/* Precondition: device->mutex is held */
static void NotifyToClientReadReady(LTDeviceUsbCDCHostMode *device) {
    if (device->readReadyCallback) {
        device->thread->API->QueueTaskProc(device->thread, device->readReadyCallback, NULL, device->clientData);
    }
}

/* Precondition: device->mutex is held */
static void NotifyToClientWriteReady(LTDeviceUsbCDCHostMode *device) {
    if (device->writeReadyCallback) {
        device->thread->API->QueueTaskProc(device->thread, device->writeReadyCallback, NULL, device->clientData);
    }
}

/* Precondition: device->mutex is held */
static void NotifyToClientFatalError(LTDeviceUsbCDCHostMode *device) {
    if (device->errorCallback) {
        device->thread->API->QueueTaskProc(device->thread, device->errorCallback, NULL, device->clientData);
    }
}

/* Synchronously called during SetLineCoding */
static void OnSetLineCodingComplete(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(buf);
    LT_UNUSED(bufLen);
    LT_UNUSED(bytesSentOrReceived);

    TransferCbData *cbData = clientData;

    if (result != kLTDeviceUsbHostTransferResult_Complete) {
        LTLOG_YELLOWALERT("setlinecoding.error", NULL);
        cbData->result = false;
        return;
    }

    DLOG("setlinecoding.done", NULL);
}

/* Precondition: device->mutex is held */
static bool SetLineCoding(LTDeviceUsbCDCHostMode *device, LTDeviceUsbCDCLineCoding *lineCoding) {
    DLOG("set.line.coding", NULL);
    LTUsbDeviceRequest request = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_HostToDevice
                       | kLTDeviceUsbStd_RequestType_Type_Class
                       | kLTDeviceUsbStd_RequestType_Recipient_Interface,
        .bRequest = kLTDeviceUsbCDCStd_Request_SetLineCoding,
        .wValue   = 0,
        .wIndex   = device->cdcData.interfaceNumber,
        .wLength  = sizeof(LTDeviceUsbCDCLineCoding)
    };

    TransferCbData cbData = {
        .result = true,
        .device = device,
    };
    device->usbHost->API->ControlTransfer(device->cdcData.deviceAddress, &request, (u8 *)lineCoding, sizeof(LTDeviceUsbCDCLineCoding), OnSetLineCodingComplete, &cbData, &kUsbTransferTimeout);
    return cbData.result;
}

/* Synchronously called during SetControlLineState */
static void OnSetControlLineStateComplete(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(buf);
    LT_UNUSED(bufLen);
    LT_UNUSED(bytesSentOrReceived);

    TransferCbData *cbData = clientData;

    if (result != kLTDeviceUsbHostTransferResult_Complete) {
        LTLOG_YELLOWALERT("set.ctrl.line.error", NULL);
        cbData->result = false;
        return;
    }

    DLOG("set.ctrl.line.done", NULL);
}

/* Precondition: device->mutex is held */
static bool SetControlLineState(LTDeviceUsbCDCHostMode *device, u16 wValue) {
    DLOG("set.ctrl.line", NULL);
    LTUsbDeviceRequest request = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_HostToDevice
                       | kLTDeviceUsbStd_RequestType_Type_Class
                       | kLTDeviceUsbStd_RequestType_Recipient_Interface,
        .bRequest = kLTDeviceUsbCDCStd_Request_SetControlLineState,
        .wValue   = wValue,
        .wIndex   = device->cdcData.interfaceNumber,
        .wLength  = 0
    };

    TransferCbData cbData = {
        .result = true,
        .device = device,
    };
    device->usbHost->API->ControlTransfer(device->cdcData.deviceAddress, &request, NULL, 0, OnSetControlLineStateComplete, &cbData, &kUsbTransferTimeout);
    return cbData.result;
}

/* Synchronously called during OnGetLineCodingComplete */
static void OnGetLineCodingComplete(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(buf);
    LT_UNUSED(bufLen);

    TransferCbData *cbData = clientData;

    if (result != kLTDeviceUsbHostTransferResult_Complete) {
        LTLOG_YELLOWALERT("getlinecoding.error", NULL);
        cbData->result = false;
        return;
    }

    DLOG("getlinecoding.done", "read %u bytes", bytesSentOrReceived);
}

/* Precondition: device->mutex is held */
static bool GetLineCoding(LTDeviceUsbCDCHostMode *device, LTDeviceUsbCDCLineCoding *outBuf) {
    DLOG("get.line.coding", NULL);
    LTUsbDeviceRequest request = {
        .bmRequestType = kLTDeviceUsbStd_RequestType_Dir_DeviceToHost
                       | kLTDeviceUsbStd_RequestType_Type_Class
                       | kLTDeviceUsbStd_RequestType_Recipient_Interface,
        .bRequest = kLTDeviceUsbCDCStd_Request_GetLineCoding,
        .wValue   = 0,
        .wIndex   = device->cdcData.interfaceNumber,
        .wLength  = sizeof(LTDeviceUsbCDCLineCoding)
    };

    TransferCbData cbData = {
        .result = true,
        .device = device,
    };
    device->usbHost->API->ControlTransfer(device->cdcData.deviceAddress, &request, (u8 *)outBuf, sizeof(LTDeviceUsbCDCLineCoding), OnGetLineCodingComplete, &cbData, &kUsbTransferTimeout);
    return cbData.result;
}

/* Precondition: None */
static bool ValidateLineCoding(LTDeviceUsbCDCLineCoding *lineCodingA, LTDeviceUsbCDCLineCoding *lineCodingB) {
    if (lineCodingA->dwDTERate != lineCodingB->dwDTERate
        || lineCodingA->bCharFormat != lineCodingB->bCharFormat
        || lineCodingA->bParityType != lineCodingB->bParityType
        || lineCodingA->bDataBits != lineCodingB->bDataBits) {
        LTLOG_YELLOWALERT("linecoding.mismatch",
                          "expected %lu/%u/%u/%u got %lu/%u/%u/%u",
                          LT_Pu32(lineCodingB->dwDTERate),
                          lineCodingB->bCharFormat,
                          lineCodingB->bParityType,
                          lineCodingB->bDataBits,
                          LT_Pu32(lineCodingA->dwDTERate),
                          lineCodingA->bCharFormat,
                          lineCodingA->bParityType,
                          lineCodingA->bDataBits);
        return false;
    }
    return true;
}

/*_____________________________________________
  HostModeThread initialization and teardown */

/* Runs in device->hostModeThread context */
static bool HostModeThread_Init(void) {
    DLOG("init", NULL);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    LTDeviceUsbCDCHostMode *device = thread->API->GetThreadSpecificClientData(thread, "device");
    // Register for USB device connect/disconnect; notified immediately if there are devices are already connected
    device->usbHost->API->OnDeviceStatus(HostModeThread_OnUsbHostDeviceStatus, true, device);
    return true;
}

/* Runs in device->hostModeThread context */
static void HostModeThread_Exit(void) {
    DLOG("exit", NULL);
    LTOThread *thread = LT_GetCore()->GetCurrentThreadObject();
    LTDeviceUsbCDCHostMode *device = thread->API->GetThreadSpecificClientData(thread, "device");
    // Unregister from USB device connect/disconnect
    device->usbHost->API->NoDeviceStatus(HostModeThread_OnUsbHostDeviceStatus);
}

/*____________________________________________________________
  HostModeThread CDC function connect/disconnect management */

/* Runs in device->hostModeThread context */
static void HostModeThread_OnUsbHostDeviceStatus(u8 deviceAddress, bool connected, const LTUsbConfigDescriptor *config, void *clientData) {
    LTDeviceUsbCDCHostMode *device = clientData;
    device->mutex->API->Lock(device->mutex);
    if (connected) {
        DLOG("connect", NULL);
        // Find and save CDC interface data
        CdcEndpointData cdcData = {.deviceAddress = deviceAddress, .complete = true};
        device->usbHost->API->EnumerateConfigDescriptor(config, HostModeThread_EnumerateConfigForCdcInterfaceCb, &cdcData);
        device->cdcData = cdcData;
        // No CDC interface found for this connected device, move on
        if (!device->cdcData.complete) {
            device->mutex->API->Unlock(device->mutex);
            return;
        }

        // Prepare a buffer to use during read polling
        device->readBuffer = lt_malloc(device->cdcData.endpointBulkIn.wMaxPacketSize);
        if (!device->readBuffer) {
            LTLOG_YELLOWALERT("init.readbuf.OOM", NULL);
            device->mutex->API->Unlock(device->mutex);
            return;
        }

        // If device should be running at this point, initialize the CDC function
        if (device->started) {
            device->hostModeThread->API->QueueTaskProcIfRequired(device->hostModeThread, HostModeThread_InitializeCdcProc, NULL, device);
        }
    } else {
        DLOG("disconnect", NULL);
        HostModeThread_OnDisconnect(device);
    }
    device->mutex->API->Unlock(device->mutex);
}

/* Precondition: device->mutex is held */
static void HostModeThread_OnDisconnect(LTDeviceUsbCDCHostMode *device) {
    // Clear CDC interface data upon device disconnect
    device->cdcData = (CdcEndpointData){};
    lt_free(device->readBuffer);
    device->readBuffer = NULL;
}

/* Precondition: device->mutex is held
 * Synchronously called during OnUsbHostDeviceStatus */
static bool HostModeThread_EnumerateConfigForCdcInterfaceCb(LTDeviceUsbStd_DescriptorType descriptorType, const LTUsbUnknownDescriptor *descriptor, void *clientData) {
    // TODO: This is an extremely basic parsing to get the first CDC function on a device. It assumes descriptors are in
    // the order of communicationInterface -> ACM endpoint -> dataInterface -> Bulk endpoints, which may not be
    // guaranteed. It does not account for things like union descriptors and CDC capability descriptors. It should be
    // made more robust later.
    // TODO: Instead of grabbing the first CDC device available, find the CDC device that matches something specified in the DeviceConfig?
    CdcEndpointData *cdcData = clientData;

    // All endpoints found, stop enumerating descriptors
    if (cdcData->endpointACM.bLength && cdcData->endpointBulkIn.bLength && cdcData->endpointBulkOut.bLength) {
        cdcData->inCommInterface = false;
        cdcData->inDataInterface = false;
        cdcData->complete = true;
        return false;
    }

    if (descriptorType == kLTDeviceUsbStd_DescriptorType_Interface) {
        const LTUsbInterfaceDescriptor *ifDesc = (const LTUsbInterfaceDescriptor *)descriptor;

        // Communication (ACM) interface
        if (ifDesc->bInterfaceClass == kLTDeviceUsbStd_Class_Comm
            && ifDesc->bInterfaceSubClass == kLTDeviceUsbStd_SubClass_CDC_ACM) {
            cdcData->interfaceNumber = ifDesc->bInterfaceNumber;
            cdcData->inCommInterface = true;
            cdcData->inDataInterface = false;
            return true;
        }

        // Data interface follows after comm interface
        if (ifDesc->bInterfaceClass == kLTDeviceUsbStd_Class_CDCData) {
            cdcData->inCommInterface = false;
            cdcData->inDataInterface = true;
            return true;
        }

        // Any other interface resets state
        cdcData->inCommInterface = false;
        cdcData->inDataInterface = false;
        return true;
    }

    if (descriptorType == kLTDeviceUsbStd_DescriptorType_Endpoint) {
        const LTUsbEndpointDescriptor *ep = (const LTUsbEndpointDescriptor *)descriptor;
        u8 epType = ep->bmAttributes & kLTDeviceUsbStd_Attribute_EPTransfer_Mask;
        bool dirIn = (ep->bEndpointAddress & kLTDeviceUsbStd_EndpointAddress_Dir_Mask) != 0;

        if (cdcData->inCommInterface && !cdcData->endpointACM.bLength) {
            if (epType == kLTDeviceUsbStd_Attribute_EPTransfer_Interrupt && dirIn) {
                cdcData->endpointACM = *ep;
            }
            return true;
        }

        if (cdcData->inDataInterface) {
            if (epType == kLTDeviceUsbStd_Attribute_EPTransfer_Bulk) {
                if (dirIn && !cdcData->endpointBulkIn.bLength) {
                    cdcData->endpointBulkIn = *ep;
                } else if (!dirIn && !cdcData->endpointBulkOut.bLength) {
                    cdcData->endpointBulkOut = *ep;
                }
            }
            return true;
        }
    }

    return true;
}

/*_____________________________________________
  HostModeThread CDC function initialization */

/* CDC initialization will either occur after Start() or when a valid USB CDC device is connected */
static void HostModeThread_InitializeCdcProc(void *clientData) {
    DLOG("start.async", NULL);
    LTDeviceUsbCDCHostMode *device = clientData;

    device->mutex->API->Lock(device->mutex);
    // CDC function is not currently connected. Abort start sequence until device is connected (see HostModeThread_OnUsbHostDeviceStatus).
    if (!device->cdcData.complete) {
        device->mutex->API->Unlock(device->mutex);
        return;
    }

    bool success = false;
    LTDeviceUsbCDCLineCoding *clientLineCoding = NULL;
    do {
        // Send ACM-specific control requests to configure the CDC device
        // 1. SET_CONTROL_LINE_STATE
        if (!SetControlLineState(device, kLTDeviceUsbCDCStd_ControlLineState_DTR | kLTDeviceUsbCDCStd_ControlLineState_RTS)) break;
        // 2. SET_LINE_CODING
        if (!SetLineCoding(device, &s_defaultLineCoding)) break;
        // 3. GET_LINE_CODING
        clientLineCoding = lt_malloc(sizeof(LTDeviceUsbCDCLineCoding));
        if (!clientLineCoding) break;
        if (!GetLineCoding(device, clientLineCoding)) break;
        // TODO: OK to ignore failed line coding validation?
        //       There seems to be a timing issue when GetLineCoding is sent too quickly after SetLineCoding, causing the device response to contain invalid data.
        ValidateLineCoding(clientLineCoding, &s_defaultLineCoding);

        // Indicate write is now ready (bulk OUT)
        NotifyToClientWriteReady(device);

        // Begin polling for available data to read (bulk IN)
        StartPollingForReadData(device);

        // TODO: Begin polling for notifications (interrupt IN)

        DLOG("start.async.success", NULL);
        success = true;
        // fall through to cleanup
    } while (false);

    if (!success) {
        NotifyToClientFatalError(device);
    }

    lt_free(clientLineCoding);
    device->mutex->API->Unlock(device->mutex);
}

/*______________________________
  HostModeThread read polling */

/* Runs in device->hostModeThread context */
static void HostModeThread_ReadCompleteCbAsync(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
    LT_UNUSED(bufLen);
    LT_UNUSED(buf);
    DLOG("read.complete", "result %lu bytesRead %lu", LT_Pu32(result), LT_Pu32(bytesSentOrReceived));
    LTDeviceUsbCDCHostMode *device = clientData;
    device->mutex->API->Lock(device->mutex);
    // No CDC device currently connected, abort
    if (!device->cdcData.complete) {
        device->mutex->API->Unlock(device->mutex);
        return;
    }
    // Data successfully read into readBuffer
    if (result == kLTDeviceUsbHostTransferResult_Complete && bytesSentOrReceived > 0) {
        // Signal to client that data is available and stop polling
        device->readBufferBytesAvailable = bytesSentOrReceived;
        device->readBufferBytesReadByClient = 0;
        NotifyToClientReadReady(device);
    } else {
        // Transfer failed or no data available. Continue polling
        StartPollingForReadData(device);
    }
    device->mutex->API->Unlock(device->mutex);
}

/* Runs in device->hostModeThread context */
static void HostModeThread_TryReadDataProc(void *clientData) {
    LTDeviceUsbCDCHostMode *device = clientData;
    DLOG("try.read", NULL);
    device->mutex->API->Lock(device->mutex);

    // No CDC device currently connected, abort
    if (!device->cdcData.complete) {
        device->mutex->API->Unlock(device->mutex);
        return;
    }
    // Asynchronous bulk transfer, completion callback will be queued when transfer completes
    device->usbHost->API->BulkTransfer(device->cdcData.deviceAddress, device->cdcData.endpointBulkIn.bEndpointAddress, (u8 *)device->readBuffer, device->cdcData.endpointBulkIn.wMaxPacketSize, HostModeThread_ReadCompleteCbAsync, device, NULL);

    device->mutex->API->Unlock(device->mutex);
}

/* Precondition: device->mutex is held */
static void StartPollingForReadData(LTDeviceUsbCDCHostMode *device) {
    device->hostModeThread->API->QueueTaskProc(device->hostModeThread, HostModeThread_TryReadDataProc, NULL, device);
}

/*_______________________
  Public API functions */

static bool LTDeviceUsbCDCHostMode_Init(LTDeviceUsbCDCHostMode *device,
                                        const char             *portName,
                                        LTOThread              *thread,
                                        LTThread_TaskProc      *readReadyCallback,
                                        LTThread_TaskProc      *writeReadyCallback,
                                        LTThread_TaskProc      *errorCallback,
                                        void                   *clientData) {
    device->mutex->API->Lock(device->mutex);
    device->portName           = portName;
    device->thread             = thread;
    device->readReadyCallback  = readReadyCallback;
    device->writeReadyCallback = writeReadyCallback;
    device->errorCallback      = errorCallback;
    device->clientData         = clientData;
    device->mutex->API->Unlock(device->mutex);
    return true;
}

static bool LTDeviceUsbCDCHostMode_Start(LTDeviceUsbCDCHostMode *device) {
    device->mutex->API->Lock(device->mutex);
    DLOG("start", NULL);

    // Device already started
    if (device->started) {
        device->mutex->API->Unlock(device->mutex);
        return false;
    }

    // Create thread for polling and connect/disconnect handling
    device->hostModeThread = lt_createobject(LTOThread);
    if (!device->hostModeThread) {
        device->mutex->API->Unlock(device->mutex);
        return false;
    }

    // Start thread and notify client when a device is available and ready!
    device->started = true;
    device->hostModeThread->API->SetThreadSpecificClientData(device->hostModeThread, "device", NULL, device);
    device->hostModeThread->API->SetStackSize(device->hostModeThread, 1024); // TODO: Tune stack size
    device->hostModeThread->API->StartSynchronous(device->hostModeThread, "cdc.host", HostModeThread_Init, HostModeThread_Exit);

    DLOG("start.success", NULL);
    device->mutex->API->Unlock(device->mutex);
    return true;
}

static void LTDeviceUsbCDCHostMode_Stop(LTDeviceUsbCDCHostMode *device) {
    device->mutex->API->Lock(device->mutex);
    // Device not started
    if (!device->started) {
        device->mutex->API->Unlock(device->mutex);
        return;
    }

    // Call the event handler to clean up any connected device
    HostModeThread_OnDisconnect(device);

    // Destroy the management thread
    device->hostModeThread->API->Terminate(device->hostModeThread);
    device->hostModeThread->API->WaitUntilFinished(device->hostModeThread, LTTime_Infinite());
    lt_destroyobject(device->hostModeThread);
    device->hostModeThread = NULL;
    device->started = false;

    device->mutex->API->Unlock(device->mutex);
}

static u32 LTDeviceUsbCDCHostMode_GetMaxWriteSize(LTDeviceUsbCDCHostMode *device) {
    device->mutex->API->Lock(device->mutex);
    // CDC interface not available
    if (!device->cdcData.complete)  {
        device->mutex->API->Unlock(device->mutex);
        return 0;
    }
    // Data available, read out and return
    u32 maxWriteSize = device->cdcData.endpointBulkOut.wMaxPacketSize;
    device->mutex->API->Unlock(device->mutex);
    return maxWriteSize;
}

static u32 LTDeviceUsbCDCHostMode_GetMaxReadSize(LTDeviceUsbCDCHostMode *device) {
    device->mutex->API->Lock(device->mutex);
    // CDC interface not available
    if (!device->cdcData.complete)  {
        device->mutex->API->Unlock(device->mutex);
        return 0;
    }
    // Data available, read out and return
    u32 maxReadSize = device->cdcData.endpointBulkIn.wMaxPacketSize;
    device->mutex->API->Unlock(device->mutex);
    return maxReadSize;
}

/* Synchronously called during LTDeviceUsbCDCHostMode_Write */
static void WriteCompleteCb(LTDeviceUsbHostTransferResult result, u8 *buf, u32 bufLen, u32 bytesSentOrReceived, void *clientData) {
   LT_UNUSED(bufLen);
   LT_UNUSED(buf);
   DLOG("write.complete", "result %lu bytesSent %lu", LT_Pu32(result), LT_Pu32(bytesSentOrReceived));
   TransferCbData *cbData = clientData;
   if (result == kLTDeviceUsbHostTransferResult_Complete) {
       cbData->result = bytesSentOrReceived;
   } else {
       LTLOG_YELLOWALERT("write.fail", NULL);
       cbData->result = -1;
   }
}

static s32 LTDeviceUsbCDCHostMode_Write(LTDeviceUsbCDCHostMode *device, void const *data, LT_SIZE size) {
    device->mutex->API->Lock(device->mutex);
    // CDC interface not available
    if (!device->cdcData.complete) {
        device->mutex->API->Unlock(device->mutex);
        return -1;
    }
    DLOG("try.write", "%lu bytes", LT_Pu32(size));
    // Result will be number of bytes written (+), device busy (0), or error (-)
    TransferCbData cbData = {
        .result = 0,
        .device = device,
    };
    // Synchronous bulk transfer. OK to ignore return value.
    device->usbHost->API->BulkTransfer(device->cdcData.deviceAddress, device->cdcData.endpointBulkOut.bEndpointAddress, (u8 *)data, size, WriteCompleteCb, &cbData, &kUsbTransferTimeout);
    // Write is always ready, notify a new write ready
    NotifyToClientWriteReady(device);
    device->mutex->API->Unlock(device->mutex);
    return cbData.result;
}

static s32 LTDeviceUsbCDCHostMode_Read(LTDeviceUsbCDCHostMode *device, void *data, LT_SIZE size) {
    device->mutex->API->Lock(device->mutex);
    // CDC interface not available
    if (!device->cdcData.complete) {
        device->mutex->API->Unlock(device->mutex);
        return -1;
    }

    // Read data from intermediate buffer
    u32 bytesRemaining = device->readBufferBytesAvailable - device->readBufferBytesReadByClient;
    u32 bytesToRead    = LT_MIN(bytesRemaining, (u32)size);
    DLOG("client.read", "bytesRemaining %lu bytesToRead %lu", LT_Pu32(bytesRemaining), LT_Pu32(bytesToRead));
    lt_memcpy(data, device->readBuffer + device->readBufferBytesReadByClient, bytesToRead);
    device->readBufferBytesReadByClient += bytesToRead;

    // All bytes read from buffer, poll for more read data!
    if (device->readBufferBytesReadByClient == device->readBufferBytesAvailable) {
        // Reset read buffer
        device->readBufferBytesReadByClient = 0;
        device->readBufferBytesAvailable = 0;
        // Start polling for more data again!
        StartPollingForReadData(device);
        DLOG("client.read.done", NULL);
    }

    // Return number of bytes copied to client's buffer
    device->mutex->API->Unlock(device->mutex);
    return bytesToRead;
}


/*____________________________________
  Object constructor and destructor */

static bool LTDeviceUsbCDCHostMode_ConstructObject(LTDeviceUsbCDCHostMode *device) {
    do {
        device->usbHost = lt_createobject(LTDeviceUsbHost);
        if (!device->usbHost) break;
        device->mutex = lt_createobject(LTMutex);
        if (!device->mutex) break;
        return true;
    } while (false);

    return false;
}

static void LTDeviceUsbCDCHostMode_DestructObject(LTDeviceUsbCDCHostMode *device) {
    // Stop any communication with this CDC interface
    if (device->started) LTDeviceUsbCDCHostMode_Stop(device);
    lt_destroyobject(device->usbHost);
    device->usbHost = NULL;
    lt_destroyobject(device->mutex);
    device->mutex = NULL;
}

/*________________________
  Object API definition */

define_LTObjectImplPublic(LTDeviceUsbCDC, LTDeviceUsbCDCHostMode,
    Init,
    Start,
    Stop,
    GetMaxReadSize,
    GetMaxWriteSize,
    Read,
    Write
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Aug-25   aurelian    created
 */