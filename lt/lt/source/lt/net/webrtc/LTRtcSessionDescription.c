/*******************************************************************************
 * source/lt/net/webrtc/LTRtcSessionDescription.c - WebRTC Session Description Protocol (SDP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTRtcSessionDescription.h"
#include <lt/utility/tokenizer/LTUtilityTokenizer.h>

DEFINE_LTLOG_SECTION("sdp");

typedef enum {
    kLTSdpParserState_FieldName,

    kLTSdpParserState_S_SessionName,

    kLTSdpParserState_A_AttributeName,
    kLTSdpParserState_A_Candidate_Foundation,
    kLTSdpParserState_A_Candidate_ComponentId,
    kLTSdpParserState_A_Candidate_Transport,
    kLTSdpParserState_A_Candidate_Priority,
    kLTSdpParserState_A_Candidate_Address,
    kLTSdpParserState_A_Candidate_Port,
    kLTSdpParserState_A_Candidate_TypeLiteral,
    kLTSdpParserState_A_Candidate_CandidateType,
    kLTSdpParserState_A_Candidate_RaddrLiteral,
    kLTSdpParserState_A_Candidate_RelAddr,
    kLTSdpParserState_A_Candidate_RportLiteral,
    kLTSdpParserState_A_Candidate_RelPort,
    kLTSdpParserState_A_IceUserFragment,
    kLTSdpParserState_A_IcePassword,
    kLTSdpParserState_A_IceOption,
    kLTSdpParserState_A_Fingerprint_Algo,
    kLTSdpParserState_A_Fingerprint_Byte,
    kLTSdpParserState_A_Setup,
    kLTSdpParserState_A_MediaId,
    kLTSdpParserState_A_RtpMap_PayloadType,
    kLTSdpParserState_A_RtpMap_Encoding,
    kLTSdpParserState_A_RtpMap_ClockRate,
    kLTSdpParserState_A_RtpMap_NumAudioChannels,
    kLTSdpParserState_A_RtcpFeedback_PayloadType,
    kLTSdpParserState_A_RtcpFeedback_Value,
    kLTSdpParserState_A_FormatParams_PayloadType,
    kLTSdpParserState_A_FormatParams_ParamName,
    kLTSdpParserState_A_FormatParams_ParamValue,
    kLTSdpParserState_A_Group_Semantics,
    kLTSdpParserState_A_Group_Mid,
    kLTSdpParserState_A_SyncSource_Value,
    kLTSdpParserState_A_MediaStreamId_Identifier,
    kLTSdpParserState_A_MediaStreamId_AppData,
    kLTSdpParserState_A_ExtensionMap_Value,
    kLTSdpParserState_A_ExtensionMap_Direction,
    kLTSdpParserState_A_ExtensionMap_Uri,
    kLTSdpParserState_A_ImageAttr_PayloadType,
    kLTSdpParserState_A_ImageAttr_Direction,
    kLTSdpParserState_A_ImageAttr_Resolution,
    kLTSdpParserState_A_ImageAttr_Width,
    kLTSdpParserState_A_ImageAttr_Height,

    kLTSdpParserState_M_Kind,
    kLTSdpParserState_M_Port,
    kLTSdpParserState_M_Protocol,
    kLTSdpParserState_M_Format,

    kLTSdpParserState_C_NetworkType,
    kLTSdpParserState_C_AddrType,
    kLTSdpParserState_C_ConnectionAddress,

    kLTSdpParserState_Ignore,
} LTSdpParserState;

typedef struct {
    LTSdpParserState            state;
    u32                         nItemIdx;
    char                       *pCurParamName;
    LTRtcSessionDescription    *pSessionDescription;
    LTRtcMediaDescription      *pCurMediaDescription;
    LTRtpPayloadFormat         *pCurPayloadFormat;
    LTRtcCandidateDescription  *pCurCandidate;
    u32                         nCurExtMapValue;
} LTRtcSessionDescriptionParser;

static LTUtilityTokenizer   *s_pTokenizerLib = NULL;

#define SkipRestOfLine() do { pParser->state = kLTSdpParserState_Ignore; s_pTokenizerLib->SetSentinels(pTokenizer, "\n"); } while(0)

static bool
StringToIPAddress(const char *str, u8 *ip) {
    int  n             = 0;
    int  len           = lt_strlen(str);
    bool octet_started = false;
    ip[0]              = 0;
    ip[1]              = 0;
    ip[2]              = 0;
    ip[3]              = 0;
    for (int i = 0; i < len && n < 4; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            octet_started = true;
            ip[n] *= 10;
            ip[n] += (c - '0');
        } else if (c == '.') {
            if (!octet_started) {
                return false;
            }
            octet_started = false;
            n++;
        } else {
            return false;
        }
    }
    if (n == 3 && octet_started) return true;
    return false;
}

static LTRtpPayloadFormat *
GetOrCreatePayloadFormatByPayloadType(LTRtcSessionDescriptionParser *pParser, const char *pPayloadType) {
    LTRtpPayloadFormat *pFormat;
    u32 nPayloadType = lt_strtou32(pPayloadType, NULL, 0);
    LTArray *pPayloadFormats = pParser->pCurMediaDescription->pPayloadFormats;
    for (u32 i = 0; i < pPayloadFormats->API->GetCount(pPayloadFormats); ++i) {
        pFormat = pPayloadFormats->API->Get(pPayloadFormats, i, NULL);
        if (pFormat->nPayloadType == nPayloadType) return pFormat;
    }
    pFormat = LTRtcSessionDescription_AddPayloadFormat(pParser->pCurMediaDescription);
    pFormat->nPayloadType = nPayloadType;
    pFormat->mediaFormat.nKind = pParser->pCurMediaDescription->nKind;
    return pFormat;
}

static bool
ProcessSDPToken(LTTokenizer *pTokenizer, const char *pToken, char nMatchedSentinel, void *pClientData) {
    LTRtcSessionDescriptionParser *pParser = pClientData;
    bool bContinue = true;
    switch (pParser->state) {
        case kLTSdpParserState_FieldName:
            if (lt_strlen(pToken) != 1) {
                LTLOG_YELLOWALERT("bad.fieldname", "Invalid fieldname: %s", pToken);
                pParser->state = kLTSdpParserState_Ignore;
                s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
            } else {
                char field = pToken[0];
                if (field == 's') {
                    pParser->state = kLTSdpParserState_S_SessionName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                } else if (field == 'a') {
                    pParser->state = kLTSdpParserState_A_AttributeName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, ":\n");
                } else if (field == 'm') {
                    pParser->pCurMediaDescription = LTRtcSessionDescription_AddMediaDescription(pParser->pSessionDescription);
                    pParser->state = kLTSdpParserState_M_Kind;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (field == 'c') {
                    pParser->state = kLTSdpParserState_C_NetworkType;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else {
                    pParser->state = kLTSdpParserState_Ignore;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                }
            }
            break;

        case kLTSdpParserState_S_SessionName:
            lt_strncpyTerm(pParser->pSessionDescription->sessionName, pToken, sizeof(pParser->pSessionDescription->sessionName));
            SkipRestOfLine();
            break;

        case kLTSdpParserState_A_AttributeName:;
            // Allow these attributes only at session-level or in the first media section
            LTArray *pMediaDescriptions = pParser->pSessionDescription->pMediaDescriptions;
            if (pMediaDescriptions->API->GetCount(pMediaDescriptions) <= 1) {
                if (lt_strcmp(pToken, "candidate") == 0) {
                    pParser->pCurCandidate = LTRtcSessionDescription_AddCandidate(pParser->pSessionDescription);
                    pParser->state = kLTSdpParserState_A_Candidate_Foundation;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "ice-ufrag") == 0) {
                    pParser->state = kLTSdpParserState_A_IceUserFragment;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                } else if (lt_strcmp(pToken, "ice-pwd") == 0) {
                    pParser->state = kLTSdpParserState_A_IcePassword;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                } else if (lt_strcmp(pToken, "ice-options") == 0) {
                    pParser->state = kLTSdpParserState_A_IceOption;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                } else if (lt_strcmp(pToken, "fingerprint") == 0) {
                    pParser->state = kLTSdpParserState_A_Fingerprint_Algo;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "setup") == 0) {
                    pParser->state = kLTSdpParserState_A_Setup;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                }
                if (pParser->state != kLTSdpParserState_A_AttributeName) break;
            }
            if (pParser->pCurMediaDescription) {
                if (lt_strcmp(pToken, "mid") == 0) {
                    pParser->state = kLTSdpParserState_A_MediaId;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "msid") == 0) {
                    pParser->state = kLTSdpParserState_A_MediaStreamId_Identifier;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "recvonly") == 0) {
                    pParser->pCurMediaDescription->nMediaFlow = kLTRtcMediaFlow_Receive;
                    pParser->state = kLTSdpParserState_FieldName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
                } else if (lt_strcmp(pToken, "sendonly") == 0) {
                    pParser->pCurMediaDescription->nMediaFlow = kLTRtcMediaFlow_Send;
                    pParser->state = kLTSdpParserState_FieldName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
                } else if (lt_strcmp(pToken, "sendrecv") == 0) {
                    pParser->pCurMediaDescription->nMediaFlow = kLTRtcMediaFlow_SendReceive;
                    pParser->state = kLTSdpParserState_FieldName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
                } else if (lt_strcmp(pToken, "inactive") == 0) {
                    pParser->pCurMediaDescription->nMediaFlow = kLTRtcMediaFlow_Inactive;
                    pParser->state = kLTSdpParserState_FieldName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
                } else if (lt_strcmp(pToken, "rtcp-mux") == 0) {
                    pParser->pCurMediaDescription->bRtcpMux = true;
                    pParser->state = kLTSdpParserState_FieldName;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
                } else if (lt_strcmp(pToken, "rtpmap") == 0) {
                    pParser->state = kLTSdpParserState_A_RtpMap_PayloadType;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "rtcp-fb") == 0) {
                    pParser->state = kLTSdpParserState_A_RtcpFeedback_PayloadType;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "fmtp") == 0) {
                    pParser->state = kLTSdpParserState_A_FormatParams_PayloadType;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "group") == 0) {
                    pParser->state = kLTSdpParserState_A_Group_Semantics;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "ssrc") == 0) {
                    pParser->state = kLTSdpParserState_A_SyncSource_Value;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else if (lt_strcmp(pToken, "extmap") == 0) {
                    pParser->state = kLTSdpParserState_A_ExtensionMap_Value;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "/ \n");
                } else if (lt_strcmp(pToken, "imageattr") == 0) {
                    pParser->state = kLTSdpParserState_A_ImageAttr_PayloadType;
                    s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
                } else {
                    pParser->state = kLTSdpParserState_Ignore;
                    s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
                }
            }
            break;

        // m=<kind> <port> UDP/TLS/RTP/SAVPF <format1> <format2> ...
        case kLTSdpParserState_M_Kind:
            pParser->state = kLTSdpParserState_M_Port;
            s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            if (lt_strcmp(pToken, "audio") == 0) {
                pParser->pCurMediaDescription->nKind = kLTMediaKind_Audio;
            } else if (lt_strcmp(pToken, "video") == 0) {
                pParser->pCurMediaDescription->nKind = kLTMediaKind_Video_SD;
            } else {
                pParser->pCurMediaDescription->nKind = kLTMediaKind_Unknown;
                LTLOG_DEBUG("bad.m.kind", "Unsupported media type: %s", pToken);
                SkipRestOfLine();
            }
            break;
        case kLTSdpParserState_M_Port:
            pParser->pCurMediaDescription->nPort = lt_strtou32(pToken, NULL, 0);
            pParser->state = kLTSdpParserState_M_Protocol;
            s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            break;
        case kLTSdpParserState_M_Protocol:
            if (lt_strcmp(pToken, "UDP/TLS/RTP/SAVPF") != 0) {
                LTLOG_DEBUG("bad.m.proto", "Unsupported media protocol: %s", pToken);
                SkipRestOfLine();
            } else {
                pParser->state = kLTSdpParserState_M_Format;
                s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            }
            break;
        case kLTSdpParserState_M_Format:
            // Ignore these. We will get them from the rtpmap attributes.
            break;

        // c=IN IP4 <ip-addr>
        case kLTSdpParserState_C_NetworkType:
            if (lt_strcmp(pToken, "IN") != 0) {
                LTLOG_DEBUG("bad.nettype", "Unsupported network type: %s", pToken);
                pParser->state = kLTSdpParserState_Ignore;
                s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
            } else {
                pParser->state = kLTSdpParserState_C_AddrType;
                s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            }
            break;
        case kLTSdpParserState_C_AddrType:
            if (lt_strcmp(pToken, "IP4") != 0) {
                LTLOG_DEBUG("bad.addrtype", "Unsupported address type: %s", pToken);
                pParser->state = kLTSdpParserState_Ignore;
                s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
            } else {
                pParser->state = kLTSdpParserState_C_ConnectionAddress;
                s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
            }
            break;
        case kLTSdpParserState_C_ConnectionAddress: {
            u32 *pDstIp;
            if (pParser->pCurMediaDescription) {
                pDstIp = &pParser->pCurMediaDescription->nConnectionAddress;
            } else {
                pDstIp = &pParser->pSessionDescription->nConnectionAddress;
            }
            if (!StringToIPAddress(pToken, (u8*)pDstIp)) {
                LTLOG_DEBUG("bad.c.addr", "Error parsing conn address: %s", pToken);
                SkipRestOfLine();
            }
            pParser->state = kLTSdpParserState_FieldName;
            s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
            break;
        }

        // a=candidate:<foundation> <component-id> <transport> <priority> <addr> <port> typ <can-type> raddr <rel-addr> rport <rel-port> ...
        case kLTSdpParserState_A_Candidate_Foundation:
            lt_strncpyTerm(pParser->pCurCandidate->foundation, pToken, sizeof(pParser->pCurCandidate->foundation));
            pParser->state = kLTSdpParserState_A_Candidate_ComponentId;
            break;
        case kLTSdpParserState_A_Candidate_ComponentId:
            pParser->pCurCandidate->nComponentId = lt_strtou32(pToken, NULL, 0);
            pParser->state = kLTSdpParserState_A_Candidate_Transport;
            break;
        case kLTSdpParserState_A_Candidate_Transport:
            if (lt_strcasecmp(pToken, "udp") == 0) {
                pParser->pCurCandidate->nTransport = kLTIceCandidateTransport_UDP;
                pParser->state = kLTSdpParserState_A_Candidate_Priority;
            } else if (lt_strcasecmp(pToken, "tcp") == 0) {
                pParser->pCurCandidate->nTransport = kLTIceCandidateTransport_TCP;
                pParser->state = kLTSdpParserState_A_Candidate_Priority;
            }else {
                pParser->pCurCandidate->nTransport = kLTIceCandidateTransport_Unknown;
                LTLOG_DEBUG("bad.candidate.transport", "Unsupported candidate transport: %s", pToken);
                SkipRestOfLine();
            }
            break;
        case kLTSdpParserState_A_Candidate_Priority:
            pParser->pCurCandidate->nPriority = lt_strtou32(pToken, NULL, 0);
            pParser->state = kLTSdpParserState_A_Candidate_Address;
            break;
        case kLTSdpParserState_A_Candidate_Address:
            if (!StringToIPAddress(pToken, (u8*)&pParser->pCurCandidate->endpoint.address)) {
                LTLOG_DEBUG("bad.canaddr", "Error parsing candidate address: %s", pToken);
                pParser->pCurCandidate = NULL;
                LTArray *pCandidates = pParser->pSessionDescription->pCandidates;
                u32 nNumCandidates = pCandidates->API->GetCount(pCandidates);
                pCandidates->API->Remove(pCandidates, nNumCandidates - 1);
                SkipRestOfLine();
            } else {
                pParser->state = kLTSdpParserState_A_Candidate_Port;
            }
            break;
        case kLTSdpParserState_A_Candidate_Port:
            pParser->pCurCandidate->endpoint.port = lt_strtou32(pToken, NULL, 0);
            pParser->state = kLTSdpParserState_A_Candidate_TypeLiteral;
            break;
        case kLTSdpParserState_A_Candidate_TypeLiteral:
            if (lt_strcmp(pToken, "typ") != 0) {
                LTLOG_DEBUG("bad.candidate.type", "Expected `typ` literal, got: %s", pToken);
                SkipRestOfLine();
            } else {
                pParser->state = kLTSdpParserState_A_Candidate_CandidateType;
            }
            break;
        case kLTSdpParserState_A_Candidate_CandidateType:
            pParser->state = kLTSdpParserState_A_Candidate_RaddrLiteral;
            if (lt_strcmp(pToken, "host") == 0) {
                pParser->pCurCandidate->nCandidateType = kLTIceCandidateType_Host;
            } else if (lt_strcmp(pToken, "srflx") == 0) {
                pParser->pCurCandidate->nCandidateType = kLTIceCandidateType_ServerReflexive;
            } else if (lt_strcmp(pToken, "prflx") == 0) {
                pParser->pCurCandidate->nCandidateType = kLTIceCandidateType_PeerReflexive;
            } else if (lt_strcmp(pToken, "relay") == 0) {
                pParser->pCurCandidate->nCandidateType = kLTIceCandidateType_Relay;
            } else {
                LTLOG_DEBUG("bad.candidate.type", "Unsupported candidate type: %s", pToken);
                SkipRestOfLine();
            }
            break;
        case kLTSdpParserState_A_Candidate_RaddrLiteral:
            if (lt_strcmp(pToken, "raddr") != 0) {
                LTLOG_DEBUG("bad.candidate.raddr", "Expected `raddr` literal, got: %s", pToken);
                SkipRestOfLine();
            } else {
                pParser->state = kLTSdpParserState_A_Candidate_RelAddr;
            }
            break;
        case kLTSdpParserState_A_Candidate_RelAddr:
            if (!StringToIPAddress(pToken, (u8*)&pParser->pCurCandidate->nRelAddress)) {
                LTLOG_DEBUG("bad.raddr", "Error parsing raddr: %s", pToken);
                SkipRestOfLine();
            }
            pParser->state = kLTSdpParserState_A_Candidate_RportLiteral;
            break;
        case kLTSdpParserState_A_Candidate_RportLiteral:
            if (lt_strcmp(pToken, "rport") != 0) {
                LTLOG_DEBUG("bad.candidate.rport", "Expected `rport` literal, got: %s", pToken);
                SkipRestOfLine();
            } else {
                pParser->state = kLTSdpParserState_A_Candidate_RelPort;
            }
            break;
        case kLTSdpParserState_A_Candidate_RelPort:
            pParser->pCurCandidate->nRelPort = lt_strtou32(pToken, NULL, 0);
            SkipRestOfLine();
            break;

        // a=ice-ufrag:<ice-user-fragment>
        case kLTSdpParserState_A_IceUserFragment:
            lt_strncpyTerm(pParser->pSessionDescription->iceUserFragment, pToken, kLTIceCandidate_UserFragmentLenMax);
            SkipRestOfLine();
            break;

        // a=ice-pwd:<ice-password>
        case kLTSdpParserState_A_IcePassword:
            lt_strncpyTerm(pParser->pSessionDescription->icePassword, pToken, kLTIceCandidate_PasswordLenMax);
            SkipRestOfLine();
            break;

        // a=ice-options:trickle
        case kLTSdpParserState_A_IceOption:
            if (lt_strcmp(pToken, "trickle") == 0) {
                pParser->pSessionDescription->iceOptions |= kLTIceOptions_Trickle;
            } else if (lt_strcmp(pToken, "renomination") == 0) {
                pParser->pSessionDescription->iceOptions |= kLTIceOptions_Renomination;
            }
            break;

        // a=fingerprint:sha-256 ED:AA:3A:59:AE:F1:30:32:E8:8A:9D:FA:4E:04:D9:99:1E:14:69:15:A0:45:49:0D:7B:01:79:95:A4:58:B1:1F
        case kLTSdpParserState_A_Fingerprint_Algo:
            if (lt_strcmp(pToken, "sha-256") == 0) {
                pParser->nItemIdx = 0;
                pParser->state = kLTSdpParserState_A_Fingerprint_Byte;
                s_pTokenizerLib->SetSentinels(pTokenizer, ":\n");
            } else {
                LTLOG_YELLOWALERT("bad.fingerprint.algo", "Unsupported fingerprint algo: %s", pToken);
                SkipRestOfLine();
            }
            break;
        case kLTSdpParserState_A_Fingerprint_Byte:
            if (pParser->nItemIdx < sizeof(pParser->pSessionDescription->fingerprint)) {
                u32 fpByte = lt_strtou32(pToken, NULL, 16);
                pParser->pSessionDescription->fingerprint[pParser->nItemIdx++] = fpByte;
            } else {
                SkipRestOfLine();
            }
            break;

        // a=setup:[active|passive|actpass]
        case kLTSdpParserState_A_Setup:
            if (lt_strcmp(pToken, "active") == 0) {
                pParser->pSessionDescription->nSetupMode = kLTRtcSetupMode_Active;
            } else if (lt_strcmp(pToken, "passive") == 0) {
                pParser->pSessionDescription->nSetupMode = kLTRtcSetupMode_Passive;
            } else if (lt_strcmp(pToken, "actpass") == 0) {
                pParser->pSessionDescription->nSetupMode = kLTRtcSetupMode_Either;
            } else {
                LTLOG_YELLOWALERT("bad.setup", "Unsupported setup mode: %s", pToken);
            }
            SkipRestOfLine();
            break;

        // a=mid:0
        case kLTSdpParserState_A_MediaId:
            lt_strncpyTerm(pParser->pCurMediaDescription->mediaId, pToken, kMediaIdLen);
            SkipRestOfLine();
            break;

        // a=rtpmap:<payload-type> <encoding>/<clock-rate>[/<num-audio-channels>]
        case kLTSdpParserState_A_RtpMap_PayloadType:
            pParser->pCurPayloadFormat = GetOrCreatePayloadFormatByPayloadType(pParser, pToken);
            pParser->state = kLTSdpParserState_A_RtpMap_Encoding;
            s_pTokenizerLib->SetSentinels(pTokenizer, "/\n");
            break;
        case kLTSdpParserState_A_RtpMap_Encoding:
            if (lt_strcasecmp("H264", pToken) == 0) {
                pParser->pCurPayloadFormat->mediaFormat.nEncoding = kLTMediaEncoding_H264;
                /* Assume default profile and packetization mode if not specified */
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nProfile     = 0x42;
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nConstraints = 0x00;
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nLevel       = 0x1f;
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nPacketizationMode = 1;
            } else if (lt_strcasecmp("PCMU", pToken) == 0) {
                pParser->pCurPayloadFormat->mediaFormat.nEncoding = kLTMediaEncoding_PCMU;
            } else if (lt_strcasecmp("PCMA", pToken) == 0) {
                pParser->pCurPayloadFormat->mediaFormat.nEncoding = kLTMediaEncoding_PCMA;
            } else if (lt_strcasecmp("opus", pToken) == 0) {
                pParser->pCurPayloadFormat->mediaFormat.nEncoding = kLTMediaEncoding_Opus;
            } else {
                pParser->pCurPayloadFormat->mediaFormat.nEncoding = kLTMediaEncoding_Unknown;
            }
            pParser->state = kLTSdpParserState_A_RtpMap_ClockRate;
            break;
        case kLTSdpParserState_A_RtpMap_ClockRate: {
            u32 nClockRate = lt_strtou32(pToken, NULL, 10);
            switch (pParser->pCurPayloadFormat->mediaFormat.nEncoding) {
                case kLTMediaEncoding_H264:
                    pParser->pCurPayloadFormat->mediaFormat.params.h264.nClockRate = nClockRate;
                    break;
                case kLTMediaEncoding_PCMU:
                case kLTMediaEncoding_PCMA:
                    pParser->pCurPayloadFormat->mediaFormat.params.g711.nSampleRate = nClockRate;
                    break;
                case kLTMediaEncoding_Opus:
                    pParser->pCurPayloadFormat->mediaFormat.params.opus.nSampleRate = nClockRate;
                    break;
                default:
                    break;
            }
            if (nMatchedSentinel == '/') {
                pParser->state = kLTSdpParserState_A_RtpMap_NumAudioChannels;
            } else {
                SkipRestOfLine();
            }
            break;
        }
        case kLTSdpParserState_A_RtpMap_NumAudioChannels: {
            u32 nChannels = lt_strtou32(pToken, NULL, 10);
            switch (pParser->pCurPayloadFormat->mediaFormat.nEncoding) {
                case kLTMediaEncoding_Opus:
                    pParser->pCurPayloadFormat->mediaFormat.params.opus.nChannels = nChannels;
                    break;
                default:
                    break;
            }
            SkipRestOfLine();
            break;
        }

        // a=rtcp-fb:<payload-type> <value>
        case kLTSdpParserState_A_RtcpFeedback_PayloadType:
            pParser->pCurPayloadFormat = GetOrCreatePayloadFormatByPayloadType(pParser, pToken);
            pParser->state = kLTSdpParserState_A_RtcpFeedback_Value;
            s_pTokenizerLib->SetSentinels(pTokenizer, "\n");
            break;
        case kLTSdpParserState_A_RtcpFeedback_Value:
            if (lt_strcmp("transport-cc", pToken) == 0) {
                pParser->pCurPayloadFormat->nRtcpFeedbackFlags |= kLTRtcpFeedbackFlag_TransportWideCC;
            } else if (lt_strcmp("nack", pToken) == 0) {
                pParser->pCurPayloadFormat->nRtcpFeedbackFlags |= kLTRtcpFeedbackFlag_Nack;
            } else if (lt_strcmp("nack pli", pToken) == 0) {
                pParser->pCurPayloadFormat->nRtcpFeedbackFlags |= kLTRtcpFeedbackFlag_Nack_PictureLossIndication;
            } else if (lt_strcmp("ccm fir", pToken) == 0) {
                pParser->pCurPayloadFormat->nRtcpFeedbackFlags |= kLTRtcpFeedbackFlag_CodecControl_FullIntraframeRequest;
            }
            SkipRestOfLine();
            break;

        // a=fmtp:<payload-type> <format-parameters>
        case kLTSdpParserState_A_FormatParams_PayloadType:
            pParser->pCurPayloadFormat = GetOrCreatePayloadFormatByPayloadType(pParser, pToken);
            pParser->state = kLTSdpParserState_A_FormatParams_ParamName;
            s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
            break;
        case kLTSdpParserState_A_FormatParams_ParamName:
            pParser->pCurParamName = lt_strdup(pToken);
            pParser->state = kLTSdpParserState_A_FormatParams_ParamValue;
            s_pTokenizerLib->SetSentinels(pTokenizer, ";\n");
            break;
        case kLTSdpParserState_A_FormatParams_ParamValue:
            LT_ASSERT(pParser->pCurParamName);
            if (lt_strcmp("profile-level-id", pParser->pCurParamName) == 0) {
                u32 nProfileLevelId = lt_strtou32(pToken, NULL, 16);
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nProfile     = (nProfileLevelId >> 16) & 0xFF;
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nConstraints = (nProfileLevelId >> 8)  & 0xFF;
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nLevel       = (nProfileLevelId >> 0)  & 0xFF;
            } else if (lt_strcmp("packetization-mode", pParser->pCurParamName) == 0) {
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nPacketizationMode = lt_strtou32(pToken, NULL, 10);
            }
            lt_free(pParser->pCurParamName);
            pParser->pCurParamName = NULL;
            pParser->state = kLTSdpParserState_A_FormatParams_ParamName;
            s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
            break;

        // a=group:BUNDLE <mid-1> <mid-2> ...
        case kLTSdpParserState_A_Group_Semantics:
            if (lt_strcmp(pToken, "BUNDLE") == 0) {
                pParser->nItemIdx = 0;
                pParser->state = kLTSdpParserState_A_Group_Mid;
                s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            } else {
                LTLOG_YELLOWALERT("bad.group", "Unsupported group semantics: %s", pToken);
                SkipRestOfLine();
            }
            break;
        case kLTSdpParserState_A_Group_Mid:
            break;

        // a=msid:<media-stream-id> <media-stream-track-id>
        case kLTSdpParserState_A_MediaStreamId_Identifier:
            // TODO: ensure that only one media stream id is specified
            break;
        case kLTSdpParserState_A_MediaStreamId_AppData:
            break;

        // a=ssrc:<ssrc> cname:<cname>
        // a=ssrc:<ssrc> msid:<media-stream-id> <media-stream-track-id>
        // a=ssrc:<ssrc> mslabel:<media-stream-id>
        // a=ssrc:<ssrc> label:<media-stream-track-id>
        case kLTSdpParserState_A_SyncSource_Value:
            pParser->pCurMediaDescription->nSyncSource = lt_strtou32(pToken, NULL, 10);
            // Skip parsing cname, msid, mslabel, label for now
            SkipRestOfLine();
            break;

        // a=extmap:<value>[/<direction>] <uri> <attr>
        case kLTSdpParserState_A_ExtensionMap_Value:
            pParser->nCurExtMapValue = lt_strtou32(pToken, NULL, 10);
            if (nMatchedSentinel == '/')
                pParser->state = kLTSdpParserState_A_ExtensionMap_Direction;
            else
                pParser->state = kLTSdpParserState_A_ExtensionMap_Uri;
            s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            break;
        case kLTSdpParserState_A_ExtensionMap_Direction:
            // Ignore direction for now
            pParser->state = kLTSdpParserState_A_ExtensionMap_Uri;
            s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            break;
        case kLTSdpParserState_A_ExtensionMap_Uri:
            if (lt_strcmp("http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01", pToken) == 0) {
                pParser->pCurMediaDescription->extMap.nTransportWideCC = pParser->nCurExtMapValue;
            }
            // Skip parsing attributes for now
            SkipRestOfLine();
            break;

        // a=imageattr:<payload-type> <direction> [x=<width>,y=<height>]
        case kLTSdpParserState_A_ImageAttr_PayloadType:
            pParser->pCurPayloadFormat = GetOrCreatePayloadFormatByPayloadType(pParser, pToken);
            pParser->state = kLTSdpParserState_A_ImageAttr_Direction;
            s_pTokenizerLib->SetSentinels(pTokenizer, " \n");
            break;
        case kLTSdpParserState_A_ImageAttr_Direction:
            if (lt_strcasecmp("recv", pToken) == 0) {
                s_pTokenizerLib->SetSentinels(pTokenizer, " [=\n");
                pParser->state = kLTSdpParserState_A_ImageAttr_Resolution;
            }
            break;
        case kLTSdpParserState_A_ImageAttr_Resolution:
            if (lt_strcasecmp("x", pToken) == 0) {
                s_pTokenizerLib->SetSentinels(pTokenizer, ",\n");
                pParser->state = kLTSdpParserState_A_ImageAttr_Width;
            }
            else if (lt_strcasecmp("y", pToken) == 0) {
                s_pTokenizerLib->SetSentinels(pTokenizer, "]\n");
                pParser->state = kLTSdpParserState_A_ImageAttr_Height;
            }
            break;
        case kLTSdpParserState_A_ImageAttr_Width:
            pParser->pCurPayloadFormat->mediaFormat.params.h264.nWidth = lt_strtou32(pToken, NULL, 10);
            s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
            pParser->state = kLTSdpParserState_A_ImageAttr_Resolution;
            break;
        case kLTSdpParserState_A_ImageAttr_Height:
            pParser->pCurPayloadFormat->mediaFormat.params.h264.nHeight = lt_strtou32(pToken, NULL, 10);
            if (pParser->pCurPayloadFormat->mediaFormat.params.h264.nWidth == 640 &&
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nHeight == 360) {
                pParser->pCurPayloadFormat->mediaFormat.nKind = kLTMediaKind_Video_SD;
            }
            else if (pParser->pCurPayloadFormat->mediaFormat.params.h264.nWidth == 1920 &&
                pParser->pCurPayloadFormat->mediaFormat.params.h264.nHeight == 1080) {
                pParser->pCurPayloadFormat->mediaFormat.nKind = kLTMediaKind_Video_HD;
            }
            SkipRestOfLine();
            break;

        case kLTSdpParserState_Ignore:
            break;
    }
    if (nMatchedSentinel == '\n') {
        if (pParser->pCurParamName) {
            lt_free(pParser->pCurParamName);
            pParser->pCurParamName = NULL;
        }
        pParser->state = kLTSdpParserState_FieldName;
        s_pTokenizerLib->SetSentinels(pTokenizer, "=\n");
    }
    return bContinue;
}



LTRtcSessionDescription *
LTRtcSessionDescription_Parse(const char *pDescriptionData) {
    LTRtcSessionDescription *pSessionDescription = LTRtcSessionDescription_Create();
    LTRtcSessionDescriptionParser parser = {
        .state = kLTSdpParserState_FieldName,
        .pSessionDescription = pSessionDescription,
        .pCurMediaDescription = NULL,
        .nItemIdx = 0,
    };

    LTTokenizer *pTokenizer = s_pTokenizerLib->CreateTokenizer("=\n", ProcessSDPToken, &parser);
    s_pTokenizerLib->ProcessData(pTokenizer, pDescriptionData, lt_strlen(pDescriptionData), false);
    s_pTokenizerLib->DestroyTokenizer(pTokenizer);

    return pSessionDescription;
}

#define IP_OCTETS(ip) (((ip) >> 0) & 0xFF), (((ip) >> 8) & 0xFF), (((ip) >> 16) & 0xFF), (((ip) >> 24) & 0xFF)

static const char *
MediaKindToString(LTMediaKind nKind) {
    switch (nKind) {
        case kLTMediaKind_Audio:       return "audio";
        case kLTMediaKind_Video_HD:    return "video";
        case kLTMediaKind_Video_SD:    return "video";
        default:                       return "";
    }
}

static const char *
MediaFlowToString(LTRtcMediaFlow nMediaFlow) {
    switch (nMediaFlow) {
        case kLTRtcMediaFlow_Send:        return "sendonly";
        case kLTRtcMediaFlow_Receive:     return "recvonly";
        case kLTRtcMediaFlow_SendReceive: return "sendrecv";
        case kLTRtcMediaFlow_Inactive:    return "inactive";
        default:                          return "";
    }
}

static const char *
SetupModeToString(LTRtcSetupMode nSetupMode) {
    switch (nSetupMode) {
        case kLTRtcSetupMode_Active:     return "active";
        case kLTRtcSetupMode_Passive:    return "passive";
        case kLTRtcSetupMode_Either:     return "actpass";
        default:                         return "";
    }
}

static const char *
CandidateTransportToString(LTIceCandidateTransport nTransport) {
    switch (nTransport) {
        case kLTIceCandidateTransport_UDP: return "udp";
        case kLTIceCandidateTransport_TCP: return "tcp";
        default:                           return "";
    }
}

const char *
LTRtcSessionDescription_ToString(LTRtcSessionDescription *pSessionDescription) {
    LTString pSDP = NULL;
    ltstring_append(&pSDP, "v=0\r\n");
    ltstring_appendformat(&pSDP, "o=lt %llu %lu IN IP4 127.0.0.1\r\n", pSessionDescription->nSessionId, pSessionDescription->nSessionVersion);
    ltstring_appendformat(&pSDP, "s=%s\r\n", pSessionDescription->sessionName);
    ltstring_append(&pSDP, "t=0 0\r\n");
    ltstring_append(&pSDP, "a=msid-semantic: WMS *\r\n");

    if (pSessionDescription->nConnectionAddress) {
        ltstring_appendformat(&pSDP, "c=IN IP4 %d.%d.%d.%d\r\n", IP_OCTETS(pSessionDescription->nConnectionAddress));
    }
    ltstring_append(&pSDP, "a=group:BUNDLE");
    LTArray *pMediaDescriptions = pSessionDescription->pMediaDescriptions;
    for (u32 i = 0; i < pMediaDescriptions->API->GetCount(pMediaDescriptions); ++i) {
        LTRtcMediaDescription *pMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, i, NULL);
        ltstring_appendformat(&pSDP, " %s", pMediaDescription->mediaId);
    }
    ltstring_append(&pSDP, "\r\n");
    for (u32 i = 0; i < pMediaDescriptions->API->GetCount(pMediaDescriptions); ++i) {
        LTRtcMediaDescription *pMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, i, NULL);
        ltstring_appendformat(&pSDP, "m=%s %lu UDP/TLS/RTP/SAVPF",
                                 MediaKindToString(pMediaDescription->nKind),
                                 pMediaDescription->nPort);
        LTArray *pPayloadFormats = pMediaDescription->pPayloadFormats;
        for (u32 j = 0; j < pPayloadFormats->API->GetCount(pPayloadFormats); ++j) {
            LTRtpPayloadFormat *pPayloadFormat = pPayloadFormats->API->Get(pPayloadFormats, j, NULL);
            ltstring_appendformat(&pSDP, " %lu", pPayloadFormat->nPayloadType);
        }
        ltstring_append(&pSDP, "\r\n");
        ltstring_appendformat(&pSDP, "c=IN IP4 %d.%d.%d.%d\r\n", IP_OCTETS(pMediaDescription->nConnectionAddress));
        if (i == 0) { // Only add candidates to the first media description
            LTArray *pCandidates = pSessionDescription->pCandidates;
            for (u32 j = 0; j < pCandidates->API->GetCount(pCandidates); ++j) {
                LTRtcCandidateDescription *pCandidate = pCandidates->API->Get(pCandidates, j, NULL);
                ltstring_appendformat(&pSDP, "a=candidate:%s %lu %s %lu %d.%d.%d.%d %lu typ %s\r\n",
                    pCandidate->foundation,
                    pCandidate->nComponentId,
                    CandidateTransportToString(pCandidate->nTransport),
                    pCandidate->nPriority,
                    IP_OCTETS(pCandidate->endpoint.address),
                    pCandidate->endpoint.port,
                    LTIceCandidateTypeToString(pCandidate->nCandidateType));
            }
        }
        ltstring_appendformat(&pSDP, "a=mid:%s\r\n", pMediaDescription->mediaId);
        if (pMediaDescription->extMap.nTransportWideCC) {
            ltstring_appendformat(&pSDP, "a=extmap:%lu http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n", pMediaDescription->extMap.nTransportWideCC);
        }
        ltstring_appendformat(&pSDP, "a=%s\r\n", MediaFlowToString(pMediaDescription->nMediaFlow));
        if (pMediaDescription->bRtcpMux) {
            ltstring_append(&pSDP, "a=rtcp-mux\r\n");
        }
        ltstring_append(&pSDP, "a=rtcp-rsize\r\n");
        for (u32 j = 0; j < pPayloadFormats->API->GetCount(pPayloadFormats); ++j) {
            LTRtpPayloadFormat *pPayloadFormat = pPayloadFormats->API->Get(pPayloadFormats, j, NULL);
            switch (pPayloadFormat->mediaFormat.nEncoding) {
                case kLTMediaEncoding_H264:
                    ltstring_appendformat(&pSDP, "a=rtpmap:%lu %s/%lu\r\n", pPayloadFormat->nPayloadType, LTMediaEncodingToString(pPayloadFormat->mediaFormat.nEncoding), pPayloadFormat->mediaFormat.params.h264.nClockRate);
                    ltstring_appendformat(&pSDP, "a=fmtp:%lu level-asymmetry-allowed=1;packetization-mode=%lu;profile-level-id=%x%02x%02x\r\n",
                        pPayloadFormat->nPayloadType,
                        LT_Pu32(pPayloadFormat->mediaFormat.params.h264.nPacketizationMode),
                        LT_Pu32(pPayloadFormat->mediaFormat.params.h264.nProfile),
                        LT_Pu32(pPayloadFormat->mediaFormat.params.h264.nConstraints),
                        LT_Pu32(pPayloadFormat->mediaFormat.params.h264.nLevel));
                    break;
                case kLTMediaEncoding_PCMU:
                case kLTMediaEncoding_PCMA:
                    ltstring_appendformat(&pSDP, "a=rtpmap:%lu %s/%lu\r\n", pPayloadFormat->nPayloadType, LTMediaEncodingToString(pPayloadFormat->mediaFormat.nEncoding), pPayloadFormat->mediaFormat.params.g711.nSampleRate);
                    break;
                case kLTMediaEncoding_Opus:
                    ltstring_appendformat(&pSDP, "a=rtpmap:%lu %s/%lu/%lu\r\n", pPayloadFormat->nPayloadType, LTMediaEncodingToString(pPayloadFormat->mediaFormat.nEncoding), pPayloadFormat->mediaFormat.params.opus.nSampleRate, pPayloadFormat->mediaFormat.params.opus.nChannels);
                    break;
                default:
                    break;
            }
            if (pPayloadFormat->nRtcpFeedbackFlags & kLTRtcpFeedbackFlag_Nack) {
                ltstring_appendformat(&pSDP, "a=rtcp-fb:%lu nack\r\n", pPayloadFormat->nPayloadType);
            }
            if (pPayloadFormat->nRtcpFeedbackFlags & kLTRtcpFeedbackFlag_Nack_PictureLossIndication) {
                ltstring_appendformat(&pSDP, "a=rtcp-fb:%lu nack pli\r\n", pPayloadFormat->nPayloadType);
            }
            if (pPayloadFormat->nRtcpFeedbackFlags & kLTRtcpFeedbackFlag_CodecControl_FullIntraframeRequest) {
                ltstring_appendformat(&pSDP, "a=rtcp-fb:%lu ccm fir\r\n", pPayloadFormat->nPayloadType);
            }
            if (pPayloadFormat->nRtcpFeedbackFlags & kLTRtcpFeedbackFlag_TransportWideCC) {
                ltstring_appendformat(&pSDP, "a=rtcp-fb:%lu transport-cc\r\n", pPayloadFormat->nPayloadType);
            }
        }
        if ((pMediaDescription->nMediaFlow == kLTRtcMediaFlow_Send) ||
            (pMediaDescription->nMediaFlow == kLTRtcMediaFlow_SendReceive)) {
            ltstring_appendformat(&pSDP, "a=msid:%s %s\r\n",          pMediaDescription->mediaStreamId, pMediaDescription->mediaStreamTrackId);
            ltstring_appendformat(&pSDP, "a=ssrc:%lu cname:%s\r\n",   pMediaDescription->nSyncSource, pMediaDescription->canonicalName);
            ltstring_appendformat(&pSDP, "a=ssrc:%lu msid:%s %s\r\n", pMediaDescription->nSyncSource, pMediaDescription->mediaStreamId, pMediaDescription->mediaStreamTrackId);
            ltstring_appendformat(&pSDP, "a=ssrc:%lu mslabel:%s\r\n", pMediaDescription->nSyncSource, pMediaDescription->mediaStreamId);
            ltstring_appendformat(&pSDP, "a=ssrc:%lu label:%s\r\n",   pMediaDescription->nSyncSource, pMediaDescription->mediaStreamTrackId);
        }
        ltstring_append(&pSDP, "a=fingerprint:sha-256 ");
        for (u32 j = 0; j < kLTIceCandidate_FingerprintLen - 1; ++j) {
            ltstring_appendformat(&pSDP, "%02X:", (u32)pSessionDescription->fingerprint[j]);
        }
        ltstring_appendformat(&pSDP, "%02X\r\n", pSessionDescription->fingerprint[kLTIceCandidate_FingerprintLen - 1]);
        ltstring_appendformat(&pSDP, "a=setup:%s\r\n", SetupModeToString(pSessionDescription->nSetupMode));
        ltstring_appendformat(&pSDP, "a=ice-ufrag:%s\r\n", pSessionDescription->iceUserFragment);
        ltstring_appendformat(&pSDP, "a=ice-pwd:%s\r\n", pSessionDescription->icePassword);
        if (pSessionDescription->iceOptions) {
            ltstring_append(&pSDP, "a=ice-options:");
            if (pSessionDescription->iceOptions & kLTIceOptions_Trickle) {
                ltstring_append(&pSDP, "trickle ");
            }
            if (pSessionDescription->iceOptions & kLTIceOptions_Renomination) {
                ltstring_append(&pSDP, "renomination ");
            }
            u32 len = lt_strlen(pSDP);
            pSDP[len - 1] = '\r';
            ltstring_appendchar(&pSDP, '\n');
        }
    }
    /* Return the allocated string */
    return pSDP;
}

LTRtcSessionDescription *
LTRtcSessionDescription_Create(void) {
    LTRtcSessionDescription *pSessionDescription = lt_malloc(sizeof(LTRtcSessionDescription));
    if (!pSessionDescription) {
        LTLOG_YELLOWALERT("sdsc.alloc.err", "Failed to allocate session description");
        return NULL;
    }
    lt_memset(pSessionDescription, 0, sizeof(LTRtcSessionDescription));
    pSessionDescription->pMediaDescriptions = LTArray_CreateStructArray(sizeof(LTRtcMediaDescription));
    pSessionDescription->pCandidates        = LTArray_CreateStructArray(sizeof(LTRtcCandidateDescription));
    return pSessionDescription;
}

void
LTRtcSessionDescription_Destroy(LTRtcSessionDescription *pSessionDescription) {
    LTArray *pMediaDescriptions = pSessionDescription->pMediaDescriptions;
    for (u32 i = 0; i < pMediaDescriptions->API->GetCount(pMediaDescriptions); ++i) {
        LTRtcMediaDescription *pMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, i, NULL);
        lt_destroyobject(pMediaDescription->pPayloadFormats);
    }
    lt_destroyobject(pSessionDescription->pCandidates);
    lt_destroyobject(pMediaDescriptions);
    lt_free(pSessionDescription);
}

LTRtcMediaDescription *
LTRtcSessionDescription_GetMediaDescription(LTRtcSessionDescription *pSessionDescription, u32 nMediaDescriptionIdx) {
    LTArray *pMediaDescriptions = pSessionDescription->pMediaDescriptions;
    return pMediaDescriptions->API->Get(pMediaDescriptions, nMediaDescriptionIdx, NULL);
}

LTRtcMediaDescription *
LTRtcSessionDescription_AddMediaDescription(LTRtcSessionDescription *pSessionDescription) {
    LTArray *pMediaDescriptions = pSessionDescription->pMediaDescriptions;
    u32 nNumMediaDescriptions = pMediaDescriptions->API->GetCount(pMediaDescriptions);
    pMediaDescriptions->API->SetCount(pMediaDescriptions, nNumMediaDescriptions + 1);
    LTRtcMediaDescription *pMediaDescription = pMediaDescriptions->API->Get(pMediaDescriptions, nNumMediaDescriptions, NULL);
    pMediaDescription->pPayloadFormats = LTArray_CreateStructArray(sizeof(LTRtpPayloadFormat));
    return pMediaDescription;
}

LTRtpPayloadFormat *
LTRtcSessionDescription_AddPayloadFormat(LTRtcMediaDescription *pMediaDescription) {
    LTArray *pPayloadFormats = pMediaDescription->pPayloadFormats;
    u32 nNumPayloadFormats = pPayloadFormats->API->GetCount(pPayloadFormats);
    pPayloadFormats->API->SetCount(pPayloadFormats, nNumPayloadFormats + 1);
    return pPayloadFormats->API->Get(pPayloadFormats, nNumPayloadFormats, NULL);
}

LTRtcCandidateDescription *
LTRtcSessionDescription_AddCandidate(LTRtcSessionDescription *pSessionDescription) {
    LTArray *pCandidates = pSessionDescription->pCandidates;
    u32 nNumCandidates = pCandidates->API->GetCount(pCandidates);
    pCandidates->API->SetCount(pCandidates, nNumCandidates + 1);
    return pCandidates->API->Get(pCandidates, nNumCandidates, NULL);
}

void
LTRtcSessionDescription_LibFini(void) {
    lt_closelibrary(s_pTokenizerLib);
}

bool
LTRtcSessionDescription_LibInit(void) {
    s_pTokenizerLib = lt_openlibrary(LTUtilityTokenizer);
    if (!s_pTokenizerLib) {
        LTRtcSessionDescription_LibFini();
        return false;
    }
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   trajan      created
 */
