/*******************************************************************************
 *
 * LTNetSpeed.c - Network Speed test utility
 *  - LTNetSpeed::LinkSpeed():
 *    Send UDP packets on link at maximum speed to measure the speed of the first link
 *    The packets does not require a reply and will quickly be dropped in the network
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/speed/LTNetSpeed.h>

DEFINE_LTLOG_SECTION("net.speed");

/*******************************************************************************
** Constant and Type Definitions
*******************************************************************************/

typedef struct SpeedData {
    char             dataBuf[1460];
    LTEvent          event;
    LTNetSpeed_Proc *eventFunc;
    void            *eventData;
    u16              msDuration;
    LTSocket         socket;
    LTTime           timeZero;
    LTTransport      transport;
    u64              totalBytes;
    bool             synchronous;
} SpeedData;

static struct Statics {
    LTCore              *core;
    LTDeviceWiFi        *deviceWiFi;
    LTUtilityMacAddress *iMacAddress;
    ILTThread           *iThread;
    LTNetCore           *netCore;
    LTNetSpeed          *netSpeed;
    ILTEvent            *iEvent;
    u32                  kbps;
} S;

static const LTArgsDescriptor SpeedEventArgs = {1, {kLTArgType_u32}};  // kbps

/*******************************************************************************
** Prototypes
*******************************************************************************/
static void LinkSpeedDone(void *clientData);
static void OnSpeedEvent(LTSocket socket, LTSocket_Event event, void *clientData);
static void WriteSocketLoop(void *clientData);

/*******************************************************************************
** Private Functions
*******************************************************************************/
static void SpeedEventProc(LTEvent event, void *proc, LTArgs *args, void *clientData) {
    // Runs in API caller thread context
    (*(LTNetSpeed_Proc *)proc)(LTArgs_u32At(0, args), clientData);
    S.iEvent->UnregisterFromCurrentEvent();
    S.iEvent->Destroy(event);
}

static void LinkSpeedDone(void *clientData) {
    SpeedData    *SD      = (SpeedData *)clientData;
    u32           usec    = 0;
    u32           kbps    = 0;
    LTTime        now     = S.core->GetKernelTime();
    LTWiFi_ApInfo ap_info = {0};
    char          oui[20] = {0};

    S.iThread->KillTimer(S.iThread->GetCurrentThread(), LinkSpeedDone, clientData);
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), WriteSocketLoop, clientData);
    S.core->DestroyHandle(SD->socket);

    if (LTTime_IsGreaterThan(now, SD->timeZero)) {
        usec = LTTime_GetMicroseconds(LTTime_Subtract(S.core->GetKernelTime(), SD->timeZero));
        kbps = (u64)SD->totalBytes * 8000 / usec;
    }
    if (kbps > 999999) kbps = 999999;  // avoid nonsense
    if (SD->synchronous) S.kbps = kbps;

    S.deviceWiFi->GetApInfo(&ap_info);
    S.iMacAddress->MacAddressToOui(&ap_info.bssid, oui, ':');
    LTLOG_SERVER("link.done",
                 "kbps:%6lu, msec:%5lu.%03lu, ch: %u bw: %u rssi: %d snr: %u bssid: %s hash: %lx ssid: \"%s\"",
                 LT_Pu32(kbps),
                 LT_Pu32(usec) / 1000,
                 LT_Pu32(usec) % 1000,
                 ap_info.channel,
                 ap_info.bandwidth,
                 ap_info.rssi,
                 ap_info.snr,
                 oui,
                 LT_Pu32(S.iMacAddress->MacAddressToHash(&ap_info.bssid)),
                 ap_info.ssid);
    if (SD->event) S.iEvent->NotifyEvent(SD->event, kbps);

    lt_free(SD);
    S.iThread->Terminate(S.iThread->GetCurrentThread());
    S.iThread->Destroy(S.iThread->GetCurrentThread());
    lt_closelibrary(S.netSpeed);  // refcount(self)-- for thread exit
}

static void WriteSocketLoop(void *clientData) {
    SpeedData *SD      = (SpeedData *)clientData;
    LTSocket   socket  = SD->socket;
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, socket);
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), WriteSocketLoop, clientData);

    int max_burst = 100;
    for (;;) {
        s32 len = pSocket->WriteSocket(socket, SD->dataBuf, sizeof(SD->dataBuf));
        if (len == 0 || --max_burst == 0) {
            // UDP doesn't get any WriteReady socket event on ESP32, Alta and T31 so we just sleep a bit
            // We also sleep a bit if we hit max_burst to allow other events to be handled
            S.iThread->SetTimer(S.iThread->GetCurrentThread(),
                                LTTime_Milliseconds(1),
                                WriteSocketLoop,
                                NULL,
                                clientData);
            break;
        }
        if (len < 0) {
            LTLOG("werror", "WriteSocket() error <%ld>\n", LT_Pu32(len));
            LinkSpeedDone(clientData);
            break;
        }
        if (LTTime_IsGreaterThanOrEqual(S.core->GetKernelTime(), SD->timeZero)) {
            SD->totalBytes += len;
        }
    }
}

static void OnSpeedEvent(LTSocket socket, LTSocket_Event event, void *clientData) {
    // LTLOG("event", "=== Event: socket: H%04lx event: 0x%lx\n", LT_Pu32(socket), LT_Pu32(event));
    if (!S.core->IsHandleValid(socket)) {
        LTLOG("invalid", "Socket handle is invalid!!!\n");
    }

    switch (event) {
        case kLTSocket_Event_SocketReady:
        case kLTSocket_Event_Connected:
            break;
        case kLTSocket_Event_WriteReady:
            S.iThread->QueueTaskProcIfRequired(S.iThread->GetCurrentThread(), WriteSocketLoop, NULL, clientData);
            break;
        default:
            LTLOG("event", "=== Odd Event: socket: H%04lx event: 0x%lx\n", LT_Pu32(socket), LT_Pu32(event));
            LinkSpeedDone(clientData);
            break;
    }
}

static void LTNetSpeed_LinkSpeedImpl(void *clientData) {
    SpeedData *SD = (SpeedData *)clientData;

    // Give the system 100 mSec to fill buffers before measureing speed
    SD->timeZero = LTTime_Add(S.core->GetKernelTime(), LTTime_Milliseconds(100));

    // Done in msDuration + 100 mSec to fill buffers before measuring speed
    S.iThread->SetTimer(S.iThread->GetCurrentThread(),
                        LTTime_Milliseconds(SD->msDuration + 100),
                        LinkSpeedDone,
                        NULL,
                        clientData);

    // 192.0.0.8 is IANA dummy IP, port 9 is discard
    SD->socket = S.netCore->OpenSocket(SD->transport, "udp ip: 192.0.0.8 port: 9", OnSpeedEvent, SD);
    if (!SD->socket) {
        LinkSpeedDone(clientData);
    }
}

/*******************************************************************************
** API Function Definitions
*******************************************************************************/
static u32 LTNetSpeed_LinkSpeed(u16              msDuration,
                                LTTransport      hTransport,
                                bool             synchronous,
                                LTNetSpeed_Proc *eventFunc,
                                void            *eventData) {
    SpeedData *SD = lt_malloc(sizeof(SpeedData));
    if (SD == NULL) return false;
    if (msDuration < 100) msDuration = 1000;  // Default duration is 1000 ms

    *SD = (SpeedData) { // also zeros unspecified fields
        .eventFunc = eventFunc,
        .eventData = eventData,
        .transport = hTransport,
        .msDuration = msDuration,
        .synchronous = synchronous
    };
    if (synchronous) S.kbps = 0;
    if (eventFunc) {
        SD->event = S.core->CreateEvent(&SpeedEventArgs, SpeedEventProc, NULL, NULL, NULL);
        S.iEvent->RegisterForEvent(SD->event, eventFunc, NULL, eventData, false);
    }
    LTThread thread = S.core->CreateThread("NetSpeed");
    if (thread) {
        S.netSpeed = lt_openlibrary(LTNetSpeed);  // refCount(self)++ for thread start
        S.iThread->SetStackSize(thread, 2048);
        S.iThread->Start(thread, NULL, NULL);
        S.iThread->QueueTaskProc(thread, LTNetSpeed_LinkSpeedImpl, NULL, SD);

        if (synchronous) {
            S.iThread->WaitUntilFinished(thread, LTTime_Milliseconds(msDuration + 1000));
            return S.kbps;
        }
    }
    return 0;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTNetSpeedImpl_LibFini(void) {
    lt_closelibrary(S.iMacAddress);
    lt_closelibrary(S.deviceWiFi);
    lt_closelibrary(S.netCore);
    S.netCore = NULL;
    S.iEvent  = NULL;
    S.iThread = NULL;
}

static bool LTNetSpeedImpl_LibInit(void) {
    S = (struct Statics) {
        .core        = LT_GetCore(),
        .deviceWiFi  = lt_openlibrary(LTDeviceWiFi),
        .iMacAddress = lt_openlibrary(LTUtilityMacAddress),
        .iThread     = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent      = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .netCore     = lt_openlibrary(LTNetCore)
    };
    if (!S.netCore) return false;
    return true;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTLIBRARY_ROOT_INTERFACE(LTNetSpeed,) {
    .LinkSpeed    = LTNetSpeed_LinkSpeed
} LTLIBRARY_DEFINITION;
