/*******************************************************************************
 * source/lt/net/webrtc/LTNetWebRtc.c - WebRTC Agent
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTStdlib.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/webrtc/LTNetWebRtc.h>
#include <lt/net/ice/LTNetIce.h>
#include <lt/net/dtls/LTNetDtls.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include "LTRtcSessionDescription.h"

DEFINE_LTLOG_SECTION("webrtc");

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTNetWebRtc, (LTNetDtls) (LTSystemCrypto) (LTUtilityByteOps));

enum {
    kWebrtcConnectionTimeoutSec = 45,
};

typedef enum {
    kIceGatheringState_New,
    kIceGatheringState_Gathering,
    kIceGatheringState_Complete,
} IceGatheringState;

typedef struct {
    char               mediaId[kMediaIdLen];
    char               mediaStreamTrackId[kMediaStreamTrackIdLen];
    LTRtpExtensionMap  extMap;
    LTRtpPayloadFormat payloadFormat;
    u32                nSendSyncSource;
    LTRtpStream       *sendStream;
    u32                nReceiveSyncSource;
    LTRtpStream       *receiveStream;
    LTRtcMediaFlow     nMediaFlow;
} LTRtcRtpTransceiver;

typedef_LTObjectImpl(LTRtcPeerConnection, LTRtcPeerConnectionImpl) {
    LTEvent                   hEvent;
    u64                       nSessionId;
    u32                       nSessionVersion;
    u64                       nRoleTieBreaker;
    char                      canonicalName[32 + 1];
    LTRtcIceConnectionState   nIceConnectionState;
    LTIceAgent               *iceAgent;
    LTSocket                  hDtlsSocket;
    LTSrtpSession            *srtpSession;
    bool                      srtpSessionInitialized;
    LTArray                  *pTransceivers;
    char                      peerFingerprint[kLTIceCandidate_FingerprintLen];
    LTThread                  hConnTimeoutThread;
    char                     *sessionId;
    LTTime                    sessionStartTime;
    LTTime                    setupDuration;
    bool                      bPaused;
} LTOBJECT_API;

#define IP_OCTETS(ip) (((ip) >> 0) & 0xFF), (((ip) >> 8) & 0xFF), (((ip) >> 16) & 0xFF), (((ip) >> 24) & 0xFF)

static ILTThread           *s_pThread       = NULL;
static ILTEvent            *s_pEvent        = NULL;
static LTNetMonitor        *s_pNetMonitor   = NULL;

static const LTArgsDescriptor s_ConnectionEventArgs =
    {2, {kLTArgType_pointer, kLTArgType_u32}};

static bool
ConfigureTransceiver(LTRtcPeerConnectionImpl *peerConn, LTRtcRtpTransceiver *pTransceiver);

static void
OnConnectionTimeout(void *clientData) {
    s_pThread->KillTimer(s_pThread->GetCurrentThread(), OnConnectionTimeout, clientData);
    LTRtcPeerConnectionImpl *peerConn = clientData;
    if (!peerConn) return;
    if (peerConn->nIceConnectionState != kLTRtcIceConnectionState_Completed) {
        LTLOG_DEBUG("conn.timeout", "failed to establish connection");
        s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_Failed);
    }
}

static void
OnSrtpSessionEvent(LTSrtpSession *session, LTSrtpSession_Event event, void *pClientData) {
    LT_UNUSED(session);
    LTRtcPeerConnectionImpl *peerConn = pClientData;

    switch (event) {
        case kLTSrtpSession_Event_Established:
            peerConn->nIceConnectionState = kLTRtcIceConnectionState_Completed;
            peerConn->setupDuration = LTTime_Subtract(LT_GetCore()->GetKernelTime(), peerConn->sessionStartTime);
            s_pThread->KillTimer(peerConn->hConnTimeoutThread, OnConnectionTimeout, peerConn);
            for (u32 i = 0; i < peerConn->pTransceivers->API->GetCount(peerConn->pTransceivers); ++i) {
                LTRtcRtpTransceiver *pTransceiver = peerConn->pTransceivers->API->Get(peerConn->pTransceivers, i, NULL);
                if (pTransceiver->payloadFormat.mediaFormat.nEncoding != kLTMediaEncoding_Unknown) {
                    if (!ConfigureTransceiver(peerConn, pTransceiver)) {
                        LTLOG_YELLOWALERT("strm.open.err", "Failed to open RTP stream");
                        return;
                    }
                }
            }
            s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_Completed);
            break;

        case kLTSrtpSession_Event_Terminated:
            s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_Disconnected);
            break;

        case kLTSrtpSession_Event_MediaError:
            s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_MediaStreamError);
            break;
    }
}

static void
OnIceEvent(LTIceAgent *agent, LTIceAgentEvent event, void *pClientData) {
    LT_UNUSED(agent);
    LTRtcPeerConnectionImpl *peerConn = pClientData;
    if (event == kLTIceAgentEvent_NewIceCandidate) {
        peerConn->nIceConnectionState = kLTRtcIceConnectionState_NewIceCandidate;
        s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_NewIceCandidate);
    } else if (event == kLTIceAgentEvent_IceGatheringComplete) {
        peerConn->nIceConnectionState = kLTRtcIceConnectionState_GatheringComplete;
        s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_IceGatheringComplete);
    } else if (event == kLTIceAgentEvent_CandidateSelected) {
        LTIceCandidatePair *selectedPair = peerConn->iceAgent->API->GetSelectedCandidatePair(peerConn->iceAgent);
        if (!selectedPair) {
            LTLOG_YELLOWALERT("no.selcan", "Selected ICE candidate pair not available");
            return;
        }
        LTSocket hDtlsTransportSocket = selectedPair->API->GetDtlsSocket(selectedPair);
        LTSocket hSrtpSocket = selectedPair->API->GetSrtpSocket(selectedPair);

        if (peerConn->srtpSessionInitialized) {
            LTLOG_SERVER("conn.chg", "ICE connection changed");
            ILTSocket *pDtlsSocket = lt_gethandleinterface(ILTSocket, peerConn->hDtlsSocket);
            pDtlsSocket->SetProperty(peerConn->hDtlsSocket, "transport.socket",    &hDtlsTransportSocket);

            peerConn->srtpSession->API->SetTransportSocket(peerConn->srtpSession, hSrtpSocket);
        } else {
            peerConn->nIceConnectionState = kLTRtcIceConnectionState_Connected;

            peerConn->hDtlsSocket = LT_GetLTNetDtls()->WrapSocket(hDtlsTransportSocket, "dtls clientcert: comm");
            if (!peerConn->hDtlsSocket) {
                LTLOG_YELLOWALERT("dtls.cr8.err", "DTLS socket creation failed");
                return;
            }

            peerConn->srtpSession->API->Init(peerConn->srtpSession, peerConn->sessionId, hSrtpSocket, peerConn->hDtlsSocket);
            peerConn->srtpSession->API->OnSessionEvent(peerConn->srtpSession, OnSrtpSessionEvent, NULL, peerConn);
            peerConn->srtpSessionInitialized = true;

            ILTSocket *pDtlsSocket = lt_gethandleinterface(ILTSocket, peerConn->hDtlsSocket);
            pDtlsSocket->SetProperty(peerConn->hDtlsSocket, "sessionid", peerConn->sessionId);
            pDtlsSocket->SetProperty(peerConn->hDtlsSocket, "peer.fingerprint.sha256", peerConn->peerFingerprint);
            pDtlsSocket->ConnectSocket(peerConn->hDtlsSocket);
            s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_Connected);
        }
    } else if (event == kLTIceAgentEvent_Disconnected) {
        s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_Disconnected);
    }
}

static void
DispatchConnectionEventCB(LTEvent hEvent, void *pEventProc, LTArgs *pEventArgs, void *pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTRtcPeerConnection_ConnectionEventProc *pCallback = (LTRtcPeerConnection_ConnectionEventProc *)pEventProc;
    LTRtcPeerConnection *peerConn  = LTArgs_pointerAt(0, pEventArgs);
    LTRtcPeerConnectionEvent event = LTArgs_u32At(1, pEventArgs);
    pCallback(peerConn, event, pEventProcClientData);
}

static void
LTRtcPeerConnectionImpl_OnConnectionEvent(LTRtcPeerConnectionImpl *peerConn, LTRtcPeerConnection_ConnectionEventProc *pCallback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) {
    s_pEvent->RegisterForEvent(peerConn->hEvent, (void *)pCallback, pClientDataReleaseProc, pClientData, false);
}

static void
LTRtcPeerConnectionImpl_NoConnectionEvent(LTRtcPeerConnectionImpl *peerConn, LTRtcPeerConnection_ConnectionEventProc *pCallback) {
    s_pEvent->UnregisterFromEvent(peerConn->hEvent, (void *)pCallback);
}

static LTRtcIceConnectionState
LTRtcPeerConnectionImpl_GetConnectionState(LTRtcPeerConnectionImpl *peerConn) {
    return peerConn->nIceConnectionState;
}

static LTRtcRtpTransceiver *
GetRtpTransceiver(LTRtcPeerConnectionImpl *peerConn, u32 nMediaLineIdx, LTRtcMediaDescription *pMediaDescription) {
    LTArray *pTransceivers = peerConn->pTransceivers;
    u32 nCurNumTransceivers = pTransceivers->API->GetCount(pTransceivers);
    LT_ASSERT(nMediaLineIdx <= nCurNumTransceivers);

    LTRtcRtpTransceiver *pTransceiver;
    if (nMediaLineIdx == nCurNumTransceivers) {
        pTransceivers->API->SetCount(pTransceivers, nCurNumTransceivers + 1);
        pTransceiver = pTransceivers->API->Get(pTransceivers, nMediaLineIdx, NULL);
        lt_strncpyTerm(pTransceiver->mediaId, pMediaDescription->mediaId, kMediaIdLen);
        pTransceiver->extMap = pMediaDescription->extMap;
        pTransceiver->nReceiveSyncSource = pMediaDescription->nSyncSource;
        LT_GetLTUtilityByteOps()->GenRandomBytes((u8*)&pTransceiver->nSendSyncSource, sizeof(u32));
        LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(kMediaStreamTrackIdLen/2, pTransceiver->mediaStreamTrackId, kMediaStreamTrackIdLen);

        // prioritize g711 over opus
        LTRtpPayloadFormat *pSelectedFormat = NULL, *pOpusFormat = NULL;
        LTArray *pPayloadFormats = pMediaDescription->pPayloadFormats;
        for (u32 i = 0; i < pPayloadFormats->API->GetCount(pPayloadFormats); ++i) {
            LTRtpPayloadFormat *pPayloadFormat = pPayloadFormats->API->Get(pPayloadFormats, i, NULL);
            if (peerConn->srtpSession->API->SupportsFormat(&pPayloadFormat->mediaFormat)) {
                if (pPayloadFormat->mediaFormat.nEncoding == kLTMediaEncoding_Opus) {
                    pOpusFormat = pPayloadFormat;
                } else {
                    pSelectedFormat = pPayloadFormat;
                    break;
                }
            }
        }
        if (pSelectedFormat == NULL) pSelectedFormat = pOpusFormat;

        if (pSelectedFormat == NULL) {
            LTLOG_YELLOWALERT("no.sup.format", "Remote description did not contain any supported media formats");
            pTransceiver->nMediaFlow = kLTRtcMediaFlow_Inactive;
            pTransceiver->payloadFormat = (LTRtpPayloadFormat) {
                .nPayloadType = 0,
                .nRtcpFeedbackFlags = 0,
                .mediaFormat = {
                    .nEncoding = kLTMediaEncoding_Unknown,
                    .nKind = pMediaDescription->nKind,
                }
            };
        } else {
            pTransceiver->payloadFormat = *pSelectedFormat;
            switch (pMediaDescription->nMediaFlow) {
                case kLTRtcMediaFlow_Send:        pTransceiver->nMediaFlow = kLTRtcMediaFlow_Receive;     break;
                case kLTRtcMediaFlow_Receive:     pTransceiver->nMediaFlow = kLTRtcMediaFlow_Send;        break;
                case kLTRtcMediaFlow_SendReceive: pTransceiver->nMediaFlow = kLTRtcMediaFlow_SendReceive; break;
                case kLTRtcMediaFlow_Inactive:
                default:                          pTransceiver->nMediaFlow = kLTRtcMediaFlow_Inactive;    break;
            }
            if (!(pSelectedFormat->nRtcpFeedbackFlags & kLTRtcpFeedbackFlag_TransportWideCC))
                pTransceiver->extMap.nTransportWideCC = 0;
        }
    } else {
        pTransceiver = pTransceivers->API->Get(pTransceivers, nMediaLineIdx, NULL);
        if (lt_strcmp(pTransceiver->mediaId, pMediaDescription->mediaId) != 0) return NULL;
        switch (pMediaDescription->nMediaFlow) {
            case kLTRtcMediaFlow_Send:        pTransceiver->nMediaFlow = kLTRtcMediaFlow_Receive;     break;
            case kLTRtcMediaFlow_Receive:     pTransceiver->nMediaFlow = kLTRtcMediaFlow_Send;        break;
            case kLTRtcMediaFlow_SendReceive: pTransceiver->nMediaFlow = kLTRtcMediaFlow_SendReceive; break;
            case kLTRtcMediaFlow_Inactive:
            default:                          pTransceiver->nMediaFlow = kLTRtcMediaFlow_Inactive;    break;
        }
    }
    return pTransceiver;
}

static LTRtpStream *
OpenRtpStream(LTRtcPeerConnectionImpl *peerConn, LTRtcMediaFlow nMediaFlow, LTRtcRtpTransceiver *pTransceiver) {
    u32                  nSyncSource;
    LTRtpStreamDirection direction;
    if (nMediaFlow == kLTRtcMediaFlow_Send) {
        nSyncSource = pTransceiver->nSendSyncSource;
        direction = kLTRtpStreamDirection_Send;
    } else {
        nSyncSource = pTransceiver->nReceiveSyncSource;
        direction = kLTRtpStreamDirection_Receive;
    }

    LTRtpStream *stream = peerConn->srtpSession->API->OpenStream(peerConn->srtpSession,
                                                   direction,
                                                   nSyncSource,
                                                   &pTransceiver->extMap,
                                                   &pTransceiver->payloadFormat);
    if (!stream) {
        LTLOG_YELLOWALERT("open.rtp.stm.err", "Failed to open RTP stream");
    }
    return stream;
}

static bool
ConfigureTransceiver(LTRtcPeerConnectionImpl *peerConn, LTRtcRtpTransceiver *pTransceiver) {
    if (pTransceiver->nMediaFlow & kLTRtcMediaFlow_Send) {
        if (!pTransceiver->sendStream) {
            pTransceiver->sendStream = OpenRtpStream(peerConn, kLTRtcMediaFlow_Send, pTransceiver);
            if (!pTransceiver->sendStream) {
                pTransceiver->nMediaFlow &= ~kLTRtcMediaFlow_Send;
            }
        }
        if (pTransceiver->sendStream) {
            pTransceiver->sendStream->API->Resume(pTransceiver->sendStream);
        }
    } else if (pTransceiver->sendStream) {
        /* The peer has requested to discontinue this stream. Pause, but keep it open in case they would like to resume it later. */
        pTransceiver->sendStream->API->Pause(pTransceiver->sendStream);
    }

    if (pTransceiver->nMediaFlow & kLTRtcMediaFlow_Receive) {
        if (!pTransceiver->receiveStream) {
            pTransceiver->receiveStream = OpenRtpStream(peerConn, kLTRtcMediaFlow_Receive, pTransceiver);
            if (!pTransceiver->receiveStream) {
                pTransceiver->nMediaFlow &= ~kLTRtcMediaFlow_Receive;
            }
        }
        if (pTransceiver->receiveStream) {
            pTransceiver->receiveStream->API->Resume(pTransceiver->receiveStream);
        }
    } else if (pTransceiver->receiveStream) {
        /* The peer has requested to discontinue this stream. Pause, but keep it open in case they would like to resume it later. */
        pTransceiver->receiveStream->API->Pause(pTransceiver->receiveStream);
    }

    return true;
}

static bool
HandleRemoteDescription(LTRtcPeerConnectionImpl *peerConn, LTRtcSessionDescription *pRemoteDescription) {
    LTArray *pMediaDescriptions = pRemoteDescription->pMediaDescriptions;
    for (u32 i = 0; i < pMediaDescriptions->API->GetCount(pMediaDescriptions); ++i) {
        LTRtcMediaDescription *pRemoteMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, i, NULL);
        LTRtcRtpTransceiver *pTransceiver = GetRtpTransceiver(peerConn, i, pRemoteMediaDescription);
        if (!pTransceiver) {
            LTLOG_DEBUG("no.xcvr", "Couldn't find transceiver with m-line index %lu and media id %s", LT_Pu32(i), pRemoteMediaDescription->mediaId);
            return false;
        }
        if (pTransceiver->payloadFormat.mediaFormat.nEncoding == kLTMediaEncoding_Unknown) {
            return false;
        }
    }

    return true;
}

static void
UpdateIfTransceiverInvalid(LTRtcSessionDescription *pRemoteDescription, LTRtcRtpTransceiver *pTransceiver) {
    LTArray *pMediaDescriptions = pRemoteDescription->pMediaDescriptions;
    for (u32 i = 0; i < pMediaDescriptions->API->GetCount(pMediaDescriptions); ++i) {
        LTRtcMediaDescription *pRemoteMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, i, NULL);
        if (lt_strcmp(pTransceiver->mediaId, pRemoteMediaDescription->mediaId) == 0)
            return;
    }
    /* Media Id not found in SDP so make it inactive. */
    pTransceiver->nMediaFlow = kLTRtcMediaFlow_Inactive;
}

static void
LTRtcPeerConnectionImpl_SetRemoteDescription(LTRtcPeerConnectionImpl *peerConn, const char *pRemoteSDP) {
    LTRtcSessionDescription *pRemoteDescription = LTRtcSessionDescription_Parse(pRemoteSDP);
    if (!pRemoteDescription) {
        LTLOG_YELLOWALERT("rmt.sdp.parse.err", "Error parsing remote description SDP");
        return;
    }

    if (!HandleRemoteDescription(peerConn, pRemoteDescription)) {
        LTLOG_YELLOWALERT("loc.sdp.err", "Error creating local description SDP");
        goto done;
    }

    if (peerConn->nIceConnectionState != kLTRtcIceConnectionState_Completed) {
        lt_memcpy(peerConn->peerFingerprint, pRemoteDescription->fingerprint, kLTIceCandidate_FingerprintLen);

        peerConn->iceAgent->API->SetRemoteCredentials(peerConn->iceAgent,
                                                       pRemoteDescription->iceUserFragment,
                                                       pRemoteDescription->icePassword);
        LTArray *pCandidates = pRemoteDescription->pCandidates;
        for (u32 i = 0; i < pCandidates->API->GetCount(pCandidates); ++i) {
            LTRtcCandidateDescription *pCandidateDesc = pCandidates->API->Get(pCandidates, i, NULL);
            if (pCandidateDesc->nTransport == kLTIceCandidateTransport_UDP) {
                LTIceCandidate *candidate = lt_createobject(LTIceCandidate);
                candidate->API->Init(candidate, peerConn->iceAgent, pCandidateDesc->nTransport, pCandidateDesc->nCandidateType, pCandidateDesc->nPriority, pCandidateDesc->endpoint, pCandidateDesc->foundation);
                peerConn->iceAgent->API->AddRemoteCandidate(peerConn->iceAgent, candidate);
            }
        }
        peerConn->iceAgent->API->StartCandidateGathering(peerConn->iceAgent);
    }
    /* Renegotiation request */
    else {
        LTArray *pMediaDescriptions = pRemoteDescription->pMediaDescriptions;
        u32 nNumMediaDescriptions = pMediaDescriptions->API->GetCount(pMediaDescriptions);
        u32 nNumTransceivers      = peerConn->pTransceivers->API->GetCount(peerConn->pTransceivers);
        bool mediaTrackRemoved    = false;
        if (nNumMediaDescriptions < nNumTransceivers) {
            LTLOG("reneg.mdesc.del", "nMediaDescps %lu nTransceivers %lu", LT_Pu32(nNumMediaDescriptions), LT_Pu32(nNumTransceivers));
            /* Peer has removed a media track */
            mediaTrackRemoved = true;
        }
        for (u32 i = 0; i < nNumTransceivers; ++i) {
            LTRtcRtpTransceiver *pTransceiver = peerConn->pTransceivers->API->Get(peerConn->pTransceivers, i, NULL);
            if (mediaTrackRemoved) UpdateIfTransceiverInvalid(pRemoteDescription, pTransceiver);
            if (pTransceiver->payloadFormat.mediaFormat.nEncoding != kLTMediaEncoding_Unknown) {
                if (!ConfigureTransceiver(peerConn, pTransceiver)) {
                    LTLOG_YELLOWALERT("strm.reconfig.err", "Failed to reconfigure RTP stream");
                    goto done;
                }
            }
        }
        s_pEvent->NotifyEvent(peerConn->hEvent, peerConn, kLTRtcPeerConnectionEvent_ReNegotiated);
    }
done:
    LTRtcSessionDescription_Destroy(pRemoteDescription);
}

static void
LTRtcPeerConnectionImpl_AddIceCandidate(LTRtcPeerConnectionImpl *peerConn, const char *pRemoteTrickleIceCandidate) {
    if (!(peerConn && pRemoteTrickleIceCandidate)) return;
    LTRtcSessionDescription *pRemoteDescription = LTRtcSessionDescription_Parse(pRemoteTrickleIceCandidate);
    if (!pRemoteDescription) {
        LTLOG_YELLOWALERT("rmt.ice.parse.err", "Error parsing remote ICE description");
        return;
    }

    do {
        LTArray *pCandidates = pRemoteDescription->pCandidates;
        if (!pCandidates) {
            LTLOG_YELLOWALERT("rmt.ice.err", "Failed to get ICE Candidates");
            break;
        }
        for (u32 i = 0; i < pCandidates->API->GetCount(pCandidates); ++i) {
            LTRtcCandidateDescription *pCandidateDesc = pCandidates->API->Get(pCandidates, i, NULL);
            if (pCandidateDesc->nTransport == kLTIceCandidateTransport_UDP) {
                LTIceCandidate *candidate = lt_createobject(LTIceCandidate);
                candidate->API->Init(candidate,
                                    peerConn->iceAgent,
                                    pCandidateDesc->nTransport,
                                    pCandidateDesc->nCandidateType,
                                    pCandidateDesc->nPriority,
                                    pCandidateDesc->endpoint,
                                    pCandidateDesc->foundation);
                peerConn->iceAgent->API->AddRemoteCandidate(peerConn->iceAgent, candidate);
            }
        }
        peerConn->iceAgent->API->StartIceConnectionCheck(peerConn->iceAgent);
    } while (false);
    LTRtcSessionDescription_Destroy(pRemoteDescription);
}

static const char *
LTRtcPeerConnectionImpl_GetLocalIceCandidate(LTRtcPeerConnectionImpl *peerConn) {
    if (!peerConn) return NULL;
    LTString pIceCandidate = NULL;
    LTArray *pCandidates = peerConn->iceAgent->API->GetLocalCandidates(peerConn->iceAgent);
    if (!pCandidates) {
        LTLOG_YELLOWALERT("local.get.ice.err", "Failed to get candidates");
        return NULL;
    }
    u8 localCandidateCount = pCandidates->API->GetCount(pCandidates);
    for (u8 i = 0; i < localCandidateCount; ++i) {
        LTIceCandidate *pCandidate = pCandidates->API->Get(pCandidates, i, NULL);

        if (pCandidate->API->GetCandidateStatus(pCandidate) == kLTIceCandidateStatus_ToBeSent) {
            LTRtcSessionDescription *pLocalDescription = LTRtcSessionDescription_Create();
            LTRtcCandidateDescription *pCandidateDesc = LTRtcSessionDescription_AddCandidate(pLocalDescription);
            if (!pCandidateDesc) {
                LTLOG_YELLOWALERT("local.ice.err", "Failed to add ice candidate in session description");
                LTRtcSessionDescription_Destroy(pLocalDescription);
                return NULL;
            }
            pCandidateDesc->nComponentId   = kLTRtcComponentId_RTP;
            pCandidateDesc->nTransport     = pCandidate->API->GetTransport(pCandidate);
            pCandidateDesc->nCandidateType = pCandidate->API->GetType(pCandidate);
            pCandidateDesc->nPriority      = pCandidate->API->GetPriority(pCandidate);
            pCandidateDesc->endpoint       = pCandidate->API->GetEndpoint(pCandidate);
            lt_strncpyTerm(pCandidateDesc->foundation, pCandidate->API->GetFoundation(pCandidate), sizeof(pCandidateDesc->foundation));
            ltstring_appendformat(&pIceCandidate, "candidate:%s %lu %s %lu %d.%d.%d.%d %lu typ %s\n",
                pCandidateDesc->foundation,
                pCandidateDesc->nComponentId,
                "udp",
                pCandidateDesc->nPriority,
                IP_OCTETS(pCandidateDesc->endpoint.address),
                pCandidateDesc->endpoint.port,
                LTIceCandidateTypeToString(pCandidateDesc->nCandidateType));

            pCandidate->API->SetCandidateStatus(pCandidate, kLTIceCandidateStatus_Sent);
            LTRtcSessionDescription_Destroy(pLocalDescription);
            break;
        }
    }
    return (const char*)pIceCandidate;
}

static bool
GetLocalFingerprint(u8 fingerprint[kLTIceCandidate_FingerprintLen]) {
    LTDriverCryptoCertManager *certManager = lt_createobject(LTDriverCryptoCertManager);
    if (!certManager) return false;
    u16 certLen = 0;
    u8 *certData = NULL;
    bool bRet = false;
    do {
        certManager->API->GetCertificate("comm", NULL, &certLen);
        if (certLen == 0) break;
        certData = lt_malloc(certLen);
        if (!certData) break;
        if (!certManager->API->GetCertificate("comm", certData, &certLen)) break;
        certLen = (certData[1] << 8) | (certData[2]);
        bRet = (kLTSystemCrypto_Result_Ok == LT_GetLTSystemCrypto()->GenDigestSHA256(certData + 3, certLen, fingerprint));
    } while (false);
    lt_destroyobject(certManager);
    lt_free(certData);
    return bRet;
}

static const char *
LTRtcPeerConnectionImpl_GetLocalDescription(LTRtcPeerConnectionImpl *peerConn) {
    LTRtcSessionDescription *pLocalDescription = LTRtcSessionDescription_Create();
    pLocalDescription->nSessionId          = peerConn->nSessionId;
    pLocalDescription->nSessionVersion     = ++peerConn->nSessionVersion;
    lt_strncpyTerm(pLocalDescription->sessionName, peerConn->sessionId, sizeof(pLocalDescription->sessionName));
    pLocalDescription->nSetupMode          = kLTRtcSetupMode_Active;
    pLocalDescription->iceOptions          = kLTIceOptions_Renomination;
    peerConn->iceAgent->API->GetLocalCredentials(peerConn->iceAgent,
        pLocalDescription->iceUserFragment, sizeof(pLocalDescription->iceUserFragment),
        pLocalDescription->icePassword,     sizeof(pLocalDescription->icePassword));

    if (!GetLocalFingerprint(pLocalDescription->fingerprint)) {
        LTLOG_YELLOWALERT("loc.fp.err", "Error creating local fingerprint");
    }
    /* Set dummy address/port per RFC8829 section 5.3.1 if no ICE candidates have been gathered. */
    LTNetIpv4Endpoint defaultEndpoint = { .address = 0, .port = 9 };
    LTArray *pCandidates = peerConn->iceAgent->API->GetLocalCandidates(peerConn->iceAgent);
    if (pCandidates->API->GetCount(pCandidates) > 0) {
        LTIceCandidate *pCandidate = pCandidates->API->Get(pCandidates, 0, NULL);
        defaultEndpoint = pCandidate->API->GetEndpoint(pCandidate);
    }

    for (u32 j = 0; j < pCandidates->API->GetCount(pCandidates); ++j) {
        LTIceCandidate *pCandidate = pCandidates->API->Get(pCandidates, j, NULL);
        LTRtcCandidateDescription *pCandidateDesc = LTRtcSessionDescription_AddCandidate(pLocalDescription);
        pCandidateDesc->nComponentId   = kLTRtcComponentId_RTP;
        pCandidateDesc->nTransport     = pCandidate->API->GetTransport(pCandidate);
        pCandidateDesc->nCandidateType = pCandidate->API->GetType(pCandidate);
        pCandidateDesc->nPriority      = pCandidate->API->GetPriority(pCandidate);
        pCandidateDesc->endpoint       = pCandidate->API->GetEndpoint(pCandidate);
        lt_strncpyTerm(pCandidateDesc->foundation, pCandidate->API->GetFoundation(pCandidate), sizeof(pCandidateDesc->foundation));
    }

    for (u32 i = 0; i < peerConn->pTransceivers->API->GetCount(peerConn->pTransceivers); ++i) {
        LTRtcRtpTransceiver *pTransceiver = peerConn->pTransceivers->API->Get(peerConn->pTransceivers, i, NULL);
        LTRtcMediaDescription *pMediaDescription = LTRtcSessionDescription_AddMediaDescription(pLocalDescription);
        pMediaDescription->nKind              = pTransceiver->payloadFormat.mediaFormat.nKind;
        pMediaDescription->bRtcpMux           = true;
        pMediaDescription->nMediaFlow         = kLTRtcMediaFlow_Inactive;
        pMediaDescription->nConnectionAddress = defaultEndpoint.address;
        pMediaDescription->nPort              = defaultEndpoint.port;
        pMediaDescription->nMediaFlow         = pTransceiver->nMediaFlow;
        pMediaDescription->extMap             = pTransceiver->extMap;
        lt_strncpyTerm(pMediaDescription->mediaId, pTransceiver->mediaId, kMediaIdLen);

        if (pTransceiver->nMediaFlow & kLTRtcMediaFlow_Send) {
            lt_strncpyTerm(pMediaDescription->canonicalName, peerConn->canonicalName, kCanonicalNameLen);
            lt_strncpyTerm(pMediaDescription->mediaStreamId, peerConn->canonicalName, kMediaStreamIdLen);
            lt_strncpyTerm(pMediaDescription->mediaStreamTrackId, pTransceiver->mediaStreamTrackId, kMediaStreamTrackIdLen);
            pMediaDescription->nSyncSource = pTransceiver->nSendSyncSource;
        }
        LTRtpPayloadFormat *pLocalPayloadFormat = LTRtcSessionDescription_AddPayloadFormat(pMediaDescription);
        *pLocalPayloadFormat = pTransceiver->payloadFormat;
    }

    const char *pLocalSDP = LTRtcSessionDescription_ToString(pLocalDescription);
    LTRtcSessionDescription_Destroy(pLocalDescription);

    return pLocalSDP;
}

static void
LTRtcPeerConnectionImpl_AddIceServer(LTRtcPeerConnectionImpl *peerConn, const char *serverSpec) {
    peerConn->iceAgent->API->AddIceServer(peerConn->iceAgent, serverSpec);
    peerConn->nIceConnectionState = kLTRtcIceConnectionState_Checking;
}

static bool
LTRtcPeerConnectionImpl_Init(LTRtcPeerConnectionImpl *peerConn, const char *sessionId) {
    do {
        if (!(peerConn->sessionId = lt_strdup(sessionId)))                 break;
        if (!(peerConn->srtpSession = lt_createobject(LTSrtpSession)))     break;
        if (!(peerConn->iceAgent = lt_createobject(LTIceAgent)))           break;
        if (!peerConn->iceAgent->API->Init(peerConn->iceAgent, sessionId)) break;
        peerConn->iceAgent->API->OnIceEvent(peerConn->iceAgent, OnIceEvent, NULL, peerConn);

        return true;
    } while(false);

    lt_free(peerConn->sessionId);
    peerConn->sessionId = NULL;

    lt_destroyobject(peerConn->iceAgent);
    peerConn->iceAgent = NULL;

    lt_destroyobject(peerConn->srtpSession);
    peerConn->srtpSession = NULL;

    return false;
}

static void OnNetStatus(LTNetMonitor_Status status, void *clientData) {
    LTRtcPeerConnectionImpl *peerConn = clientData;
    if (!peerConn || !peerConn->srtpSession) return;

    if (status == kLTNetMonitor_Status_NetworkUp) {
        if (peerConn->bPaused) {
            LTLOG("resume", "network up");
            peerConn->srtpSession->API->Resume(peerConn->srtpSession);
            peerConn->bPaused = false;
        }
    } else if (status == kLTNetMonitor_Status_NetworkDown) {
        if (!peerConn->bPaused) {
            LTLOG("pause", "network down");
            peerConn->srtpSession->API->Pause(peerConn->srtpSession);
            peerConn->bPaused = true;
        }
    }
}

static bool
LTRtcPeerConnectionImpl_ConstructObject(LTRtcPeerConnectionImpl *peerConn) {
    do {
        peerConn->sessionStartTime    = LT_GetCore()->GetKernelTime();
        peerConn->hEvent              = LT_GetCore()->CreateEvent(&s_ConnectionEventArgs, DispatchConnectionEventCB, NULL, NULL, NULL);
        peerConn->nSessionId          = LT_GetCore()->GetClockTimeUTC().nNanoseconds;
        peerConn->nSessionVersion     = 0;
        peerConn->nIceConnectionState = kLTRtcIceConnectionState_New;
        peerConn->bPaused             = false;
        peerConn->pTransceivers       = LTArray_CreateStructArray(sizeof(LTRtcRtpTransceiver));
        if (!peerConn->pTransceivers) break;
        LT_GetLTUtilityByteOps()->GenRandomBytesAsHexString(sizeof(peerConn->canonicalName)/2, peerConn->canonicalName, sizeof(peerConn->canonicalName));
        LT_GetLTUtilityByteOps()->GenRandomBytes((u8*)&peerConn->nRoleTieBreaker, sizeof(u64));

        peerConn->hConnTimeoutThread = s_pThread->GetCurrentThread();
        s_pThread->SetTimer(peerConn->hConnTimeoutThread, LTTime_Seconds(kWebrtcConnectionTimeoutSec), OnConnectionTimeout, NULL, peerConn);

        s_pNetMonitor->OnStatusChange(OnNetStatus, NULL, peerConn);
        return true;
    } while(false);

    return false;
}

static void
LogSessionSummary(LTRtcPeerConnectionImpl *peerConn) {
    LTTime now = LT_GetCore()->GetKernelTime();
    LTTime sessionDuration = LTTime_Subtract(now, peerConn->sessionStartTime);
    LTTime streamDuration = LTTime_Zero();
    if (!LTTime_IsZero(peerConn->setupDuration)) {
        streamDuration = LTTime_Subtract(sessionDuration, peerConn->setupDuration);
    }

    LTLOG_SERVER("sum", "sid=%s session_dur(ms)=%lld setup_dur(ms)=%lld stream_dur(ms)=%lld offers=%lu",
        peerConn->sessionId,
        LT_Ps64(LTTime_GetMilliseconds(sessionDuration)),
        LT_Ps64(LTTime_GetMilliseconds(peerConn->setupDuration)),
        LT_Ps64(LTTime_GetMilliseconds(streamDuration)),
        LT_Pu32(peerConn->nSessionVersion));
}

static void
LTRtcPeerConnectionImpl_DestructObject(LTRtcPeerConnectionImpl *peerConn) {
    LogSessionSummary(peerConn);
    s_pThread->KillTimer(peerConn->hConnTimeoutThread, OnConnectionTimeout, peerConn);
    if (peerConn->srtpSession)  lt_destroyobject(peerConn->srtpSession);
    if (peerConn->hDtlsSocket)  lt_destroyhandle(peerConn->hDtlsSocket);
    if (peerConn->iceAgent)     lt_destroyobject(peerConn->iceAgent);
    if (peerConn->hEvent)       lt_destroyhandle(peerConn->hEvent);
    lt_destroyobject(peerConn->pTransceivers);
    lt_free(peerConn->sessionId);
    s_pNetMonitor->NoStatusChange(OnNetStatus);
}

/*_____________________________________________
  LTRtcPeerConnection LTObjectApi definition */
define_LTObjectImplPublic(LTRtcPeerConnection, LTRtcPeerConnectionImpl,
    GetConnectionState,
    OnConnectionEvent,
    NoConnectionEvent,
    AddIceCandidate,
    SetRemoteDescription,
    GetLocalDescription,
    GetLocalIceCandidate,
    AddIceServer,
    Init,
);

/*___________________________________________
 / LTNetWebRtc library initialization */
static bool
LTNetWebRtcImpl_LibInit(void) {
    do {
        if (!LTRtcSessionDescription_LibInit()) break;
        if (!(s_pNetMonitor = lt_openlibrary(LTNetMonitor))) break;
        s_pThread      = lt_getlibraryinterface(ILTThread, LT_GetCore());
        s_pEvent       = lt_getlibraryinterface(ILTEvent,  LT_GetCore());
        return true;
    } while(0);
    return false;
}

static void
LTNetWebRtcImpl_LibFini(void) {
    LTRtcSessionDescription_LibFini();
    lt_closelibrary(s_pNetMonitor);
    s_pNetMonitor = NULL;
    s_pThread = NULL;
    s_pEvent = NULL;
}

/*______________________________________________
 / LTNetWebRtc library root interface binding */
/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetWebRtc, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTNetWebRtc, ) LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTNetWebRtc,
    (LTRtcPeerConnectionImpl)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
