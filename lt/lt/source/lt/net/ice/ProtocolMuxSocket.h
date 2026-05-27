/*******************************************************************************
 * source/lt/net/ice/ProtocolMuxSocket.h - UDP socket protocol multiplexer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

 #ifndef ROKU_LT_SOURCE_LT_NET_ICE_PROTOCOLMUXSOCKET_H
 #define ROKU_LT_SOURCE_LT_NET_ICE_PROTOCOLMUXSOCKET_H

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTRtcProtocol_STUN,
    kLTRtcProtocol_DTLS,
    kLTRtcProtocol_SRTP,
    kLTRtcProtocol_Count
} LTRtcProtocol;

struct ProtocolMuxSocket;
typedef struct ProtocolMuxSocket ProtocolMuxSocket;

bool
ProtocolMuxSocket_LibInit(void);

void
ProtocolMuxSocket_LibFini(void);

ProtocolMuxSocket *
ProtocolMuxSocket_Create(LTSocket hSocket, LTNetIpv4Endpoint defaultEndpoint);

void
ProtocolMuxSocket_Destroy(ProtocolMuxSocket *mux);

LTSocket
ProtocolMuxSocket_GetProtocolSocket(ProtocolMuxSocket *mux, LTRtcProtocol protocol);

void
ProtocolMuxSocket_SetDefaultEndpoint(ProtocolMuxSocket *mux, LTNetIpv4Endpoint endpoint);

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_ICE_PROTOCOLMUXSOCKET_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Jul-23   trajan      created
 */
