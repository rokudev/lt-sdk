/*******************************************************************************
 * source/lt/net/webrtc/LTRtcSessionDescription.h - WebRTC Session Description Protocol (SDP)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

 #ifndef ROKU_LT_SOURCE_LT_NET_WEBRTC_LTRTCSESSIONDESCRIPTION_H
 #define ROKU_LT_SOURCE_LT_NET_WEBRTC_LTRTCSESSIONDESCRIPTION_H

#include <lt/LT.h>
#include <lt/net/srtp/LTNetSrtp.h>
#include <lt/net/ice/LTNetIce.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTRtcMediaFlow_Inactive    = 0,
    kLTRtcMediaFlow_Send        = 1 << 0,
    kLTRtcMediaFlow_Receive     = 1 << 1,
    kLTRtcMediaFlow_SendReceive = kLTRtcMediaFlow_Send | kLTRtcMediaFlow_Receive,
} LTRtcMediaFlow;

typedef enum {
    kLTRtcSetupMode_Active,
    kLTRtcSetupMode_Passive,
    kLTRtcSetupMode_Either
} LTRtcSetupMode;

typedef enum {
    kLTRtcComponentId_RTP  = 1,
    kLTRtcComponentId_RTCP = 2,
} LTRtcComponentId;

typedef enum {
    kLTRtcRtpExtension_TransportCC,
} LTRtcRtpExtension;

typedef enum {
    kLTIceOptions_None         = 0,
    kLTIceOptions_Trickle      = (1 << 0),
    kLTIceOptions_Renomination = (1 << 1),
} LTIceOptions;

enum {
    kCanonicalNameLen      = 32 + 1,
    kMediaIdLen            = 32 + 1,
    kMediaStreamTrackIdLen = 32 + 1,
    kMediaStreamIdLen      = 32 + 1,
    kSessionNameLen        = 64 + 1,
};

typedef struct {
    u64            nSessionId;
    u32            nSessionVersion;
    char           sessionName[kSessionNameLen];
    u32            nConnectionAddress;
    LTRtcSetupMode nSetupMode;                                           // a=setup:active/passive/actpass
    u8             fingerprint[kLTIceCandidate_FingerprintLen];          // a=fingerprint:sha-256 <fingerprint>
    LTIceOptions   iceOptions;                                           // a=ice-options:[trickle] [renomination]
    char           iceUserFragment[kLTIceCandidate_UserFragmentLenMax];  // a=ice-ufrag:
    char           icePassword[kLTIceCandidate_PasswordLenMax];          // a=ice-pwd:
    LTArray       *pMediaDescriptions;
    LTArray       *pCandidates;                                          // a=candidate:
} LTRtcSessionDescription;

typedef struct {
    // m=<kind> <port> UDP/TLS/RTP/SAVPF <payload-type-1> <payload-type-2> ...
    LTMediaKind           nKind;
    u16                   nPort;
    // c=IN IP4 <addr>
    u32                   nConnectionAddress;
    // a=*
    char                  mediaId[kMediaIdLen];                       // a=mid:<media-id>
    char                  mediaStreamId[kMediaStreamIdLen];           // a=msid:<media-stream-id> <media-stream-track-id> and a=ssrc:<ssrc> msid:<media-stream-id> <media-stream-track-id>
    char                  mediaStreamTrackId[kMediaStreamTrackIdLen]; // mediaStreamId and mediaStreamTrackId also used in:
                                                                      //   a=ssrc:<ssrc> mslabel:<media-stream-id>
                                                                      //   a=ssrc:<ssrc> label:<media-stream-track-id>
    u32                   nSyncSource;                                //   a=ssrc:<ssrc> ...
    char                  canonicalName[kCanonicalNameLen];           //   a=ssrc:<ssrc> cname:<cname>
    LTRtcMediaFlow        nMediaFlow;                                 // a=recvonly/sendonly/sendrecv/inactive
    bool                  bRtcpMux;                                   // a=rtcp-mux
    LTRtpExtensionMap     extMap;                                     // a=extmap:<id> http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
    LTArray             *pPayloadFormats;                            // a=rtpmap:
} LTRtcMediaDescription;

typedef struct {
    // a=candidate:<foundation> <component-id> <transport> <priority> <addr> <port> typ <can-type> raddr <rel-addr> rport <rel-port> ...
    char                        foundation[kLTIceCandidate_FoundationLen];
    LTRtcComponentId            nComponentId;
    LTIceCandidateTransport     nTransport;
    u32                         nPriority;
    LTNetIpv4Endpoint           endpoint;
    LTIceCandidateType          nCandidateType;
    u32                         nRelAddress;
    u16                         nRelPort;
} LTRtcCandidateDescription;

void
LTRtcSessionDescription_LibFini(void);

bool
LTRtcSessionDescription_LibInit(void);

LTRtcSessionDescription *
LTRtcSessionDescription_Parse(const char *pDescriptionData);

const char *
LTRtcSessionDescription_ToString(LTRtcSessionDescription *pSessionDescription);

LTRtcSessionDescription *
LTRtcSessionDescription_Create(void);

void
LTRtcSessionDescription_Destroy(LTRtcSessionDescription *pSessionDescription);

LTRtcMediaDescription *
LTRtcSessionDescription_GetMediaDescription(LTRtcSessionDescription *pSessionDescription, u32 nMediaDescriptionIdx);

LTRtcMediaDescription *
LTRtcSessionDescription_AddMediaDescription(LTRtcSessionDescription *pSessionDescription);

LTRtpPayloadFormat *
LTRtcSessionDescription_AddPayloadFormat(LTRtcMediaDescription *pMediaDescription);

LTRtcCandidateDescription *
LTRtcSessionDescription_AddCandidate(LTRtcSessionDescription *pSessionDescription);

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_WEBRTC_LTRTCSESSIONDESCRIPTION_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-May-22   trajan      created
 */
