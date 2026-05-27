/*******************************************************************************
 * source/lt/net/turn/LTNetTurn.c - Implementation of TURN client interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTArray.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/net/stun/LTNetStun.h>
#include <lt/net/turn/LTNetTurn.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

DEFINE_LTLOG_SECTION("turn");

#define IS_CHANNEL(ptr) (((TurnChannel *)(ptr))->magic == kTurnChannelMagic)

enum {
    kMaxNumChannels                    = 0x1000,
    kFirstChannelNum                   = 0x4000,
    kLastChannelNum                    = kFirstChannelNum + kMaxNumChannels - 1,

    kTurnChannelMagic                  = 0x8656C0DE,

    kRefreshBufferSeconds              = 60,     // send refreshes 1 minute before expiration
    kChannelInactivityThresholdSeconds = 2 * 60,
    kPermissionLifetimeSeconds         = 5 * 60, // permission lifetime per RFC 8656
    kPermissionRefreshIntervalSec      = kPermissionLifetimeSeconds - kRefreshBufferSeconds,
};

typedef enum {
    kLTTurnStunMethod_Allocate         = 0x0003,
    kLTTurnStunMethod_Refresh          = 0x0004,
    kLTTurnStunMethod_Send             = 0x0006,
    kLTTurnStunMethod_Data             = 0x0007,
    kLTTurnStunMethod_CreatePermission = 0x0008,
    kLTTurnStunMethod_ChannelBind      = 0x0009,
} LTTurnStunMethod;

typedef enum {
    kTurnStunAttrType_ChannelNumber           = 0x000C,
    kTurnStunAttrType_Lifetime                = 0x000D,
    kTurnStunAttrType_XorPeerAddress          = 0x0012,
    kTurnStunAttrType_Data                    = 0x0013,
    kTurnStunAttrType_XorRelayedAddress       = 0x0016,
    kTurnStunAttrType_RequestedAddressFamily  = 0x0017,
    kTurnStunAttrType_EvenPort                = 0x0018,
    kTurnStunAttrType_RequestedTransport      = 0x0019,
    kTurnStunAttrType_DontFragment            = 0x001A,
    kTurnStunAttrType_ReservationToken        = 0x0022,
    kTurnStunAttrType_AdditionalAddressFamily = 0x8000,
    kTurnStunAttrType_AddressErrorCode        = 0x8001,
    kTurnStunAttrType_ICMP                    = 0x8004,
} TurnStunAttrType;

/*___________________________________________
  LTTurnClient object private data members */

typedef enum {
    kTurnClientState_Allocate,
    kTurnClientState_RefreshAllocation,
} TurnClientState;

typedef_LTObjectImpl(LTTurnClient, LTTurnClientImpl) {
    LTThread            hThread;
    LTEvent             hEvent;
    TurnClientState     state;
    LTNetIpv4Endpoint   serverEndpoint;
    LTStunAgent        *stunAgent;
    LTStunTransport    *stunTransport;
    LTNetIpv4Endpoint   relayedAddress;
    u32                 lifetime;
    LTNetIpv4Endpoint   mappedAddress;
    char               *username;
    char               *password;
    char               *nonce;
    u32                 nonceLen;
    char               *realm;
    u32                 realmLen;
    LTArray            *channels;
} LTOBJECT_API;

typedef enum {
    kTurnChannelState_CreatePermission,
    kTurnChannelState_ChannelBind,
    kTurnChannelState_Ready,
} TurnChannelState;

typedef struct {
    u32               magic;
    LTSocket          hHandle;
    LTEvent           hEvent;
    LTTurnClientImpl *client;
    TurnChannelState  state;
    u16               channelNum;
    LTNetIpv4Endpoint peerEndpoint;
    LTList            rxQueue;
    LTTime            lastPacketTime;
    LTAtomic          readPending;
    u8                dscp;
} TurnChannel;

typedef struct {
    LTList_Node node;
    u16 size;
    u8  data[];
} ChannelPacket;

static ILTThread       *s_pThread = NULL;
static ILTEvent        *s_pEvent  = NULL;

static const ILTSocket s_ILTSocket;

static const LTArgsDescriptor s_TurnEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_u32}}; // client, event

static const LTArgsDescriptor s_SocketEventArgs =
    {2, { kLTArgType_lthandle, kLTArgType_u32 }}; // socket, event

static void DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *data);

static void
DispatchTurnEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTTurnClient_TurnEventProc *pCallback = (LTTurnClient_TurnEventProc *)pEventProc;
    LTTurnClient *client = LTArgs_pointerAt(0, pEventArgs);
    LTNetTurnEvent event = LTArgs_u32At(1, pEventArgs);
    pCallback(client, event, pEventProcClientData);
}

static void
SendAllocateMessage(LTTurnClientImpl *client, bool authenticated) {
    LTStunMessage *message = lt_createobject(LTStunMessage);
    LTStunMessageFlags flags = authenticated ? kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_Fingerprint : 0;
    message->API->InitRequest(message, client->serverEndpoint, kLTTurnStunMethod_Allocate, flags, client);
    message->API->SetAttribute(message, kTurnStunAttrType_RequestedTransport,     kStunAttrFormat_u32, 17);   // UDP
    message->API->SetAttribute(message, kTurnStunAttrType_RequestedAddressFamily, kStunAttrFormat_u32, 0x01); // IPv4
    if (authenticated) {
        message->API->SetAttribute(message, kLTStunAttrType_Realm,                    kStunAttrFormat_bytes, client->realm, client->realmLen);
        message->API->SetAttribute(message, kLTStunAttrType_Nonce,                    kStunAttrFormat_bytes, client->nonce, client->nonceLen);
    }
    client->state = kTurnClientState_Allocate;
    client->stunTransport->API->Send(client->stunTransport, message);
}

static void
SendRefreshAllocationMessage(LTTurnClientImpl *client) {
    LTStunMessage *message = lt_createobject(LTStunMessage);
    message->API->InitRequest(message, client->serverEndpoint, kLTTurnStunMethod_Refresh, kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_Fingerprint, client);
    message->API->SetAttribute(message, kLTStunAttrType_Realm, kStunAttrFormat_bytes, client->realm, client->realmLen);
    message->API->SetAttribute(message, kLTStunAttrType_Nonce, kStunAttrFormat_bytes, client->nonce, client->nonceLen);
    client->state = kTurnClientState_RefreshAllocation;
    client->stunTransport->API->Send(client->stunTransport, message);
}

static void
SendCreatePermissionMessage(LTTurnClientImpl *client, TurnChannel *channel) {
    LTStunMessage *message = lt_createobject(LTStunMessage);
    message->API->InitRequest(message, client->serverEndpoint, kLTTurnStunMethod_CreatePermission, kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_Fingerprint, channel);
    message->API->SetAttribute(message, kTurnStunAttrType_XorPeerAddress, kStunAttrFormat_XorEndpointV4, channel->peerEndpoint);
    message->API->SetAttribute(message, kLTStunAttrType_Realm,            kStunAttrFormat_bytes,         client->realm, client->realmLen);
    message->API->SetAttribute(message, kLTStunAttrType_Nonce,            kStunAttrFormat_bytes,         client->nonce, client->nonceLen);
    channel->state = kTurnChannelState_CreatePermission;
    client->stunTransport->API->Send(client->stunTransport, message);
}

static void
SendChannelBindMessage(LTTurnClientImpl *client, TurnChannel *channel) {
    LTStunMessage *message = lt_createobject(LTStunMessage);
    u16 channelNumAttrData[2] = { LT_HTONS(channel->channelNum), 0 };
    message->API->InitRequest(message, client->serverEndpoint, kLTTurnStunMethod_ChannelBind, kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_Fingerprint, channel);
    message->API->SetAttribute(message, kTurnStunAttrType_ChannelNumber,  kStunAttrFormat_bytes,         &channelNumAttrData, sizeof(channelNumAttrData));
    message->API->SetAttribute(message, kTurnStunAttrType_XorPeerAddress, kStunAttrFormat_XorEndpointV4, channel->peerEndpoint);
    message->API->SetAttribute(message, kLTStunAttrType_Realm,            kStunAttrFormat_bytes,         client->realm, client->realmLen);
    message->API->SetAttribute(message, kLTStunAttrType_Nonce,            kStunAttrFormat_bytes,         client->nonce, client->nonceLen);
    channel->state = kTurnChannelState_ChannelBind;
    client->stunTransport->API->Send(client->stunTransport, message);
}

static void
OnRefreshAllocationTimerExpired(void *clientData) {
    LTTurnClientImpl *client = clientData;
    LTLOG_DEBUG("refresh.alloc.send", "Refreshing allocation");
    s_pThread->KillTimer(client->hThread, OnRefreshAllocationTimerExpired, client);
    SendRefreshAllocationMessage(client);
}

static void
OnRefreshChannel(void *clientData) {
    TurnChannel *channel = clientData;
    s_pThread->KillTimer(channel->client->hThread, OnRefreshChannel, channel);

    LTTime timeSinceLastPacket = LTTime_Subtract(LT_GetCore()->GetKernelTime(), channel->lastPacketTime);
    if (LTTime_IsLessThan(timeSinceLastPacket, LTTime_Seconds(kChannelInactivityThresholdSeconds))) {
        LTLOG_DEBUG("refresh.chnl.send", "Refreshing channel 0x%04lx", LT_Pu32(channel->channelNum));
        SendCreatePermissionMessage(channel->client, channel);
    } else {
        LTLOG_DEBUG("refresh.chnl.skip", "No activity on channel 0x%04lx for >%lu seconds. Skipping refresh.",
            LT_Pu32(channel->channelNum), LT_Pu32(kChannelInactivityThresholdSeconds));
    }
}

static bool
ExtractAuthAttrs(LTTurnClientImpl *client, LTStunMessage *challengeMessage) {
    u8 *nonce;
    u8 *realm;
    if (!challengeMessage->API->GetAttribute(challengeMessage, kLTStunAttrType_Nonce, kStunAttrFormat_bytes, &nonce, &client->nonceLen)) {
        LTLOG_YELLOWALERT("xauth.no.nonce", "Auth challenge is missing nonce");
        return false;
    }
    if (!challengeMessage->API->GetAttribute(challengeMessage, kLTStunAttrType_Realm, kStunAttrFormat_bytes, &realm, &client->realmLen)) {
        LTLOG_YELLOWALERT("xauth.no.realm", "Auth challenge is missing realm");
        return false;
    }
    if (client->nonce) lt_free(client->nonce);
    client->nonce = lt_memdup(nonce, client->nonceLen);
    if (!client->nonce) {
        LTLOG_YELLOWALERT("xauth.nonc.oom", "Failed to allocate buffer for nonce");
        return false;
    }

    if (client->realm) lt_free(client->realm);
    client->realm = lt_memdup(realm, client->realmLen);
    if (!client->realm) {
        LTLOG_YELLOWALERT("xauth.rlm.oom", "Failed to allocate buffer for realm");
        return false;
    }
    return true;
}

static void
RetryClientMessage(LTTurnClientImpl *client) {
    switch (client->state) {
        case kTurnClientState_Allocate:
            SendAllocateMessage(client, true);
            break;

        case kTurnClientState_RefreshAllocation:
            SendRefreshAllocationMessage(client);
            break;

        default:
            LTLOG_YELLOWALERT("cl.rtry.badstate", "Invalid state for client message retry: %lu", LT_Pu32(client->state));
            break;
    }
}

static void
RetryChannelMessage(TurnChannel *channel) {
    switch (channel->state) {
        case kTurnChannelState_CreatePermission:
            SendCreatePermissionMessage(channel->client, channel);
            break;

        case kTurnChannelState_ChannelBind:
            SendChannelBindMessage(channel->client, channel);
            break;

        default:
            LTLOG_YELLOWALERT("ch.rtry.badstate", "Invalid state for channel message retry: %lu", LT_Pu32(channel->state));
            break;
    }
}

static void
HandleRetryableError(LTTurnClientImpl *client, LTStunMessage *message) {
    if (!ExtractAuthAttrs(client, message)) {
        LTLOG_YELLOWALERT("chg.xauth.fail", "Failed to extract auth attributes from error response");
        return;
    }

    void *clientData = message->API->GetClientData(message);
    if (IS_CHANNEL(clientData)) {
        RetryChannelMessage(clientData);
    } else {
        RetryClientMessage(clientData);
    }
}

static void
HandleStunError(LTTurnClientImpl *client, LTStunMessage *errorMessage) {
    u32 errorCode = errorMessage->API->GetErrorCode(errorMessage);
    switch (errorCode) {
        case kLTStunErrorCode_Unauthenticated:
        case kLTStunErrorCode_StaleNonce:
            HandleRetryableError(client, errorMessage);
            break;

        default: {
            const char *errorReason = errorMessage->API->GetErrorReason(errorMessage);
            LTLOG_YELLOWALERT("err.msg", "Server returned error: %lu - %s", LT_Pu32(errorCode), errorReason);
            break;
        }
    }
}

static void
HandleStunResponse(LTTurnClientImpl *client, LTStunMessage *pMessage) {
    u32 method = pMessage->API->GetMethod(pMessage);
    switch (method) {
        case kLTTurnStunMethod_Allocate:
            if (!pMessage->API->GetAttribute(pMessage, kTurnStunAttrType_XorRelayedAddress, kStunAttrFormat_XorEndpointV4, &client->relayedAddress)) {
                LTLOG_YELLOWALERT("alloc.rsp.norlyaddr", "Allocate response is missing relayed address attribute");
                return;
            }
            if (!pMessage->API->GetAttribute(pMessage, kLTStunAttrType_XorMappedAddress, kStunAttrFormat_XorEndpointV4, &client->mappedAddress)) {
                LTLOG_YELLOWALERT("alloc.rsp.nomapaddr", "Allocate response is missing mapped address attribute");
                return;
            }
            if (!pMessage->API->GetAttribute(pMessage, kTurnStunAttrType_Lifetime, kStunAttrFormat_u32, &client->lifetime)) {
                LTLOG_YELLOWALERT("alloc.rsp.nolif", "Allocate response is missing lifetime attribute");
                return;
            }
            LTLOG_DEBUG("alloc.ok", "Allocate succeeded - relayed address: %lu.%lu.%lu.%lu:%lu, mapped address: %lu.%lu.%lu.%lu:%lu, lifetime: %lu",
                LT_Pu32((client->relayedAddress.address >> 0) & 0xFF),
                LT_Pu32((client->relayedAddress.address >> 8) & 0xFF),
                LT_Pu32((client->relayedAddress.address >> 16) & 0xFF),
                LT_Pu32((client->relayedAddress.address >> 24) & 0xFF),
                LT_Pu32(client->relayedAddress.port),
                LT_Pu32((client->mappedAddress.address >> 0) & 0xFF),
                LT_Pu32((client->mappedAddress.address >> 8) & 0xFF),
                LT_Pu32((client->mappedAddress.address >> 16) & 0xFF),
                LT_Pu32((client->mappedAddress.address >> 24) & 0xFF),
                LT_Pu32(client->mappedAddress.port),
                LT_Pu32(client->lifetime));
            s_pThread->SetTimer(client->hThread, LTTime_Seconds(client->lifetime - kRefreshBufferSeconds), OnRefreshAllocationTimerExpired, NULL, client);
            s_pEvent->NotifyEvent(client->hEvent, client, kLTTurnEvent_CreateAllocationSuccess);
            break;

        case kLTTurnStunMethod_Refresh:
            LTLOG_DEBUG("refresh.ok", "Allocation refresh succeeded");
            s_pThread->SetTimer(client->hThread, LTTime_Seconds(client->lifetime - kRefreshBufferSeconds), OnRefreshAllocationTimerExpired, NULL, client);
            break;

        case kLTTurnStunMethod_CreatePermission: {
            TurnChannel *channel = pMessage->API->GetClientData(pMessage);
            LTLOG_DEBUG("perm.ok", "Permission successfully created for %lu.%lu.%lu.%lu:%lu",
                LT_Pu32((channel->peerEndpoint.address >> 0) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 8) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 16) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 24) & 0xFF),
                LT_Pu32(channel->peerEndpoint.port));
            SendChannelBindMessage(client, channel);
            break;
        }

        case kLTTurnStunMethod_ChannelBind: {
            TurnChannel *channel = pMessage->API->GetClientData(pMessage);
            LTLOG_DEBUG("bind.ok", "Channel bind succeeded: channel 0x%04lx -> %lu.%lu.%lu.%lu:%lu",
                LT_Pu32(channel->channelNum),
                LT_Pu32((channel->peerEndpoint.address >> 0) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 8) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 16) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 24) & 0xFF),
                LT_Pu32(channel->peerEndpoint.port));
            s_pThread->SetTimer(client->hThread, LTTime_Seconds(kPermissionRefreshIntervalSec), OnRefreshChannel, NULL, channel);
            s_pEvent->NotifyEvent(channel->client->hEvent, client, kLTTurnEvent_ChannelBindingSuccess);
            s_pEvent->NotifyEvent(channel->hEvent, channel->hHandle, kLTSocket_Event_SocketReady);
            break;
        }

        default:
            LTLOG_YELLOWALERT("bad.rsp.method", "Unsupported response method: %04lx", LT_Pu32(method));
            break;
    }
}

static void
OnStunMessageReceived(LTStunTransport *stunTransport, LTStunMessage *pMessage, void *clientData) {
    LT_UNUSED(stunTransport);
    LTTurnClientImpl *client = clientData;

    u32 class = pMessage->API->GetClass(pMessage);
    switch (class) {
        case kLTStunClass_Error:
            HandleStunError(client, pMessage);
            break;

        case kLTStunClass_Response:
            HandleStunResponse(client, pMessage);
            break;

        default:
            LTLOG_DEBUG("bad.class", "Unsupported message class: %04lx", LT_Pu32(class));
            break;
    }
}

static TurnChannel *
FindChannel(LTTurnClientImpl *client, u16 channelNumber) {
    LTArray *channels = client->channels;
    for (u32 i = 0; i < channels->API->GetCount(channels); ++i) {
        TurnChannel *channel = channels->API->Get(channels, i, NULL);
        if (channel->channelNum == channelNumber) {
            return channel;
        }
    }
    return NULL;
}

static void
OnStunUnhandledDatagram(const u8 *datagram, u32 datagramSize, void *clientData) {
    LTTurnClientImpl *client = clientData;
    if (datagramSize < 4) {
        LTLOG_YELLOWALERT("bad.dlen", "Datagram too small: %lu bytes - Ignoring datagram.", LT_Pu32(datagramSize));
        return;
    }
    u16 channelNumber    = LT_NTOHS(((u16*)datagram)[0]);
    u16 channelMsgLength = LT_NTOHS(((u16*)datagram)[1]);
    if ((channelNumber < kFirstChannelNum) || (channelNumber > kLastChannelNum)) {
        LTLOG_YELLOWALERT("bad.chnum", "Illegal channel number: %lu - Ignoring datagram.", LT_Pu32(channelNumber));
        return;
    }
    if (channelMsgLength != (datagramSize - 4)) {
        LTLOG_YELLOWALERT("bad.chlen", "Invalid channel message length: %lu - Ignoring datagram.", LT_Pu32(channelMsgLength));
        return;
    }
    TurnChannel *channel = FindChannel(client, channelNumber);
    if (!channel) {
        LTLOG_YELLOWALERT("no.chnum", "Channel not found with channel number: %lu - Ignoring datagram.", LT_Pu32(channelNumber));
        return;
    }

    ChannelPacket *channelPacket = lt_malloc(sizeof(ChannelPacket) + channelMsgLength);
    if (!channelPacket) {
        LTLOG_YELLOWALERT("chpkt.alloc.fail", "Failed to allocate buffer for channel packet");
        return;
    }
    channelPacket->size = channelMsgLength;
    lt_memcpy(channelPacket->data, datagram + 4, channelMsgLength);
    LTList_AddTail(&channel->rxQueue, &channelPacket->node);

    if (LTAtomic_CompareAndExchange(&channel->readPending, 0, 1)) {
        s_pEvent->NotifyEvent(channel->hEvent, channel->hHandle, kLTSocket_Event_ReadReady);
    }
}

static void
LTTurnClientImpl_CreateAllocation(LTTurnClientImpl *client) {
    SendAllocateMessage(client, false);
}

static void
LTTurnClientImpl_BindChannel(LTTurnClientImpl *client, LTNetIpv4Endpoint peerEndpoint) {
    u16 channelIdx = client->channels->API->GetCount(client->channels);
    if (channelIdx > kMaxNumChannels) return;

    LTSocket hSocket = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTSocket, sizeof(TurnChannel));
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return;
    lt_memset(channel, 0, sizeof(TurnChannel));

    channel->magic        = kTurnChannelMagic;
    channel->hHandle      = hSocket;
    channel->client       = client;
    channel->channelNum   = kFirstChannelNum + channelIdx;
    channel->peerEndpoint = peerEndpoint;
    channel->hEvent       = LT_GetCore()->CreateEvent(&s_SocketEventArgs, DispatchSocketEvent, NULL, NULL, NULL);
    LTList_Init(&channel->rxQueue);

    client->channels->API->Append(client->channels, channel);

    SendCreatePermissionMessage(client, channel);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
}

static LTSocket
LTTurnClientImpl_GetChannelSocket(LTTurnClientImpl *client, LTNetIpv4Endpoint peerEndpoint) {
    LTArray *channels = client->channels;
    for (u32 i = 0; i < channels->API->GetCount(channels); ++i) {
        TurnChannel *channel = channels->API->Get(channels, i, NULL);
        if (LTNetIpv4Endpoint_IsEqual(channel->peerEndpoint, peerEndpoint)) {
            return channel->hHandle;
        }
    }
    return 0;
}

static LTNetIpv4Endpoint
LTTurnClientImpl_GetRelayedAddress(LTTurnClientImpl *client) {
    return client->relayedAddress;
}

static LTNetIpv4Endpoint
LTTurnClientImpl_GetMappedAddress(LTTurnClientImpl *client) {
    return client->mappedAddress;
}

static void
LTTurnClientImpl_OnTurnEvent(LTTurnClientImpl *client, LTTurnClient_TurnEventProc *pCallback, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void *clientData) {
    s_pEvent->RegisterForEvent(client->hEvent, (void *)pCallback, clientDataReleaseProc, clientData, false);
}

static void
LTTurnClientImpl_NoTurnEvent(LTTurnClientImpl *client, LTTurnClient_TurnEventProc *pCallback) {
    s_pEvent->UnregisterFromEvent(client->hEvent, (void *)pCallback);
}

static void
LTTurnClientImpl_Init(LTTurnClientImpl *client, LTStunAgent *stunAgent, LTNetIpv4Endpoint serverEndpoint, const char *username, const char *password) {
    client->serverEndpoint       = serverEndpoint;
    client->username             = lt_strdup(username);
    client->password             = lt_strdup(password);

    client->stunAgent = stunAgent;
    client->stunAgent->API->AddCredentials(client->stunAgent, serverEndpoint, client->username, client->password, kLTStunCredentialsUsageContext_Any);
    client->stunTransport = client->stunAgent->API->OpenTransport(client->stunAgent, 0, serverEndpoint, OnStunUnhandledDatagram, client);
    client->stunTransport->API->OnMessageReceived(client->stunTransport, OnStunMessageReceived, NULL, client);
}

/*___________________________________
  LTTurnClient object constructors */
static bool
LTTurnClientImpl_ConstructObject(LTTurnClientImpl *client) {
    client->hThread = s_pThread->GetCurrentThread();
    client->hEvent = LT_GetCore()->CreateEvent(&s_TurnEventArgs, DispatchTurnEventCB, NULL, NULL, NULL);
    client->channels = lt_createobject(LTArray);
    return true;
}

static void
LTTurnClientImpl_DestructObject(LTTurnClientImpl *client) {
    s_pThread->KillTimer(client->hThread, OnRefreshAllocationTimerExpired, client);
    if (client->hEvent)        lt_destroyhandle(client->hEvent);
    if (client->username)      lt_free(client->username);
    if (client->password)      lt_free(client->password);
    if (client->nonce)         lt_free(client->nonce);
    if (client->realm)         lt_free(client->realm);
    LTArray *channels = client->channels;
    for (u32 i = 0; i < channels->API->GetCount(channels); ++i) {
        TurnChannel *channel = channels->API->Get(channels, i, NULL);
        if (channel->hHandle) lt_destroyhandle(channel->hHandle);
    }
    lt_destroyobject(channels);
}

/*______________________________________
  LTTurnClient LTObjectApi definition */
define_LTObjectImplPublic(LTTurnClient, LTTurnClientImpl,
    Init,
    CreateAllocation,
    BindChannel,
    GetChannelSocket,
    GetRelayedAddress,
    GetMappedAddress,
    OnTurnEvent,
    NoTurnEvent,
);

/*________________________________________________
  TurnChannel LTSocket private helper functions */
static void
DispatchSocketEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc || !args) return;
    LTSocket hSocket = LTArgs_lthandleAt(0, args);
    u32 sockEvent    = LTArgs_u32At(1, args);

    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return;
    if (sockEvent == kLTSocket_Event_ReadReady) {
        LTAtomic_Store(&channel->readPending, 0);
    }

    (*(LTSocket_EventProc *)proc)(hSocket, sockEvent, data);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
}

/*____________________________________________
  TurnChannel LTSocket public api functions */
static void
TurnChannel_DestroySocket(LTHandle hSocket) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return;
    s_pThread->KillTimer(channel->client->hThread, OnRefreshChannel, channel);
    if (channel->hEvent) lt_destroyhandle(channel->hEvent);
    while (!LTList_IsEmpty(&channel->rxQueue)) {
        ChannelPacket *packet = LT_CONTAINER_OF(channel->rxQueue.pNext, ChannelPacket, node);
        LTList_Remove(&packet->node);
        lt_free(packet);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
}

static void
TurnChannel_OnSocketEvent(LTSocket hSocket, LTSocket_EventProc proc, void *procData) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return;
    s_pEvent->RegisterForEvent(channel->hEvent, proc, NULL, procData, false);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
}

static void
TurnChannel_NoSocketEvent(LTSocket hSocket, LTSocket_EventProc proc) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return;
    s_pEvent->UnregisterFromEvent(channel->hEvent, proc);
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
}

static void
TurnChannel_GetSocketSpec(LTSocket hSocket, char *spec, int specSize) {
    LT_UNUSED(hSocket);
    LT_UNUSED(spec);
    LT_UNUSED(specSize);
}

static bool
TurnChannel_GetProperty(LTSocket hSocket, const char *name, void *value) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return false;
    if (lt_strcmp(name, "recv.endpoint.v4") == 0) {
        *((LTNetIpv4Endpoint *)value) = channel->peerEndpoint;
    } else {
        LTLOG_YELLOWALERT("getprop.notimp", NULL);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
    return true;
}

static bool
TurnChannel_SetProperty(LTSocket hSocket, const char *name, const void *value) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return false;
    if (lt_strcmp(name, "send.endpoint.v4") == 0) {
        const LTNetIpv4Endpoint *sendEndpoint = value;
        if (!LTNetIpv4Endpoint_IsEqual(channel->peerEndpoint, *sendEndpoint)) {
            LTLOG_YELLOWALERT("bad.ep", "Request to send to a different endpoint (%lu.%lu.%lu.%lu:%lu) than is bound to TURN channel (%lu.%lu.%lu.%lu:%lu)",
                LT_Pu32((sendEndpoint->address >> 0) & 0xFF),
                LT_Pu32((sendEndpoint->address >> 8) & 0xFF),
                LT_Pu32((sendEndpoint->address >> 16) & 0xFF),
                LT_Pu32((sendEndpoint->address >> 24) & 0xFF),
                LT_Pu32(sendEndpoint->port),
                LT_Pu32((channel->peerEndpoint.address >> 0) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 8) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 16) & 0xFF),
                LT_Pu32((channel->peerEndpoint.address >> 24) & 0xFF),
                LT_Pu32(channel->peerEndpoint.port));
            LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
            return false;
        }
    } else if (lt_strcmp(name, "dscp") == 0) {
        channel->dscp = *(u8 *)value;
    } else {
        LTLOG_YELLOWALERT("setprop.notimp", NULL);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
    return true;
}

static void
TurnChannel_ConnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
    LTLOG_YELLOWALERT("conn.notimp", NULL);
}

static void
TurnChannel_DisconnectSocket(LTSocket hSocket) {
    LT_UNUSED(hSocket);
    LTLOG_YELLOWALERT("disconn.notimp", NULL);
}

static s32
TurnChannel_WriteSocket(LTSocket hSocket, const void *data, u32 dataLen) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return 0;

    u32 channelMsgLen = dataLen + 4;
    u8 *channelMsg = lt_malloc(channelMsgLen);
    if (!channelMsg) {
        LTLOG_YELLOWALERT("chan.msg.alloc.err", "Failed to allocate buffer for TURN channel message");
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
        return -1;
    }

    u8 *p = channelMsg;
    *((u16*)p) = LT_NTOHS(channel->channelNum);
    p += sizeof(u16);
    *((u16*)p) = LT_NTOHS(dataLen);
    p += sizeof(u16);
    lt_memcpy(p, data, dataLen);
    s32 ret = channel->client->stunTransport->API->SendDatagram(channel->client->stunTransport, channel->client->serverEndpoint, channelMsg, channelMsgLen, channel->dscp);
    channel->dscp = 0;
    lt_free(channelMsg);
    channel->lastPacketTime = LT_GetCore()->GetKernelTime();

    if (ret <= 0) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
        return ret;
    }
    if ((u32)ret < channelMsgLen) {
        LTLOG_YELLOWALERT("part.wrt", "Partial write of datagram: %lu of %lu sent", LT_Pu32(ret), LT_Pu32(channelMsgLen));
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
    return ret - 4;
}

static s32
TurnChannel_ReadSocket(LTSocket hSocket, void *data, u32 dataSize) {
    TurnChannel *channel = LT_GetCore()->ReserveHandlePrivateData(hSocket);
    if (!channel) return 0;
    if (LTList_IsEmpty(&channel->rxQueue)) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
        return 0;
    }
    ChannelPacket *packet = LT_CONTAINER_OF(channel->rxQueue.pNext, ChannelPacket, node);
    u32 packetSize = packet->size;
    if (!data && !dataSize) {
        LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
        return packetSize;
    }
    if (data && dataSize) {
        if (dataSize < packetSize) {
            packetSize = dataSize;
        }
        LTList_Remove(&packet->node);
        lt_memcpy(data, packet->data, packetSize);
        lt_free(packet);
        channel->lastPacketTime = LT_GetCore()->GetKernelTime();
    }
    LT_GetCore()->ReleaseHandlePrivateData(hSocket, channel);
    return packetSize;
}

define_LTLIBRARY_INTERFACE(ILTSocket, TurnChannel_DestroySocket)
    .OnSocketEvent    = TurnChannel_OnSocketEvent,
    .NoSocketEvent    = TurnChannel_NoSocketEvent,
    .GetSocketSpec    = TurnChannel_GetSocketSpec,
    .GetProperty      = TurnChannel_GetProperty,
    .SetProperty      = TurnChannel_SetProperty,
    .ConnectSocket    = TurnChannel_ConnectSocket,
    .DisconnectSocket = TurnChannel_DisconnectSocket,
    .WriteSocket      = TurnChannel_WriteSocket,
    .ReadSocket       = TurnChannel_ReadSocket,
};

/*____________________________________
 / LTNetTurn library initialization */
static bool
LTNetTurnImpl_LibInit(void) {
    s_pThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_pEvent  = lt_getlibraryinterface(ILTEvent,  LT_GetCore());
    return true;
}

static void
LTNetTurnImpl_LibFini(void) {
}

/*____________________________________________
 / LTNetTurn library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetTurn, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetTurn, ) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTNetTurn,
    (LTTurnClientImpl)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Sep-23   trajan      created
 */
