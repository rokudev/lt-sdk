/*******************************************************************************
 * source/lt/device/usb/LTDeviceUsbCDC.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/device/usb/LTDeviceUsbClient.h>
#include <lt/device/usb/LTDeviceUsbCDCStd.h>
#include <lt/device/usb/LTDeviceUsbCDC.h>
#include <lt/device/identity/LTDeviceIdentity.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>


DEFINE_LTLOG_SECTION("sys.usbcdc");

#define AK3918XUSBCLIENT_DO_LOG     0
#if     AK3918XUSBCLIENT_DO_LOG
#define DLOG                        LTLOG
#else
#define DLOG                        LTLOG_LOGNULL
#endif

#define AK3918XUSBCLIENT_DO_ISRLOG  0
#if     AK3918XUSBCLIENT_DO_ISRLOG
#define ISRLOG                      LTLOG_STOMP
#else
#define ISRLOG                      LTLOG_LOGNULL
#endif

/*________________________________________________
  static constants */

enum {
    kEndpointsPerPort  = 3,
    kInterfacesPerPort = 2,
    kEndpointControl   = 0,
    kEndpointACM       = 1,
    kEndpointCDCTX     = 2,
    kEndpointCDCRX     = 3,
    kCdcDescriptorSize = 66,
};

/*________________________________________________
  forward declarations */

typedef struct __attribute__((packed)) {
    LTUsbInterfaceAssociationDescriptor    interfaceAssociation;
    LTUsbInterfaceDescriptor               interfaceComms;
    LTDeviceUsbCDCHeaderDescriptor         header;
    LTDeviceUsbCDCCallManagementDescriptor callManagement;
    LTDeviceUsbCDCACMDescriptor            acm;
    LTDeviceUsbCDCUnionDescriptor          unionDesc;
    LTUsbEndpointDescriptor                endpointACM;
    LTUsbInterfaceDescriptor               interfaceCDC;
    LTUsbEndpointDescriptor                endpointBulkOut;
    LTUsbEndpointDescriptor                endpointBulkIn;
} LTUsbCdcDescriptor;

LT_STATIC_ASSERT_SIZE_32_64(LTUsbCdcDescriptor, kCdcDescriptorSize, kCdcDescriptorSize);

/*
 * Complete config descriptor set
 */
typedef struct __attribute__((packed)) {
    LTUsbConfigDescriptor                  config;
    LTUsbCdcDescriptor                     cdc[];
} LTDeviceUsbCDCConfigDescriptor;

/*  ______________________________
 *  Object private data members */
typedef struct LTDeviceUsbCDCPort LTDeviceUsbCDCPort;
typedef_LTObjectImpl(LTDeviceUsbCDC, LTDeviceUsbCDCImpl) {
    LTDeviceUsbCDCPort *port;
    LTOThread          *callbackThread;
    LTThread_TaskProc  *readReadyCallback;
    LTThread_TaskProc  *writeReadyCallback;
    LTThread_TaskProc  *errorCallback;
    void               *clientData;
} LTOBJECT_API;

struct LTDeviceUsbCDCPort {
    LTDeviceUsbCDCImpl      *user;
    const char              *name;
    u32                      endpointACM;
    u32                      endpointCDCTX;
    u32                      endpointCDCRX;
    bool                     canRead;
    bool                     canWrite;
    u8                       commFeature[8];
    LTDeviceUsbCDCLineCoding lineCoding;
};

/*_________________________________
  Library private implementation */

static u8 const s_serialStateNotification[10] = {0xa1, 0x20, 0, 0, 0, 0, 2, 0, 3, 0};

static LTArray                       *s_stringDescriptors;
static LTDeviceUsbClient             *s_lDevUsbClient;
static ILTDriverUsbClientDeviceUnit  *s_iDevUsbClient;
static LTDeviceUnit                   s_hDevUsbClient;
static LTDeviceConfig                *s_deviceConfig;
static LTDeviceIdentity              *s_deviceIdentity;

static LTUsbDeviceDescriptor           s_deviceDescriptor;
static LTDeviceUsbCDCConfigDescriptor *s_configDescriptor;
static LT_SIZE                         s_configDescriptorSize;

static LTDeviceUsbCDCPort             *s_Ports = NULL;
static u32                             s_NumPorts = 0;

static void LT_ISR_SAFE NotifyReadReady(LTDeviceUsbCDCPort *port) {
    LTDeviceUsbCDCImpl *user = port->user;
    if (user && user->readReadyCallback) {
        user->callbackThread->API->QueueTaskProc(user->callbackThread, user->readReadyCallback, NULL, user->clientData);
    }
}

static void LT_ISR_SAFE NotifyWriteReady(LTDeviceUsbCDCPort *port) {
    LTDeviceUsbCDCImpl *user = port->user;
    if (user && user->writeReadyCallback) {
        user->callbackThread->API->QueueTaskProc(user->callbackThread, user->writeReadyCallback, NULL, user->clientData);
    }
}

static void LT_ISR_SAFE NotifyError(LTDeviceUsbCDCPort *port) {
    LTDeviceUsbCDCImpl *user = port->user;
    if (user && user->errorCallback) {
        user->callbackThread->API->QueueTaskProc(user->callbackThread, user->errorCallback, NULL, user->clientData);
    }
}

static void LT_ISR_SAFE SetupFinished(LTDeviceUsbCDCPort *port) {
    if (!port->canWrite) {
        port->canWrite = true;
        NotifyWriteReady(port);
    }
}

static void LT_ISR_SAFE SetIoBuf(LTUsbIOBuffer *ioBuf, void* data, u32 size) {
    ioBuf->data = data;
    ioBuf->size = size;
    ioBuf->processed = 0;
    ioBuf->releaseProc = NULL;
}

static bool LT_ISR_SAFE DescriptorRequestHandler(LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    const u8 descriptorIndex = request->wValue & 0xff;
    const u8 descriptorType = request->wValue >> 8;

    //ISRLOG("desc", "Descriptor requested, type %u index %u", (unsigned)descriptorType, (unsigned)descriptorIndex);
    switch (descriptorType) {
        case kLTDeviceUsbStd_DescriptorType_Device:
            //ISRLOG("desc.dev", "Device descriptor requested");
            SetIoBuf(ioBuf, &s_deviceDescriptor, LT_MIN((LT_SIZE)request->wLength, sizeof(s_deviceDescriptor)));
            return true;
        case kLTDeviceUsbStd_DescriptorType_Config:
            //ISRLOG("desc.conf", "Config descriptor requested");
            SetIoBuf(ioBuf, s_configDescriptor, LT_MIN((LT_SIZE)request->wLength, s_configDescriptorSize));
            return true;
        case kLTDeviceUsbStd_DescriptorType_String: {
            //ISRLOG("desc.str", "String descriptor %lu requested", LT_Pu32(descriptorIndex));
            const LTUsbStringDescriptor *stringDescriptor = s_stringDescriptors->API->Get(s_stringDescriptors, descriptorIndex, NULL);
            if (stringDescriptor) {
                SetIoBuf(ioBuf, (void *)stringDescriptor, LT_MIN(request->wLength, (u16)stringDescriptor->bLength));
                return true;
            }
            ISRLOG("desc.str", "Unknown string descriptor %u requested", (unsigned)descriptorIndex);
            return false;
        }
        default:
            ISRLOG("desc.unk", "Unknown descriptor requested, descriptorType %u", (unsigned)descriptorType);
            break;
    }
    return false;
}

static bool LT_ISR_SAFE DeviceRequestHandler(void *clientData, LTDeviceUsbStd_ControlStage stage, LTUsbDeviceRequest const *request, LTUsbIOBuffer *ioBuf) {
    LT_UNUSED(clientData);
    u8 requestType = request->bmRequestType & kLTDeviceUsbStd_RequestType_Type_Mask;

    //ISRLOG("req", "Device request type 0x%02x, bRequest 0x%02x, wIndex %u, wLength %u, stage %u", (unsigned)requestType, (unsigned)request->bRequest, (unsigned)request->wIndex, (unsigned)request->wLength, (unsigned)stage);

    if (requestType == kLTDeviceUsbStd_RequestType_Type_Standard && request->bRequest == kLTDeviceUsbStd_Request_GetDescriptor) {
        if (stage != kLTDeviceUsbStd_ControlStage_Setup) {
            //ISRLOG("req.desc.stage", "GetDescriptor request, but wrong stage");
            return true;
        }
        //ISRLOG("req.desc", "GetDescriptor request");
        return DescriptorRequestHandler(request, ioBuf);
    } else if (requestType == kLTDeviceUsbStd_RequestType_Type_Class) {
        const u8 recipient = request->bmRequestType & kLTDeviceUsbStd_RequestType_Recipient_Mask;
        if (recipient != kLTDeviceUsbStd_RequestType_Recipient_Interface) {
            return false;
        }
        u16 interface = request->wIndex;
        if (interface >= s_configDescriptor->config.bNumInterfaces) {
            return false;
        }
        LTDeviceUsbCDCPort *port = &s_Ports[interface / kInterfacesPerPort];
        if (stage == kLTDeviceUsbStd_ControlStage_Setup) {
            /* control transmit setup stage */
            switch (request->bRequest) {
                case kLTDeviceUsbCDCStd_Request_GetLineCoding:
                    //ISRLOG("req.glc", "GetLineCoding");
                    SetIoBuf(ioBuf, &port->lineCoding, sizeof(port->lineCoding));
                    SetupFinished(port);
                    return true;
                case kLTDeviceUsbCDCStd_Request_SetLineCoding:
                    //ISRLOG("req.slc", "SetLineCoding %p %p", port, &port->lineCoding);
                    SetIoBuf(ioBuf, &port->lineCoding, sizeof(port->lineCoding));
                    SetupFinished(port);
                    return true;
                case kLTDeviceUsbCDCStd_Request_SetControlLineState:
                    //ISRLOG("req.scls", "SetControlLineState, wValue 0x%02x", (unsigned)request->wValue);
                    SetupFinished(port);
                    return true;
                case kLTDeviceUsbCDCStd_Request_GetCommFeature:
                    //ISRLOG("req.gcf", "GetCommFeature");
                    SetIoBuf(ioBuf, &port->lineCoding, sizeof(port->commFeature));
                    return true;
                case kLTDeviceUsbCDCStd_Request_SetCommFeature:
                    //ISRLOG("req.scf", "SetCommFeature");
                    SetIoBuf(ioBuf, &port->lineCoding, sizeof(port->commFeature));
                    return true;
                default:
                    // don't care about the rest
                    return true;
            }
        } else if (stage == kLTDeviceUsbStd_ControlStage_DataOut) {
            /* control transmit data(out) stage */
            switch (request->bRequest) {
                case kLTDeviceUsbCDCStd_Request_SetLineCoding:
                    // already read into device->lineCoding, nothing to do
                    return true;
                case kLTDeviceUsbCDCStd_Request_SetCommFeature:
                    // already read into device->commFeature, nothing to do
                    return true;
                default:
                    // don't care about the rest
                    return true;
            }
        } else {
            // do nothing in other stages
            return true;
        }
    }

    return false;
}

static void LT_ISR_SAFE ConfigOkHandler(bool ready, void *clientData) {
    LT_UNUSED(clientData);
    //ISRLOG("configok", "ready: %u", (unsigned)ready);
    if (!ready) {
        for (u32 i = 0; i < s_NumPorts; ++i) {
            LTDeviceUsbCDCPort *port = &s_Ports[i];
            NotifyError(port);
        }
    }
}

static void LT_ISR_SAFE EndpointACMWriteReadyHandler(void *clientData) {
    LTDeviceUsbCDCPort *port = clientData;
    //ISRLOG("ready.acm", "ACMWriteReady");
    s32 written = s_iDevUsbClient->Write(s_hDevUsbClient, port->endpointACM, s_serialStateNotification, sizeof(s_serialStateNotification));
    if (written < 0 || (LT_SIZE)written != sizeof(s_serialStateNotification)) {
        ISRLOG("ready.acm.fail", "Failed to write serial state notification: %d", (int)written);
    }
}

static void LT_ISR_SAFE EndpointCDCWriteReadyHandler(void *clientData) {
    LTDeviceUsbCDCPort *port = clientData;
    //ISRLOG("ready.cdc.tx", "CDCWriteReady");
    if (port->canWrite) NotifyWriteReady(port);
}

static void LT_ISR_SAFE EndpointCDCReadReadyHandler(void *clientData) {
    LTDeviceUsbCDCPort *port = clientData;
    //ISRLOG("ready.cdc.rx", "CDCReadReady");
    port->canRead = true;
    NotifyReadReady(port);
}

static bool LTDeviceUsbCDCImpl_Init(LTDeviceUsbCDCImpl *device, const char *portName, LTOThread *thread, LTThread_TaskProc *readReadyCallback, LTThread_TaskProc *writeReadyCallback, LTThread_TaskProc *errorCallback, void *clientData) {
    // Configure the USB Client
    DLOG("start", "Start port=%s", portName);

    for (u32 i = 0; i < s_NumPorts; ++i) {
        if (lt_strcmp(portName, s_Ports[i].name) == 0) { device->port = &s_Ports[i]; break; }
    }

    if (!device->port) {
        DLOG("start.badport", "Failed to locate port name: %s", portName);
        return false;
    }
    if (!thread || !readReadyCallback || !writeReadyCallback) {
        DLOG("start.io", "No IO callbacks provided");
        return false;
    }

    device->callbackThread     = thread;
    device->readReadyCallback  = readReadyCallback;
    device->writeReadyCallback = writeReadyCallback;
    device->errorCallback      = errorCallback;
    device->clientData         = clientData;

    return true;
}

static bool LTDeviceUsbCDCImpl_Start(LTDeviceUsbCDCImpl *device) {
    if (device->port->user) {
        DLOG("start.open", "Port is already open");
        return false;
    }
    device->port->user = device;
    if (device->port->canRead)  NotifyReadReady(device->port);
    if (device->port->canWrite) NotifyWriteReady(device->port);
    return true;
}

static void LTDeviceUsbCDCImpl_Stop(LTDeviceUsbCDCImpl *device) {
    DLOG("stop", "Stop");
    if (device && device->port) device->port->user = NULL;
}

static u32 LTDeviceUsbCDCImpl_GetMaxWriteSize(LTDeviceUsbCDCImpl *device) {
    return s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, device->port->endpointCDCTX);
}

static u32 LTDeviceUsbCDCImpl_GetMaxReadSize(LTDeviceUsbCDCImpl *device) {
    return s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, device->port->endpointCDCRX);
}

static s32 LTDeviceUsbCDCImpl_Write(LTDeviceUsbCDCImpl *device, void const *data, LT_SIZE size) {
    return s_iDevUsbClient->Write(s_hDevUsbClient, device->port->endpointCDCTX, data, size);
}

static s32 LTDeviceUsbCDCImpl_Read(LTDeviceUsbCDCImpl *device, void *data, LT_SIZE size) {
    return s_iDevUsbClient->Read(s_hDevUsbClient, device->port->endpointCDCRX, data, size);
}

static bool InitPorts(u32 deviceSection) {
    bool result = false;
    while (true) {
        char portKey[20];
        lt_snprintf(portKey, sizeof(portKey), "config/ports/%lu", LT_Pu32(s_NumPorts));
        const char *portName = s_deviceConfig->ReadString(deviceSection, portKey);
        if (!portName) break;

        s_Ports = lt_realloc(s_Ports, sizeof(*s_Ports) * (s_NumPorts + 1));
        if (!s_Ports) {
            result = false;
            break;
        }

        u32 portIndex = s_NumPorts++;
        s_Ports[portIndex] = (LTDeviceUsbCDCPort) {
            .user          = NULL,
            .name          = portName,
            .endpointACM   = (kEndpointsPerPort * portIndex) + kEndpointACM,
            .endpointCDCRX = (kEndpointsPerPort * portIndex) + kEndpointCDCRX,
            .endpointCDCTX = (kEndpointsPerPort * portIndex) + kEndpointCDCTX,
            .lineCoding    = (LTDeviceUsbCDCLineCoding) {
                .dwDTERate   = 115200,
                .bCharFormat = 1,
                .bParityType = 8,
                .bDataBits   = 0,
            },
            .canRead = false,
            .canWrite = false,
            .commFeature = {},
        };
        result = true;
    }

    return result;
}

/*  _________________________________
 *  Object constructor and destructor
 */

static void LTDeviceUsbCDCImpl_DestructObject(LTDeviceUsbCDCImpl *device) {
    LT_UNUSED(device);
    DLOG("dtr", "Destructing LTDeviceUsbCDC");
    LTDeviceUsbCDCImpl_Stop(device);
}

static bool LTDeviceUsbCDCImpl_ConstructObject(LTDeviceUsbCDCImpl *device) {
    DLOG("ctr", "Constructing LTDeviceUsbCDC");
    device->port               = NULL;
    device->callbackThread     = NULL;
    device->readReadyCallback  = NULL;
    device->writeReadyCallback = NULL;
    device->errorCallback      = NULL;
    device->clientData         = NULL;

    return true;
}

static u8 AddStringDescriptor(const char *value) {
    if (!value) return 0;
    u32 valueLen = lt_strlen(value);
    if (valueLen == 0) {
        value = "?";
        valueLen = 1;
    }
    u32 descriptorLen = sizeof(LTUsbStringDescriptor) + (valueLen * sizeof(u16));
    LTUsbStringDescriptor *descriptor = lt_malloc(descriptorLen);
    if (!descriptor) return 0;
    descriptor->bDescriptorType = kLTDeviceUsbStd_DescriptorType_String;
    descriptor->bLength = descriptorLen;
    for (u32 i = 0; i < valueLen; ++i) {
        descriptor->wData[i] = LT_LE16(value[i]);
    }
    s32 strIndex = s_stringDescriptors->API->Append(s_stringDescriptors, descriptor);
    if ((strIndex <= 0) || (strIndex > 255)) return 0;
    return strIndex;
}

static bool InitStringDescriptors(void) {
    s_stringDescriptors = lt_createobject(LTArray);
    if (!s_stringDescriptors) return false;

    static const LTUsbStringDescriptor languageDescriptor = {
        .bLength = 4,
        .bDescriptorType = kLTDeviceUsbStd_DescriptorType_String,
        .wData = { LT_LE16(0x0409) }, /* language index (0x0409 = US-English) */
    };
    return s_stringDescriptors->API->Append(s_stringDescriptors, &languageDescriptor) == 0;
}

// Returns a heap-allocated string suitable for use as a serial number.
static LTString GetSerialNumber(void) {
    // Use the device ID by default if available
    const char *deviceID = s_deviceIdentity->GetDeviceId();
    if (deviceID && lt_strlen(deviceID) > 0) {
        return lt_strdup(deviceID);
    }

    // Generate a fallback serial number, in case we're still on the manufacturing line.
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    LTUtilityByteOps *byteOps = NULL;
    if (!settings) {
        LTLOG_YELLOWALERT("settings.open", "Failed to open LTSystemSettings");
        goto exit;
    }
    LTString tempID = NULL;
    if (settings->GetStringValue("mfg/tempID", &tempID)) {
        // Already have a tempID.
        goto exit;
    }
    LTLOG("settings.gen", "Generating temporary ID for manufacturing");
    byteOps = lt_openlibrary(LTUtilityByteOps);
    if (!byteOps) {
        LTLOG_YELLOWALERT("byteops.open", "Failed to open LTUtilityByteOps");
        goto exit;
    }
    u8 uuid[LT_UUID_BYTE_LEN];
    byteOps->GenUUID(uuid);
    tempID = ltstring_createempty(LT_UUID_STRING_LEN+1);
    if (!byteOps->UUIDToString(uuid, tempID, LT_UUID_STRING_LEN+1)) {
        LTLOG_YELLOWALERT("uuid.string", "Failed to convert UUID to string");
        goto exit;
    }
    settings->SetStringValue("mfg/tempID", tempID);

exit:
    lt_closelibrary(byteOps);
    lt_closelibrary(settings);
    if (!tempID) {
        // Guarantee that we return a valid string.
        tempID = ltstring_create("");
    }
    return tempID;
}

/*  ____________________
 *  Library init/destroy
 */
static bool InitUsbDevice(void) {
    u32 deviceSection, vendorId, productId;
    bool configValid = (deviceSection = s_deviceConfig->GetDeviceSection("LTDeviceUsbCDC"))            &&
                       (vendorId      = s_deviceConfig->ReadInteger(deviceSection, "config/vendorId")) &&
                       (productId     = s_deviceConfig->ReadInteger(deviceSection, "config/productId"));
    if (!configValid) return false;

    if (!InitStringDescriptors())  return false;
    if (!InitPorts(deviceSection)) return false;

    s_lDevUsbClient = lt_openlibrary(LTDeviceUsbClient);
    if (!s_lDevUsbClient) {
        DLOG("ctr.open", "Failed to open LTDeviceUsbClient library");
        return false;
    }

    // Get the handle for an appropriate USB Client device
    s_hDevUsbClient = LTHANDLE_INVALID;
    const u32 numHandles = s_lDevUsbClient->GetNumDeviceUnits();
    for (u32 i = 0; i < numHandles; i++) {
        LTDeviceUnit client = s_lDevUsbClient->CreateDeviceUnitHandle(i);
        if (!client) {
            continue;
        }
        s_iDevUsbClient = lt_gethandleinterface(ILTDriverUsbClientDeviceUnit, client);

        /*
         * Minimum number of endpoints needed to run N CDC interfaces:
         *     EP0: USB control
         * For each port:
         *     EPX+1: CIC ACM interrupt in
         *     EPX+2: CDC bulk pipe in (TX)
         *     EPX+3: CDC bulk pipe out (RX)
         */
        u32 requiredEndpoints = 1 + (s_NumPorts * kEndpointsPerPort);
        if (s_iDevUsbClient->GetNumEndpoints(client) < requiredEndpoints) {
            //DLOG("ctr.badunit", "Discarding unit %u, not enough endpoints", (unsigned)i);
            lt_destroyhandle(client);
            continue;
        }

        //DLOG("ctr.goodunit", "Using unit %u", (unsigned)i);
        s_hDevUsbClient = client;
        break;
    }
    if (!s_hDevUsbClient) {
        DLOG("ctr.nohandle", "Failed to find a suitable device among %u units", (unsigned)numHandles);
        return false;
    }

    // The serial number is on the heap, so save it for later freeing.
    LTString serialNumber = GetSerialNumber();

    // Construct descriptors
    s_deviceDescriptor = (LTUsbDeviceDescriptor) {
        .bLength            = kLTDeviceUsbStd_DescriptorSize_Device,
        .bDescriptorType    = kLTDeviceUsbStd_DescriptorType_Device,
        .bcdUSB             = kLTDeviceUsbStd_SpecVersion_2_0,
        .bDeviceClass       = s_NumPorts > 1 ? kLTDeviceUsbStd_Class_MultiFunction    : kLTDeviceUsbStd_Class_Comm,
        .bDeviceSubClass    = s_NumPorts > 1 ? kLTDeviceUsbStd_SubClass_MultiFunction : kLTDeviceUsbStd_SubClass_None,
        .bDeviceProtocol    = s_NumPorts > 1 ? kLTDeviceUsbStd_Protocol_MultiFunction : kLTDeviceUsbStd_Protocol_None,
        .bMaxPacketSize0    = s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, kEndpointControl),
        .idVendor           = LT_LE16(vendorId),
        .idProduct          = LT_LE16(productId),
        .bcdDevice          = 0x01,
        .iManufacturer      = AddStringDescriptor(s_deviceIdentity->GetManufacturer()),
        .iProduct           = AddStringDescriptor(s_deviceIdentity->GetModel()),
        .iSerialNumber      = AddStringDescriptor(serialNumber),
        .bNumConfigurations = 1,
    };
    ltstring_destroy(serialNumber);
    if (!s_deviceDescriptor.iManufacturer || !s_deviceDescriptor.iProduct || !s_deviceDescriptor.iSerialNumber) return false;

    s_configDescriptorSize = sizeof(LTUsbConfigDescriptor) + sizeof(LTUsbCdcDescriptor) * s_NumPorts;
    s_configDescriptor = lt_malloc(s_configDescriptorSize);
    s_configDescriptor->config = (LTUsbConfigDescriptor) {
        .bLength             = kLTDeviceUsbStd_DescriptorSize_Config,
        .bDescriptorType     = kLTDeviceUsbStd_DescriptorType_Config,
        .wTotalLength        = s_configDescriptorSize,
        .bNumInterfaces      = s_NumPorts * kInterfacesPerPort,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = kLTDeviceUsbStd_Attribute_Config_BusPower | kLTDeviceUsbStd_Attribute_Config_One,
        .bMaxPower           = 250,
    };

    for (u32 i = 0; i < s_NumPorts; ++i) {
        LTDeviceUsbCDCPort *port = &s_Ports[i];
        LTUsbCdcDescriptor *cdcDescriptor = &s_configDescriptor->cdc[i];

        u8 interfaceString = AddStringDescriptor(port->name);
        if (!interfaceString) break;

        cdcDescriptor->interfaceAssociation = (LTUsbInterfaceAssociationDescriptor) {
            .bLength = kLTDeviceUsbStd_DescriptorSize_InterfaceAssociation,
            .bDescriptorType = kLTDeviceUsbStd_DescriptorType_InterfaceAssociation,
            .bFirstInterface = i * kInterfacesPerPort,
            .bInterfaceCount = 2,
            .bFunctionClass = kLTDeviceUsbStd_Class_Comm,
            .bFunctionSubClass = kLTDeviceUsbStd_SubClass_None,
            .bFunctionProtocol = 0,
            .iFunction = 0,
        };
        cdcDescriptor->interfaceComms = (LTUsbInterfaceDescriptor) {
            .bLength            = kLTDeviceUsbStd_DescriptorSize_Interface,
            .bDescriptorType    = kLTDeviceUsbStd_DescriptorType_Interface,
            .bInterfaceNumber   = i * kInterfacesPerPort,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 1,
            .bInterfaceClass    = kLTDeviceUsbStd_Class_Comm,
            .bInterfaceSubClass = kLTDeviceUsbStd_SubClass_CDC_ACM,
            .bInterfaceProtocol = kLTDeviceUsbCDCStd_Protocol_None,
            .iInterface         = interfaceString,
        };
        cdcDescriptor->header = (LTDeviceUsbCDCHeaderDescriptor) {
            .bLength            = kLTDeviceUsbCDCStd_DescriptorSize_Header,
            .bDescriptorType    = kLTDeviceUsbCDCStd_DescriptorType_CSInterface,
            .bDescriptorSubtype = kLTDeviceUsbCDCStd_DescriptorSubtype_Header,
            .bcdCDC             = kLTDeviceUsbCDCStd_SpecVersion_1_2,
        };
        cdcDescriptor->callManagement = (LTDeviceUsbCDCCallManagementDescriptor) {
            .bLength            = kLTDeviceUsbCDCStd_DescriptorSize_CallManagement,
            .bDescriptorType    = kLTDeviceUsbCDCStd_DescriptorType_CSInterface,
            .bDescriptorSubtype = kLTDeviceUsbCDCStd_DescriptorSubtype_CallManagement,
            .bmCapabilities     = kLTDeviceUsbCDCStd_Capabilities_None,
            .bDataInterface     = (i * kInterfacesPerPort) + 1,
        };
        cdcDescriptor->acm = (LTDeviceUsbCDCACMDescriptor) {
            .bLength            = kLTDeviceUsbCDCStd_DescriptorSize_ACM,
            .bDescriptorType    = kLTDeviceUsbCDCStd_DescriptorType_CSInterface,
            .bDescriptorSubtype = kLTDeviceUsbCDCStd_DescriptorSubtype_ACM,
            .bmCapabilities     = kLTDeviceUsbCDCStd_Capabilities_Line,
        };
        cdcDescriptor->unionDesc = (LTDeviceUsbCDCUnionDescriptor) {
            .bLength            = kLTDeviceUsbCDCStd_DescriptorSize_Union,
            .bDescriptorType    = kLTDeviceUsbCDCStd_DescriptorType_CSInterface,
            .bDescriptorSubtype = kLTDeviceUsbCDCStd_DescriptorSubtype_Union,
            .bMasterInterface   = (i * kInterfacesPerPort),
            .bSlaveInterface0   = (i * kInterfacesPerPort) + 1,
        };
        cdcDescriptor->endpointACM = (LTUsbEndpointDescriptor) {
            .bLength          = kLTDeviceUsbStd_DescriptorSize_Endpoint,
            .bDescriptorType  = kLTDeviceUsbStd_DescriptorType_Endpoint,
            .bEndpointAddress = s_Ports[i].endpointACM | kLTDeviceUsbStd_EndpointAddress_Dir_In,
            .bmAttributes     = kLTDeviceUsbStd_Attribute_EPTransfer_Interrupt,
            .wMaxPacketSize   = s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, s_Ports[i].endpointACM),
            .bInterval        = 10,
        };
        cdcDescriptor->interfaceCDC = (LTUsbInterfaceDescriptor) {
            .bLength            = kLTDeviceUsbStd_DescriptorSize_Interface,
            .bDescriptorType    = kLTDeviceUsbStd_DescriptorType_Interface,
            .bInterfaceNumber   = (i * kInterfacesPerPort) + 1,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 2,
            .bInterfaceClass    = kLTDeviceUsbStd_Class_CDCData,
            .bInterfaceSubClass = kLTDeviceUsbStd_SubClass_None,
            .bInterfaceProtocol = kLTDeviceUsbCDCStd_Protocol_None,
            .iInterface         = 0,
        };
        cdcDescriptor->endpointBulkOut = (LTUsbEndpointDescriptor) {
            .bLength          = kLTDeviceUsbStd_DescriptorSize_Endpoint,
            .bDescriptorType  = kLTDeviceUsbStd_DescriptorType_Endpoint,
            .bEndpointAddress = s_Ports[i].endpointCDCRX | kLTDeviceUsbStd_EndpointAddress_Dir_Out,
            .bmAttributes     = kLTDeviceUsbStd_Attribute_EPTransfer_Bulk,
            .wMaxPacketSize   = LT_MIN(s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, s_Ports[i].endpointCDCRX), (u16)512u),
            .bInterval        = 0,
        };
        cdcDescriptor->endpointBulkIn = (LTUsbEndpointDescriptor) {
            .bLength          = kLTDeviceUsbStd_DescriptorSize_Endpoint,
            .bDescriptorType  = kLTDeviceUsbStd_DescriptorType_Endpoint,
            .bEndpointAddress = s_Ports[i].endpointCDCTX | kLTDeviceUsbStd_EndpointAddress_Dir_In,
            .bmAttributes     = kLTDeviceUsbStd_Attribute_EPTransfer_Bulk,
            .wMaxPacketSize   = LT_MIN(s_iDevUsbClient->GetEndpointMaxPacketSize(s_hDevUsbClient, s_Ports[i].endpointCDCTX), (u16)512u),
            .bInterval        = 0,
        };
    }

    s_iDevUsbClient->SetDeviceRequestHandler(s_hDevUsbClient, &DeviceRequestHandler, NULL);
    for (u32 i = 0; i < s_NumPorts; ++i) {
        LTDeviceUsbCDCPort *port = &s_Ports[i];
        if (!s_iDevUsbClient->ConfigureEndpoint(s_hDevUsbClient, &s_configDescriptor->cdc[i].endpointACM, &EndpointACMWriteReadyHandler, port)) {
            DLOG("start.acm", "ACM endpoint config failed");
            return false;
        }
        if (!s_iDevUsbClient->ConfigureEndpoint(s_hDevUsbClient, &s_configDescriptor->cdc[i].endpointBulkOut, &EndpointCDCReadReadyHandler, port)) {
            DLOG("start.out", "Out endpoint config failed");
            return false;
        }
        if (!s_iDevUsbClient->ConfigureEndpoint(s_hDevUsbClient, &s_configDescriptor->cdc[i].endpointBulkIn, &EndpointCDCWriteReadyHandler, port)) {
            DLOG("start.in", "In endpoint config failed");
            return false;
        }
    }

    // Enable the device
    if (!s_iDevUsbClient->Enable(s_hDevUsbClient, ConfigOkHandler, NULL)) {
        DLOG("start.enable", "Device enable failed");
        return false;
    }

    return true;
}

static void DestroyUsbDevice(void) {
    s_iDevUsbClient->Disable(s_hDevUsbClient);

    if (s_hDevUsbClient) {
        lt_destroyhandle(s_hDevUsbClient);
        s_hDevUsbClient = 0;
    }

    if (s_lDevUsbClient) {
        lt_closelibrary(s_lDevUsbClient);
        s_lDevUsbClient = NULL;
    }

    for (u32 i = 1; i < s_stringDescriptors->API->GetCount(s_stringDescriptors); ++i) {
        lt_free(s_stringDescriptors->API->Get(s_stringDescriptors, i, NULL));
    }
    lt_destroyobject(s_stringDescriptors);

    lt_free(s_Ports);
    s_Ports = NULL;
    s_NumPorts = 0;

    lt_free(s_configDescriptor);
    s_configDescriptor = NULL;
    s_configDescriptorSize = 0;
}

static bool LTDeviceUsbCDCLibImpl_LibInit(void) {
    if (!(s_deviceConfig  = lt_openlibrary(LTDeviceConfig))) return false;
    if (!(s_deviceIdentity = lt_openlibrary(LTDeviceIdentity))) return false;
    return InitUsbDevice();
}

static void LTDeviceUsbCDCLibImpl_LibFini(void) {
    DestroyUsbDevice();
    lt_closelibrary(s_deviceIdentity);
    lt_closelibrary(s_deviceConfig);
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceUsbCDC, LTDeviceUsbCDCImpl,
    Init,
    Start,
    Stop,
    GetMaxReadSize,
    GetMaxWriteSize,
    Read,
    Write,
);

typedef_LTLIBRARY_ROOT_INTERFACE(LTDeviceUsbCDCLib, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDeviceUsbCDCLib) LTLIBRARY_DEFINITION;
LTLIBRARY_EXPORT_INTERFACES(LTDeviceUsbCDCLib, (LTDeviceUsbCDCImpl));
