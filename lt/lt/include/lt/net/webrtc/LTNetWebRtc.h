/*******************************************************************************
 * lt/net/webrtc/LTNetWebRtc.h - WebRTC Agent
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_WEBRTC_LTNETWEBRTC_H
#define ROKU_LT_INCLUDE_LT_NET_WEBRTC_LTNETWEBRTC_H

#include <lt/LTTypes.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/srtp/LTNetSrtp.h>
LT_EXTERN_C_BEGIN

typedef struct LTRtcPeerConnection LTRtcPeerConnection;

typedef enum {
    kLTRtcIceConnectionState_New,                   /* New connection */
    kLTRtcIceConnectionState_Checking,              /* Start Ice gathering */
    kLTRtcIceConnectionState_NewIceCandidate,       /* New Ice candidate found */
    kLTRtcIceConnectionState_GatheringComplete,     /* Start Ice gathering */
    kLTRtcIceConnectionState_Connected,             /* Peer connection established */
    kLTRtcIceConnectionState_Completed,             /* Peer dtls session established */
    kLTRtcIceConnectionState_Failed,                /* Connection failed */
    kLTRtcIceConnectionState_Disconnected,          /* Disconnected */
    kLTRtcIceConnectionState_Closed,                /* Connection closed */
} LTRtcIceConnectionState;

typedef enum {
    kLTRtcPeerConnectionEvent_NewIceCandidate,          /* New Ice candidate gathered */
    kLTRtcPeerConnectionEvent_IceGatheringComplete,     /* Ice gathering complete */
    kLTRtcPeerConnectionEvent_Connected,                /* Ice connection complete with the peer */
    kLTRtcPeerConnectionEvent_Completed,                /* DTLS session established with the peer */
    kLTRtcPeerConnectionEvent_Disconnected,             /* Peer disconnected */
    kLTRtcPeerConnectionEvent_MediaStreamError,         /* Media stream error */
    kLTRtcPeerConnectionEvent_Failed,                   /* Peer connection failed */
    kLTRtcPeerConnectionEvent_ReNegotiated,             /* Session renegotiated */
} LTRtcPeerConnectionEvent;

typedef void (LTRtcPeerConnection_ConnectionEventProc)(LTRtcPeerConnection *peerConn, LTRtcPeerConnectionEvent event, void *pClientData);
/**< callback for indicating the occurrence of ICE candidate gathering events.
 *
 *   This callback is called to indicate the occurrence of peer connection events.
 *
 *   @param hPeerConn the client instance
 *   @param event the current connection event which has occurred.
 *   @param pClientData the client data that was specified when the client was created.
 */

/*______________________________
 / LTRtcPeerConnection Object */
typedef_LTObject(LTRtcPeerConnection, 1) {
    LTRtcIceConnectionState (* GetConnectionState)(LTRtcPeerConnection *peerConn);
        /**< gets the peer connection's current connection state.
         *
         *   @param hPeerConn the handle of the peer connection.
         *   @returns the current connection state.
         */

    void (*OnConnectionEvent)(LTRtcPeerConnection *peerConn,
                              LTRtcPeerConnection_ConnectionEventProc *pCallback,
                              LTThread_ClientDataReleaseProc *pClientDataReleaseProc,
                              void *pClientData);
        /**< registers a callback for RTC Agent connection events
         *
         *   @param hPeerConn the handle of the peer connection.
         *   @param pCallback the callback.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData the client provided data to be delivered in the callback.
         */

    void (*NoConnectionEvent)(LTRtcPeerConnection *peerConn,
                              LTRtcPeerConnection_ConnectionEventProc *pCallback);
        /**< unregisters the callback for ICE gathering complete event
         *
         *   @param hPeerConn the handle of the client.
         *   @param pCallback the callback.
         */

    const char * (*GetLocalDescription)(LTRtcPeerConnection *peerConn);
        /**< gets the local session description in SDP format.
         *
         *   Gets the session description to be sent to the remote peer in answer to
         *   it's offer.
         *
         *   @param hPeerConn the peer connection handle.
         *   @returns the local session description in SDP format.
         */

    const char *(*GetLocalIceCandidate)(LTRtcPeerConnection *peerConn);
        /**< gets the local ice candidate description to be sent in ICE format.
         *
         *   Gets the local ice candidates to be sent to the remote peer in answer.
         *
         *   @param hPeerConn the peer connection handle.
         *   @returns the local ice candidate description in ICE format.
         */

    void (*SetRemoteDescription)(LTRtcPeerConnection *peerConn,
                                 const char *pRemoteDescription);
        /**< sets the remote peer's session description.
         *
         *   Sets the session description received from the remote peer and initiates
         *   ICE candidate gathering in order to generate an answer session description.
         *
         *   @param hPeerConn the handle of the client.
         *   @param pRemoteDescription the remote session description in SDP format.
         */

    void (*AddIceCandidate)(LTRtcPeerConnection *peerConn,
                                 const char *pRemoteIceCandidate);
        /**< sets the remote peer's new ice candidates description.
         *
         *   Sets the new ice candidates received from the remote peer.
         *
         *   @param hPeerConn the handle of the client.
         *   @param pRemoteIceCandidate the remote ice candidates description.
         */

    void (*AddIceServer)(LTRtcPeerConnection *peerConn,
                         const char *serverSpec);
        /**< Adds a STUN or TURN server to be used to gather ICE candidates
         *
         *   @param hPeerConn the handle of the ICE agent.
         *   @param serverSpec spec string containing the connection details for the ICE server. The expected format is:
         *     "<protocol>:<url>[:<port>] [<protocol-specific parameter name>:<protocol-specific parameter value>]*"
         *         <protocol> - the ICE server protocol ("stun" or "turn")
         *         <url>  - the ICE server url
         *         <port> - the ICE server port. (defaults to 3478 if not provided)
         *     Supported parameters by protocol:
         *         STUN:
         *             none
         *         TURN:
         *             username - the TURN server username
         *             password - the TURN server password
         *     Example:
         *         "stun:stun.l.google.com:19302"
         */
    bool (*Init)(LTRtcPeerConnection *peerConn, const char *pSessionId);
        /**< Initializes and sets the webrtc session id.
         *
         *   Sets the unique session id received from cloud.
         *
         *   @param hPeerConn the handle of the client.
         *   @param pSessionId the unique session id.
         *   @returns success or failure
         */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_WEBRTC_LTNETWEBRTC_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
