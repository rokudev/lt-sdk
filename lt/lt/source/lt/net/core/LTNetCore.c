/*******************************************************************************
 *
 * LTNet: Network Library
 * ----------------------
 *
 * Provides network data transport via lower layer network stack protocols
 * including but not limited to TCP/IP.
 *
 * This interface is independent of TCP/IP. Other mechanisms are possible,
 * so do not add IP-isms at this level.
 *
 * This is a data transport interface. It is NOT a control interface for
 * data-link layer devices. (For example, making Wi-Fi connections to APs.)
 * Access those via their respective libraries.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/net/dtls/LTNetDtls.h>
#include <lt/net/tls/LTNetTls.h>

DEFINE_LTLOG_SECTION("lt.net.core");

/*******************************************************************************
** Module Local Definitions and Variables
*******************************************************************************/

// Function definition indicators:
#define PUB static // Part of public interface
#define FWD static // Forward declaration
#define LOC static // Local definition
#define UTL static // Utility purposes

static struct Statics { // cleared in LibInit
    LTCore            *iCore;
    ILTEvent          *iEvent;
    ILTThread         *iThread;
    LTMutex           *mutex;
    LTList             allTransports;
    LTNetTls          *tlsLibrary;              // TLS library for TLS socket wrapper. Only close by network core, not by socket.
    LTNetDtls         *dtlsLibrary;             // DTLS library for DTLS socket wrapper.
// The default transport driver (a library), once created, remains permanently open.
// The main reason is that all users of the default transport may destroy their
// transport instances, leaving no instance to keep the default driver open and
// usable as the default. Therefore, it needs to be kept around for any new instance.
// The secondary reason is that the default driver is usually Wi-Fi, which may not be
// gracefully shutdown on some chips (ESP32 for example). Therefore, it is left active.
    LTTransportDriver *defaultTransportDriver;
    // health metrics
    LTAtomic socketCreatedCount;        // all successfully created sockets in all transport
    LTAtomic socketDestroyedCount;      // all destroyed sockets in all transport, including both successfully created and failed sockets
    LTAtomic socketWriteCount;          // all successful socket writes in all transport
    LTAtomic socketWriteFailCount;      // all failed socket writes in all transport
} S;

// Interface used for Socket handles:
LOC void LTSocket_DestroySocket(LTSocket handle);
static ILTSocket s_ILTSocket;

/*******************************************************************************
** Local Utility Defines and Functions
*******************************************************************************/

UTL void GetFirstToken(const char *cp, char *tp, s16 len)  { // len includes terminator
    if (cp && tp) {
        for (len--; len > 0 && *cp && *cp != ' '; len--) *tp++ = *cp++;
        *tp = 0;
    }
}

/*******************************************************************************
** Event Dispatch Procs
*******************************************************************************/

LOC const LTArgsDescriptor TransportEventArgs = {2, { kLTArgType_u32, kLTArgType_u32 }}; // transport, event

LOC void DispatchTransportEvent(LTEvent event, void *proc, LTArgs *args, void *clientData) {
    LT_UNUSED(event);
    if (!proc) return;
    if (!clientData) {
        LTLOG_YELLOWALERT("evt.bad.data", "null devent data");
        return;
    }
    // Note: It's the clientData when event is registered that lets us to get back to the transport handle.
    LTTransportData *tran = (LTTransportData*)clientData;
    // Translates to: LTTransport_EventProc(LTTransport transport, LTTransport_Event event, void *clientData);
    (*(LTTransport_EventProc *)proc)(tran->hTransport, (int)LTArgs_u32At(1, args), tran->eventData);
}

LOC const LTArgsDescriptor SocketEventArgs = {2, { kLTArgType_u32, kLTArgType_u32 }}; // socket, event

LOC void DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc) return;
    LTSocket hSocket = (int)LTArgs_u32At(0, args);
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;

    LTSocket_Event socketEvent = (LTSocket_Event)LTArgs_u32At(1, args);
    if (socketEvent == kLTSocket_Event_ReadReady) {
        LTAtomic_Store(&socket->readPending, 0);
    }
    if (socketEvent == kLTSocket_Event_WriteReady) {
        LTAtomic_Store(&socket->writePending, 0);
    }

    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    (*(LTSocket_EventProc *)proc)(hSocket, socketEvent, data);
}

/*******************************************************************************
** Network Transport Support
*******************************************************************************/

FWD void LTNetCore_DestroyTransport(LTTransport hTransport);
FWD LTSocket CreateSocket(LTTransportData *transData, const char *socketSpec, LTEvent socketEvent);

typedef_LTLIBRARY_INTERFACE(INetTransport, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(INetTransport, LTNetCore_DestroyTransport) {} LTLIBRARY_DEFINITION;

LOC const char *GetDefaultTransportName(char *name) {
    name[0] = 0;
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!deviceConfig) return "no device config";
    const char *defaultName = deviceConfig->GetDefaultNetTransport();
    if (!defaultName) {
        lt_closelibrary(deviceConfig);
        return "no default in device config";
    }
    lt_strncpyTerm(name, defaultName, kLTLibrary_MaxNameBufferSize);
    lt_closelibrary(deviceConfig);
    return NULL;
}

LOC void FreeTransportDriver(LTTransportDriver *data) {
    // Only call this when there are no transport handles for this driver
    if (!data) return;
    // Any of these can be null:
    S.iCore->DestroyHandle(data->hEvent);
    lt_closelibrary(data->tranLib);
    lt_free(data->tranSpec);
    lt_free(data);
}

LOC const char *GetTransportDriver(const char *transportSpec, LTTransportDriver **driverData) {
    // Returns error for message like: "transport open failed due to: <string>"
    const char *error;
    char libName[kLTLibrary_MaxNameBufferSize] = {};

    // Were we provide an actual transport name or use the default name?
    if (transportSpec) {
        GetFirstToken(transportSpec, libName, kLTLibrary_MaxNameBufferSize);
        if (!libName[0]) return "no transport name";
    } else { // use the default
        if (S.defaultTransportDriver) {
            LTLOG("use.dflt", "using default transport");
            *driverData = S.defaultTransportDriver;
            return NULL;
        }
        error = GetDefaultTransportName(libName);
        if (error) return error;
    }

    // Initialize the primary driver data structure:
    LTTransportDriver *driver = lt_malloc(sizeof(LTTransportDriver));
    if (!driver) return "OOM for driver data";
    *driver = (LTTransportDriver) {};

    driver->tranSpec = lt_strdup(transportSpec ? transportSpec: libName);
    if (!driver->tranSpec) {
        FreeTransportDriver(driver);
        return "OOM for spec";
    }

    // Open the transport library and get the driver interface:
    LTLOG("open.tran", "opening transport: %s", libName);
    driver->tranLib = S.iCore->OpenLibrary(libName);
    if (!driver->tranLib) {
        FreeTransportDriver(driver);
        return "missing transport library";
    }
    driver->iDriver = lt_getlibraryinterface(LTNetDriver, driver->tranLib);

    // Create the event handler:
    driver->hEvent = S.iCore->CreateEvent(&TransportEventArgs, DispatchTransportEvent, NULL, NULL, driver);
        /* DRW: Last parameter 'driver' as pNotifyImmediateEventStateClientData has no meaning when pNotifyImmediateEventStateProc is NULL */
    if (!driver->hEvent) {
        FreeTransportDriver(driver);
        return "create event";
    }

    *driverData = driver;
    return NULL;
}

UTL LTTransportDriver *VetTransport(LTTransport hTransport) {
    if (hTransport) {
        if ((INetTransport*)S.iCore->GetHandleInterface(hTransport) == &s_INetTransport) {
            LTTransportData *transData = S.iCore->ReserveHandlePrivateData(hTransport);
            if (transData) {
                S.iCore->ReleaseHandlePrivateData(hTransport, transData);
                return transData->driverData;
            }
        }
        LTLOG_YELLOWALERT("tran.bad.hndl", "handle is not a transport");
        return 0;
    }
    if (S.defaultTransportDriver) return S.defaultTransportDriver;

    LTLOG_YELLOWALERT("tran.no.dflt", "no default transport");
    return 0;
}

UTL LTTransportData *GetDefaultTransportData(void) {
    if (S.defaultTransportDriver) {
        S.mutex->API->Lock(S.mutex);
        LTList_ForEach(node, &S.allTransports) {
            LTTransportData *transData = (LTTransportData *)node;
            if (transData->driverData == S.defaultTransportDriver) {
                S.mutex->API->Unlock(S.mutex);
                return transData;
            }
        } LTList_EndForEach;
        S.mutex->API->Unlock(S.mutex);
    }

    LTLOG_YELLOWALERT("tran.no.dflt", "no default transport");
    return NULL;
}


/*******************************************************************************
** Network Transport API
*******************************************************************************/

PUB LTTransport LTNetCore_OpenTransport(const char *transportSpec, LTTransport_EventProc eventProc, void *eventData) {
    LTTransportDriver *driverData = NULL; // pointer gets returned from next line
    const char *error = GetTransportDriver(transportSpec, &driverData);
    if (error) {
        LTLOG("trn.fail", "transport open failed due to: %s", error);
        return 0;
    }

    LTTransport hTransport = S.iCore->CreateHandle((LTInterface *)&s_INetTransport, sizeof(LTTransportData));
    LTTransportData *transData = S.iCore->ReserveHandlePrivateData(hTransport);
    *transData = (LTTransportData) { // clears struct
        .driverData = driverData,
        .hTransport = hTransport,
        .eventProc  = eventProc,
        .eventData  = eventData
    };
    LTList_Init(&transData->socketList);

    if (eventProc) S.iEvent->RegisterForEvent(driverData->hEvent, eventProc, NULL, transData, false);

    driverData->socketSize = driverData->iDriver->OpenTransport(transData, CreateSocket); //needs to provide up event!
    driverData->socketSize += sizeof(LTSocket_Data);
    LTList_AddTail(&S.allTransports, &transData->node);

    // If there's no default transport, use this new one:
    if (!transportSpec && !S.defaultTransportDriver) {
        S.defaultTransportDriver = driverData;
    }

    S.iCore->ReleaseHandlePrivateData(hTransport, transData);
    return hTransport;
}

PUB void LTNetCore_DestroyTransport(LTTransport hTransport) { // aka CloseTransport
    // This is called via the LTCore->DestroyHandle() mechanism, not explicitly.
    LTTransportData *transData = S.iCore->ReserveHandlePrivateData(hTransport);
    if (!transData) return;

    LTList_Remove(&transData->node);

    // Destroy all sockets.
    while (true) {
        S.mutex->API->Lock(S.mutex);
        if (LTList_IsEmpty(&transData->socketList)) {
            S.mutex->API->Unlock(S.mutex);
            break;
        }
        LTSocket_Data *socket = LT_CONTAINER_OF(transData->socketList.pNext, LTSocket_Data, node);
        S.mutex->API->Unlock(S.mutex);
        // Minimal race if another thread destroys the socket here. But ReserveHandlePrivateData
        // in the destroy protects that. The handle will become zero and exit the destroy.
        S.iCore->DestroyHandle(socket->h_socket);
    }
    LTList_Init(&transData->socketList);

    LTTransportDriver *driverData = transData->driverData;
    if (transData->eventProc) {
        S.iEvent->UnregisterFromEvent(driverData->hEvent, transData->eventProc);
    }

    if (driverData != S.defaultTransportDriver) {
        driverData->iDriver->CloseTransport(driverData);
        FreeTransportDriver(driverData);
    }

    *transData = (LTTransportData) {};
    LTLOG("tran.dstr", "transport destroyed %08lx\n", LT_PLT_HANDLE(hTransport));
    S.iCore->ReleaseHandlePrivateData(hTransport, transData);
}

PUB void LTNetCore_GetTransportSpec(LTTransport hTransport, char *spec, u16 specSize) {
    LTTransportDriver *driverData = VetTransport(hTransport);
    if (!spec || !specSize) return;
    spec[0] = 0;
    if (driverData) driverData->iDriver->GetTransportSpec(driverData, spec, specSize);
}

PUB void LTNetCore_GetMetrics(LTTransport hTransport, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    LTTransportDriver *driverData = VetTransport(hTransport);
    if (!driverData) return;
    driverData->iDriver->GetMetrics(driverData, metrics, sizeOfMetrics);
}

PUB s32 LTNetCore_IsOperating(LTTransport hTransport, LTTransport_Nudge nudge) {
    LTTransportDriver *driverData = VetTransport(hTransport);
    if (!driverData) return -1;
    return driverData->iDriver->IsOperating(driverData, nudge);
}

PUB void LTNetCore_ShowLwipStat(LTTransport hTransport, bool logToServer) {
    LTTransportDriver *driverData = VetTransport(hTransport);
    if (!driverData) return;
    driverData->iDriver->ShowLwipStat(driverData, logToServer);
}

PUB void LTNetCore_ProcTransportMetrics(LTTransport hTransport, LTTransport_MetricsAction action, bool logToServer) {
    LTTransportDriver *driverData = VetTransport(hTransport);
    if (!driverData) return;
    driverData->iDriver->ProcTransportMetrics(driverData, action, logToServer);
}

/*******************************************************************************
** Network Sockets
*******************************************************************************/

LOC LTSocket CreateSocket(LTTransportData *transData, const char *socketSpec, LTEvent socketEvent) {
    // Create's basic socket struct and optionally the event handler
    // This is also called from the transport driver when a new socket is needed (for accept on listener)
    LTSocket hSocket = S.iCore->CreateHandle((LTInterface *) &s_ILTSocket, transData->driverData->socketSize);
    if (!hSocket) {
        LTLOG_YELLOWALERT("skt.crt.oom", "create socket oom");
        return 0;
    }
    // Set up new event handler if we weren't given one:
    if (!socketEvent) {
        if (!(socketEvent = S.iCore->CreateEvent(&SocketEventArgs, DispatchSocketEvent, NULL, NULL, NULL))) {
            LTLOG_YELLOWALERT("skt.fail.event", "create event");
            return 0;
        }
    }
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    lt_memset(socket, 0, transData->driverData->socketSize); // clear both LTSocket_Data and private sock data
    *socket = (LTSocket_Data) {               // clears unset fields
     // .h_transport = transData->handle,     // !!! get rid of it?
        .transData   = transData,             // shortcut to data
        .h_socket    = hSocket,               // needed for event proc callbacks
        .spec        = socketSpec ? lt_strdup(socketSpec) : NULL,
        .driver      = transData->driverData->iDriver, // driver calls (necessary for TCP listen-created sockets!)
        .event       = socketEvent,
        .eventData   = S.iCore->ReserveHandlePrivateData(socketEvent), // event RefCount++ (released in LTSocket_DestroySocket)
    };

    S.mutex->API->Lock(S.mutex);
    LTList_AddTail(&transData->socketList, &socket->node);
    S.mutex->API->Unlock(S.mutex);

    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    return hSocket;
}

PUB LTSocket LTNetCore_OpenSocket(LTTransport hTransport, const char *socketSpec, LTSocket_EventProc procFunc, void *procData) {
    LTTransportData *transData = NULL;
    if (hTransport) {
        if (!VetTransport(hTransport)) return 0;
    } else {
        transData = GetDefaultTransportData();
        if (!transData) {
            // See Note 1 above
            LTLOG_YELLOWALERT("sock.no.dflt", "no active default transport");
            return 0;
        }
        hTransport = transData->hTransport;
    }
    transData = S.iCore->ReserveHandlePrivateData(hTransport);
    if (!transData) return 0;

    LTSocket hBaseSocket = CreateSocket(transData, socketSpec, 0);
    if (!hBaseSocket) {
        LTLOG_YELLOWALERT("skt.fail.base", "base socket");
        return 0;
    }

    LTSocket hWrappedSocket = 0;
    LTSocket_Data *socket   = S.iCore->ReserveHandlePrivateData(hBaseSocket);
    const char *reason = NULL;
    do {
        socket->driver = NULL;  // set to NULL before the rest socket initialization succeeds.

        char protocol[32];
        protocol[0] = '\0';
        if (socketSpec) GetFirstToken(socketSpec, protocol, 32);
        if (lt_strcmp(protocol, "tls") == 0) {
            bool bTlsInDriver = transData->driverData->iDriver->IsTlsSupported ? transData->driverData->iDriver->IsTlsSupported() : false;
            if (bTlsInDriver) {
                S.iEvent->RegisterForEvent(socket->event, procFunc, NULL, procData, false);
            } else {
                reason = "no tlsLibrary";
                if (!(S.tlsLibrary)) break;
                reason = "tlsLibrary WrapSocket";
                if (!(hWrappedSocket = S.tlsLibrary->WrapSocket(hBaseSocket, socketSpec, socket->event, procFunc, procData, &S.socketWriteCount, &S.socketWriteFailCount))) break;
            }
        } else if (lt_strcmp(protocol, "dtls") == 0) {
            reason = "no dtlsLibrary";
            if (!(S.dtlsLibrary)) break;
            reason = "dtlsLibrary WrapSocket";
            if (!(hWrappedSocket = S.dtlsLibrary->WrapSocket(hBaseSocket, socketSpec))) break;
        } else {
            S.iEvent->RegisterForEvent(socket->event, procFunc, NULL, procData, false);
        }

        // Ask driver to create new socket - done after the socket is wrapped
        // to ensure that the wrapper doesn't miss any socket events.
        socket->driver = transData->driverData->iDriver;
        reason = "driver OpenSocket";
        if (!(socket->driver->OpenSocket(socket))) break;

        S.iCore->ReleaseHandlePrivateData(hBaseSocket, socket);
        LTAtomic_FetchAdd(&S.socketCreatedCount, 1);
        S.iCore->ReleaseHandlePrivateData(hTransport, transData);
        if (hWrappedSocket) return hWrappedSocket;
        return hBaseSocket;
    } while (false);

    // Failure handling:
    LTLOG_YELLOWALERT("skt.fail.open", "open socket failed: %s", reason);
    S.iCore->ReleaseHandlePrivateData(hBaseSocket, socket);
    if (hWrappedSocket) lt_destroyhandle(hWrappedSocket); // destroy tls/dtls handle and socket handle
    else lt_destroyhandle(hBaseSocket);                   // no tls/dtls, so destroy socket handle
    S.iCore->ReleaseHandlePrivateData(hTransport, transData);
    return 0;
}

PUB void LTSocket_DestroySocket(LTSocket hSocket) { // aka CloseSocket
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket); // zero handle returns NULL
    if (!socket) return;
    if (socket->h_socket) {
        S.mutex->API->Lock(S.mutex);
        LTList_Remove(&socket->node);
        S.mutex->API->Unlock(S.mutex);
        LTAtomic_FetchAdd(&S.socketDestroyedCount, 1);
        if (socket->driver) socket->driver->CloseSocket(hSocket); // clear and free private resources
        S.iCore->ReleaseHandlePrivateData(socket->event, socket->eventData); // event RefCount--
        S.iCore->DestroyHandle(socket->event); // also "UnRegisters" event proc
        lt_free(socket->spec);                 // NULL okay
        // clear socket handle data before free.
        *socket = (LTSocket_Data){};
    }
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB void LTSocket_OnSocketEvent(LTSocket hSocket, LTSocket_EventProc proc, void *procData) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;
    S.iEvent->RegisterForEvent(socket->event, proc, NULL, procData, false);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB void LTSocket_NoSocketEvent(LTSocket hSocket, LTSocket_EventProc proc) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;
    S.iEvent->UnregisterFromEvent(socket->event, proc);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB void LTSocket_GetSocketSpec(LTSocket hSocket, char *spec, int specSize) {
    if (!spec || !specSize) return;
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;
    socket->driver->GetSocketSpec(socket, spec, specSize);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB bool LTSocket_GetProperty(LTSocket hSocket, const char *name, void *value) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return false;
    bool result = socket->driver->GetSocketProperty(socket, name, value);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    return result;
}

PUB bool LTSocket_SetProperty(LTSocket hSocket, const char *name, const void *value) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return false;
    bool result = socket->driver->SetSocketProperty(socket, name, value);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    return result;
}

PUB void LTSocket_ConnectSocket(LTSocket hSocket) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;
    socket->driver->ConnectSocket(socket);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB void LTSocket_DisconnectSocket(LTSocket hSocket) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return;
    socket->driver->DisconnectSocket(socket);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
}

PUB s32 LTSocket_WriteSocket(LTSocket hSocket, const void *data, u32 dataLen) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return -1;
    s32 len = socket->driver->WriteSocket(socket, data, dataLen);
    if (len >= 0) LTAtomic_FetchAdd(&S.socketWriteCount, 1);
    else LTAtomic_FetchAdd(&S.socketWriteFailCount, 1);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    return len;
}

PUB s32 LTSocket_ReadSocket(LTSocket hSocket, void *data, u32 dataSize) {
    LTSocket_Data *socket = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socket) return -1;
    s32 len = socket->driver->ReadSocket(socket, data, dataSize);
    S.iCore->ReleaseHandlePrivateData(hSocket, socket);
    return len;
}

/*******************************************************************************
 * Health functions
 ******************************************************************************/

PUB void LTNetCore_GetSocketCounts(u32 *createdCount, u32 *destroyedCount, u32 *writeCount, u32 *writeFailCount) {
    *createdCount   = LTAtomic_Load(&S.socketCreatedCount);
    *destroyedCount = LTAtomic_Load(&S.socketDestroyedCount);
    *writeCount     = LTAtomic_Load(&S.socketWriteCount);
    *writeFailCount = LTAtomic_Load(&S.socketWriteFailCount);
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

PUB void LTNetCoreImpl_LibFini(void) {
    lt_destroyobject(S.mutex);
    S.mutex = NULL;
    lt_closelibrary(S.tlsLibrary);
    lt_closelibrary(S.dtlsLibrary);
    LTTransportDriver *driverData = S.defaultTransportDriver;
    if (driverData) {
        driverData->iDriver->CloseTransport(driverData);
        FreeTransportDriver(driverData);
    }
    // !!! ToDo - destroy all transports

    S = (struct Statics) {};
}

PUB bool LTNetCoreImpl_LibInit(void) {
    S = (struct Statics) {
        .iCore   = LT_GetCore(),
        .iEvent  = lt_getlibraryinterface(ILTEvent, LT_GetCore()),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .tlsLibrary = lt_openlibrary(LTNetTls),
        .dtlsLibrary = lt_openlibrary(LTNetDtls),
    };
    S.mutex = lt_createobject(LTMutex);
    LTList_Init(&S.allTransports);
    return true;
}

PUB int LTNetCore_Run(int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    return 0;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTLIBRARY_ROOT_INTERFACE(LTNetCore, LTNetCore_Run, 1024)
    .OpenTransport           = LTNetCore_OpenTransport,
    .GetTransportSpec        = LTNetCore_GetTransportSpec,
    .GetMetrics              = LTNetCore_GetMetrics,
    .IsOperating             = LTNetCore_IsOperating,
    .OpenSocket              = LTNetCore_OpenSocket,
    .GetSocketCounts         = LTNetCore_GetSocketCounts,
    .ShowLwipStat            = LTNetCore_ShowLwipStat,
    .ProcTransportMetrics    = LTNetCore_ProcTransportMetrics,
};

define_LTLIBRARY_INTERFACE(ILTSocket, LTSocket_DestroySocket)
    .OnSocketEvent    = LTSocket_OnSocketEvent,
    .NoSocketEvent    = LTSocket_NoSocketEvent,
    .GetSocketSpec    = LTSocket_GetSocketSpec,
    .GetProperty      = LTSocket_GetProperty,
    .SetProperty      = LTSocket_SetProperty,
    .ConnectSocket    = LTSocket_ConnectSocket,
    .DisconnectSocket = LTSocket_DisconnectSocket,
    .WriteSocket      = LTSocket_WriteSocket,
    .ReadSocket       = LTSocket_ReadSocket,
};
