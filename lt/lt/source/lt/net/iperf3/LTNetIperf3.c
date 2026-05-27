/*******************************************************************************
 *
 * LT NetIperf3
 * ------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/net/iperf3/LTNetIperf3.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <lt/utility/jsonparser/LTUtilityJsonParser.h>

DEFINE_LTLOG_SECTION("lt.net.iperf3");
/* Defines */
#define UDP_CONNECT_MSG "9876"
#define UDP_CONNECT_REPLY "6789"
#define IPERF_UNLIMITED_BITRATE LT_U32_MAX
#define TCP_EMPTY_RESULTS_JSON "{\
    \"cpu_util_total\" :  0.0, \
    \"cpu_util_user\" :   0.0, \
    \"cpu_util_system\" : 0.0, \
    \"sender_has_retransmits\" : 0, \
    \"streams\" : [ { \
        \"id\" :           1,   \
        \"bytes\" :        0,   \
        \"retransmits\" :  0,   \
        \"errors\" :       0,   \
        \"packets\" :      0,   \
        \"jitter\" :       0.0, \
        \"startTime\" :    0.0, \
        \"end_time\" :     0.0  \
    } ] \
}"
#define RESULT_JSON_MAX_SIZE    (1536)
#define RESULTS_JSON_FORMAT "{\
\"cpu_util_total\":0.0,\
\"cpu_util_user\":0.0,\
\"cpu_util_system\":0.0,\
\"sender_has_retransmits\":0,\
\"streams\":[%s]\
}"

// support up to 8 parallel stream
#define RESULT_JSON_STREAM_MAX_SIZE    (156)
#define RESULT_JSON_STREAM_FORMAT "{\
\"id\":%d,\
\"bytes\":%llu,\
\"retransmits\":0,\
\"errors\":%u,\
\"packets\":%u,\
\"jitter\":%.12f,\
\"startTime\":0,\
\"end_time\":%llu.%09llu\
}"
#define FUNC_CAST(F) (void (*)(void *))F
#define GET_JITTER_REPORT(J, P) (double)((J) / 1000000000) / (P)
#define GET_TPUT_REPORT(B, T) (((double)((B) << 3) / (T)) / 1000000)

enum {
    kDefaultIPerfPort = 5201,
    kMaxPort          = 65535,
    kWriteSize        = 1460,   // bytes
    kTestDuration     = 10,    // seconds
    kReportInterval   =  1,    // seconds
    kSocketSpecSize   = 128,    // bytes
    kUdpBitrate       = 1048576, // bits/sec
    kIpAddrSize       = 46,    // bytes
};

typedef enum {
    kIPerfState_INITIALIZE       = -1, /* private */
    kIPerfState_NONE             =  0,
    kIPerfState_TEST_START       =  1,
    kIPerfState_TEST_RUNNING     =  2,
    kIPerfState_RESULT_REQUEST   =  3, /* not used */
    kIPerfState_TEST_END         =  4,
    kIPerfState_STREAM_BEGIN     =  5, /* not used */
    kIPerfState_STREAM_RUNNING   =  6, /* not used */
    kIPerfState_STREAM_END       =  7, /* not used */
    kIPerfState_ALL_STREAMS_END  =  8, /* not used */
    kIPerfState_PARAM_EXCHANGE   =  9,
    kIPerfState_CREATE_STREAMS   = 10,
    kIPerfState_SERVER_TERMINATE = 11,
    kIPerfState_CLIENT_TERMINATE = 12,
    kIPerfState_EXCHANGE_RESULTS = 13,
    kIPerfState_DISPLAY_RESULTS  = 14,
    kIPerfState_IPERF_START      = 15,
    kIPerfState_IPERF_DONE       = 16,
} IPerfState;

typedef enum {
    kIPerfServerMode_TCP = 0,
    kIPerfServerMode_UDP = 1,
} IperfServerMode;

/* Structures */
#define MAX_IPERF_CLIENT_CNT    8
typedef struct {
    LTThread                    hThread;
    LTSocket                    hControlSocket;
    LTNetIperf3_ReportCallback  * callback;
    void                        * callback_arg;
    char                        uuid[37];
    u8                          * data;
} IperfCommonInfo;

typedef struct IperfStreamStat{
    s64     startTimeSys;
    s64     startTimePeer;
    s64     lastRecvedSysTime;
    s64     lastRecvedPeerTime;
    u32     lastRecvedFrameNo;
    u64     receivedBytes;
    u32     retransmissions;
    s64     jitters;
    s64     latency;
    u32     errors;
    u32     packets;
    u32     frames;
} IperfStreamStat;

typedef struct ClientDataStream {
    struct ClientDataStream         * next;
    LTThread                        hThread;
    LTSocket                        hDataSocket;
    u32                             nIterationSent;
    IperfStreamStat                 streamStat;
    bool                            bWaitTimer;
    struct IperfClientInfo          * pClient;
} ClientDataStream;

typedef struct IperfClientInfo {
    IperfCommonInfo             common;
    char                        server[kIpAddrSize];
    LTNetIperf3_Parameters      params;
    struct ClientDataStream     * stream_list;
    IPerfState                  state;

    LTTime                      startTime;
    LTTime                      lastReportTime;
    u32                         nTotalSent;
    u32                         nReportedSent;
    bool                        bTestDone;
} IperfClientInfo;

typedef struct IPerfServerInfo {
    IperfCommonInfo            common;
    struct ServerDataClient    * pClient;
    LTSocket                    hListenSocket;
    LTSocket                    hUdpSocket;
    u32                         nClient;
    u32                         nConnectedClient;
    LTNetIperf3_Parameters      params;
    u32                         client_param_size;
    u32                         state;
    u32                         nResultsSize;
    u32                         nResultsReceived;
    char                        * pResults;
    u32                         mode;
    u32                         streamId;
} IPerfServerInfo;

typedef struct ServerDataClient {
    struct ServerDataClient * next;
    u32                      streamId;
    LTThread                 hThread;
    LTSocket                 hDataSocket;
    LTNetIpv4Endpoint        endPoint;
    IperfStreamStat          clientStat;
    struct IPerfServerInfo   * pServer;
} ServerDataClient;

typedef struct IperfFrame {
    u32     sec;
    u32     usec;
    u32     frameNo;
} IperfFrame;

/* Prototypes */
/* Library Interface Prototypes */
static void LTNetIperf3Impl_RunClient(const char *ipaddr, LTNetIperf3_Parameters *p, LTNetIperf3_ReportCallback *cb, void *cb_arg);
static void LTNetIperf3Impl_KillClient(void *pClientData);
static void LTNetIperf3Impl_RunServer(LTNetIperf3_Parameters *p, LTNetIperf3_ReportCallback *cb, void *cb_arg);
static void LTNetIperf3Impl_KillServer(void *pClientData);

/* Iperf3 Client Prototypes */
static void Iperf3_Client_RunClient(void * pClientData);

static void Iperf3_Client_ControlSocketEventProc(LTSocket hControlSocket, LTSocket_Event event, void * pClientData);
static void Iperf3_Client_ControlSocketDataReady(LTSocket hControlSocket, void * pClientData);

static void Iperf3_Client_DataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pClientData);
static void Iperf3_Client_DataSocketWriteProc(void * pClientData);
static void Iperf3_Client_DataSocketWrite(LTSocket hDataSocket, void * pClientData);
static void Iperf3_Client_DataTimerEventProc(void * pClientData);
static void Iperf3_Client_TestTimerProc(void * pClientData);

static void Iperf3_Client_CleanUp(IperfClientInfo * pClientThread);


static void Iperf3_Client_Util_SendUDPConnectMessage(ClientDataStream *pStream);
static u32  Iperf3_Client_Util_GetActiveDataStreams(IperfClientInfo* pClientThread);
static void Iperf3_Client_Util_DestroyDataStream(ClientDataStream *stream);
static ClientDataStream *
            Iperf3_Client_Util_GetDataStreamSock(LTSocket hSocket, IperfClientInfo* pClientThread);

/* Iperf3 Server Prototypes */
static void Iperf3_Server_CloseClientSockets(void);
static void Iperf3_Server_DataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pClientData);
static void Iperf3_Server_DataSocketReadProc(void *pClientData);
static void Iperf3_Server_DataSocketProc(void *pClientData);

static void Iperf3_Server_DataSocketDataReady(LTSocket hDataSocket, void * pClientData);
static void Iperf3_Server_ControlSocketEventProc(LTSocket hControlSocket, LTSocket_Event event, void * pClientData);
static void Iperf3_Server_ControlSocketDataReady(LTSocket hControlSocket, void * pClientData);
static void Iperf3_Server_ListenSocketEventProc(LTSocket hListenSocket, LTSocket_Event event, void * pClientData);
static void Iperf3_Server_RunServer(void * pServerData);

static void Iperf3_Server_ShowProgressTimerProc(void * arg);
static void Iperf3_Server_Util_SendUDPConnectMessage(IPerfServerInfo *pServerInfo, LTNetIpv4Endpoint ep);
static void Iperf3_Server_UdpDataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pClientData);;

/* Iperf3 Utility Prototypes */
static void Iperf3_Util_CallbackHandler(IperfClientInfo * pClient, const char *tag, const char *fmt, ...);

/* Static Variables */
static struct {
    LTCore           *pCore;
    LTMutex          *mutex;
    LTNetCore        *pNetCore;
    ILTThread        *iThread;
} S;

static const LTNetIperf3_Parameters LTNetIperf3_ParametersDefault = {
    .port = kDefaultIPerfPort,
    .udp = false,
    .time = kTestDuration,
    .bitrate = IPERF_UNLIMITED_BITRATE,
};

static IperfClientInfo s_client_info;
static IPerfServerInfo s_server_info;
/*******************************************************************************
 * Library Functions
 ******************************************************************************/
static void LTNetIperf3Impl_LibFini(void) {
    LTNetIperf3Impl_KillServer(NULL);
    LTNetIperf3Impl_KillClient(NULL);
    lt_closelibrary(S.pNetCore);
    S.pNetCore = NULL;
}

static bool LTNetIperf3Impl_LibInit(void) {
    S.pCore = LT_GetCore();
    S.pNetCore = lt_openlibrary(LTNetCore);
    S.mutex = lt_createobject(LTMutex);
    S.iThread = lt_getlibraryinterface(ILTThread, S.pCore);

    if (!S.pNetCore) {
        LTNetIperf3Impl_LibFini();
        return false;
    }
    return true;
}

/* Iperf3 Client Implementation Start */
static void Iperf3_Client_RunClient(void * pClientData) {
    IperfClientInfo *pClient = (IperfClientInfo *)pClientData;

    if (!pClient) {
        LTLOG("clnt.lnch.err", "Client info not available\n");
        return;
    }
    char *spec = lt_malloc(kSocketSpecSize);
    if (!spec) return;
    lt_snprintf(spec, kSocketSpecSize, "tcp dscp: %d ip: %s port: %lu",
                pClient->params.dscp,
                pClient->server,
                LT_Pu32(pClient->params.port));
    pClient->common.hControlSocket = S.pNetCore->OpenSocket(0, spec, Iperf3_Client_ControlSocketEventProc, pClientData);
    if (pClient->common.hControlSocket == 0) {
        LTLOG_YELLOWALERT("clnt.ctrl.sock.fail", "spec: %s", spec);
        LTNetIperf3Impl_KillClient(NULL);
    }
    lt_free(spec);
}

static void Iperf3_Client_DataSocketWriteProc(void * pClientData) {
    if (pClientData == NULL) {
        return;
    }
    ClientDataStream *stream = (ClientDataStream*) pClientData;
    Iperf3_Client_DataSocketWrite(stream->hDataSocket, stream->pClient);
}

static void Iperf3_Util_CallbackHandler(IperfClientInfo * pClient, const char *tag, const char *fmt, ...) {
    S.mutex->API->Lock(S.mutex);

    char buf[512];
    lt_va_list args;
    lt_va_start(args, fmt);
    lt_vsnprintf(buf, sizeof(buf), fmt, args);
    lt_va_end(args);
    LTLOG(tag, "%s", buf);
    if (pClient && pClient->common.callback) pClient->common.callback(buf, pClient->common.callback_arg);

    S.mutex->API->Unlock(S.mutex);
}

static void Iperf3_Client_ControlSocketEventProc(LTSocket hControlSocket,
                                                 LTSocket_Event event,
                                                 void * pClientData) {
    IperfClientInfo *pClient = pClientData;
    if (!pClient) {
        LTLOG("clnt.evt.null", "pClient is NULL");
        return;
    }
    if (pClient->common.hControlSocket != hControlSocket) {
        Iperf3_Util_CallbackHandler(pClient, "clnt.sock.hdl.err", "Error: socket handler error %d != %d", pClient->common.hControlSocket, hControlSocket);
        LTNetIperf3Impl_KillClient(pClient);
        return;
    }

    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hControlSocket);

    LTLOG_DEBUG("ctrl.evt", "sock: %lu, event: 0x%lx", LT_Pu32(hControlSocket), LT_Pu32(event));

    if (!LTSocket_Event_Error(event)) {
        if (event == kLTSocket_Event_Connected) {
            pClient->state = kIPerfState_NONE;
            LTUtilityByteOps * pByteOps = lt_openlibrary(LTUtilityByteOps);
            u8 uuid[16] = { 0 };
            pByteOps->GenUUID(uuid);
            pByteOps->UUIDToString(uuid, pClient->common.uuid, sizeof(pClient->common.uuid));
            lt_closelibrary(pByteOps);
            LTLOG("clnt.ctrl.uuid", "%s\n", pClient->common.uuid);
            iSocket->WriteSocket(hControlSocket, pClient->common.uuid, sizeof(pClient->common.uuid));
        } else if (event == kLTSocket_Event_Disconnected) {
            // Destroy the socket after disconnect
            iSocket->Destroy(hControlSocket);
            pClient->common.hControlSocket = 0;
            Iperf3_Client_CleanUp(pClient);
        } else if (event == kLTSocket_Event_ReadReady) {
            while (iSocket->ReadSocket(hControlSocket, NULL, 0) > 0) {
                Iperf3_Client_ControlSocketDataReady(hControlSocket, pClient);
            }
        }
    } else {
        char buf[64];
        lt_snprintf(buf, sizeof(buf), "Error: <%lx> Event error '%d'\n", LT_PLT_HANDLE(hControlSocket), event);
        LTLOG("clnt.ctrl.evt.err", "%s", buf);
        if (pClient && pClient->common.callback) pClient->common.callback(buf, pClient->common.callback_arg);
        LTNetIperf3Impl_KillClient(NULL);
    }
}

static void Iperf3_Client_ControlSocketDataReady(LTSocket hControlSocket, void * pClientData) {
    IperfClientInfo * pClient = pClientData;

    LT_ASSERT(pClient->common.hControlSocket == hControlSocket);
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, pClient->common.hControlSocket);
    if (pClient->state == kIPerfState_EXCHANGE_RESULTS) {
        u32 nResultsSize = 0;
        u32 nResultsReceived = 0;
        char *pResults = NULL;
        if (nResultsSize == 0) {
            u32 len = 0;
            if (iSocket->ReadSocket(hControlSocket, NULL, 0) < (s32)sizeof(len)) return;
            iSocket->ReadSocket(hControlSocket, &len, sizeof(len));
            nResultsSize = LT_NTOHL(len);
            pResults = lt_malloc(nResultsSize + 1);
            pResults[nResultsSize] = 0;
        }
        while (nResultsReceived < nResultsSize) {
            u32 rcvd = iSocket->ReadSocket(hControlSocket, pResults + nResultsReceived,
                                           nResultsSize - nResultsReceived);
            nResultsReceived += rcvd;
        }

        if (nResultsReceived == nResultsSize) {
            Iperf3_Util_CallbackHandler(pClient, "clnt.results", "results: %s", pResults);
            lt_free(pResults);
            pClient->state = kIPerfState_NONE;
        }
    } else {
        u8 what;
        iSocket->ReadSocket(hControlSocket, &what, 1);
        u32 nlen = 0;

        switch (what) {
        case kIPerfState_PARAM_EXCHANGE:
            LTLOG("clnt.ctrl.in.param", "PARAM_EXCHANGE\n");
            pClient->state = (IPerfState) what;
            char json[80];
            if (pClient->params.udp) {
                lt_snprintf(json, sizeof(json), "{\"udp\": \"true\",\n\"time\": %d,\n\"len\": %d,\n\"parallel\": %d}",
                        pClient->params.time, pClient->params.mss, pClient->params.streams);
            } else {
                lt_snprintf(json, sizeof(json), "{\"tcp\": \"true\",\n\"time\": %d,\n\"parallel\": %d}",
                        pClient->params.time, pClient->params.streams);
            }
            nlen = LT_HTONL(lt_strlen(json));
            iSocket->WriteSocket(hControlSocket, &nlen, sizeof(nlen));
            iSocket->WriteSocket(hControlSocket, json, lt_strlen(json));
            break;

        case kIPerfState_CREATE_STREAMS:
            pClient->state = (IPerfState) what;
            char *spec = lt_malloc(kSocketSpecSize);
            if (!spec) return;
            lt_snprintf(spec, kSocketSpecSize, "%s ip: %s port: %lu dscp: %d",
                        pClient->params.udp?"udp":"tcp",
                        pClient->server,
                        LT_Pu32(pClient->params.port),
                        pClient->params.dscp);
            LTLOG("clnt.ctrl.in.strms", "CREATE_STREAMS %s\n", spec);

            for (u32 i = 0; i < pClient->params.streams; i++) {
                ClientDataStream *pDataSock = lt_malloc(sizeof(ClientDataStream));
                lt_memset(pDataSock, 0, sizeof(ClientDataStream));
                char name[20];
                lt_snprintf(name, 20, "iperfClnt_%08lX", LT_Pu32(pDataSock));
                LTLOG("clnt.ctrl.in.th.name", "name: %s", name);
                pDataSock->hThread = S.pCore->CreateThread(name);
                S.iThread->SetStackSize(pDataSock->hThread, 2048);
                S.iThread->SetPriority(pDataSock->hThread, pClient->params.prio);
                S.iThread->Start(pDataSock->hThread, NULL, NULL);
                pDataSock->pClient = pClient;
                pDataSock->next = pClient->stream_list;
                pClient->stream_list = pDataSock;
                pDataSock->hDataSocket = S.pNetCore->OpenSocket(0, spec, Iperf3_Client_DataSocketEventProc, pClient);
                LTLOG("clnt.ctrl.in.sock", "hDataSock: %lu", LT_Pu32(pDataSock->hDataSocket));
                if (pDataSock->hDataSocket == 0) {
                    LTLOG_YELLOWALERT("clnt.sock.fail", "spec: %s", spec);
                    LTNetIperf3Impl_KillClient(NULL);
                    break;
                }
            }
            lt_free(spec);
            break;

        case kIPerfState_TEST_START:
            LTLOG("clnt.ctrl.in.strt", "TEST_START");
            pClient->state = (IPerfState) what;
            // Set timer for killing current clients for Error handling
            S.iThread->SetTimer(pClient->common.hThread, LTTime_Seconds(pClient->params.time + 10), LTNetIperf3Impl_KillClient, NULL, NULL);
            break;

        case kIPerfState_TEST_RUNNING:
            LTLOG("clnt.ctrl.in.run", "TEST_RUNNING\n");
            pClient->state = (IPerfState) what;
            pClient->startTime = S.pCore->GetKernelTime();
            for (ClientDataStream *stream = pClient->stream_list; stream != NULL; stream = stream->next) {
                S.iThread->QueueTaskProcIfRequired(stream->hThread, Iperf3_Client_DataSocketWriteProc, NULL, stream);
            }
            S.iThread->SetTimer(pClient->common.hThread, LTTime_Seconds(kReportInterval), Iperf3_Client_TestTimerProc, NULL, pClient);
            break;

        case kIPerfState_EXCHANGE_RESULTS:
            LTLOG("clnt.ctrl.in.res", "EXCHANGE_RESULTS\n");
            pClient->state = (IPerfState) what;
            nlen = LT_HTONL(lt_strlen(TCP_EMPTY_RESULTS_JSON));
            iSocket->WriteSocket(pClient->common.hControlSocket, &nlen, sizeof(nlen));
            iSocket->WriteSocket(pClient->common.hControlSocket, TCP_EMPTY_RESULTS_JSON, lt_strlen(TCP_EMPTY_RESULTS_JSON));
            break;

        case kIPerfState_DISPLAY_RESULTS:
            LTLOG("clnt.ctrl.in.disp", "DISPLAY_RESULTS\n");
            u8 state = kIPerfState_IPERF_DONE;
            for(ClientDataStream *stream = pClient->stream_list; stream != NULL; stream = stream->next) {
                if (stream->hDataSocket) {
                    Iperf3_Client_Util_DestroyDataStream(stream);
                }
            }
            iSocket->WriteSocket(pClient->common.hControlSocket, &state, 1);
            iSocket->DisconnectSocket(pClient->common.hControlSocket);
            break;

        case kIPerfState_RESULT_REQUEST:
        case kIPerfState_TEST_END:
        case kIPerfState_STREAM_BEGIN:
        case kIPerfState_STREAM_RUNNING:
        case kIPerfState_STREAM_END:
        case kIPerfState_ALL_STREAMS_END:
        case kIPerfState_CLIENT_TERMINATE:
        case kIPerfState_IPERF_START:
        case kIPerfState_IPERF_DONE:
            LTLOG("clnt.ctrl.in.unh", "unhandled state %02x\n", what);
            break;
        case kIPerfState_SERVER_TERMINATE:
            Iperf3_Util_CallbackHandler(pClient, "clnt.ctrl.server.term", "Error: Server Terminated");
            LTNetIperf3Impl_KillClient(pClientData);
            break;
        }
    }
}
static void Iperf3_Client_Util_SendUDPConnectMessage(ClientDataStream *pStream) {
    LTSocket hDataSocket = pStream->hDataSocket;
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);
    char *msg = UDP_CONNECT_MSG;
    iSocket->WriteSocket(hDataSocket, msg, 4);
}
static void Iperf3_Client_DataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pClientData) {
    IperfClientInfo *pClient = (IperfClientInfo *)pClientData;
    ClientDataStream *pStream = Iperf3_Client_Util_GetDataStreamSock(hDataSocket, pClient);
    if (pStream == NULL) {
        return;
    }
    if (event != kLTSocket_Event_WriteReady) {
        LTLOG_DEBUG("clnt.data.evt", "Sock: %lx, Event: %lx", LT_Pu32(hDataSocket), LT_Pu32(event));
    }

    if (!LTSocket_Event_Error(event)) {
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);
        if (event == kLTSocket_Event_Connected) {
            iSocket->WriteSocket(hDataSocket, pClient->common.uuid, 37);
        } else if (event == kLTSocket_Event_SocketReady) {
            if (pClient->params.udp) {
                Iperf3_Client_Util_SendUDPConnectMessage(pStream);
                S.iThread->SetTimer(pStream->hThread, LTTime_Milliseconds(100), FUNC_CAST(Iperf3_Client_Util_SendUDPConnectMessage), NULL, pStream);
            }
        } else if (event == kLTSocket_Event_ReadReady) {
            if (pClient->params.udp) { // Receive UDP_CONNECT_REPLY
                u32 buf;
                iSocket->ReadSocket(hDataSocket, &buf, sizeof(buf));
                S.iThread->KillTimer(pStream->hThread, FUNC_CAST(Iperf3_Client_Util_SendUDPConnectMessage), pStream);
            }
        } else if (event == kLTSocket_Event_Disconnected) {
            Iperf3_Client_Util_DestroyDataStream(Iperf3_Client_Util_GetDataStreamSock(hDataSocket, pClient));
        } else if (event == kLTSocket_Event_WriteReady) {
            S.iThread->QueueTaskProcIfRequired(pStream->hThread, Iperf3_Client_DataSocketWriteProc, NULL, pStream);
        }
    } else {
        Iperf3_Util_CallbackHandler(pClient, "clnt.data.evt.err", "Error: <%lx> Event '0x%lx'\n",
              LT_PLT_HANDLE(hDataSocket), LT_Pu32(event));
        LTNetIperf3Impl_KillClient(pClientData);
    }
}

static void Iperf3_Client_TestTimerProc(void * pClientData) {
    IperfClientInfo *pClient = (IperfClientInfo *)pClientData;
    if (pClient->bTestDone) {
        return;
    }
    u32    sent    = pClient->nTotalSent;
    LTTime now     = S.pCore->GetKernelTime();

    LTTime elapsed = now;
    LTTime_SubtractFrom(elapsed, pClient->startTime);

    LTTime time_delta = now;
    if (LTTime_GetMicroseconds(pClient->lastReportTime) != 0) {
        LTTime_SubtractFrom(time_delta, pClient->lastReportTime);
        u64 sent_delta = sent - pClient->nReportedSent;
        u64 rate       = (sent_delta * 8000) / LTTime_GetMilliseconds(time_delta);
        Iperf3_Util_CallbackHandler(pClient, "clnt.rpt", "thput: %llu bps", LT_Pu64(rate));
    }

    //LTLOG("clnt.elapsed", "%lld", LTTime_GetSeconds(time_delta));
    if (!pClient->bTestDone && LTTime_GetSeconds(elapsed) >= (s64) pClient->params.time) {
        pClient->bTestDone = true;
        S.iThread->KillTimer(pClient->common.hThread, Iperf3_Client_TestTimerProc, pClientData);

        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, pClient->common.hControlSocket);
        if (!iSocket) {
            LTLOG_DEBUG("clnt.test.tmr.err", "Error: <%lx> Control socket not available or already closed", LT_PLT_HANDLE(pClient->common.hControlSocket));
            return;
        }
        u8 state = kIPerfState_TEST_END;
        iSocket->WriteSocket(pClient->common.hControlSocket, &state, 1);
    }

    pClient->nReportedSent = sent;
    pClient->lastReportTime = now;
}

static void Iperf3_Client_DataTimerEventProc(void * pStreamData) {
    ClientDataStream *pStream = (ClientDataStream *)pStreamData;
    S.iThread->KillTimer(pStream->hThread, Iperf3_Client_DataTimerEventProc, pStreamData);
    pStream->bWaitTimer = false;
    S.iThread->QueueTaskProcIfRequired(pStream->hThread, Iperf3_Client_DataSocketWriteProc, NULL, pStream);
}

static void Iperf3_Client_DataSocketWrite(LTSocket hDataSocket, void * pClientData) {
    s32 sent = 0;
    IperfClientInfo *pClient = (IperfClientInfo *)pClientData;
    if (pClient->bTestDone) {
        return;
    }
    ClientDataStream *pDataSock = Iperf3_Client_Util_GetDataStreamSock(hDataSocket, pClient);
    if (pDataSock == NULL) {
        return;
    }
    if (pDataSock->bWaitTimer) {
        return;
    }
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);

    if (LTTime_IsZero(pClient->startTime) || // Not started yet
        (pClient->bTestDone && (pDataSock->nIterationSent == pClient->params.mss))) { // Finished
        return;
    }

    int max_burst = 100;
    while (!(pClient->bTestDone && (pDataSock->nIterationSent == pClient->params.mss))) {
        // If we limit bitrate then delay if we are ahead
        if (pClient->params.bitrate > 0 && pClient->params.bitrate != IPERF_UNLIMITED_BITRATE) {
            LTTime elapsed = S.pCore->GetKernelTime();
            LTTime_SubtractFrom(elapsed, pClient->startTime);

            LTTime targetTime = LTTimeInitializer_Seconds(pClient->nTotalSent * 8);
            LTTime_DivideBy(targetTime, pClient->params.bitrate);
            LTTime_SubtractFrom(targetTime, elapsed);

            if (LTTime_GetMilliseconds(targetTime) > 0) {
                S.iThread->SetTimer(S.iThread->GetCurrentThread(),
                                  LTTime_Milliseconds(LTTime_GetMilliseconds(targetTime)),
                                  Iperf3_Client_DataTimerEventProc, NULL, pDataSock);
                pDataSock->bWaitTimer = true;
                return;
            }
        }

        // If this is UDP then set timestamp & seq in the packet
        if (pClient->params.udp && (pDataSock->nIterationSent == 0)) {
            struct UdpTimestamp { u32 sec; u32 usec; u32 seq; };
            struct UdpTimestamp *ts = (struct UdpTimestamp *)pClient->common.data;
            LTTime now = S.pCore->GetKernelTime();
            ts->sec = LT_HTONL(LTTime_GetSeconds(now));
            ts->usec = LT_HTONL(LTTime_GetMicroseconds(now)%1000000);
            ts->seq = LT_HTONL(pClient->nTotalSent / pClient->params.mss + 1);
        }

        // Send data on the socket
        if (!hDataSocket || !pClient || !pClient->common.data || pDataSock->nIterationSent > pClient->params.mss) {
            LTLOG("clnt.data.wrt", "hDataSocket %lx, pClient %p, pClient->data %p, nIterationSent %lu", LT_PLT_HANDLE(hDataSocket),
                                                                                                        pClient,
                                                                                                        pClient->common.data,
                                                                                                        LT_Pu32(pDataSock->nIterationSent));
        } else {
            sent = iSocket->WriteSocket(hDataSocket,
                                        pClient->common.data + pDataSock->nIterationSent,
                                        pClient->params.mss - pDataSock->nIterationSent);
        }

        if (sent < 0) {
            if (!pClient->bTestDone) {
                Iperf3_Util_CallbackHandler(pClient, "clnt.data.wrt.err", "Error: <%lx> Unrecoverable write error <%ld>",
                                                                                                        LT_PLT_HANDLE(hDataSocket),
                                                                                                        LT_Ps32(sent));
                Iperf3_Client_Util_DestroyDataStream(pDataSock);
            }
            return;
        } else if (sent > 0) {
            pDataSock->nIterationSent += sent;
            S.mutex->API->Lock(S.mutex);
            pClient->nTotalSent += sent;
            S.mutex->API->Unlock(S.mutex);
            if (!pClient->bTestDone && pDataSock->nIterationSent >= pClient->params.mss) {
                if (pDataSock->nIterationSent > pClient->params.mss) {
                    LTLOG("clnt.data.wrt.over", "pClient->nIterationSent %lld", LT_Ps64(pDataSock->nIterationSent));
                }
                pDataSock->nIterationSent = 0;
            }
        }
        if (sent == 0 && !pClient->params.udp) {
            return; // TCP: kLTSocket_Event_WriteReady will wake us up again
        }
        if ((sent == 0 && pClient->params.udp) || (--max_burst == 0)) {
            // UDP doesn't get any WriteReady socket event so we just sleep a bit
            // We also sleep a bit if we hit max_burst to allow other event to be handled
            S.iThread->SetTimer(S.iThread->GetCurrentThread(), LTTime_Milliseconds(1),
                              Iperf3_Client_DataTimerEventProc, NULL, Iperf3_Client_Util_GetDataStreamSock(hDataSocket, pClient));
            return;
        }
    }
}

static void Iperf3_Client_CleanUp(IperfClientInfo * pClient) {
    if (Iperf3_Client_Util_GetActiveDataStreams(pClient) == 0) {
        ClientDataStream *stream = pClient->stream_list;
        while(stream) {
            ClientDataStream *next = stream->next;
            S.iThread->KillTimer(stream->hThread, Iperf3_Client_DataTimerEventProc, stream);
            S.iThread->Destroy(stream->hThread);
            lt_free(stream);
            stream = next;
        }

        S.iThread->KillTimer(pClient->common.hThread, Iperf3_Client_TestTimerProc, pClient);
        if (pClient->common.hControlSocket) {
            ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, pClient->common.hControlSocket);
            if (iSocket) {
                iSocket->DisconnectSocket(pClient->common.hControlSocket);
                iSocket->Destroy(pClient->common.hControlSocket);
            }
            pClient->common.hControlSocket = 0;
        }
        S.iThread->Destroy(pClient->common.hThread);

        lt_free(pClient->common.data);
        lt_memset(&s_client_info, 0, sizeof(s_client_info));
    }
}

static u32 Iperf3_Client_Util_GetActiveDataStreams(IperfClientInfo* pClient) {
    u32 ret = 0;
    for(ClientDataStream *stream = pClient->stream_list; stream != NULL; stream = stream->next) {
        if (stream->hDataSocket != 0) {
            ret++;
        }
    }
    return ret;
}

static void Iperf3_Client_Util_DestroyDataStream(ClientDataStream *stream) {
    if (stream == NULL) {
        return;
    }
    if (stream->hDataSocket) {
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, stream->hDataSocket);
        iSocket->DisconnectSocket(stream->hDataSocket);
        iSocket->Destroy(stream->hDataSocket);
        stream->hDataSocket = 0;
    }
}

static ClientDataStream *Iperf3_Client_Util_GetDataStreamSock(LTSocket hSocket, IperfClientInfo* pClientThread) {
    if (hSocket == 0 || pClientThread == NULL) {
        return NULL;
    }
    for(ClientDataStream *sock = pClientThread->stream_list; sock != NULL; sock = sock->next) {
        if (sock->hDataSocket == hSocket) {
            return sock;
        }
    }
    return NULL;
}
/* Iperf3 Client Implementation End */

/* Iperf3 Server Implementation Start */
static void Iperf3_Server_DataSocketDataReady(LTSocket hDataSocket, void * pClientData) {
    ServerDataClient   * pClient = (ServerDataClient *)pClientData;
    IPerfServerInfo     * pServer = pClient->pServer;
    if (pClient == NULL || pClient->hDataSocket == 0 || hDataSocket != pClient->hDataSocket) {
        return;
    }
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);

    //LTLOG("srvr.data.arr", "new data arrived, %ld", LT_Ps32(iSocket->ReadSocket(hDataSocket, NULL, 0)));

    if (pServer->state == kIPerfState_CREATE_STREAMS) {
        if (iSocket->ReadSocket(hDataSocket, NULL, 0) < 37) return;
        iSocket->ReadSocket(hDataSocket, pServer->common.uuid, 37);
        LTLOG("srvr.data.uuid", "%s\n", pServer->common.uuid);
    } else {
        s64 recved_bytes = 0;
        while ((recved_bytes = iSocket->ReadSocket(hDataSocket, pServer->common.data, kWriteSize)) > 0) {
            pClient->clientStat.receivedBytes += recved_bytes;
        }
    }
}

static void Iperf3_Server_Util_SendUDPConnectMessage(IPerfServerInfo *pServerInfo, LTNetIpv4Endpoint ep) {
    LTSocket hDataSocket = pServerInfo->hUdpSocket;
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);
    iSocket->SetProperty(hDataSocket, "send.endpoint.v4", &ep);
    char *msg = UDP_CONNECT_REPLY;
    iSocket->WriteSocket(hDataSocket, msg, 4);
}

static void Iperf3_Server_ShowProgressTimerProc(void * arg) {
    LT_UNUSED(arg);
    IPerfServerInfo *pServer = (IPerfServerInfo *)&s_server_info;
    LTTime now = LT_GetCore()->GetKernelTime();
    for(ServerDataClient *pClient = pServer->pClient; pClient != NULL; pClient = pClient->next) {
        IperfStreamStat *pStat = &pClient->clientStat;
        LTTime duration = LTTime_Subtract(now, LTTime_Nanoseconds(pStat->startTimeSys));
        u32 sec = LTTime_GetSeconds(duration);
        LTLOG("svr.report", "[%lu - %lu sec] stream: %lu, tput: %.2f Mbit/sec", LT_Pu32(sec - 1), LT_Pu32(sec), LT_Pu32(pClient->streamId), GET_TPUT_REPORT(pStat->receivedBytes, LTTime_GetSeconds(duration)));
    }
}

static void Iperf3_Server_UdpDataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pServerInfo) {
    IPerfServerInfo * pServer = (IPerfServerInfo *)pServerInfo;
    if (pServer == NULL || pServer->hUdpSocket == 0 || hDataSocket != pServer->hUdpSocket) {
        LTLOG("udp.srvr.data.evt.err", "event Error");
        return;
    }

    if (event != kLTSocket_Event_ReadReady && event != kLTSocket_Event_WriteReady) {
        LTLOG("udp.srvr.data.evt", "hDataSock: %lx, event: %lx", LT_Pu32(hDataSocket), LT_Pu32(event));
    }
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hDataSocket);
    if (event == kLTSocket_Event_SocketReady) {
        pServer->state = kIPerfState_CREATE_STREAMS;
        iSocket->WriteSocket(pServer->common.hControlSocket, &pServer->state, 1);
    } else if (event == kLTSocket_Event_ReadReady) {
        if (pServer->state == kIPerfState_CREATE_STREAMS) {
            if (iSocket->ReadSocket(hDataSocket, pServer->common.data, kWriteSize) > 0) {
                LTNetIpv4Endpoint ep;
                iSocket->GetProperty(hDataSocket,"recv.endpoint.v4", &ep);
                LTLOG_DEBUG("recv.endpoint.v4", "ipv4: %08lx, port: %d", LT_Pu32(ep.address), ep.port);
                Iperf3_Server_Util_SendUDPConnectMessage(pServer, ep);
                ServerDataClient *pClient = (ServerDataClient *)lt_malloc(sizeof(ServerDataClient));
                lt_memset(pClient, 0, sizeof(ServerDataClient));
                pClient->streamId = pServer->nConnectedClient;
                pClient->endPoint = ep;
                pClient->clientStat.startTimeSys = LT_GetCore()->GetKernelTime().nNanoseconds;
                pClient->next = pServer->pClient;

                pServer->pClient = pClient;
                pServer->nConnectedClient++;
            }
            if (pServer->nClient == pServer->nConnectedClient) {
                pServer->state = kIPerfState_TEST_START;
                iSocket->WriteSocket(pServer->common.hControlSocket, &pServer->state, 1);
                pServer->state = kIPerfState_TEST_RUNNING;
                iSocket->WriteSocket(pServer->common.hControlSocket, &pServer->state, 1);
                S.iThread->SetTimer(pServer->common.hThread, LTTime_Seconds(1), Iperf3_Server_ShowProgressTimerProc, NULL, NULL);
            }
        } else {
            s32 recv_size = 0;
            while ((recv_size = iSocket->ReadSocket(hDataSocket, pServer->common.data, kWriteSize)) > 0) {
                LTNetIpv4Endpoint ep;
                iSocket->GetProperty(hDataSocket,"recv.endpoint.v4", &ep);
                for (ServerDataClient *pClient = pServer->pClient; pClient; pClient=pClient->next) {
                    if (pClient->endPoint.address == ep.address && pClient->endPoint.port == ep.port) {
                        LTTime now = LT_GetCore()->GetKernelTime();
                        pClient->clientStat.receivedBytes += recv_size;
                        IperfFrame *frame = (IperfFrame *)pServer->common.data;
                        frame->frameNo = LT_HTONL(frame->frameNo);
                        pClient->clientStat.frames = frame->frameNo;
                        pClient->clientStat.packets++;

                        LTTime t = LTTime_Add(LTTime_Seconds(frame->sec), LTTime_Microseconds(frame->usec));
                        s64 receivedPeerTime_curr = t.nNanoseconds;
                        if (pClient->clientStat.startTimePeer == 0) {
                            pClient->clientStat.startTimePeer = receivedPeerTime_curr;
                        } else {
                            s64 timeDiff_peer = receivedPeerTime_curr - pClient->clientStat.startTimePeer;
                            s64 timeDiff_sys = now.nNanoseconds - pClient->clientStat.startTimeSys;
                            pClient->clientStat.latency += timeDiff_sys - timeDiff_peer;
                            pClient->clientStat.errors += (frame->frameNo - pClient->clientStat.lastRecvedFrameNo) - 1;
                            pClient->clientStat.jitters += (now.nNanoseconds - pClient->clientStat.lastRecvedSysTime);
                        }
                        pClient->clientStat.lastRecvedFrameNo = frame->frameNo;
                        pClient->clientStat.lastRecvedPeerTime = receivedPeerTime_curr;
                        pClient->clientStat.lastRecvedSysTime = now.nNanoseconds;
                    }
                }
            }
        }
    }
}

static void Iperf3_Server_DataSocketProc(void *pClientData) {
    ServerDataClient   * pClient = (ServerDataClient *)pClientData;
    ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, pClient->hDataSocket);
    iSocket->OnSocketEvent(pClient->hDataSocket, Iperf3_Server_DataSocketEventProc, pClient);
    Iperf3_Server_DataSocketDataReady(pClient->hDataSocket, pClientData);
}
static void Iperf3_Server_DataSocketReadProc(void *pClientData) {
    ServerDataClient   * pClient = (ServerDataClient *)pClientData;
    Iperf3_Server_DataSocketDataReady(pClient->hDataSocket, pClientData);
}
static void Iperf3_Server_DataSocketEventProc(LTSocket hDataSocket, LTSocket_Event event, void * pClientData) {
    ServerDataClient   * pClient = (ServerDataClient *)pClientData;
    if (pClient == NULL || pClient->hDataSocket == 0 || hDataSocket != pClient->hDataSocket) {
        return;
    }

    if (event != kLTSocket_Event_ReadReady && event != kLTSocket_Event_WriteReady) {
        LTLOG_DEBUG("srvr.data.evt", "hDataSock: %lx, event: %lx", LT_Pu32(hDataSocket), LT_Pu32(event));
    }

    if (event == kLTSocket_Event_Disconnected) {
        ;// Handled when the control socket is closed
    } else if (event == kLTSocket_Event_ReadReady) {
        S.iThread->QueueTaskProcIfRequired(pClient->hThread, Iperf3_Server_DataSocketReadProc, NULL, pClientData);
    }
}

static void
Iperf3_Server_ControlSocketDataReady(LTSocket hControlSocket, void * pClientData) {
    IPerfServerInfo * pServer = (IPerfServerInfo *)pClientData;
    if (pServer->common.hControlSocket == 0 || hControlSocket != pServer->common.hControlSocket) {
        LTLOG("srvr.ctrl.done", "%p, sock: %lu", pServer->pClient, LT_Pu32(pServer->common.hControlSocket));
        return;
    }
    ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, hControlSocket);

    switch (pServer->state) {
    case kIPerfState_INITIALIZE: {
        LTLOG_DEBUG("srvr.ctrl.state", "kIPerfState_INITIALIZE");
        if (iSocket->ReadSocket(hControlSocket, NULL, 0) < 37) return;
        iSocket->ReadSocket(hControlSocket, pServer->common.uuid, 37);
        pServer->common.uuid[36] = 0;
        LTLOG_DEBUG("srvr.ctrl.uuid", "%s\n", pServer->common.uuid);

        // Set up for PARAM_EXCHANGE state
        pServer->state       = kIPerfState_PARAM_EXCHANGE;
        // Notify client that we are ready for its parameters
        iSocket->WriteSocket(hControlSocket, &pServer->state, 1);
        break;
    }

    case kIPerfState_PARAM_EXCHANGE: {
        LTLOG_DEBUG("srvr.ctrl.state", "kIPerfState_PARAM_EXCHANGE");
        if (pServer->client_param_size == 0) {
            u32 param_size = 0;
            if (iSocket->ReadSocket(hControlSocket, NULL, 0) < (int)sizeof(param_size)) return;

            iSocket->ReadSocket(hControlSocket, &param_size, sizeof(param_size));
            param_size = LT_HTONL(param_size);
            LTLOG("srvr.param.recv", "param size: %llu", LT_Pu64(param_size));
            pServer->client_param_size = param_size;
        }
        LTLOG("srvr.param.recv", "receiving client parameters");
        char *param = lt_malloc(pServer->client_param_size + 4);
        lt_memset(param, 0, pServer->client_param_size + 4);
        char *ptr = param;
        u32 received_size = 0;
        while (iSocket->ReadSocket(hControlSocket, NULL, 0) > 0 &&
            pServer->client_param_size > received_size) {
            // Discard the parameters for now.
            u32 read = pServer->client_param_size - received_size;
            if (read > 32) read = 32;
            read = iSocket->ReadSocket(hControlSocket, ptr, read);
            received_size += read;
            ptr = ptr + read;
        };

        if (received_size == pServer->client_param_size) {
            LTLOG_DEBUG("srvr.rcvd.params", "Received %lu bytes\n", LT_Pu32(received_size));
            LTLOG_DEBUG("param", "%s", param);

            LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);
            LTUtilityJsonParser_Value value;
            parser->API->GetValue(parser, param, "parallel", &value);
            pServer->nClient = 1;
            if (value.type == kLTUtilityJsonParser_ValueType_Integer) {
                pServer->nClient = value.integer;
            }

            parser->API->GetValue(parser, param, "time", &value);
            if (value.type == kLTUtilityJsonParser_ValueType_Integer) {
                LTLOG("srvr", "time: %lld", value.integer);
                S.iThread->SetTimer(pServer->common.hThread, LTTime_Seconds(value.integer + 2), LTNetIperf3Impl_KillServer, NULL, NULL);
            }

            parser->API->GetValue(parser, param, "tcp", &value);
            if (value.type == kLTUtilityJsonParser_ValueType_True) {
                pServer->mode = kIPerfServerMode_TCP;
                // Set up for CREATE_STREAMS state
                pServer->state       = kIPerfState_CREATE_STREAMS;
                iSocket->WriteSocket(hControlSocket, &pServer->state, 1);
            } else if (value.type == kLTUtilityJsonParser_ValueType_KeyNotFound ||
                       value.type == kLTUtilityJsonParser_ValueType_False) {
                pServer->mode = kIPerfServerMode_UDP;
                pServer->streamId = 1;
                // An UDP client is connected
                char *spec = lt_malloc(kSocketSpecSize);
                lt_snprintf(spec, kSocketSpecSize, "udp myport: %lu", LT_Pu32(pServer->params.port));
                LTLOG("srvr.udp.sock", "Socket Creating, spec: %s", spec);
                pServer->hUdpSocket = S.pNetCore->OpenSocket(0, spec, Iperf3_Server_UdpDataSocketEventProc, (void *)pServer);
                if (pServer->hUdpSocket == 0) {
                    LTLOG_YELLOWALERT("srvr.udp.sock.fail", "spec: %s", spec);
                    LTNetIperf3Impl_KillServer(NULL);
                }

            }
        }
        lt_free(param);
        break;
    }
    case kIPerfState_TEST_RUNNING: {
        LTLOG_DEBUG("srvr.ctrl.state", "kIPerfState_TEST_RUNNING");
        u8 data;
        iSocket->ReadSocket(hControlSocket, &data, 1);
        if (data == kIPerfState_TEST_END) {
            LTLOG_DEBUG("srvr.test.end", "Client sent end of test");
            iSocket->DisconnectSocket(hControlSocket);
            pServer->state = kIPerfState_EXCHANGE_RESULTS;
            pServer->nResultsSize = 0;
            iSocket->WriteSocket(hControlSocket, &pServer->state, 1);
        }
        break;
    }
    case kIPerfState_EXCHANGE_RESULTS: {
        LTLOG_DEBUG("srvr.ctrl.state", "kIPerfState_EXCHANGE_RESULTS");
        if (pServer->nResultsSize == 0) {
            LTLOG_DEBUG("srvr.res.recv.size", "receiving client results size");
            u32 len;
            if (iSocket->ReadSocket(hControlSocket, NULL, 0) < (int)sizeof(len)) return;
            iSocket->ReadSocket(hControlSocket, &len, sizeof(len));
            pServer->nResultsSize = LT_NTOHL(len);
            pServer->nResultsReceived = 0;
            pServer->pResults = lt_malloc(pServer->nResultsSize);
        }
        if (pServer->nResultsReceived < pServer->nResultsSize) {
            while (iSocket->ReadSocket(hControlSocket, NULL, 0) > 0) {
                u32 rcvd = iSocket->ReadSocket(hControlSocket,
                                            pServer->pResults + pServer->nResultsReceived,
                                            pServer->nResultsSize - pServer->nResultsReceived);
                pServer->nResultsReceived += rcvd;
                LTLOG_DEBUG("srvr.res.recv.prog", "rcvd %lu, tot %lu",
                    LT_Pu32(rcvd), LT_Pu32(pServer->nResultsReceived));
                if (pServer->nResultsReceived == pServer->nResultsSize) break;
            }
        }
        if (pServer->nResultsReceived == pServer->nResultsSize) {
            LTLOG_DEBUG("json.peer", "%s", pServer->pResults);
            char *ptr = pServer->pResults;

            /* Find Stream IDs in Json from Peer */
            ServerDataClient *pClient = pServer->pClient;
            while((ptr = lt_strstr(ptr, "\"id\":")) != NULL) {
                LTLOG_DEBUG("id", "%s", ptr + 5);
                pClient->streamId = lt_strtou32(ptr + 5, NULL, 0);
                pClient = pClient->next;
                ptr+=5;
            }

            lt_free(pServer->pResults);
            char *result_json = TCP_EMPTY_RESULTS_JSON;
            LTTime now = LT_GetCore()->GetKernelTime();
            LTTime duration = LTTime_Subtract(now, LTTime_Nanoseconds(pServer->pClient->clientStat.startTimeSys));
            u64 totalReceivedBytes = 0;
            if (pServer->mode == kIPerfServerMode_UDP) {
                ServerDataClient *pClient = pServer->pClient;
                char *result_stream_json = lt_malloc(RESULT_JSON_STREAM_MAX_SIZE * pServer->nClient);
                lt_memset(result_stream_json, 0, RESULT_JSON_STREAM_MAX_SIZE * pServer->nClient);
                LT_SIZE offset = 0;
                u64 totalErrors = 0;
                u64 totalFrames = 0;
                LTLOG_DEBUG("streams", "pClient: %p, pServer->nClient: %lu", pClient, LT_Pu32(pServer->nClient));
                for (u32 id = 1; pClient && id <= pServer->nClient; id++) {
                    IperfStreamStat *pClientStat = &pClient->clientStat;
                    LTTime duration_msec = LTTime_Subtract(duration, LTTime_Seconds(LTTime_GetSeconds(duration)));

                    char *tmp = lt_malloc(RESULT_JSON_STREAM_MAX_SIZE);
                    lt_snprintf(tmp, RESULT_JSON_MAX_SIZE - 1, RESULT_JSON_STREAM_FORMAT, pClient->streamId,
                                                                                            pClientStat->receivedBytes,
                                                                                            pClientStat->errors,
                                                                                            pClientStat->frames,
                                                                                            GET_JITTER_REPORT(pClientStat->jitters, pClientStat->packets),
                                                                                            LTTime_GetSeconds(duration), LTTime_GetMilliseconds(duration_msec));
                    LTLOG("svr.udp.result.stream", "stream: %lu, tput: %.2f Mbits/sec", LT_Pu32(pClient->streamId),
                                                                    GET_TPUT_REPORT(pClientStat->receivedBytes, LTTime_GetSeconds(duration)));
                    totalReceivedBytes += pClientStat->receivedBytes;
                    totalErrors += pClientStat->errors;
                    totalFrames += pClientStat->frames;
                    u32 len = lt_strlen(tmp);
                    LTLOG_DEBUG("svr.result.json.stream", "len: %lu, %s", LT_Pu32(len), tmp);
                    if (id > 1) {
                        lt_memcpy(result_stream_json + offset, ", ", 2);
                        offset += 2;
                    }
                    lt_memcpy(result_stream_json + offset, tmp, len);
                    lt_free(tmp);
                    offset += len;
                    pClient = pClient->next;
                }
                LTLOG("svr.udp.result.total", "duration: %llu sec, transfer: %llu Bytes, bitrate: %.2f Mbits/sec, lost: %llu / %llu", LTTime_GetSeconds(duration),
                                                                                                                    totalReceivedBytes,
                                                                                                                    GET_TPUT_REPORT(totalReceivedBytes, LTTime_GetSeconds(duration)),
                                                                                                                    totalErrors, totalFrames
                                                                                                                    );
                result_json = lt_malloc(RESULT_JSON_MAX_SIZE);
                lt_memset(result_json, 0, RESULT_JSON_MAX_SIZE);
                lt_snprintf(result_json, RESULT_JSON_MAX_SIZE - 1, RESULTS_JSON_FORMAT, result_stream_json);
                lt_free(result_stream_json);
            } else if (pServer->mode == kIPerfServerMode_TCP){
                /* TCP report */
                for (ServerDataClient *pClient = pServer->pClient; pClient; pClient = pClient->next) {
                    totalReceivedBytes = pClient->clientStat.receivedBytes;
                }
                LTLOG("svr.tcp.result.total", "duration: %llu sec, transfer: %llu Bytes, bitrate: %.2f Mbits/sec", LTTime_GetSeconds(duration),
                                                                                                                    totalReceivedBytes,
                                                                                                                    GET_TPUT_REPORT(totalReceivedBytes, LTTime_GetSeconds(duration))
                                                                                                                    );
            }
            LTLOG_DEBUG("svr.result.json", "size: %u, %s", lt_strlen(result_json), result_json);
            u32 len = lt_strlen(result_json);
            // send server results length
            u32 nlen = LT_HTONL(len);
            iSocket->WriteSocket(hControlSocket, &nlen, 4);
            // send server results
            iSocket->WriteSocket(hControlSocket, result_json, len);
            // send new state
            pServer->state = kIPerfState_DISPLAY_RESULTS;
            iSocket->WriteSocket(hControlSocket, &pServer->state, 1);
            if (result_json && pServer->mode == kIPerfServerMode_UDP) {
                lt_free(result_json);
            }
        }
        break;
    }
    case kIPerfState_NONE:
        break;
    case kIPerfState_DISPLAY_RESULTS: {
        LTLOG_DEBUG("srvr.ctrl.state", "kIPerfState_DISPLAY_RESULTS");
        u8 data;
        iSocket->ReadSocket(hControlSocket, &data, 1);
        if (data == kIPerfState_IPERF_DONE) {
            LTLOG("server.term", "Iperf Server is terminated");
            LTNetIperf3Impl_KillServer(NULL);
        }
        break;
    }

    default:
        break;
    }
}

static void
Iperf3_Server_ControlSocketEventProc(LTSocket hControlSocket,
                             LTSocket_Event event,
                             void * pClientData) {
    IPerfServerInfo *pServer = (IPerfServerInfo *)pClientData;
    if (pServer->common.hControlSocket == 0 || hControlSocket != pServer->common.hControlSocket) {
        return;
    }

    LTLOG_DEBUG("srvr.ctrl.evt", "hControlSocket: %lx, Event: %lx", LT_Pu32(hControlSocket), LT_Pu32(event));

    if (event == kLTSocket_Event_Disconnected) {
        Iperf3_Server_CloseClientSockets();
    } else if (event == kLTSocket_Event_ReadReady) {
        Iperf3_Server_ControlSocketDataReady(hControlSocket, pClientData);
    }
}

static void
Iperf3_Server_ListenSocketEventProc(LTSocket hListenSocket,
                            LTSocket_Event event,
                            void * pClientData) {
    IPerfServerInfo * pServer = pClientData;

    if (event != kLTSocket_Event_ReadReady && event != kLTSocket_Event_WriteReady) {
        LTLOG_DEBUG("srvr.lstn.evt", "hListenSocket: %lx, Event: %lx", LT_Pu32(hListenSocket), LT_Pu32(event));
    }

    if (event == kLTSocket_Event_Connected) {
        ILTSocket *iSocket = lt_gethandleinterface(ILTSocket, hListenSocket);
        LTSocket hSocket = hListenSocket;
        if (pServer->common.hControlSocket != 0) {
            LTLOG("sock.datasock", "Data Sock: %lu", LT_Pu32(hSocket));
            ServerDataClient *pClient = lt_malloc(sizeof(ServerDataClient));
            lt_memset(pClient, 0, sizeof(ServerDataClient));
            // for progress report
            pClient->streamId = pServer->nConnectedClient;
            pClient->clientStat.startTimeSys = LT_GetCore()->GetKernelTime().nNanoseconds;

            pClient->hDataSocket = hSocket;
            pClient->pServer = pServer;
            pClient->next = pServer->pClient;
            pServer->pClient = pClient;
            char name[20];
            lt_snprintf(name, 20, "Iperf_Recv_%05X", hSocket);
            pClient->hThread = S.pCore->CreateThread(name);
            S.iThread->SetStackSize(pClient->hThread, 2048);
            S.iThread->SetPriority(pClient->hThread, pServer->params.prio);
            S.iThread->Start(pClient->hThread, NULL, NULL);
            S.iThread->QueueTaskProc(pClient->hThread, Iperf3_Server_DataSocketProc, NULL, pClient);

            pServer->nConnectedClient++;
            if (pServer->nClient == pServer->nConnectedClient) {
                // Transition to TEST_START state
                pServer->state = kIPerfState_TEST_START;
                iSocket->WriteSocket(pServer->common.hControlSocket, &pServer->state, 1);

                // Transition to TEST_RUNNING state
                pServer->state = kIPerfState_TEST_RUNNING;
                iSocket->WriteSocket(pServer->common.hControlSocket, &pServer->state, 1);
                S.iThread->SetTimer(pServer->common.hThread, LTTime_Seconds(1), Iperf3_Server_ShowProgressTimerProc, NULL, NULL);
            }
        } else {
            pServer->state = kIPerfState_INITIALIZE;
            pServer->common.hControlSocket = hSocket;
            LTLOG("sock.ctrlsock", "hControlSocket: %lu", LT_Pu32(pServer->common.hControlSocket));
            iSocket->OnSocketEvent(hSocket, Iperf3_Server_ControlSocketEventProc, pServer);
            Iperf3_Server_ControlSocketDataReady(hSocket, pServer);
        }
    }
}

static void Iperf3_Server_RunServer(void * pServerData) {
    IPerfServerInfo * pServer = pServerData;

    char *spec = lt_malloc(kSocketSpecSize);
    if (!spec) return;
    lt_snprintf(spec, kSocketSpecSize, "tcp listen addr-reuse port: %lu", LT_Pu32(pServer->params.port));

    pServer->hListenSocket = S.pNetCore->OpenSocket(0, spec, Iperf3_Server_ListenSocketEventProc, pServer);
    if (pServer->hListenSocket == 0) {
        LTLOG_YELLOWALERT("srvr.listen.sock.fail", "spec: %s", spec);
        LTNetIperf3Impl_KillServer(NULL);
    }
    LTLOG("sock.listen", "listen socket: %lu", LT_Pu32(pServer->hListenSocket));
    if (pServer->common.callback) {
        if (pServer->hListenSocket) {
            pServer->common.callback("server_ready", pServer->common.callback_arg);
        } else {
            pServer->common.callback("Error: server_not_ready", pServer->common.callback_arg);
        }
    }
    lt_free(spec);
}

static void Iperf3_Server_CloseClientSockets(void) {
    for (ServerDataClient *ptr = s_server_info.pClient; ptr; ptr = ptr->next) {
        if (ptr->hDataSocket == 0) {
            continue;
        }
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, ptr->hDataSocket);
        iSocket->DisconnectSocket(ptr->hDataSocket);
        iSocket->Destroy(ptr->hDataSocket);
        ptr->hDataSocket = 0;
    }
    if (s_server_info.common.hControlSocket) {
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, s_server_info.common.hControlSocket);
        iSocket->DisconnectSocket(s_server_info.common.hControlSocket);
        iSocket->Destroy(s_server_info.common.hControlSocket);
        s_server_info.common.hControlSocket = 0;
    }
    if (s_server_info.hUdpSocket) {
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, s_server_info.hUdpSocket);
        iSocket->DisconnectSocket(s_server_info.hUdpSocket);
        iSocket->Destroy(s_server_info.hUdpSocket);
        s_server_info.hUdpSocket = 0;
    }
    if (s_server_info.hListenSocket) {
        ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, s_server_info.hListenSocket);
        iSocket->DisconnectSocket(s_server_info.hListenSocket);
        iSocket->Destroy(s_server_info.hListenSocket);
        s_server_info.hListenSocket = 0;
    }
}
/* Iperf3 Server Implementation End */

/* Iperf3 Library Interface Implementation */
void LTNetIperf3Impl_RunClient(const char *ipaddr, LTNetIperf3_Parameters *p, LTNetIperf3_ReportCallback *cb, void *cb_arg) {
    IperfClientInfo  *pClient   = &s_client_info;
    lt_memset(pClient, 0, sizeof(IperfClientInfo));

    // Copy supplied parameters, using defaults for all that are 0
    pClient->params = LTNetIperf3_ParametersDefault;
    if (p != NULL) {
        if (p->port) pClient->params.port = p->port;
        if (p->time) pClient->params.time = p->time;
        if (p->udp) {
            pClient->params.udp = p->udp;
            pClient->params.bitrate = (p->bitrate)?p->bitrate:kUdpBitrate;
        } else {
            pClient->params.bitrate = (p->bitrate)?p->bitrate:IPERF_UNLIMITED_BITRATE;
        }
        if (p->dscp)       pClient->params.dscp = p->dscp;
        if (p->prio)       pClient->params.prio = p->prio;
        if (p->mss)        pClient->params.mss = p->mss;
        if (p->streams)    pClient->params.streams = p->streams;
    }
    lt_strncpyTerm(pClient->server, ipaddr, sizeof(pClient->server));
    pClient->common.callback = cb;
    pClient->common.callback_arg = cb_arg;
    if (p->mss == 0) {
        pClient->params.mss = kWriteSize;
    }
    if (pClient->params.streams == 0) {
        pClient->params.streams = 1;
    }
    pClient->common.data = lt_malloc(pClient->params.mss);
    for (u32 i = 0; i < pClient->params.mss; i++) pClient->common.data[i] = i & 0xff;

    LTLOG("clnt.lnch.cfg", "thread priority: %d, dscp: %d, mss: %d, bitrate: %lu", pClient->params.prio,
                                                                    pClient->params.dscp,
                                                                    pClient->params.mss,
                                                                    LT_Pu32(pClient->params.bitrate));

    pClient->common.hThread = LT_GetCore()->CreateThread("Iperf_Client");
    S.iThread->SetPriority(pClient->common.hThread, p->prio);
    S.iThread->SetStackSize(pClient->common.hThread, 2048);
    S.iThread->Start(pClient->common.hThread, NULL, NULL);
    S.iThread->QueueTaskProc(pClient->common.hThread, Iperf3_Client_RunClient, NULL, (void *)pClient);
}

static void LTNetIperf3Impl_KillClient(void *pReason) {
    IperfClientInfo * pClient = &s_client_info;
    LTLOG("clnt.kill", "Killing Clients: %s", (pReason)?(char*)pReason:"Timer");

    if (pClient->common.hThread) {
        S.iThread->KillTimer(pClient->common.hThread, LTNetIperf3Impl_KillClient, NULL);
    }
    pClient->params.time = 0;
    for(ClientDataStream *stream = pClient->stream_list; stream != NULL; stream = stream->next) {
        if (stream->hDataSocket) {
            ILTSocket * iSocket = lt_gethandleinterface(ILTSocket, stream->hDataSocket);
            if (iSocket) {
                iSocket->DisconnectSocket(stream->hDataSocket);
                iSocket->Destroy(stream->hDataSocket);
            }
        stream->hDataSocket = 0;
        }
    }
    Iperf3_Client_CleanUp(pClient);
}

void LTNetIperf3Impl_RunServer(LTNetIperf3_Parameters *p, LTNetIperf3_ReportCallback *cb, void *cb_arg) {
    LTNetIperf3Impl_KillServer(NULL);
    s_server_info.params = LTNetIperf3_ParametersDefault;
    if (p && p->port) s_server_info.params.port = p->port;
    s_server_info.common.data = lt_malloc(kWriteSize);
    s_server_info.common.callback = cb;
    s_server_info.common.callback_arg = cb_arg;
    s_server_info.common.hThread = S.pCore->CreateThread("Iperf3_Server");
    S.iThread->SetStackSize(s_server_info.common.hThread, 2048);
    S.iThread->Start(s_server_info.common.hThread, NULL, NULL);
    S.iThread->QueueTaskProc(s_server_info.common.hThread, Iperf3_Server_RunServer, NULL, &s_server_info);
}

static void LTNetIperf3Impl_KillServer(void *pClientData) {
    LT_UNUSED(pClientData);
    LTLOG("srvr.kill", "Killing server");
    if (s_server_info.common.hThread) {
        S.iThread->KillTimer(s_server_info.common.hThread, Iperf3_Server_ShowProgressTimerProc, NULL);
    }
    Iperf3_Server_CloseClientSockets();
    if (s_server_info.common.hThread) {
        S.iThread->Destroy(s_server_info.common.hThread);
    }

    ServerDataClient *ptr = s_server_info.pClient;
    while (ptr != NULL) {
        ServerDataClient *next = ptr->next;
        S.iThread->Destroy(ptr->hThread);
        lt_free(ptr);
        ptr = next;
    }
    lt_free(s_server_info.common.data);
    lt_memset(&s_server_info, 0, sizeof(IPerfServerInfo));
}

define_LTLIBRARY_ROOT_INTERFACE(LTNetIperf3) {
    .RunClient = LTNetIperf3Impl_RunClient,
    .RunServer = LTNetIperf3Impl_RunServer,
    .KillClient = LTNetIperf3Impl_KillClient,
    .KillServer = LTNetIperf3Impl_KillServer,
} LTLIBRARY_DEFINITION;
