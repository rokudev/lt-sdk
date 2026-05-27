/*******************************************************************************
 * source/lt/net/ice/LTNetIce.c - Interactive Connectivity Establishment (ICE) Agent
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/ice/LTNetIce.h>
#include <lt/net/stun/LTNetStun.h>
#include <lt/net/turn/LTNetTurn.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "ProtocolMuxSocket.h"

DEFINE_LTLOG_SECTION("ice");

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTNetIce, (LTNetCore) (LTUtilityByteOps) (LTUtilityTokenizer) (LTSystemCrypto));

typedef struct LTIceServer LTIceServer;
typedef struct LTIceAgentImpl LTIceAgentImpl;

enum {
    kCandidateLocalPreference_SingleHomed    = 0xFFFFu,
    kComponentId_RTP                         = 1,
    kIceTransactionIntervalMs                = 15, // RFC 8445 'Ta' timeout
    kSendConsentCheckIntervalMs              = 15000,
    kSendConsentCheckTimeoutMs               = kSendConsentCheckIntervalMs + 1000,
    kCanonicalNameLen                        = 32 + 1,
    kDefaultIceServerPort                    = 3478,
    kUserFragmentLen                         = 16 + 1,
    kPasswordLen                             = 64 + 1,
};

// Introduced Trickle and NonTrickle mode as don't want to change default flow
typedef enum {
    kLTIceAgentMode_NonTrickle,
    kLTIceAgentMode_Trickle,
} LTIceAgentMode;

static const u32 candidateTypePreference[kLTIceCandidateType_Count] = {
    [kLTIceCandidateType_Host]            = 126,
    [kLTIceCandidateType_PeerReflexive]   = 110,
    [kLTIceCandidateType_ServerReflexive] = 100,
    [kLTIceCandidateType_Relay]           = 0,
};

// ICE Candidate priority calculation per RFC8445 section 5.1.2
LT_INLINE u32
CalcCandidatePriority(LTIceCandidateType type) {
    return (u32)(candidateTypePreference[type]         << 24) +
           (u32)(kCandidateLocalPreference_SingleHomed <<  8) +
           (u32)(256u - (u32)kComponentId_RTP);
}

typedef enum {
    kLTIceGatheringState_New,
    kLTIceGatheringState_Gathering,
    kLTIceGatheringState_Complete,
} LTIceGatheringState;

LT_INLINE const char *
LTIceGatheringStateToString(LTIceGatheringState gatheringState) {
    switch (gatheringState) {
        case kLTIceGatheringState_New:       return "new";
        case kLTIceGatheringState_Gathering: return "gathering";
        case kLTIceGatheringState_Complete:  return "complete";
        default:                             return "unknown";
    }
}

typedef enum {
    kCandidatePairFlags_New           = 0,
    kCandidatePairFlags_CheckComplete = (1 << 0),
    kCandidatePairFlags_Nominated     = (1 << 1),
    kCandidatePairFlags_Selected      = kCandidatePairFlags_CheckComplete | kCandidatePairFlags_Nominated
} CandidatePairFlags;

// RFC5245 ICE/STUN Attributes
typedef enum {
    kIceStunAttrType_Priority               = 0x0024,
    kIceStunAttrType_UseCandidate           = 0x0025,
    kIceStunAttrType_IceControlled          = 0x8029,
    kIceStunAttrType_IceControlling         = 0x802A,
    kIceStunAttrType_Nomination             = 0xC001,
} IceStunAttrType;

/*_____________________________________________
  LTIceCandidate object private data members */
typedef_LTObjectImpl(LTIceCandidate, LTIceCandidateImpl) {
    LTIceAgentImpl         *agent;
    LTIceCandidateType      nCandidateType;
    u32                     nPriority;
    LTNetIpv4Endpoint       endpoint;
    LTIceCandidateTransport transport;
    char                    foundation[kLTIceCandidate_FoundationLen];
    LTIceServer            *server;
    LTIceCandidateStatus    nCandidateStatus; // whether ice candidate status is tobesent or sent
} LTOBJECT_API;

typedef_LTObjectImpl(LTIceCandidatePair, LTIceCandidatePairImpl) {
    LTIceAgentImpl     *agent;
    LTIceCandidateImpl *localCandidate;
    LTIceCandidateImpl *remoteCandidate;
    u64                 priority;
    CandidatePairFlags  flags;
    LTSocket            hSocket;
    ProtocolMuxSocket  *protocolMux;
    LTStunTransport    *stunTransport;
    LTStunTransactionId transactionId;
} LTOBJECT_API;

typedef_LTObjectImpl(LTIceAgent, LTIceAgentImpl) {
    LTEvent                 hIceEvent;
    u64                     nRoleTieBreaker;
    char                    canonicalName[kCanonicalNameLen];
    LTIceAgentMode          iceMode;
    LTIceGatheringState     gatheringState;
    LTTime                  gatheringStartTime;
    LTTime                  gatheringDuration;
    LTIceConnectionState    connectionState;
    LTTime                  connectionStartTime;
    LTTime                  connectionDuration;
    u32                     pendingIceRequests;
    LTNetIpv4Endpoint       baseEndpoint;
    LTIceCandidateImpl     *baseCandidate;
    LTArray                *iceServers;
    LTArray                *pLocalCandidates;
    LTArray                *pRemoteCandidates;
    LTArray                *candidateChecklist;
    u32                     connCheckCandidateIdx;
    LTIceCandidatePairImpl *selectedPair;
    LTStunAgent            *stunAgent;
    LTStunTransport        *stunTransport;
    LTThread                hConnCheckThread;
    char                   *pLocalUserFragment;
    char                   *pLocalPassword;
    ProtocolMuxSocket      *protocolMux;
    char                   *sessionId;
    u32                     curNomination;
    bool                    isRemoteCredentialsSet; /* This flag handles the scenario when candidate pair starts connection checks before sdp offer is received in trickle live streaming */
} LTOBJECT_API;

typedef enum {
    kIceServerState_New,
    kIceServerState_DnsResolving,
    kIceServerState_DnsResolved,
    kIceServerState_DnsFailed,
    kIceServerState_Checking,
    kIceServerState_CheckComplete,
} LTIceServerState;

typedef enum {
    kLTIceServerType_None,
    kLTIceServerType_STUN,
    kLTIceServerType_TURN,
} LTIceServerType;

typedef struct LTIceServer {
    LTIceAgentImpl    *agent;
    LTIceServerState   state;
    LTIceServerType    type;
    char              *host;
    char              *username;
    char              *password;
    LTNetIpv4Endpoint  endpoint;
    LTTurnClient      *turnClient;
    u32                pendingChannelBinds;
} LTIceServer;

static ILTEvent             *s_iEvent       = NULL;
static ILTThread            *s_iThread      = NULL;

static const LTArgsDescriptor s_IceEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_u32}};

static void TriggerConnectionCheck(LTIceAgentImpl *agent, LTIceCandidatePairImpl *candidatePair);
static void OnSendConsentExpired(void *clientData);
static void OnStunMessage(LTStunTransport *stunTransport, LTStunMessage *message, void *clientData);

/*_____________________________________
  LTIceCandidate object constructors */
static bool
LTIceCandidateImpl_ConstructObject(LTIceCandidateImpl *candidate) {
    LT_UNUSED(candidate);
    return true;
}

static void
LTIceCandidateImpl_DestructObject(LTIceCandidateImpl *candidate) {
    LT_UNUSED(candidate);
}

/*______________________________________
  LTIceCandidate public api functions */
static void
LTIceCandidateImpl_Init(LTIceCandidateImpl *candidate, LTIceAgentImpl *agent, LTIceCandidateTransport transport, LTIceCandidateType type, u32 priority, LTNetIpv4Endpoint endpoint, const char *foundation) {
    candidate->agent            = agent;
    candidate->transport        = transport;
    candidate->nCandidateType   = type;
    candidate->nPriority        = priority;
    candidate->endpoint         = endpoint;
    candidate->nCandidateStatus = kLTIceCandidateStatus_ToBeSent;
    lt_strncpyTerm(candidate->foundation, foundation, sizeof(candidate->foundation));
}

static LTIceCandidateType
LTIceCandidateImpl_GetType(LTIceCandidateImpl *candidate) {
    return candidate->nCandidateType;
}

static LTIceCandidateTransport
LTIceCandidateImpl_GetTransport(LTIceCandidateImpl *candidate) {
    return candidate->transport;
}

static u32
LTIceCandidateImpl_GetPriority(LTIceCandidateImpl *candidate) {
    return candidate->nPriority;
}

static LTNetIpv4Endpoint
LTIceCandidateImpl_GetEndpoint(LTIceCandidateImpl *candidate) {
    return candidate->endpoint;
}

static const char *
LTIceCandidateImpl_GetFoundation(LTIceCandidateImpl *candidate) {
    return candidate->foundation;
}

static LTIceCandidateStatus
LTIceCandidateImpl_GetCandidateStatus(LTIceCandidateImpl *candidate) {
    if (!candidate) return kLTIceCandidateStatus_Invalid;
    return candidate->nCandidateStatus;
}

static void
LTIceCandidateImpl_SetCandidateStatus(LTIceCandidateImpl *candidate, LTIceCandidateStatus candidateStatus) {
    if (!candidate) return;
    candidate->nCandidateStatus = candidateStatus;
}

/*_______________________________________
  LTIceCandidate LTObjectApi definition */
define_LTObjectImplPublic(LTIceCandidate, LTIceCandidateImpl,
    Init,
    GetType,
    GetTransport,
    GetPriority,
    GetEndpoint,
    GetFoundation,
    GetCandidateStatus,
    SetCandidateStatus,
);

/*_________________________________________
  LTIceCandidatePair object constructors */
static bool
LTIceCandidatePairImpl_ConstructObject(LTIceCandidatePairImpl *candidatePair) {
    LT_UNUSED(candidatePair);
    return true;
}

static void
LTIceCandidatePairImpl_DestructObject(LTIceCandidatePairImpl *candidatePair) {
    if (candidatePair->protocolMux)   ProtocolMuxSocket_Destroy(candidatePair->protocolMux);
}

/*__________________________________________
  LTIceCandidatePair public api functions */
static LTIceCandidate *
LTIceCandidatePairImpl_GetLocalCandidate(LTIceCandidatePairImpl *candidatePair) {
    return (LTIceCandidate *)candidatePair->localCandidate;
}

static LTIceCandidate *
LTIceCandidatePairImpl_GetRemoteCandidate(LTIceCandidatePairImpl *candidatePair) {
    return (LTIceCandidate *)candidatePair->remoteCandidate;
}

static LTSocket
LTIceCandidatePairImpl_GetDtlsSocket(LTIceCandidatePairImpl *candidatePair) {
    return ProtocolMuxSocket_GetProtocolSocket(candidatePair->protocolMux, kLTRtcProtocol_DTLS);
}

static LTSocket
LTIceCandidatePairImpl_GetSrtpSocket(LTIceCandidatePairImpl *candidatePair) {
    return ProtocolMuxSocket_GetProtocolSocket(candidatePair->protocolMux, kLTRtcProtocol_SRTP);
}

/*_______________________________________
  LTIceCandidate LTObjectApi definition */
define_LTObjectImplPublic(LTIceCandidatePair, LTIceCandidatePairImpl,
    GetLocalCandidate,
    GetRemoteCandidate,
    GetDtlsSocket,
    GetSrtpSocket,
);

static bool
IsDuplicateCandidate(LTArray *candidateList, LTIceCandidateType nCandidateType, LTNetIpv4Endpoint endpoint) {
    for (u32 i = 0; i < candidateList->API->GetCount(candidateList); ++i) {
        LTIceCandidateImpl *candidate = candidateList->API->Get(candidateList, i, NULL);
        if ((candidate->nCandidateType   == nCandidateType) &&
            (candidate->endpoint.address == endpoint.address) &&
            (candidate->endpoint.port    == endpoint.port)) {
            return true;
        }
    }
    return false;
}

static void
ComputeFoundation(char foundation[kLTIceCandidate_FoundationLen], LTIceCandidateType candidateType, u32 baseAddress, u32 serverAddress, LTIceCandidateTransport transport) {
    LT_SHA256_CTX *hashCtx = LT_GetLTSystemCrypto()->CreateSeqSHA256();
    if (!hashCtx) return;
    u8 hash[SHA256_HASH_LENGTH];
    LT_GetLTSystemCrypto()->UpdateSeqSHA256(hashCtx, (u8 *)&candidateType, sizeof(candidateType));
    LT_GetLTSystemCrypto()->UpdateSeqSHA256(hashCtx, (u8 *)&baseAddress, sizeof(baseAddress));
    if ((candidateType == kLTIceCandidateType_ServerReflexive) || (candidateType == kLTIceCandidateType_Relay)) {
        LT_GetLTSystemCrypto()->UpdateSeqSHA256(hashCtx, (u8 *)&serverAddress, sizeof(serverAddress));
    }
    LT_GetLTSystemCrypto()->UpdateSeqSHA256(hashCtx, (u8 *)&transport, sizeof(transport));
    LT_GetLTSystemCrypto()->FinishSeqSHA256(hashCtx, hash);
    LT_GetLTSystemCrypto()->DestroySeqSHA256(hashCtx);

    LT_GetLTUtilityByteOps()->HexEncode(hash, kLTIceCandidate_FoundationLen/2, foundation, kLTIceCandidate_FoundationLen, false);
}

static void
AddLocalCandidate(LTIceAgentImpl *agent, LTIceCandidateTransport transport, LTIceCandidateType nCandidateType, LTNetIpv4Endpoint endpoint, LTIceServer *server) {
    if (IsDuplicateCandidate(agent->pLocalCandidates, nCandidateType, endpoint)) {
        LTLOG_DEBUG("dup.candidate", "Ignoring duplicate local ICE candidate: addr=%lu.%lu.%lu.%lu:%lu, type=%s",
            LT_Pu32((endpoint.address >> 0) & 0xFF),
            LT_Pu32((endpoint.address >> 8) & 0xFF),
            LT_Pu32((endpoint.address >> 16) & 0xFF),
            LT_Pu32((endpoint.address >> 24) & 0xFF),
            LT_Pu32(endpoint.port),
            LTIceCandidateTypeToString(nCandidateType));
        return;
    }
    LTLOG_DEBUG("new.candidate", "Adding local ICE candidate: addr=%lu.%lu.%lu.%lu:%lu, type=%s",
        LT_Pu32((endpoint.address >> 0) & 0xFF),
        LT_Pu32((endpoint.address >> 8) & 0xFF),
        LT_Pu32((endpoint.address >> 16) & 0xFF),
        LT_Pu32((endpoint.address >> 24) & 0xFF),
        LT_Pu32(endpoint.port),
        LTIceCandidateTypeToString(nCandidateType));

    u32 serverAddress = server ? server->endpoint.address : 0;
    char foundation[kLTIceCandidate_FoundationLen];
    ComputeFoundation(foundation, nCandidateType, agent->baseEndpoint.address, serverAddress, transport);

    LTIceCandidateImpl *candidate = (LTIceCandidateImpl *)lt_createobject(LTIceCandidate);
    candidate->API->Init((LTIceCandidate *)candidate, (LTIceAgent *)agent, transport, nCandidateType, CalcCandidatePriority(nCandidateType), endpoint, foundation);
    candidate->server = server;

    if (nCandidateType == kLTIceCandidateType_Host) agent->baseCandidate = candidate;
    agent->pLocalCandidates->API->Append(agent->pLocalCandidates, candidate);
}

static void
AddHostCandidate(LTIceAgentImpl *agent) {
    AddLocalCandidate(agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_Host, agent->baseEndpoint, NULL);
}

static LTIceCandidatePairImpl *
FindCandidatePair(LTIceAgentImpl *agent, LTStunTransport *stunTransport, LTNetIpv4Endpoint endpoint) {
    LTArray *candidateChecklist = agent->candidateChecklist;
    for (u32 i = 0; i < candidateChecklist->API->GetCount(agent->candidateChecklist); ++i) {
        LTIceCandidatePairImpl *candidatePair = candidateChecklist->API->Get(agent->candidateChecklist, i, NULL);
        if ((candidatePair->remoteCandidate->endpoint.address == endpoint.address) &&
            (candidatePair->remoteCandidate->endpoint.port == endpoint.port) &&
            (candidatePair->stunTransport == stunTransport)) {
            return candidatePair;
        }
    }
    return NULL;
}

static void
SelectCandidatePair(LTIceAgentImpl *agent, LTIceCandidatePairImpl *selectedPair) {
    if (selectedPair == agent->selectedPair) return;
    LTIceCandidateImpl *localCandidate  = selectedPair->localCandidate;
    LTIceCandidateImpl *remoteCandidate = selectedPair->remoteCandidate;
    const char *localTypeStr  = LTIceCandidateTypeToString(localCandidate->nCandidateType);
    const char *remoteTypeStr = LTIceCandidateTypeToString(remoteCandidate->nCandidateType);
    // IP addresses are PII so only the types get logged to the server
    LTLOG_SERVER_ONLY("can.pr.typ", "Candidate pair selected: local=%s remote=%s", localTypeStr, remoteTypeStr);
    LTLOG("can.pr.sel", "Candidate pair selected: local=%lu.%lu.%lu.%lu:%lu (%s) remote=%lu.%lu.%lu.%lu:%lu (%s)",
            LT_Pu32((localCandidate->endpoint.address >> 0) & 0xFF),
            LT_Pu32((localCandidate->endpoint.address >> 8) & 0xFF),
            LT_Pu32((localCandidate->endpoint.address >> 16) & 0xFF),
            LT_Pu32((localCandidate->endpoint.address >> 24) & 0xFF),
            LT_Pu32(localCandidate->endpoint.port),
            localTypeStr,
            LT_Pu32((remoteCandidate->endpoint.address >> 0) & 0xFF),
            LT_Pu32((remoteCandidate->endpoint.address >> 8) & 0xFF),
            LT_Pu32((remoteCandidate->endpoint.address >> 16) & 0xFF),
            LT_Pu32((remoteCandidate->endpoint.address >> 24) & 0xFF),
            LT_Pu32(remoteCandidate->endpoint.port),
            remoteTypeStr);
    agent->connectionState = kLTIceConnectionState_Connected;
    agent->connectionDuration = LTTime_Subtract(LT_GetCore()->GetKernelTime(), agent->connectionStartTime);
    if (agent->selectedPair) {
        agent->selectedPair->flags &= ~kCandidatePairFlags_Nominated;
    }
    agent->selectedPair = selectedPair;
    ProtocolMuxSocket_SetDefaultEndpoint(selectedPair->protocolMux, remoteCandidate->endpoint);
    s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_CandidateSelected);
}

static void
HandleIceConnectionCheckRequest(LTIceAgentImpl *agent, LTStunTransport *stunTransport, LTStunMessage *pStunMessage) {
    if (!pStunMessage->API->GetAttribute(pStunMessage, kLTStunAttrType_Username, kStunAttrFormat_void)) {
        LTLOG_DEBUG("no.uname", "STUN request is missing username. Ignoring.");
        return;
    }
    if (!pStunMessage->API->GetAttribute(pStunMessage, kIceStunAttrType_Priority, kStunAttrFormat_void)) {
        LTLOG_DEBUG("no.prio", "STUN request is missing priority. Ignoring.");
        return;
    }
    u32 flags = pStunMessage->API->GetFlags(pStunMessage);
    if (!(flags & kLTStunMessageFlags_FingerprintValid)) {
        LTLOG_DEBUG("bad.fprint", "STUN request fingerprint validation failed. Ignoring.");
        return;
    }

    LTNetIpv4Endpoint peerEndpoint = pStunMessage->API->GetEndpoint(pStunMessage);
    LTLOG_DEBUG("ice.conn.chk", "Valid ICE connection check STUN request from peer: %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((peerEndpoint.address >> 0) & 0xFF),
        LT_Pu32((peerEndpoint.address >> 8) & 0xFF),
        LT_Pu32((peerEndpoint.address >> 16) & 0xFF),
        LT_Pu32((peerEndpoint.address >> 24) & 0xFF),
        LT_Pu32(peerEndpoint.port));

    LTIceCandidatePairImpl *candidatePair = FindCandidatePair(agent, stunTransport, peerEndpoint);
    if (!candidatePair) {
        // should only ever receive peer reflexive candidates on the agent-level STUN transport
        LT_ASSERT(stunTransport == agent->stunTransport);
        LTLOG_DEBUG("new.prflx", "Unknown peer address/port - Adding new peer reflexive candidate");

        char foundation[kLTIceCandidate_FoundationLen];
        LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(kLTIceCandidate_FoundationLen/2, foundation, kLTIceCandidate_FoundationLen);

        u32 priority;
        if (!pStunMessage->API->GetAttribute(pStunMessage, kIceStunAttrType_Priority, kStunAttrFormat_u32, &priority)) {
            priority = CalcCandidatePriority(kLTIceCandidateType_PeerReflexive);
        }
        LTIceCandidate *prflxCandidate = lt_createobject(LTIceCandidate);
        prflxCandidate->API->Init(prflxCandidate, (LTIceAgent *)agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_PeerReflexive, priority, peerEndpoint, foundation);
        agent->API->AddRemoteCandidate((LTIceAgent *)agent, prflxCandidate);

        candidatePair = (LTIceCandidatePairImpl *)lt_createobject(LTIceCandidatePair);
        candidatePair->agent = agent;
        candidatePair->localCandidate = agent->baseCandidate;
        candidatePair->remoteCandidate = (LTIceCandidateImpl *)prflxCandidate;
        candidatePair->stunTransport = agent->stunTransport;
        candidatePair->protocolMux = agent->protocolMux;
        agent->candidateChecklist->API->Append(agent->candidateChecklist, candidatePair);
    }

    LTStunMessage *pResponseMessage = lt_createobject(LTStunMessage);
    if (!pResponseMessage) {
        LTLOG_YELLOWALERT("rsp.alloc.err", "Error allocating response message");
        return;
    }
    pResponseMessage->API->InitResponse(pResponseMessage, pStunMessage);
    pResponseMessage->API->SetAttribute(pResponseMessage, kIceStunAttrType_IceControlled,   kStunAttrFormat_u64,           agent->nRoleTieBreaker);
    pResponseMessage->API->SetAttribute(pResponseMessage, kLTStunAttrType_XorMappedAddress, kStunAttrFormat_XorEndpointV4, peerEndpoint);
    stunTransport->API->Send(stunTransport, pResponseMessage);

    u32 nomination = 0;
    bool bHasNomination = pStunMessage->API->GetAttribute(pStunMessage, kIceStunAttrType_Nomination, kStunAttrFormat_u32, &nomination);
    bool bHasUseCandidate = pStunMessage->API->GetAttribute(pStunMessage, kIceStunAttrType_UseCandidate, kStunAttrFormat_void);
    if ((bHasUseCandidate && (agent->connectionState == kLTIceConnectionState_Checking)) ||
        (bHasNomination   && (nomination > agent->curNomination))) {
        if (bHasNomination) agent->curNomination = nomination;
        candidatePair->flags |= kCandidatePairFlags_Nominated;
        if (candidatePair->flags == kCandidatePairFlags_Selected) {
            SelectCandidatePair(agent, candidatePair);
        } else {
            LTLOG_SERVER("wait.conn.ck", "Candidate not selected yet - waiting for connection check response");
            TriggerConnectionCheck(agent, candidatePair);
        }
    } else if ((agent->connectionState == kLTIceConnectionState_Connected) && (candidatePair == agent->selectedPair)) {
        LTLOG_DEBUG("sndcnst.rfrsh.req", "STUN request received from selected candidate. Refreshing send-consent timer.");
        s_iThread->SetTimer(agent->hConnCheckThread, LTTime_Milliseconds(kSendConsentCheckTimeoutMs), OnSendConsentExpired, NULL, agent);
    }
}

static void
SendConnectionCheck(LTIceCandidatePairImpl *candidatePair) {
    s_iThread->KillTimer(s_iThread->GetCurrentThread(), (LTThread_TaskProc *)SendConnectionCheck, candidatePair);
    LTNetIpv4Endpoint remoteCandidateEndpoint = candidatePair->remoteCandidate->endpoint;
    LTLOG_DEBUG("conn.chk", "Sending Connection check request to %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((remoteCandidateEndpoint.address >> 0) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 8) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 16) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 24) & 0xFF),
        LT_Pu32(remoteCandidateEndpoint.port));

    LTStunMessage *message = lt_createobject(LTStunMessage);
    if (!message) {
        LTLOG_YELLOWALERT("snd.alloc.err", "Error allocating send message");
        return;
    }

    LTIceAgentImpl *agent = candidatePair->agent;
    // Retransmission Timeout calculation per RFC 8445 section 14.3
    // with N=1, all candidates in checklist are implicitly waiting or in-progress
    u32 retransmissionTimeoutMs = LT_MAX(500u, (unsigned)(kIceTransactionIntervalMs * agent->candidateChecklist->API->GetCount(agent->candidateChecklist)));
    message->API->InitRequest(message, remoteCandidateEndpoint, kLTStunMethod_Binding, kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_Fingerprint, candidatePair);
    message->API->SetRetransmissionTimeout(message, retransmissionTimeoutMs);
    message->API->SetAttribute(message, kIceStunAttrType_Priority,      kStunAttrFormat_u32, candidatePair->localCandidate->nPriority);
    message->API->SetAttribute(message, kIceStunAttrType_IceControlled, kStunAttrFormat_u64, agent->nRoleTieBreaker);

    candidatePair->transactionId = candidatePair->stunTransport->API->Send(candidatePair->stunTransport, message);
}

static void
TriggerConnectionCheck(LTIceAgentImpl *agent, LTIceCandidatePairImpl *candidatePair) {
#ifdef LT_DEBUG
    LTNetIpv4Endpoint remoteCandidateEndpoint = candidatePair->remoteCandidate->endpoint;
    LTLOG_DEBUG("connck.triggered", "Connection check triggered for candidate: %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((remoteCandidateEndpoint.address >> 0) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 8) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 16) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 24) & 0xFF),
        LT_Pu32(remoteCandidateEndpoint.port));
#endif
    s_iThread->QueueTaskProc(agent->hConnCheckThread, (LTThread_TaskProc *)SendConnectionCheck, NULL, candidatePair);
}

static int
CompareCandidatePair(const void *element1, const void *element2, void *clientData) {
    LT_UNUSED(clientData);
    LTIceCandidatePairImpl **pair1 = (LTIceCandidatePairImpl **)element1;
    LTIceCandidatePairImpl **pair2 = (LTIceCandidatePairImpl **)element2;
    if ((*pair1)->priority == (*pair2)->priority) return 0;
    return (*pair1)->priority > (*pair2)->priority ? -1 : 1;
}

#ifdef LT_DEBUG
static void
FormatCandidate(char *addrStr, u32 addrStrLen, LTIceCandidateImpl *candidate) {
    lt_snprintf(addrStr, addrStrLen, "%lu.%lu.%lu.%lu:%lu (%s)",
        LT_Pu32((candidate->endpoint.address >> 0) & 0xFF),
        LT_Pu32((candidate->endpoint.address >> 8) & 0xFF),
        LT_Pu32((candidate->endpoint.address >> 16) & 0xFF),
        LT_Pu32((candidate->endpoint.address >> 24) & 0xFF),
        LT_Pu32(candidate->endpoint.port),
        LTIceCandidateTypeToString(candidate->nCandidateType));
}
#endif

static void
StartConnectionChecking(LTIceAgentImpl *agent) {
    agent->connectionStartTime = LT_GetCore()->GetKernelTime();
    LTArray *pLocalCandidates = agent->pLocalCandidates;
    for (u32 i = 0; i < pLocalCandidates->API->GetCount(pLocalCandidates); ++i) {
        LTIceCandidateImpl *localCandidate = pLocalCandidates->API->Get(pLocalCandidates, i, NULL);
        // Don't create candidate pairs for local server reflexive candidates. They are
        // redundant since they are sent from the same base address as host candidates.
        if (localCandidate->nCandidateType == kLTIceCandidateType_ServerReflexive) continue;

        // Trigger event to send host candidate in trickle, if candidate status is 'ToBeSent'.
        if (agent->iceMode == kLTIceAgentMode_Trickle && localCandidate->nCandidateType == kLTIceCandidateType_Host && localCandidate->nCandidateStatus == kLTIceCandidateStatus_ToBeSent) {
            s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_NewIceCandidate);
        }
        for (u32 j = 0; j < agent->pRemoteCandidates->API->GetCount(agent->pRemoteCandidates); ++j) {
            bool isCandidatePairDuplicate = false;
            LTIceCandidateImpl *remoteCandidate = agent->pRemoteCandidates->API->Get(agent->pRemoteCandidates, j, NULL);
            // Don't create candidate pairs from local relay candidates to remote non-relay candidates.
            // Roku's TURN infrastructure can't support these connections anyway.
            if ((localCandidate->nCandidateType  == kLTIceCandidateType_Relay) &&
                (remoteCandidate->nCandidateType != kLTIceCandidateType_Relay)) continue;

            // Candidate pair priority calculation per RFC 8445 section 6.1.2.3.
            u64 priority = ((u64)LT_MIN(remoteCandidate->nPriority, localCandidate->nPriority) << 32) +
                           ((u64)LT_MAX(remoteCandidate->nPriority, localCandidate->nPriority) << 1) +
                           (remoteCandidate->nPriority > localCandidate->nPriority ? 1 : 0);
            if (agent->iceMode == kLTIceAgentMode_Trickle) {
                /* Don't create duplicate candidate pairs whenever function is called by StartIceConnectionCheck api*/
                for (u32 i = 0; i < agent->candidateChecklist->API->GetCount(agent->candidateChecklist); ++i) {
                    LTIceCandidatePairImpl *candidatePair = agent->candidateChecklist->API->Get(agent->candidateChecklist, i, NULL);
                    if ((candidatePair->localCandidate->endpoint.address == localCandidate->endpoint.address) &&
                    (candidatePair->remoteCandidate->endpoint.address == remoteCandidate->endpoint.address) &&
                    (candidatePair->remoteCandidate->endpoint.port == remoteCandidate->endpoint.port) &&
                    (candidatePair->localCandidate->endpoint.port == localCandidate->endpoint.port) &&
                    (candidatePair->priority == priority)) {
                        isCandidatePairDuplicate = true;
                        break;
                    }
                }
            }
            if ((agent->iceMode != kLTIceAgentMode_Trickle) || (!isCandidatePairDuplicate)) {
                LTIceCandidatePairImpl *candidatePair = (LTIceCandidatePairImpl *)lt_createobject(LTIceCandidatePair);
                if (!candidatePair) {
                    LTLOG_YELLOWALERT("canpair.oom", "Failed to allocate memory for candidate pair");
                    return;
                }
                candidatePair->agent           = agent;
                candidatePair->localCandidate  = localCandidate;
                candidatePair->remoteCandidate = remoteCandidate;
                candidatePair->priority        = priority;

                if (localCandidate->nCandidateType == kLTIceCandidateType_Relay) {
                    LTIceServer *server = localCandidate->server;
                    LT_ASSERT(server);
                    LT_ASSERT(server->type == kLTIceServerType_TURN);

                    candidatePair->hSocket = localCandidate->server->turnClient->API->GetChannelSocket(localCandidate->server->turnClient, remoteCandidate->endpoint);
                    candidatePair->protocolMux = ProtocolMuxSocket_Create(candidatePair->hSocket, remoteCandidate->endpoint);
                    if (!candidatePair->protocolMux) {
                        lt_destroyobject(candidatePair);
                        continue;
                    }
                    LTSocket relayedStunSocket = ProtocolMuxSocket_GetProtocolSocket(candidatePair->protocolMux, kLTRtcProtocol_STUN);
                    candidatePair->stunTransport = agent->stunAgent->API->OpenTransport(agent->stunAgent, relayedStunSocket, remoteCandidate->endpoint, NULL, NULL);
                    candidatePair->stunTransport->API->OnMessageReceived(candidatePair->stunTransport, OnStunMessage, NULL, agent);
                } else {
                    candidatePair->protocolMux   = agent->protocolMux;
                    candidatePair->stunTransport = agent->stunTransport;
                }
                agent->candidateChecklist->API->Append(agent->candidateChecklist, candidatePair);
            }
        }
    }
    agent->candidateChecklist->API->Sort(agent->candidateChecklist, CompareCandidatePair, NULL);

#ifdef LT_DEBUG
    LTLOG_DEBUG("cklst.head", "LOCAL CANDIDATE                     REMOTE CANDIDATE                    PRIORITY");
    for (u32 i = 0; i < agent->candidateChecklist->API->GetCount(agent->candidateChecklist); ++i) {
        LTIceCandidatePairImpl *candidatePair = agent->candidateChecklist->API->Get(agent->candidateChecklist, i, NULL);
        char localAddr[36];
        char remoteAddr[36];
        FormatCandidate(localAddr, sizeof(localAddr), candidatePair->localCandidate);
        FormatCandidate(remoteAddr, sizeof(remoteAddr), candidatePair->remoteCandidate);
        LTLOG_DEBUG("cklst.pair", "%-35s %-35s %llu", localAddr, remoteAddr, LT_Pu64(candidatePair->priority));
    }
#endif
    if (!agent->hConnCheckThread) {
        char threadName[20] = {};
        lt_snprintf(threadName, sizeof(threadName), "ICEConn-%10s", agent->sessionId);
        agent->hConnCheckThread = LT_GetCore()->CreateThread(threadName);
        s_iThread->Start(agent->hConnCheckThread, NULL, NULL);
    }
    LTArray *candidateChecklist = agent->candidateChecklist;
    for (u32 i = 0; i < candidateChecklist->API->GetCount(candidateChecklist); ++i) {
        LTIceCandidatePairImpl *candidatePair = candidateChecklist->API->Get(agent->candidateChecklist, i, NULL);
        s_iThread->SetTimer(agent->hConnCheckThread, LTTime_Milliseconds(kIceTransactionIntervalMs * i), (LTThread_TaskProc *)SendConnectionCheck, NULL, candidatePair);
    }
}

static void
OnSendConsentExpired(void *clientData) {
    s_iThread->KillTimer(s_iThread->GetCurrentThread(), OnSendConsentExpired, clientData);
    LTIceAgentImpl *agent = clientData;
    if (!agent) return;
    LTLOG_DEBUG("sndcnst.xpr", "ICE send-consent check failed");
    s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_Disconnected);
}

static void
HandleIceConnectionCheckResponse(LTIceAgentImpl *agent, LTStunMessage *message) {
    LTIceCandidatePairImpl *candidatePair = message->API->GetClientData(message);
    if (!candidatePair) return;
#ifdef LT_DEBUG
    LTNetIpv4Endpoint remoteCandidateEndpoint = candidatePair->remoteCandidate->endpoint;
    LTLOG_DEBUG("connck.comp", "Connection succeeded complete for candidate: %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((remoteCandidateEndpoint.address >> 0) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 8) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 16) & 0xFF),
        LT_Pu32((remoteCandidateEndpoint.address >> 24) & 0xFF),
        LT_Pu32(remoteCandidateEndpoint.port));
#endif
    candidatePair->flags |= kCandidatePairFlags_CheckComplete;
    if (candidatePair->flags == kCandidatePairFlags_Selected) {
        SelectCandidatePair(agent, candidatePair);
    } else if (!agent->selectedPair) {
        LTLOG_SERVER("wait.nom", "Candidate not selected yet - waiting for candidate to be nominated by peer");
    }
    s_iThread->SetTimer(agent->hConnCheckThread, LTTime_Milliseconds(kSendConsentCheckIntervalMs), (LTThread_TaskProc *)SendConnectionCheck, NULL, candidatePair);
    if (candidatePair == agent->selectedPair) {
        LTLOG_DEBUG("sndcnst.rfrsh.rsp", "STUN response received from selected candidate. Refreshing send-consent timer.");
        s_iThread->SetTimer(agent->hConnCheckThread, LTTime_Milliseconds(kSendConsentCheckTimeoutMs), OnSendConsentExpired, NULL, agent);
    }
}

static void
IceGatheringComplete(LTIceAgentImpl *agent) {
    LTLOG_DEBUG("gather.comp", "ICE candidate gathering complete.");
    agent->gatheringState = kLTIceGatheringState_Complete;
    agent->gatheringDuration = LTTime_Subtract(LT_GetCore()->GetKernelTime(), agent->gatheringStartTime);
    agent->connectionState = kLTIceConnectionState_Checking;
    s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_IceGatheringComplete);

    StartConnectionChecking(agent);
}

static void
IceServerRequestComplete(LTIceServer *server) {
    if (server->state == kIceServerState_Checking) {
        LTIceAgentImpl *agent = server->agent;
        server->state = kIceServerState_CheckComplete;
        if (--agent->pendingIceRequests == 0) {
            IceGatheringComplete(agent);
        }
    }
}

static void
OnStunMessage(LTStunTransport *stunTransport, LTStunMessage *message, void *clientData) {
    LTIceAgentImpl *agent = clientData;
    if (!agent || !message) return;

    u32 msgClass = message->API->GetClass(message);
    if (msgClass == kLTStunClass_Error) {
        LTLOG_YELLOWALERT("stun.err", "STUN server returned error: %lu", LT_Pu32(message->API->GetErrorCode(message)));
        return;
    }

    if ((agent->gatheringState == kLTIceGatheringState_Gathering) && (agent->iceMode != kLTIceAgentMode_Trickle)) {
        if (msgClass != kLTStunClass_Response) {
            LTLOG_YELLOWALERT("bad.class", "Invalid STUN message class in ICE gathering state. Ignoring.");
            return;
        }

        LTIceServer *server = message->API->GetClientData(message);
        if (server->state != kIceServerState_Checking) {
            LTLOG_DEBUG("stun.rsp.bad.state", "Ignoring STUN server response - invalid state: %lu", LT_Pu32(server->state));
            return;
        }

        LTNetIpv4Endpoint mappedAddress;
        if (!message->API->GetAttribute(message, kLTStunAttrType_XorMappedAddress, kStunAttrFormat_XorEndpointV4, &mappedAddress)) {
            LTLOG_YELLOWALERT("no.addr", "STUN response missing mapped address attribute. Ignoring.");
            return;
        }

        LTNetIpv4Endpoint senderEndpoint = message->API->GetEndpoint(message);
        LT_UNUSED(senderEndpoint);
        LTLOG_DEBUG("rx.stun.msg", "Received STUN response from server: %lu.%lu.%lu.%lu:%lu",
            LT_Pu32((senderEndpoint.address >> 0) & 0xFF),
            LT_Pu32((senderEndpoint.address >> 8) & 0xFF),
            LT_Pu32((senderEndpoint.address >> 16) & 0xFF),
            LT_Pu32((senderEndpoint.address >> 24) & 0xFF),
            LT_Pu32(senderEndpoint.port));
        AddLocalCandidate(agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_ServerReflexive, mappedAddress, server);

        IceServerRequestComplete(server);
    } else if ((agent->gatheringState == kLTIceGatheringState_Complete) || (agent->iceMode == kLTIceAgentMode_Trickle)) {
        // requests and responses must be authenticated, indications need not be
        if ((msgClass == kLTStunClass_Request) || (msgClass == kLTStunClass_Response)) {
            u32 flags = message->API->GetFlags(message);
            if (!(flags & (kLTStunMessageFlags_MacSHA1 | kLTStunMessageFlags_MacSHA256))) {
                LTLOG_DEBUG("no.mac", "STUN request is missing MAC. Ignoring.");
                return;
            }
            if (!(flags & kLTStunMessageFlags_MacValid)) {
                LTLOG_DEBUG("bad.auth", "STUN request auth failed. Ignoring.");
                return;
            }
        }
        if (msgClass == kLTStunClass_Request) {
            HandleIceConnectionCheckRequest(agent, stunTransport, message);
        } else if ((msgClass == kLTStunClass_Response) || (msgClass == kLTStunClass_Indication)) {
            HandleIceConnectionCheckResponse(agent, message);
        }
    }
}

static bool
HasRemoteRelayCandidate(LTIceAgentImpl *agent) {
    LTArray *pRemoteCandidates = agent->pRemoteCandidates;
    for (u32 i = 0; i < pRemoteCandidates->API->GetCount(pRemoteCandidates); ++i) {
        LTIceCandidateImpl *candidate = pRemoteCandidates->API->Get(pRemoteCandidates, i, NULL);
        if (candidate->nCandidateType == kLTIceCandidateType_Relay)
            return true;
    }
    return false;
}

static void
OnTurnEvent(LTTurnClient *turnClient, LTNetTurnEvent event, void *clientData) {
    LTIceServer *server = clientData;
    LTIceAgentImpl *agent = server->agent;
    switch (event) {
        case kLTTurnEvent_CreateAllocationSuccess: {
            if (server->state != kIceServerState_Checking) {
                LTLOG_DEBUG("turn.rsp.bad.state", "Ignoring TURN server response - invalid state: %lu", LT_Pu32(server->state));
                return;
            }
            LTNetIpv4Endpoint relayedAddress = turnClient->API->GetRelayedAddress(turnClient);
            LTNetIpv4Endpoint mappedAddress  = turnClient->API->GetMappedAddress(turnClient);
            AddLocalCandidate(server->agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_ServerReflexive, mappedAddress,  server);
            if (agent->iceMode == kLTIceAgentMode_Trickle) s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_NewIceCandidate);
            if (!HasRemoteRelayCandidate(agent)) {
                IceServerRequestComplete(server);
                return;
            }
            AddLocalCandidate(server->agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_Relay,           relayedAddress, server);
            if (agent->iceMode == kLTIceAgentMode_Trickle) s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_NewIceCandidate);

            LTArray *pRemoteCandidates = agent->pRemoteCandidates;
            for (u32 i = 0; i < pRemoteCandidates->API->GetCount(pRemoteCandidates); ++i) {
                LTIceCandidateImpl *candidate = pRemoteCandidates->API->Get(pRemoteCandidates, i, NULL);
                if (candidate->nCandidateType == kLTIceCandidateType_Relay) {
                    turnClient->API->BindChannel(turnClient, candidate->endpoint);
                    ++server->pendingChannelBinds;
                }
            }
            break;
        }

        case kLTTurnEvent_ChannelBindingSuccess:
            if (--server->pendingChannelBinds == 0) {
                IceServerRequestComplete(server);
            }
            break;
    }
}

static void
DispatchIceEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTIceAgent_IceEventProc *pCallback = (LTIceAgent_IceEventProc *)pEventProc;
    LTIceAgent *agent = LTArgs_pointerAt(0, pEventArgs);
    LTIceAgentEvent event = LTArgs_u32At(1, pEventArgs);
    pCallback(agent, event, pEventProcClientData);
}

static void
LTIceAgentImpl_GetCandidateType(LTIceAgentImpl *agent, char *candidateType, u32 candidateTypeLen) {
    if (agent->selectedPair)
        lt_snprintf(candidateType, candidateTypeLen, "local:%s remote:%s",
            LTIceCandidateTypeToString(agent->selectedPair->localCandidate->nCandidateType),
            LTIceCandidateTypeToString(agent->selectedPair->remoteCandidate->nCandidateType));
    else
        lt_snprintf(candidateType, candidateTypeLen, "local:none remote:none");
}

static void
LTIceAgentImpl_OnIceEvent(LTIceAgentImpl *agent, LTIceAgent_IceEventProc *pCallback, LTThread_ClientDataReleaseProc * clientDataReleaseProc, void *clientData) {
    s_iEvent->RegisterForEvent(agent->hIceEvent, (void *)pCallback, clientDataReleaseProc, clientData, false);
}

static void
LTIceAgentImpl_NoIceEvent(LTIceAgentImpl *agent, LTIceAgent_IceEventProc *pCallback) {
    s_iEvent->UnregisterFromEvent(agent->hIceEvent, (void *)pCallback);
}

static void
InitiateServerReflexiveCandidateDiscovery(LTIceServer *server) {
    LTNetIpv4Endpoint serverEndpoint = server->endpoint;
    LTLOG_DEBUG("srflx.start", "Initiating server reflexive address discovery using server: %lu.%lu.%lu.%lu:%lu",
            LT_Pu32((serverEndpoint.address >> 0) & 0xFF),
            LT_Pu32((serverEndpoint.address >> 8) & 0xFF),
            LT_Pu32((serverEndpoint.address >> 16) & 0xFF),
            LT_Pu32((serverEndpoint.address >> 24) & 0xFF),
            LT_Pu32(serverEndpoint.port));
    LTStunMessage *message = lt_createobject(LTStunMessage);
    if (!message) {
        LTLOG_YELLOWALERT("stun.alloc.err", "Error allocating STUN message");
        return;
    }
    // Retransmission Timeout calculation per RFC 8445 section 14.3
    u32 count = server->agent->iceServers->API->GetCount(server->agent->iceServers);
    u32 retransmissionTimeoutMs = LT_MAX(500u, (unsigned)(kIceTransactionIntervalMs * count));
    message->API->InitRequest(message, serverEndpoint, kLTStunMethod_Binding, 0, server);
    message->API->SetRetransmissionTimeout(message, retransmissionTimeoutMs);
    server->agent->stunTransport->API->Send(server->agent->stunTransport, message);
}

static void
InitiateServerRelayCandidateDiscovery(LTIceServer *server) {
    LTNetIpv4Endpoint serverEndpoint = server->endpoint;
    LTLOG_DEBUG("relay.start", "Initiating server relay address discovery using server: %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((serverEndpoint.address >> 0) & 0xFF),
        LT_Pu32((serverEndpoint.address >> 8) & 0xFF),
        LT_Pu32((serverEndpoint.address >> 16) & 0xFF),
        LT_Pu32((serverEndpoint.address >> 24) & 0xFF),
        LT_Pu32(serverEndpoint.port));
    server->turnClient = lt_createobject(LTTurnClient);
    server->turnClient->API->Init(server->turnClient, server->agent->stunAgent, serverEndpoint, server->username, server->password);
    server->turnClient->API->OnTurnEvent(server->turnClient, OnTurnEvent, NULL, server);
    server->turnClient->API->CreateAllocation(server->turnClient);
}

static void
LTIceAgentImpl_AddRemoteCandidate(LTIceAgentImpl *agent, LTIceCandidate *pCandidate) {
    LTNetIpv4Endpoint candidateEndpoint = pCandidate->API->GetEndpoint(pCandidate);
    if (IsDuplicateCandidate(agent->pRemoteCandidates, pCandidate->API->GetType(pCandidate), candidateEndpoint)) {
        LTLOG_DEBUG("dup.rmt.cnd", "Ignoring duplicate remote ICE candidate: %lu.%lu.%lu.%lu:%lu",
            LT_Pu32((candidateEndpoint.address >> 0) & 0xFF),
            LT_Pu32((candidateEndpoint.address >> 8) & 0xFF),
            LT_Pu32((candidateEndpoint.address >> 16) & 0xFF),
            LT_Pu32((candidateEndpoint.address >> 24) & 0xFF),
            LT_Pu32(candidateEndpoint.port));
        return;
    }
    LTLOG_DEBUG("add.rmt.cand", "Adding remote ICE candidate: %lu.%lu.%lu.%lu:%lu",
        LT_Pu32((candidateEndpoint.address >> 0) & 0xFF),
        LT_Pu32((candidateEndpoint.address >> 8) & 0xFF),
        LT_Pu32((candidateEndpoint.address >> 16) & 0xFF),
        LT_Pu32((candidateEndpoint.address >> 24) & 0xFF),
        LT_Pu32(candidateEndpoint.port));
    agent->pRemoteCandidates->API->Append(agent->pRemoteCandidates, pCandidate);
}

static LTArray *
LTIceAgentImpl_GetLocalCandidates(LTIceAgentImpl *agent) {
    return agent->pLocalCandidates;
}

static char *
MakeUsername(const char *ufrag1, const char *ufrag2) {
    u32 usernameLen = lt_strlen(ufrag1) + lt_strlen(ufrag2) + 2;
    char *username = lt_malloc(usernameLen);
    lt_snprintf(username, usernameLen, "%s:%s", ufrag1, ufrag2);
    return username;
}

static void
LTIceAgentImpl_SetRemoteCredentials(LTIceAgentImpl *agent, const char *pRemoteUserFragment, const char *pRemotePassword) {
    char *localUsername  = MakeUsername(pRemoteUserFragment, agent->pLocalUserFragment);
    char *remoteUsername = MakeUsername(agent->pLocalUserFragment, pRemoteUserFragment);

    agent->stunAgent->API->AddCredentials(agent->stunAgent, LTNetIpv4Endpoint_Any(), localUsername, pRemotePassword, kLTStunCredentialsUsageContext_Local);
    agent->stunAgent->API->AddCredentials(agent->stunAgent, LTNetIpv4Endpoint_Any(), remoteUsername, agent->pLocalPassword, kLTStunCredentialsUsageContext_Remote);
    agent->isRemoteCredentialsSet = true;
    lt_free(localUsername);
    lt_free(remoteUsername);
}

static void
LTIceAgentImpl_GetLocalCredentials(LTIceAgentImpl *agent, char *pUserFragment, u32 nUserFragmentSize, char *pPassword, u32 nPasswordSize) {
    lt_strncpyTerm(pUserFragment, agent->pLocalUserFragment, nUserFragmentSize);
    lt_strncpyTerm(pPassword,     agent->pLocalPassword,     nPasswordSize);
}

static LTIceCandidatePair *
LTIceAgentImpl_GetSelectedCandidatePair(LTIceAgentImpl *agent) {
    return (LTIceCandidatePair *)agent->selectedPair;
}

static void
StartCandidateGathering(LTIceServer *server) {
    if (server->state == kIceServerState_DnsResolved) {
        server->state = kIceServerState_Checking;
        switch (server->type) {
            case kLTIceServerType_STUN:
                InitiateServerReflexiveCandidateDiscovery(server);
                break;
            case kLTIceServerType_TURN:
                InitiateServerRelayCandidateDiscovery(server);
                break;
            default: break;
        }
    } else {
        LTLOG_YELLOWALERT("gth.strt.badstate", "Invalid server state for start gathering: %lu", LT_Pu32(server->state));
    }
}

static void
OnDnsSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LTIceServer *server = clientData;
    ILTSocket *socket = lt_gethandleinterface(ILTSocket, hSocket);
    switch (event) {
        case kLTSocket_Event_DnsResolved: {
            u32 serverAddr;
            if (!socket->GetProperty(hSocket, "dns.addr.v4", &serverAddr)) {
                LTLOG_YELLOWALERT("dns.get.err", "Failed to get DNS result for host: %s", server->host);
                return;
            }
            server->endpoint.address = serverAddr;
            LTLOG_DEBUG("dns.done", "ICE server DNS resolution complete: %s -> %lu.%lu.%lu.%lu",
                server->host,
                LT_Pu32((serverAddr >> 0) & 0xFF),
                LT_Pu32((serverAddr >> 8) & 0xFF),
                LT_Pu32((serverAddr >> 16) & 0xFF),
                LT_Pu32((serverAddr >> 24) & 0xFF));
            server->state = kIceServerState_DnsResolved;
            StartCandidateGathering(server);
            lt_destroyhandle(hSocket);
            break;
        }

        case kLTSocket_Event_DnsError:
        case kLTSocket_Event_DnsTimeout:
            LTLOG_YELLOWALERT("dns.err", "DNS resolution failed for host: %s", server->host);
            server->state = kIceServerState_DnsFailed;
            lt_destroyhandle(hSocket);
            break;

        default:
            LTLOG_DEBUG("sock.ev.unhdld", "Unhandled ICE DNS socket event: %lu", LT_Pu32(event));
            break;
    }
}

static void
ResolveIceServer(LTIceServer *server) {
    server->state = kIceServerState_DnsResolving;
    int specSize = lt_snprintf(NULL, 0, "dns host: %s", server->host);
    char *sockSpec = lt_malloc(specSize + 1);
    lt_snprintf(sockSpec, specSize + 1, "dns host: %s", server->host);
    LT_GetLTNetCore()->OpenSocket(0, sockSpec, OnDnsSocketEvent, server);
    lt_free(sockSpec);
}

static void LTIceAgentImpl_StartIceConnectionCheck(LTIceAgentImpl *agent) {
    if (agent->iceMode != kLTIceAgentMode_Trickle) agent->iceMode = kLTIceAgentMode_Trickle;
    if (!agent->isRemoteCredentialsSet) return;
    bool isLocalIceCandidateHasTypeRelay = false;
    LTArray *pLocalCandidates = agent->pLocalCandidates;
    u32 localIceCandidateCount = pLocalCandidates->API->GetCount(pLocalCandidates);
    for (u32 i = 0; i < localIceCandidateCount; ++i) {
        LTIceCandidateImpl *localCandidate = pLocalCandidates->API->Get(pLocalCandidates, i, NULL);
        if (localCandidate->nCandidateType == kLTIceCandidateType_Relay) {
            isLocalIceCandidateHasTypeRelay = true;
        }
    }

    /* if remote relay type ice candidate is present, and local relay type ice candidate is not gathered then, gather local relay candidate & bind it */
    LTArray *pRemoteCandidates = agent->pRemoteCandidates;
    u32 remoteIceCandidateCount = pRemoteCandidates->API->GetCount(pRemoteCandidates);
    for (u32 i = 0; i < remoteIceCandidateCount; ++i) {
        LTIceCandidateImpl *remoteCandidate = pRemoteCandidates->API->Get(pRemoteCandidates, i, NULL);
        if (remoteCandidate->nCandidateType == kLTIceCandidateType_Relay) {
            if (isLocalIceCandidateHasTypeRelay) break;
            LTArray *iceServers = agent->iceServers;
            for (u32 i = 0; i < iceServers->API->GetCount(iceServers); ++i) {
                LTIceServer *server = iceServers->API->Get(iceServers, i, NULL);
                if (server->state == kIceServerState_CheckComplete) {
                    LTNetIpv4Endpoint relayedAddress = server->turnClient->API->GetRelayedAddress(server->turnClient);

                    AddLocalCandidate(server->agent, kLTIceCandidateTransport_UDP, kLTIceCandidateType_Relay, relayedAddress, server);
                    s_iEvent->NotifyEvent(agent->hIceEvent, agent, kLTIceAgentEvent_NewIceCandidate);

                    server->turnClient->API->BindChannel(server->turnClient, remoteCandidate->endpoint);
                    ++server->pendingChannelBinds;
                }
            }
        }
    }
    StartConnectionChecking(agent);
}

static void
LTIceAgentImpl_StartCandidateGathering(LTIceAgentImpl *agent) {
    agent->gatheringState = kLTIceGatheringState_Gathering;
    agent->gatheringStartTime = LT_GetCore()->GetKernelTime();
    LTArray *iceServers = agent->iceServers;
    agent->pendingIceRequests = iceServers->API->GetCount(iceServers);
    if (agent->pendingIceRequests == 0) {
        IceGatheringComplete(agent);
        return;
    }
    for (u32 i = 0; i < iceServers->API->GetCount(iceServers); ++i) {
        LTIceServer *server = iceServers->API->Get(iceServers, i, NULL);
        if (server->state == kIceServerState_New) {
            ResolveIceServer(server);
        } else {
            StartCandidateGathering(server);
        }
    }
}

typedef enum {
    kServerSpecParserState_ParamKey,
    kServerSpecParserState_Url,
    kServerSpecParserState_Port,
    kServerSpecParserState_Username,
    kServerSpecParserState_Password,
    kServerSpecParserState_Error,
} ServerSpecParserState;

typedef struct {
    ServerSpecParserState state;
    LTIceServer          *server;
} ServerSpecParserContext;

static bool
ParseIpAddress(const char *ipStr, u32 *ip) {
    u8 ipOctets[4];
    for (u32 i = 0; i < 4; ++i) {
        u32 octet = lt_strtou32(ipStr, (char **)&ipStr, 10);
        if (octet > 255) return false;
        if (((i <  3) && (*ipStr != '.')) ||
            ((i == 3) && (*ipStr != '\0'))) return false;
        ++ipStr;
        ipOctets[i] = octet;
    }
    lt_memcpy(ip, ipOctets, sizeof(u32));
    return true;
}

static bool
OnServerSpecToken(LTTokenizer *tokenizer, const char *token, char matchedSentinel, void *clientData) {
    LT_UNUSED(matchedSentinel);
    ServerSpecParserContext *parser = clientData;
    switch (parser->state) {
        case kServerSpecParserState_ParamKey: {
            const char *nextSentinels = " ";
            if (lt_strcmp(token, "stun") == 0) {
                parser->server->type = kLTIceServerType_STUN;
                parser->state = kServerSpecParserState_Url;
                nextSentinels = " :";
            } else if (lt_strcmp(token, "turn") == 0) {
                parser->server->type = kLTIceServerType_TURN;
                parser->state = kServerSpecParserState_Url;
                nextSentinels = " :";
            } else if (lt_strcmp(token, "username") == 0) {
                parser->state = kServerSpecParserState_Username;
            } else if (lt_strcmp(token, "password") == 0) {
                parser->state = kServerSpecParserState_Password;
                LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, " ");
            } else {
                LTLOG_YELLOWALERT("srvspec.badparam", "Unsupported ICE server parameter: %s", token);
                parser->state = kServerSpecParserState_Error;
                return false;
            }
            LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, nextSentinels);
            break;
        }

        case kServerSpecParserState_Url:
            if (ParseIpAddress(token, &parser->server->endpoint.address)) {
                parser->server->state = kIceServerState_DnsResolved;
            } else {
                parser->server->host = lt_strdup(token);
            }
            if (matchedSentinel == ' ') {
                parser->state = kServerSpecParserState_ParamKey;
                LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, ":");
            } else {
                parser->state = kServerSpecParserState_Port;
                LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, " ");
            }
            break;

        case kServerSpecParserState_Port:
            parser->server->endpoint.port = lt_strtou32(token, NULL, 0);
            parser->state = kServerSpecParserState_ParamKey;
            LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, ":");
            break;

        case kServerSpecParserState_Username:
            if (parser->server->type != kLTIceServerType_TURN) {
                LTLOG_YELLOWALERT("srvspec.user.badproto", "'username' parameter not supported for ICE server protocol: %lu", LT_Pu32(parser->server->type));
                parser->state = kServerSpecParserState_Error;
                return false;
            }
            parser->server->username = lt_strdup(token);
            parser->state = kServerSpecParserState_ParamKey;
            LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, ":");
            break;

        case kServerSpecParserState_Password:
            if (parser->server->type != kLTIceServerType_TURN) {
                LTLOG_YELLOWALERT("srvspec.pass.badproto", "'password' parameter not supported for ICE server protocol: %lu", LT_Pu32(parser->server->type));
                parser->state = kServerSpecParserState_Error;
                return false;
            }
            parser->server->password = lt_strdup(token);
            parser->state = kServerSpecParserState_ParamKey;
            LT_GetLTUtilityTokenizer()->SetSentinels(tokenizer, ":");
            break;

        default: return false;
    }
    return true;
}

static bool
ParseServerSpec(const char *serverSpec, LTIceServer *server) {
    ServerSpecParserContext parser = {
        .state = kServerSpecParserState_ParamKey,
        .server = server,
    };
    LTTokenizer *tokenizer = LT_GetLTUtilityTokenizer()->CreateTokenizer(":", OnServerSpecToken, &parser);
    LT_GetLTUtilityTokenizer()->ProcessData(tokenizer, serverSpec, lt_strlen(serverSpec), false);
    LT_GetLTUtilityTokenizer()->DestroyTokenizer(tokenizer);
    if (parser.state == kServerSpecParserState_Error) return false;
    if (!server->host && !server->endpoint.address) {
        LTLOG_YELLOWALERT("srvspec.nohost", "ICE server spec is missing required 'host' or 'ip' parameter");
        return false;
    }
    if (server->type == kLTIceServerType_TURN) {
        if (!server->username) {
            LTLOG_YELLOWALERT("srvspec.nouser", "TURN server spec is missing required 'username' parameter");
            return false;
        }
        if (!server->password) {
            LTLOG_YELLOWALERT("srvspec.nopass", "TURN server spec is missing required 'password' parameter");
            return false;
        }
    }
    return true;
}

static void
LTIceAgentImpl_AddIceServer(LTIceAgentImpl *agent, const char *serverSpec) {
    LTIceServer server = {
        .agent         = agent,
        .state         = kIceServerState_New,
        .type          = kLTIceServerType_None,
        .endpoint.port = kDefaultIceServerPort,
    };
    if (ParseServerSpec(serverSpec, &server)) {
        agent->iceServers->API->Append(agent->iceServers, &server);
    }
}

static bool
LTIceAgentImpl_Init(LTIceAgentImpl *agent, const char *sessionId) {
    agent->sessionId = lt_strdup(sessionId);
    if (!agent->sessionId) return false;
    LTSocket hBaseSocket = LT_GetLTNetCore()->OpenSocket(0, "udp", NULL, 0);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hBaseSocket);
    if (!pSocket) {
        LTLOG_YELLOWALERT("skt.err", "not able to open udp socket");
        lt_free(agent->sessionId);
        agent->sessionId = NULL;
        return false;
    }
    agent->iceMode = kLTIceAgentMode_NonTrickle;
    pSocket->GetProperty(hBaseSocket, "local.endpoint.v4", &agent->baseEndpoint);
    agent->protocolMux = ProtocolMuxSocket_Create(hBaseSocket, LTNetIpv4Endpoint_Any());

    LTSocket stunSocket = ProtocolMuxSocket_GetProtocolSocket(agent->protocolMux, kLTRtcProtocol_STUN);
    agent->stunAgent = lt_createobject(LTStunAgent);
    agent->stunAgent->API->Init(agent->stunAgent, sessionId, stunSocket);
    agent->stunTransport = agent->stunAgent->API->OpenTransport(agent->stunAgent, stunSocket, LTNetIpv4Endpoint_Any(), NULL, NULL);
    agent->stunTransport->API->OnMessageReceived(agent->stunTransport, OnStunMessage, NULL, agent);
    AddHostCandidate(agent);
    return true;
}

static void
LogSessionSummary(LTIceAgentImpl *agent) {
    LTLOG_SERVER("sum", "sid=%s sel_can_typ=%s gather_state=%s gather_dur(ms)=%lld conn_state=%s conn_dur(ms)=%lld",
        agent->sessionId,
        agent->selectedPair ? LTIceCandidateTypeToString(agent->selectedPair->remoteCandidate->nCandidateType) : "none",
        LTIceGatheringStateToString(agent->gatheringState),
        LT_Ps64(LTTime_GetMilliseconds(agent->gatheringDuration)),
        LTIceConnectionStateToString(agent->connectionState),
        LT_Ps64(LTTime_GetMilliseconds(agent->connectionDuration)));
}

/*_________________________________
  LTIceAgent object constructors */
static bool
LTIceAgentImpl_ConstructObject(LTIceAgentImpl *agent) {
    agent->hIceEvent           = LT_GetCore()->CreateEvent(&s_IceEventArgs, DispatchIceEventCB, NULL, NULL, NULL);
    agent->gatheringState      = kLTIceGatheringState_New;
    agent->connectionState     = kLTIceConnectionState_New;
    agent->pLocalCandidates    = lt_createobject_typed(LTArray, List);
    agent->pRemoteCandidates   = lt_createobject_typed(LTArray, List);
    agent->candidateChecklist  = lt_createobject_typed(LTArray, List);
    agent->iceServers          = LTArray_CreateStructArray(sizeof(LTIceServer));
    agent->pLocalUserFragment  = lt_malloc(kUserFragmentLen);
    agent->pLocalPassword      = lt_malloc(kPasswordLen);
    agent->isRemoteCredentialsSet = false;
    LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(sizeof(agent->canonicalName) / 2, agent->canonicalName, sizeof(agent->canonicalName));
    LT_GetLTUtilityByteOps()->GenRandomBytes((u8*)&agent->nRoleTieBreaker, sizeof(u64));
    LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(kUserFragmentLen/2, agent->pLocalUserFragment, kUserFragmentLen);
    LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(kPasswordLen/2,     agent->pLocalPassword,     kPasswordLen);

    return true;
}

static void
LTIceAgentImpl_DestructObject(LTIceAgentImpl *agent) {
    LogSessionSummary(agent);
    if (agent->hConnCheckThread)    lt_destroyhandle(agent->hConnCheckThread);
    if (agent->stunAgent)           lt_destroyobject(agent->stunAgent);
    if (agent->hIceEvent)           lt_destroyhandle(agent->hIceEvent);
    if (agent->candidateChecklist) {
        LTArray *candidateChecklist = agent->candidateChecklist;
        for (u32 i = 0; i < candidateChecklist->API->GetCount(candidateChecklist); ++i) {
            LTIceCandidatePairImpl *candidatePair = candidateChecklist->API->Get(candidateChecklist, i, NULL);
            if (candidatePair->protocolMux   == agent->protocolMux)   candidatePair->protocolMux = NULL;
            if (candidatePair->stunTransport == agent->stunTransport) candidatePair->stunTransport = NULL;
            lt_destroyobject(candidatePair);
        }
        lt_destroyobject(candidateChecklist);
    }
    if (agent->pLocalCandidates) {
        LTArray *pLocalCandidates = agent->pLocalCandidates;
        for (u32 i = 0; i < pLocalCandidates->API->GetCount(pLocalCandidates); ++i) {
            LTIceCandidate *candidate = pLocalCandidates->API->Get(pLocalCandidates, i, NULL);
            lt_destroyobject(candidate);
        }
        lt_destroyobject(pLocalCandidates);
    }
    if (agent->pRemoteCandidates) {
        LTArray *pRemoteCandidates = agent->pRemoteCandidates;
        for (u32 i = 0; i < pRemoteCandidates->API->GetCount(pRemoteCandidates); ++i) {
            LTIceCandidate *candidate = pRemoteCandidates->API->Get(pRemoteCandidates, i, NULL);
            lt_destroyobject(candidate);
        }
        lt_destroyobject(pRemoteCandidates);
    }
    if (agent->iceServers) {
        LTArray *iceServers = agent->iceServers;
        for (u32 i = 0; i < iceServers->API->GetCount(iceServers); ++i) {
            LTIceServer *server = iceServers->API->Get(iceServers, i, NULL);
            if (server->host)       lt_free(server->host);
            if (server->username)   lt_free(server->username);
            if (server->password)   lt_free(server->password);
            if (server->turnClient) lt_destroyobject(server->turnClient);
        }
        lt_destroyobject(iceServers);
    }
    if (agent->pLocalUserFragment)  lt_free(agent->pLocalUserFragment);
    if (agent->pLocalPassword)      lt_free(agent->pLocalPassword);
    ProtocolMuxSocket_Destroy(agent->protocolMux);
    lt_free(agent->sessionId);
}

/*____________________________________
  LTIceAgent LTObjectApi definition */
define_LTObjectImplPublic(LTIceAgent, LTIceAgentImpl,
    OnIceEvent,
    NoIceEvent,
    AddRemoteCandidate,
    GetLocalCandidates,
    AddIceServer,
    StartCandidateGathering,
    StartIceConnectionCheck,
    SetRemoteCredentials,
    GetLocalCredentials,
    GetSelectedCandidatePair,
    GetCandidateType,
    Init,
);

/*___________________________________________
 / LTNetIce library initialization */
static bool
LTNetIceImpl_LibInit(void) {
    if (!ProtocolMuxSocket_LibInit()) return false;
    s_iEvent     = lt_getlibraryinterface(ILTEvent, LT_GetCore());
    s_iThread    = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return true;
}

static void
LTNetIceImpl_LibFini(void) {
    ProtocolMuxSocket_LibFini();
}

/*___________________________________________
 / LTNetIce library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetIce, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetIce, ) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTNetIce,
    (LTIceAgentImpl)
    (LTIceCandidateImpl)
    (LTIceCandidatePairImpl)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  12-Jul-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
