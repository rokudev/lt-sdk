/*******************************************************************************
 *
 * LTNetCoreDriver: Private Transport Driver Definitions
 * -----------------------------------------------------
 *
 * These are not public to applications but are used for implementing
 * transport drivers called from LTNetCore.c.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTNETDRIVER_H_2
#define LTNETDRIVER_H_2

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>

typedef struct LTTransportDriver LTTransportDriver;

// The structure below is stored in the transport handle. This data is related to
// the upper level of the transport, not the lower level driver.
typedef struct {
    LTList_Node            node;        ///< linked lists of transports (for libfini)
    LTTransportDriver     *driverData;  ///< transport related driver data
    LTTransport            hTransport;  ///< needed for events via clientData
    LTTransport_EventProc *eventProc;   ///< needed to unregister from transport events
    void                  *eventData;   ///< needed for event dispatch and unregister
    LTList                 socketList;  ///< sockets using this transport
} LTTransportData;

typedef struct Priv_Tran Priv_Tran;     // Platform dependent opaque pointer for driver

// The structure below is the platform independent part of the transport driver and
// is allocated independently from the transport handle above. There may be more than one
// transport handle using this same driver. The default transport is one example.
typedef struct LTTransportDriver {
    Priv_Tran             *privData;    ///< netcore driver implementation related data
    char                  *tranSpec;    ///< transport specification (can be null)
    LTLibrary             *tranLib;     ///< transport driver library
    LTNetDriver           *iDriver;     ///< interface into transport driver
    LTEvent                hEvent;      ///< notification on status changes
    u32                    socketSize;  ///< socket size can vary between drivers
} LTTransportDriver;

// Callback used by driver to create new sockets:
typedef LTSocket (LTSocket_Create)(LTTransportData *tranData, const char *socketSpec, LTEvent socketEvent);

typedef struct LTSocket_Data LTSocket_Data; // Forward reference

/**
 * Network Driver
 *
 * Specifies the interface between LTNetCore and a transport driver.
 * This part can be dependent on transport type or its implementation but must not export or
 * leak back to LTNetCore or any other part of the system.
 *
 *   - The design is independent of the transport type or implementation (not just IP)
 *   - All major functions operate asynchronously in the LT thread proc style.
 *   - Simple network applications are simple to create; just a few lines are needed.
 *   - More complex network applications are still possibl; capabilities gracefully expand as needed.
 *
 * Primary Objects:
 *
 *   - Transport: represents a network stack including any devices or drivers necessary to support it.
 *     There can be more than one transport running on a system. Although an IP-based stack is typical
 *     for a transport, that's not a strict requirement. The API allows any type of network implementation.
 *   - Socket: represents a data stream from one endpoint to another. Each socket encapsulates and manages
 *     all its related resources. Each socket gets associated with a transport.
 *
 */
typedef_LTLIBRARY_INTERFACE(LTNetDriver, 1) {
    u32  (*OpenTransport)    (LTTransportData   *tranData, LTSocket_Create create_socket);  // returns socket size, or zero on failure
    void (*CloseTransport)   (LTTransportDriver *driverData);
    bool (*GetTransportSpec) (LTTransportDriver *driverData, char *spec, u16 spec_size);
    void (*GetMetrics)       (LTTransportDriver *driverData, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics);
    s32  (*IsOperating)      (LTTransportDriver *driverData, LTTransport_Nudge nudge);
    bool (*OpenSocket)       (LTSocket_Data *socket);
    bool (*GetSocketSpec)    (LTSocket_Data *socket, char *spec, u16 spec_size);
    bool (*GetSocketProperty)(LTSocket_Data *socket, const char *name, void *value);
    bool (*SetSocketProperty)(LTSocket_Data *socket, const char *name, const void *value);
    void (*CloseSocket)      (LTSocket hSocket);
    void (*ConnectSocket)    (LTSocket_Data *socket);
    void (*DisconnectSocket) (LTSocket_Data *socket);
    s32  (*WriteSocket)      (LTSocket_Data *socket, const void *data, u32 data_len);
    s32  (*ReadSocket)       (LTSocket_Data *socket, void *data, u32 data_size);
    void (*ShowLwipStat)     (LTTransportDriver *driverData, bool logToServer);
    void (*ProcTransportMetrics) (LTTransportDriver *driverData, LTTransport_MetricsAction action, bool logToServer);
    bool (*IsTlsSupported)   (void);
} LTLIBRARY_INTERFACE;

// Data shared between LTNet transport and its driver-level implementation.
typedef struct LTSocket_Data {
    LTList_Node             node;            ///< Transport keeps a list of sockets
    //LTTransport           h_transport;     ///< Transport used for this socket
    LTTransportData        *transData;       ///< Shortcut to transport data
    LTSocket                h_socket;        ///< Socket handle for proc event callbacks
    char                   *spec;            ///< OpenSocket() spec used for config
    LTNetDriver            *driver;          ///< Reference to driver API
    LTEvent                 event;           ///< Socket status change event
    bool                    connected;       ///< Indicates socket is connected
    void                   *eventData;       ///< Refcount event handle shared with its parent listener
    LTAtomic                readPending;
    LTAtomic                writePending;
    // Extended from here with implementation private data
} LTSocket_Data;

#endif //LTNETDRIVER_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jun-22   hadrian     created
 *  12-Jul-22   hadrian     split out transport data and driver sections
 *  03-Aug-23   augustus    obsoleted LTAtomic32; changed to LTAtomic
 *  09-Apr-24   maximian    refcount LTEvent by holding LTSocket_Data->eventData
 */
