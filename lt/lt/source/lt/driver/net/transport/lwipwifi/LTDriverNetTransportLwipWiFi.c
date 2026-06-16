/*******************************************************************************
 *
 * CommonDriverNetIpWifi - Driver for LwIP using WiFi
 * --------------------------------------------------
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
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

// What's needed for LwIP access:
#include <ctype.h>
#include <string.h>
#include "lwipopts.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/dhcp.h"
#include "netif/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/ethip6.h"
#include "lwip/prot/dhcp.h"
#include "lwip/dns.h"

#ifndef LT_WIFI_DRV_MEMPOOL
#define LT_WIFI_DRV_MEMPOOL 0
#endif

DEFINE_LTLOG_SECTION("net.ip");
// DBLOG expose debug log in release and debug builds
//#define DBLOG(...) LTLOG_STOMP(__VA_ARGS__)
#define DBLOG(...)

#define TRANSPORT_THREAD_STACK_SIZE	1792

/*******************************************************************************
** Debug and Code Documenting Definitions
*******************************************************************************/

#define PUB static // Part of public interface
#define CBK static      // Callback function
#define FWD static      // Forward reference
#define LOC static      // Local primary function
#define UTL static      // Local utility function
#define EXP             // Exported
#ifndef LT_LWIP_MEMP_STAT
#define LT_LWIP_MEMP_STAT 0
#endif
/*******************************************************************************
** Local Constants and Definitions
*******************************************************************************/

typedef enum {
    kIPProtocol_Unknown,
    kIPProtocol_Dns,
    kIPProtocol_Raw,
    kIPProtocol_Icmp,
    kIPProtocol_Udp,
    kIPProtocol_TcpClient,
    kIPProtocol_TcpServer,
    kIPProtocol_Max
} IPProtocol;

typedef enum {
    kSocketFlags_NoConnect = (1 << 0), // socket does not connect (UDP and TCP listen)
    kSocketFlags_KeepAlive = (1 << 1), // socket keepalive enabled (TCP)
    kSocketFlags_AddrReuse = (1 << 2), // socket so_reuse enabled (TCP)
} SocketFlags;

static const char *IPProtocolNames[] = { // must match above enum
    "?",
    "dns",
    "raw",
    "icmp",
    "udp",
    "tcp",
    "tcp listen",
    "tls",
};

typedef struct {
    void             *payload;
    LTNetIpv4Endpoint endpoint;
} RecvPayload;

/** indicates this pbuf payload points to a RecvPayload struct */
#define PBUF_FLAG_RECV_PAYLOAD    0x80U

// Transport driver data is stored in its LTNetCore handle allocation
typedef struct Priv_Tran {
    LTTransportData      *tranData;
    LTTransportDriver    *driverData;
    struct netif          netif;          // LwIP network interface
    LTSocket_Create      *createSocket;   // Needed for listen to create new sockets (could expand to interface)
    LTTransport_Metrics   metrics;        // Basic RX/TX metrics
    LTThread              hThread;
    ip4_addr_t            ipAddr;         // Our address
    ip4_addr_t            ipMask;
    ip4_addr_t            ipGate;
    ip4_addr_t            ipDns;
    u32                   nextRecvPayload;
    RecvPayload           recvPayloadPool[PBUF_POOL_SIZE];
    LTDeviceUnit          wifiUnit;
    bool                  wifiEnabled;
    bool                  linkConnected;
    bool                  dhcpActive;
    bool                  dhcpEnabled;
    bool                  debug;            // !!! change this to multilevel
    u32                   socketCount;      // number of open sockets for this transport
    u32                   openCount;
} Priv_Tran;

// Compilation assert that there is adequate space for above struct.
// If this fails, expand the Priv_Tran "space" array size
#ifndef LT_PBUF_POOL_SIZE
int Priv_TranTooBig[(sizeof(Priv_Tran)) > (sizeof(PrivImpl)) ? -1 : 1];
#endif

#define GET_NETIF_PRIV_TRAN(n) ((Priv_Tran*)netif_get_client_data(n, LWIP_NETIF_CLIENT_DATA_INDEX_MAX));

// This drivers's private data as stored within the socket handle:
typedef struct Priv_Sock {
    Priv_Tran         *privTran;      // transport for this socket
    LTSocket_Data     *socketData;    // points back up to handle data (switch to offset???)
    union {                           // LwIP control blocks:
      void            *anyPcb;        // Use lwip naming for these fields
      struct ip_pcb   *ipPcb;
      struct tcp_pcb  *tcpPcb;
      struct udp_pcb  *udpPcb;
      struct raw_pcb  *rawPcb;
    };
    struct pbuf       *rxPbuf;        // receive packet buffer chain
    ip4_addr_t         ipAddr;        // remote ip, or local ip for listen
    u16                ipPort;        // remote port, or local port for listen
    u16                myPort;        // remote port, or local port for listen
    ip4_addr_t         ipAllow;       // accept connections from this address only
    LTNetIpv4Endpoint  recvAddr;      // sender address of last received packet (UDP and RAW)
    char              *hostname;      // for "host:" spec DNS lookup
    IPProtocol         ipProto;       // IP protocol, eg TCP UDP ...
    u8                 dscp;          // Differentiated Services Codepoint
    u8                 tcpPrio;        // tcp socket priority
    bool               isOpen;        // socket open was successful
    bool               connected;     // socket is connected
    bool               dnsTimer;      // DNS timer is active
    SocketFlags        flags;         // socket flags
} Priv_Sock;

#define GET_SOCKET_PRIV(s) (Priv_Sock*)(s + 1)  // driver private socket data

#define LOCK_API        lt_lwip_lock()
#define UNLOCK_API      lt_lwip_unlock()
#define LOCK_NET(code)  LOCK_API; code; UNLOCK_API

#define LTNET_TRANSPORT_DATA_KEY "Net_Tr"

#define DNS_TIMEOUT_SECONDS 5

#define METRICS_GUARD(expr) {       \
    S.iMetricsMutex->API->Lock(S.iMetricsMutex);   \
    expr;                          \
    S.iMetricsMutex->API->Unlock(S.iMetricsMutex); \
}

/*******************************************************************************
** Module Variables
*******************************************************************************/
#define MAX_TRANSPORT_COUNT 1
#define GET_NETIF(T) (&(T)->netif)
static struct Statics {
    LTCore         *iCore;
    ILTThread      *iThread;
    ILTEvent       *iEvent;
    LTDeviceWiFi   *iWiFi;
    LTMutex        *iMetricsMutex;       // mutex for metrics
    u32             lastDnsErrorLogTime; // time of last Dns error log time, used to filter error log spam. Thread unsafe is fine here.
    Priv_Tran       transportPool[MAX_TRANSPORT_COUNT];
} S;
static Priv_Tran *getNewTransport(void) {
    for (u32 i = 0; i < MAX_TRANSPORT_COUNT; i++) {
        if (S.transportPool[i].openCount == 0) {
            return &S.transportPool[i];
        }
    }

    return NULL;
}

/*******************************************************************************
** Basic Utility Functions
******************************************************************************/
static void NetIpWiFi_ShowLwipStat(LTTransportDriver *data, bool logToServer) {
    LT_UNUSED(data);

    // Probably need to permanently enable this flag ??
#if LT_LWIP_MEMP_STAT
    stats_display(logToServer);
#else
    LT_UNUSED(logToServer);
#endif
    return;
}

UTL char *AppendStr(const char *from, char *to, const char *end) { // LT has no strcat
    if (!to)   return NULL;
    if (!from) return to;
    u32 len = lt_strlen(from);
    if (to + len > end) return NULL;
    lt_memcpy(to, from, len+1);
    return to + len;
}

UTL char *IpToStr(const ip_addr_t *addr, char *to, char *end) {
    char out[IPADDR_STRLEN_MAX];
    ipaddr_ntoa_r(addr, out, IPADDR_STRLEN_MAX);
    out[IPADDR_STRLEN_MAX-1] = 0; // safety
    return AppendStr(out, to, end);
}

UTL char *FormU32(char* buf, u16 max, u32 value) {  // LT has no u32tostr (itoa)
    char str[12];
    u16 i = 0;
    if (value == 0) {
        if (max > 0) {
            str[0] = '0';
            i = 1;
        }
    } else while (value && i < 11) {
        str[i++] = '0' + value % 10;
        value /= 10;
    }
    while (i > 0 && --max > 0) *buf++ = str[--i];
    *buf = 0;
    return buf;
}

UTL char *FormIp4Addr(const ip_addr_t *ip) { // WARNING: for debug use -- must copy result!
    if (!ip) return "B.A.D.!";
    static char str[IPADDR_STRLEN_MAX];
    IpToStr(ip, str, str + IPADDR_STRLEN_MAX - 1);
    return str;
}

/*******************************************************************************
** Debug Helpers
*******************************************************************************/

#define TBUG_INIT(s) Priv_Tran *tran = s->privTran; LT_UNUSED(tran);
#define TBUG if (tran->debug) LT_GetCore()->ConsoleStomp
#define BUG LT_GetCore()->ConsoleStomp


UTL void DumpBytes(const char *label, u32 len, u8 *bytes) {
    BUG("%s: %lu bytes: ", label, LT_Pu32(len));
    for (u32 i = 0; i < (64 < len ? 64 : len); i++) BUG("%02x ", bytes[i]);
    BUG("\n");
}

UTL void DumpPbufInfo(const char *label, struct pbuf *pbuf) {
    BUG("--- %s pbuf %p, payld %p totlen %d len %d type %x flags %x ref %x idx %d\n", label, pbuf,
        pbuf->payload, pbuf->tot_len, pbuf->len, pbuf->type_internal, pbuf->flags, pbuf->ref, pbuf->if_idx);
}

/*******************************************************************************
** WiFi Driver Data Interface to LwIP
*******************************************************************************/

#define MAX_WIFI_BUFS 4  // Add to transport config???

// Convert LwIP buffers to LT buffers for WiFi output:
CBK err_t OnWiFiOutput(struct netif *netif, struct pbuf *pbuf) {
    // WiFi is not up.
    if (!S.iWiFi || !netif) return ERR_CLSD;

    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    if (!tran || !GET_NETIF(tran) || !tran->openCount) return ERR_CLSD;
    LTBufferChain wifiBuffers[MAX_WIFI_BUFS];
    LTBufferChain *wbuf = wifiBuffers;
    u32 total_len = 0;
    u8 *pb = (u8*)(pbuf->payload);
    int n;
    for (n = 0; pbuf && n < MAX_WIFI_BUFS; pbuf = pbuf->next, n++) {
        // PR("%s: [%d] len: %u\n", __FUNCTION__, n, pbuf->len);
        wbuf->buffer    = pbuf->payload;
        wbuf->size      = pbuf->len;
        wbuf->bytesUsed = pbuf->len;
        wbuf->next      = &wifiBuffers[n+1];
        wbuf            = wbuf->next;
        total_len       += (u32)pbuf->len;
    }
    wifiBuffers[n-1].next = NULL; // terminate chain, note: n is never 0
    DBLOG("wf.tx", "WiFi transmit: %lu bytes in %d bufs (thread %lx)\n", LT_Pu32(total_len), n, LT_PLT_HANDLE(S.iThread->GetCurrentThread()));
    if (tran->debug) DumpBytes("wifi out", (total_len > 40) ? 40 : total_len, pb);
    if (!S.iWiFi->TransmitFrames(tran->wifiUnit, wifiBuffers)) {
        METRICS_GUARD(tran->metrics.dropPacketsTx++; tran->metrics.dropPacketsTx_shadow++);
        return ERR_IF;
    }
    METRICS_GUARD(tran->metrics.lowerBytesTx += total_len);
    return ERR_OK;
}

// Convert LT WiFi input to LwIP buffers:
CBK bool OnWiFiInput(LTBufferChain *wifiBuffer, void *clientData) {
    static LTTime rx_time_prev;
    if (!clientData) return false;
    struct netif *netif = (struct netif *)clientData;
    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    u32 len = wifiBuffer->bytesUsed;
    if (tran->debug) DumpBytes(__FUNCTION__, len, wifiBuffer->buffer);
    LT_ASSERT(wifiBuffer->next == NULL);
    LT_ASSERT(len <= (u32)LT_U16_MAX);
    // Copy WiFi buffer into LwIP buffer:
    LOCK_API;
    struct pbuf *pbuf = pbuf_alloc(PBUF_RAW, (u16)len, PBUF_POOL);
    DBLOG("wf.rx", "WiFi receive: %lu bytes in %p buf (thread %lx)\n", LT_Pu32(len), pbuf, LT_PLT_HANDLE(S.iThread->GetCurrentThread()));
    if (pbuf) pbuf_take(pbuf, wifiBuffer->buffer, len); // never fails because alloc'd above
    #if LT_WIFI_DRV_MEMPOOL == 0
    // LT_WIFI_DRV_MEMPOOL is defined in LTProductConfig.json
    lt_free(wifiBuffer->buffer); // free LT mem before calling netif->input or on oom/drop
    #endif
    wifiBuffer->buffer = NULL;
    if (pbuf) {
        netif->input(pbuf, netif);
        METRICS_GUARD(tran->metrics.lowerBytesRx += len);
    } else {
        S.iMetricsMutex->API->Lock(S.iMetricsMutex);

        tran->metrics.dropPacketsRx++;
        tran->metrics.dropPacketsRx_shadow++;
        LTTime rx_now = LT_GetCore()->GetKernelTime();
        if (LTTime_IsGreaterThanOrEqual(LTTime_Subtract(rx_now, rx_time_prev), LTTime_Seconds(1))) {
            LTLOG_YELLOWALERT("in.oom.obuf", "out of buf mem, %lu", LT_Pu32(tran->metrics.dropPacketsRx));
            NetIpWiFi_ShowLwipStat(NULL, true);
            rx_time_prev = rx_now;
        }
        S.iMetricsMutex->API->Unlock(S.iMetricsMutex);
    }
    UNLOCK_API;
    return pbuf != NULL;
}

/*******************************************************************************
** Transport Event Handling
*******************************************************************************/

static void NotifyTransport(Priv_Tran *tran, LTTransport_Event event) {
    S.iEvent->NotifyEvent(tran->driverData->hEvent, 0, event);
}

LOC void NotifySocketEvent(Priv_Sock *sock, int event) {
    if (!sock || !sock->socketData) return;
    TBUG_INIT(sock);
    // P("--- sock %p, data %p, event %x, handle %x\n", sock, sock->socketData, sock->socketData->event, sock->socketData->h_socket);
    if ((event == kLTSocket_Event_ReadReady)  && !LTAtomic_CompareAndExchange(&sock->socketData->readPending, 0, 1))  return;
    if ((event == kLTSocket_Event_WriteReady) && !LTAtomic_CompareAndExchange(&sock->socketData->writePending, 0, 1)) return;
    S.iEvent->NotifyEvent(sock->socketData->event, sock->socketData->h_socket, event);
}

LOC Priv_Sock *NewSocket(struct tcp_pcb *pcb, Priv_Sock *parent) {
    Priv_Tran *tran = parent->privTran;
    LTSocket hSocket = tran->createSocket(tran->tranData, NULL, parent->socketData->event); // use same event handler as the listener
    if (!hSocket) return 0;
    LTSocket_Data *socketData = S.iCore->ReserveHandlePrivateData(hSocket);
    socketData->connected = true; // remove this as duplicate???
    tran->socketCount++;

    // Clone lower socket and update fields:
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    *sock = (Priv_Sock) {
        .privTran    = parent->privTran,
        .socketData  = socketData,
        .tcpPcb      = pcb,
        .ipAddr      = pcb->remote_ip,
        .ipPort      = pcb->remote_port,
        .myPort      = pcb->local_port,
        .ipProto     = kIPProtocol_TcpClient,
        .connected   = true
    };
    LOCK_NET(tcp_arg(pcb, sock));
    S.iCore->ReleaseHandlePrivateData(hSocket, socketData);
    return sock;
}

LOC bool RxAppendSocket(Priv_Sock *sock, struct pbuf *pbuf, LTNetIpv4Endpoint *senderAddr) {
    TBUG_INIT(sock);
    if (tran->debug) DumpPbufInfo(__FUNCTION__, pbuf);
    s32 maxPbufCount = PBUF_POOL_SIZE - ((sock->privTran->socketCount - 1) * 2);
    if (maxPbufCount < 1) maxPbufCount = 1;
    if (sock->rxPbuf && pbuf_clen(sock->rxPbuf) >= maxPbufCount) return false;
    if (senderAddr) {
        RecvPayload *payload = &sock->privTran->recvPayloadPool[sock->privTran->nextRecvPayload++];
        if (sock->privTran->nextRecvPayload >= PBUF_POOL_SIZE) sock->privTran->nextRecvPayload = 0;
        *payload = (RecvPayload) {
            .payload = pbuf->payload,
            .endpoint = *senderAddr
        };
        pbuf->payload = payload;
        pbuf->flags |= PBUF_FLAG_RECV_PAYLOAD;
    }
    if (!sock->rxPbuf) {
        sock->rxPbuf = pbuf;
    } else {
        pbuf_cat(sock->rxPbuf, pbuf);
    }
    NotifySocketEvent(sock, kLTSocket_Event_ReadReady);
    return true;
}

// LwIP requires this global function for events (rather than using an API to set it)
// NOTE: this callback is called with the API lock held, so no need to lock here.
EXP err_t lwip_tcp_event(void *arg, struct tcp_pcb *pcb, enum lwip_event event, struct pbuf *pbuf, u16_t size, err_t err) {
    // tcp is closed
    if (!arg) return ERR_ARG;
    LT_UNUSED(pcb);
    LT_UNUSED(size);
    LT_UNUSED(err);
    Priv_Sock *sock = (Priv_Sock*)arg;
    TBUG_INIT(sock);
#ifdef LT_DEBUG
        static const char *LwipEventNames[] =
            {"accept", "sent", "recv", "connected", "poll", "error"};  // enum lwip_event
        //u32 thread = S.iThread->GetCurrentThread(); // debug
        //u8 pri = S.iThread->GetPriority(thread);    // debug
        LT_UNUSED(LwipEventNames); // in case DBLOG is defined to DBLOG(...) and the line that uses LwipEventNames is elided
        if (event > LWIP_EVENT_ERR) return ERR_VAL; // clip it
        DBLOG("tcp.evt", "Event %s PCB: %p Pbuf: %p Size: %u Err %d (thread %lx)\n", LwipEventNames[event], pcb, pbuf, size, err, LT_PLT_HANDLE(S.iThread->GetCurrentThread()));
#endif
    switch (event) {
        case LWIP_EVENT_ACCEPT:
            if (err == ERR_OK) {
                if (!sock->ipAllow.addr || sock->ipAllow.addr == pcb->remote_ip.addr) {
                    Priv_Sock *nsock = NewSocket(pcb, sock); // New PCB, create a socket to match.
                    nsock->isOpen = true;
                    NotifySocketEvent(nsock, kLTSocket_Event_Connected);
                    NotifySocketEvent(nsock, kLTSocket_Event_WriteReady);
                } else tcp_close(pcb);
            }
            break;
        case LWIP_EVENT_SENT:
            TBUG("--- Sent %d bytes, pbuf %p\n", size, pbuf);
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;
        case LWIP_EVENT_RECV:
            if (!pbuf) {  // How LWIP indicates remote disconnect
                TBUG(("--- Disconnected!\n"));
                NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
                break;
            }
            if (!RxAppendSocket(sock, pbuf, NULL)) {
                return ERR_MEM;
            }
            break;
        case LWIP_EVENT_CONNECTED:
            TBUG("--- PCB IP state: %d, ip: %08lx, port %d, mss %d\n",
                sock->tcpPcb->state, LT_Pu32(sock->tcpPcb->local_ip.addr), sock->tcpPcb->local_port, sock->tcpPcb->mss);
            NotifySocketEvent(sock, kLTSocket_Event_Connected);
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady); // tcp_sndbuf! for size
            break;
        case LWIP_EVENT_POLL:
            break;
        case LWIP_EVENT_ERR:
            switch (err) {
                case ERR_ABRT:  /** Connection aborted.      */
                case ERR_RST:   /** Connection reset.        */
                case ERR_CLSD:  /** Connection closed.       */
                    if (sock->tcpPcb->state == SYN_SENT) {
                        NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                    }
                    NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
                    break;

                default:
                    LTLOG_YELLOWALERT("evt.err", "Transport driver event error: %d", err);
                    NotifySocketEvent(sock, kLTSocket_Event_Error);
                    break;
            }
            // On error the PCB is freed within LWIP and should not be referenced ever again
            sock->tcpPcb = NULL;
            break;
    }
    return ERR_OK;
}

// Append new data to the socket read buffer
CBK void OnUdpReceive(void *arg, struct udp_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *addr, u16 port) {
    LT_UNUSED(pcb);
    Priv_Sock *sock = (Priv_Sock*)arg;
    LTNetIpv4Endpoint endpoint = {
        .address = addr->addr,
        .port = port
    };
    if (!RxAppendSocket(sock, pbuf, &endpoint)) {
        pbuf_free(pbuf);
    }
}

CBK unsigned char OnRawReceive(void *arg, struct raw_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *addr) {
    LT_UNUSED(pcb);
    OnUdpReceive(arg, NULL, pbuf, addr, 0);
    return 1;
}

/*******************************************************************************
** Network Initialization
*******************************************************************************/

CBK err_t OnNetAdded(struct netif *netif) {
    // Note: WiFi may not be UP here, so don't depend on it.
    // LwIp will call "output" when a packet must be sent.
    netif->output = etharp_output;
    // LwIP ARP will call "linkoutput" when a packet must be sent.
    netif->linkoutput = OnWiFiOutput;

    // LwIP calls this anytime the interface is brought up and down.
    // netif_set_status_callback(&net_if, OnStatusChange);
    // void (*netif_status_callback_fn)(struct netif *netif);
    // Set the LWIP_NETIF_STATUS_CBK option in your lwipopts.h file.

    // Descriptive abbreviation and number (for if_api, netifapi_netif, IPV6 zones)
    netif->name[0]='l';
    netif->name[1]='w';
    netif->num='0';

    // Initial maximum transfer in bytes
    netif->mtu = 1500;

    // Special flags to enable broadcast address and ARP traffic
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    // Others: NETIF_FLAG_LINK_UP, NETIF_FLAG_MLD6; //Multicast listener discovery for IPv6

    etharp_init();
    //LTLOG_DEBUG("lwip.if.init", "if 0x%lx\n",(unsigned long) netif);
    return ERR_OK;
}

static const char *DhcpStateNames[] = {
    "off",
    "requesting",
    "init",
    "rebooting",
    "rebinding",
    "renewing",
    "selecting",
    "informing",
    "checking",
    "permanent",
    "bound",
    "releasing",
    "backing-off"
};

CBK void OnDhcpChange(struct netif *netif, u8_t oldState, u8_t newState) {
    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    LT_UNUSED(DhcpStateNames); // debug only
    LTLOG_DEBUG("dhcp.state", "DHCP %s -> %s\n", DhcpStateNames[oldState], DhcpStateNames[newState]);

    if (oldState != newState && newState == DHCP_STATE_SELECTING) {
        NotifyTransport(tran, kLTTransport_Event_DhcpStart);
    }

    if (oldState == newState && newState == DHCP_STATE_SELECTING) {
        NotifyTransport(tran, kLTTransport_Event_DhcpRetry);
    }
    if (oldState != newState && newState == DHCP_STATE_BOUND) {
        dhcp_supplied_address(netif);
        struct dhcp *dhcp = netif_dhcp_data(netif);
        LOCK_NET(netif_set_addr(netif, &dhcp->offered_ip_addr, &dhcp->offered_sn_mask, &dhcp->offered_gw_addr));
        tran->ipAddr = netif->ip_addr;
        tran->ipMask = netif->netmask;
        tran->ipGate = netif->gw;
        if (oldState == DHCP_STATE_RENEWING) {
            char offered_ip[IPADDR_STRLEN_MAX];
            char server_ip[IPADDR_STRLEN_MAX];
            ip4addr_ntoa_r(&dhcp->offered_ip_addr, offered_ip, IPADDR_STRLEN_MAX);
            ipaddr_ntoa_r(&dhcp->server_ip_addr, server_ip, IPADDR_STRLEN_MAX);
            LTLOG("dhcp", "lease of %s renewed from %s, lease time %lu",
                      offered_ip, server_ip, LT_Pu32(dhcp->offered_t0_lease));
        }
        #if 0
        PR(">>>>>>>> IP: %s ", FormIp4Addr(&tran->ipAddr));
        PR("GW: %s ",          FormIp4Addr(&tran->ipGate));
        PR("OIP: %s\n",        FormIp4Addr(&dhcp->offered_ipAddr));
        #endif
        tran->linkConnected = true;
        NotifyTransport(tran, kLTTransport_Event_Up);
    }
}

CBK void OnWiFiChange(LTDeviceWiFi_Status status,  LTDeviceUnit unit, void *clientData) {
    LT_UNUSED(clientData);
    LTLOG_DEBUG("wifi.stat", "WiFi status %d unit %d\n", (int)status, (int)unit);
    Priv_Tran *tran = S.iThread->GetThreadSpecificClientData(S.iThread->GetCurrentThread(),
                                                                LTNET_TRANSPORT_DATA_KEY);
    struct netif *netif = GET_NETIF(tran);
    if (!netif) return;
    switch (status) {
        case kLTDeviceWiFi_Status_Up: // Can now get MAC address and setup LWIP netif
            TBUG(("WiFi: up\n"));
            if (!tran->wifiUnit) {
                tran->wifiUnit = unit;
                netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_MAX) = tran;
                S.iWiFi->ReceiveFrame(unit, OnWiFiInput, netif);
                LTMacAddress address;
                S.iWiFi->GetMacAddress(&address);
                lt_memcpy(netif->hwaddr, address.octet, ETHARP_HWADDR_LEN);
                netif->hwaddr_len = ETHARP_HWADDR_LEN;
                LOCK_NET(netif_set_default(netif));
            }
            LOCK_NET(netif_set_up(netif));
            break;
        case kLTDeviceWiFi_Status_Connected:
            TBUG("WiFi: Connected: %d\n", tran->dhcpEnabled);
            LOCK_API;
            netif_set_link_up(netif);
            if (tran->dhcpEnabled) {
                // Assumes single thread accessor -- valid?
                tran->dhcpActive = true;
                dhcp_register_state_callback(netif, OnDhcpChange); // okay to call more than once
                dhcp_start(netif);
                UNLOCK_API;
            } else {
                dhcp_inform(netif); // Tell it what static IP we insist on using
                UNLOCK_API;
                tran->linkConnected = true;
                NotifyTransport(tran, kLTTransport_Event_Up);
            }
            S.iMetricsMutex->API->Lock(S.iMetricsMutex);
            u32 count = tran->metrics.connections;
            tran->metrics = (LTTransport_Metrics) { // reset metrics on new connection
                .connections = count + 1
            };
            S.iMetricsMutex->API->Unlock(S.iMetricsMutex);
            break;
        case kLTDeviceWiFi_Status_Disconnected:
        case kLTDeviceWiFi_Status_Down:
            TBUG(("WiFi: Disconnected or down\n"));
            LOCK_API;
            if (tran->dhcpActive) {
                tran->dhcpActive = false;
                dhcp_stop(netif);
                dhcp_cleanup(netif);
            }
            netif_set_link_down(netif);
            if (status == kLTDeviceWiFi_Status_Down) netif_set_down(netif);
            UNLOCK_API;
            if (tran->linkConnected) {
                tran->linkConnected = false;
                NotifyTransport(tran, kLTTransport_Event_Down);
            }
            break;
        case kLTDeviceWiFi_Status_LinkDisconnected:
            if (tran->linkConnected) {
                // Temporarily disconnected - rejoin is probably in progress
                NotifyTransport(tran, kLTTransport_Event_Down);
            }
            break;
        case kLTDeviceWiFi_Status_LinkConnected:
            if (tran->linkConnected) {
                LOCK_API;
                if (tran->dhcpActive) {
                    dhcp_renew(netif); // make sure dhcp is still valid
                }
                UNLOCK_API;
                // Rejoined the AP - inform NetMonitor
                NotifyTransport(tran, kLTTransport_Event_Up);
            }
            break;
        default:
            LTLOG_DEBUG("wifi.evt", "wifi status %d", status);
            break;
    }
}

LOC bool StartNetwork(void) {
    Priv_Tran *tran = (Priv_Tran*)S.iThread->GetThreadSpecificClientData(S.iThread->GetCurrentThread(),
                                                                        LTNET_TRANSPORT_DATA_KEY);
    lt_lwip_sys_init();
    if (!tran->wifiEnabled) {  // was started in a prior instance
        lwip_init();
    } else {
        sys_timeouts_init();
    }
    do {
        // Only open the WiFi driver one time (and keep it open)
        if (!tran->wifiEnabled) {
            // Open WiFi device to get link events:
            S.iWiFi = (LTDeviceWiFi*)S.iCore->OpenLibrary("LTDeviceWiFi"); // will start Join if set-up
            if (!S.iWiFi) break;
            // Basic network interface setup, but prior to wifi operation:
            lt_memset(GET_NETIF(tran), 0, sizeof(struct netif));
            LOCK_NET(netif_add(GET_NETIF(tran), &tran->ipAddr, &tran->ipMask, &tran->ipGate, NULL, &OnNetAdded, &netif_input));
        }
        // Trigger wifi operation:
        S.iWiFi->OnStatusChange(OnWiFiChange, NULL, NULL);
        // If WiFi driver was already open, must rejoin AP if there was one:
        if (tran->wifiEnabled) {
            if (S.iWiFi->IsConnected()) {
                NotifyTransport(tran, kLTTransport_Event_Up);
            } else {
                LTLOG("tran.rejoin", "transport forced WiFi rejoin");
                LTWiFi_ApInfo ap = {}; // use prior config for AP spec
                S.iWiFi->JoinAp(&ap, NULL, NULL);
            }
        }
        tran->wifiEnabled = true;
        return true;
    } while (0);

    LTLOG_REDALERT("init.fail", "failed");
    S.iCore->CloseLibrary((LTLibrary*)S.iWiFi); // null ok
    return false;
}

LOC void StopNetwork(void) {
    S.iWiFi->NoStatusChange(OnWiFiChange);
}

/*******************************************************************************
** DNS Functions
*******************************************************************************/

FWD void NetIpWiFi_ConnectSocket(LTSocket_Data *socketData);

LOC void InitDns(const char *dns_server_address) {
    dns_init();
    ip_addr_t dnsserver;
    if (dns_server_address && ip4addr_aton(dns_server_address, &dnsserver)) {
        dns_setserver(0, &dnsserver);
    }
}

LOC void OnDnsTimer(void* arg) {
    Priv_Sock *sock = (Priv_Sock*)arg;
    LT_UNUSED(sock); // debug only
    LTLOG_DEBUG("dns.chk", "DNS check - sock %p\n", sock);
    LOCK_NET(dns_tmr());
}

LOC void KillDnsTimer(Priv_Sock *sock) {
    if (!sock || !sock->dnsTimer) return;
    sock->dnsTimer = false;
    Priv_Tran *tran = sock->privTran;
    if (tran) {
        S.iThread->KillTimer(tran->hThread, OnDnsTimer, sock);
    }
}
#define LT_ROKU_CACHE_DNS_TABLE_SIZE  8
typedef struct {
    LTTime timestamp;
    char name[128];
    ip_addr_t ipaddr;
} Roku_DNS_Cache_t;
static Roku_DNS_Cache_t roku_dns_cache[LT_ROKU_CACHE_DNS_TABLE_SIZE];
static void UpdateRokuBackupDns(const char *name, const ip_addr_t *ipaddr) {
    Roku_DNS_Cache_t *found = NULL;
    Roku_DNS_Cache_t *avail = NULL;
    Roku_DNS_Cache_t *oldest = NULL;
    LTTime diff = LTTime_Seconds(0);
    LTTime now = S.iCore->GetKernelTime();
    for (int i = 0; i < LT_ROKU_CACHE_DNS_TABLE_SIZE; i++) {
        if (!lt_strncmp(name, roku_dns_cache[i].name, sizeof(roku_dns_cache[i].name) - 1)) {
            found = &roku_dns_cache[i];
        } else if (roku_dns_cache[i].name[0] == 0) {
            avail = &roku_dns_cache[i];
        } else if (LTTime_IsGreaterThanOrEqual(LTTime_Subtract(now, roku_dns_cache[i].timestamp), diff)) {
            oldest = &roku_dns_cache[i];
            diff = LTTime_Subtract(now, roku_dns_cache[i].timestamp);
        }
    }
    Roku_DNS_Cache_t *update = (found)?found: avail;
    if (update == NULL) {
        update = oldest;
    }
    if (update) {
        lt_strncpyTerm(update->name, name, sizeof(update->name) - 1);
        update->ipaddr = *ipaddr;
        update->timestamp = S.iCore->GetKernelTime();
        LTLOG("roku.dns.update", "update: %p, ts: %lld, name: %s, ip: 0x%08lX", update, update->timestamp.nNanoseconds, update->name, LT_Pu32(update->ipaddr.addr));
    }
}
static Roku_DNS_Cache_t *GetRokuBackupDns(const char *name) {
    Roku_DNS_Cache_t *found = NULL;
    for (int i = 0; i < LT_ROKU_CACHE_DNS_TABLE_SIZE; i++) {
        if (!lt_strcmp(name, roku_dns_cache[i].name)) {
            found = &roku_dns_cache[i];
            LTLOG("roku.dns.found", "update: %p, ts: %lld, name: %s, ip: 0x%08lX", found, found->timestamp.nNanoseconds, found->name, LT_Pu32(found->ipaddr.addr));
            break;
        }
    }
    return found;
}
LOC void OnDnsHostResolved(const char *name, const ip_addr_t *ipaddr, void *arg) {
    LT_UNUSED(FormIp4Addr); // debug only
    LTSocket hSocket = VOIDPTR_TO_LTHANDLE(arg);
    LTSocket_Data *socketData = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socketData) return;
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    KillDnsTimer(sock);
    LTSocket_Event event = kLTSocket_Event_DnsResolved;
    if (!ipaddr) {
        Roku_DNS_Cache_t *roku_dns_cache = GetRokuBackupDns(name);
        if (roku_dns_cache && LTTime_IsLessThanOrEqual(LTTime_Subtract(S.iCore->GetKernelTime(), roku_dns_cache->timestamp), LTTime_Seconds(3600))) {
            LTLOG_SERVER("dns.roku.backup", "ts: %lld, name: %s, ip: 0x%08lX", roku_dns_cache->timestamp.nNanoseconds, roku_dns_cache->name, LT_Pu32(roku_dns_cache->ipaddr.addr));
            ipaddr = &roku_dns_cache->ipaddr;
            event = kLTSocket_Event_DnsRokuCache;
        } else {
            event = kLTSocket_Event_DnsTimeout;
            LTLOG("dns.timeout.err", "too many DNS failure");
            NotifyTransport(sock->privTran, kLTTransport_Event_DnsTimeout);
        }
    }

    if (event != kLTSocket_Event_DnsTimeout) {
        DBLOG("dns.done", "DNS done %s -> %s, (thread %lx)\n", name, FormIp4Addr(ipaddr), LT_PLT_HANDLE(S.iThread->GetCurrentThread()));
        if (event == kLTSocket_Event_DnsResolved) {
            UpdateRokuBackupDns(name, ipaddr);
        } else {
            event = kLTSocket_Event_DnsResolved;
        }
        sock->ipAddr.addr = ipaddr->addr;
        NetIpWiFi_ConnectSocket(sock->socketData);
    }
    NotifySocketEvent(sock, event);
    S.iCore->ReleaseHandlePrivateData(hSocket, socketData);
}

LOC void ResolveDnsName(const char *name, Priv_Sock *sock) {
    err_t err;
    void *arg = LTHANDLE_TO_VOIDPTR(sock->socketData->h_socket); // Socket LTHandle
    LOCK_NET(err = dns_gethostbyname(name, &sock->ipAddr, OnDnsHostResolved, arg));
    DBLOG("dns.res", "DNS resolve \"%s\" status: %d", name, err);
    switch (err) {
        case ERR_OK: // found in local cache
            NotifySocketEvent(sock, kLTSocket_Event_DnsResolved);
            break;
        case ERR_INPROGRESS: // full DNS lookup in progress
        {
            Priv_Tran *tran = sock->privTran;
            if (tran) {
                S.iThread->SetTimer(tran->hThread, LTTime_Seconds(DNS_TIMEOUT_SECONDS),
                                    OnDnsTimer, NULL, sock);
                sock->dnsTimer = true;
            }
            break;
        }
        default:
            LTLOG_YELLOWALERT("dns.fail", "DNS failed %d on %s\n", err, name);
            NotifySocketEvent(sock, kLTSocket_Event_DnsError);
            break;
    }
}

/*******************************************************************************
** Very simple tokenizer [KISS] (for transport and socket specs)
*******************************************************************************/

UTL char **TokenizeSpec(char *args) {
    if (!args) return NULL;
    u16 count = 1;
    for (char *cp = args; *cp; cp++) {
        if (*cp == ' ') {
            count++;
            while (*cp && *cp == ' ') cp++;
            if (!*cp) break;
        }
    }
    char **list = lt_malloc((count+1) * sizeof(char*));
    if (!list) return NULL;
    char **tokens = list;
    *tokens++ = args;
    for (char *cp = args; *cp; cp++) {
        if (*cp == ' ') {
            *cp++ = 0;
            while (*cp && *cp == ' ') cp++;
            if (*cp) *tokens++ = cp;
            else break;
        }
    }
    *tokens = NULL;
    //for (s16 n = 0; list[n]; n++) PR("Token[%d]: %s\n", n, list[n]);
    return list;
}

UTL s16 FindToken(char **list, char *token) {
    for (s16 n = 0; list[n]; n++) {
        if (!lt_strcmp(list[n], token)) return n;
    }
    return -1;
}

/*******************************************************************************
** API Transport Functions
*******************************************************************************/

PUB u32 NetIpWiFi_OpenTransport(LTTransportData *tranData, LTSocket_Create *createSocket) {
    LTTransportDriver *driverData = tranData->driverData;
    LTLOG("open.tran", "open transport: %s", driverData->tranSpec);
    Priv_Tran *tran = driverData->privData; // note: may not exist yet

    // Already exists, WiFi and LwIP already open and setup:
    if (tran && tran->openCount) {
        tran->openCount++;
        LTLOG("open.tran", "already opened, cnt: %ld", LT_Pu32(tran->openCount));
        return sizeof(Priv_Sock); // driver was already created
    }

    tran = getNewTransport();
    if (!tran) {
        LTLOG("open.tran.notran", NULL);
        return 0;
    }
    lt_memset(tran, 0, sizeof(Priv_Tran));
    driverData->privData = tran;
    *tran = (Priv_Tran) {
        .createSocket  = createSocket, // !!! move to layer above! Not needed here
        .tranData      = tranData,
        .driverData    = driverData,
        .debug = false,
        .openCount = 0,
    };

    // Parse specification string
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use symbolic abbreviations for words. Example: "gw" rather than "gateway"
    char *spec = lt_strdup(driverData->tranSpec);
    char **tokens = TokenizeSpec(spec); // does malloc()
    if (!tokens) {
        LTLOG_YELLOWALERT("open.tran", "token oom");
        lt_free(spec); // NULL ok
        return 0;
    }

    bool fail = false;
    s16 n; // arg num
    ip_addr_t ipAddr; // needed because ip4addr_aton sets bogus addr on parse failure

    LTLOG_DEBUG("open1", "mask = %d\n", (int)tran->ipMask.addr);
    n = FindToken(tokens, "ip:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        tran->ipAddr.addr = ipAddr.addr;
    }

    n = FindToken(tokens, "gw:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        tran->ipGate.addr = ipAddr.addr;
    }

    n = FindToken(tokens, "mask:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        tran->ipMask.addr = ipAddr.addr;
    } else {
        IP4_ADDR(&tran->ipMask, 255,255,255,0); // default to C mask if not specified
    }

    n = FindToken(tokens, "dns:");
    const char *dns_server = NULL; // "8.8.8.8"; // Google DNS for default
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        dns_server = tokens[n+1];
    }
    InitDns(dns_server);

    tran->dhcpEnabled = (FindToken(tokens, "dhcp") >= 0 || !tran->ipAddr.addr);
    if (!tran->dhcpEnabled && (!tran->ipAddr.addr || !tran->ipGate.addr)) {
        fail = true;
        LTLOG_YELLOWALERT("init.no.ip", "no DHCP nor IP/GW settings");
    }

    if (FindToken(tokens, "debug") >= 0) tran->debug = true;
    lt_free(tokens);
    lt_free(spec);
    if (fail) return 0;

    char name[20];
    lt_snprintf(name, 20, "LwipTran-%08x", LT_Pu32(tran));
    tran->hThread = S.iCore->CreateThread(name);
    S.iThread->SetStackSize(tran->hThread, TRANSPORT_THREAD_STACK_SIZE);
    S.iThread->SetThreadSpecificClientData(tran->hThread, LTNET_TRANSPORT_DATA_KEY, NULL, tran);
    S.iThread->StartSynchronous(tran->hThread, StartNetwork, StopNetwork);
    tran->openCount = 1;
    return sizeof(Priv_Sock); // Used by LTNet for creating sockets
}

PUB void NetIpWiFi_CloseTransport(LTTransportDriver *driverData) { // Called from DestroyTransport
    // All sockets get closed in LTNet before this is called
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;
    if (--tran->openCount) {
        LTLOG("close.tran", "not closing. cnt:  %ld", LT_Pu32(tran->openCount));
        return;
    }

    LTLOG("wifi.disc", "force WiFi disconnect");
    S.iWiFi->Disconnect();
    if (tran->linkConnected) {
        tran->linkConnected = false;
        NotifyTransport(tran, kLTTransport_Event_Down);
    }

    LOCK_API;
    if (tran->dhcpActive) {
        tran->dhcpActive = false;
        dhcp_stop(GET_NETIF(tran));
        dhcp_cleanup(GET_NETIF(tran));
    }
    S.iThread->Sleep(LTTime_Milliseconds(500)); // hack to wait for disconnect !!!

    netif_set_link_down(GET_NETIF(tran));
    netif_remove(GET_NETIF(tran));
    sys_timeouts_destroy();
    UNLOCK_API;

    // Terminate transport thread
    S.iThread->Terminate(tran->hThread);
    S.iThread->WaitUntilFinished(tran->hThread, LTTime_Seconds(10));
    S.iThread->Destroy(tran->hThread);

    lt_lwip_sys_destroy();
    lt_memset(tran, 0, sizeof(Priv_Tran));
}

PUB bool NetIpWiFi_GetTransportSpec(LTTransportDriver *driverData, char *spec, u16 specSize) {
    // Could be extended to take a spec like "ip:" and return just ip address. !!
    // LTNetCore asserts spec and specSize are valid
    // driverData is never NULL
    Priv_Tran *tran = driverData->privData;
    spec[0] = 0;
    if (!tran) return false;
    char *s = spec;
    char *e = s + specSize - 1;
    if (tran->dhcpEnabled) {
        s = AppendStr("dhcp ", s, e);
    }
    if (tran->ipAddr.addr) {
        s = AppendStr("ip: ", s, e);
        s = IpToStr(&tran->ipAddr, s, e);
    }
    if (tran->ipGate.addr) {
        s = AppendStr(" gw: ", s, e);
        s = IpToStr(&tran->ipGate, s, e);
    }
    if (tran->ipMask.addr) {
        s = AppendStr(" mask: ", s, e);
        s = IpToStr(&tran->ipMask, s, e);
    }
    if (tran->ipDns.addr) {
        s = AppendStr(" dns: ", s, e);
        s = IpToStr(&tran->ipDns, s, e);
    }
    return true;
}

PUB void NetIpWiFi_GetMetrics(LTTransportDriver *driverData, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    // driverData is never NULL
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;

    S.iMetricsMutex->API->Lock(S.iMetricsMutex);
    lt_memset(metrics, 0, sizeOfMetrics);
    lt_memcpy(metrics, &tran->metrics, sizeof(metrics) > sizeOfMetrics ? sizeOfMetrics : sizeof(*metrics));
    S.iMetricsMutex->API->Unlock(S.iMetricsMutex);
}

PUB void NetIpWiFi_ProcTransportMetrics(LTTransportDriver *driverData, LTTransport_MetricsAction action, bool logToServer) {
    if (!driverData) return;

    Priv_Tran *tran = driverData->privData;
    if (!tran) return;

    S.iMetricsMutex->API->Lock(S.iMetricsMutex);

    // Dump log
    if (action == kLTTransport_MetricsAct_Show) {
        u32 logFlag = kLTCore_LogFlags_LogTypeLog | kLTCore_LogFlags_LogToConsole;
        if (logToServer) logFlag |= kLTCore_LogFlags_LogToServer;

        LT_GetCore()->Log(s_pLTLOG_Section, "drop.metrics", logFlag, "dropTx:%lu, dropRx:%lu",
                    LT_Pu32(tran->metrics.dropPacketsTx_shadow), LT_Pu32(tran->metrics.dropPacketsRx_shadow));
    }

    tran->metrics.dropPacketsTx_shadow = 0;
    tran->metrics.dropPacketsRx_shadow = 0;

    S.iMetricsMutex->API->Unlock(S.iMetricsMutex);
}

PUB s32 NetIpWiFi_IsOperating(LTTransportDriver *driverData, LTTransport_Nudge nudge) {
    // driverData is never NULL
    Priv_Tran *tran = driverData->privData;
    if (!tran || !tran->linkConnected) return -1;
    if (nudge == kLTTransport_Nudge_Soft) {
        LOCK_NET(dhcp_inform(GET_NETIF(tran)));
    }
    // !!! add wifi rejoin for reset
    // !!! deal with unsigned to signed and wrapping
    u32 bytesRx;
    METRICS_GUARD(bytesRx = tran->metrics.lowerBytesRx);
    return bytesRx;
}

/*******************************************************************************
** API Socket Functions
*******************************************************************************/

PUB bool NetIpWiFi_OpenSocket(LTSocket_Data *socketData) {
    // !!! Question: what if link not up
    LTLOG_DEBUG("sock.open.spec", "open socket: %s", socketData->spec);
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    *sock = (Priv_Sock) {
        .socketData = socketData, // back reference
        .privTran = socketData->transData->driverData->privData,
        .tcpPrio = 64
    };

    // Parse specification string
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use short symbolic abbreviations.
    char *spec = lt_strdup(socketData->spec);
    char **tokens = TokenizeSpec(spec); // does malloc()
    if (!tokens) {
        LTLOG_YELLOWALERT("sock.spec.fail", "socket spec failed"); // missing or OOM
        lt_free(spec); // NULL ok
        return false;
    }

    bool listener = false;
    sock->ipProto = kIPProtocol_TcpClient;
    do {
        // Note: "tcp" is the default.
        if (FindToken(tokens, "udp"   ) >= 0) { sock->ipProto = kIPProtocol_Udp;  break; }
        if (FindToken(tokens, "icmp"  ) >= 0) { sock->ipProto = kIPProtocol_Icmp; break; }
        if (FindToken(tokens, "raw"   ) >= 0) { sock->ipProto = kIPProtocol_Raw;  break; }
        if (FindToken(tokens, "dns"   ) >= 0) { sock->ipProto = kIPProtocol_Dns;  break; }
    } while (0);

    if (FindToken(tokens, "listen") >= 0) listener = true;

    s16 n;
    ip_addr_t ipAddr; // needed because ip4addr_aton sets bogus addr on parse failure

    n = FindToken(tokens, "host:");  // maybe add "host: foo.com:80" format? !!
    if (n >= 0 && tokens[n+1]) {
        sock->hostname = lt_strdup(tokens[n+1]);
    }

    n = FindToken(tokens, "ip:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        sock->ipAddr = ipAddr;
    }

    n = FindToken(tokens, "port:");
    if (n >= 0 && tokens[n+1]) sock->ipPort = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "myport:");
    if (n >= 0 && tokens[n+1]) sock->myPort = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "allow:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        sock->ipAllow = ipAddr;
    } else sock->ipAllow.addr = 0;

    n = FindToken(tokens, "dscp:");
    if (n >= 0 && tokens[n+1]) sock->dscp = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "keepalive");
    if (n >= 0) sock->flags |= kSocketFlags_KeepAlive;

    n = FindToken(tokens, "no-connect");
    if (n >= 0) sock->flags |= kSocketFlags_NoConnect;

    n = FindToken(tokens, "tcp-prio:");
    if (n >= 0 && tokens[n+1]) sock->tcpPrio = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "addr-reuse");
    if (n >= 0) sock->flags |= kSocketFlags_AddrReuse;

    lt_free(tokens);
    lt_free(spec);

    if (listener && sock->ipProto == kIPProtocol_TcpClient) {
        if (sock->ipPort && !sock->myPort) sock->myPort = sock->ipPort;
        sock->ipProto = kIPProtocol_TcpServer;
    }

    sock->rxPbuf = NULL;

    if (sock->hostname && sock->privTran->linkConnected) ResolveDnsName(sock->hostname, sock);
    //LTLOG("tcp.prio", "tcp prio: %d", sock->tcpPrio);
    const char *failure = NULL;
    err_t err = 0;
    switch (sock->ipProto) {
        case kIPProtocol_TcpServer:
            sock->flags |= kSocketFlags_NoConnect;
            // fall-thru
        case kIPProtocol_TcpClient:
            failure = "tcp-pcb";
            LOCK_NET(sock->tcpPcb = tcp_new(sock->tcpPrio));
            if (!sock->tcpPcb) break;
            if (sock->flags & kSocketFlags_KeepAlive) ip_set_option(sock->tcpPcb, SOF_KEEPALIVE);
            if (sock->flags & kSocketFlags_AddrReuse) ip_set_option(sock->tcpPcb, SOF_REUSEADDR);
            LOCK_NET(tcp_arg(sock->tcpPcb, sock));
            if ((sock->flags & kSocketFlags_NoConnect) || sock->ipProto == kIPProtocol_TcpServer) {
                failure = "tcp-bind";
                LOCK_NET(err = tcp_bind(sock->tcpPcb, IP_ANY_TYPE, sock->myPort));
                sock->myPort = sock->tcpPcb->local_port;
                if (err) {
                    LOCK_API;
                    tcp_close(sock->tcpPcb);
                    tcp_arg(sock->tcpPcb, NULL);
                    sock->tcpPcb = NULL;
                    UNLOCK_API;
                    LTLOG("sock.bind.err", "socket bind failed: %d", err);
                    break;
                }
            }
            sock->isOpen = true;
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if ((!(sock->flags & kSocketFlags_NoConnect) && sock->ipAddr.addr) || sock->ipProto == kIPProtocol_TcpServer) {
                NetIpWiFi_ConnectSocket(socketData);
            }
            failure = NULL;
            break;
        case kIPProtocol_Udp:
            // if (!sock->ip_port) only if sending
            sock->flags |= kSocketFlags_NoConnect;
            LOCK_NET(sock->udpPcb = udp_new());
            failure = "udp-pcb";
            if (!sock->udpPcb) break;
            failure = "udp-bind";
            LOCK_NET(err = udp_bind(sock->udpPcb, IP_ADDR_ANY, sock->myPort));
            sock->myPort = sock->udpPcb->local_port;
            if (err) {
                LOCK_NET(udp_remove(sock->udpPcb));
                break;
            }
            sock->isOpen = true;
            LOCK_NET(udp_recv(sock->udpPcb, OnUdpReceive, sock));
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if (sock->ipAddr.addr) NetIpWiFi_ConnectSocket(socketData);
            failure = NULL;
            break;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            sock->flags |= kSocketFlags_NoConnect;
            // For raw frames, this will eventually want to get the IP_PROTO from the packet !!!
            failure = "raw-pcb";
            LOCK_NET(sock->rawPcb = raw_new(sock->ipProto == kIPProtocol_Icmp ? IP_PROTO_ICMP : 0));
            if (!sock->rawPcb) break;
            failure = "raw-bind";
            LOCK_NET(err = raw_bind(sock->rawPcb, &sock->privTran->ipAddr));
            if (err) {
                LOCK_NET(raw_remove(sock->rawPcb));
                break;
            }
            sock->isOpen = true;
            LOCK_NET(raw_recv(sock->rawPcb, OnRawReceive, sock));
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if (sock->ipAddr.addr) NetIpWiFi_ConnectSocket(socketData);
            failure = NULL;
            break;
        case kIPProtocol_Dns: // do nothing more
            // failure is NULL;
            break;
        default: break;
    }
    if (sock->ipPcb) sock->ipPcb->tos = sock->dscp << 2;

    if (!failure) {
        sock->privTran->socketCount++;
        return true;
    }
    LTLOG_YELLOWALERT("sock.open.fail", "OpenSocket failed: %s err: %d\n", failure, err);
    LTLOG_SERVER("sock.open.fail.spec", "%s", socketData->spec);
    return false;
}

PUB bool NetIpWiFi_GetSocketSpec(LTSocket_Data *socketData, char *spec, u16 specSize) {
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    if (!spec) return false;
    char *s = spec;
    char *e = s + specSize - 1;
    if (sock->ipProto >= kIPProtocol_Max) return false;
    s = AppendStr(IPProtocolNames[sock->ipProto], s, e);
    if (sock->hostname) {
        s = AppendStr(" host: ", s, e);
        s = AppendStr(sock->hostname, s, e);
    }
    if (sock->ipAddr.addr) {
        s = AppendStr(" ip: ", s, e);
        s = IpToStr(&sock->ipAddr, s, e);
    }
    if (sock->ipPort) {
        s = AppendStr(" port: ", s, e);
        s = FormU32(s, 6, sock->ipPort);
    }
    if (sock->myPort) {
        s = AppendStr(" myport: ", s, e);
        s = FormU32(s, 6, sock->myPort);
    }
    if (sock->ipAllow.addr) {
        s = AppendStr(" allow: ", s, e);
        s = IpToStr(&sock->ipAddr, s, e);
    }
    return true;
}

PUB bool NetIpWiFi_GetSocketProperty(LTSocket_Data *socketData, const char *name, void *value) {
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "recv.endpoint.v4") == 0) {
        LTNetIpv4Endpoint *ep = value;
        *ep = sock->recvAddr;
    } else if (lt_strcmp(name, "local.endpoint.v4") == 0) {
        LTNetIpv4Endpoint *ep = value;
        ep->address = sock->privTran->ipAddr.addr;
        ep->port    = sock->myPort;
    } else if (lt_strcmp(name, "dns.addr.v4") == 0) {
        if (sock->ipProto != kIPProtocol_Dns) {
            LTLOG_REDALERT("getprop.notdns", "Invalid attempt to get DNS address from non-DNS socket");
            return false;
        }
        *(u32 *)value = sock->ipAddr.addr;
    } else {
        LTLOG_YELLOWALERT("getprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB bool NetIpWiFi_SetSocketProperty(LTSocket_Data *socketData, const char *name, const void *value) {
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "send.endpoint.v4") == 0) {
        const LTNetIpv4Endpoint *ep = value;
        sock->ipAddr.addr = ep->address;
        sock->ipPort      = ep->port;
    } else if (lt_strcmp(name, "dscp") == 0) {
        sock->dscp = *(u8 *)value;
        sock->ipPcb->tos = sock->dscp << 2;
    } else if (lt_strcmp(name, "metrics.reset") == 0) {
        if (socketData->transData) {
            Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
            if (sock && sock->isOpen && sock->privTran) NotifyTransport(sock->privTran, kLTTransport_Event_ResetMetrics);
        }
    } else if (lt_strcmp(name, "metrics.show") == 0) {
        if (socketData->transData) {
            Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
            if (sock && sock->isOpen && sock->privTran) NotifyTransport(sock->privTran, kLTTransport_Event_ShowMetrics);

        }
    } else {
        LTLOG_YELLOWALERT("setprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB void NetIpWiFi_CloseSocket(LTSocket hSocket) { // Also called by Transport.DestroySocket()
    LTLOG_DEBUG("sock.close", "close socket: %04lx", LT_PLT_HANDLE(hSocket));
    LTSocket_Data *socketData = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!socketData) return;
    if (socketData->h_socket == hSocket && socketData->h_socket) {
        Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
        if (sock && sock->isOpen && sock->privTran) {
            LOCK_API;
            LT_ASSERT(sock->privTran->socketCount >= 1);
            KillDnsTimer(sock);
            if (sock->anyPcb) {
                err_t err;
                switch (sock->ipProto) {
                    case kIPProtocol_TcpClient:
                    case kIPProtocol_TcpServer:
                        err = tcp_close(sock->tcpPcb);
                        tcp_arg(sock->tcpPcb, NULL);
                        if (err) {
                            LTLOG("sock.close.code", "non-zero close socket: %d", err);
                            tcp_poll(sock->tcpPcb, NULL, 4); // try again (usage!!!)
                            UNLOCK_API;
                            S.iCore->ReleaseHandlePrivateData(hSocket, socketData);
                            return;
                        }
                        break;
                    case kIPProtocol_Udp:
                        udp_remove(sock->udpPcb);
                        break;
                    case kIPProtocol_Icmp:
                    case kIPProtocol_Raw:
                        raw_remove(sock->rawPcb);
                        break;
                    default: break;
                }
            }
            sock->privTran->socketCount--;
            if (sock->rxPbuf) pbuf_free(sock->rxPbuf);
            UNLOCK_API;
        }
        lt_free(sock->hostname); // NULL okay
        // clear private sock data before freed by netcore's destroy
        // socket handle data will be cleared and freed by netcore's destroy.
        *sock = (Priv_Sock){}; // isOpen = false
    }
    S.iCore->ReleaseHandlePrivateData(hSocket, socketData);
}

PUB void NetIpWiFi_ConnectSocket(LTSocket_Data *socketData) {
    Priv_Sock *sock  = GET_SOCKET_PRIV(socketData);
    if (!sock || !sock->isOpen || sock->ipProto >= kIPProtocol_Max) return;
    Priv_Tran *tran = socketData->transData->driverData->privData;
    if (!netif_is_link_up(GET_NETIF(tran))) {
        LTLOG("sock.con.dwn", "not linked");
        return;
    }
    DBLOG("sock.con", "connect[%02x] %s ip: %s port: %d (thread %lx)\n", sock->socketData->h_socket,
            IPProtocolNames[sock->ipProto], FormIp4Addr(&sock->ipAddr), sock->ipPort, LT_PLT_HANDLE(S.iThread->GetCurrentThread()));
    err_t err = 0;
    switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
            if (sock->tcpPcb->state > CLOSED) return; // don't try to do it again
            LOCK_NET(err = tcp_connect(sock->tcpPcb, &sock->ipAddr, sock->ipPort, NULL));
            DBLOG("sock.con", "connect sent %d", err);
            if (err) {
                NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                return;
            }
            break;
        case kIPProtocol_TcpServer:
            if (sock->tcpPcb->state > CLOSED) return; // don't try to do it again
            LOCK_NET(sock->tcpPcb = tcp_listen_with_backlog_and_err(sock->tcpPcb, TCP_DEFAULT_LISTEN_BACKLOG, &err));
            if (!sock->tcpPcb) {
                LTLOG("sock.lsn.err", "socket listen failed: %d", err);
                NotifySocketEvent(sock, kLTSocket_Event_SocketError);
            }
            break;
        case kIPProtocol_Udp:
            LOCK_NET(udp_connect(sock->udpPcb, &sock->ipAddr, sock->ipPort));
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            LOCK_NET(raw_connect(sock->rawPcb, &sock->ipAddr));
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;
        default: break;
    }
}

PUB void NetIpWiFi_DisconnectSocket(LTSocket_Data *socketData) {
    if (!socketData) return;
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    if (!sock->isOpen) return; // socket not open
    switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
        case kIPProtocol_TcpServer:
            return;
        case kIPProtocol_Udp:
            LOCK_NET(udp_disconnect(sock->udpPcb));
            NotifySocketEvent(sock, kLTSocket_Event_Disconnected); // for connectionless sockets
            break;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            LOCK_NET(raw_disconnect(sock->rawPcb));
            NotifySocketEvent(sock, kLTSocket_Event_Disconnected); // for connectionless sockets
            break;
        default: break;
    }
}

PUB s32 NetIpWiFi_WriteSocket(LTSocket_Data *socketData, const void *data, u32 dataLength) {
    //LTLOG_DEBUG("skt.writ", "write %h data: %p, len: %d", __FUNCTION__, socketData->handle, data, dataLength);
    static LTTime tx_time_prev;
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    Priv_Tran *tran = sock->privTran;
    TBUG("%s: %lu\n", __FUNCTION__, LT_Pu32(dataLength));
    err_t err = 0;

    LOCK_API;
    if (!sock->isOpen) err = -1; // socket not open
    else switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
        case kIPProtocol_TcpServer:
            // tcp-write will fail with ERR_MEM if the callers tries to send more than pcb->snd_buf bytes
            // The LT HTTP client doesn't understand this and will stall. Limit dataLength to an amount
            // guaranteed to fit
            if (dataLength > sock->tcpPcb->snd_buf) dataLength = sock->tcpPcb->snd_buf;
            err = tcp_write(sock->tcpPcb, data, dataLength, TCP_WRITE_FLAG_COPY);
            if (err) break;
            tcp_output(sock->tcpPcb);
            break;
        case kIPProtocol_Udp:
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:;
            struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, dataLength, PBUF_RAM);
            if (pbuf) {
                err = pbuf_take(pbuf, data, dataLength);
                if (!err) {
                    if (sock->ipProto != kIPProtocol_Udp) err = raw_send(sock->rawPcb, pbuf);  //raw_sendto(sock->rawPcb, pbuf, &sock->ipAddr);
                    else err = udp_sendto(sock->udpPcb, pbuf, &sock->ipAddr, sock->ipPort); // check err, notify on failure !!!
                    if (err == ERR_IF) err = ERR_MEM; // Tx Packet dropped
                }
                pbuf_free(pbuf);
            } else {
                LTTime tx_now = LT_GetCore()->GetKernelTime();
                if (LTTime_IsGreaterThanOrEqual(LTTime_Subtract(tx_now, tx_time_prev), LTTime_Seconds(1))) {
                    LTLOG_YELLOWALERT("wrtsock.oom.buf", "failed to allocate pbuf for socket write");
                    NetIpWiFi_ShowLwipStat(NULL, true);
                    tx_time_prev = tx_now;
                }
                err = ERR_MEM;
            }
            break;
        default: break;
    }
    UNLOCK_API;
    if (err) {
        // Write buffer is full or unavailable temporarily. Upper stack needs to wait a bit and resend.
        if (err == ERR_MEM) return 0;
        // Other errors
        LTLOG("wrt.err", "write error %d", err); // code is platform dependent
        NotifySocketEvent(sock, kLTSocket_Event_WriteError);
        return -1;
    }
    METRICS_GUARD(tran->metrics.upperBytesTx += dataLength);
    return dataLength;
}

PUB s32 NetIpWiFi_ReadSocket(LTSocket_Data *socketData, void *data, u32 dataLength) {
    // All of the fancy buffering happens in LwIP. So, it doesn't belong here.
    //LTLOG_DEBUG("skt.read", "read %h data: %p, len: %d", __FUNCTION__, socketData->handle, data, dataLength);
    Priv_Sock *sock = GET_SOCKET_PRIV(socketData);
    Priv_Tran *tran = sock->privTran;
    TBUG("%s: %lu\n", __FUNCTION__, LT_Pu32(dataLength));
    u16 tail = 0;
    bool query = !data && !dataLength;
    if (!sock->isOpen) {  // socket not open
        NotifySocketEvent(sock, kLTSocket_Event_ReadError);
        return -1;
    }
    LOCK_API;
    if (sock->rxPbuf) {
        struct pbuf *pbuf = sock->rxPbuf;
        if (pbuf->flags & PBUF_FLAG_RECV_PAYLOAD) {
            RecvPayload *payload = (RecvPayload*)pbuf->payload;
            sock->recvAddr = payload->endpoint;
            pbuf->payload = payload->payload;
            pbuf->flags &= ~PBUF_FLAG_RECV_PAYLOAD;
        }
        // A NULL data pointer means flush without reading (but metrics still count it):
        if (!data) {
            tail = (sock->ipProto < kIPProtocol_TcpClient) ? pbuf->len : pbuf->tot_len;
        } else {
            // For datagram protocols, limit read to a single packet (assumes first pbuf is full packet):
            if (sock->ipProto < kIPProtocol_TcpClient && dataLength > pbuf->len) dataLength = pbuf->len;
            u16 num = 0;
            while (dataLength > 0 && (num = pbuf_copy_partial(pbuf, data, (u16)dataLength, tail))) { // does not need LOCK_API
                tail += num;
                dataLength -= num;
            }
        }
        if (!query) {
            METRICS_GUARD(tran->metrics.upperBytesRx += tail);
            TBUG("--------- Read %u bytes - avail: %lu (pbuf: %p tot: %u next: %p)\n", tail, LT_Pu32(dataLength), pbuf, pbuf->tot_len, pbuf->next);
            sock->rxPbuf = pbuf_free_header(pbuf, tail);
        }
    }
    if (!query && !(sock->flags & kSocketFlags_NoConnect) && tail > 0) {
        tcp_recved(sock->tcpPcb, tail); // adjust window
    }
    UNLOCK_API;
    return tail;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

PUB void NetIpWifiImpl_LibFini(void) {
    // Add link list of transports to destroy !!!
    for (u32 i = 0; i < MAX_TRANSPORT_COUNT; i++) {
        if (S.transportPool[i].openCount) {
            NetIpWiFi_CloseTransport(S.transportPool[i].driverData);
        }
    }
    if (S.iWiFi) {
        lt_closelibrary(S.iWiFi);
        S.iWiFi = NULL;
    }
    lt_destroyobject(S.iMetricsMutex);
    S.iMetricsMutex = NULL;
}

PUB bool NetIpWifiImpl_LibInit(void) {
    S = (struct Statics) {
        .iCore   = LT_GetCore(),
        .iEvent  = lt_getlibraryinterface(ILTEvent,  LT_GetCore()),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
    };
    if (!S.iMetricsMutex) {
        S.iMetricsMutex = lt_createobject(LTMutex);
    }
    return true;
}

PUB void NetIpWifi_Destroy(LTHandle handle) {
    LT_UNUSED(handle);
    // Future: what else to cleanup???
}

/*******************************************************************************
 * Library Function Vectors - Exported as an LTNetDriver interface
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(NetIpWifi, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(NetIpWifi) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(LTNetDriver, NetIpWifi_Destroy) {
    .OpenTransport     = NetIpWiFi_OpenTransport,
    .CloseTransport    = NetIpWiFi_CloseTransport,
    .GetTransportSpec  = NetIpWiFi_GetTransportSpec,
    .GetMetrics        = NetIpWiFi_GetMetrics,
    .IsOperating       = NetIpWiFi_IsOperating,
    .OpenSocket        = NetIpWiFi_OpenSocket,
    .GetSocketSpec     = NetIpWiFi_GetSocketSpec,
    .GetSocketProperty = NetIpWiFi_GetSocketProperty,
    .SetSocketProperty = NetIpWiFi_SetSocketProperty,
    .CloseSocket       = NetIpWiFi_CloseSocket,
    .ConnectSocket     = NetIpWiFi_ConnectSocket,
    .DisconnectSocket  = NetIpWiFi_DisconnectSocket,
    .WriteSocket       = NetIpWiFi_WriteSocket,
    .ReadSocket        = NetIpWiFi_ReadSocket,
    .ShowLwipStat      = NetIpWiFi_ShowLwipStat,
    .ProcTransportMetrics = NetIpWiFi_ProcTransportMetrics,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(NetIpWifi, (LTNetDriver));
