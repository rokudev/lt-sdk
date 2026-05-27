/******************************************************************************
 * source/lt/net/socketproxy/LTSocketProxyClient.h - LTSocket proxy client
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_CLIENT_LTSOCKETPROXYCLIENT_H
#define ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_CLIENT_LTSOCKETPROXYCLIENT_H

#include <lt/net/core/LTNetCore.h>

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTSocketProxyClient, 1) {
/** ___________________________________
 *   @section socketproxyclient_library LTSocketProxyClient Library
 *   * @brief a library for proxied access to remote sockets.
 *   *
 *   * LTSocketProxyClient provides proxied access to remote sockets
 \______________________________________________________________________________________*/

    LTSocket (* GetSocket)(u32 id);
        /**< gets a socket proxy by id
         *
         *   @param id the proxied socket id.
         *   @return the LTSocket instance
         */
} LTLIBRARY_INTERFACE;

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_CLIENT_LTSOCKETPROXYCLIENT_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-Oct-24   trajan      created
 */
