/*******************************************************************************
 * lt/source/lt/device/mipidsi/LTDeviceMipiDsiImpl.c
 *
 * LT Device Library for MIPI-DSI display host controllers.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTMutex.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/mipidsi/LTDeviceMipiDsi.h>
#include <lt/driver/mipidsi/LTDriverMipiDsi.h>

/*___________________________
  LTDeviceMipiDsi #defines */
DEFINE_LTLOG_SECTION("lt.dev.mipidsi");

                             /*  \|/  */
#define LTDEVICEMIPIDSI_DO_DLOG  (0)  /* ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE MERGING */
                             /*  /|\  */
#if     LTDEVICEMIPIDSI_DO_DLOG
#define DLOG                     LTLOG
#else
#define DLOG                     LTLOG_LOGNULL
#endif

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*______________________________
  LTDeviceMipiDsiImpl constants */
static const LTArgsDescriptor s_mipiDsiErrorEventArgs = { 1, { kLTArgType_u32 } };

/*_________________________________________________
  typedef_LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDeviceMipiDsi, LTDeviceMipiDsiImpl) {
    LTDriverMipiDsi        *driver;
    LTMutex                *transferMutex;    /* serializes packet transfers so panel drivers may call from any thread */
    ILTEvent               *iEvent;
    LTEvent                 hEvent;
    LTDeviceMipiDsi_Config  config;
    LTAtomic                pendingErrors;    /* errors seen by the ISR, not yet dispatched */
    LTAtomic                accumulatedErrors;/* errors since the last GetErrors(bClear = true) */
    bool                    bConfigured;
    bool                    bPoweredOn;
    bool                    bVideoActive;
    bool                    bFrameBufferSet;
    bool                    bCommandsHighSpeed;
} LTOBJECT_API;

/*_______________________________________
  LTDeviceMipiDsiImpl private functions */

static void LTDeviceMipiDsiImpl_ErrorEventDispatchProc(LTEvent hEvent, void *proc, LTArgs *args, void *pClientData) {
    LT_UNUSED(hEvent);
    ((LTDeviceMipiDsi_ErrorEventProc *)proc)((LTDeviceMipiDsi_Error)LTArgs_u32At(0, args), pClientData);
}

static void LTDeviceMipiDsiImpl_StaticNotifyErrorProc(void *pClientData) LT_ISR_SAFE {
    LTDeviceMipiDsiImpl *dsi = (LTDeviceMipiDsiImpl *)pClientData;
    u32 errors = LTAtomic_Exchange(&dsi->pendingErrors, 0);
    if (errors) dsi->iEvent->NotifyEvent(dsi->hEvent, errors);
}

static void LTDeviceMipiDsiImpl_StaticErrorProc(LTDeviceMipiDsi_Error errors, void *pClientData) LT_ISR_SAFE {
    /* this procedure is running in ISR context */
    LTDeviceMipiDsiImpl *dsi = (LTDeviceMipiDsiImpl *)pClientData;
    if (! errors) return;
    LTAtomic_FetchOr(&dsi->accumulatedErrors, (u32)errors);
    /* only the transition from "nothing pending" to "something pending" needs a notification queued */
    if (0 == LTAtomic_FetchOr(&dsi->pendingErrors, (u32)errors)) {
        dsi->iEvent->NotifyEventFromISR(&LTDeviceMipiDsiImpl_StaticNotifyErrorProc, pClientData);
    }
}

static bool LTDeviceMipiDsiImpl_IsVideoMode(const LTDeviceMipiDsi_Config *pConfig) {
    return (kLTDeviceMipiDsi_Mode_VideoSyncPulse == pConfig->mode)
        || (kLTDeviceMipiDsi_Mode_VideoSyncEvent == pConfig->mode)
        || (kLTDeviceMipiDsi_Mode_VideoBurst     == pConfig->mode);
}

static bool LTDeviceMipiDsiImpl_IsConfigValid(const LTDeviceMipiDsi_Config *pConfig) {
    if (kLTDeviceMipiDsi_Mode_Unset == pConfig->mode) return false;
    if (kLTDeviceMipiDsi_PixelFormat_Unset == pConfig->pixelFormat) return false;
    if ((0 == pConfig->nDataLanes) || (pConfig->nDataLanes > kLTDeviceMipiDsi_MaxLanes)) return false;
    if (pConfig->nVirtualChannel > kLTDeviceMipiDsi_MaxVirtualChannel) return false;
    if ((0 == pConfig->timing.nHorizontalActive) || (0 == pConfig->timing.nVerticalActive)) return false;
    /* a video mode panel is driven by timing rather than by explicit frame writes, so it needs a refresh rate */
    if (LTDeviceMipiDsiImpl_IsVideoMode(pConfig) && (0 == pConfig->timing.nFrameRateHz)) return false;
    return true;
}

static bool LTDeviceMipiDsiImpl_SendPacketLocked(LTDeviceMipiDsiImpl *dsi, LTDriverMipiDsi_PacketType packetType, const u8 *pPayload, u32 nPayloadBytes) {
    LTDriverMipiDsi_Packet packet;
    lt_memset(&packet, 0, sizeof(packet));
    packet.packetType      = packetType;
    packet.nVirtualChannel = dsi->config.nVirtualChannel;
    packet.pPayload        = pPayload;
    packet.nPayloadBytes   = nPayloadBytes;
    packet.bHighSpeed      = dsi->bCommandsHighSpeed;
    return dsi->driver->API->SendPacket(dsi->driver, &packet);
}

/*____________________________________
  LTDeviceMipiDsiImpl constructors  */

static void LTDeviceMipiDsiImpl_DestructObject(LTDeviceMipiDsiImpl *dsi) {
    DLOG("destruct", NULL);
    if (dsi->driver) {
        dsi->driver->API->SetErrorProc(dsi->driver, NULL, NULL);
        if (dsi->bVideoActive) dsi->driver->API->StopVideo(dsi->driver);
        if (dsi->bPoweredOn)   dsi->driver->API->PowerOff(dsi->driver);
    }
    lt_destroyobject(dsi->driver);
    lt_destroyobject(dsi->transferMutex);
    lt_destroyhandle(dsi->hEvent);
}

static bool LTDeviceMipiDsiImpl_ConstructObject(LTDeviceMipiDsiImpl *dsi) {
    do {
        DLOG("construct.start", NULL);
        if (NULL == (dsi->driver = lt_createdriverobject_fordevice(LTDriverMipiDsi, dsi))) {
            LTLOG_REDALERT("construct.fail.driver", "Failed to create driver object");
            break;
        }
        if (NULL == (dsi->transferMutex = lt_createobject(LTMutex))) {
            LTLOG_REDALERT("construct.fail.mutex", "Failed to create transfer mutex");
            break;
        }
        if (LTHANDLE_INVALID == (dsi->hEvent = LT_GetCore()->CreateEvent(&s_mipiDsiErrorEventArgs, &LTDeviceMipiDsiImpl_ErrorEventDispatchProc, NULL, NULL, NULL))) {
            LTLOG_REDALERT("construct.fail.event", "Failed to create error event");
            break;
        }
        dsi->iEvent = lt_gethandleinterface(ILTEvent, dsi->hEvent);
        dsi->bCommandsHighSpeed = false;   /* panel init sequences require low power escape mode */
        dsi->driver->API->SetErrorProc(dsi->driver, &LTDeviceMipiDsiImpl_StaticErrorProc, dsi);
        DLOG("construct.success", NULL);
        return true;
    } while (false);
    lt_destroyobject(dsi->driver);
    lt_destroyobject(dsi->transferMutex);
    lt_destroyhandle(dsi->hEvent);
    return false;
}

/*____________________________________
  LTDeviceMipiDsiImpl API functions */

static bool LTDeviceMipiDsiImpl_Configure(LTDeviceMipiDsiImpl *dsi, const LTDeviceMipiDsi_Config *pConfig) {
    if (! pConfig) return false;
    if (dsi->bPoweredOn) {
        LTLOG_YELLOWALERT("configure.poweredon", "Configure called while the link is powered on");
        return false;
    }
    if (! LTDeviceMipiDsiImpl_IsConfigValid(pConfig)) {
        LTLOG_YELLOWALERT("configure.invalid", "mode=%lu fmt=%lu lanes=%lu vc=%lu",
                          LT_Pu32((u32)pConfig->mode), LT_Pu32((u32)pConfig->pixelFormat),
                          LT_Pu32(pConfig->nDataLanes), LT_Pu32(pConfig->nVirtualChannel));
        return false;
    }
    if (! dsi->driver->API->Configure(dsi->driver, pConfig)) {
        LTLOG_YELLOWALERT("configure.driver.reject", NULL);
        return false;
    }
    dsi->config = *pConfig;
    dsi->bFrameBufferSet = false;   /* the geometry may have changed under it */
    dsi->bConfigured = true;
    return true;
}

static bool LTDeviceMipiDsiImpl_GetConfig(LTDeviceMipiDsiImpl *dsi, LTDeviceMipiDsi_Config *pConfigOut) {
    if (! pConfigOut || ! dsi->bConfigured) return false;
    *pConfigOut = dsi->config;
    return true;
}

static bool LTDeviceMipiDsiImpl_PowerOn(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bConfigured) {
        LTLOG_YELLOWALERT("poweron.unconfigured", NULL);
        return false;
    }
    if (dsi->bPoweredOn) return true;
    if (! dsi->driver->API->PowerOn(dsi->driver)) {
        LTLOG_YELLOWALERT("poweron.fail", NULL);
        return false;
    }
    dsi->bPoweredOn = true;
    return true;
}

static void LTDeviceMipiDsiImpl_PowerOff(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bPoweredOn) return;
    if (dsi->bVideoActive) {
        dsi->driver->API->StopVideo(dsi->driver);
        dsi->bVideoActive = false;
    }
    dsi->driver->API->PowerOff(dsi->driver);
    dsi->bPoweredOn = false;
}

static bool LTDeviceMipiDsiImpl_IsPoweredOn(LTDeviceMipiDsiImpl *dsi) { return dsi->bPoweredOn; }

static bool LTDeviceMipiDsiImpl_SetFrameBuffer(LTDeviceMipiDsiImpl *dsi, const void *pPixels, u32 nBytes) {
    if (! LTDeviceMipiDsiImpl_IsVideoMode(&dsi->config)) {
        LTLOG_YELLOWALERT("setframebuffer.commandmode", NULL);
        return false;
    }
    if (dsi->bVideoActive) {
        LTLOG_YELLOWALERT("setframebuffer.videoactive", NULL);
        return false;
    }
    if (pPixels && ! nBytes) return false;
    if (! dsi->driver->API->SetFrameBuffer(dsi->driver, pPixels, nBytes)) return false;
    dsi->bFrameBufferSet = (NULL != pPixels);
    return true;
}

static bool LTDeviceMipiDsiImpl_StartVideo(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bPoweredOn) {
        LTLOG_YELLOWALERT("startvideo.poweredoff", NULL);
        return false;
    }
    if (! LTDeviceMipiDsiImpl_IsVideoMode(&dsi->config)) {
        LTLOG_YELLOWALERT("startvideo.commandmode", NULL);
        return false;
    }
    if (dsi->bVideoActive) return true;
    if (! dsi->bFrameBufferSet) {
        LTLOG_YELLOWALERT("startvideo.noframebuffer", NULL);
        return false;
    }
    if (! dsi->driver->API->StartVideo(dsi->driver)) return false;
    dsi->bVideoActive = true;
    return true;
}

static void LTDeviceMipiDsiImpl_StopVideo(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bVideoActive) return;
    dsi->driver->API->StopVideo(dsi->driver);
    dsi->bVideoActive = false;
}

static bool LTDeviceMipiDsiImpl_IsVideoActive(LTDeviceMipiDsiImpl *dsi) { return dsi->bVideoActive; }

static bool LTDeviceMipiDsiImpl_WriteFrame(LTDeviceMipiDsiImpl *dsi, const void *pPixels, u32 nBytes) {
    if (! pPixels || ! nBytes) return false;
    if (! dsi->bPoweredOn) return false;
    if (kLTDeviceMipiDsi_Mode_Command != dsi->config.mode) {
        LTLOG_YELLOWALERT("writeframe.videomode", NULL);
        return false;
    }
    dsi->transferMutex->API->Lock(dsi->transferMutex);
    bool bOK = dsi->driver->API->WriteFrame(dsi->driver, pPixels, nBytes);
    dsi->transferMutex->API->Unlock(dsi->transferMutex);
    return bOK;
}

static bool LTDeviceMipiDsiImpl_DcsWrite(LTDeviceMipiDsiImpl *dsi, u8 dcsCommand, const u8 *pParams, u32 nParams) {
    if (! dsi->bPoweredOn) return false;
    if (nParams && ! pParams) return false;

    /* a DCS packet carries the command byte followed by its parameters; a short packet
       holds the command plus at most one parameter, so anything longer goes out long */
    bool bShort = (nParams < kLTDeviceMipiDsi_MaxShortParams);
    u32 nPacketBytes = nParams + 1;
    u8 *pPacketBytes = lt_malloc(nPacketBytes);
    if (! pPacketBytes) return false;
    pPacketBytes[0] = dcsCommand;
    if (nParams) lt_memcpy(&pPacketBytes[1], pParams, nParams);

    dsi->transferMutex->API->Lock(dsi->transferMutex);
    bool bOK = LTDeviceMipiDsiImpl_SendPacketLocked(dsi,
                   bShort ? kLTDriverMipiDsi_PacketType_DcsShort : kLTDriverMipiDsi_PacketType_DcsLong,
                   pPacketBytes, nPacketBytes);
    dsi->transferMutex->API->Unlock(dsi->transferMutex);

    lt_free(pPacketBytes);
    return bOK;
}

static s32 LTDeviceMipiDsiImpl_DcsRead(LTDeviceMipiDsiImpl *dsi, u8 dcsCommand, u8 *pReadBuffer, u32 nReadBufferBytes) {
    if (! dsi->bPoweredOn) return -1;
    if (! pReadBuffer || ! nReadBufferBytes) return -1;

    LTDriverMipiDsi_Packet request;
    lt_memset(&request, 0, sizeof(request));
    request.packetType      = kLTDriverMipiDsi_PacketType_DcsShort;
    request.nVirtualChannel = dsi->config.nVirtualChannel;
    request.pPayload        = &dcsCommand;
    request.nPayloadBytes   = 1;
    request.bHighSpeed      = false;    /* a read needs a bus turnaround, which is an LP escape mode operation */

    dsi->transferMutex->API->Lock(dsi->transferMutex);
    s32 nRead = dsi->driver->API->ReceivePacket(dsi->driver, &request, pReadBuffer, nReadBufferBytes);
    dsi->transferMutex->API->Unlock(dsi->transferMutex);
    return nRead;
}

static bool LTDeviceMipiDsiImpl_GenericWrite(LTDeviceMipiDsiImpl *dsi, const u8 *pPayload, u32 nPayloadBytes) {
    if (! dsi->bPoweredOn) return false;
    if (nPayloadBytes && ! pPayload) return false;

    bool bShort = (nPayloadBytes <= kLTDeviceMipiDsi_MaxShortParams);
    dsi->transferMutex->API->Lock(dsi->transferMutex);
    bool bOK = LTDeviceMipiDsiImpl_SendPacketLocked(dsi,
                   bShort ? kLTDriverMipiDsi_PacketType_GenericShort : kLTDriverMipiDsi_PacketType_GenericLong,
                   pPayload, nPayloadBytes);
    dsi->transferMutex->API->Unlock(dsi->transferMutex);
    return bOK;
}

static s32 LTDeviceMipiDsiImpl_GenericRead(LTDeviceMipiDsiImpl *dsi, const u8 *pPayload, u32 nPayloadBytes, u8 *pReadBuffer, u32 nReadBufferBytes) {
    if (! dsi->bPoweredOn) return -1;
    if (! pReadBuffer || ! nReadBufferBytes) return -1;
    if (nPayloadBytes && ! pPayload) return -1;
    if (nPayloadBytes > kLTDeviceMipiDsi_MaxShortParams) return -1;

    LTDriverMipiDsi_Packet request;
    lt_memset(&request, 0, sizeof(request));
    request.packetType      = kLTDriverMipiDsi_PacketType_GenericShort;
    request.nVirtualChannel = dsi->config.nVirtualChannel;
    request.pPayload        = pPayload;
    request.nPayloadBytes   = nPayloadBytes;
    request.bHighSpeed      = false;    /* see DcsRead */

    dsi->transferMutex->API->Lock(dsi->transferMutex);
    s32 nRead = dsi->driver->API->ReceivePacket(dsi->driver, &request, pReadBuffer, nReadBufferBytes);
    dsi->transferMutex->API->Unlock(dsi->transferMutex);
    return nRead;
}

static void LTDeviceMipiDsiImpl_SetCommandsUseHighSpeed(LTDeviceMipiDsiImpl *dsi, bool bHighSpeed) {
    dsi->bCommandsHighSpeed = bHighSpeed;
}

static bool LTDeviceMipiDsiImpl_GetCommandsUseHighSpeed(LTDeviceMipiDsiImpl *dsi) { return dsi->bCommandsHighSpeed; }

static bool LTDeviceMipiDsiImpl_EnterUltraLowPowerState(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bPoweredOn) return false;
    if (dsi->bVideoActive) {
        LTLOG_YELLOWALERT("ulps.videoactive", "the pixel stream must be stopped before entering ULPS");
        return false;
    }
    return dsi->driver->API->SetUltraLowPowerState(dsi->driver, true);
}

static bool LTDeviceMipiDsiImpl_ExitUltraLowPowerState(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bPoweredOn) return false;
    return dsi->driver->API->SetUltraLowPowerState(dsi->driver, false);
}

static LTDeviceMipiDsi_LaneState LTDeviceMipiDsiImpl_GetClockLaneState(LTDeviceMipiDsiImpl *dsi) {
    if (! dsi->bPoweredOn) return kLTDeviceMipiDsi_LaneState_Off;
    return dsi->driver->API->GetClockLaneState(dsi->driver);
}

static LTDeviceMipiDsi_LaneState LTDeviceMipiDsiImpl_GetDataLaneState(LTDeviceMipiDsiImpl *dsi, u32 nLane) {
    if (! dsi->bPoweredOn) return kLTDeviceMipiDsi_LaneState_Off;
    if (nLane >= dsi->config.nDataLanes) return kLTDeviceMipiDsi_LaneState_Off;
    return dsi->driver->API->GetDataLaneState(dsi->driver, nLane);
}

static void LTDeviceMipiDsiImpl_OnErrorEvent(LTDeviceMipiDsiImpl *dsi, LTDeviceMipiDsi_ErrorEventProc *pErrorEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    dsi->iEvent->RegisterForEvent(dsi->hEvent, pErrorEventProc, pClientDataReleaseProc, pClientData, false);
}

static void LTDeviceMipiDsiImpl_NoErrorEvent(LTDeviceMipiDsiImpl *dsi, LTDeviceMipiDsi_ErrorEventProc *pErrorEventProc) {
    dsi->iEvent->UnregisterFromEvent(dsi->hEvent, pErrorEventProc);
}

static LTDeviceMipiDsi_Error LTDeviceMipiDsiImpl_GetErrors(LTDeviceMipiDsiImpl *dsi, bool bClear) {
    u32 errors = bClear ? LTAtomic_Exchange(&dsi->accumulatedErrors, 0) : LTAtomic_Load(&dsi->accumulatedErrors);
    return (LTDeviceMipiDsi_Error)errors;
}

static const char *LTDeviceMipiDsiImpl_ErrorToString(LTDeviceMipiDsi_Error error) {
    switch (error) {
        case kLTDeviceMipiDsi_Error_None:             return "None";
        case kLTDeviceMipiDsi_Error_TxFifoOverflow:   return "TxFifoOverflow";
        case kLTDeviceMipiDsi_Error_TxFifoUnderflow:  return "TxFifoUnderflow";
        case kLTDeviceMipiDsi_Error_RxFifoOverflow:   return "RxFifoOverflow";
        case kLTDeviceMipiDsi_Error_RxFifoUnderflow:  return "RxFifoUnderflow";
        case kLTDeviceMipiDsi_Error_PixelCountShort:  return "PixelCountShort";
        case kLTDeviceMipiDsi_Error_PixelCountLong:   return "PixelCountLong";
        case kLTDeviceMipiDsi_Error_EccUncorrectable: return "EccUncorrectable";
        case kLTDeviceMipiDsi_Error_Checksum:         return "Checksum";
        case kLTDeviceMipiDsi_Error_Timeout:          return "Timeout";
        case kLTDeviceMipiDsi_Error_PanelReported:    return "PanelReported";
        default: break;
    }
    return "Unknown";
}

static void LTDeviceMipiDsiImpl_DumpRegisters(LTDeviceMipiDsiImpl *dsi, LTDeviceMipiDsi_RegisterProc *pRegisterProc, void *pClientData) {
    if (pRegisterProc == NULL) return;
    dsi->driver->API->DumpRegisters(dsi->driver, pRegisterProc, pClientData);
}

/*_______________________________
  LTDeviceMipiDsi api binding  */
define_LTObjectImplPublic(LTDeviceMipiDsi, LTDeviceMipiDsiImpl,
    Configure,
    GetConfig,
    PowerOn,
    PowerOff,
    IsPoweredOn,
    SetFrameBuffer,
    StartVideo,
    StopVideo,
    IsVideoActive,
    WriteFrame,
    DcsWrite,
    DcsRead,
    GenericWrite,
    GenericRead,
    SetCommandsUseHighSpeed,
    GetCommandsUseHighSpeed,
    EnterUltraLowPowerState,
    ExitUltraLowPowerState,
    GetClockLaneState,
    GetDataLaneState,
    OnErrorEvent,
    NoErrorEvent,
    GetErrors,
    ErrorToString,
    DumpRegisters
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 *  01-Oct-26   dwoodward   added DumpRegisters
 */
