/******************************************************************************
 * lt/source/lt/driver/doublehorn/DoublehornAccessoryDriver.c
 *
 * LT Driver Library for Doublehorn Accessory
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/
/** @file LTDriverDoublehorn.c Implementation of Doublehorn protocol driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/LT.h>
#include <lt/driver/floodlightaccessory/LTDriverFloodlightAccessory.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>
#include <lt/device/usbserial/LTDeviceUSBSerial.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("drv.doublehorn");

/* _____________________________
/  object impl struct typedef */
typedef_LTObjectImpl(LTDriverFloodlightAccessory, DoublehornAccessoryDriver) {
} LTOBJECT_API;

#define SIZEOF(a)              (sizeof(a) - 1)

typedef enum {
    kLTDriverDoublehornKeyBufferLength      = 64,    /* Maximum size of key in msgpack */
    kLTDriverDoublehornValueBufferLength    = 128,   /* Maximum size of value in msgpack */
    kLTDriverDoublehornRequestBufferLength  = 200,   /* Typical size of request msgpack payloads from the host device */
    kLTDriverDoublehornEventEndpointIndex   = 7,     /* Position of key endpoint in event msgpack */
    kLTDriverDoublehornMaxReadMessagePack   = 2000,  /* Maximum size of response msgpack payloads received from accessory */
    kLTDriverDoublehornMaxReadRetry         = 2,     /* Number of times to retry reading valid response from the serial port */
    kLTDriverDoublehornIdentityBufferLength = 32
} LTDriverDoublehorn;

/* Doublehorn accessory message types */
typedef enum {
    kLTDriverDoublehornMessageTypeRequest  = 1,
    kLTDriverDoublehornMessageTypeResponse = 2,
    kLTDriverDoublehornMessageTypeEvent    = 3,
    kLTDriverDoublehornMessageTypeLog      = 4
} LTDriverDoublehornMessageType;

/* Doublehorn accessory command types */
typedef enum {
    kLTDriverDoublehornRequestCmdTypeHandshake     = 1,
    kLTDriverDoublehornRequestCmdTypePing          = 2,
    kLTDriverDoublehornRequestCmdTypeSet           = 3,
    kLTDriverDoublehornRequestCmdTypeGet           = 4,
    kLTDriverDoublehornRequestCmdTypeRestart       = 5,
    kLTDriverDoublehornRequestCmdTypeFwUpdate      = 6,
    kLTDriverDoublehornRequestCmdTypeFwUpdateChunk = 7,
    kLTDriverDoublehornRequestCmdTypeLog           = 8,
    kLTDriverDoublehornRequestCmdTypeAuthenticate  = 9,
    kLTDriverDoublehornRequestCmdTypeRestartReason = 10
} LTDriverDoublehornRequestCmdType;

/* Doublehorn accessory response status code */
typedef enum {
   kLTDriverDoublehornResponseStatusSuccess = 0,
   kLTDriverDoublehornResponseStatusError   = 1
} LTDriverDoublehornResponseStatus;

/* Doublehorn accessory log level*/
typedef enum {
    kLTDriverDoublehornLogLevelNone    = 0,
    kLTDriverDoublehornLogLevelError   = 1,
    kLTDriverDoublehornLogLevelWarning = 2,
    kLTDriverDoublehornLogLevelDebug   = 3,
    kLTDriverDoublehornLogLevelTrace   = 4
} LTDriverDoublehornLogLevel;

/* ____________________
/  static variables  */
static const LTTime kPollInterval         = LTTimeInitializer_Milliseconds(200);
static const LTTime kKeepAliveTime        = LTTimeInitializer_Seconds(30);
static const LTTime kConnectRetryInterval = LTTimeInitializer_Seconds(1);
static const LTTime kRequestInterval      = LTTimeInitializer_Milliseconds(2000);

/* Doublehorn accessory messagePack keys */
static const char kMessagePackKeyType[]                    = "type";
static const char kMessagePackKeyCommand[]                 = "command";
static const char kMessagePackKeyEndpoint[]                = "endpoint";
static const char kMessagePackKeyEventName[]               = "event_name";
static const char kMessagePackKeySetting[]                 = "setting";
static const char kMessagePackKeyValue[]                   = "value";
static const char kMessagePackKeyLogLevel[]                = "log_level";
static const char kMessagePackKeyUniqueId[]                = "id";
static const char kMessagePackKeyStatus[]                  = "status";
static const char kMessagePackKeyPayload[]                 = "payload";
static const char kMessagePackKeyEventMotion[]             = "motion_detected";
static const char kMessagePackKeyEventOverheated[]         = "overheated";
static const char kMessagePackKeyEventBrightnessChange[]   = "brightness_changed";
static const char kMessagePackKeyHash[]                    = "hash";
static const char kMessagePackKeyParameter[]               = "parameters";
static const char kMessagePackKeyHostName[]                = "host_name";
static const char kMessagePackKeyProtocolVersion[]         = "protocol_version";
static const char kMessagePackKeyHost[]                    = "ABC";
static const char kMessagePackKeyPlid[]                    = "plid";
static const char kMessagePackKeyAccessoryName[]           = "accessory_name";
static const char kMessagePackKeyFwVersion[]               = "fw_version";
static const char kMessagePackKeyDeviceId[]                = "device_id";
static const char kMessagePackKeyLogMessage[]              = "message";
static const char *kMessagePackKeySettings[]               = {"invalid", "brightness", "flash_frequency", "fade_time", "invalid",
                                                                                    "sensitivity", "zones", "cooldown_time", "invalid",
                                                                                    "boot_reason", "uptime"};
static const char *kMessagePackKeyEndpoints[]              = {"pir", "floodlight", "system"};
static const char *kDeviceBootReason[]                     = {"Hard_Reset", "Soft_Reset", "Watchdog_Reset", "Brown_Out", "Unknown"};
static const char *kMessagePackKeyLogLevelValue[]          = {"ERROR", "WARNING", "DEBUG", "TRACE"};

typedef struct ResponseContext {
    LTMessagePack_Obj *pMP;             /* The MessagePack being processed */
} ResponseContext;

static struct Statics {
    LTUtilityMessagePack *pMP;
    LTDeviceUSBSerial    *libUSBSerial;
    ILTThread *thread;
    LTThread   pollingThread;
    LTThread   reconnectThread;
    ILTEvent  *event;
    LTEvent    triggerEvent;
    LTMutex   *mutex;                       /* Mutex required for protecting serial port handle hPort */
    s32        hPort;
    bool       portBusy;
    char       pDevicePort[15];             /* /dev/ttyUSBx */
    u32        uniqueId;                    /* Request msgPack from host includes a unique id field, The client includes same id in its response */
                                            /* host uses this id to match responses to the original requests */
    u8         deviceId[kLTDriverDoublehornIdentityBufferLength];
    u8         firmwareVersion[kLTDriverDoublehornIdentityBufferLength];
    u64        protocolVersion;
    u8         accessoryName[kLTDriverDoublehornIdentityBufferLength];
    u8         accessoryPlid[kLTDriverDoublehornIdentityBufferLength];
} S;

static const LTArgsDescriptor s_triggerEventArgs = {1, {kLTArgType_u32}};

/* Buffer to store the event data from doublehorn */
static union {
    u8   brightness;
    bool overheated;
    bool motionDetected;
} s_EventData;

/* private helper functions  */
/** Thread proc to check doublehorn accessory availability on specific period of time
 *  kill timer on success
 */
static void DoublehornAccessoryDriver_ConnectDevice(void *clientData) {
    LT_UNUSED(clientData);
    if (S.libUSBSerial->API->CheckPort(S.libUSBSerial, (u8 *)S.pDevicePort)) {
        S.thread->KillTimer(S.reconnectThread, DoublehornAccessoryDriver_ConnectDevice, NULL);
        S.thread->Terminate(S.reconnectThread);
    }
}

static char MakeNybbleChar(u8 nybble) {
    return nybble > 9 ? nybble - 10 + 'A'
                      : nybble      + '0';
}

/* Logs the request/response messagePack for debug purpose */
static void DoublehornAccessoryDriver_LogMessagePack(const char *direction, u8 *pBuf, u32 bufLen) {
    LT_UNUSED(direction);
    enum { kMaxLogLineLength = 16, kMaxLogDump = kMaxLogLineLength * 15 }; /* limit to 15 lines as SDP incoming and outgoing data is huge */
    char *pHexdumpBuf = lt_malloc(kMaxLogLineLength * 3);  /* two chars, plus a space (or null at the end) per byte, per log line */
    if (pHexdumpBuf) {
        LTLOG_DEBUG("msg.l", "%s %lu bytes:", direction, LT_Pu32(bufLen));
        for (u32 nLineOffset = 0; bufLen && nLineOffset < kMaxLogDump; nLineOffset += kMaxLogLineLength) {
            char *pO = pHexdumpBuf;
            for (u32 nLineBytesRemaining = kMaxLogLineLength; nLineBytesRemaining && bufLen; --nLineBytesRemaining, --bufLen, ++pBuf) {
                *pO++ = MakeNybbleChar(*pBuf >> 4);
                *pO++ = MakeNybbleChar(*pBuf  & 0xF);
                *pO++ = ' ';
            }
            *--pO = '\0';
            LTLOG_DEBUG("msg.d", "%04lX: %s", LT_Pu32(nLineOffset), pHexdumpBuf);
        }
        lt_free(pHexdumpBuf);
    }
}

/** Thread release proc, open available USB port, notify connection event,
 *  set S.portBusy false
 */
static void DoublehornAccessoryDriver_ConnectionComplete(LTThread_ReleaseReason reason, void *clientData) {
    LT_UNUSED(reason);
    LT_UNUSED(clientData);
    S.libUSBSerial->API->OpenPort(S.libUSBSerial, S.pDevicePort, &S.hPort);
    S.event->NotifyEvent(S.triggerEvent, kLTDeviceFloodlightAccessory_Event_Connected);
    S.thread->Destroy(S.reconnectThread);
    S.reconnectThread = 0;
    S.portBusy = false;
}

/** Attempt to access the doublehorn accessory, if not succeed on
 * first try, then delegate connection task to thread
 */
static bool DoublehornAccessoryDriver_SetupDevice(void) {
    if (S.hPort) {
        S.libUSBSerial->API->ClosePort(S.libUSBSerial, &S.hPort);
        S.hPort = 0;
    }
    else {
        if (S.libUSBSerial->API->CheckPort(S.libUSBSerial, (u8 *)S.pDevicePort)) {
            S.libUSBSerial->API->OpenPort(S.libUSBSerial, S.pDevicePort, &S.hPort);
            S.portBusy = false;
            S.event->NotifyEvent(S.triggerEvent, kLTDeviceFloodlightAccessory_Event_Connected);
            return true;
        }
        return false;
    }
    S.portBusy = true;
    S.event->NotifyEvent(S.triggerEvent, kLTDeviceFloodlightAccessory_Event_Disconnected);
    S.reconnectThread = LT_GetCore()->CreateThread("ReconnectThread");
    S.thread->SetStackSize(S.reconnectThread, 1024);
    S.thread->Start(S.reconnectThread, NULL, NULL);
    S.thread->SetTimer(S.reconnectThread, kConnectRetryInterval, DoublehornAccessoryDriver_ConnectDevice, DoublehornAccessoryDriver_ConnectionComplete, NULL);
    return false;
}

/* Process events received from doublehorn accessory */
static void DoublehornAccessoryDriver_ProcessEvents(LTMessagePack_Obj *mp) {
    u32 length = 0;
    u8 *key = lt_malloc(kLTDriverDoublehornKeyBufferLength);
    if (!key) return;
    do {
        S.pMP->SetPosition(mp, kLTDriverDoublehornEventEndpointIndex);
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyEndpoint, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);        /* endpoint value read out of the msgpack stream is ignored intentionally, relying on event_name to derive the event */
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyEventName, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);
        LTDeviceFloodlightAccessory_Event eventName = kLTDeviceFloodlightAccessory_Event_Idle;
        if (lt_strncasecmp(kMessagePackKeyEventMotion, (const char *)key, length) == 0) {
            eventName = kLTDeviceFloodlightAccessory_Event_PirTrigger;
        } else if (lt_strncasecmp(kMessagePackKeyEventOverheated, (const char *)key, length) == 0) {
            eventName = kLTDeviceFloodlightAccessory_Event_Overheated;
        } else if (lt_strncasecmp(kMessagePackKeyEventBrightnessChange, (const char *)key, length) == 0) {
            eventName = kLTDeviceFloodlightAccessory_Event_BrightnessChange;
        }
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyValue, (const char *)key, length)) break;
        LTMessagePack_Value value;
        LTMessagePack_Type dataType = S.pMP->GetValue(mp, &value);
        if (dataType == LTMessagePack_Type_Boolean) {
            if (eventName == kLTDeviceFloodlightAccessory_Event_PirTrigger) s_EventData.motionDetected = value.boolean;
            else if (eventName == kLTDeviceFloodlightAccessory_Event_Overheated) s_EventData.overheated = value.boolean;
        } else if (dataType == LTMessagePack_Type_Integer) {
            s_EventData.brightness = value.integer;
        }
        S.event->NotifyEvent(S.triggerEvent, eventName);
    } while (false);

    lt_free(key);
}

/* Process handshake response from messagePack received from doublehorn accessory */
static bool DoublehornAccessoryDriver_GetHandshakeResponse(LTMessagePack_Obj *mp) {
    u32 mapLength = 0;
    u32 length = 0;
    u8 *key = lt_malloc(kLTDriverDoublehornKeyBufferLength);
    if (!key) return false;
    do {
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyPayload, (const char *)key, length)) break;
        LTMessagePack_Type tp = S.pMP->GetArray(mp, &mapLength);
        if (tp != LTMessagePack_Type_Map) {
            LTLOG_DEBUG("err.payload.res.handshake", "No payload map, found type: %d", tp);
            break;
        }
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyPlid, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);
        lt_strncpyTerm((char *)S.accessoryPlid, (char *)key, kLTDriverDoublehornIdentityBufferLength);
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyAccessoryName, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);
        lt_strncpyTerm((char *)S.accessoryName, (char *)key, kLTDriverDoublehornIdentityBufferLength);
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyFwVersion, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);
        lt_strncpyTerm((char *)S.firmwareVersion, (char *)key, kLTDriverDoublehornIdentityBufferLength);
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyDeviceId, (const char *)key, length)) break;
        S.pMP->GetString(mp, &key, &length);
        lt_strncpyTerm((char *)S.deviceId, (char *)key, kLTDriverDoublehornIdentityBufferLength);
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyProtocolVersion, (const char *)key, length)) break;
        S.pMP->GetInteger(mp, &S.protocolVersion);
        // TODO currently assumed the doublehorn accessory has endpoints "floodlight" & "pir"
        lt_free(key);
        return true;
    } while (false);

    lt_free(key);
    return false;
}

/* Gets response type from messagePack received from doublehorn accessory */
static LTDriverDoublehornMessageType DoublehornAccessoryDriver_GetResponseType(LTMessagePack_Obj *mp, u32 *identity, u8 *status) {
    LTDriverDoublehornMessageType typeKey = 0;
    u32 mapLength = 0;
    LTMessagePack_Type t = S.pMP->GetArray(mp, &mapLength);
    if (t != LTMessagePack_Type_Map) {
        LTLOG_YELLOWALERT("err.nomap", "No map, found type: %d", t);
        return false;
    }
    u32 length = 0;
    u8 *key = lt_malloc(kLTDriverDoublehornKeyBufferLength);
    if (!key) return false;
    do {
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyType, (const char *)key, length)) break;
        u64 keyValue = 0;
        S.pMP->GetInteger(mp, &keyValue);
        typeKey = keyValue;
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyUniqueId, (const char *)key, length)) break;
        S.pMP->GetInteger(mp, &keyValue);
        *identity = keyValue;
        S.pMP->GetString(mp, &key, &length);
        if (lt_strncasecmp(kMessagePackKeyStatus, (const char *)key, length)) break;
        S.pMP->GetInteger(mp, &keyValue);
        *status = keyValue;
    } while (false);

    lt_free(key);
    return typeKey;
}

/* Read messagePack from serialPort */
static bool DoublehornAccessoryDriver_ReadSerialPort(u32 identity, u8 cmdDirection, u8 *data, u8 readCount) {
    bool ret;
    u8 *readPayload = readPayload = lt_malloc(kLTDriverDoublehornMaxReadMessagePack);
    if (!readPayload) {
        LTLOG_YELLOWALERT("readserial.oom", "Failed to malloc for read buffer");
        return false;
    }
    u8 *key = NULL;
    LTMessagePack_Obj mp;
    if (!S.pMP->Init(&mp, readPayload, kLTDriverDoublehornMaxReadMessagePack)) {
        LTLOG_YELLOWALERT("f.mp.ini", "Failed to initialize MessagePack object");
        return false;
    }
    do {
        ret = S.libUSBSerial->API->ReadBytes(S.libUSBSerial, &S.hPort, readPayload, kLTDriverDoublehornMaxReadMessagePack);
        if (!ret) break;
        u32 responseId = 0;
        u8 status = 0;
        ResponseContext context = { .pMP = &mp };
        u8 typeKeyValue = DoublehornAccessoryDriver_GetResponseType(context.pMP, &responseId, &status);
        if (typeKeyValue != kLTDriverDoublehornMessageTypeResponse || responseId != identity || status != kLTDriverDoublehornResponseStatusSuccess) {
            LTLOG_DEBUG("f.response", "Invalid response from endpoint");
            if (readCount++ < kLTDriverDoublehornMaxReadRetry) DoublehornAccessoryDriver_ReadSerialPort(identity, cmdDirection, data, readCount);
            break;
        }
        if (cmdDirection == kLTDriverDoublehornRequestCmdTypeGet) {
            // read the payload received from endpoint
            u32 length = 0, mapLength;
            key = lt_malloc(kLTDriverDoublehornKeyBufferLength);
            if (!key) break;
            u64 keyValue;
            S.pMP->GetString(context.pMP, &key, &length);
            if (lt_strncasecmp(kMessagePackKeyPayload, (const char *)key, length)) break;
            LTMessagePack_Type tp = S.pMP->GetArray(context.pMP, &mapLength);
            if (tp != LTMessagePack_Type_Map) {
                LTLOG_DEBUG("err.payload.get.setting", "No payload map, found type: %d", tp);
                break;
            }
            S.pMP->GetString(context.pMP, &key, &length);
            if (lt_strncasecmp(kMessagePackKeyValue, (const char *)key, length)) break;
            S.pMP->GetInteger(context.pMP, &keyValue);
            *data = keyValue;
            lt_free(key);
        } else if (cmdDirection == kLTDriverDoublehornRequestCmdTypeHandshake) {
            DoublehornAccessoryDriver_GetHandshakeResponse(context.pMP);
        }
        lt_free(readPayload);
        S.pMP->Free(&mp);
        return ret;
    } while (false);

    LTLOG_YELLOWALERT("readserial.fail", "Failed to read serial port");
    lt_free(readPayload);
    S.pMP->Free(&mp);
    lt_free(key);
    return false;
}

/* Function to handle actual transaction between host and client doublehorn accessory*/
static bool DoublehornAccessoryDriver_USBReadWrite(u8 *pBuf, u32 writeLength, u32 identity, u8 cmdDirection, u8 *data) {
    bool ret = true;
    if (S.portBusy) return false;
    S.mutex->API->Lock(S.mutex);
    ret = S.libUSBSerial->API->WriteBytes(S.libUSBSerial, &S.hPort, pBuf, writeLength);
    if (ret) {
        u8 readCount = 1;
        ret = DoublehornAccessoryDriver_ReadSerialPort(identity, cmdDirection, data, readCount);
    }
    S.mutex->API->Unlock(S.mutex);
    /**Reconnect in case of failure */
    if (!ret) {
        DoublehornAccessoryDriver_SetupDevice();
    }
    return ret;
}

/* Creates request messagePack */
static bool LTEncodeMessagePack_Command(LTMessagePack_Obj *mp, u32 uniqueId, const char* endpointKey, const char* settingKey, u8 *value, u8 direction) {
    u8 totalKeyValuePairs = 6;
    if (direction == kLTDriverDoublehornRequestCmdTypeGet || direction == kLTDriverDoublehornRequestCmdTypeHandshake) totalKeyValuePairs = 5;
    else if (direction == kLTDriverDoublehornRequestCmdTypeSet) totalKeyValuePairs = 6;
    else if (direction == kLTDriverDoublehornRequestCmdTypePing || direction == kLTDriverDoublehornRequestCmdTypeAuthenticate
             || kLTDeviceFloodlightAccessory_Setting_Restart) totalKeyValuePairs = 3;
    else if (direction == kLTDriverDoublehornRequestCmdTypeLog) totalKeyValuePairs = 4;
    if (!S.pMP->Init(mp, NULL, kLTDriverDoublehornRequestBufferLength)) {
        return false;
    }
    S.pMP->PutMap(mp, totalKeyValuePairs);
    S.pMP->PutString(mp, kMessagePackKeyType, SIZEOF(kMessagePackKeyType));
    S.pMP->PutIntU32(mp, kLTDriverDoublehornMessageTypeRequest);
    S.pMP->PutString(mp, kMessagePackKeyUniqueId, SIZEOF(kMessagePackKeyUniqueId));
    S.pMP->PutIntU32(mp, uniqueId);
    S.pMP->PutString(mp, kMessagePackKeyCommand, SIZEOF(kMessagePackKeyCommand));
    S.pMP->PutIntU32(mp, direction);
    if (direction == kLTDriverDoublehornRequestCmdTypeGet || direction == kLTDriverDoublehornRequestCmdTypeSet) {
        S.pMP->PutString(mp, kMessagePackKeyEndpoint, SIZEOF(kMessagePackKeyEndpoint));
        S.pMP->PutString(mp, endpointKey, lt_strlen(endpointKey));
        S.pMP->PutString(mp, kMessagePackKeySetting, SIZEOF(kMessagePackKeySetting));
        S.pMP->PutString(mp, settingKey, lt_strlen(settingKey));
        if (direction == kLTDriverDoublehornRequestCmdTypeSet) {
            S.pMP->PutString(mp, kMessagePackKeyValue, SIZEOF(kMessagePackKeyValue));
            S.pMP->PutIntU32(mp, value[0]);
        }
    } else if (direction == kLTDriverDoublehornRequestCmdTypeLog) {
        S.pMP->PutString(mp, kMessagePackKeyLogLevel, SIZEOF(kMessagePackKeyLogLevel));
        S.pMP->PutIntU32(mp, value[0]);

    } else if (direction == kLTDriverDoublehornRequestCmdTypeHandshake) {
        S.pMP->PutString(mp, kMessagePackKeyHash, SIZEOF(kMessagePackKeyHash));
        if (!value) S.pMP->PutNil(mp);
        S.pMP->PutString(mp, kMessagePackKeyPayload, SIZEOF(kMessagePackKeyPayload));
        S.pMP->PutMap(mp, 1);
        S.pMP->PutString(mp, kMessagePackKeyParameter, SIZEOF(kMessagePackKeyParameter));
        S.pMP->PutMap(mp, 2);
        S.pMP->PutString(mp, kMessagePackKeyHostName, SIZEOF(kMessagePackKeyHostName));
        S.pMP->PutString(mp, kMessagePackKeyHost, SIZEOF(kMessagePackKeyHost));
        S.pMP->PutString(mp, kMessagePackKeyProtocolVersion, SIZEOF(kMessagePackKeyProtocolVersion));
        S.pMP->PutIntU32(mp, 1);

    }
    DoublehornAccessoryDriver_LogMessagePack("Outgoing", mp->head, (u32)(mp->next - mp->head));
    return true;
}

static u32 DoublehornAccessoryDriver_CreateCommand(LTMessagePack_Obj *mp, const char *setting, const char *endpoint, u8 *data, u32 dataLength, u8 cmdDirection) {
    LT_UNUSED(dataLength);
    u32 bufferLength = 0;
    S.uniqueId++;
    LTEncodeMessagePack_Command(mp, S.uniqueId, endpoint, setting, data, cmdDirection);
    bufferLength = S.pMP->GetPosition(mp);
    return bufferLength;
}

/* Set data to the setting to doublehorn endpoint */
static bool DoublehornAccessoryDriver_SetCommand(u8 endpoint, u8 setting, u8 *data, u32 dataLength) {
    LTMessagePack_Obj mp;
    // Message pack initialization
    if (!S.pMP->Init(&mp, NULL, kLTDriverDoublehornRequestBufferLength)) {
        LTLOG_YELLOWALERT("set.error", "Failed to initialize MessagePack object for setting %d", setting);
        return false;
    }
    char *settingKey = NULL, *endpointKey = NULL;
    do {
        settingKey = lt_malloc(kLTDriverDoublehornKeyBufferLength);
        if (!settingKey) break;
        endpointKey = lt_malloc(kLTDriverDoublehornKeyBufferLength);
        if (!endpointKey) break;
        u8 command = 0;
        // update settingKey and endpointKey for valid endpoint and setting
        switch (setting) {
            case kLTDeviceFloodlightAccessory_Setting_Brightness:
            case kLTDeviceFloodlightAccessory_Setting_FlashFrequency:
            case kLTDeviceFloodlightAccessory_Setting_FadeTime:
            case kLTDeviceFloodlightAccessory_Setting_Sensitivity:
            case kLTDeviceFloodlightAccessory_Setting_Zones:
            case kLTDeviceFloodlightAccessory_Setting_CooldownTime:
                lt_strncpyTerm(settingKey, kMessagePackKeySettings[setting], kLTDriverDoublehornKeyBufferLength);
                lt_strncpyTerm(endpointKey, kMessagePackKeyEndpoints[endpoint], kLTDriverDoublehornKeyBufferLength);
                command = kLTDriverDoublehornRequestCmdTypeSet;
                break;
            case kLTDeviceFloodlightAccessory_Setting_Ping:
                command = kLTDriverDoublehornRequestCmdTypePing;
                break;
            case kLTDeviceFloodlightAccessory_Setting_LogLevel:
                command = kLTDriverDoublehornRequestCmdTypeLog;
                *data = (LTDriverDoublehornLogLevel)*data;
                break;
            case kLTDeviceFloodlightAccessory_Setting_Authenticate:
                command = kLTDriverDoublehornRequestCmdTypeAuthenticate;
                break;
            case kLTDeviceFloodlightAccessory_Setting_Restart:
                command = kLTDriverDoublehornRequestCmdTypeRestart;
                break;
            case kLTDeviceFloodlightAccessory_Setting_Handshake:
                command = kLTDriverDoublehornRequestCmdTypeHandshake;
                break;
            default:
                LTLOG("set.f", "invalid setting");
                break;
        }
        u32 bufferLength = DoublehornAccessoryDriver_CreateCommand(&mp, settingKey, endpointKey, data, dataLength, command);
        bool ret = DoublehornAccessoryDriver_USBReadWrite(mp.head, bufferLength, S.uniqueId, command, NULL);
        if (!ret) {
            LTLOG_YELLOWALERT("set", "failed setting: %d", setting);
            break;
        }
        S.pMP->Free(&mp);
        lt_free(settingKey);
        lt_free(endpointKey);
        return true;
    } while (false);

    S.pMP->Free(&mp);
    lt_free(settingKey);
    lt_free(endpointKey);
    return false;
}

/* Get data of setting from doublehorn endpoint */
static bool DoublehornAccessoryDriver_GetCommand(u8 endpoint, u8 setting, u8 *data, u32 dataLength) {
    LTMessagePack_Obj mp;
    // Message pack initialization
    if (!S.pMP->Init(&mp, NULL, kLTDriverDoublehornRequestBufferLength)) {
        LTLOG_YELLOWALERT("get.error", "Failed to initialize MessagePack object for setting %d", setting);
        return false; // Initialization failure
    }
    // check if valid setting is requested
    if ((setting >= kLTDeviceFloodlightAccessory_Setting_Brightness && setting <= kLTDeviceFloodlightAccessory_Setting_CooldownTime) || 
        setting == kLTDeviceFloodlightAccessory_Setting_BootReason || setting == kLTDeviceFloodlightAccessory_Setting_Uptime) {
        u32 bufferLength = DoublehornAccessoryDriver_CreateCommand(&mp, kMessagePackKeySettings[setting], kMessagePackKeyEndpoints[endpoint],
                                                                                       data, dataLength, kLTDriverDoublehornRequestCmdTypeGet);
        bool ret = DoublehornAccessoryDriver_USBReadWrite(mp.head, bufferLength, S.uniqueId, kLTDriverDoublehornRequestCmdTypeGet, data);
        if (!ret) {
            LTLOG_YELLOWALERT("get", "failed setting: %s", kMessagePackKeySettings[setting]);
            S.pMP->Free(&mp);
            return false;
        }
    } else if (setting == kLTDeviceFloodlightAccessory_Setting_FwVersion) {
        lt_memcpy(data, S.firmwareVersion, sizeof(data));
    }
    // Freeing message pack object
    S.pMP->Free(&mp);
    return true;
}

static void DoublehornAccessoryDriver_KeepAlive(void *pClientData) {
    LT_UNUSED(pClientData);
    if (!DoublehornAccessoryDriver_SetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_Ping, 0, 0)) {
        LTLOG_YELLOWALERT("watchdog.missed", "failed to send watchdog feeder");
    }
}

static void DoublehornAccessoryDriver_OnStatusChange(LTDriverFloodlightAccessoryEventProc proc, void *pClientData) {
    S.event->RegisterForEvent(S.triggerEvent, proc, NULL, pClientData, false);
}

static bool DoublehornAccessoryDriver_NoStatusChange(LTDriverFloodlightAccessoryEventProc proc) {
    return S.event->UnregisterFromEvent(S.triggerEvent, proc);
}

static void DoublehornAccessoryDriver_PollingThread(void *pClientData) {
    LT_UNUSED(pClientData);
    lt_memset(&s_EventData, 0, sizeof(s_EventData));
    u8 *readPayload = lt_malloc(kLTDriverDoublehornValueBufferLength);
    if (!readPayload) return;
    u8 *key = NULL;
    LTMessagePack_Obj mp;
    do {
        S.mutex->API->Lock(S.mutex);
        bool ret = S.libUSBSerial->API->ReadBytes(S.libUSBSerial, &S.hPort, readPayload, 100);
        if (!ret) {
            S.mutex->API->Unlock(S.mutex);
            break;
        }
        S.mutex->API->Unlock(S.mutex);
        u32 identity;
        u8 status;
        if (!S.pMP->Init(&mp, readPayload, kLTDriverDoublehornRequestBufferLength)) break;
        lt_free(readPayload);
        ResponseContext context = { .pMP = &mp };
        LTDriverDoublehornMessageType responseType = DoublehornAccessoryDriver_GetResponseType(context.pMP, &identity, &status);
        if (responseType == kLTDriverDoublehornMessageTypeEvent) {
            LTLOG_DEBUG("res.poll.evt", "processing event %d", S.pMP->GetPosition(context.pMP));
            DoublehornAccessoryDriver_ProcessEvents(context.pMP);

        } else if (responseType == kLTDriverDoublehornMessageTypeLog) {
            u32 length = 0;
            key = lt_malloc(kLTDriverDoublehornKeyBufferLength);
            if (!key) break;
            S.pMP->SetPosition(context.pMP, kLTDriverDoublehornEventEndpointIndex);
            S.pMP->GetString(context.pMP, &key, &length);
            if (lt_strncasecmp(kMessagePackKeyLogLevel, (const char *)key, length)) break;
            u64 logLevel;
            S.pMP->GetInteger(context.pMP, &logLevel);
            S.pMP->GetString(context.pMP, &key, &length);
            S.pMP->GetString(context.pMP, &key, &length);
            S.pMP->GetString(context.pMP, &key, &length);
            if (lt_strncasecmp(kMessagePackKeyLogMessage, (const char *)key, length)) break;
            S.pMP->GetString(context.pMP, &key, &length);
            LTLOG_DEBUG("endpoint.log.i", "%s: %s", kMessagePackKeyLogLevelValue[logLevel - 1], key);
        }
    } while (false);

    lt_free(readPayload);
    //Freeing message pack object
    S.pMP->Free(&mp);
    lt_free(key);
}

static void FLEventDispatcher(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = NULL;
    LTDeviceFloodlightAccessory_Event eventType = (LTDeviceFloodlightAccessory_Event)LTArgs_u32At(0, args);
    if (eventType == kLTDeviceFloodlightAccessory_Event_PirTrigger) pEventData = &s_EventData.motionDetected;
    if (eventType == kLTDeviceFloodlightAccessory_Event_Overheated) pEventData = &s_EventData.overheated;
    if (eventType == kLTDeviceFloodlightAccessory_Event_BrightnessChange) pEventData = &s_EventData.brightness;
    (*(LTDriverFloodlightAccessoryEventProc)proc)(eventType, pEventData, pClientData);
}

static bool DoublehornAccessoryDriver_Start(void) {
    S.thread->SetTimer(S.pollingThread, kPollInterval, DoublehornAccessoryDriver_PollingThread, NULL, NULL);
    S.thread->SetTimer(S.pollingThread, kKeepAliveTime, DoublehornAccessoryDriver_KeepAlive, NULL, NULL);
    return true;
}

static void DoublehornAccessoryDriver_Handshake(void *clientData) {
    LT_UNUSED(clientData);
    DoublehornAccessoryDriver_SetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_Handshake, 0, 0);
    S.thread->KillTimer(S.pollingThread, DoublehornAccessoryDriver_Handshake, NULL);
}

static bool DoublehornAccessoryDriver_InitAccessory(void) {
    S.pollingThread = LT_GetCore()->CreateThread("DoublehornThread");
    S.thread->SetStackSize(S.pollingThread, 2048);        /* TODO tune thread stack size later */
    S.thread->Start(S.pollingThread, NULL, NULL);
    u8 data = 0;
    DoublehornAccessoryDriver_GetCommand(kLTDeviceFloodlightAccessory_Endpoint_System, kLTDeviceFloodlightAccessory_Setting_BootReason, &data, 1);
    LTLOG("boot.i", "Boot reason %s", kDeviceBootReason[data - 1]);
    // Handshake on establishing connection with accessory
    S.thread->SetTimer(S.pollingThread, kRequestInterval, DoublehornAccessoryDriver_Handshake, NULL, NULL);
    return true;
}

/* Library initialization and deinitialization */
static void DoublehornAccessoryDriver_LibFini(void) {
    if ((S.thread != NULL) && (S.pollingThread != 0)) {
        S.thread->KillTimer(S.pollingThread, DoublehornAccessoryDriver_PollingThread, NULL);
        S.thread->KillTimer(S.pollingThread, DoublehornAccessoryDriver_KeepAlive, NULL);
        S.thread->Terminate(S.pollingThread);
        S.thread->WaitUntilFinished(S.pollingThread, LTTime_Infinite());
        S.thread->Destroy(S.pollingThread);
    }
    if (S.event != NULL) S.event->Destroy(S.triggerEvent);
    if (S.mutex) lt_destroyobject(S.mutex);
    if (S.hPort) S.libUSBSerial->API->ClosePort(S.libUSBSerial, &S.hPort);
    S.hPort = 0;
    if (S.libUSBSerial) lt_destroyobject(S.libUSBSerial);
}

static bool DoublehornAccessoryDriver_LibInit(void) {
    LTDeviceConfig *deviceConfig = NULL;
    /* Initialise the statics */
    S = (struct Statics) {
        .portBusy = true
    };
    do {
        if (!(S.thread = lt_getlibraryinterface(ILTThread, LT_GetCore()))
            || !(S.event = lt_getlibraryinterface(ILTEvent, LT_GetCore()))
            || !(S.libUSBSerial = lt_createobject(LTDeviceUSBSerial))) {
            LTLOG_YELLOWALERT("f.init.iface", "Failed to get lib interface");
            break;
        }
        if (!(S.pMP = lt_openlibrary(LTUtilityMessagePack))) {
            LTLOG_YELLOWALERT("mp.no", "Message Pack init failed");
            break;
        }
        S.mutex = lt_createobject(LTMutex);
        if (!(S.mutex = lt_createobject(LTMutex))) {
            LTLOG_REDALERT("f.create.mutex", "Failed to create S.mutex");
            break;
        }
        deviceConfig = lt_openlibrary(LTDeviceConfig);
        u32 libSection = deviceConfig->GetDriverSection("LTDeviceFloodlightAccessory", "DoublehornAccessoryDriver");
        if (libSection == 0) {
            LTLOG_YELLOWALERT("open.fail", "failed to get library section for LTDeviceFLUnit");
            break;
        }
        S.triggerEvent = LT_GetCore()->CreateEvent(&s_triggerEventArgs, FLEventDispatcher, NULL, NULL, NULL);
        if (!S.triggerEvent) break;
        lt_memcpy(S.pDevicePort, (char *)deviceConfig->ReadString(libSection, "port"), sizeof(S.pDevicePort));
        lt_closelibrary(deviceConfig);
        return true;
    } while (0);

    lt_closelibrary(deviceConfig);
    DoublehornAccessoryDriver_LibFini();
    return false;
}
/*  _________________________________
 *  Object constructor and destructor
 */
static void DoublehornAccessoryDriver_DestructObject(DoublehornAccessoryDriver *accessory) {
    LT_UNUSED(accessory);
}

static bool DoublehornAccessoryDriver_ConstructObject(DoublehornAccessoryDriver *accessory) {
    LT_UNUSED(accessory);
    return true;
}

/* ____________________________________________
/  LTDriverPower object and library bindings */
define_LTObjectImplPublic(LTDriverFloodlightAccessory, DoublehornAccessoryDriver, 
    Start,
    InitAccessory,
    SetCommand,
    GetCommand,
    SetupDevice,
    OnStatusChange,
    NoStatusChange
);
define_LTObjectLibrary(1, DoublehornAccessoryDriver_LibInit, DoublehornAccessoryDriver_LibFini);
