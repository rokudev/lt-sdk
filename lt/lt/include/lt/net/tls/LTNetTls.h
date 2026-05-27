/*******************************************************************************
 * include/lt/net/tls/LTNetTls.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * !!! Important !!!
 * (1) LTNetTls shall be called only by LTNetCore. Applications only request TLS with "tls" in socket spec through LTNetCore.
 * (2) Applications specify TLS options via the socket spec string passed to LTNetCore's OpenSocket.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLS_H
#define ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLS_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

/**
 * @brief LTNetTls Library for creating a TLS session as a socket for client to use.
 *        LTNetTLS-created sockets conform to the standard ILTSocket interface.
 */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetTls, 1) {

    LTSocket (*WrapSocket)(LTSocket hTransportSocket, const char *socketSpec, LTEvent transportSocketEvent, LTSocket_EventProc procFunc, void *procData, LTAtomic *socketWriteCount, LTAtomic *socketWriteFailCount);
        /**< @brief Create a new TLS socket which wraps a transport socket.
         *
         *  The socketSpec parameter used to open the transport socket can include TLS-related options,
         *  which this library will apply to the created TLS socket. The supported options are:
         *     - servername:[server name]                 - The TLS servername value expected from the server's certificate. ex: xyz.net
         *     - cakeys:[CA 1 id],[CA 2 id],...,[CA N id] - A comma-separated list of CA root key ids to be used to verify the server's certificate. The actual key data for each CA key id will be read from the keystore.
         *     - clientcert:[client cert id]              - The identifier for the client certificate/key. The actual cert/key data will be read from the keystore.
         *     - alpn:[alpn string]                       - The "application level protocol negotiation" string. ex: http/1.1 or ntske/1
         *
         * @param hTransportSocket     The transport socket that will be used to carry TLS data
         * @param socketSpec           The socket spec used to create the transport socket
         * @param transportSocketEvent The event which will be used to signal socket events from hTransportSocket
         * @param procFunc             The socket event callback which will receive socket events from the created TLS socket
         * @param procData             The procFunc callback data
         */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif  // ROKU_LT_INCLUDE_LT_NET_TLS_LTNETTLS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  21-Oct-22   gallienus   created
 */
