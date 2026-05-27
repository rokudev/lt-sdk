/*******************************************************************************
 * lt/source/lt/device/flunit/LTDriverFLUnit.c
 *
 * LTDeviceFLUnit.c: Floodlight Accessory Unit Implementation
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>

#define BYTE_SIZE 1
#define INT_ARR_TO_CHAR(src, size)  \
    for (u8 i = 0; i < (size); ++i) {(src)[i] += '0';}

DEFINE_LTLOG_SECTION("device.flunit");

static ILTEvent  *s_iEvent;
static ILTThread *s_iLTThread;
static LTThread   s_hThread;

typedef struct {
    LTDriverLibrary *hDriver;
    ILTDriverFLUnit *iFLDriver;
    LTEvent          h_pirTriggerEvent;
    LTEvent          h_photoTriggerEvent;
    LTEvent          h_connectionEvent;
    bool             isMounted;
} FLUnitDriver;

FLUnitDriver instance;

// Command specific to Floodlight controls
enum {
    LT_FL_CMD_DEVICE_TYPE                          = 0x27,
    LT_FL_CMD_DEVICE_TYPE_RESP                     = 0x28,
    LT_FL_CMD_SEND_RANDOM_NUMBER                   = 0x32,
    LT_FL_CMD_SEND_RANDOM_NUMBER_RESP              = 0x33,
    LT_FL_CMD_MAC_ADDRESS                          = 0x34,
    LT_FL_CMD_MAC_ADDRESS_RESP                     = 0x35,
    LT_FL_CMD_SOFTWARE_VERSION                     = 0x3C,
    LT_FL_CMD_SOFTWARE_VERSION_RESP                = 0x3D,
    LT_FL_CMD_FIRMWARE_UPGRADE_STATE               = 0x3E,
    LT_FL_CMD_FIRMWARE_UPGRADE_STATE_RESP          = 0x3F,
    LT_FL_CMD_HARDWARE_VERSION                     = 0x40,
    LT_FL_CMD_HARDWARE_VERSION_RESP                = 0x41,
    LT_FL_CMD_AUTHENTICATION_SUCCESS               = 0x62,
    LT_FL_CMD_AUTHENTICATION_SUCCESS_RESP          = 0x63,
    LT_FL_CMD_GET_FLOODLIGHT_WORKING_MODE          = 0x64,
    LT_FL_CMD_GET_FLOODLIGHT_WORKING_MODE_RESP     = 0x65,
    LT_FL_CMD_SET_FLOODLIGHT_WORKING_MODE          = 0x66,
    LT_FL_CMD_SET_FLOODLIGHT_WORKING_MODE_RESP     = 0x67,
    LT_FL_CMD_SET_SCM_ACTIVE_RESTART               = 0x6A,
    LT_FL_CMD_SET_SCM_ACTIVE_RESTART_RESP          = 0x6B,
    LT_FL_CMD_GET_WATCHDOG_TIMEOUT_RESET_TIME      = 0xA0,
    LT_FL_CMD_GET_WATCHDOG_TIMEOUT_RESET_TIME_RESP = 0xA1,
    LT_FL_CMD_SET_WATCHDOG_TIMEOUT_RESET_TIME      = 0xA2,
    LT_FL_CMD_SET_WATCHDOG_TIMEOUT_RESET_TIME_RESP = 0xA3
};

/**Data lengths of some device specific commands **/
enum {
    kCmdLength_1  = 1,
    kCmdLength_2  = 2,
    kCmdLength_4  = 4,
    kCmdLength_6  = 6,
    kCmdLength_8  = 8,
    kCmdLength_16 = 16,
};

static const LTArgsDescriptor ConnectionEventArgs   = {1, {kLTArgType_u32}};
static const LTArgsDescriptor PirTriggerEventArgs   = {1, {kLTArgType_u32}};
static const LTArgsDescriptor PhotoTriggerEventArgs = {2, {kLTArgType_u32, kLTArgType_pointer}};

/**Floodlight unit last reboot reasons **/
static char *const s_restartReason[] = {
    "Electric reset",
    "Software reset",
    "Hardware watchdog reset",
    "Pin external input is reset",
    "Software watchdog (ping-pong) reset"
};

static bool LTDeviceFLUnitImpl_SetCommand(LTHandle *flunit, LTDriverFLUnitCmdType cmdType, u8 *data, u32 dataLength) {
    if (!flunit || !data) return false;
    return instance.iFLDriver->SetCommand(cmdType, data, dataLength);
}

static bool LTDeviceFLUnitImpl_GetCommand(LTHandle *flunit, LTDriverFLUnitCmdType cmdType, u8 *data, u32 dataLength) {
    if (!flunit || !data) return false;
    return instance.iFLDriver->GetCommand(cmdType, data, dataLength);
}

static bool LTDeviceFLUnitImpl_GetDeviceType(LTHandle *flunit, u8 *type) {
    if (!flunit || !type) return false;
    return instance.iFLDriver->GetCommand(LT_FL_CMD_DEVICE_TYPE, type, kCmdLength_1);
}

static bool LTDeviceFLUnitImpl_GetMacID(LTHandle *flunit, u8 *macId) {
    if (!flunit || !macId) return false;
    return instance.iFLDriver->GetCommand(LT_FL_CMD_MAC_ADDRESS, macId, kCmdLength_8);
}

static bool LTDeviceFLUnitImpl_AuthenticateDevice(LTHandle *flunit) {
    if (!flunit) return false;
    return instance.iFLDriver->SetCommand(LT_FL_CMD_AUTHENTICATION_SUCCESS, NULL, 0);
}

static bool LTDeviceFLUnitImpl_FirmwareVersion(LTHandle *flunit, u32 *version) {
    if (!flunit || !version) return false;
    u8 buf[4] = {0};
    if (!instance.iFLDriver->GetCommand(LT_FL_CMD_SOFTWARE_VERSION, (u8 *)buf, kCmdLength_4)) return false;
    INT_ARR_TO_CHAR(buf, 4);
    *version = lt_strtou32((const char *)buf, NULL, 0);
    return true;
}

static bool LTDeviceFLUnitImpl_HardwareVersion(LTHandle *flunit, u32 *version) {
    u8 buf[4] = {0};
    if (!flunit || !version) return false;
    if (!instance.iFLDriver->GetCommand(LT_FL_CMD_HARDWARE_VERSION, buf, kCmdLength_4)) return false;
    INT_ARR_TO_CHAR(buf, 4);
    *version = lt_strtou32((const char *)buf, NULL, 0);
    return true;
}

static bool LTDeviceFLUnitImpl_GetEncryptedKey(LTHandle *flunit, u8 *encryptedKey) {
    if (!flunit || !encryptedKey) return false;
    return instance.iFLDriver->SetGetCommand(LT_FL_CMD_SEND_RANDOM_NUMBER, encryptedKey, kCmdLength_16);
}

static bool LTDeviceFLUnitImpl_GetWatchdogTimer(LTHandle *flunit, u16 *watchdogTimer) {
    u8 buf[2] = {0};
    if (!flunit || !watchdogTimer) return false;
    if (!instance.iFLDriver->GetCommand(LT_FL_CMD_GET_WATCHDOG_TIMEOUT_RESET_TIME, buf, kCmdLength_2)) return false;
    *watchdogTimer = (((buf[1] & 0xff) << 8) | ((buf[0] & 0xff)));
    return true;
}

static bool LTDeviceFLUnitImpl_SetWatchdogTimer(LTHandle *flunit, u16 watchdogTimer) {
    if (!flunit) return false;
    u8 buf[2] = {0};
    buf[0] = (watchdogTimer >> 0) & 0xff;
    buf[1] = (watchdogTimer >> 8) & 0xff;
    return instance.iFLDriver->SetCommand(LT_FL_CMD_SET_WATCHDOG_TIMEOUT_RESET_TIME, buf, kCmdLength_2);
}

static bool LTDeviceFLUnitImpl_InitFLUnit(void) {
    u8 index;
    u8 buf[] = {60, 0};  // Setting watchdog for 60s
    bool success = instance.iFLDriver->SetCommand(LT_FL_CMD_SET_WATCHDOG_TIMEOUT_RESET_TIME, buf, kCmdLength_2);
    /**Logging the last restart reason **/
    if (instance.iFLDriver->GetCommand(LT_FL_CMD_GET_SCM_RESTART_REASON, &index, kCmdLength_1)) {
        LTLOG("init.reset.reason", "%s | %s", s_restartReason[index - 1], success ? "WDT Timer 60 sec" : "Failed to set WDT");
    }
    return success;
}

static bool LTDeviceFLUnitImpl_SCMRestart(LTHandle *flunit) {
    if (!flunit) return false;
    if (!instance.iFLDriver->SetCommand(LT_FL_CMD_SET_SCM_ACTIVE_RESTART, NULL, 0)) return false;
    return true;
}

static bool LTDeviceFLUnitImpl_SCMRestartReason(LTHandle *flunit, u8 *reason) {
    if (!flunit || !reason) return false;
    return instance.iFLDriver->GetCommand(LT_FL_CMD_GET_SCM_RESTART_REASON, reason, kCmdLength_1);
}

static void LTDeviceFLUnitImpl_OnPIRTrigger(LTHandle *flunit, LTDeviceFLUnitEventProc proc, void *pClientData) {
    LT_UNUSED(flunit);
    if (!proc) return;
    s_iEvent->RegisterForEvent(instance.h_pirTriggerEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFLUnitImpl_NoPIRTrigger(LTHandle *flunit, LTDeviceFLUnitEventProc proc) {
    LT_UNUSED(flunit);
    return s_iEvent->UnregisterFromEvent(instance.h_pirTriggerEvent, proc);
}

static void LTDeviceFLUnitImpl_OnConnectionChange(LTHandle *flunit, LTDeviceFLUnitEventProc proc, void *pClientData) {
    LT_UNUSED(flunit);
    if (!proc) return;
    return s_iEvent->RegisterForEvent(instance.h_connectionEvent, proc, NULL, pClientData, false);
}

static bool LTDeviceFLUnitImpl_NoConnectionChange(LTHandle *flunit, LTDeviceFLUnitEventProc proc) {
    LT_UNUSED(flunit);
    return s_iEvent->UnregisterFromEvent(instance.h_connectionEvent, proc);
}

static void LTDeviceFLUnitImpl_OnStatusChange(LTDriverFLUnitEvent event, void *pEventData, void *pClientData) {
    LT_UNUSED(pClientData);
    switch (event) {
        case kLTDriverFLUnitEvent_PirTrigger:
            s_iEvent->NotifyEvent(instance.h_pirTriggerEvent, event);
            break;
        case kLTDriverFLUnitEvent_PhotoSensorTrigger:
            s_iEvent->NotifyEvent(instance.h_photoTriggerEvent, event, pEventData);
            break;
        case kLTDriverFLUnitEvent_Connected:
            if (!LTDeviceFLUnitImpl_InitFLUnit()) break;
            s_iEvent->NotifyEvent(instance.h_connectionEvent, event);
            break;
        case kLTDriverFLUnitEvent_Disconnected:
            s_iEvent->NotifyEvent(instance.h_connectionEvent, event);
            break;
        default:
            break;
    }
}

static void LTDeviceFLUnitImpl_DispatchConnectionEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = NULL;
    LTDriverFLUnitEvent eventType = (LTDriverFLUnitEvent)LTArgs_u32At(0, args);
    (*(LTDeviceFLUnitEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDeviceFLUnitImpl_DispatchPIREvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = NULL;
    LTDriverFLUnitEvent eventType = (LTDriverFLUnitEvent)LTArgs_u32At(0, args);
    (*(LTDeviceFLUnitEventProc)proc)(eventType, pEventData, pClientData);
}

static void LTDeviceFLUnitImpl_DispatchPhotoEvent(LTEvent event, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(event);
    if (!proc) return;
    void *pEventData = LTArgs_pointerAt(1, args);
    LTDriverFLUnitEvent eventType  = (LTDriverFLUnitEvent)LTArgs_u32At(0, args);
    (*(LTDeviceFLUnitEventProc)proc)(eventType, pEventData, pClientData);
}

static bool LTDeviceFLUnitImpl_IsMounted(LTHandle *flunit) {
    if (!flunit) return false;
    return instance.isMounted;
}

static bool OnStartThread(void) {
    instance.iFLDriver->OnStatusChange(LTDeviceFLUnitImpl_OnStatusChange, (void *)instance.iFLDriver);
    return true;
}

static void OnEndThread(void) {
    instance.iFLDriver->NoStatusChange(LTDeviceFLUnitImpl_OnStatusChange);
}

static u32 LTDeviceFLUnitImpl_GetNumDeviceUnits(void) {
    return 1;
}

static LTHandle LTDeviceFLUnitImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    return LT_GetCore()->CreateHandle((LTInterface *)instance.iFLDriver, sizeof(FLUnitDriver *));
}

static void LTDeviceFLUnitImpl_LibFini(void) {
    if (instance.h_photoTriggerEvent) s_iEvent->Destroy(instance.h_photoTriggerEvent);
    if (instance.h_pirTriggerEvent)   s_iEvent->Destroy(instance.h_pirTriggerEvent);
    if (instance.h_connectionEvent)   s_iEvent->Destroy(instance.h_connectionEvent);
    if (s_hThread) {
        s_iLTThread->Terminate(s_hThread);
        s_iLTThread->WaitUntilFinished(s_hThread, LTTime_Infinite());
        s_iLTThread->Destroy(s_hThread);
    }
    lt_closelibrary(instance.hDriver);
}

static bool LTDeviceFLUnitImpl_LibInit(void) {
    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (deviceConfig) {
            const char *pLibraryName = deviceConfig->GetDriverAt("LTDeviceFLUnit", 0);
            if (!(instance.hDriver = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pLibraryName))) {
                LTLOG_YELLOWALERT("init.lib", "Failed to open library %s", pLibraryName);
                lt_closelibrary(deviceConfig);
                break;
            }
            if (!(instance.iFLDriver = lt_getlibraryinterface(ILTDriverFLUnit, instance.hDriver))) {
                LTLOG_YELLOWALERT("init.iface", "Failed to get interface %s", pLibraryName);
                lt_closelibrary(deviceConfig);
                break;
            }
            lt_closelibrary(deviceConfig);
        }
        instance.isMounted = instance.iFLDriver->SetupDevice();
        if (!instance.isMounted || !LTDeviceFLUnitImpl_InitFLUnit()) break;
        instance.iFLDriver->Start();

        instance.h_pirTriggerEvent = LT_GetCore()->CreateEvent(&PirTriggerEventArgs,
                                                               LTDeviceFLUnitImpl_DispatchPIREvent,
                                                               NULL, NULL, NULL);
        if (!instance.h_pirTriggerEvent) break;
        instance.h_photoTriggerEvent = LT_GetCore()->CreateEvent(&PhotoTriggerEventArgs,
                                                                LTDeviceFLUnitImpl_DispatchPhotoEvent,
                                                                NULL, NULL, NULL);
        if (!instance.h_photoTriggerEvent) break;
        instance.h_connectionEvent = LT_GetCore()->CreateEvent(&ConnectionEventArgs,
                                                                LTDeviceFLUnitImpl_DispatchConnectionEvent,
                                                                NULL, NULL, NULL);
        if (!instance.h_connectionEvent) break;
        s_iLTThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
        if (s_iLTThread == NULL) {
            LTLOG_YELLOWALERT("init.thread", "Failed to get IThread interface");
            break;
        }
        s_hThread = LT_GetCore()->CreateThread("devFLUnitEvent");
        if (s_hThread == 0) {
            LTLOG_YELLOWALERT("init.create.thread", "Failed to create the device thread");
            break;
        }
        s_iLTThread->Start(s_hThread, OnStartThread, OnEndThread);
        s_iEvent = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    	return true;
    } while (0);

    LTDeviceFLUnitImpl_LibFini();
    return false;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFLUnit) {
    .GetDeviceType      = &LTDeviceFLUnitImpl_GetDeviceType,
    .GetMacID           = &LTDeviceFLUnitImpl_GetMacID,
    .AuthenticateDevice = &LTDeviceFLUnitImpl_AuthenticateDevice,
    .FirmwareVersion    = &LTDeviceFLUnitImpl_FirmwareVersion,
    .HardwareVersion    = &LTDeviceFLUnitImpl_HardwareVersion,
    .GetEncryptedKey    = &LTDeviceFLUnitImpl_GetEncryptedKey,
    .GetWatchdogTimer   = &LTDeviceFLUnitImpl_GetWatchdogTimer,
    .SetWatchdogTimer   = &LTDeviceFLUnitImpl_SetWatchdogTimer,
    .SCMRestart         = &LTDeviceFLUnitImpl_SCMRestart,
    .SCMRestartReason   = &LTDeviceFLUnitImpl_SCMRestartReason,
    .SetCommand         = &LTDeviceFLUnitImpl_SetCommand,
    .GetCommand         = &LTDeviceFLUnitImpl_GetCommand,
    .OnConnectionChange = &LTDeviceFLUnitImpl_OnConnectionChange,
    .NoConnectionChange = &LTDeviceFLUnitImpl_NoConnectionChange,
    .OnPIRTrigger       = &LTDeviceFLUnitImpl_OnPIRTrigger,
    .NoPIRTrigger       = &LTDeviceFLUnitImpl_NoPIRTrigger,
    .IsMounted          = &LTDeviceFLUnitImpl_IsMounted,
} LTLIBRARY_DEFINITION;
