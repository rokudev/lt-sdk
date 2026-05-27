/*******************************************************************************
 * lt/source/lt/device/ble/LTDeviceBle.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/ble/LTDeviceBle.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.ble");
#define PLOG(...) LTLOG(__VA_ARGS__)
#define P(...)
#define PSERVER(...) LTLOG_SERVER(__VA_ARGS__)

static struct Statics {
    LTDriverLibrary  *driver;
    LTDeviceUnit      hDevHost;
    ILTHostBle       *host;
} S;


/*******************************************************************************
 * Interface implementation
*******************************************************************************/

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static bool LTDeviceBle_SetService(LTBleSvcHandle hSvc) {
    if (S.host) return S.host->SetService(hSvc);
    return false;
}

// TODO: if stopped, only restart after reboot.
static void LTDeviceBle_Stop(void) {
    if (S.host) S.host->Stop();
}

// create ble thread and start advertising
static bool LTDeviceBle_Start(LTBleDeviceHandle hDevice) {
    if (!S.host) return false;
    bool ret = S.host->Start(hDevice);
    if (!ret) LTLOG_REDALERT("host.err", "fail to start ble host");
    return ret;
}

static bool LTDeviceBle_StartAdvertise(LTBleDeviceHandle hDevice, LTBleAdvFields* advFields, LTBleAdvFields* rspFields, LTBleAdvParams* advParam) {
    if (S.host) {
        return S.host->StartAdvertise(hDevice, advFields, rspFields, advParam);
    } else {
        return false;
    }
}

static bool LTDeviceBle_DirectedAdvertise(LTBleDeviceHandle hDevice, LTMacAddress peer) {
    if (S.host) {
        return S.host->StartDirectedAdvertise(hDevice, peer);
    } else {
        return false;
    }
}

static void LTDeviceBle_StopAdvertise(void) {
    if (S.host) S.host->StopAdvertise();
}

static bool LTDeviceBle_Indicate(LTBleDeviceHandle hDevice, LTBleChrHandle hChr) {
    if (!S.host) return false;
    return S.host->Indicate(hDevice, hChr);
}

static bool LTDeviceBle_Notify(LTBleDeviceHandle hDevice, LTBleChrHandle hChr) {
    if (!S.host) return false;
    return S.host->Notify(hDevice, hChr);
}

static LTDeviceBle_State LTDeviceBle_GetState (void) {
    if (!S.host) return kLTDeviceBle_State_Off;
    return S.host->GetState();
}

static void LTDeviceBle_GetOwnAddress(LTMacAddress *addr) {
    if (S.host) S.host->GetOwnAddress(addr);
}

static void LTDeviceBle_GetPeerAddress(LTMacAddress *addr) {
    if (S.host) S.host->GetPeerAddress(addr);
}

static s8 LTDeviceBle_GetRSSI(u16 connHandle, s8* rssi) {
    if (S.host) return S.host->GetRSSI(connHandle, rssi);
    return -1;
}

static bool LTDeviceBle_SecurityInitiate(LTBleDeviceHandle hDevice) {
    if (!S.host) return false;
    return S.host->SecurityInitiate(hDevice);
}

static bool LTDeviceBle_Reset(s16 reason) {
    if (!S.host) return false;
    return S.host->Reset(reason);
}

static s16 LTDeviceBle_GetControllerResetReason(void) {
    if (!S.host) return 0;
    return S.host->GetControllerResetReason();
}

static bool LTDeviceBle_GetConnectionInfo(u16 connHandle, LTBleConnectionInfo *info) {
    if (!S.host || !S.host->GetConnectionInfo) return false;
    return S.host->GetConnectionInfo(connHandle, info);
}

static bool LTDeviceBle_RegisterPktTimestamp(u16 att_handle, u16 tag_offset,
        u8 tag_len, LTBlePktTimestampCb cb, void *userData) {
    if (!S.host || !S.host->RegisterPktTimestamp) return false;
    return S.host->RegisterPktTimestamp(att_handle, tag_offset, tag_len, cb, userData);
}

static bool LTDeviceBle_UnregisterPktTimestamp(u16 att_handle) {
    if (!S.host || !S.host->UnregisterPktTimestamp) return false;
    return S.host->UnregisterPktTimestamp(att_handle);
}

// TODO: if unloaded, only reload after reboot.
static void LTDeviceBleImpl_LibFini(void) {
    LTDeviceBle_Stop();
    lt_destroyhandle(S.hDevHost);
    lt_closelibrary(S.driver);
    S = (struct Statics){};
}

static bool LTDeviceBleImpl_LibInit(void) {
    S = (struct Statics){};
    do {
        S.driver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceBle", 0);
        if (!S.driver) break;
        S.hDevHost = S.driver->CreateDeviceUnitHandle(0);
        if (!S.hDevHost) break;
        S.host = lt_gethandleinterface(ILTHostBle, S.hDevHost);
        return true;
    } while (0);

    LTLOG_REDALERT("init.fail", "fail to load ble device");
    LTDeviceBleImpl_LibFini();
    return false;
}

static u32 LTDeviceBleImpl_GetNumDeviceUnits(void) {
    return S.driver->GetNumDeviceUnits();
}

static LTDeviceUnit LTDeviceBleImpl_CreateDeviceUnitHandle(u32 deviceUnitNumber) {
    return S.driver->CreateDeviceUnitHandle(deviceUnitNumber);
}

static bool LTDeviceBle_SendHciCommand(const u16 opcode, const u8 *cmd, u32 len,  u8* resp, u32 respLen) {
    if (!S.host) return false;
    return S.host->SendHciCommand(opcode, cmd, len, resp, respLen);
}

static bool LTDeviceBle_SendPrivVenCommand(const char *cmd, u8* resp, u32 respLen) {
    if (!S.host) return false;
    return S.host->SendPrivVenCommand(cmd, resp, respLen);
}

static void LTDeviceBle_EnableLogging(bool en) {
    if (!S.host) return;
    return S.host->EnableLogging(en);
}

static const char* LTDeviceBle_GetDeviceName(void) {
    if (!S.host) return false;
    return S.host->GetDeviceName();
}

static bool LTDeviceBle_SetMaxTransmitParams(LTBleDeviceCtx *devCtx, u16 maxTxOctets, u16 maxTxTime) {
    if (!S.host) return false;
    return S.host->SetMaxTransmitParams(devCtx, maxTxOctets, maxTxTime);
}

static bool LTDeviceBle_UpdateConnectionParams(LTBleDeviceHandle hDevice, LTBleUpdConnParams* updConnParams) {
    if (!S.host) return false;
    return S.host->UpdateConnectionParams(hDevice, updConnParams);
}

static bool LTDeviceBle_Disconnect(LTBleDeviceHandle hDevice) {
    if (!S.host) return false;
    return S.host->Disconnect(hDevice);
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/
define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceBle)
    .SetService              = &LTDeviceBle_SetService,
    .Start                   = &LTDeviceBle_Start,
    .Stop                    = &LTDeviceBle_Stop,
    .StartAdvertise          = &LTDeviceBle_StartAdvertise,
    .StartDirectedAdvertise  = &LTDeviceBle_DirectedAdvertise,
    .StopAdvertise           = &LTDeviceBle_StopAdvertise,
    .Indicate                = &LTDeviceBle_Indicate,
    .Notify                  = &LTDeviceBle_Notify,
    .GetState                = &LTDeviceBle_GetState,
    .SendHciCommand          = &LTDeviceBle_SendHciCommand,
    .SendPrivVenCommand      = &LTDeviceBle_SendPrivVenCommand,
    .GetOwnAddress           = &LTDeviceBle_GetOwnAddress,
    .GetPeerAddress          = &LTDeviceBle_GetPeerAddress,
    .GetRSSI                 = &LTDeviceBle_GetRSSI,
    .EnableLogging           = &LTDeviceBle_EnableLogging,
    .GetDeviceName           = &LTDeviceBle_GetDeviceName,
    .SetMaxTransmitParams    = &LTDeviceBle_SetMaxTransmitParams,
    .UpdateConnectionParams  = &LTDeviceBle_UpdateConnectionParams,
    .SecurityInitiate        = &LTDeviceBle_SecurityInitiate,
    .Disconnect              = &LTDeviceBle_Disconnect,
    .Reset                   = &LTDeviceBle_Reset,
    .GetControllerResetReason= &LTDeviceBle_GetControllerResetReason,
    .GetConnectionInfo       = &LTDeviceBle_GetConnectionInfo,
    .RegisterPktTimestamp    = &LTDeviceBle_RegisterPktTimestamp,
    .UnregisterPktTimestamp  = &LTDeviceBle_UnregisterPktTimestamp,

LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Sep-22   vespasian   created
 *  21-Dec-22   gallienus   restructured and cleaned
 *  03-Apr-23   augustus    load driver from LTDeviceConfig
 */
