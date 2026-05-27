/*******************************************************************************
 *
 * LTNetCore: Network Core
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTNETCORE_H_1
#define LTNETCORE_H_1

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>

LT_EXTERN_C_BEGIN

/*******************************************************************************
** Constant and Type Definitions
*******************************************************************************/

typedef enum {
    kLTTransport_Event_Unknown    = 0,    ///< Event unset or unknown
    kLTTransport_Event_Error      = 0x01, ///< General error
    kLTTransport_Event_DhcpStart  = 0x02, ///< DHCP start to discover (LwipWiFi only)
    kLTTransport_Event_DhcpRetry  = 0x03, ///< DHCP retrying (LwipWiFi only)
    kLTTransport_Event_DnsTimeout = 0x04, ///< DNS timeout err (LwipWiFi only)
    kLTTransport_Event_Up         = 0x10, ///< Link is up
    kLTTransport_Event_IP4AddrSet = 0x11, ///< IPv4 address has been set
    kLTTransport_Event_IP6AddrSet = 0x12, ///< IPv6 address has been set
    kLTTransport_Event_Down       = 0x20, ///< Link is down
    kLTTransport_Event_ResetMetrics = 0x30, ///< Reset LTTransport_Metrics.xxx_shadow counter
    kLTTransport_Event_ShowMetrics  = 0x31, ///< Show LTTransport_Metrics.xxx_shadow counter
} LTTransport_Event;

typedef enum {
    // Upper bits indicate category, lower bits indicate errors (error is always success + 1)
    kLTSocket_Event_Unknown       = 0,    ///< Event unset or unknown !!!NOT USED!!!
    kLTSocket_Event_Error         = 0x01, ///< General error

    kLTSocket_Event_SocketReady   = 0x08, ///< Socket ready for connection
    kLTSocket_Event_SocketError   = 0x09, ///< Socket error

    kLTSocket_Event_DnsResolved   = 0x10, ///< DNS name resolved
    kLTSocket_Event_DnsError      = 0x11, ///< DNS lookup failed
    kLTSocket_Event_DnsTimeout    = 0x12, ///< DNS timed out
    kLTSocket_Event_DnsRokuCache  = 0x13, ///< DNS timed out, but using IP from roku cache entry

    kLTSocket_Event_Connected     = 0x20, ///< Remote connected
    kLTSocket_Event_ConnectError  = 0x21, ///< Connection failed
    kLTSocket_Event_ConnectTimeout= 0x22, ///< Connection timed-out
    kLTSocket_Event_ConnectRequest= 0x28, ///< Remote asking to connect (for listen)
    kLTSocket_Event_Disconnected  = 0x2C, ///< Remote disconnected

    kLTSocket_Event_WriteReady    = 0x30, ///< Socket ready for writing (has space)
    kLTSocket_Event_WriteError    = 0x31, ///< Socket write error
    kLTSocket_Event_WriteTimeout  = 0x32, ///< Socket write time-out !!!NOT USED!!!

    kLTSocket_Event_ReadReady     = 0x40, ///< Socket ready for reading (has data)
    kLTSocket_Event_ReadError     = 0x41, ///< Socket read error
    kLTSocket_Event_ReadTimeout   = 0x42, ///< Socket read time-out !!!NOT USED!!!
} LTSocket_Event;

#define LTSocket_Event_Error(code) ((code) & 3) // Quick check for error cases

typedef LTHandle LTTransport;
typedef LTHandle LTSocket;

typedef const struct LTNetDriverApi LTNetDriver; // Note: Remains opaque at this LTNetCore layer

// Implementation private transport data is embedded within LTTransport_data defined below.
// Space is preallocated as a given size to avoid compiler "past end" errors.
// The implementation performs a compile time sizeof check to avoid exceeding this space.
typedef struct {
    u32 space[84];                      // See transport impl for Priv_Trans size check.
                                        // For now, need 320 bytes to hold Qemu's PrivTran.
} PrivImpl;

// This is the primary data structure for all transports. The transport implementation data is
// embedded within it. Both are stored in the transport handle allocation.
typedef struct {
    char              * spec;           ///< Original transport specification
    LTLibrary         * library;        ///< Transport library
    LTNetDriver       * driver;         ///< Interface into transport driver
    LTTransport         handle;         ///< Handle for self (used for events)
    LTEvent             event;          ///< Notifications on status changes
    u32                 socketSize;     ///< Socket size can vary between drivers
    LTList              socketList;     ///< Sockets that are using this transport
    PrivImpl            privData;       ///< Reserved space for private data
} LTTransport_Data;

// Basic transport metrics. This is intended to be implementation independent.
// Note: these counters can wrap, so any related computations need to handle that.
typedef struct {          // Make atomic?  Make fields 64 bit? !!!
    u32 connections;
    u32 lowerBytesTx;     // Lower layer data to/from packets (e.g. WiFi)
    u32 lowerBytesRx;
    u32 upperBytesTx;     // Upper layer data to/from application
    u32 upperBytesRx;
    u32 dropPacketsTx;     // example: WiFi transmit failed
    u32 dropPacketsRx;     // example: out of buffers
    u32 dropPacketsTx_shadow; // This counter is resettable
    u32 dropPacketsRx_shadow; // This counter is resettable
} LTTransport_Metrics;

typedef enum {
    kLTTransport_MetricsAct_Unknown = 0,
    kLTTransport_MetricsAct_Reset,           // reset LTTransport_Metrics.xxx_shadow
    kLTTransport_MetricsAct_Show             // show LTTransport_Metrics.xxx_shadow
} LTTransport_MetricsAction;

typedef enum {
    kLTTransport_Nudge_None,
    kLTTransport_Nudge_Soft,
    kLTTransport_Nudge_Hard,
    kLTTransport_Nudge_Reset,
} LTTransport_Nudge;

typedef enum {
    kLTNetDscp_CS0  = 0,
    kLTNetDscp_CS1  = 8,
    kLTNetDscp_AF11 = 10,
    kLTNetDscp_AF12 = 12,
    kLTNetDscp_AF13 = 14,
    kLTNetDscp_CS2  = 16,
    kLTNetDscp_AF21 = 18,
    kLTNetDscp_AF22 = 20,
    kLTNetDscp_AF23 = 22,
    kLTNetDscp_CS3  = 24,
    kLTNetDscp_AF31 = 26,
    kLTNetDscp_AF32 = 28,
    kLTNetDscp_AF33 = 30,
    kLTNetDscp_CS4  = 32,
    kLTNetDscp_AF41 = 34,
    kLTNetDscp_AF42 = 36,
    kLTNetDscp_AF43 = 38,
    kLTNetDscp_CS5  = 40,
    kLTNetDscp_EF   = 46,
    kLTNetDscp_CS6  = 48,
    kLTNetDscp_CS7  = 56,
} LTNetDscp;

typedef struct {
    u32 address;
    u16 port;
} LTNetIpv4Endpoint;

LT_INLINE LTNetIpv4Endpoint LTNetIpv4Endpoint_Any(void) LT_ISR_SAFE  {
    return (LTNetIpv4Endpoint){ 0, 0 };
}
LT_INLINE bool LTNetIpv4Endpoint_IsAny(LTNetIpv4Endpoint endpoint) LT_ISR_SAFE  {
    return (endpoint.address == 0) && (endpoint.port == 0);
}
LT_INLINE bool LTNetIpv4Endpoint_IsEqual(LTNetIpv4Endpoint endpoint1, LTNetIpv4Endpoint endpoint2) LT_ISR_SAFE  {
    return (endpoint1.address == endpoint2.address) && (endpoint1.port == endpoint2.port);
}

/*******************************************************************************
** Callback Proc Definitions
*******************************************************************************/

    typedef void (LTTransport_EventProc)(LTTransport hTransport, LTTransport_Event event, void *clientData);
    /**<
     * @brief The function prototype for transport-related events (async callback)
     *
     * Passed as an argument to OpenTransport(), this function gets called when a transport
     * changes state. For example when a link goes up, down, or changes address.
     *
     * See the LTTransport_Event enum for the list of possible events.
     *
     * @param[in] hTransport: handle for the transport to which this callback applies
     * @param[in] event: the transport event that triggered this callback
     * @param[in] clientData: client data originally provided to OpenTransport
     */

    typedef void (LTSocket_EventProc)(LTSocket hSocket, LTSocket_Event event, void *clientData);
    /**<
     * @brief The function prototype for socket-related events (async callback)
     *
     * Passed as an argument to OpenSocket(), this function is called when a socket changes
     * state that may require user processing.
     *
     * See the LTSocket_Event enum for the list of possible events.
     *
     * @param[in] hSocket: handle for the socket to which this callback applies
     * @param[in] event: the socket event that triggered this callback
     * @param[in] clientData: client data originally provided to OpenSocket
     */

/*******************************************************************************
** API Function Definitions
*******************************************************************************/

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTNetCore, 1);

/**
 * Network Transport Library
 *
 * Objectives of LTNetCore:
 *
 *   - The design is independent of the transport type or implementation (not just for ethernet
 *     IP, but could be used for Bluetooth or even file system access.)
 *   - All major functions operate asynchronously using LTEvent callback functions.
 *   - Simple network applications must be simple to create. Don't make them harder than necessary.
 *   - More complex network applications are still possible; capabilities expand as needed.
 *
 * Primary Objects:
 *
 *   - TRANSPORT: represents a network stack including any devices or drivers necessary to support it.
 *     There can be more than one transport running on a system. An IP-based stack is a typical
 *     transport, but that's not a requirement. The API allows any type of implementation.
 *   - SOCKET: represents a data stream from one endpoint to another (or multiple). Each socket
 *     is associated with its related transport and encapsulates/manages related resources.
 *
 */
struct LTNetCoreApi {   // A struct not a macro -- allows IDEs to work properly

    INHERIT_LIBRARY_BASE

    LTTransport (*OpenTransport)(const char *transportSpec, LTTransport_EventProc eventProc, void *clientData);
    /**<
     * @brief Open access to a network transport layer
     *
     * This function is used to initialize a new data transport mechanism. This design generalizes
     * the concept of transports. Although mostly applied to Internet protocols, the same mechanism
     * can support a wide range of data transport methods, including for instance, file storage devices.
     *
     * This function is typically called by LT OS initialization/configuration using a preset
     * specification. Applications may not need to call this function if some other library
     * has already started the transport.
     *
     * The @p transportSpec is an extensible method of specifying a range of parameters
     * for the transport and data-link layers. For example, for an IP transport, parameters might
     * include the local IP address, gateway, DNS, or option to use DHCP. Because the spec is string
     * based, it is capable of specifying both IPv4 and IPV6 addresses, symbolic names, while being
     * forward extensible and backwards compatible.
     *
     * Typical examples for an IP transport are:
     *
     *      "IpWiFi dhcp"
     *      "IpWiFi ip: 192.168.0.20 gw: 192.168.0.1 mask: 255.255.255.0"
     *
     * A handle to the transport is returned and is used for opening sockets or getting transport
     * metrics. A return of zero indicates an error. View the LT log for details.
     *
     * Whenever the state of the transport changes, an LTTransport_Event will be sent to the @p proc
     * function within the thread context that called the OpenTransport().
     *
     * Specifying a NULL @p transportSpec will return the default transport handle and can also
     * specify additional @p proc functions to be called on events.
     *
     * When done, LTCore->DestroyHandle() is used to shut-down and free transport resources.
     *
     * @param[in] transportSpec: string that specifies transport parameters
     * @param[in] eventProc: function that receives events for this transport (up/down/error)
     * @param[in] clientData: client data pointer provided to the eventProc function
     * @return handle to the transport or zero for failure (log provides details)
     */

    void (*GetTransportSpec)(LTTransport hTransport, char *transportSpec, u16 specSize);
    /**<
     * @brief Return the specification string for a given transport handle
     *
     * The @p transportSpec string that is returned will be roughly equivalent to that
     * specified in its original OpenTransport() call with additional fields as the
     * transport gets further configured (by DHCP for example.)
     *
     * If a zero transport is passed, then the spec for the default transport is returned
     * or an empty string if there is none.
     *
     * If any parameter is invalid, this function does nothing. Check your work.
     *
     * @param[in] hTransport: handle for a previously opened transport or zero for default
     * @param[out] transportSpec: string to receive transport specification, null terminated
     * @param[in] specSize: maximum size of the transportSpec string including terminator
     */

    void (*GetMetrics)(LTTransport hTransport, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics);
    /**<
     * @brief Return transport metrics
     *
     * These are device-independent metrics for bytes in and out of the lower and upper layers
     * of the transport stack.
     *
     * If a zero transport is passed, then the metrics for the default transport are returned.
     *
     * If any parameter is invalid, this function does nothing. Check your work.
     *
     * @param[in] hTransport: handle for a previously opened transport or zero for default
     * @param[out] metrics: pointer to metrics struct for returned values
     * @param[in] sizeOfMetrics: size of metrics struct being passed
     */

    s32 (*IsOperating)(LTTransport hTransport, LTTransport_Nudge nudge);
    /**<
     * @brief Check a transport's operating status
     *
     * Indicates the general operating state of the transport in terms of data transferred.
     *
     * It's possible for some types of networking to "stall" without indicating a
     * disconnected state (no disconnect event). For example, WiFi may appear connected
     * but not be receiving data frames or somehow lost its routing or gateway.
     * When monitoring a transport state, it is good policy to periodically check that
     * traffic is flowing, and if no traffic has been heard for a period of time, "nudge"
     * the data-link in some minimal way that will confirm a connected state.
     *
     * To do this in a transport independent way, this function returns an integer that is
     * counting upward. This may be a packet counter or a byte counter; it doesn't
     * matter. If a given number (determined by user) of calls to this function return the same
     * integer, then it's possible the data-link layer has stalled. At that point, the user
     * can "nudge" that layer to confirm that it is actually operating. For example, the user
     * might call it every second and only nudge if there's been no change after 5 seconds.
     *
     * Note that the returned counter can wrap. This is indicated by returning a zero result.
     *
     * The actual mechanism of nudging is not defined by this transport layer, and it may vary
     * depending on the LT device platform and implementation. However, there can be different
     * levels of nudging. For example, for IP, a soft nudge may just send an ICMP packet that
     * expects a response, like a DHCP inform or ARP ping. But, if that fails, a hard nudge may
     * cause the WiFi link to disconnect and reassociate.
     *
     *
     * @param[in] hTransport: handle for a previously opened transport
     * @param[in] nudge: how hard to nudge the data-link layer to get it to respond
     * @return An advancing counter or -1 when not connected.
     */

    LTSocket (*OpenSocket)(LTTransport hTransport, const char *socketSpec, LTSocket_EventProc eventProc, void *clientData);
    /**<
     * @brief Open a socket with the given specifications (async)
     *
     * Initializes a socket for a given transport and returns a handle for it. Depending on the transport
     * and the @p socketSpec, this may also initiate async actions such as connecting to a remote endpoint.
     *
     * The @p transport identifies either a transport from OpenTransport or zero to use the default transport
     * (which must have been opened earlier during startup).
     *
     * The @p socketSpec is an extensible, abstract method of specifying a wide range of parameters
     * related to the socket.
     *
     * Examples:
     *
     *     tcp host: example.com port: 80
     *     tcp ip: 93.184.216.34 port: 80
     *     tcp listen port: 5555
     *     udp ip: 192.168.0.1 port: 8080
     *
     *  For IP transports, these spec options may be available:
     *
     *   - Protocol name: tcp, udp, icmp, dns
     *   - Listen mode: listen
     *   - IPv4 address such as "ip: 192.168.0.20"
     *   - IPv6 address such as "ip: 2607:f0d0:1002:51::4"
     *   - Port number such as "port: 80"
     *   - DNS host name such as "host: google.com"
     *   - Other: "debug, "no-auto-connect"
     *
     * Various default specifications are allowed. For example not specifying a protocol will
     * default to "tcp". Not specifying a mode will default to "client".
     *
     * Whenever the state of the socket changes, the @p proc function will be called within
     * the context of the thread that called OpenSocket().
     * Events include: connected, disconnected, read_ready, write_ready, address_resolved, and more.
     * See LTSocket_Event for the full list. Not all events may be supported, depending on the transport.
     *
     * For connection-based protocols, an auto-connect is initiated immediately on OpenSocket()
     * unless socketSpec indicates "no-auto-connect".
     *
     * When a host name is provided, a DNS lookup is initiated before proceeding with the auto-connect.
     * The proc function will be called once the lookup is complete and the remote address has been
     * set for the socket. The GetSocketSpec function can be used to find out the IP address.
     *
     * When done, LTCore->DestroyHandle() is used to close the socket and free its resources.
     *
     * @param[in] hTransport: handle to transport or zero for default transport
     * @param[in] socketSpec: string that specifies socket parameters
     * @param[in] eventProc: function that receives events for this socket
     * @param[in] clientData: client data pointer provided to eventProc function
     * @return handle to the socket or zero for failure (log provides details)
     */

    void (*GetSocketCounts)(u32 *createdCount, u32 *destroyedCount, u32 *writeCount, u32 *writeFailCount);
    /**<
     * @brief Get socket health data
     * @param[out] createdCount: accumulated count of created sockets
     * @param[out] destroyedCount: accumulated count of destroyed sockets
     * @param[out] writeCount: accumulated count of successful socket writes
     * @param[out] writeFailCount: accumulated count of failed socket writes
     */

    void (*ShowLwipStat)(LTTransport hTransport, bool logToServer);
    /**<
     * @brief Dump LWIP memory stats
     *
     * Dump LWIP memory stats
     *
     * @param[in] hTransport: handle for a previously opened transport or zero for default
     * @param[in] logToServer: whether log to server or not.
     */

    void (*ProcTransportMetrics)(LTTransport hTransport, LTTransport_MetricsAction action, bool logToServer);
    /**<
     * @brief Process transport metrics
     *
     * Process transport metrics.
     *
     * @param[in] hTransport: Handle for a previously opened transport or zero for default
     * @param[in] action: Action to be done to LTTransport_Metrics.xxx_shadow
     * @param[in] logToServer: Used when action=kLTTransport_MetricsAct_Show, 1: dump log to server, 0: console only
     */
};

TYPEDEF_LTLIBRARY_INTERFACE(ILTSocket, 1);

struct ILTSocketApi {

    INHERIT_INTERFACE_BASE

    void (*OnSocketEvent)(LTSocket socket, LTSocket_EventProc proc, void *procData);
    void (*NoSocketEvent)(LTSocket socket, LTSocket_EventProc proc);

    void (*GetSocketSpec)(LTSocket hSocket, char *socketSpec, int specSize);
    /**<
     * @brief Return the specification string for a given socket
     *
     * The @p socketSpec string that is returned will be roughly equivalent to that
     * specified in its original OpenSocket() call with additional fields as the
     * socket gets further configured. For example, when a socket gets connected
     * this function can be used to obtain the remote IP address and port.
     *
     * @param[in] hSocket: handle for a previously opened socket
     * @param[in] socketSpec: string to receive socket specification, null terminated
     * @param specSize: maximum size of the @p socketSpec string including terminator
     */

    bool (*SetProperty)(LTSocket hSocket, const char *name, const void *value);
    /**<
     * @brief Sets a property value of a given socket
     *
     * Socket properties are an extensible, abstract method of specifying a wide range
     * of values related to the socket. Supported properties are dependent on the socket
     * protocol and transport implementation. Each property is defined by a string name
     * and a value whose data type is dependent on the property.
     *
     * Transports MUST support setting the following properties and MAY support additional ones:
     *
     *    -====================================================================================================-
     *    | Name               | Data Type          | Protocol  | Description                                  |
     *    -====================================================================================================-
     *    | send.endpoint.v4   | LTNetIpv4Endpoint  | udp,raw   | Network endpoint to which subsequent packets |
     *    |                    |                    |           | written with WriteSocket() will be sent      |
     *    -====================================================================================================-
     *
     * @param[in] hSocket: handle for a previously opened socket
     * @param[in] name: name of the property to be set
     * @param[in] value: value of the property to be set
     * @return true if the property was set successfully, false if not supported or setting failed
     */

    bool (*GetProperty)(LTSocket hSocket, const char *name, void *value);
    /**<
     * @brief Gets a property value of a given socket
     *
     * Socket properties are an extensible, abstract method of specifying a wide range
     * of values related to the socket. Supported properties are dependent on the socket
     * protocol and transport implementation. Each property is defined by a string name
     * and a value whose data type is dependent on the property.
     *
     * Transports MUST support getting the following properties and MAY support additional ones:
     *
     *    -====================================================================================================-
     *    | Name               | Data Type          | Protocol  | Description                                  |
     *    -====================================================================================================-
     *    | recv.endpoint.v4   | LTNetIpv4Endpoint  | udp,raw   | Network endpoint from which the last packet  |
     *    |                    |                    |           | received with ReadSocket() was be sent       |
     *    -====================================================================================================-
     *    | local.endpoint.v4  | LTNetIpv4Endpoint  | udp,raw   | Network endpoint to which the socket is      |
     *    |                    |                    |           | bound locally                                |
     *    -====================================================================================================-
     *
     * @param[in] hSocket: handle for a previously opened socket
     * @param[in] name: name of the property to be read
     * @param[in] value: pointer to memory into which the property value will be copied
     * @return true if the property was read successfully, false if not supported or read failed
     */

    void (*ConnectSocket)(LTSocket hSocket);
    /**<
     * @brief Initiate or accept socket connection (async)
     *
     * Typically, OpenSocket() will begin the connection process or otherwise setup
     * the transport layer (even for connection-less sockets). However, if the no-auto-connect
     * was specified, then this function can be used for that purpose.
     *
     * Note that the connection state changes asynchronously and will be indicated by an event.
     * The return from this function does not guarantee a connection.
     *
     * @param[in] hSocket: handle for a previously opened socket
     */

    void (*DisconnectSocket)(LTSocket hSocket);
    /**<
     * @brief Disconnect socket endpoint
     *
     * A disconnect indicates the end of transfer. No more data should be read or written after
     * this function is called.
     *
     * Disconnecting a socket does not free its resources. To do that, use LTCore->DestoryHandle.
     *
     * @param[in] hSocket: handle for a previously opened socket
     */

    s32 (*WriteSocket)(LTSocket hSocket, const void *data, u32 dataSize);
    /**<
     * @brief Write bytes to a socket (async)
     *
     * This function is called from your LTSocket_EventProc() callback whenever data needs to be written.
     * Data will be transferred from your @p data pointer to the transport's internal buffers.
     * There is no intermediate buffering within LTNetCore itself. Buffering happens in the transport stack.
     *
     * Usually your code will write small segments of data to avoid large memory allocations within the
     * transport buffering mechanism. A packet or two at a time works well. They are written out immediately.
     *
     * This function returns the number of bytes written. Some transports may return only the full size or zero.
     * Others may return less than full size. A zero return indicates the socket is still working, but the write
     * needs be attempted again, often due to low memory. A negative return indicates an unrecoverable error.
     * See the log for details.
     *
     * @param[in] hSocket: handle for a previously opened socket
     * @param[in] data: pointer to data to be written
     * @param[in] dataSize: number of bytes to write, or zero, or negative for error
     * @return The number of bytes written. Zero means low memory. Negative indicates an error (see log)
     */

    s32 (*ReadSocket)(LTSocket hSocket, void *data, u32 dataSize);
    /**<
     * @brief Read bytes from a socket (async)
     *
     * This function is called from your LTSocket_EventProc() callback whenever data is available to be read.
     *
     * If you provide a @p data pointer, data will be transferred to that location from the transport's internal
     * buffers. The size will be limited to the @p dataSize specified. The size may also be limited by the
     * transport protocol. For instance, UDP may return only the next packet or a block transfer device may
     * return only the next full block. The length of the data transfer is returned. If not all available data
     * is read, the transport's buffer will continue to store it until the next ReadSocket is called.
     *
     * If a NULL @p data pointer is specified, then @p dataSize bytes will be deleted from internal buffers.
     * If @p dataSize is zero, then the length of available data is returned (what would be read if a buffer
     * were provided, subject to the same protocol limits described above.)
     *
     * When no data is available, a zero will be returned.
     *
     * A negative return indicates an unrecoverable error.
     *
     * Note: The ReadReady event only occurs as new data arrives via the transport. If your code
     * does not read all available data, it will not receive another ReadReady event until new data
     * has arrived. Therefore, it is wise to read all available data or provide some other mechanism
     * such as a timer to continue reading data.
     *
     * Also note: It is possible for multiple ReadReady events to be queued as data arrives. If your
     * code reads all available data, it is still possible to receive ReadReady events but with no data
     * available. This is due to the async nature of the event mechanism. Your code can ignore those events.
     *
     * @param[in] hSocket: handle for a previously opened socket
     * @param[in] data: pointer to buffer to hold data that is read, or NULL if no data is to be read
     * @param[in] dataSize: number of bytes to read or delete, or zero to get the number of bytes available
     * @return number of bytes actually read or available, or zero, or negative for error
     */
};

LT_EXTERN_C_END

#endif //LTNETCORE_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  04-May-22   hadrian     created
 */
