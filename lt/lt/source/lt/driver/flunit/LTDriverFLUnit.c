/******************************************************************************
 * lt/source/lt/driver/flunit/LTDriverFLUnit.c
 *
 * LT Driver Library for FloodLight Accessory 
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/
/** @file LTDriverFLUnit.c Implementation of Floodlight unit specific protocol driver */

#include <lt/LT.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>

DEFINE_LTLOG_SECTION("driver.flunit");

/**
 * Floodlight Communication Protocol
 *
 *  -----------------------------------------------------------------------------------------
 * | Source | Destination | DeviceType | CmdLength | CmdType | Data[0-N] | Checksum(2 bytes) |
 *  -----------------------------------------------------------------------------------------
 *
 *  Source      =  0xAA (Host)
 *  Destination =  0x55 (FL)
 *  DeviceType  =  0x43 (FL)
 *  minimum CmdLength = Checksum(2 bytes) + CmdType(1 byte) = 3 bytes
 *  maximum CmdLength = Checksum(2 bytes) + CmdType(1 byte) + Data(N bytes) = (3 + N) bytes
 *  CmdType = LTDriverFLUnitCmdType enums
 */

#define POLL_INTERVAL          200  // milliseconds
#define KEEP_ALIVE_TIME        30   // seconds
#define CONNECT_RETRY_INTERVAL 1    // seconds
#define MAX_PORT_RETRY         3

/**
 * FL Unit specific constants
 */
enum {
    kFL_Cmd_Checksum_Length = 0x02,
    kFL_Cmd_Min_Length      = 0x03,
    kFL_Cmd_Expected_Length = 0x04,
    kFL_Cmd_Data_Offset     = 0x05,
    kFL_Camera_Address      = 0xAA,
    kFL_Device_Type         = 0x43,
    kFL_Unit_Address        = 0x55,
    kFL_Pir_Event_Size      = 0x01,
    kFL_Photos_Event_Size   = 0x04
};

enum {
    LT_FL_CMD_SCM_DOG_FEEDING      = 0x68,
    LT_FL_CMD_SCM_DOG_FEEDING_RESP = 0x69,
    LT_FL_SET_CMD                  = 0x11,
    LT_FL_GET_CMD                  = 0x55,
    LT_FL_SET_GET_CMD              = 0xAA
};

static LTDeviceUSBSerial *s_libUSBSerial;
static ILTThread *s_thread;
static LTCore    *s_pCore;
static LTThread   s_pollingThread;
static LTThread   s_reconnectThread;
static ILTEvent  *s_event;
static LTEvent    s_triggerEvent;
static LTMutex   *s_mutex;
static s32        s_hPort = 0;
static bool       s_portBusy         = true;
static char       s_pDevicePort[15]  = {};     // /dev/ttyUSBx

static const LTArgsDescriptor s_triggerEventArgs = {1, {kLTArgType_u32}};

/**Buffer to poll pir/photosensitive sensor data to notify**/
static union {
    u8  pir;
    u32 photo;
} s_pollingData;

// FLs protocol command header
typedef union {
    struct {
        u8 src;
        u8 dest;
        u8 deviceType;
        u8 cmdLength;
        u8 cmdType;
    };
    u8 raw[57];  // as per device specification
} CmdPayload;

static u16 LTDriverFLUnitImpl_SerialChecksum(u8 *data, u32 len) {
    u32 sum = 0;
    if (data == NULL) return -1;
    for (; len; --len) sum += *data++;
    return sum;
}

/** Thread release proc, open available usb port, notify connection event,
 *  set s_portBusy false
 */
static void LTFLDriverUnitImpl_ConnectionComplete(LTThread_ReleaseReason reason,  void *clientData) {
    LT_UNUSED(reason);
    LT_UNUSED(clientData);
    s_libUSBSerial->API->OpenPort(s_libUSBSerial, s_pDevicePort, &s_hPort);
    s_event->NotifyEvent(s_triggerEvent, kLTDriverFLUnitEvent_Connected);
    s_thread->Destroy(s_reconnectThread);
    s_reconnectThread = 0;
    s_portBusy = false;
}

/** Thread proc to check floodlight availability on specific period of time
 *  kill timer on success
 */
static void LTFLDriverUnitImpl_ConnectDevice(void *clientData) {
    LT_UNUSED(clientData);
    if (s_libUSBSerial->API->CheckPort(s_libUSBSerial, (u8 *)s_pDevicePort)) {
        s_thread->KillTimer(s_reconnectThread, NULL, NULL);
        s_thread->Terminate(s_reconnectThread);
    }
}

/** Attempt to access the floodlight, if not succeed on
 * first try, then delegate connection task to thread
 */
static bool LTDriverFLUnitImpl_SetupDevice(void) {
    if (s_hPort) {
        s_libUSBSerial->API->ClosePort(s_libUSBSerial, &s_hPort);
        s_hPort = 0;
    }
    else {
        if (s_libUSBSerial->API->CheckPort(s_libUSBSerial, (u8 *)s_pDevicePort)) {
            s_libUSBSerial->API->OpenPort(s_libUSBSerial, s_pDevicePort, &s_hPort);
            s_portBusy = false;
            s_event->NotifyEvent(s_triggerEvent, kLTDriverFLUnitEvent_Connected);
            return true;
        }
        return false;
    }
    s_portBusy = true;
    s_event->NotifyEvent(s_triggerEvent, kLTDriverFLUnitEvent_Disconnected);
    s_reconnectThread = LT_GetCore()->CreateThread("ReconnectThread");
    s_thread->SetStackSize(s_reconnectThread, 2048);
    s_thread->Start(s_reconnectThread, NULL, NULL);
    s_thread->SetTimer(s_reconnectThread, LTTime_Seconds(CONNECT_RETRY_INTERVAL), LTFLDriverUnitImpl_ConnectDevice, LTFLDriverUnitImpl_ConnectionComplete, NULL);
    return false;
}

/* Function to handle actual transaction between floodlight and host*/
static bool LTDriverFLUnitImpl_USBReadWrite(u8 *serialBuffer, u32 writeLength, u8 cmdType, u8 *data, u32 dataLength) {
    bool ret = true;
    u8 reReadCount = 0;
    if (s_portBusy) return false;
    s_mutex->API->Lock(s_mutex);
    ret = s_libUSBSerial->API->WriteBytes(s_libUSBSerial, &s_hPort, serialBuffer, writeLength);
    if (ret) {
        reReadCount  = 0;
        do {
            ++reReadCount;
            /**reading 4 bytes header first */
            if (s_libUSBSerial->API->ReadBytes(s_libUSBSerial, &s_hPort, serialBuffer, kFL_Cmd_Expected_Length)) {
                /**then followed by the payload **/
                s_libUSBSerial->API->ReadBytes(s_libUSBSerial, &s_hPort, &serialBuffer[kFL_Cmd_Expected_Length],
                                                        serialBuffer[kFL_Cmd_Min_Length]);
                /**Validating if the response is against the current request only,
                as per hualai's FL accessory. response id is always equals to (request id + 1)*/
                if ((serialBuffer[kFL_Cmd_Expected_Length] == (cmdType + 1))) {
                    if (dataLength != 0) {
                        lt_memcpy((void *)data, (void *)&serialBuffer[kFL_Cmd_Data_Offset], dataLength);
                    }
                    s_mutex->API->Unlock(s_mutex);
                    // ret is true by default
                    break;
                }
            }
        // retry to read valid response in case of mismatch/late response from floodlight ep
        } while (reReadCount <= MAX_PORT_RETRY);
    }
    s_mutex->API->Unlock(s_mutex);
    /**Reconnect in case of failure */
    if (!ret) {
        LTDriverFLUnitImpl_SetupDevice();
    }
    return ret;
}

static u32 LTDriverFLUnitImpl_CreateCommand(CmdPayload *s_Cmd, u8 cmdType, u8 *data, u32 dataLength, u8 cmdDirection) {
    u32 bufferLength  = kFL_Cmd_Data_Offset;
    s_Cmd->src        = kFL_Camera_Address;
    s_Cmd->dest       = kFL_Unit_Address;
    s_Cmd->deviceType = kFL_Device_Type;
    s_Cmd->cmdLength  = kFL_Cmd_Min_Length;
    s_Cmd->cmdType    = cmdType;

    // for set add data to command payload
    if (cmdDirection == LT_FL_SET_CMD || cmdDirection == LT_FL_SET_GET_CMD) {
        if (dataLength != 0) {
            s_Cmd->cmdLength += dataLength;
            bufferLength     += dataLength;
            for (u32 i = 0; i < dataLength; i++) {
                s_Cmd->raw[kFL_Cmd_Data_Offset + i] = data[i];
            }
        }
    }
    u16 checksum                 = LTDriverFLUnitImpl_SerialChecksum(s_Cmd->raw, bufferLength);
    s_Cmd->raw[bufferLength]     = (checksum >> 8) & 0xff;
    s_Cmd->raw[bufferLength + 1] = (checksum >> 0) & 0xff;
    bufferLength                 += kFL_Cmd_Checksum_Length;
    return bufferLength;
}

/**Send data with associated command */
static bool LTDriverFLUnitImpl_SetCommand(u8 cmdType, u8 *data, u32 dataLength) {
    bool ret = true;
    CmdPayload s_Cmd;
    u32 bufferLength = LTDriverFLUnitImpl_CreateCommand(&s_Cmd, cmdType, data, dataLength, LT_FL_SET_CMD);
    ret = LTDriverFLUnitImpl_USBReadWrite(s_Cmd.raw, bufferLength, cmdType, data, dataLength);
    if (!ret) {
        LTLOG_YELLOWALERT("set", "failed : cmdType=%x", cmdType);
        return false;
    }
    return ret;
}

/**Receive response with associated command */
static bool LTDriverFLUnitImpl_GetCommand(u8 cmdType, u8 *data, u32 dataLength) {
    bool ret = true;
    CmdPayload s_Cmd;
    u32 bufferLength = LTDriverFLUnitImpl_CreateCommand(&s_Cmd, cmdType, data, dataLength, LT_FL_GET_CMD);
    ret = LTDriverFLUnitImpl_USBReadWrite(s_Cmd.raw, bufferLength, cmdType, data, dataLength);
    if (!ret) {
        LTLOG_YELLOWALERT("get", "failed : cmdType=%x", cmdType);
        return false;
    }
    return ret;
}

/**Receive response against data with associated command */
static bool LTDriverFLUnitImpl_SetGetCommand(u8 cmdType, u8 *data, u32 dataLength) {
    bool ret = true;
    CmdPayload s_Cmd;
    u32 bufferLength = LTDriverFLUnitImpl_CreateCommand(&s_Cmd, cmdType, data, dataLength, LT_FL_SET_GET_CMD);
    ret = LTDriverFLUnitImpl_USBReadWrite(s_Cmd.raw, bufferLength, cmdType, data, dataLength);
    if (!ret) {
        LTLOG_YELLOWALERT("setget", "failed : cmdType=%x", cmdType);
        return false;
    }
    return ret;
}

static void LTDriverFLUnitImpl_KeepAlive(void *pClientData) {
    LT_UNUSED(pClientData);
    if (!LTDriverFLUnitImpl_SetCommand(LT_FL_CMD_SCM_DOG_FEEDING, NULL, 0)) {
        LTLOG_YELLOWALERT("watchdog.missed", "failed to send watchdog feeder");
    }
}

static void LTDriverFLUnitImpl_OnStatusChange(LTDriverFLUnitEventProc proc, void *pClientData) {
    s_event->RegisterForEvent(s_triggerEvent, proc, NULL, pClientData, false);
}

static bool LTDriverFLUnitImpl_NoStatusChange(LTDriverFLUnitEventProc proc) {
    return s_event->UnregisterFromEvent(s_triggerEvent, proc);
}

static void LTFLDriverUnitImpl_PollingThread(void *pClientData) {
    LT_UNUSED(pClientData);
    lt_memset(&s_pollingData, 0, sizeof(s_pollingData));
    if (LTDriverFLUnitImpl_GetCommand(LT_FL_CMD_GET_PIR_TRIGGER_STATE, (u8 *)&s_pollingData, kFL_Pir_Event_Size)) {
        if (s_pollingData.pir == 1) {
            s_event->NotifyEvent(s_triggerEvent, kLTDriverFLUnitEvent_PirTrigger);
        }
    }
    if (LTDriverFLUnitImpl_GetCommand(LT_FL_CMD_GET_LIGHT_PHOTOSENSITIVE, (u8 *)&s_pollingData, kFL_Photos_Event_Size)) {
        s_event->NotifyEvent(s_triggerEvent, kLTDriverFLUnitEvent_PhotoSensorTrigger);
    }
}

static void FLEventDispatcher(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    void               *pEventData = NULL;
    LTDriverFLUnitEvent eventType  = (LTDriverFLUnitEvent)LTArgs_u32At(0, args);
    if (!proc) return;
    if (eventType == kLTDriverFLUnitEvent_PirTrigger) pEventData = &s_pollingData.pir;
    if (eventType == kLTDriverFLUnitEvent_PhotoSensorTrigger) pEventData = &s_pollingData.photo;
    (*(LTDriverFLUnitEventProc)proc)(eventType, pEventData, pClientData);
}

static bool LTDriverFLUnitImpl_Start(void) {
    s_pollingThread = s_pCore->CreateThread("PollFloodLight");
    s_thread->SetStackSize(s_pollingThread, 2048);
    s_thread->Start(s_pollingThread, NULL, NULL);
    s_thread->SetTimer(s_pollingThread, LTTime_Milliseconds(POLL_INTERVAL), LTFLDriverUnitImpl_PollingThread, NULL, NULL);
    s_thread->SetTimer(s_pollingThread, LTTime_Seconds(KEEP_ALIVE_TIME), LTDriverFLUnitImpl_KeepAlive, NULL, NULL);
    return true;
}

/* Library initialization and deinitialization */
static void LTDriverFLUnitImpl_LibFini(void) { 
    if ((s_thread != NULL) && (s_pollingThread != 0)) {
        s_thread->KillTimer(s_pollingThread, NULL, NULL);
        s_thread->Terminate(s_pollingThread);
        s_thread->WaitUntilFinished(s_pollingThread, LTTime_Infinite());
        s_thread->Destroy(s_pollingThread);
    }
    if (s_event != NULL) {
        s_event->Destroy(s_triggerEvent);
    }
    if (s_mutex) {
        lt_destroyobject(s_mutex);
    }
    if (s_hPort) s_libUSBSerial->API->ClosePort(s_libUSBSerial, &s_hPort);
    s_hPort = 0;
    lt_destroyobject(s_libUSBSerial);
}

static bool LTDriverFLUnitImpl_LibInit(void) {
    s_pCore = LT_GetCore();
    if (!(s_thread = lt_getlibraryinterface(ILTThread, s_pCore))
        || !(s_event = lt_getlibraryinterface(ILTEvent, s_pCore))
        || !(s_libUSBSerial = lt_createobject(LTDeviceUSBSerial))) {
        return false;
    }
    s_mutex = lt_createobject(LTMutex);
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    u32 libSection   = deviceConfig->GetDriverSection("LTDeviceFLUnit", "LTDriverFLUnit");
    if (libSection == 0) {
        LTLOG_YELLOWALERT("open.fail", "failed to get library section for LTDeviceFLUnit");
        lt_closelibrary(deviceConfig);
        return false;
    }
    lt_memcpy(s_pDevicePort, (char *)deviceConfig->ReadString(libSection, "port"), sizeof(s_pDevicePort));
    s_triggerEvent = s_pCore->CreateEvent(&s_triggerEventArgs, FLEventDispatcher, NULL, NULL, NULL);
    if (!s_triggerEvent) return false;
    lt_closelibrary(deviceConfig);
    return true;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverFLUnit, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverFLUnit) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverFLUnit) {
    .Start          = &LTDriverFLUnitImpl_Start,
    .SetCommand     = &LTDriverFLUnitImpl_SetCommand,
    .GetCommand     = &LTDriverFLUnitImpl_GetCommand,
    .SetGetCommand  = &LTDriverFLUnitImpl_SetGetCommand,
    .SetupDevice    = &LTDriverFLUnitImpl_SetupDevice,
    .OnStatusChange = &LTDriverFLUnitImpl_OnStatusChange,
    .NoStatusChange = &LTDriverFLUnitImpl_NoStatusChange,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverFLUnit, (ILTDriverFLUnit))
