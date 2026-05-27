/******************************************************************************
 * source/lt/net/socketproxy/LTSocketProxyServer.h - LTSocket proxy server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_SERVER_LTSOCKETPROXYSERVER_H
#define ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_SERVER_LTSOCKETPROXYSERVER_H

#include <lt/net/core/LTNetCore.h>

typedef struct LTProxiedSocket LTProxiedSocket;

typedef enum {
    LTProxiedSocket_Event_NoActivity    /**< No activity on this socket */
} LTProxiedSocket_Event;

typedef void (LTProxiedSocket_EventProc)(LTProxiedSocket_Event event, void * pClientData);

typedef_LTObject(LTProxiedSocket, 1) {
/** ___________________________________
 *   @section LTProxiedSocket Object
 *   * @brief a wrapper object for exposing a local LTSocket to a remote user.
 *   *
 *   * LTProxiedSocket provides an interface for exporting local LTSockets to remote users.
 \______________________________________________________________________________________*/

    void (* Init)(LTProxiedSocket *prxSocket, LTSocket socket);
        /**< Initializes the socket proxy instance.
         *
         *   Binds the proxied socket instance to a local LTSocket handle and
         *   sends a message announcing the creation of this socket proxy to the
         *   LTSocketProxyClient library running on the remote peer. Once complete,
         *   a socket proxy can be retrieved on the remote end using LTSocketProxyClient::GetSocket()
         *   using the id returned by LTProxiedSocket::GetId().
         *
         *   @param prxSocket the proxied socket instance.
         *   @param socket the local socket handle to be exported.
         */

    u32  (* GetId)(LTProxiedSocket *prxSocket);
        /**< Gets the proxied socket id.
         *
         *   Each new proxied socket is assigned a unique id which can be used
         *   by the remote user to get access to a corresponding socket proxy
         *   handle using LTSocketProxyClient::GetSocket(). This socket proxy
         *   handle implements the ILTSocket interface and works identically
         *   to a normal LTSocket handle.
         *
         *   @param prxSocket the proxied socket instance.
         *   @return the proxied socket id.
         */

    void (* Close)(LTProxiedSocket *prxSocket);
        /**< Closes the proxied socket to stop any transfers with media
         *
         *   @param prxSocket the proxied socket instance.
         */

    void (* SetSocket)(LTProxiedSocket *prxSocket, LTSocket hSocket);
        /**< Binds the proxied socket instance to a new socket.
         *
         *   @param prxSocket the proxied socket instance.
         *   @param hSocket the new socket to bind to.
         */

    void (* OnEvent)(LTProxiedSocket *prxSocket, LTProxiedSocket_EventProc * pCallback, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);
        /**< Registers a proxied socket callback.
         *
         *   @param prxSocket the proxied socket instance.
         *   @param pCallback the callback to be registered.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData client data to be passed to the callback.
         */

    void (* NoEvent)(LTProxiedSocket *prxSocket, LTProxiedSocket_EventProc * pCallback);
        /**< Unregisters a proxied socket callback.
         *
         *   @param prxSocket the proxied socket instance.
         *   @param pCallback the callback to be unregistered.
         */
} LTOBJECT_API;

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_SERVER_LTSOCKETPROXYSERVER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-Oct-24   trajan      created
 */
