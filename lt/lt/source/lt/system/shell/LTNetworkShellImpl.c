/******************************************************************************
 * lt/source/system/shell/LTNetworkShellImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/system/network/LTSystemNetwork.h>
#include <lt/system/network/LTSocket.h>
#include <lt/system/network/LTSocketInputBuffer.h>
#include <lt/system/network/LTSocketOutputBuffer.h>
#include "LTShellImpl.h"

#define ENABLE_BLOCK_HACK 1

enum {
    kListenPort            = 4444,
    kInputBufferSize       = 256,
    kOutputBufferSize      = 2048 /* = 16 * 128 */,
};

static const LTSocketSpec s_ListenSpec = {
    .device           = 0,
    .protocol         = kLTNetwork_ProtocolTCP,
    .local            = { { .nAddressNetEndian = 0 }, kListenPort },
    .remote           = { { .nAddressNetEndian = 0 }, 0 },
    .bServer          = true,
    .inputBufferSize  = kInputBufferSize,
    .outputBufferSize = kOutputBufferSize,
    .ttl              = 64,
    .raw_protocol     = kLTRawNetwork_ProtocolNone,
};

typedef struct {
    int info;
} ServerInfo;

typedef struct {
    LTSocket         hSocket;
    ShellPriv       *pShell;
    LTString         pBuffer;
} ConnectedClientInfo;

DEFINE_LTLOG_SECTION("sys.sh.net");

static LTCore                *s_pCore                = NULL;
static LTSystemNetwork       *s_pNetwork             = NULL;
static ILTSocket             *s_pSocket              = NULL;
static ILTThread             *s_pThread              = NULL;
static ILTSocketInputBuffer  *s_pInputBuffer         = NULL;
static ILTSocketOutputBuffer *s_pOutputBuffer        = NULL;
static LTSocket               s_hListenSocket        = 0;
static LTThread               s_hThread              = 0;
static ServerInfo            *s_pServerInfo          = NULL;
static bool                   s_bInitialized         = false;

static bool IsConsoleShell(ShellPriv *priv);
static void PutString(ShellPriv *priv, const char *str);
static void VPrint(ShellPriv *priv, const char *fmt, lt_va_list args);
static void PutChar(ShellPriv *priv, char c);
static void MoveCursorRelative(ShellPriv *priv, s32 incr);
static void EraseEntireLine(ShellPriv *priv);
static void UpdateToEndOfLine(ShellPriv *priv, const char *str);
static void FlushOutput(ShellPriv *pClient);
static void DestroyNetworkShell(void *pPriv);

bool LTNetworkShellImpl_SetConsolePrintState(LTShell hShell, bool turnon);

static ConnectedClientInfo *CreateClientInfo(LTSocket hSock) {
    ConnectedClientInfo *pInfo = lt_malloc(sizeof(ConnectedClientInfo));
    *pInfo = (ConnectedClientInfo){
        .hSocket = hSock,
        .pShell  = NULL,
        .pBuffer = NULL,
    };
    pInfo->pBuffer    = ltstring_create("");
    pInfo->pShell     = LTShellImpl_CreateShellPriv();
    ShellPriv *pShell = pInfo->pShell;
    pShell->pImpl                  = (void *) pInfo;
    pShell->IsConsoleShell         = IsConsoleShell;
    pShell->PutStringProc          = PutString;
    pShell->VPrintProc             = VPrint;
    pShell->PutCharProc            = PutChar;
    pShell->EchoCharProc           = PutChar;
    pShell->MoveCursorRelativeProc = MoveCursorRelative;
    pShell->EraseEntireLineProc    = EraseEntireLine;
    pShell->UpdateToEndOfLineProc  = UpdateToEndOfLine;
    pShell->Flush                  = FlushOutput;
    pShell->Destroy                = DestroyNetworkShell;
    /* LTSystemNetwork accepts writes only after the ConnectEvent is
       reported. ShellReady() dumps out a bunch of text. We may not
       be ready to log to the socket here. Don't call ShellReady()
       here. */
    return pInfo;
}

static void DestroyNetworkShell(void *pPriv) {
    ConnectedClientInfo *pInfo = (ConnectedClientInfo *) pPriv;
    LTNetworkShellImpl_SetConsolePrintState(pInfo->pShell->handle, false);
    s_pCore->Destroy(pInfo->hSocket);
    ltstring_destroy(pInfo->pBuffer);
    lt_free(pInfo);
}

static bool IsConsoleShell(ShellPriv *priv) { LT_UNUSED(priv); return false; }

static void WriteString(ConnectedClientInfo *pClient, const char *pData, u32 len) {
    LTSocketOutputBuffer hBuffer = s_pSocket->GetOutputBuffer(pClient->hSocket);
    #if ENABLE_BLOCK_HACK
    for (u32 sent = 0, iter = 0; sent != len; sent += iter) {
        iter = s_pOutputBuffer->WriteData(hBuffer, pData + sent, len - sent);
        if (iter == 0) s_pThread->Sleep(LTTime_Milliseconds(10));
    }
    #else
    s_pOutputBuffer->WriteData(hBuffer, pData, len);
    #endif
}

static void FlushOutput(ShellPriv *priv) {
    ConnectedClientInfo *pClient = (ConnectedClientInfo *) priv->pImpl;
    LTSocketOutputBuffer hBuffer = s_pSocket->GetOutputBuffer(pClient->hSocket);
    s_pOutputBuffer->NotifyWriteComplete(hBuffer);
}

static void PutString(ShellPriv *priv, const char *str) {
    ConnectedClientInfo *pClient = (ConnectedClientInfo *) priv->pImpl;
    WriteString(pClient, str, lt_strlen(str));
    s_pOutputBuffer->NotifyWriteComplete(s_pSocket->GetOutputBuffer(((ConnectedClientInfo *)priv->pImpl)->hSocket));
}

static void VPrint(ShellPriv *priv, const char *fmt, lt_va_list args) {
    ConnectedClientInfo *pClient = (ConnectedClientInfo *) priv->pImpl;
    ltstring_vformat(pClient->pBuffer, fmt, args);
    WriteString(pClient, pClient->pBuffer, lt_strlen(pClient->pBuffer));
    ltstring_empty(pClient->pBuffer);
}

static void PutChar(ShellPriv *priv, char c) {
    s_pOutputBuffer->WriteData(s_pSocket->GetOutputBuffer(((ConnectedClientInfo *)priv->pImpl)->hSocket), &c, 1);
}

static void MoveCursorRelative(ShellPriv *priv, s32 incr) {
    if (incr) {
        char buff[16];
        int nLen = lt_snprintf(buff, sizeof(buff), "\x1b[%ld%c", LT_Ps32((incr < 0) ? -incr : incr), (incr < 0) ? 'D' : 'C');
        if (nLen && nLen < (int)sizeof(buff)) {
            WriteString((ConnectedClientInfo *)priv->pImpl, buff, nLen);
            s_pOutputBuffer->NotifyWriteComplete(s_pSocket->GetOutputBuffer(((ConnectedClientInfo *)priv->pImpl)->hSocket));
        }
    }
}

static void EraseEntireLine(ShellPriv *priv) {
    static const char kEraseEntireLineSequence[] = "\x1b[2K";
    WriteString((ConnectedClientInfo *)priv->pImpl, kEraseEntireLineSequence, sizeof(kEraseEntireLineSequence) - 1);
    s_pOutputBuffer->NotifyWriteComplete(s_pSocket->GetOutputBuffer(((ConnectedClientInfo *)priv->pImpl)->hSocket));
}

static void UpdateToEndOfLine(ShellPriv *priv, const char *str) {
    static const char kSaveCursorAndEraseToEOLSequence[] = "\x1b\x37\x1b[0K";
    static const char kRestoreCursorSequence[] = "\x1b\x38";
    ConnectedClientInfo *pClient = (ConnectedClientInfo *)priv->pImpl;
    WriteString(pClient, kSaveCursorAndEraseToEOLSequence, sizeof(kSaveCursorAndEraseToEOLSequence) - 1);
    if (str && *str) WriteString(pClient, str, lt_strlen(str));
    WriteString(pClient, kRestoreCursorSequence, sizeof(kRestoreCursorSequence) - 1);
    s_pOutputBuffer->NotifyWriteComplete(s_pSocket->GetOutputBuffer(((ConnectedClientInfo *)priv->pImpl)->hSocket));
}

static void LTNetworkShellImpl_LogEntryEventProc(LTLogEntry *hLogEntries, u32 nNumLogEntries, void *pClientData) {
    LT_UNUSED(hLogEntries); LT_UNUSED(nNumLogEntries); LT_UNUSED(pClientData);
    #if 0
    ConnectedClientInfo *pInfo = (ConnectedClientInfo *) pClientData;
    WriteString(pInfo, pString, length);
    LTSocketOutputBuffer hBuffer = s_pSocket->GetOutputBuffer(pInfo->hSocket);
    s_pOutputBuffer->NotifyWriteComplete(hBuffer);
    #endif
}

static void ClientSocketDataArrived(LTSocket hSocket, LTSocketInputBuffer hBuffer, void *pClientData) {
    LT_UNUSED(hSocket);
    ConnectedClientInfo *pClient = (ConnectedClientInfo *)pClientData;
    LTString str = ltstring_create("");
    u32 buffered = s_pInputBuffer->GetBufferedAmount(hBuffer);
    s_pInputBuffer->ReadString(hBuffer, str, buffered);
    LTShellImpl_OnCharactersReceived(pClient->pShell, str, buffered);
    FlushOutput(pClient->pShell);
    lstring_destroy(str);
}

static void ClientSocketEventProc(LTSocket hSocket, LTNetworkEvent event, LTNetworkResult result, void *pClientData) {
    LT_UNUSED(hSocket);
    LT_UNUSED(result);
    ConnectedClientInfo *pClient = (ConnectedClientInfo *)pClientData;
    LTLOG("clnt.evt", "ClientSocketEventProc - hSocket %lx, event %s, result %s, pClientData %p\n",
          LT_PLT_HANDLE(hSocket), s_pNetwork->EventToString(event), s_pNetwork->ResultToString(result), pClientData);
    if (result == kLTNetwork_ResultOk) {
        switch (event) {
        case kLTNetwork_EventError:
            LTLOG("clnt.evt.err", "error on client connection\n");
            break;
        case kLTNetwork_EventConnect:
            /* client is connected */
            LTLOG("clnt.evt.conn", "client connected\n");
            /* LTSystemNetwork accepts writes only after this event is reported. ShellReady() dumps out a bunch of text. */
            LTShellImpl_ShellReady(pClient->pShell);
            s_pSocket->OnDataArrived(hSocket, ClientSocketDataArrived, pClient);
            break;
        case kLTNetwork_EventDisconnect:
            /* client is disconnected */
            LTLOG("clnt.evt.dscn", "client disconnected\n");
            s_pCore->Destroy(pClient->pShell->handle);
            break;
        case kLTNetwork_EventFindDone:
        case kLTNetwork_EventConnectRequest:
        case kLTNetwork_EventIPAddress:
        case kLTNetwork_EventNone:
        case kLTNetwork_EventReady:
        case kLTNetwork_EventRemoteClosed:
            /* Nothing to do ... ignore */
            break;
        }
    } else if (result == kLTNetwork_ResultNotConnected) {
        if (event == kLTNetwork_EventConnect) {
            LTLOG("clnt.evt.conn.nok", "client connect error\n");
            /* error in connection state */
            s_pCore->Destroy(pClient->pShell->handle);
        }
    }
}

static void ListenerSocketEventProc(LTSocket hSocket, LTNetworkEvent event, LTNetworkResult result, void *pClientData) {
    LT_UNUSED(result);
    LT_UNUSED(pClientData);
    if (event == kLTNetwork_EventConnectRequest) {
        LTLOG("svr.conn.req", "Connection request\n");
        LTSocket hNewSocket = s_pSocket->ConnectSocket(hSocket, NULL);
        ConnectedClientInfo *pClientInfo = CreateClientInfo(hNewSocket);
        s_pSocket->OnNetworkSocketEvent(hNewSocket, ClientSocketEventProc, pClientInfo);
    }
}

static void NetworkConfigEventProc(LTNetworkEvent event, LTNetworkResult result, void *pData) {
    LT_UNUSED(result); LT_UNUSED(pData);
    LTLOG("net.ev", "event %ld, result %ld\n", LT_Ps32(event), LT_Ps32(result));
    if (event == kLTNetwork_EventIPAddress) {
        LTLOG("net.rdy", "Network shell ready\n");
        LT_ASSERT(result == kLTNetwork_ResultOk);
        /* Network is now up, create the socket to listen for connect requests: */
        s_hListenSocket = s_pSocket->CreateSocket(&s_ListenSpec, ListenerSocketEventProc, s_pServerInfo);
    }
}

static bool LTNetworkShellImpl_ThreadInitProc(void) {
    /* Wait for network to come up before creating listen socket: */
    s_pNetwork->OnNetworkConfigEvent(NetworkConfigEventProc, NULL, NULL);
    return true;
}

static void LTNetworkShellImpl_ThreadExitProc(void) {
    if (s_hListenSocket == 0) return;
    s_pSocket->Destroy(s_hListenSocket);
    s_hListenSocket = 0;
}

bool LTNetworkShellImpl_StartNetworkShellSubsystem(void) {
    if (s_bInitialized) return true;        /* DRW 14-Aug-21 : no thread safety needed - this function either
                                               gets called from LibInt() (currently commented out) or a Shell command.
                                               If LibInit, it will be fully run before it can be run from a shell command.
                                               If from a shell command it will be run fully from the console shell before
                                               a telnet shell is operable where it might be run again, but return early.
                                               LTNetworkShellImpl needs to be enhanced to support proper teardown
                                               so that LTSystemShell library shutdown will work properly. */
    do {
        s_pCore        = LT_GetCore();
        s_pThread      = lt_getlibraryinterface(ILTThread, s_pCore);
        s_pNetwork     = lt_openlibrary(LTSystemNetwork);
        if (!s_pNetwork) break;
        s_pOutputBuffer = lt_getlibraryinterface(ILTSocketOutputBuffer, s_pNetwork);
        s_pInputBuffer  = lt_getlibraryinterface(ILTSocketInputBuffer, s_pNetwork);
        s_pSocket       = lt_getlibraryinterface(ILTSocket, s_pNetwork);
        s_pServerInfo  = lt_malloc(sizeof(ServerInfo));
        if (!s_pServerInfo) break;
        s_hThread      = s_pCore->CreateThread("NetworkShell");
        s_pThread->SetStackSize(s_hThread, 1536);
        s_pThread->SetPriority(s_hThread, kLTThread_PriorityLowest);
        s_pThread->Start(s_hThread, LTNetworkShellImpl_ThreadInitProc,
                                    LTNetworkShellImpl_ThreadExitProc);
        LTLOG("init", "LTNetworkShell initialized\n");
        s_bInitialized = true;
        return true;
    } while (0);
    if (s_pServerInfo) lt_free(s_pServerInfo);
    lt_closelibrary(s_pNetwork);
    LTLOG("init.fail", "LTNetworkShell initialization failed\n");
    s_pNetwork = NULL;
    s_pCore    = NULL;
    return false;
}

void LTNetworkShellImpl_StopNetworkShellSubsystem(void) {
    s_pThread->Destroy(s_hThread);
    s_hThread       = 0;
    lt_free(s_pServerInfo);
    s_pServerInfo   = NULL;
    s_pThread       = NULL;
    s_pOutputBuffer = NULL;
    s_pInputBuffer  = NULL;
    s_pSocket       = NULL;
    lt_closelibrary(s_pNetwork);
    s_pNetwork      = NULL;
    s_pCore         = NULL;
    s_bInitialized  = false;
}

bool LTNetworkShellImpl_LibInit(void) { return LTNetworkShellImpl_StartNetworkShellSubsystem(); }

void LTNetworkShellImpl_LibFini(void) { LTNetworkShellImpl_StopNetworkShellSubsystem(); }
