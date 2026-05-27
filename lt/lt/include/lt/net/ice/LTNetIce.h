/*******************************************************************************
 * <lt/net/ice/LTNetIce.h> - Interactive Connectivity Establishment (ICE) Agent
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_ICE_LTNETICE_H
#define ROKU_LT_INCLUDE_LT_NET_ICE_LTNETICE_H

#include <lt/LTTypes.h>
#include <lt/core/LTArray.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

typedef struct LTIceAgent LTIceAgent;

typedef enum {
    kLTIceConnectionState_New,
    kLTIceConnectionState_Checking,
    kLTIceConnectionState_Connected,
    kLTIceConnectionState_Completed,
    kLTIceConnectionState_Failed,
    kLTIceConnectionState_Disconnected,
    kLTIceConnectionState_Closed,
} LTIceConnectionState;

LT_INLINE const char *
LTIceConnectionStateToString(LTIceConnectionState connState) {
    switch (connState) {
        case kLTIceConnectionState_New:          return "new";
        case kLTIceConnectionState_Checking:     return "checking";
        case kLTIceConnectionState_Connected:    return "connected";
        case kLTIceConnectionState_Completed:    return "completed";
        case kLTIceConnectionState_Failed:       return "failed";
        case kLTIceConnectionState_Disconnected: return "disconnected";
        case kLTIceConnectionState_Closed:       return "closed";
        default:                                 return "unknown";
    }
}

typedef enum {
    kLTIceAgentEvent_NewIceCandidate,
    kLTIceAgentEvent_IceGatheringComplete,
    kLTIceAgentEvent_CandidateSelected,
    kLTIceAgentEvent_Disconnected,
} LTIceAgentEvent;

typedef enum {
    kLTIceCandidateType_Host,
    kLTIceCandidateType_ServerReflexive,
    kLTIceCandidateType_PeerReflexive,
    kLTIceCandidateType_Relay,
    kLTIceCandidateType_Count
} LTIceCandidateType;

typedef enum {
    kLTIceCandidateStatus_ToBeSent,
    kLTIceCandidateStatus_Sent,
    kLTIceCandidateStatus_Invalid
} LTIceCandidateStatus;

LT_INLINE const char *
LTIceCandidateTypeToString(LTIceCandidateType candidateType) {
    switch (candidateType) {
        case kLTIceCandidateType_Host:            return "host";
        case kLTIceCandidateType_ServerReflexive: return "srflx";
        case kLTIceCandidateType_PeerReflexive:   return "prflx";
        case kLTIceCandidateType_Relay:           return "relay";
        default:                                  return "unknown";
    }
}

typedef enum {
    kLTIceCandidateTransport_Unknown,
    kLTIceCandidateTransport_UDP,
    kLTIceCandidateTransport_TCP,
} LTIceCandidateTransport;

enum {
    kLTIceCandidate_FingerprintLen     = 32,
    kLTIceCandidate_FoundationLen      = 32 + 1,
    kLTIceCandidate_UserFragmentLenMax = 256 + 1,
    kLTIceCandidate_PasswordLenMax     = 256 + 1,
};

/*_________________________
 / LTIceCandidate Object */
typedef_LTObject(LTIceCandidate, 1) {
    void                    (*Init)(LTIceCandidate *candidate, LTIceAgent *agent, LTIceCandidateTransport transport, LTIceCandidateType type, u32 priority, LTNetIpv4Endpoint endpoint, const char *foundation);
        /**< initializes the candidate.
         *
         *   @param candidate the ICE candidate instance
         *   @param agent the ICE agent that this candidate is associated with
         *   @param transport the transport protocol used to communicate with this candidate.
         *   @param type the type of this ICE candidate.
         *   @param priority the priority of this ICE candidate.
         *   @param endpoint the network endpoint at which this candidate can be reached.
         *   @param foundation the foundation of this candidate.
         */

    LTIceCandidateType      (*GetType)(LTIceCandidate *candidate);
        /**< gets the candidate type: host, server reflexive, peer reflexive or relay
         *
         *   @param candidate the ICE candidate instance
         */

    LTIceCandidateTransport (*GetTransport)(LTIceCandidate *candidate);
        /**< gets the candidate transport type: TCP or UDP
         *
         *   @param candidate the ICE candidate instance
         */

    u32                     (*GetPriority)(LTIceCandidate *candidate);
        /**< gets the candidate priority
         *
         *   @param candidate the ICE candidate instance
         */

    LTNetIpv4Endpoint       (*GetEndpoint)(LTIceCandidate *candidate);
        /**< gets the candidate endpoint
         *
         *   @param candidate the ICE candidate instance
         */

    const char *            (*GetFoundation)(LTIceCandidate *candidate);
        /**< gets the candidate foundation
         *
         *   @param candidate the ICE candidate instance
         */

    LTIceCandidateStatus (*GetCandidateStatus)(LTIceCandidate *candidate);
        /**< gets the candidate status whether it is sent or received.
         *
         *   @param candidate the ICE candidate instance
         *   @return ICE candidate status
         */

    void (*SetCandidateStatus)(LTIceCandidate *candidate, LTIceCandidateStatus candidateStatus);
        /**< sets the candidate status
         *
         *   @param candidate the ICE candidate instance
         *   @param candidateStatus value to set ICE Candidate's status
         */
} LTOBJECT_API;

/*_________________________
 / LTIceCandidate Object */
typedef_LTObject(LTIceCandidatePair, 1) {

    LTIceCandidate         *(*GetLocalCandidate)(LTIceCandidatePair *candidatePair);
        /**< gets the local candidate associated with the pair
         *
         *   @param candidatePair the ICE candidate pair instance
         */

    LTIceCandidate         *(*GetRemoteCandidate)(LTIceCandidatePair *candidatePair);
        /**< gets the remote candidate associated with the pair
         *
         *   @param candidatePair the ICE candidate pair instance
         */

    LTSocket                (*GetDtlsSocket)(LTIceCandidatePair *candidatePair);
        /**< gets a socket handle used to send and receive DTLS data
         *
         *   @param candidatePair the ICE candidate pair instance
         */

    LTSocket                (*GetSrtpSocket)(LTIceCandidatePair *candidatePair);
        /**< gets a socket handle used to send and receive SRTP data
         *
         *   @param candidatePair the ICE candidate pair instance
         */
} LTOBJECT_API;

typedef void (LTIceAgent_IceEventProc)(LTIceAgent *agent, LTIceAgentEvent event, void *pClientData);
    /**< callback for indicating the occurrence of ICE candidate gathering events.
     *
     *   This callback is called to indicate the occurrence of peer connection events.
     *
     *   @param hAgent the client instance
     *   @param event the current connection event which has occurred.
     *   @param pClientData the client data that was specified when the client was created.
     */

typedef void (LTIceAgent_DataArrivedEventProc)(LTIceAgent *agent, u8 *pData, u32 nDataLen, void *pClientData);
    /**< callback for data receipt from the selected candidate.
     *
     *   @param hAgent the client instance
     *   @param pData the received data.
     *   @param nDataLen the received data length.
     *   @param pClientData the client data that was specified when the client was created.
     */

/*_____________________
 / LTIceAgent Object */
typedef_LTObject(LTIceAgent, 1) {
/** ___________________________________
 *   @section ILTIceAgent Object
 *   * @brief an object for P2P connection establishment using ICE.
 *   *
 *   * ILTIceAgent provides an interface for establishing P2P connections using
 *   * the ICE protocol.
 \______________________________________________________________________________________*/

    void (*OnIceEvent)(LTIceAgent *agent,
                       LTIceAgent_IceEventProc *pCallback,
                       LTThread_ClientDataReleaseProc *pClientDataReleaseProc,
                       void *pClientData);
        /**< Registers a callback for ICE events
         *
         *   @param hAgent the handle of the ICE agent.
         *   @param pCallback the callback.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData the client data to be delivered in the callback.
         */

    void (*NoIceEvent)(LTIceAgent *agent,
                       LTIceAgent_IceEventProc *pCallback);
        /**< Unregisters a callback for ICE events
         *
         *   @param hAgent the handle of the ICE agent.
         *   @param pCallback the callback.
         */

    void (*SetRemoteCredentials)(LTIceAgent *agent,
                                 const char *pUserFragment,
                                 const char *pPassword);
        /**< Sets the remote username fragment and password used to authenticate STUN requests sent from this agent to a remote agent.
         *
         *   @param hAgent the handle of the agent.
         *   @param pUserFragment the remote agent username fragment.
         *   @param pPassword the remote agent password.
         */

    void (*GetLocalCredentials)(LTIceAgent *agent,
                                char *pUserFragment,
                                u32 nUserFragmentSize,
                                char *pPassword,
                                u32 nPasswordSize);
        /**< Gets the local user fragment and password to be used by remote peer to authenticate STUN requests sent to this agent.
         *
         *   @param hAgent the handle of the agent.
         *   @param pUserFragment Pointer to a buffer into which the local username fragment will be copied. Must be at least kLTStunUsernameAttrLenMax in length.
         *   @param nUserFragmentSize The size of the pUserFragment buffer. Must be at least kLTStunUsernameAttrLenMax.
         *   @param pPassword Pointer to a buffer into which the local password will be copied. Must be at least kLTStunPasswordAttrLenMax in length.
         *   @param nPasswordSize The size of the pPassword buffer. Must be at least kLTStunPasswordAttrLenMax.
         */

    void (*AddRemoteCandidate)(LTIceAgent *agent,
                               LTIceCandidate *pCandidate);
        /**< Adds a remote candidate to be checked for connectivity
         *
         *   @param hAgent the handle of the ICE agent.
         *   @param pCandidate the candidate to be added.
         */

    LTArray *(*GetLocalCandidates)(LTIceAgent *agent);
        /**< Gets the current list of gather candidates
         *
         *   @param hAgent the handle of the ICE agent.
         *   @returns an LTArray containing the list of gathered `LTIceCandidate`s
         */

    void (*AddIceServer)(LTIceAgent *agent,
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

    void (*StartCandidateGathering)(LTIceAgent *agent);
        /**< Starts the process of gathering local ICE candidates
         *
         *   @param hAgent the handle of the ICE agent.
         */

    void (*StartIceConnectionCheck)(LTIceAgent *agent);
        /**< Starts the process of connection checking of remote Trickle ICE candidates
         *
         *   @param hAgent the handle of the ICE agent.
         */

    LTIceCandidatePair *(*GetSelectedCandidatePair)(LTIceAgent *agent);
        /**< Gets the selected ICE candidate.
         *
         *   @param hAgent the handle of the ICE agent.
         *   @return the selected candidate if one has been selected, else NULL
         */

    void (*GetCandidateType)(LTIceAgent *agent, char *candidateType, u32 candidateTypeLen);
        /**< Gets the selected ICE candidate type.
         *
         *   @param hAgent the handle of the ICE agent.
         *   @param candidateType the candidate string to be filled with type configuration.
         *   @param candidateTypeLen the candidate string length.
         */

    bool (*Init)(LTIceAgent *agent, const char *pSessionId);
        /**< Initialize and sets the webrtc unique session id.
         *
         *   @param hAgent the handle of the ICE agent.
         *   @param pSessionId the webrtc session id
         *   @return true on success, otherwise false.
         */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_ICE_LTNETICE_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jul-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
