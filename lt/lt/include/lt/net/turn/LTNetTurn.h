/*******************************************************************************
 * <lt/net/turn/LTNetTurn.h> - Traversal Using Relays for NAT (TURN) Client
 *
 * The LTNetTurn library implements a TURN client protocol as defined by RFC-8656.
 * The TURN protocol can be used to relay data between peers via a public intermediate
 * relay server in cases where direct communications are not possible due to the
 * presence of restrictive NAT devices between them.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_TURN_LTNETTURN_H
#define ROKU_LT_INCLUDE_LT_NET_TURN_LTNETTURN_H

#include <lt/LTTypes.h>
#include <lt/net/stun/LTNetStun.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTTurnEvent_CreateAllocationSuccess,
    kLTTurnEvent_ChannelBindingSuccess,
} LTNetTurnEvent;

typedef struct LTTurnClient LTTurnClient;
typedef void (LTTurnClient_TurnEventProc)(LTTurnClient *client, LTNetTurnEvent event, void *pClientData);

/*_______________________
 / LTTurnClient Object */
typedef_LTObject(LTTurnClient, 1) {
    void (*Init)(LTTurnClient *client, LTStunAgent *stunAgent, LTNetIpv4Endpoint serverEndpoint, const char *username, const char *password);
        /**< Initializes the TURN client.
         *
         *   @param client the client instance.
         *   @param stunAgent the STUN agent to be used to send/receive messages.
         *   @param serverEndpoint the server's address/port.
         *   @param username the username to be used in communications with the server.
         *   @param password the password to be used to authenticate messages with the server.
         */

    void (*CreateAllocation)(LTTurnClient *client);
        /**< Initiates creation of an allocation with the TURN server.
         *
         *   Initiates creation of an allocation with the TURN server. The result of the allocation
         *   request is communicated asynchronously via the event handler registered with OnTurnEvent.
         *
         *   @param client the client instance.
         */

    void (*BindChannel)(LTTurnClient *client, LTNetIpv4Endpoint peerEndpoint);
        /**< Configures permissions and binds a channel to a given endpoint.
         *
         *   Initiates creation of permissions and a channel binding for a given endpoint on an allocation
         *   previously created on the TURN server. The result of the permission and channel binding requests
         *   are communicated asynchronously via the event handler registered with OnTurnEvent.
         *
         *   @param client the client instance.
         *   @param peerEndpoint the client instance.
         */

    LTSocket (*GetChannelSocket)(LTTurnClient *client, LTNetIpv4Endpoint peerEndpoint);
        /**< Gets a socket over which data can be sent/received to/from a peer via the TURN relay.
         *
         *   Creates a channel binding for a given peer endpoint on the TURN server and
         *   returns a socket handle which can be used to transparently communicate with
         *   that peer via the TURN relay.
         *
         *   @param client the client instance.
         *   @param peerEndpoint the peer endpoint to which the channel should be bound.
         *   @return the channel-bound socket handle.
         */

    LTNetIpv4Endpoint (*GetRelayedAddress)(LTTurnClient *client);
        /**< Gets the relayed address associated with an allocation created with CreateAllocation().
         *
         *   The relayed address is the address on the TURN server that peers may send data to in
         *   order to have it relayed to this TURN client.
         *
         *   @param client the client instance.
         *   @return the relayed address.
         */

    LTNetIpv4Endpoint (*GetMappedAddress)(LTTurnClient *client);
        /**< Gets the mapped address returned with the successful creation of an allocation.
         *
         *   The mapped address is the address of this TURN client as seen by the TURN server
         *   after exiting the last NAT device along the path to the server.
         *
         *   @param client the client instance.
         *   @return the mapped address.
         */

    void (*OnTurnEvent)(LTTurnClient *client, LTTurnClient_TurnEventProc *pCallback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< Registers a callback used to indicate receipt of a valid TURN message.
         *
         *   @param client the client instance.
         *   @param pCallback the callback.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData client data which will be passed to the callback.
         */

    void (*NoTurnEvent)(LTTurnClient *client, LTTurnClient_TurnEventProc *pCallback);
        /**< Unregisters a message receive callback.
         *
         *   @param client the client instance.
         *   @param pCallback the message receive callback.
         */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_TURN_LTNETTURN_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Sep-23   trajan      created
 */
