/*******************************************************************************
 *
 * LTNetEcho: Network ICMP Echo (Ping)
 * -----------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTNETECHO_H_1
#define LTNETECHO_H_1

typedef void (LTNetEcho_Proc)(LTHandle hPing, u16 id, u16 count, void *data);
    /**<
     * @brief The function prototype for ping reply events
     *
     * Passed as an argument to Ping(), this function gets called when an echo
     * command (a ping) receives a reply.
     *
     * @param[in] hPing: handle used by the Ping() function
     * @param[in] id: ICMP identifier for the ping reply, 0 for outgoing request
     * @param[in] count: counter for the ping reply
     * @param[in] data: user data
     */

enum {
    LTNetEcho_MaxPayload = 1024  // arbitrary limit
}; 

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetEcho, 1) {

bool (*Ping)(LTHandle hTransport, const char *remoteAddress, u16 id, u16 count, u16 period,
        u8 *payload, u16 payloadSize, LTNetEcho_Proc eventFunc, void *eventData);
    /**<
     * @brief Ping a network device at a remote address
     *
     * Send an ICMP echo command to a remote address and trigger an event on the reply.
     *
     * @param[in] hTransport: handle for an open transport or zero for default transport
     * @param[in] remoteAddress: IP address as a string; if NULL, address is IP gateway
     * @param[in] id: ICMP identifier field (set to any u16 you want)
     * @param[in] count: number of pings to send
     * @param[in] period: optional time period in msec between pings (default 1000 msec)
     * @param[in] payload: optional payload to send or null (MaxPayload is the size limit)
     * @param[in] payloadSize: optional size of payload in bytes (if string, include terminator)
     * @param[in] eventFunc: ping reply event function
     * @param[in] eventData: ping reply event function data
     */

} LTLIBRARY_INTERFACE;

#endif //LTNETECHO_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Jun-22   hadrian     created
 */
