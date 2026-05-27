/*******************************************************************************
 * <lt/net/sntpclient/LTNetSNTPClient.h>     Simple Network Time Protocol Client
 *
 * The LTNetSNTPClient library fetches time UTC from the specified host using
 * the SNTP v3 protocol and delivers to the client a struct LTCore_TimeBase
 * suitable for calling LTCore_SetTimeBaseUTC().  It also delivers the complete
 * sent and received NTP v.3 packets so the client can use the packet information to
 * implement NTP or other custom accurizing protocol, e.g. MeshTime.
 *
 * TODO: upgrade to NTP v.4 packets and add NTP v.4 MAC security.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_SNTPCLIENT_LTNETSNTPCLIENT_H
#define ROKU_LT_INCLUDE_LT_NET_SNTPCLIENT_LTNETSNTPCLIENT_H

#include <lt/core/LTCore.h>
LT_EXTERN_C_BEGIN

/*____________________________
_/ LTNetSNTPClient typedefs */
typedef LTHandle  LTNetSNTPClient_TimeRequest;
    ///<* an SNTP time request handle

typedef struct    LTNetSNTPClient_TimeResult      LTNetSNTPClient_TimeResult;
    ///<* result data of an SNTP time request     (full declaration below)

typedef void     (LTNetSNTPClient_TimeResultProc)(LTNetSNTPClient_TimeRequest hRequest, const LTNetSNTPClient_TimeResult * pTimeResult, void * pClientData);
    ///<* client supplied callback type for asynchronous receipt of time request results
    /// @param hRequest the request
    /// @param pTimeResult the result of the request - valid only for the duration of the callback invocation (and only while hResult remains undestroyed)
    /// @param pClientData the client data supplied when the callback was registered
    /// @note  it is legal to destroy hRequest in this callback.  If you do so, then pTimeResult will no longer be valid - do not dereference pTimeResult after destroying hRequest!
    /// @note  it is legal to call RequestInternetTime() from within this callback to initiate a new SNTP request.  If you do so, then *pTimeResult will be cleared for the new request so
    ///        be sure to copy any result data you wish to persist out from *pTimeResult before initiating a new request.

/*____________________________________________
_/ LTNetSNTPClient LTLIBRARY_ROOT_INTERFACE */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetSNTPClient, 1) {

    LTNetSNTPClient_TimeRequest (* CreateTimeRequest)(void);
        ///< creates a time request.
        ///
        ///  Example Usage - setting clock time using LTNetSNTPClient:
        ///  <pre>
        ///  static LTNetSNTPClient * s_pLTNetSNTPClient = NULL; // lt_openlibrary() assumed, not shown here
        //
        ///  // first define a result callback proc to handle the result of the SNTP time request
        ///  static void MyTimeResultProc(LTNetSNTPClient_TimeRequest hRequest, const LTNetSNTPClient_TimeResult * pTimeResult, void * pClientData) {
        ///      LTCore * pCore = LT_GetCore();
        ///      if (LTNetSNTPClient_ResultStatus_IsSuccess(pTimeResult->resultStatus)) {
        ///          pCore->SetClockTimeBaseUTC(&pTimeResult->timeBaseUTC);
        ///      }
        ///      pCore->DestroyHandle(hRequest); // I destroy hRequest here because I no longer need it; cannot deref pTimeResult after I destroy the request!
        ///  }
        ///
        ///  // create a time request, set the callback, and request the time
        ///  void FetchTime(void) {
        ///      LTNetSNTPClient_TimeRequest hRequest = s_pLTNetSNTPClient->CreateTimeRequest();
        ///      if (hRequest) {
        ///          s_pLTNetSNTPClient->OnTimeResult(hRequest, &MyTimeResultProc, NULL);
        ///          s_pLTNetSNTPClient->RequestInternetTime(hRequest);
        ///      }
        ///  }
        ///  </pre>

    void (* OnTimeResult)(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResultProc * pResultProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData);
        ///< registers result proc with hRequest for asynchronous receipt of request result

    void (* NoTimeResult)(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResultProc * pResultProc, void * pClientData);
        ///< unregisters result proc from hRequest that was previously registered for asynchronous receipt of request result

    void (* RequestInternetTimeFromServer)(LTNetSNTPClient_TimeRequest hRequest, const char * pServerName, LTTime timeout);
        ///< initiates request for SNTP time from the server identified by pServerName
        ///
        /// @param hRequest the request to initiate
        /// @param pServerName the dns server name or ipv4 dotted ip address string of the time server to request time from
        /// @param timeout the timeout for the request - use LTTime_Zero() for the default timeout and LTTime_Infinite() for no timeout
        /// @note  only one request can be in flight at a time per request handle; calls to RequestInternetTime or RequestInternetTimeFromServer
        ///        made while a request is still in progress will be ignored

    void (* RequestInternetTime)(LTNetSNTPClient_TimeRequest hRequest);
        ///< initiates request for SNTP time from the Internet using an internally defined default time server with default timeout
        ///
        /// @param hRequest the request to initiate
        /// @note  only one request can be in flight at a time per request handle; calls to RequestInternetTime or RequestInternetTimeFromServer
        ///        made while a request is still in progress will be ignored

    void (* GetTimeResult)(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResult * pResultToSet);
        ///< fills pResultToSet with the current result of the specified time request; may be used to poll
        ///
        /// @param hRequest the request to retrieve the result from
        /// @param pResultToSet a pointer to an allocated LTNetSNTPClient_TimeResult that will receive the result

} LTLIBRARY_INTERFACE;

/*___________________________________________
_/ LTNetSNTPClient SNTP packet declarations /
  _________________________________________
_/      enum LTNetSNTPClient_PacketFlags */
typedef enum LTNetSNTPClient_PacketFlags {
    kLTNetSNTPClient_PacketFlags_LeapIndicatorMask  = 0xC0,
    kLTNetSNTPClient_PacketFlags_LeapIndicatorNone  = 0x00,
    kLTNetSNTPClient_PacketFlags_LeapIndicatorLong  = 0x40,
    kLTNetSNTPClient_PacketFlags_LeapIndicatorShort = 0x80,
    kLTNetSNTPClient_PacketFlags_LeapIndicatorAlarm = 0xC0,
    kLTNetSNTPClient_PacketFlags_VersionMask        = 0x38,
    kLTNetSNTPClient_PacketFlags_Version1           = 0x08,
    kLTNetSNTPClient_PacketFlags_Version2           = 0x10,
    kLTNetSNTPClient_PacketFlags_Version3           = 0x18,
    kLTNetSNTPClient_PacketFlags_ModeMask           = 0x07,
    kLTNetSNTPClient_PacketFlags_ModeActive         = 0x01,
    kLTNetSNTPClient_PacketFlags_ModePassive        = 0x02,
    kLTNetSNTPClient_PacketFlags_ModeClient         = 0x03,
    kLTNetSNTPClient_PacketFlags_ModeServer         = 0x04,
    kLTNetSNTPClient_PacketFlags_ModeBroadcast      = 0x05,
    kLTNetSNTPClient_PacketFlags_ModeControl        = 0x06,
} LTNetSNTPClient_PacketFlags; /**< [S]NTP packet flag values of bitmasks and field values used for encoding the flags member of the [S]NTP packet struct */

/*_________________________________________
_/      struct LTNetSNTPClient_Timestamp */
typedef struct LTNetSNTPClient_Timestamp {
    u32                                             seconds;
    u32                                             fractional;
} LTNetSNTPClient_Timestamp; /**< representation of a timestamp within [S]NTP packets */

/*______________________________________
_/      struct LTNetSNTPClient_Packet */
typedef struct LTNetSNTPClient_Packet {
 /* BEGIN NTP v3.1 packet fields */
    u8                                              flags;
    u8                                              stratum;
    u8                                              poll;
    u8                                              precision;
    u32                                             rootDelay;
    u32                                             rootDispersion;
    u32                                             refID;
    LTNetSNTPClient_Timestamp                       refTimeStamp;
    LTNetSNTPClient_Timestamp                       originateTimeStamp;
    LTNetSNTPClient_Timestamp                       receiveTimeStamp;
    LTNetSNTPClient_Timestamp                       transmitTimeStamp;
 /* END   NTP v3.1 packet fields */
 /* BEGIN NTP v4.0 packet fields */
    //
    // [S]NTP v4.0 support not yet implemented, 4.0 packet fields go here
    //
 /* END   NTP v4.0 packet fields */
} LTNetSNTPClient_Packet; /**<  the [S]NTP packet type */

/*__________________________________________
_/      enum LTNetSNTPClient_ResultStatus */
typedef enum  {
     /* Do not reorder/renumber these */
    kLTNetSNTPClient_ResultStatus_RequestNotYetSent = 0,    ///<* request for time has not yet been sent
    kLTNetSNTPClient_ResultStatus_AwaitingServerResponse,   ///<* awaiting response from time server
    kLTNetSNTPClient_ResultStatus_ValidTimeReceived,        ///<* pTimeResult->timeBaseUTC is valid and accurate and may be used
    kLTNetSNTPClient_ResultStatus_ServerConnectionFailed,   ///<* connection to server failed
    kLTNetSNTPClient_ResultStatus_ServerHasNoTimeService,   ///<* server not running time service (connection refused)
    kLTNetSNTPClient_ResultStatus_ServerResponseTimeout,    ///<* timeout while awaiting server response
    kLTNetSNTPClient_ResultStatus_ServerTimeUnsynchronized, ///<* server reported that its time is unsynchronized
    kLTNetSNTPClient_ResultStatus_ServerHasNoTimeSource,    ///<* server reported that it has no time source
    kLTNetSNTPClient_ResultStatus_ServerResponseInvalid     ///<* server delivered invalid response
} LTNetSNTPClient_ResultStatus; /**< represents the outcome status of a time request */

/*_________________________________________________________________________________
_/      enum LTNetSNTPClient_ResultStatus interpretation inline helper functions */
LT_FORCEINLINE bool LTNetSNTPClient_ResultStatus_IsIncomplete(LTNetSNTPClient_ResultStatus status) { return (status <  kLTNetSNTPClient_ResultStatus_ValidTimeReceived); }
LT_FORCEINLINE bool LTNetSNTPClient_ResultStatus_IsSuccess(LTNetSNTPClient_ResultStatus status)    { return (status == kLTNetSNTPClient_ResultStatus_ValidTimeReceived); }
LT_FORCEINLINE bool LTNetSNTPClient_ResultStatus_IsFailure(LTNetSNTPClient_ResultStatus status)    { return (status >  kLTNetSNTPClient_ResultStatus_ValidTimeReceived); }

/*__________________________________________
_/      struct LTNetSNTPClient_TimeResult */
typedef struct LTNetSNTPClient_TimeResult {
    LTTimeBase                                      timeBaseUTC;                ///<* calculated local system UTC timeBase suitable for calling LTCore->SetTimeBaseUTC() as desired; only valid with valid resultStatus
    LTTime                                          calculatedRoundTripDelay;   ///<* calculated round trip delay of this SNTP fetch; only valid with valid resultStatus
    LTNetSNTPClient_Packet                          packetSent;                 ///<* entire SNTP packet that was sent for the outbound request should you care to examine
    LTNetSNTPClient_Packet                          packetReceived;             ///<* entire SNTP packet that was received should you care to examine
    LTNetSNTPClient_ResultStatus                    resultStatus;               ///<* indicates whether or not this result is valid
} LTNetSNTPClient_TimeResult;
    /**< &LTNetSNTPClient_TimeResult contains the the result of the SNTP request with calculated round trip delay
     *   and calculated local system UTC time base that may be conveniently used to call LTCore->SetClockTimeBaseUTC() to set
     *   the local system's notion of UTC time
     */

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_SNTPCLIENT_LTNETSNTPCLIENT_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  18-May-05   augustus    created
 *  20-Apr-22   augustus    ported from soundbridge
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
