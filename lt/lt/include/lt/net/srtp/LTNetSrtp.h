/*******************************************************************************
 * <lt/net/srtp/LTNetSrtp.h> - Secure Realtime Transport Protocol (SRTP)
 *
 * `LTNetSrtp` provides an implementation of the Secure Realtime Transport Protocol
 * (SRTP) used to send media data over the network. The `LTNetSrtp` library root
 * interface provides facilities for media format negotiation and creation of SRTP
 * `LTSrtpSession`s.
 *
 * The `LTSrtpSession` maintains the cryptographic context used to send/receive
 * encrypted and authenticated media packets from/to one or more associated
 * `LTSrtpStream` objects. Each `LTSrtpStream` corresponds to a single incoming
 * or outgoing media stream and a local media source or sink object from/to which
 * that media is received/sent. For example, a typical camera application might
 * create a session which contains three `LTSrtpStream`s:
 *     - An outgoing video stream sourced from a local image sensor & video encoder.
 *     - An outgoing audio stream sourced from a local microphone.
 *     - An incoming audio stream received from a remote peer and played back over a local speaker.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_SRTP_LTNETSRTP_H
#define ROKU_LT_INCLUDE_LT_NET_SRTP_LTNETSRTP_H

#include <lt/LTTypes.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/device/media/LTDeviceMedia.h>

LT_EXTERN_C_BEGIN

typedef struct LTSrtpSession LTSrtpSession;
typedef struct LTRtpStream   LTRtpStream;
typedef struct LTRtpStreamImpl LTRtpStreamImpl;

typedef_LTENUM_SIZED(LTRtcpFeedbackFlag, u32) {
    kLTRtcpFeedbackFlag_Nack                               = (1 << 0),
    kLTRtcpFeedbackFlag_Nack_PictureLossIndication         = (1 << 1),
    kLTRtcpFeedbackFlag_CodecControl_FullIntraframeRequest = (1 << 2),
    kLTRtcpFeedbackFlag_TransportWideCC                    = (1 << 3),
};

typedef_LTENUM_SIZED(LTRtpStreamDirection, u32) {
    kLTRtpStreamDirection_Send,
    kLTRtpStreamDirection_Receive,
};

typedef struct {
    u8 nTransportWideCC;                    /**< RTP extension header ID for Transport Wide Congestion Control */
} LTRtpExtensionMap;

typedef struct {
    u32                nPayloadType;        /**< The payload type ID for the RTP stream. */
    LTRtcpFeedbackFlag nRtcpFeedbackFlags;  /**< Flags indicating the supported RTCP feedback features for the RTP stream. */
    LTMediaFormat      mediaFormat;         /**< The format of the media carried by the RTP stream. */
} LTRtpPayloadFormat;

typedef enum {
    kLTSrtpSession_Event_Established,
    kLTSrtpSession_Event_Terminated,
    kLTSrtpSession_Event_MediaError,
} LTSrtpSession_Event;

typedef void (LTSrtpSession_SessionEventProc)(LTSrtpSession *session, LTSrtpSession_Event event, void * pClientData);
    /**< callback for SRTP session events
     *
     *   @param hSession the session instance
     *   @param event the session event
     *   @param pClientData the client data that was specified when the callback was registered.
     */

typedef_LTObject(LTSrtpSession, 1) {
/** ___________________________________
 *   @section LTSrtpSession Object
 *   * @brief an interface for interacting with SRTP media sessions.
 *   *
 *   * LTSrtpSession provides an interface for creating SRTP media sessions.
 \______________________________________________________________________________________*/

    bool          (* SupportsFormat)(LTMediaFormat * pFormat);
        /**< Checks if the the session is capable of transmitting media with the given format.
         *
         *   Each media format carried in an SRTP session must be packetized according to
         *   format-specific rules. This function checks if the SRTP subsystem provides
         *   support for packetization of the given format and is used in the negotiation
         *   of media formats during session establishment.
         *
         *   NOTE: This may be called prior to calling Init()
         *
         *   @param pFormat the format to be evaluated for support.
         *   @return true if the given media format is supported, else false.
         */

    void             (* Init)(LTSrtpSession *session, const char *sessionId, LTSocket hSocket, LTSocket hDtlsSocket);
        /**< initializes a new SRTP media session
         *
         *   @param session the SRTP session instance.
         *   @param hSocket the socket used to send SRTP data.
         *   @param hDtlsSocket the DTLS socket from which to derive session keys.
         *   @param pSessionId the unique webrtc session id
         */

    void             (* OnSessionEvent)(LTSrtpSession *session, LTSrtpSession_SessionEventProc * pCallback, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);
        /**< Registers a session event callback.
         *
         *   @param session the SRTP session instance.
         *   @param pCallback the callback to be registered.
         *   @param pClientDataReleaseProc the client data release proc.
         *   @param pClientData client data to be passed to the callback.
         */

    void             (* NoSessionEvent)(LTSrtpSession *session, LTSrtpSession_SessionEventProc * pCallback);
        /**< Unregisters a session event callback.
         *
         *   @param session the SRTP session instance.
         *   @param pCallback the callback to be unregistered.
         */

    LTRtpStream     *(* OpenStream)(LTSrtpSession *session, LTRtpStreamDirection direction, u32 nSyncSource, LTRtpExtensionMap * pExtMap, LTRtpPayloadFormat * pPayloadFormat);
        /**< Creates a new SRTP stream instance used to send/receive media data from/to the given media endpoint.
         *
         *   Creates a send or receive media stream depending on direction.
         *
         *   @param session the SRTP session instance.
         *   @param direction the media flow direction.
         *   @param nSyncSource the Synchronization Source (SSRC) identifier for the stream.
         *   @param pExtMap contains extension header IDs for all extension headers which will be attached to RTP packet associated with the stream.
         *   @param pPayloadFormat contains information about the format of the media data carried by the stream.
         *   @return the created LTRtpStream instance
         */

    void (*SetTransportSocket)(LTSrtpSession *session, LTSocket hSocket);
        /**< sets the socket over which RTP packets will be sent/received.
         *
         *   @param session the SRTP session instance.
         *   @param hSocket the new transport socket
         */

    void (*Pause)(LTSrtpSession *session);
        /**< Pause sender and receiver streams.
         *
         *   @param session the SRTP session instance.
         */

    void (*Resume)(LTSrtpSession *session);
        /**< Resume sender and receiver streams.
         *
         *   @param session the SRTP session instance.
         */
} LTOBJECT_API;

typedef_LTObject(LTRtpStream, 1) {
/** ___________________________________
 *   @section ILTRtpStream Interface
 *   * @brief an interface for interacting with SRTP media streams.
 *   *
 *   * ILTRtpStream provides a simple interface for interacting with SRTP media streams.
 \______________________________________________________________________________________*/

    u32          (* GetSyncSource)(LTRtpStream *stream);
        /**< Gets the Synchronization Source (SSRC) for the given RTP stream.
         *
         *   @param stream the RTP stream handle.
         *   @return the synchronization source identifier for the RTP stream
         */

    void         (* Pause)(LTRtpStream *stream);
        /**< Pauses the RTP stream and underlying media endpoint.
         *
         *   @param stream the RTP stream to be paused.
         */

    void         (* Resume)(LTRtpStream *stream);
        /**< Resumes the RTP stream and underlying media endpoint.
         *
         *   @param stream the RTP stream to be resumed.
         */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_SRTP_LTNETSRTP_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  26-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
