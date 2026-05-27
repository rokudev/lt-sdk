/******************************************************************************
 * source/lt/net/srtpproxy/LTSrtpProxy.h - LTSrtp proxy
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SRTPPROXY_LTSRTPPROXY_H
#define ROKU_LT_SOURCE_LT_NET_SRTPPROXY_LTSRTPPROXY_H

typedef enum {
    // Session ops
    kLTSrtpProxy_PacketType_Session_Init,
    kLTSrtpProxy_PacketType_Session_Destroy,
    kLTSrtpProxy_PacketType_Session_RegisterForEvents,
    kLTSrtpProxy_PacketType_Session_UnregisterFromEvents,
    kLTSrtpProxy_PacketType_Session_Event,
    kLTSrtpProxy_PacketType_Session_OpenStream,
    kLTSrtpProxy_PacketType_Session_Pause,
    kLTSrtpProxy_PacketType_Session_Resume,
    kLTSrtpProxy_PacketType_Destroy_Done,

    // Stream ops
    kLTSrtpProxy_PacketType_Stream_Pause,
    kLTSrtpProxy_PacketType_Stream_Resume,
    kLTSrtpProxy_PacketType_Stream_Destroy,
} LTSrtpProxy_PacketType;



#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SRTPPROXY_LTSRTPPROXY_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Nov-24   trajan      created
 */
