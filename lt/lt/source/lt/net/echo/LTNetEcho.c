/*******************************************************************************
 *
 * LTNetEcho.c - ICMP Echo (Ping) Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/echo/LTNetEcho.h>
#include <lt/utility/ipaddress/LTUtilityIPAddress.h>

DEFINE_LTLOG_SECTION("net.echo");

/*******************************************************************************
** Constant and Type Definitions
*******************************************************************************/

typedef struct IPv4Header {        // as defined for IPv4 without options
    u8  verlen;
    u8  tos;
    u16 len;
    u16 id;
    u16 frag;
    u8  ttl;
    u8  proto;
    u16 csum;
    u32 saddr;
    u32 daddr;
} IPv4Header;

typedef struct EchoHeader {        // as defined for ICMP ECHO
    u8  type;
    u8  code;
    u16 chksum;
    u16 id;
    u16 seqno;
} EchoHeader;

typedef struct {
    LTHandle         echo_handle;  // used for DestroyHandle within event handler
    LTSocket         hSocket;      // also for DestroyHandler
    u16              pingLimit;    // max number of pings
    u16              pingTxCount;  // current ping counter
    u16              pingRxCount;  // current ping counter
    u16              periodMsec;   // ping repeat period in msec
    u16              timeoutMsec;  // final rx ping timeout in msec
    u16              payloadSize;  // size of ping payload section
    u16              packetSize;   // size of ping header + payload
    u16              replySize;    // size of IPv4 header + ping header + payload
    u8              *replyPacket;  // holds the ping reply
    LTNetEcho_Proc  *eventFunc;    // event callback function
    u8              *eventData;    // event callback data
    EchoHeader       header;       // icmp header
    u8               payload[4];   // payload section (expands to payloadSize)
} EchoData;

static struct Statics {
    LTCore    *iCore;
    ILTThread *iThread;
    LTNetCore *iNetCore;
} S;

/*******************************************************************************
** Special Utilities
*******************************************************************************/

static u16 ChecksumIcmp(const void *dataptr, u32 len) {
    // A refactored lwip_standard_chksum (inet_chksum.c)
    u32 acc = 0;
    u16 src;
    const u8 *dp = (const u8*)dataptr;
    for (; len > 1; len -= 2) {
        src  = *dp++ << 8; // most significant first (network order)
        src |= *dp++;
        acc += src;
    }
    if (len > 0) { // extra byte
        src = *dp << 8;
        acc += src;
    }
    // add deferred carry bits:
    acc = (acc >> 16) + (acc & 0x0000ffffUL);
    if ((acc & 0xffff0000UL) != 0) acc = (acc >> 16) + (acc & 0x0000ffffUL);
    return ~LT_HTONS(acc);
}

static bool GetGateway(LTTransport hTransport, char *ip) { // ip str must be sized for max address
    char spec[64];
    S.iNetCore->GetTransportSpec(hTransport, spec, sizeof(spec));
    char *s = lt_strstr(spec, "gw: ");
    if (!s) return false;
    for (s += 4; *s && *s != ' '; s++) *ip++ = *s;
    *ip = 0;
    return true;
}

/*******************************************************************************
** Special Utilities
*******************************************************************************/

static bool SendPing(EchoData *edata) {
    if (edata->pingTxCount >= edata->pingLimit) return false;
    edata->pingTxCount++;
    edata->header.seqno  = LT_HTONS(edata->pingTxCount);
    edata->header.chksum = 0;
    edata->header.chksum = ChecksumIcmp(&edata->header, edata->packetSize);
    //LTLOG("send", "send socket: %04lx ping: %d size: %d", LT_Pu32(edata->hSocket), edata->pingTxCount, edata->packetSize);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, edata->hSocket);
    if (pSocket->WriteSocket(edata->hSocket, &edata->header,
                             edata->packetSize) != edata->packetSize) {
        return false;
    }
    edata->eventFunc(edata->echo_handle, 0, edata->pingTxCount, edata->eventData);
    return true;
}

static void OnTimerProc(void* data) {
    EchoData *edata = (EchoData *)data;

    if (edata->pingTxCount == edata->pingLimit) {
        LTLOG_SERVER("ping.tmo", "Timeout stats, tx:%d, rx:%d", edata->pingTxCount, edata->pingRxCount);
    }

    if (!SendPing(edata)) {
        S.iCore->DestroyHandle(edata->echo_handle);
        S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimerProc, edata);
        return;
    }

    // After tx last ping, set timeout
    if (edata->pingTxCount == edata->pingLimit) {
        //LTLOG("timer.wait", "OnTimerProc final wait for ping Rx to trickle in");
        S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimerProc, edata);
        S.iThread->SetTimer(S.iThread->GetCurrentThread(),
                            LTTime_Milliseconds(edata->timeoutMsec), OnTimerProc, NULL, edata);
    }
}

static void ReadEchoReply(LTSocket hSocket, EchoData *edata)
{
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, edata->hSocket);
    s32 len;
    while ((len = pSocket->ReadSocket(hSocket, edata->replyPacket, edata->replySize))) {
        if (len != edata->replySize) {
            LTLOG("read.err", "ReadSocket() returned %ld", LT_Pu32(len));
            break;
        }

        IPv4Header *IPv4hdr = (IPv4Header *) edata->replyPacket;
        EchoHeader *reply = (EchoHeader *) (IPv4hdr + 1);
        if (reply->type == 0 && reply->code == 0 && reply->id == edata->header.id) {
            edata->pingRxCount++;

            LTLOG("ping.rx", "type:%d, code:%d, src ip:0x%lx, dst ip:0x%lx",
                    reply->type, reply->code, LT_Pu32(IPv4hdr->saddr), LT_Pu32(IPv4hdr->daddr));

            edata->eventFunc(edata->echo_handle, LT_NTOHS(reply->id),
                             LT_NTOHS(reply->seqno), edata->eventData);
            if (edata->pingRxCount >= edata->pingLimit) {
                S.iCore->DestroyHandle(edata->echo_handle);
                break;
            }
        } else {
            LTLOG("icmp.err", "Unrecognized ICMP received from "LT_PRIIP
                  " ICMP type: %d, code: %d, id: %d, seqno: %d",
                  LT_PLT_IP(IPv4hdr->saddr), reply->type, reply->code,
                  LT_NTOHS(reply->id), LT_NTOHS(reply->seqno));
        }
    }
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *data) {
    //LTLOG("event", "socket[%04lx] event: 0x%lx\n", LT_Pu32(hSocket), LT_Pu32(event));
    EchoData *edata = (EchoData *)data;
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (!edata->pingTxCount) { // get it started
                if (!SendPing(data)) S.iCore->DestroyHandle(edata->echo_handle);
                else S.iThread->SetTimer(S.iThread->GetCurrentThread(),
                    LTTime_Milliseconds(edata->periodMsec), OnTimerProc, NULL, edata);
            }
            break;
        case kLTSocket_Event_ReadReady: {
            ReadEchoReply(hSocket, edata);
            break;
        }
        default: break;
    }
}

/*******************************************************************************
** API Function Definitions
*******************************************************************************/

static void LTNetEcho_Destroy(LTHandle handle) {
    EchoData *edata = S.iCore->ReserveHandlePrivateData(handle);
    S.iThread->KillTimer(S.iThread->GetCurrentThread(), OnTimerProc, edata);
    S.iCore->DestroyHandle(edata->hSocket);
    lt_free(edata->replyPacket);
    S.iCore->ReleaseHandlePrivateData(handle, edata);
}

// Interface used for echo handles contains nothing unique:
typedef_LTLIBRARY_INTERFACE(INetEcho, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(INetEcho, LTNetEcho_Destroy) {} LTLIBRARY_DEFINITION;

static bool LTNetEcho_Ping(LTTransport hTransport, const char *remoteAddress, u16 id, u16 count,
        u16 period, u8 *payload, u16 payloadSize, LTNetEcho_Proc eventFunc, void *eventData) {

    LTHandle    hEcho        = 0;
    char       *spec         = NULL;
    EchoData   *edata        = NULL;

    const char *reason; // yellow alert failure reason
    do {
        // Handle target spec, if none, use gateway address:
        char ip[16];

        // Handle payload, if none, make it "Roku":
        reason = "payload size";
        if (payloadSize > LTNetEcho_MaxPayload) break;
        if (!payload) { // use default
            payload = (u8*)"Roku";
            payloadSize = 4;
        }

        // Create socket spec for the given ping target:
        LT_SIZE len = 0;
        LT_SIZE size = 0;
        char *prefix[] = {"icmp ip: ", "icmp host: "};
        char **pprefix;

        // Ping gateway when NULL
        if (remoteAddress == NULL) {
            len = lt_strlen(prefix[0]);
            pprefix = &prefix[0];

            reason = "no ip or no default gateway";
            if (!GetGateway(hTransport, ip)) { break; }
            remoteAddress = ip;
        } else {
            LTIPAddress dummyIp;
            LTUtilityIPAddress *ipUtil = lt_openlibrary(LTUtilityIPAddress);

            if (ipUtil && ipUtil->StringToIPAddress(remoteAddress, &dummyIp)) {
                len = lt_strlen(prefix[0]);
                pprefix = &prefix[0];
            } else {
                len = lt_strlen(prefix[1]);
                pprefix = &prefix[1];
            }
            lt_closelibrary(ipUtil);
        }

        reason = "spec oom";
        size = len + lt_strlen(remoteAddress) + 1;
        spec = lt_malloc(size);
        if (!spec) break;

        lt_strncpyTerm(spec, *pprefix, size);
        lt_strncpyTerm(spec + len, remoteAddress, size - len);

        // Create the reply packet buffer:
        reason = "reply oom";
        u16 packetSize = sizeof(EchoHeader) + payloadSize;
        u16 replySize = packetSize + sizeof(IPv4Header);

        // Create an echo object handle:
        reason = "no handle";
        hEcho = S.iCore->CreateHandle((LTInterface*)&s_INetEcho, sizeof(EchoData) + payloadSize - 4);
        if (!hEcho) break;
        u8 *replyPacket = lt_malloc(replySize);
        if (!replyPacket) break;
        edata = S.iCore->ReserveHandlePrivateData(hEcho);
        *edata = (EchoData) { // clears unset fields
            .echo_handle  = hEcho,
            .pingLimit    = count,
            .periodMsec   = period ? period : 1000,
            .timeoutMsec  = 3000,
            .payloadSize  = payloadSize,
            .packetSize   = packetSize,
            .replySize    = replySize,
            .replyPacket  = replyPacket,
            .eventFunc    = eventFunc,
            .eventData    = eventData,
            .header = {
                .type     = 8, // ICMP_ECHO command
                .id       = LT_HTONS(id),
            }
        };
        lt_memcpy(&edata->payload, payload, payloadSize);
        S.iCore->ReleaseHandlePrivateData(hEcho, edata);

        // Open the socket and off we go:
        reason = "socket open";
        LTSocket hSocket = S.iNetCore->OpenSocket(hTransport, spec, OnSocketEvent, edata);
        if (!hSocket) break;
        edata->hSocket = hSocket;
        lt_free(spec);

        return true;
    } while (false);

    // Failure:
    LTLOG_YELLOWALERT("ping.fail", "init failure: %s", reason);
    S.iCore->DestroyHandle(hEcho); // also destroys socket, zero handle ok
    lt_free(spec);                 // null ok
    return false;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTNetEchoImpl_LibFini(void) {
    lt_closelibrary(S.iNetCore);
    S.iNetCore = NULL;
}

static bool LTNetEchoImpl_LibInit(void) {
    S = (struct Statics) {
        .iCore    = LT_GetCore(),
        .iThread  = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iNetCore = lt_openlibrary(LTNetCore)
    };
    if (!S.iNetCore) return false;
    return true;
}

/*******************************************************************************
 * Library Function Vectors
 ******************************************************************************/

define_LTLIBRARY_ROOT_INTERFACE(LTNetEcho,) {
    .Ping    = LTNetEcho_Ping
} LTLIBRARY_DEFINITION;
