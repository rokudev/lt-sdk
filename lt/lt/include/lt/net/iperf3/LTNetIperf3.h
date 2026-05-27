/*******************************************************************************
 * <lt/net/iperf3/LTNetIperf3.h>                        iperf3 client and server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_IPERF3_H
#define ROKU_LT_INCLUDE_LT_NET_IPERF3_H
#include <lt/net/core/LTNetCore.h>
#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN
enum {
    kLTNetIperf3_Default_Port = 5201
};
void *LTNetIperf3_GetGlobals(void);
typedef void (LTNetIperf3_ReportCallback)(const char *report, void *clientData);

typedef struct LTNetIperf3_Parameters {
    // For all values in LTNetIperf3_Parameters: A value of 0 means use default.
    int port;       // defaults to 5201
    bool udp;       // defaults to using tcp
    u16 time;       // defaults to 10 (seconds)
    u32 bitrate;    // defaults to unlimited for TCP and 1Mbit/sec for UDP
    u16 dscp;       // defaults to 0, DC0
    u16 prio;       // default to 0, priority 0
    u16 mss;        // default to 1460
    u16 streams;    // default to 1
} LTNetIperf3_Parameters;

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetIperf3, 1) {

    void (* RunClient)(const char *ipaddr, LTNetIperf3_Parameters *params, LTNetIperf3_ReportCallback *callback, void *clientData);
        /* runs the iperf3 client
         *
         * @param ipaddr the ip address of the iperf3 server to connect to_char_type
         * @param params the iperf parameters or NULL to use default parameters
         * @param callback the reporting callback
         * @param clientData client data passed back to the reporting callback
         */

    void (* RunServer)(LTNetIperf3_Parameters *params, LTNetIperf3_ReportCallback *callback, void *clientData);
        /* runs the iperf3 server
         *
         * @param params the iperf parameters or NULL to use default parameters
         * @param callback the reporting callback
         * @param clientData client data passed back to the reporting callback
         */
    void (* KillClient)(void *);
    void (* KillServer)(void *);

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_IPERF3_H */
