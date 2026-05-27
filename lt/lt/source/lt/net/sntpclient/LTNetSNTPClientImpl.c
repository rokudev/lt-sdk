/*******************************************************************************
 * source/lt/net/sntpclient/LTNetSNTPClientImpl.c     Simple Network Time Protocol Client
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

#include <lt/net/sntpclient/LTNetSNTPClient.h>
#include <lt/system/network/LTSocket.h>
#include <lt/system/network/LTSocketInputBuffer.h>
#include <lt/system/network/LTSocketOutputBuffer.h>
#include <lt/system/network/LTSystemNetwork.h>

/**************************************
 * file LTNetSNTPClientImpl #defines */
#define MS_BETWEEN_1900_AND_2000    LT_CONSTU64(3155673600000)
#define MS_BETWEEN_1900_AND_1970    LT_CONSTU64(2208988800000)
#define PORT_NTP                    (123)
#define DEFAULT_TIME_SERVER         "132.163.96.2"                  /* a time.nist.gov address; temp until DNS available */
#define DEFAULT_REQUEST_TIMEOUT     LTTime_Milliseconds(3000)

DEFINE_LTLOG_SECTION("lt.net.sntp");

/******************************************************
* file LTNetSNTPClientImpl static file scope globals */
static const LTArgsDescriptor   s_resultEventArgs = { 2, {kLTArgType_lthandle, kLTArgType_pointer} };

static LTCore *                 s_pCore                 = NULL;
static LTSystemNetwork *        s_pSystemNetwork        = NULL;
static ILTThread *              s_iThread               = NULL;
static ILTEvent *               s_iEvent                = NULL;
static ILTSocket *              s_iSocket               = NULL;
static ILTSocketInputBuffer *   s_iSocketInputBuffer    = NULL;

/*******************************************
* file LTNetSNTPClientImpl private structs */
typedef struct LTNetSNTPClientImpl_TimeRequestData {
    LTTime                      transmitTime;
    LTEvent                     hEvent;
    LTSocket                    hSocket;
    LTThread                    hTimerThread;
    u32                         ipAddress;
    LTNetSNTPClient_TimeResult  result;
} LTNetSNTPClientImpl_TimeRequestData;

/****************************************************
* file LTNetSNTPClientImpl static helper functions */
static u32
TempDottedIPStringToIPAddressNetEndian(const char * pString) {
    u32 nRetVal = 0;
    u8 * ip = (u8 *)&nRetVal;
    int n = 0;
    int len = pString ? lt_strlen(pString) : 0;
    if (! len) return 0;
    bool octet_started = false;
    for (int i = 0; i < len && n < 4; i++) {
        char c = pString[i];
        if (c >= '0' && c <= '9') {
            octet_started = true;
            ip[n] *= 10;
            ip[n] += (c - '0');
        }
        else if (c == '.') {
            if (!octet_started) return 0;
            octet_started = false;
            n++;
        }
        else return 0;
    }
    return (n == 3 && octet_started) ? nRetVal : 0;
}

#if 0
static LTTime
LTTime_CreateTimeout(LTTime timeoutDuration) {
    return LTTime_Add(s_pCore->GetKernelTime(), timeoutDuration);
}

static bool
LTTime_IsTimeoutExpired(LTTime timeout) {
    return LTTime_IsGreaterThanOrEqual(s_pCore->GetKernelTime(), timeout);
}

static LTTime
LTTime_GetTimeoutRemaining(LTTime timeout) {
    LTTime kernelTime = s_pCore->GetKernelTime();
    return (LTTime_IsGreaterThanOrEqual(kernelTime, timeout)) ? LTTime_Zero() : LTTime_Subtract(timeout, kernelTime);
}
#endif

static void HandleTimeout(void * pClientData);

static void
KillTimer(LTNetSNTPClient_TimeRequest hRequest) {
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData && pData->hTimerThread) {
        s_iThread->KillTimer(pData->hTimerThread, &HandleTimeout, LTHANDLE_TO_VOIDPTR(hRequest));
        pData->hTimerThread = 0;
    }
}

#if 0
static void
HostToNetworkPacket(LTNetSNTPClient_Packet * pPacket) {
    pPacket->rootDelay                      = LT_HTONL(pPacket->rootDelay);
    pPacket->rootDispersion                 = LT_HTONL(pPacket->rootDispersion);
    pPacket->refID                          = LT_HTONL(pPacket->refID);
    pPacket->refTimeStamp.seconds           = LT_HTONL(pPacket->refTimeStamp.seconds);
    pPacket->refTimeStamp.fractional        = LT_HTONL(pPacket->refTimeStamp.fractional);
    pPacket->originateTimeStamp.seconds     = LT_HTONL(pPacket->originateTimeStamp.seconds);
    pPacket->originateTimeStamp.fractional  = LT_HTONL(pPacket->originateTimeStamp.fractional);
    pPacket->receiveTimeStamp.seconds       = LT_HTONL(pPacket->receiveTimeStamp.seconds);
    pPacket->receiveTimeStamp.fractional    = LT_HTONL(pPacket->receiveTimeStamp.fractional);
    pPacket->transmitTimeStamp.seconds      = LT_HTONL(pPacket->transmitTimeStamp.seconds);
    pPacket->transmitTimeStamp.fractional   = LT_HTONL(pPacket->transmitTimeStamp.fractional);
}
#endif

static void
NetworkToHostPacket(LTNetSNTPClient_Packet * pPacket) {
    pPacket->rootDelay                      = LT_NTOHL(pPacket->rootDelay);
    pPacket->rootDispersion                 = LT_NTOHL(pPacket->rootDispersion);
    pPacket->refID                          = LT_NTOHL(pPacket->refID);
    pPacket->refTimeStamp.seconds           = LT_NTOHL(pPacket->refTimeStamp.seconds);
    pPacket->refTimeStamp.fractional        = LT_NTOHL(pPacket->refTimeStamp.fractional);
    pPacket->originateTimeStamp.seconds     = LT_NTOHL(pPacket->originateTimeStamp.seconds);
    pPacket->originateTimeStamp.fractional  = LT_NTOHL(pPacket->originateTimeStamp.fractional);
    pPacket->receiveTimeStamp.seconds       = LT_NTOHL(pPacket->receiveTimeStamp.seconds);
    pPacket->receiveTimeStamp.fractional    = LT_NTOHL(pPacket->receiveTimeStamp.fractional);
    pPacket->transmitTimeStamp.seconds      = LT_NTOHL(pPacket->transmitTimeStamp.seconds);
    pPacket->transmitTimeStamp.fractional   = LT_NTOHL(pPacket->transmitTimeStamp.fractional);
}

static void
SetTimeStampInNetworkByteOrderWithKernelTime(LTTime kernelTime, LTNetSNTPClient_Timestamp * pTimestamp) {
    u64 nMilliseconds = (u64)LTTime_GetMilliseconds(kernelTime);
    u64 nMillisecondsFractional = ((u64)1) << 32;
    nMillisecondsFractional *= nMilliseconds % 1000;
    nMillisecondsFractional /= 1000;
    pTimestamp->seconds = LT_HTONL(((u32)(nMilliseconds / 1000)));
    pTimestamp->fractional = LT_HTONL(((u32)(nMillisecondsFractional)));
}

static LTTime
GetTimeUTCSince1970(const LTNetSNTPClient_Timestamp * pTimestamp) {
    u64 nMillisecondsSince1970 = ((u64)pTimestamp->fractional * 1000 + 0xFFFFFFFF/2) / 0xFFFFFFFF ;
    // ISSUE: should use < 1000, but I hit the assert with nMillisecondsSince1970 == 1000
    // ASSERT(nMillisecondsSince1970 < 1000);
    LT_ASSERT(nMillisecondsSince1970 <= 1000);
    nMillisecondsSince1970 += ((u64)pTimestamp->seconds) * 1000;

    if (nMillisecondsSince1970 < MS_BETWEEN_1900_AND_1970) nMillisecondsSince1970 = 0;
    else nMillisecondsSince1970 -= MS_BETWEEN_1900_AND_1970;

    return LTTime_Milliseconds(nMillisecondsSince1970);
}

static void
OnDestroyTimeRequest(LTNetSNTPClient_TimeRequest hRequest) {
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData) {
        KillTimer(hRequest);
        if (pData->hSocket) s_pCore->DestroyHandle(pData->hSocket);
        if (pData->hEvent) s_pCore->DestroyHandle(pData->hEvent);
    }
}

static void
DispatchResultEventProc(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData) {
    LT_UNUSED(hEvent);
    LTNetSNTPClient_TimeResultProc *   pResultProc = (LTNetSNTPClient_TimeResultProc *)pEventProc;
    LTNetSNTPClient_TimeRequest           hRequest = LTArgs_lthandleAt(0, pEventArgs);
    const LTNetSNTPClient_TimeResult * pTimeResult = (const LTNetSNTPClient_TimeResult *)LTArgs_pointerAt(1, pEventArgs);
    pResultProc(hRequest, pTimeResult, pEventProcClientData);
}

LT_FORCEINLINE bool
TimeStampsAreEqual(const LTNetSNTPClient_Timestamp * pTimestamp1, const LTNetSNTPClient_Timestamp * pTimestamp2) {
    return pTimestamp1->seconds == pTimestamp2->seconds && pTimestamp1->fractional == pTimestamp2->fractional;
}

static void
HandleSocketDataArrived(LTSocket hSocket, LTSocketInputBuffer hBuffer, void * pClientData) {
    LTTime timeReceived = s_pCore->GetKernelTime();
    LT_UNUSED(hSocket);
    LTNetSNTPClient_TimeRequest hRequest = VOIDPTR_TO_LTHANDLE(pClientData);
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData && pData->result.resultStatus == kLTNetSNTPClient_ResultStatus_AwaitingServerResponse) {
        u32 nBytes = s_iSocketInputBuffer->GetBufferedAmount(hBuffer);
        if (nBytes == sizeof(pData->result.packetReceived)) {
            s_iSocketInputBuffer->ReadData(hBuffer, &pData->result.packetReceived, nBytes);
            NetworkToHostPacket(&pData->result.packetReceived);
            if ((pData->result.packetReceived.flags & kLTNetSNTPClient_PacketFlags_LeapIndicatorMask) == kLTNetSNTPClient_PacketFlags_LeapIndicatorAlarm) {
                pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ServerTimeUnsynchronized;
            }
            else if (pData->result.packetReceived.stratum == 0) {
                pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ServerHasNoTimeSource;
            }
            else if (! TimeStampsAreEqual(&pData->result.packetSent.transmitTimeStamp, &pData->result.packetReceived.originateTimeStamp)) {
                // not the packet I'm looking for; return hoping to receive the right one!
                LTLOG_YELLOWALERT("pkt.recv.notmine", "send timestamp mismatch on packet received");
                return;
            }
            else {         
                // ok, got the receive packet.  calculate the round trip delay and the timebase for pData->result
                LT_ASSERT(LTTime_IsGreaterThanOrEqual(timeReceived, pData->transmitTime));
                pData->result.calculatedRoundTripDelay = LTTime_Subtract(timeReceived, pData->transmitTime);
                
                // first calculate the midpoint of the server reported timeUTCs using both times in the timeBase
                // but leave the result in the secondary time
                pData->result.timeBaseUTC.primaryClockTime = GetTimeUTCSince1970(&pData->result.packetReceived.receiveTimeStamp);
                pData->result.timeBaseUTC.secondaryClockTime = GetTimeUTCSince1970(&pData->result.packetReceived.transmitTimeStamp);
                if (LTTime_IsGreaterThanOrEqual(pData->result.timeBaseUTC.primaryClockTime, pData->result.timeBaseUTC.secondaryClockTime)) {
                    LTTime_SubtractFrom(pData->result.timeBaseUTC.primaryClockTime, pData->result.timeBaseUTC.secondaryClockTime);
                    LTTime_DivideBy(pData->result.timeBaseUTC.primaryClockTime, 2);
                    LTTime_AddTo(pData->result.timeBaseUTC.secondaryClockTime, pData->result.timeBaseUTC.primaryClockTime);
                }
                else {
                    LTTime_SubtractFrom(pData->result.timeBaseUTC.secondaryClockTime, pData->result.timeBaseUTC.primaryClockTime);
                    LTTime_DivideBy(pData->result.timeBaseUTC.secondaryClockTime, 2);
                    LTTime_AddTo(pData->result.timeBaseUTC.secondaryClockTime, pData->result.timeBaseUTC.primaryClockTime);
                }
                
                // now set the midpoint of the kernel times in the primaryClockTime
                pData->result.timeBaseUTC.primaryClockTime = LTTime_Divide(pData->result.calculatedRoundTripDelay, 2);
                LTTime_AddTo(pData->result.timeBaseUTC.primaryClockTime, pData->transmitTime);
                
                pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ValidTimeReceived;
            }
        }
        else pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ServerResponseInvalid;
        
        KillTimer(hRequest);
        s_iEvent->NotifyEvent(pData->hEvent, hRequest, &pData->result);
    }
}

static void
HandleTimeout(void * pClientData) {
    LTNetSNTPClient_TimeRequest hRequest = VOIDPTR_TO_LTHANDLE(pClientData);
    KillTimer(hRequest);
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData) {
        // TODO: rather than destroy the socket, unregister the events and close the connection
        s_pCore->DestroyHandle(pData->hSocket);
        pData->hSocket = 0;
        pData->ipAddress = 0;
        pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ServerResponseTimeout;
        s_iEvent->NotifyEvent(pData->hEvent, hRequest, &pData->result);
    }
}

static void
OnSocketEvent(LTSocket hSocket, LTNetworkEvent event, LTNetworkResult result, void * pClientData) {
    LT_UNUSED(hSocket);
    
    if (result != kLTNetwork_ResultOk) {
        LTLOG("sock.err", "%s:%s", s_pSystemNetwork->EventToString(event), s_pSystemNetwork->ResultToString(result));
        return;
    }
    
    if (event == kLTNetwork_EventConnect) {
        LTNetSNTPClient_TimeRequest hRequest = VOIDPTR_TO_LTHANDLE(pClientData);
        LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
        if (pData) {
            LT_ASSERT(hSocket == pData->hSocket);
            LT_ASSERT(pData->result.resultStatus == kLTNetSNTPClient_ResultStatus_RequestNotYetSent);
            // register a data arrived callback on the now connected socket
            //          ISSUE : Make sure it's ok to do this again when the socket is reused            
            s_iSocket->OnDataArrived(pData->hSocket, &HandleSocketDataArrived, pClientData);
            // send out the packet; set all temporary and packet variables ensuring everything in the packet is net-endian
            // add the transmit time last
            u32 nBytesLeft = sizeof(pData->result.packetSent);
            u8 * pBuffer = (u8 *)&pData->result.packetSent;
            pData->result.packetSent.flags = kLTNetSNTPClient_PacketFlags_Version3 | kLTNetSNTPClient_PacketFlags_ModeClient;
            pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_AwaitingServerResponse;
            pData->transmitTime = s_pCore->GetKernelTime();
            SetTimeStampInNetworkByteOrderWithKernelTime(pData->transmitTime, &pData->result.packetSent.transmitTimeStamp);
            // pData->result.packetSent is ready to send in network byte order:
            //     packetSent.flags is u8
            //     packetSent.transmitTimeStamp was set in network byte order
            //     everything else is 0
            while (1) {
                u32 nBytesSent = s_iSocket->WriteData(hSocket, pBuffer, nBytesLeft);
                if (0 == (nBytesLeft -= nBytesSent)) break;
                pBuffer += nBytesSent;
                s_iThread->Sleep(LTTime_Milliseconds(1));
            }
            // packet fully sent (or at least submitted), change packetSent into host byte order for client callback convenience
            NetworkToHostPacket(&pData->result.packetSent);    
        }
    }
}

/***************************************************************
* file LTNetSNTPClientImpl LTNetSNTPClient private interfaces */
typedef_LTLIBRARY_INTERFACE(ILTNetSNTPClientTimeRequest, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_INTERFACE(ILTNetSNTPClientTimeRequest, OnDestroyTimeRequest) LTLIBRARY_DEFINITION;

/*************************************************************
* LTNetSNTPClient public api functions */
static LTNetSNTPClient_TimeRequest
CreateTimeRequest(void) {
    LTNetSNTPClient_TimeRequest hRequest = s_pCore->CreateHandle((LTInterface * )&s_ILTNetSNTPClientTimeRequest, sizeof(LTNetSNTPClientImpl_TimeRequestData));
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (NULL == pData) return 0;
    lt_memset(pData, 0, sizeof(*pData));
    pData->hEvent = s_pCore->CreateEvent(&s_resultEventArgs, &DispatchResultEventProc, NULL, NULL);
    // ISSUE error check pData->hEvent
    return hRequest;
}

static void
OnTimeResult(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResultProc * pResultProc, LTThread_ClientDataReleaseProc * pClientDataReleaseProc, void * pClientData) {
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData && pData->hEvent) s_iEvent->RegisterForEvent(pData->hEvent, pResultProc, pClientDataReleaseProc, pClientData, false);
}

static void
NoTimeResult(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResultProc * pResultProc, void * pClientData) {
    LT_UNUSED(pClientData);
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (pData && pData->hEvent) s_iEvent->UnregisterFromEvent(pData->hEvent, pResultProc);
}

static void
RequestInternetTimeFromServer(LTNetSNTPClient_TimeRequest hRequest, const char * pServerName, LTTime timeout) {
    LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
    if (! pData) return;
    u32 ipAddress = TempDottedIPStringToIPAddressNetEndian(pServerName);
    if (! ipAddress) return;
    
    if (pData->hSocket) {
        // we have a socket from a previous request, see if we can reuse it
        if (LTNetSNTPClient_ResultStatus_IsIncomplete(pData->result.resultStatus)) {
            // previous request is still in flight, ignore this request
            return;
        }
        if (ipAddress == pData->ipAddress) {
            // new request is for same server; clear data and reuse socket by simulating fresh connection
            if (pData->hTimerThread) {
                LTLOG_YELLOWALERT("request.2timers.1", "leftover timer");
                KillTimer(hRequest);
            }
            lt_memset(&pData->result, 0, sizeof(pData->result));
            OnSocketEvent(pData->hSocket, kLTNetwork_EventConnect, kLTNetwork_ResultOk, LTHANDLE_TO_VOIDPTR(hRequest));
            return;
        }
        // new request is for different server, don't reuse socket, destroy it
        s_pCore->DestroyHandle(pData->hSocket);
    }
    
    // going to initiate request, kill timer if running, should not be unless new request issue by other thread
    // which is client thread safety issue not our issue, but kill the timer anyway
    if (pData->hTimerThread) {
        LTLOG_YELLOWALERT("request.2timers.2", "leftover timer");
        KillTimer(hRequest);
    }
    lt_memset(&pData->result, 0, sizeof(pData->result));
    
    // set our timeout if required
    if (! LTTime_IsInfinite(timeout)) {
        if (LTTime_IsZero(timeout)) timeout = DEFAULT_REQUEST_TIMEOUT;
        LTThread hThread = s_iThread->GetCurrentThread();
        if (hThread) {
            pData->hTimerThread = hThread;
            s_iThread->SetTimer(hThread, timeout, &HandleTimeout, NULL, LTHANDLE_TO_VOIDPTR(hRequest));
        }
    }
    
    // create socket
    LTSocketSpec socketSpec = {
        .device           = 0,
        .protocol         = kLTNetwork_ProtocolUDP,
        .local            = {{.nAddressNetEndian = 0}, 0},
        .remote           = {{.nAddressNetEndian = ipAddress}, PORT_NTP},
        .bServer          = false,
        .inputBufferSize  = sizeof(LTNetSNTPClient_Packet),
        .outputBufferSize = sizeof(LTNetSNTPClient_Packet),
    };
    
    pData->ipAddress = ipAddress;
    if (0 == (pData->hSocket = s_iSocket->CreateSocket(&socketSpec, OnSocketEvent, (void *)hRequest))) {
        KillTimer(hRequest);
        pData->ipAddress = 0;
        LTLOG_YELLOWALERT("request.csf", "create socket failure");
        pData->result.resultStatus = kLTNetSNTPClient_ResultStatus_ServerConnectionFailed;
        s_iEvent->NotifyEvent(pData->hEvent, hRequest, &pData->result);
    }
}

static void
RequestInternetTime(LTNetSNTPClient_TimeRequest hRequest) {
    RequestInternetTimeFromServer(hRequest, DEFAULT_TIME_SERVER, DEFAULT_REQUEST_TIMEOUT);
}

static void
GetTimeResult(LTNetSNTPClient_TimeRequest hRequest, LTNetSNTPClient_TimeResult * pResultToSet) {
    if (pResultToSet) {
        LTNetSNTPClientImpl_TimeRequestData * pData = (LTNetSNTPClientImpl_TimeRequestData *)s_pCore->GetHandlePrivateData(hRequest);
        if (pData) lt_memcpy(pResultToSet, &pData->result, sizeof(*pResultToSet));
    }
}

/*__________________________________________
 / LTNetSNTPClient library initialization */
static void LTNetSNTPClientImpl_LibFini(void);

static bool
LTNetSNTPClientImpl_LibInit(void) {

    bool bResult = false;
    do {
        if (NULL == (s_pCore              = LT_GetCore())) break;
        if (NULL == (s_pSystemNetwork     = (LTSystemNetwork *)     s_pCore->OpenLibrary("LTSystemNetwork")))                                break;
        if (NULL == (s_iThread            = (ILTThread *)           s_pCore->GetLibraryInterface((LTLibrary *)s_pCore, "ILTThread")))                     break;
        if (NULL == (s_iEvent             = (ILTEvent *)            s_pCore->GetLibraryInterface((LTLibrary *)s_pCore, "ILTEvent")))                      break;
        if (NULL == (s_iSocket            = (ILTSocket *)           s_pCore->GetLibraryInterface((LTLibrary *)s_pSystemNetwork, "ILTSocket")))            break;
        if (NULL == (s_iSocketInputBuffer = (ILTSocketInputBuffer *)s_pCore->GetLibraryInterface((LTLibrary *)s_pSystemNetwork, "ILTSocketInputBuffer"))) break;
        bResult = true;
    } while (false);
    
    if (! bResult) LTNetSNTPClientImpl_LibFini();
    return bResult;
}

static void
LTNetSNTPClientImpl_LibFini(void) {
    if (s_pSystemNetwork) s_pCore->CloseLibrary((LTLibrary *)s_pSystemNetwork);
    s_iSocketInputBuffer  = NULL;
    s_iSocket             = NULL;
    s_iEvent              = NULL;
    s_iThread             = NULL;
    s_pSystemNetwork      = NULL;
    s_pCore               = NULL;
}

/*__________________________________________________
 / LTNetSNTPClient library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTNetSNTPClient) {
    .CreateTimeRequest              = &CreateTimeRequest,
    .OnTimeResult                   = &OnTimeResult,
    .NoTimeResult                   = &NoTimeResult,
    .RequestInternetTimeFromServer  = &RequestInternetTimeFromServer,
    .RequestInternetTime            = &RequestInternetTime,
    .GetTimeResult                  = &GetTimeResult
} LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  19-May-05   augustus    created
 *  16-May-22   augustus    ported to LT from Cascade
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 */
