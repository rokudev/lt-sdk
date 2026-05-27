/*******************************************************************************
 * <lt/net/stun/LTNetStun.h> - Session Traversal Utilities for NAT (STUN) Agent
 *
 * The LTNetStun library implements a subset of the STUN protocol as defined by RFC-8489.
 * The STUN protocol can be used by other network protocols as a tool to detect the
 * presence of NAT devices, discover the NAT address/port mappings that they create,
 * check connectivity between endpoints, and as a keepalive to maintain NAT bindings.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_STUN_LTNETSTUN_H
#define ROKU_LT_INCLUDE_LT_NET_STUN_LTNETSTUN_H

#include <lt/LTTypes.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

enum {
    kLTStunTransactionIdLen      = 12,
    kLTStunUsernameAttrLenMax    = 256 + 1,
    kLTStunPasswordAttrLenMax    = 256 + 1,
    kLTStunRealmAttrLenMax       = 128 + 1,
};

/*__________________________
_/      enum LTStunMethod */
typedef enum {
    kLTStunMethod_Binding = 0x0001,
} LTStunMethod;

/*_________________________
_/      enum LTStunClass */
typedef enum {
    kLTStunClass_Request    = 0x0000,
    kLTStunClass_Response   = 0x0100,
    kLTStunClass_Indication = 0x0010,
    kLTStunClass_Error      = 0x0110,
} LTStunClass;

/*_____________________________
_/      enum LTStunErrorCode */
typedef enum {
    kLTStunErrorCode_TryAlternate     = 300,
    kLTStunErrorCode_BadRequest       = 400,
    kLTStunErrorCode_Unauthenticated  = 401,
    kLTStunErrorCode_UnknownAttribute = 420,
    kLTStunErrorCode_StaleNonce       = 438,
    kLTStunErrorCode_ServerError      = 500,
} LTStunErrorCode;

/*____________________________
_/      enum LTStunAttrType */
typedef enum {
    kLTStunAttrType_MappedAddress          = 0x0001,
    kLTStunAttrType_Username               = 0x0006,
    kLTStunAttrType_MessageIntegrity       = 0x0008,
    kLTStunAttrType_ErrorCode              = 0x0009,
    kLTStunAttrType_UnknownAttributes      = 0x000A,
    kLTStunAttrType_Realm                  = 0x0014,
    kLTStunAttrType_Nonce                  = 0x0015,
    kLTStunAttrType_MessageIntegritySHA256 = 0x001C,
    kLTStunAttrType_PasswordAlgorithm      = 0x001D,
    kLTStunAttrType_Userhash               = 0x001E,
    kLTStunAttrType_XorMappedAddress       = 0x0020,
    kLTStunAttrType_PasswordAlgorithms     = 0x8002,
    kLTStunAttrType_AlternateDomain        = 0x8003,
    kLTStunAttrType_Software               = 0x8022,
    kLTStunAttrType_AlternateServer        = 0x8023,
    kLTStunAttrType_Fingerprint            = 0x8028,
    kLTStunAttrType_NetworkInfo            = 0xC057,
} LTStunAttrType;

/*______________________________
_/      enum LTStunAttrFormat */
typedef enum {
                                   // Required data type:
    kStunAttrFormat_void,          // (none)
    kStunAttrFormat_u32,           // u32
    kStunAttrFormat_u64,           // u64
    kStunAttrFormat_bytes,         // u8*, u32
    kStunAttrFormat_XorEndpointV4, // LTNetIpv4Endpoint
} LTStunAttrFormat;

/*________________________________
_/      enum LTStunMessageFlags */
typedef enum {
    kLTStunMessageFlags_None              = 0,
    kLTStunMessageFlags_MacSHA1           = (1 << 0),
    kLTStunMessageFlags_MacSHA256         = (1 << 1),
    kLTStunMessageFlags_MacValid          = (1 << 2),
    kLTStunMessageFlags_Fingerprint       = (1 << 3),
    kLTStunMessageFlags_FingerprintValid  = (1 << 4),
} LTStunMessageFlags;

/*_________________________________
_/      enum LTStunAddressFamily */
typedef enum {
    kLTStunAddressFamily_IPv4 = 0x01,
    kLTStunAddressFamily_IPv6 = 0x02
} LTStunAddressFamily;

/*_____________________________________
_/      enum LTStunPasswordAlgorithm */
typedef enum {
    kLTStunPasswordAlgorithm_MD5    = 0x0001,
    kLTStunPasswordAlgorithm_SHA256 = 0x0002,
} LTStunPasswordAlgorithm;

typedef enum {
    kLTStunCredentialsUsageContext_Local  = (1 << 0), // Credentials may be used in locally-generated requests or responses thereof
    kLTStunCredentialsUsageContext_Remote = (1 << 1), // Credentials may be used in remotely-generated requests or responses thereof
    kLTStunCredentialsUsageContext_Any    = kLTStunCredentialsUsageContext_Local | kLTStunCredentialsUsageContext_Remote,
} LTStunCredentialsUsageContext;

typedef struct {
    u8 value[kLTStunTransactionIdLen];
} LTStunTransactionId;

/*________________________
 / LTStunMessage Object */
typedef_LTObject(LTStunMessage, 1) {
    void (*InitRequest)(LTStunMessage *message, LTNetIpv4Endpoint endpoint, u16 method, LTStunMessageFlags flags, void *clientData);
        /**< Initializes the STUN message instance as a request message.
         *
         *   @param message the message instance.
         *   @param endpoint the network endpoint to which this request will be sent.
         *   @param method the STUN method for this request.
         *   @param flags flags for this request.
         *   @param clientData the client data associated with the request. Responses to this request will contain this client data.
         */

    void (*InitResponse)(LTStunMessage *response, LTStunMessage *request);
        /**< Initializes the STUN message instance as a response message.
         *
         *   @param response the response message instance.
         *   @param request the request message which is being responded to.
         */

    void (*SetRetransmissionTimeout)(LTStunMessage *message, u32 retransmissionTimeoutMs);
        /**< Sets the retransmission timeout for this message.
         *
         *   The retransmission timeout is the amount of time that must pass without
         *   a response being received before the message is retransmitted. Each
         *   subsequent retransmission timeout will be twice as long as the last.
         *
         *   @param message the message instance.
         *   @param retransmissionTimeoutMs the retransmission timeout in milliseconds.
         */

    u32 (*GetClass)(LTStunMessage *message);
        /**< Gets the STUN message class for this message.
         *
         *   @param message the message instance.
         *   @return the message class.
         */

    u32 (*GetMethod)(LTStunMessage *message);
        /**< Gets the STUN message method for this message.
         *
         *   @param message the message instance.
         *   @return the message method.
         */

    u32 (*GetFlags)(LTStunMessage *message);
        /**< Gets the STUN message flags for this message.
         *
         *   @param message the message instance.
         *   @return the message flags.
         */

    u32 (*GetErrorCode)(LTStunMessage *message);
        /**< Gets the STUN message error code for this message.
         *
         *   @param message the message instance.
         *   @return the message error code if present, else 0.
         */

    const char *(*GetErrorReason)(LTStunMessage *message);
        /**< Gets the STUN message error reason string for this message.
         *
         *   @param message the message instance.
         *   @return the message error reason if present, else NULL.
         */

    LTNetIpv4Endpoint (*GetEndpoint)(LTStunMessage *message);
        /**< Gets the network endpoint to/from which the message was received/will be sent.
         *
         *   @param message the message instance.
         *   @return the message endpoint.
         */

    void (*SetAttribute)(LTStunMessage *message, u32 attrType, LTStunAttrFormat format, ...);
        /**< Sets a STUN message attribute to be included in this message.
         *
         *   @param message the message instance.
         *   @param attrType the attribute type identifier.
         *   @param format the format of the data for this attribute.
         *   @param ... the attribute value, passed in the format required by the "format" parameter.
         */

    bool (*GetAttribute)(LTStunMessage *message, u32 attrType, LTStunAttrFormat format, ...);
        /**< Gets a message attribute value.
         *
         *   @param message the message instance.
         *   @param attrType the attribute type identifier.
         *   @param format the format of the data for this attribute.
         *   @param ... user buffer into which the attribute value will be copied.
         *   @return true if the requested attribute was found and copied into the user's buffer, else false.
         */

    void *(*GetClientData)(LTStunMessage *message);
        /**< Gets client data associated with this message.
         *
         *   @param message the message instance.
         *   @return the client data.
         */
} LTOBJECT_API;

typedef struct LTStunTransport LTStunTransport;
typedef void (LTStunTransport_MessageReceivedProc)(LTStunTransport *transport, LTStunMessage *pMessage, void *pClientData);
typedef void (LTStunTransport_UnhandledDatagramProc)(const u8 *pDatagram, u32 nDatagramSize, void *pClientData);

/*__________________________
 / LTStunTransport Object */
typedef_LTObject(LTStunTransport, 1) {
    void (*OnMessageReceived)(LTStunTransport *transport, LTStunTransport_MessageReceivedProc *pCallback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< Registers a callback used to indicate receipt of a valid STUN message.
         *
         *   @param transport the transport instance.
         *   @param pCallback the callback.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData client data which will be passed to the callback.
         */

    void (*NoMessageReceived)(LTStunTransport *transport, LTStunTransport_MessageReceivedProc *pCallback);
        /**< Unregisters a message receive callback.
         *
         *   @param transport the transport instance.
         *   @param pCallback the message receive callback.
         */

    LTStunTransactionId (*Send)(LTStunTransport *transport, LTStunMessage *pMessage);
        /**< Sends a STUN message
         *
         *   @param transport the transport instance.
         *   @param pMessage the STUN message to be sent.
         */

    void (*Cancel)(LTStunTransport *transport, LTStunTransactionId transactionId);
        /**< Cancels a STUN transaction initiated with Send()
         *
         *   @param transport the transport instance.
         *   @param transactionId the STUN transaction to be canceled.
         */

    s32 (*SendDatagram)(LTStunTransport *transport, LTNetIpv4Endpoint endpoint, const u8 *pDatagram, u32 nDatagramSize, u8 dscp);
        /**< Sends a raw datagram over the STUN transport's transport socket.
         *
         *   @param transport the transport instance.
         *   @param endpoint the endpoint to which the datagram is to be sent.
         *   @param pDatagram the datagram buffer to be sent.
         *   @param nDatagramSize the size of the datagram buffer.
         *   @param dscp the dscp value with which to send the datagram.
         */
} LTOBJECT_API;

/*______________________
 / LTStunAgent Object */
typedef_LTObject(LTStunAgent, 1) {
    void (*Init)(LTStunAgent *agent, const char *sessionId, LTSocket hSocket);
        /**< Initializes a STUN agent instance.
         *
         *   @param agent the agent instance.
         *   @param hSocket the default socket to be used to send and receive STUN messages if not provided to a transport instance.
         *   @param pSessionId the unique session id
         */

    void (*AddCredentials)(LTStunAgent *agent, LTNetIpv4Endpoint endpoint, const char *username, const char *password, LTStunCredentialsUsageContext usageContext);
        /**< Adds credentials used to authenticate messages sent or received by the STUN agent.
         *
         *   @param agent the agent instance.
         *   @param endpoint the network endpoint to which these credentials apply. LTNetIpv4Endpoint_Any() can be used as a wildcard to match any endpoint.
         *   @param username the username sent or received in messages.
         *   @param password the password used to generate or verify message integrity attributes.
         *   @param usageContext the context under which these credentials may be used.
         */

    LTStunTransport *(*OpenTransport)(LTStunAgent *agent, LTSocket hSocket, LTNetIpv4Endpoint endpoint, LTStunTransport_UnhandledDatagramProc *pCallback, void *pClientData);
        /**< Creates a STUN transport used to send and receive messages.
         *
         *   @param agent the agent instance.
         *   @param hSocket the socket to be used to send and receive STUN messages or 0 to use the agent's default socket.
         *   @param endpoint the endpoint to which messages will be sent/received. LTNetIpv4Endpoint_Any() may be passed if multiple addresses will be used.
         *   @param pCallback the callback for TURN messages.
         *   @param pClientData the client data which will be passed as argument to the TURN message pCallback.
         */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_STUN_LTNETSTUN_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
