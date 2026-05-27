/*******************************************************************************
 *
 * CommonDriverNetLwIpOpenCoreEth.c - Driver for LwIP using OpenCore Ethernet interface(QEMU)
 * --------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

//#include "debug.h"

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/utility/macaddress/LTUtilityMacAddress.h>
#include <lt/system/crypto/LTSystemCrypto.h>
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

DEFINE_LTLOG_SECTION("net.ip.eth");

/*******************************************************************************
** Debug and Code Documenting Definitions
*******************************************************************************/

#define P(...) LT_GetCore()->ConsoleStomp(__VA_ARGS__)
// #define P(...)
#undef  PF // Print Function name to console
#define PF

#define PUB static      // Part of public interface
#define CBK static      // Callback function
#define FWD static      // Forward reference
#define LOC static      // Local primary function
#define UTL static      // Local utility function
#define EXP             // Exported
#ifdef EXTRA_DEBUG
#define DEBUG_RX(...) LTLOG_DEBUG(__VA_ARGS__)
#define DEBUG_TX(...) LTLOG_DEBUG(__VA_ARGS__)
#define DEBUG_IRQ(...) LTLOG_DEBUG(__VA_ARGS__)
#else
#define DEBUG_RX(...)
#define DEBUG_TX(...)
#define DEBUG_IRQ(...)
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
    LTThread              lwipThread;
    struct netif          netif;          // LwIP network interface, allows use container of!
    LTSocket_Create     * createSocket;   // Needed for listen to create new sockets (could expand to interface)
    LTTransport_Metrics   metrics;        // Basic RX/TX metrics
    ip4_addr_t            ipAddr;         // Our address
    ip4_addr_t            ipMask;
    ip4_addr_t            ipGate;
    ip4_addr_t            ipDns;
    u32                   nextRecvPayload;
    RecvPayload           recvPayloadPool[PBUF_POOL_SIZE];
    LTDeviceUnit          wifiUnit;
    bool                  linkConnected;
    bool                  useDhcp;
    bool                  debug;
} Priv_Tran;

// Compilation assert that there is adequate space for above struct.
// If this fails, expand the Priv_Tran "space" array size
int Priv_TranTooBig[(sizeof(Priv_Tran)) > (sizeof(PrivImpl)) ? -1 : 1];

#define GET_TRANSPORT_PRIV(t)   ((Priv_Tran*)&(t->privData))
#define GET_NETIF_PRIV_TRAN(n) ((Priv_Tran*)LT_CONTAINER_OF(n, Priv_Tran, netif))

// This drivers's private data as stored within the socket handle:
typedef struct Priv_Sock {
    Priv_Tran         * privTran;      // mainly for debug
    LTSocket_Data     * socketData;    // points back up to handle data (switch to offset???)
    union {                            // LwIP control blocks:
      void            * any_pcb;       // Use lwip naming for these fields
      struct tcp_pcb  * tcp_pcb;
      struct udp_pcb  * udp_pcb;
      struct raw_pcb  * raw_pcb;
    };
    struct pbuf       * rxPbuf;        // receive packet buffer chain
    ip4_addr_t          ipAddr;        // remote ip, or local ip for listen
    u16                 ipPort;        // remote port, or local port for listen
    u16                 myPort;        // remote port, or local port for listen
    ip4_addr_t          ipAllow;       // accept connections from this address only
    LTNetIpv4Endpoint   recvAddr;      // sender address of last received packet (UDP and RAW)
    const char        * hostname;      // for "host:" spec DNS lookup
    IPProtocol          ipProto;       // IP protocol, eg TCP UDP ...
    bool                connected;     // socket is connected
    bool                noConnect;     // socket does not connect (UDP and TCP listen)
} Priv_Sock;

#define GET_SOCKET_PRIV(s) (Priv_Sock*)(s + 1)  // driver private socket data

#define LOCK_API        lt_lwip_lock()
#define UNLOCK_API      lt_lwip_unlock()
#define LOCK_NET(code)  LOCK_API; code; UNLOCK_API

/*******************************************************************************
** Module Variables
*******************************************************************************/

static LTCore         * pCore;
static ILTThread      * iThread;
static ILTEvent       * iEvent;
static LTSystemCrypto * pLCrypto;
/*******************************************************************************
** Basic Utility Functions
******************************************************************************/

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
    BUG("%s: %lu bytes: ", label, len);
    for (u32 i = 0; i < len; i++) BUG("%02x ", bytes[i]);
    BUG("\n");
}

UTL void DumpPbufInfo(const char *label, struct pbuf *pbuf) {
    BUG("--- %s pbuf %p, payld %p totlen %d len %d type %x flags %x ref %x idx %d\n", label, pbuf,
        pbuf->payload, pbuf->tot_len, pbuf->len, pbuf->type_internal, pbuf->flags, pbuf->ref, pbuf->if_idx);
}

/*******************************************************************************
** Opencores Ethernet structures
*******************************************************************************/

enum OpencoresEthernet_RegisterMODER {
    kOpencoresEthernet_RegisterMODER_B                  = OPENCORES_ETH_REG_BASE + 0x00,
    kOpencoresEthernet_RegisterMODER_RST_VALUE_V        = 0xa000,
    kOpencoresEthernet_RegisterMODER_RESET_S            = 11,
    kOpencoresEthernet_RegisterMODER_RESET_V            = 1 << kOpencoresEthernet_RegisterMODER_RESET_S,
    kOpencoresEthernet_RegisterMODER_RESET_M            = 1 << kOpencoresEthernet_RegisterMODER_RESET_S,
    kOpencoresEthernet_RegisterMODER_PROMISC_S          = 5,
    kOpencoresEthernet_RegisterMODER_PROMISC_V          = 1 << kOpencoresEthernet_RegisterMODER_PROMISC_S,
    kOpencoresEthernet_RegisterMODER_PROMISC_M          = 1 << kOpencoresEthernet_RegisterMODER_PROMISC_S,
    kOpencoresEthernet_RegisterMODER_TX_EN_S            = 1,
    kOpencoresEthernet_RegisterMODER_TX_EN_V            = 1 << kOpencoresEthernet_RegisterMODER_TX_EN_S,
    kOpencoresEthernet_RegisterMODER_TX_EN_M            = 1 << kOpencoresEthernet_RegisterMODER_TX_EN_S,
    kOpencoresEthernet_RegisterMODER_RX_EN_S            = 0,
    kOpencoresEthernet_RegisterMODER_RX_EN_V            = 1 << kOpencoresEthernet_RegisterMODER_RX_EN_S,
    kOpencoresEthernet_RegisterMODER_RX_EN_M            = 1 << kOpencoresEthernet_RegisterMODER_RX_EN_S,
};

enum OpencoresEthernet_RegisterINTSRC {
    kOpencoresEthernet_RegisterINTSRC_B                 = OPENCORES_ETH_REG_BASE + 0x04,
    kOpencoresEthernet_RegisterINTSRC_BUSY_S            = 4,
    kOpencoresEthernet_RegisterINTSRC_BUSY_V            = 1 << kOpencoresEthernet_RegisterINTSRC_BUSY_S,
    kOpencoresEthernet_RegisterINTSRC_BUSY_M            = 1 << kOpencoresEthernet_RegisterINTSRC_BUSY_S,
    kOpencoresEthernet_RegisterINTSRC_RX_S              = 2,
    kOpencoresEthernet_RegisterINTSRC_RX_V              = 1 << kOpencoresEthernet_RegisterINTSRC_RX_S,
    kOpencoresEthernet_RegisterINTSRC_RX_M              = 1 << kOpencoresEthernet_RegisterINTSRC_RX_S,
    kOpencoresEthernet_RegisterINTSRC_TX_S              = 0,
    kOpencoresEthernet_RegisterINTSRC_TX_V              = 1 << kOpencoresEthernet_RegisterINTSRC_TX_S,
    kOpencoresEthernet_RegisterINTSRC_TX_M              = 1 << kOpencoresEthernet_RegisterINTSRC_TX_S,
};

enum OpencoresEthernet_RegisterINTMASK {
    kOpencoresEthernet_RegisterINTMASK_B                 = OPENCORES_ETH_REG_BASE + 0x08,
    kOpencoresEthernet_RegisterINTMASK_BUSY_S            = 4,
    kOpencoresEthernet_RegisterINTMASK_BUSY_V            = 1 << kOpencoresEthernet_RegisterINTMASK_BUSY_S,
    kOpencoresEthernet_RegisterINTMASK_BUSY_M            = 1 << kOpencoresEthernet_RegisterINTMASK_BUSY_S,
    kOpencoresEthernet_RegisterINTMASK_RX_S              = 2,
    kOpencoresEthernet_RegisterINTMASK_RX_V              = 1 << kOpencoresEthernet_RegisterINTMASK_RX_S,
    kOpencoresEthernet_RegisterINTMASK_RX_M              = 1 << kOpencoresEthernet_RegisterINTMASK_RX_S,
    kOpencoresEthernet_RegisterINTMASK_TX_S              = 0,
    kOpencoresEthernet_RegisterINTMASK_TX_V              = 1 << kOpencoresEthernet_RegisterINTMASK_TX_S,
    kOpencoresEthernet_RegisterINTMASK_TX_M              = 1 << kOpencoresEthernet_RegisterINTMASK_TX_S,
};

enum OpencoresEthernet_RegisterPACKETLEN {
    kOpencoresEthernet_RegisterPACKETLEN_B               = OPENCORES_ETH_REG_BASE + 0x18,
    kOpencoresEthernet_RegisterPACKETLEN_MINFL_S         = 16,
    kOpencoresEthernet_RegisterPACKETLEN_MINFL_M         = 0xffff << kOpencoresEthernet_RegisterPACKETLEN_MINFL_S,
    kOpencoresEthernet_RegisterPACKETLEN_MAXFL_S         = 0,
    kOpencoresEthernet_RegisterPACKETLEN_MAXFL_M         = 0xffff << kOpencoresEthernet_RegisterPACKETLEN_MAXFL_S,
};

enum OpencoresEthernet_RegisterTXDESCNUM {
    kOpencoresEthernet_RegisterTXDESCNUM_B               = OPENCORES_ETH_REG_BASE + 0x20,
};

enum OpencoresEthernet_RegisterDESCBASE {
    kOpencoresEthernet_RegisterDESCBASE_B               = OPENCORES_ETH_REG_BASE + 0x400,
};

enum OpencoresEthernet_RegisterMIICOMMAND {
    kOpencoresEthernet_RegisterMIICOMMAND_B                        = OPENCORES_ETH_REG_BASE + 0x2c,
    kOpencoresEthernet_RegisterMIICOMMAND_WRITE_CTRL_DATA_S        = 2,
    kOpencoresEthernet_RegisterMIICOMMAND_WRITE_CTRL_DATA_M        = 1 << kOpencoresEthernet_RegisterMIICOMMAND_WRITE_CTRL_DATA_S,
    kOpencoresEthernet_RegisterMIICOMMAND_WRITE_CTRL_DATA_V        = 1 << kOpencoresEthernet_RegisterMIICOMMAND_WRITE_CTRL_DATA_S,
    kOpencoresEthernet_RegisterMIICOMMAND_READ_STATUS_S            = 1,
    kOpencoresEthernet_RegisterMIICOMMAND_READ_STATUS_M            = 1 << kOpencoresEthernet_RegisterMIICOMMAND_READ_STATUS_S,
    kOpencoresEthernet_RegisterMIICOMMAND_READ_STATUS_V            = 1 << kOpencoresEthernet_RegisterMIICOMMAND_READ_STATUS_S,
    kOpencoresEthernet_RegisterMIICOMMAND_SCAN_STATUS_S            = 0,
    kOpencoresEthernet_RegisterMIICOMMAND_SCAN_STATUS_M            = 1 << kOpencoresEthernet_RegisterMIICOMMAND_SCAN_STATUS_S,
    kOpencoresEthernet_RegisterMIICOMMAND_SCAN_STATUS_V            = 1 << kOpencoresEthernet_RegisterMIICOMMAND_SCAN_STATUS_S,
};

enum OpencoresEthernet_RegisterMIIADDRESS {
    kOpencoresEthernet_RegisterMIIADDRESS_B                        = OPENCORES_ETH_REG_BASE + 0x30,
    kOpencoresEthernet_RegisterMIIADDRESS_REG_ADDRESS_S            = 8,
    kOpencoresEthernet_RegisterMIIADDRESS_REG_ADDRESS_M            = 0x1f << kOpencoresEthernet_RegisterMIIADDRESS_REG_ADDRESS_S,
    kOpencoresEthernet_RegisterMIIADDRESS_PHY_ADDRESS_S            = 0,
    kOpencoresEthernet_RegisterMIIADDRESS_PHY_ADDRESS_M            = 0x1f << kOpencoresEthernet_RegisterMIIADDRESS_PHY_ADDRESS_S,

};

enum OpencoresEthernet_RegisterMIITXDATA {
    kOpencoresEthernet_RegisterMIITXDATA_B                        = OPENCORES_ETH_REG_BASE + 0x34,
    kOpencoresEthernet_RegisterMIITXDATA_M                        = 0xffff,
};

enum OpencoresEthernet_RegisterMIIRXDATA {
    kOpencoresEthernet_RegisterMIIRXDATA_B                        = OPENCORES_ETH_REG_BASE + 0x38,
    kOpencoresEthernet_RegisterMIIRXDATA_M                        = 0xffff,
};

enum OpencoresEthernet_RegisterMIISTATUS {
    kOpencoresEthernet_RegisterMIISTATUS_B                        = OPENCORES_ETH_REG_BASE + 0x3c,
    kOpencoresEthernet_RegisterMIISTATUS_LINK_FAILURE_S           = 0,
    kOpencoresEthernet_RegisterMIISTATUS_LINK_FAILURE_M           = 1 << kOpencoresEthernet_RegisterMIISTATUS_LINK_FAILURE_S,
    kOpencoresEthernet_RegisterMIISTATUS_LINK_FAILURE_V           = 1 << kOpencoresEthernet_RegisterMIISTATUS_LINK_FAILURE_S,
};

enum OpencoresEthernet_RegisterMACADDR0 {
    kOpencoresEthernet_RegisterMACADDR0_B               = OPENCORES_ETH_REG_BASE + 0x40,
};

enum OpencoresEthernet_RegisterMACADDR1 {
    kOpencoresEthernet_RegisterMACADDR1_B               = OPENCORES_ETH_REG_BASE + 0x44,
};

// Total number of (TX + RX) DMA descriptors
#define OPENCORES_ETH_DESC_CNT            128

#define ETH_REG(r)                       (*(volatile u32 *)kOpencoresEthernet_Register ## r ## _B)
#define ETH_REG_ADDR(r)                  ((volatile u32 *)kOpencoresEthernet_Register ## r ## _B)


/* Obtain register masks, shifts and values */
#define ETH_REG_MASK(r, m)               (kOpencoresEthernet_Register ## r ## _ ## m ## _M)
#define ETH_REG_SHIFT(r, s)              (kOpencoresEthernet_Register ## r ## _ ## s ## _S)
#define ETH_REG_VAL(r, v)                (kOpencoresEthernet_Register ## r ## _ ## v ## _V)

/* TX descriptor */
enum Openeth_TX_Desc_State {
    /*
     * Bit positions:
     */
    kOpeneth_TX_Desc_State_CarrierSenseLost_S         = 0, // RO
    kOpeneth_TX_Desc_State_DeferIndication_S          = 1, // RO
    kOpeneth_TX_Desc_State_LateCollisionOccured_S     = 2, // RO
    kOpeneth_TX_Desc_State_FailedDueToRetransitLimit_S= 3, // RO
    kOpeneth_TX_Desc_State_NumberOfRetries_S          = 4, // RO
    kOpeneth_TX_Desc_State_UnderrunStatus_S           = 8, // RO
    kOpeneth_TX_Desc_State_AddCRC_S                   = 11, // RW
    kOpeneth_TX_Desc_State_AddPadding_S               = 12, // RW
    kOpeneth_TX_Desc_State_WrapAround_S               = 13, // RW (Last descriptor in desc table has 1)
    kOpeneth_TX_Desc_State_GenerateTxIRQ_S            = 14, // RW
    kOpeneth_TX_Desc_State_Ready_S                    = 15, // RW (0 - owned by software, 1 - owned by hardware. Hardware cleared)
};
typedef struct {
    u16 state;             // Descriptor state/flags
    u16 length;            // Data size
    void* data_ptr;        // Data pointer
} Opencoreeth_TX_desc_t;
enum Openeth_RX_Desc_State {
    /*
     * Bit positions:
     */
    kOpeneth_RX_Desc_State_LateCollision_S            = 0, // RO
    kOpeneth_RX_Desc_State_CRCError_S                 = 1, // RO
    kOpeneth_RX_Desc_State_FrameTooShort_S            = 2, // RO
    kOpeneth_RX_Desc_State_FrameTooLong_S             = 3, // RO
    kOpeneth_RX_Desc_State_DribbleNibble_S            = 4, // RO (Length % 8 != 0)
    kOpeneth_RX_Desc_State_InvalidSymbol_S            = 5, // RO
    kOpeneth_RX_Desc_State_Overrun_S                  = 6, // RO
    kOpeneth_RX_Desc_State_PromisciousModeFrame_S     = 7, // RO
    kOpeneth_RX_Desc_State_WrapAround_S               = 13, // RW (Last descriptor in desc table has 1)
    kOpeneth_RX_Desc_State_GenerateRxIRQ_S            = 14, // RW
    kOpeneth_RX_Desc_State_Empty_S                    = 15, // RW
};

typedef struct {
    u16 state;             // Descriptor state/flags
    u16 length;            // Data size
    void* data_ptr;        // Data pointer
} Opencoreeth_RX_desc_t;

#define LT_ETH_RX_DMA_BUFS 8
#define LT_ETH_TX_DMA_BUFS 8
#define LT_ETH_DMA_BUF_SIZE 1600

#define OPENCORES_RX_DESC_BIT(name)              (1 << kOpeneth_RX_Desc_State_ ## name ## _S)
#define OPENCORES_TX_DESC_BIT(name)              (1 << kOpeneth_TX_Desc_State_ ## name ## _S)
#define OPENCORES_ETH_TX_DESC(i) ((volatile Opencoreeth_TX_desc_t*)(kOpencoresEthernet_RegisterDESCBASE_B) + i)
#define OPENCORES_ETH_RX_DESC(i) ((volatile Opencoreeth_RX_desc_t*)(kOpencoresEthernet_RegisterDESCBASE_B) + LT_ETH_TX_DMA_BUFS + i)

typedef struct {
    LTMacAddress address;
    LTThread hThread;
    LTMutex *mtx;
    bool bShutdown;
    struct netif *netifp;
    u8 rx_desc_idx;
    u8 tx_desc_idx;
    u8 *rx_dma_buf[LT_ETH_RX_DMA_BUFS];
    u8 *tx_dma_buf[LT_ETH_TX_DMA_BUFS];
}LTEthernetAdapter;

static LTEthernetAdapter s_EthAdapter;

/*******************************************************************************
** Transport Event Handling
*******************************************************************************/

LOC void NotifySocketEvent(Priv_Sock *sock, int event) {
    if (!sock || !sock->socketData) return;
    TBUG_INIT(sock);
    // P("--- sock %p, data %p, event %x, handle %x\n", sock, sock->socketData, sock->socketData->event, sock->socketData->h_socket);
    if ((event == kLTSocket_Event_ReadReady)  && !LTAtomic_CompareAndExchange(&sock->socketData->readPending, 0, 1))  return;
    if ((event == kLTSocket_Event_WriteReady) && !LTAtomic_CompareAndExchange(&sock->socketData->writePending, 0, 1)) return;
    iEvent->NotifyEvent(sock->socketData->event, sock->socketData->h_socket, event);
}

LOC Priv_Sock *NewSocket(struct tcp_pcb *pcb, Priv_Sock *parent) {
    Priv_Tran *tran = parent->privTran;
    LTSocket h_socket = tran->createSocket(tran->tranData, NULL, parent->socketData->event); // use same event handler as the listener
    if (!h_socket) return 0;
    LTSocket_Data *socket = pCore->ReserveHandlePrivateData(h_socket);
    socket->connected = true; // remove this as duplicate???

    // Clone lower socket and update fields:
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    *sock = (Priv_Sock) {
        .privTran    = parent->privTran,
        .socketData  = socket,
        .tcp_pcb     = pcb,
        .ipAddr      = pcb->remote_ip,
        .ipPort      = pcb->remote_port,
        .myPort      = pcb->local_port,
        .ipProto     = kIPProtocol_TcpClient,
        .connected   = true
    };
    LOCK_NET(tcp_arg(pcb, sock));
    pCore->ReleaseHandlePrivateData(h_socket, socket);
    return sock;
}

LOC void RxAppendSocket(Priv_Sock *sock, struct pbuf *pbuf, LTNetIpv4Endpoint *senderAddr) {
    TBUG_INIT(sock);
    if (tran->debug) DumpPbufInfo(__FUNCTION__, pbuf);
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
    if (!sock->rxPbuf) sock->rxPbuf = pbuf;
    else pbuf_cat(sock->rxPbuf, pbuf);
    NotifySocketEvent(sock, kLTSocket_Event_ReadReady);
}

// LwIP requires this global function for events (rather than using an API to set it)
EXP err_t lwip_tcp_event(void *arg, struct tcp_pcb *pcb, enum lwip_event event, struct pbuf *pbuf, u16_t size, err_t err) {
    if (!arg) return ERR_ARG;
    LT_UNUSED(pcb);
    LT_UNUSED(size);
    LT_UNUSED(err);
    Priv_Sock *sock = (Priv_Sock*)arg;
    TBUG_INIT(sock);
#ifdef LT_DEBUG
        static const char *LwipEventNames[] =
            {"accept", "sent", "recv", "connected", "poll", "error"};  // enum lwip_event
        //u32 thread = iThread->GetCurrentThread(); // debug
        //u8 pri = iThread->GetPriority(thread);    // debug
        if (event > LWIP_EVENT_ERR) return ERR_VAL; // clip it
        TBUG("----- Event %s ----- PCB: %p Pbuf: %p Size: %u Err %d\n", LwipEventNames[(u8)event], pcb, pbuf, size, err);
#endif
    switch (event) {
        case LWIP_EVENT_ACCEPT:
            if (err == ERR_OK) {
                if (!sock->ipAllow.addr || sock->ipAllow.addr == pcb->remote_ip.addr) {
                    Priv_Sock *nsock = NewSocket(pcb, sock); // New PCB, create a socket to match.
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
            RxAppendSocket(sock, pbuf, NULL);
            break;
        case LWIP_EVENT_CONNECTED:
            TBUG("--- PCB IP state: %d, ip: %08lx, port %d, mss %d\n",
                sock->tcp_pcb->state, sock->tcp_pcb->local_ip.addr, sock->tcp_pcb->local_port, sock->tcp_pcb->mss);
            NotifySocketEvent(sock, kLTSocket_Event_Connected);
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady); // tcp_sndbuf! for size
            break;
        case LWIP_EVENT_POLL:
            break;
        case LWIP_EVENT_ERR:
            LTLOG_YELLOWALERT("evt.err", "Transport driver event error: %d", err);
            switch (err) {
                case ERR_ABRT:  /** Connection aborted.      */
                case ERR_RST:   /** Connection reset.        */
                case ERR_CLSD:  /** Connection closed.       */
                    if (sock->tcp_pcb->state == SYN_SENT) {
                        NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                    }
                    NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
                    break;

                default:
                    NotifySocketEvent(sock, kLTSocket_Event_Error);
                    break;
            }
            // On error the PCB is freed within LWIP and should not be referenced ever again
            sock->tcp_pcb = NULL;
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
    RxAppendSocket(sock, pbuf, &endpoint);
}

CBK unsigned char OnRawReceive(void *arg, struct raw_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *addr) {
    LT_UNUSED(pcb);
    OnUdpReceive(arg, NULL, pbuf, addr, 0);
    return 1;
}

/*******************************************************************************
** Network Initialization
*******************************************************************************/
static void OpencoresEthernetSetMacAddr(LTEthernetAdapter * pAdapt) {
    ETH_REG(MACADDR0) = (((u32)pAdapt->address.octet[5]) | ((u32)pAdapt->address.octet[4]<<8) | ((u32)pAdapt->address.octet[3]<<16) | ((u32)pAdapt->address.octet[2]<<24));
    ETH_REG(MACADDR1) = (((u32)pAdapt->address.octet[1]) | ((u32)pAdapt->address.octet[0]<<8));
}

static void OpencoresEthernetGetMacAddr(LTEthernetAdapter * pAdapt) {
    u32 mac_reg_0 = ETH_REG(MACADDR0);
    u32 mac_reg_1 = ETH_REG(MACADDR1);
    pAdapt->address.octet[1] = (u8)(mac_reg_1 & 0xff);
    pAdapt->address.octet[0] = (u8)((mac_reg_1 >> 8) & 0xff);
    pAdapt->address.octet[5] = (u8)(mac_reg_0 & 0xff);
    pAdapt->address.octet[4] = (u8)((mac_reg_0 >> 8) & 0xff);
    pAdapt->address.octet[3] = (u8)((mac_reg_0 >> 16) & 0xff);
    pAdapt->address.octet[2] = (u8)((mac_reg_0 >> 24) & 0xff);
}

static void OpencoresEthernetSetState(bool bUp) {
    LTLOG_DEBUG("eth.set.state", "to %s", (bUp)?("Up"):("Down"));
    if (bUp) {
        // Interrupts are routed at BSP level, now just enable them on EMAC
        ETH_REG(MODER) |= ETH_REG_MASK(MODER,TX_EN) | ETH_REG_MASK(MODER,RX_EN);
        ETH_REG(INTMASK) |= ETH_REG_MASK(INTMASK,RX) | ETH_REG_MASK(INTMASK,BUSY); // TX?
    } else {
        ETH_REG(INTMASK) &= ~(ETH_REG_MASK(INTMASK,RX) | ETH_REG_MASK(INTMASK,BUSY)); // TX?
        ETH_REG(MODER) &= ~(ETH_REG_MASK(MODER,TX_EN) | ETH_REG_MASK(MODER,RX_EN));
    }
}

static bool OpencoresEthernetTransmit(LTEthernetAdapter * pAdapt, LTBufferChain *bufferChain) {
    while (bufferChain) {
        LT_ASSERT(bufferChain->bytesUsed <= LT_ETH_DMA_BUF_SIZE);
        if (bufferChain->bytesUsed > LT_ETH_DMA_BUF_SIZE) {
            return false;
        }
        pAdapt->mtx->API->Lock(pAdapt->mtx);
        Opencoreeth_TX_desc_t currDesc = *OPENCORES_ETH_TX_DESC(pAdapt->tx_desc_idx);
        if (currDesc.state & OPENCORES_TX_DESC_BIT(Ready)) {
            pAdapt->mtx->API->Unlock(pAdapt->mtx);
            LT_ASSERT(0 && "TX desc is busy");
            return false;
        }
        DEBUG_TX("eth.tx.desc", "id %u, len %lu", pAdapt->tx_desc_idx, bufferChain->bytesUsed);
        lt_memcpy(pAdapt->tx_dma_buf[pAdapt->tx_desc_idx], bufferChain->buffer, bufferChain->bytesUsed);
        currDesc.state |= OPENCORES_TX_DESC_BIT(Ready);
        if (pAdapt->tx_desc_idx == LT_ETH_TX_DMA_BUFS-1) {
            currDesc.state |= OPENCORES_TX_DESC_BIT(WrapAround);
        }
        currDesc.length = bufferChain->bytesUsed;
        *OPENCORES_ETH_TX_DESC(pAdapt->tx_desc_idx) = currDesc;
        pAdapt->tx_desc_idx = (pAdapt->tx_desc_idx + 1) % LT_ETH_TX_DMA_BUFS;
        pAdapt->mtx->API->Unlock(pAdapt->mtx);
        bufferChain = bufferChain->next;
    }
    return true;
}

// Convert LwIP buffers to LT buffers ethernet output
CBK err_t OnLwIPOutput(struct netif *netif, struct pbuf *pbuf) {
    // Eth is not up.
    if (!s_EthAdapter.hThread || !netif) return ERR_CLSD;

    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    LTBufferChain net_buf;
    LTBufferChain *wbuf = &net_buf;
    err_t ret;
    u32 total_len = 0;
    u8 *pb = (u8*)(pbuf->payload);
    int n;
    net_buf.next = NULL;
    for (n = 0; pbuf; pbuf = pbuf->next, n++) {
        // PR("%s: [%d] len: %u\n", __FUNCTION__, n, pbuf->len);
        wbuf->buffer    = pbuf->payload;
        wbuf->size      = pbuf->len;
        wbuf->bytesUsed = pbuf->len;
        if (pbuf->next) {
            wbuf->next      = lt_malloc(sizeof(LTBufferChain));
        } else {
            wbuf->next = NULL;
        }
        wbuf             = wbuf->next;
        total_len       += (u32)pbuf->len;
    }
    tran->metrics.lowerBytesTx += total_len;
    TBUG("Eth transmit: %lu bytes in %d bufs (thread %lx)\n", total_len, n, LT_PLT_HANDLE(iThread->GetCurrentThread()));
    if (tran->debug) DumpBytes("eth out", (total_len > 40) ? 40 : total_len, pb);
    if (!OpencoresEthernetTransmit(&s_EthAdapter, &net_buf)) {
        ret = ERR_IF;
    } else {
        ret = ERR_OK;
    }
    wbuf = net_buf.next;
    while (wbuf) {
        LTBufferChain *wbuf_next = wbuf->next;
        lt_free(wbuf);
        wbuf = wbuf_next;
    }
    return ret;
}

static bool OpencoresEthernetRxBuff(LTEthernetAdapter * pAdapt) {
    struct netif* netif = pAdapt->netifp;
    if (!netif || pAdapt->bShutdown) return false;
    Opencoreeth_RX_desc_t currDesc = *OPENCORES_ETH_RX_DESC(pAdapt->rx_desc_idx);
    if (currDesc.state & OPENCORES_RX_DESC_BIT(Empty)) {
        return false;
    }
    DEBUG_RX("eth.rx.desc", "id %u, len %u, state 0x%x\n", pAdapt->rx_desc_idx, currDesc.length, currDesc.state);
    // Copy directly to lwip buffer and process it here
    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    u32 len = currDesc.length;
    tran->metrics.lowerBytesRx += len;
    if (tran->debug) DumpBytes(__FUNCTION__, len, currDesc.data_ptr);
    LT_ASSERT(len <= (u32)LT_U16_MAX);
    LOCK_API;
    struct pbuf *pbuf = pbuf_alloc(PBUF_RAW, (u16)len, PBUF_POOL);
    if (pbuf) pbuf_take(pbuf, currDesc.data_ptr, len); // never fails because alloc'd above
    else {
        UNLOCK_API;
        LTLOG_YELLOWALERT("in.oom.obuf", "out of buf mem");
        return false;
    }
    if (pbuf) netif->input(pbuf, netif);
    UNLOCK_API;

    pAdapt->mtx->API->Lock(pAdapt->mtx);
    OPENCORES_ETH_RX_DESC(pAdapt->rx_desc_idx)->state |= OPENCORES_RX_DESC_BIT(Empty);
    pAdapt->rx_desc_idx = (pAdapt->rx_desc_idx + 1) % LT_ETH_RX_DMA_BUFS;
    pAdapt->mtx->API->Unlock(pAdapt->mtx);
    return true;
}

static void OpencoresEthernetRxTask(void * pData) {
    while(OpencoresEthernetRxBuff(pData));
}

static void OpencoresEthernetIRQHandler(void) {
    u32 int_src = ETH_REG(INTSRC);
    DEBUG_IRQ("eth.irq", "src %X", int_src);
    // Clear
    ETH_REG(INTSRC) = int_src;
    // rx
    if ((int_src & ETH_REG_MASK(INTSRC,RX)) && s_EthAdapter.hThread && !s_EthAdapter.bShutdown) {
        // ILTThread * irqThread = lt_gethandleinterface(ILTThread, s_EthAdapter.hThread);
        iThread->QueueTaskProc(s_EthAdapter.hThread, OpencoresEthernetRxTask, NULL, &s_EthAdapter);
    }

    // tx
    if (int_src & ETH_REG_MASK(INTSRC,BUSY)) {
        ;
    }

}

static bool EthernetRx_InitThread(void) {
    s_EthAdapter.mtx->API->Lock(s_EthAdapter.mtx);
    pCore->SetInterruptVector(OPENCORES_ETH_INT_NUMBER, OpencoresEthernetIRQHandler, OPENCORES_ETH_INT_PRIORITY);
    OpencoresEthernetSetState(true);
    s_EthAdapter.mtx->API->Unlock(s_EthAdapter.mtx);
    return true;
}

static void EthernetRx_ExitThread(void) {
    s_EthAdapter.mtx->API->Lock(s_EthAdapter.mtx);
    OpencoresEthernetSetState(false);
    s_EthAdapter.mtx->API->Unlock(s_EthAdapter.mtx);
}

static void OpencoresEthernetCleanup(LTEthernetAdapter * pAdapt) {
    if (pAdapt->hThread) {
        pAdapt->bShutdown = true;
        iThread->Terminate(pAdapt->hThread);
        iThread->WaitUntilFinished(pAdapt->hThread, LTTime_Infinite());
        lt_destroyhandle(pAdapt->hThread);
        pAdapt->hThread = 0;
    }
    pAdapt->netifp = NULL; /* Is used by RX thread, reset it only once thread is gone */
    for (u8 i = 0; i < LT_ETH_RX_DMA_BUFS; i++) {
        if (pAdapt->rx_dma_buf[i]) {
            lt_free(pAdapt->rx_dma_buf[i]);
            pAdapt->rx_dma_buf[i] = NULL;
        }
    }
    for (u8 i = 0; i < LT_ETH_TX_DMA_BUFS; i++) {
        if (pAdapt->tx_dma_buf[i]) {
            lt_free(pAdapt->tx_dma_buf[i]);
            pAdapt->tx_dma_buf[i] = NULL;
        }
    }
    if (pAdapt->mtx) {
        lt_destroyobject(pAdapt->mtx);
        pAdapt->mtx = NULL;
    }
}

static bool OpencoresEthernetInit(LTEthernetAdapter * pAdapt) {
    if (ETH_REG(MODER) != ETH_REG_VAL(MODER, RST_VALUE)) {
        LTLOG("err.no.qemu", "Opencores ethernet is not present (not running under QEMU?)");
        return false;
    }
    pAdapt->bShutdown = false;
    // mutex
    pAdapt->mtx = lt_createobject(LTMutex);
    if (!pAdapt->mtx) return false;

    do {
        pAdapt->mtx->API->Lock(pAdapt->mtx);

        // Setup RX and TX rings
        for (u8 i = 0; i < LT_ETH_RX_DMA_BUFS; i++) {
            LT_ASSERT(!pAdapt->rx_dma_buf[i]);
            pAdapt->rx_dma_buf[i] = lt_malloc(LT_ETH_DMA_BUF_SIZE);
            if (!pAdapt->rx_dma_buf[i]) break;
            OPENCORES_ETH_RX_DESC(i)->data_ptr = pAdapt->rx_dma_buf[i];
            OPENCORES_ETH_RX_DESC(i)->length = LT_ETH_DMA_BUF_SIZE;
            OPENCORES_ETH_RX_DESC(i)->state = OPENCORES_RX_DESC_BIT(GenerateRxIRQ) | OPENCORES_RX_DESC_BIT(Empty);
        }
        OPENCORES_ETH_RX_DESC(LT_ETH_RX_DMA_BUFS-1)->state |= OPENCORES_RX_DESC_BIT(WrapAround);
        pAdapt->rx_desc_idx = 0;

        for (u8 i = 0; i < LT_ETH_TX_DMA_BUFS; i++) {
            LT_ASSERT(!pAdapt->tx_dma_buf[i]);
            pAdapt->tx_dma_buf[i] = lt_malloc(LT_ETH_DMA_BUF_SIZE);
            if (!pAdapt->tx_dma_buf[i]) break;
            OPENCORES_ETH_TX_DESC(i)->data_ptr = pAdapt->tx_dma_buf[i];
            OPENCORES_ETH_TX_DESC(i)->length = LT_ETH_DMA_BUF_SIZE;
            OPENCORES_ETH_TX_DESC(i)->state = 0;
        }
        OPENCORES_ETH_TX_DESC(LT_ETH_TX_DMA_BUFS-1)->state |= OPENCORES_TX_DESC_BIT(WrapAround);
        pAdapt->tx_desc_idx = 0;

        // Set interrupt to thread.
        pAdapt->hThread = pCore->CreateThread("eth_rx");
        if (!pAdapt->hThread) break;
        iThread->SetStackSize(pAdapt->hThread, 1024);
        iThread->Start(pAdapt->hThread, EthernetRx_InitThread, EthernetRx_ExitThread);

        // Reset
        ETH_REG(MODER) |= ETH_REG_MASK(MODER,RESET);
        ETH_REG(MODER) &= ~ETH_REG_MASK(MODER,RESET);

        // Set number of TX buffers and max packet len
        ETH_REG(TXDESCNUM) = LT_ETH_TX_DMA_BUFS;
        u32 packlen_reg = ETH_REG(PACKETLEN);
        packlen_reg &= ~ETH_REG_MASK(PACKETLEN, MAXFL);
        packlen_reg |= 1536;
        ETH_REG(PACKETLEN) = packlen_reg;

        // Set MAC address from pAdapt
        OpencoresEthernetSetMacAddr(pAdapt);

        pAdapt->mtx->API->Unlock(pAdapt->mtx);
        return true;
    } while (0);

    // cleanup on anything failed
    pAdapt->mtx->API->Unlock(pAdapt->mtx);
    OpencoresEthernetCleanup(pAdapt);
    return false;
}

static bool OpencoresEthernetDeinit(LTEthernetAdapter * pAdapt) {
    if (ETH_REG(MODER) != (ETH_REG_VAL(MODER, RST_VALUE) | ETH_REG_MASK(MODER,TX_EN) | ETH_REG_MASK(MODER,RX_EN))) {
        LTLOG("err.deinit.no.adapter", "Opencores ethernet is not present (not running under QEMU?)");
        return false;
    }
    OpencoresEthernetCleanup(pAdapt);
    return true;
}

typedef u8 EthernetLink_Status;
enum EthernetLink_Status {
    kLink_Status_If_Up,
    kLink_Status_If_Down,
    kLink_Status_Connected,
    kLink_Status_Disconnected,
};

CBK err_t OnNetAdded(struct netif *netif) {
    // Bring Ethernet interface
    lt_memset(&s_EthAdapter, 0, sizeof(LTEthernetAdapter));
    s_EthAdapter.netifp = netif;
    OpencoresEthernetGetMacAddr(&s_EthAdapter);
    if (lt_memcmp(&s_EthAdapter.address, &(LTMacAddress)LTMacAddress_Zero, sizeof(LTMacAddress)) == 0) {
        // Generate random MAC
        LTMacAddress tmp = {{0xaa,0xbc,0xcc,0xdd,0xee,0x11}};
        pLCrypto->GenRandomBytes((u8 *)&tmp.octet[2], sizeof(LTMacAddress)-2);
        lt_memcpy(&s_EthAdapter.address, &tmp, ETHARP_HWADDR_LEN);
    }
    lt_memcpy(netif->hwaddr, s_EthAdapter.address.octet, ETHARP_HWADDR_LEN);
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    // Note: WiFi may not be UP here, so don't depend on it.
    // LwIp will call "output" when a packet must be sent.
    netif->output = etharp_output;
    // LwIP ARP will call "linkoutput" when a packet must be sent.
    netif->linkoutput = OnLwIPOutput;

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
    OpencoresEthernetInit(&s_EthAdapter);
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

CBK void OnDhcpChange(struct netif *netif, u8_t old_state, u8_t new_state) {
    Priv_Tran *tran = GET_NETIF_PRIV_TRAN(netif);
    LT_UNUSED(DhcpStateNames); // debug only
    // P("dhcp %s -> %s\n", DhcpStateNames[old_state], DhcpStateNames[new_state]);
    if (old_state != new_state && new_state == DHCP_STATE_BOUND) {
        dhcp_supplied_address(netif);
        struct dhcp *dhcp = netif_dhcp_data(netif);
        LOCK_NET(netif_set_addr(netif, &dhcp->offered_ip_addr, &dhcp->offered_sn_mask, &dhcp->offered_gw_addr));
        tran->ipAddr = netif->ip_addr;
        tran->ipMask = netif->netmask;
        tran->ipGate = netif->gw;
        #if 0
        PR(">>>>>>>> IP: %s ", FormIp4Addr(&tran->ipAddr));
        PR("GW: %s ",          FormIp4Addr(&tran->ipGate));
        PR("OIP: %s\n",        FormIp4Addr(&dhcp->offered_ipAddr));
        #endif
        tran->linkConnected = true;
        iEvent->NotifyEvent(tran->driverData->hEvent, 0, kLTTransport_Event_Up);
    }
}

static void OnLinkChange(EthernetLink_Status status, Priv_Tran *tran) {
    struct netif *netif = &tran->netif;
    if (!netif) return;
    switch (status) {
        case kLink_Status_If_Up: // Can now get MAC address and setup LWIP netif
            LOCK_API;
            netif_set_default(netif);
            netif_set_up(netif);
            UNLOCK_API;
            break;
        case kLink_Status_Connected:
            LOCK_API;
            netif_set_link_up(netif);
            if (tran->useDhcp) {
                // Assumes single thread accessor -- valid?
                dhcp_register_state_callback(netif, OnDhcpChange); // okay to call more than once
                dhcp_start(netif);
                UNLOCK_API;
            } else {
                dhcp_inform(netif); // Tell it what static IP we insist on using
                UNLOCK_API;
                tran->linkConnected = true;
                iEvent->NotifyEvent(tran->driverData->hEvent, 0, kLTTransport_Event_Up);
            }
            u32 count = tran->metrics.connections;
            tran->metrics = (LTTransport_Metrics) { // reset metrics on new connection
                .connections = count + 1
            };
            break;
        case kLink_Status_Disconnected:
        case kLink_Status_If_Down:
            tran->linkConnected = false;
            LOCK_API;
            if (tran->useDhcp) {
                dhcp_stop(netif);
                dhcp_cleanup(netif);
                tran->useDhcp = false;
            }
            netif_set_link_down(netif);
            netif_set_down(netif);
            UNLOCK_API;
            iEvent->NotifyEvent(tran->driverData->hEvent, 0, kLTTransport_Event_Down);
            break;
        default:
            break;
    }
}

LOC void StartNetwork(Priv_Tran *tran) {
    PF;

    lt_lwip_sys_init();
    lwip_init();

    do {
        // Basic network interface setup, but prior to wifi operation:
        LOCK_NET(netif_add(&tran->netif, &tran->ipAddr, &tran->ipMask, &tran->ipGate, NULL, &OnNetAdded, &netif_input));
        OnLinkChange(kLink_Status_If_Up,tran);
        OnLinkChange(kLink_Status_Connected,tran);
        return;
    } while (0);

    LTLOG_REDALERT("init.fail", "failed");
}

/*******************************************************************************
** DNS Functions
*******************************************************************************/

FWD void NetLwIpEthernet_ConnectSocket(LTSocket_Data *socket);

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

LOC void OnDnsHostResolved(const char *name, const ip_addr_t *ipaddr, void *arg) { LT_UNUSED(name);
    Priv_Sock *sock = (Priv_Sock*)arg;
    LT_UNUSED(FormIp4Addr); // debug only
    LTLOG_DEBUG("dns.done", "DNS done %s -> %s\n", name, FormIp4Addr(ipaddr));
    iThread->KillTimer(sock->privTran->lwipThread, OnDnsTimer, sock);
    LTSocket_Event event;
    if (!ipaddr) event = kLTSocket_Event_DnsTimeout;
    else {
        event = kLTSocket_Event_DnsResolved;
        sock->ipAddr.addr = ipaddr->addr;
        NetLwIpEthernet_ConnectSocket(sock->socketData);
    }
    NotifySocketEvent(sock, event);
}

LOC void ResolveDnsName(const char *name, Priv_Sock *sock) {
    err_t err;
    LOCK_NET(err = dns_gethostbyname(name, &sock->ipAddr, OnDnsHostResolved, sock));
    LTLOG_DEBUG("dns.res", "DNS resolve \"%s\" status: %d", name, err);
    switch (err) {
        case ERR_OK: // found in local cache
            NotifySocketEvent(sock, kLTSocket_Event_DnsResolved);
            break;
        case ERR_INPROGRESS: // full DNS lookup in progress
            iThread->SetTimer(sock->privTran->lwipThread, LTTime_Milliseconds(1000), OnDnsTimer, NULL, sock);
            break;
        default:
            LTLOG("dns.fail", "DNS failed %d on %s\n", err, name);
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

PUB u32 NetLwIpEthernet_OpenTransport(LTTransportData *tranData, LTSocket_Create *createSocket) {
    LTTransportDriver *driverData = tranData->driverData;

    if (driverData->privData) return sizeof(Priv_Sock); // driver is already setup
    Priv_Tran *tran = lt_malloc(sizeof(Priv_Tran));
    if (!tran) return 0;
    driverData->privData = tran;
    *tran = (Priv_Tran) {
        .createSocket  = createSocket,
        .tranData      = tranData,
        .driverData    = driverData
    };

    // Parse specification string
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use symbolic abbreviations for words. Example: "gw" rather than "gateway"
    char *spec = lt_strdup(driverData->tranSpec);
    char **tokens = TokenizeSpec(spec);
    if (!tokens) {
        LTLOG_YELLOWALERT("open.tran", "token oom");
        lt_free(spec); // NULL ok
        return 0;
    }

    bool fail = false;
    s16 n; // arg num

    ip_addr_t ipAddr; // needed because ip4addr_aton sets bogus addr on parse failure

    n = FindToken(tokens, "ip:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        tran->ipAddr.addr = ipAddr.addr;
    }

    n = FindToken(tokens, "gw:");
    if (n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr)) {
        tran->ipGate.addr = ipAddr.addr;
    }

    n = FindToken(tokens, "mask:");
    if (!(n >= 0 && tokens[n+1] && ip4addr_aton(tokens[n+1], &ipAddr))) {
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

    tran->useDhcp = (FindToken(tokens, "dhcp") >= 0 || !tran->ipAddr.addr);
    if (!tran->useDhcp && (!tran->ipAddr.addr || !tran->ipGate.addr)) {
        fail = true;
        LTLOG_YELLOWALERT("init.no.ip", "no DHCP nor IP/GW settings");
    }

    if (FindToken(tokens, "debug") >= 0) tran->debug = true;
    lt_free(tokens);
    lt_free(spec);
    if (fail) return 0;

    tran->lwipThread = iThread->GetCurrentThread();

    StartNetwork(tran);

    return sizeof(Priv_Sock); // Used by LTNet for creating sockets
}

PUB void NetLwIpEthernet_CloseTransport(LTTransportDriver *driverData) { // Called from DestroyTransport
    PF;
    sys_timeouts_destroy();
    // All sockets get closed in LTNet before this is called
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;
    OnLinkChange(kLink_Status_If_Down,tran);
    OpencoresEthernetDeinit(&s_EthAdapter);
    LOCK_NET(netif_remove(&tran->netif));
    lt_lwip_sys_destroy();
    lt_free(tran);
    driverData->privData = NULL;
}

PUB bool NetLwIpEthernet_GetTransportSpec(LTTransportDriver *driverData, char *spec, u16 spec_size) {
    // Could be extended to take a spec like "ip:" and return just ip address. !!
    // LTNetCore asserts spec and spec_size are valid
    Priv_Tran *tran = driverData->privData;
    spec[0] = 0;
    if (!tran) return false;
    char *s = spec;
    char *e = s + spec_size - 1;
    if (tran->useDhcp) {
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

PUB void NetLwIpEthernet_GetMetrics(LTTransportDriver *driverData, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    // driverData is never NULL
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;
    lt_memset(metrics, 0, sizeOfMetrics);
    lt_memcpy(metrics, &tran->metrics, sizeof(metrics) > sizeOfMetrics ? sizeOfMetrics : sizeof(*metrics));
}

PUB s32 NetLwIpEthernet_IsOperating(LTTransportDriver *driverData, LTTransport_Nudge nudge) {
    Priv_Tran *tran = driverData->privData;
    if (!tran || !tran->linkConnected) return -1;
    if (nudge == kLTTransport_Nudge_Soft) {
        LOCK_NET(dhcp_inform(&tran->netif));
    }
    // !!! add wifi rejoin for reset
    // !!! deal with unsigned to signed and wrapping
    return tran->metrics.lowerBytesRx;
}

/*******************************************************************************
** API Socket Functions
*******************************************************************************/

PUB bool NetLwIpEthernet_OpenSocket(LTSocket_Data *sock_data) {
    // !!! Question: what if link not up
    LTLOG_DEBUG("sock.open", "open socket: %s", sock_data->spec);
    Priv_Sock *sock = GET_SOCKET_PRIV(sock_data);
    lt_memset(sock, 0, sizeof(Priv_Sock));
    sock->socketData = sock_data; // back reference
    //Priv_Tran *tran = sock_data->transData->driverData->privData;

    // Parse specification string
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use short symbolic abbreviations.
    char *spec = lt_strdup(sock_data->spec);
    char **tokens = TokenizeSpec(spec); // does malloc()
    if (!tokens) {
        lt_free(spec); // NULL ok
        return false;
    }

    sock->ipProto = kIPProtocol_TcpClient;
    do {
        // Note: "tcp" is the default.
        if (FindToken(tokens, "udp"   ) >= 0) { sock->ipProto = kIPProtocol_Udp;  break; }
        if (FindToken(tokens, "icmp"  ) >= 0) { sock->ipProto = kIPProtocol_Icmp; break; }
        if (FindToken(tokens, "raw"   ) >= 0) { sock->ipProto = kIPProtocol_Raw;  break; }
        if (FindToken(tokens, "dns"   ) >= 0) { sock->ipProto = kIPProtocol_Dns;  break; }
        if (FindToken(tokens, "listen") >= 0 && sock->ipProto == kIPProtocol_TcpClient) {
            sock->ipProto = kIPProtocol_TcpServer;
            break;
        }
    } while (0);

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

    n = FindToken(tokens, "no-connect");
    if (n >= 0) sock->noConnect = true;

    lt_free(tokens);
    lt_free(spec);

    sock->rxPbuf = NULL;

    if (sock->hostname && sock->privTran->linkConnected) ResolveDnsName(sock->hostname, sock);

    bool bind_failed = false;
    err_t err = 0;
    switch (sock->ipProto) {
        case kIPProtocol_TcpServer:
            sock->noConnect = true;
            // fall-thru
        case kIPProtocol_TcpClient:
            LOCK_NET(sock->tcp_pcb = tcp_new());
            if (!sock->tcp_pcb) break;
            LOCK_NET(tcp_arg(sock->tcp_pcb, sock));
            if (sock->noConnect || sock->ipProto == kIPProtocol_TcpServer) {
                LOCK_NET(err = tcp_bind(sock->tcp_pcb, IP_ANY_TYPE, sock->myPort));
                sock->myPort = sock->tcp_pcb->local_port;
                if (err) {
                    bind_failed = true;
                    LOCK_NET(tcp_close(sock->tcp_pcb));
                    break;
                }
            }
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if (!sock->noConnect && sock->ipAddr.addr) NetLwIpEthernet_ConnectSocket(sock_data);
            return true;
        case kIPProtocol_Udp:
            // if (!sock->ip_port) only if sending
            sock->noConnect = true;
            LOCK_NET(sock->udp_pcb = udp_new());
            if (!sock->udp_pcb) break;
            LOCK_NET(err = udp_bind(sock->udp_pcb, IP_ADDR_ANY, sock->myPort));
            sock->myPort = sock->udp_pcb->local_port;
            if (err) {
                bind_failed = true;
                LOCK_NET(udp_remove(sock->udp_pcb));
                break;
            }
            LOCK_NET(udp_recv(sock->udp_pcb, OnUdpReceive, sock));
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if (sock->ipAddr.addr) NetLwIpEthernet_ConnectSocket(sock_data);
            return true;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            sock->noConnect = true;
            // For raw frames, this will eventually want to get the IP_PROTO from the packet !!!
            LOCK_NET(sock->raw_pcb = raw_new(sock->ipProto == kIPProtocol_Icmp ? IP_PROTO_ICMP : 0));
            if (!sock->raw_pcb) break;
            LOCK_NET(err = raw_bind(sock->raw_pcb, &sock->privTran->ipAddr));
            if (err) {
                bind_failed = true;
                LOCK_NET(raw_remove(sock->raw_pcb));
                break;
            }
            LOCK_NET(raw_recv(sock->raw_pcb, OnRawReceive, sock));
            NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
            if (sock->ipAddr.addr) NetLwIpEthernet_ConnectSocket(sock_data);
            return true;
        case kIPProtocol_Dns: // do nothing more
            return true;
        default: break;
    }
    LTLOG_YELLOWALERT("set.fail", "OpenSocket failed: %s err: %d\n", bind_failed ? "bind" : "new", err);
    return false; // Should we also Notify? !!!
}

PUB bool NetLwIpEthernet_GetSocketSpec(LTSocket_Data *socket, char *spec, u16 spec_size) {
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    if (!spec) return false;
    char *s = spec;
    char *e = s + spec_size - 1;
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

PUB bool NetLwIpEthernet_GetSocketProperty(LTSocket_Data *sockData, const char *name, void *value) {
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "recv.endpoint.v4") == 0) {
        LTNetIpv4Endpoint *ep = value;
        *ep = sock->recvAddr;
    } else if (lt_strcmp(name, "local.endpoint.v4") == 0) {
        LTNetIpv4Endpoint *ep = value;
        ep->address = sock->privTran->ipAddr.addr;
        ep->port    = sock->ipPort;
    } else {
        LTLOG_YELLOWALERT("getprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB bool NetLwIpEthernet_SetSocketProperty(LTSocket_Data *sockData, const char *name, const void *value) {
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "send.endpoint.v4") == 0) {
        const LTNetIpv4Endpoint *ep = value;
        sock->ipAddr.addr = ep->address;
        sock->ipPort      = ep->port;
    } else {
        LTLOG_YELLOWALERT("setprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB void NetLwIpEthernet_CloseSocket(LTSocket h_socket) { // Also called by Transport.DestroySocket()
    LTLOG_DEBUG("sock.close", "close socket: %04x", h_socket);
    if (!h_socket) return;
    LTSocket_Data *socket = pCore->ReserveHandlePrivateData(h_socket);
    if (!socket || !socket->h_socket) return;
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    if (sock->any_pcb) {
        err_t err;
        switch (sock->ipProto) {
            case kIPProtocol_TcpClient:
            case kIPProtocol_TcpServer:
                LOCK_NET(err = tcp_close(sock->tcp_pcb));
                LOCK_NET(tcp_arg(sock->tcp_pcb, NULL));
                if (err) {
                    LTLOG_DEBUG("sock.close.code", "non-zero close socket: %d", err);
                    LOCK_NET(tcp_poll(sock->tcp_pcb, NULL, 4)); // try again (usage!!!)
                    pCore->ReleaseHandlePrivateData(h_socket, socket);
                    return;
                }
                break;
            case kIPProtocol_Udp:
                LOCK_NET(udp_remove(sock->udp_pcb));
                break;
            case kIPProtocol_Icmp:
            case kIPProtocol_Raw:
                LOCK_NET(raw_remove(sock->raw_pcb));
                break;
            default: break;
        }
    }
    if (sock->rxPbuf) {
        pbuf_free(sock->rxPbuf);
        sock->rxPbuf = NULL;
    }
    sock->connected = false;
    socket->h_socket = 0;
    pCore->ReleaseHandlePrivateData(h_socket, socket);
}

PUB void NetLwIpEthernet_ConnectSocket(LTSocket_Data *socket) {
    Priv_Sock *sock  = GET_SOCKET_PRIV(socket);
    if (!sock || !sock->tcp_pcb) return;           // socket is closed
    if (sock->ipProto >= kIPProtocol_Max) return; // bad protocol
    Priv_Tran *tran = socket->transData->driverData->privData;
    if (!netif_is_link_up(&tran->netif)) {
        LTLOG("sock.con.dwn", "not linked");
        return;
    }
    LTLOG_DEBUG("sock.con", "connect[%02x] %s ip: %s port: %d ...\n", sock->socketData->h_socket,
            IPProtocolNames[sock->ipProto], FormIp4Addr(&sock->ipAddr), sock->ipPort);
    err_t err = 0;
    switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
            if (sock->tcp_pcb->state > CLOSED) return; // don't try to do it again
            LOCK_NET(err = tcp_connect(sock->tcp_pcb, &sock->ipAddr, sock->ipPort, NULL));
            if (err) {
                NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                return;
            }
            break;
        case kIPProtocol_TcpServer:
            if (sock->tcp_pcb->state > CLOSED) return; // don't try to do it again
            LOCK_NET(sock->tcp_pcb = tcp_listen_with_backlog_and_err(sock->tcp_pcb, TCP_DEFAULT_LISTEN_BACKLOG, &err));
            if (!sock->tcp_pcb) {
                LTLOG("sock.lsn.err", "socket listen error %d", err);
            } else {
                //LOCK_NET(tcp_accept(sock->tcp_pcb, OnTcpAccept)); // !!!
            }
            break;
        case kIPProtocol_Udp:
            LOCK_NET(udp_connect(sock->udp_pcb, &sock->ipAddr, sock->ipPort));
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            LOCK_NET(raw_connect(sock->raw_pcb, &sock->ipAddr));
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;
        default: break;
    }
}

PUB void NetLwIpEthernet_DisconnectSocket(LTSocket_Data *socket) {
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
        case kIPProtocol_TcpServer:
            // !!! Should this be in DisconnectSocket or in CloseSocket?
            // !!! It can be a problem to close a socket that's still transferring data.
            //LOCK_NET(tcp_close(sock->tcp_pcb));  // check error result for NO_MEM and poll !!!
            //sock->tcp_pcb = NULL;
            return;
        case kIPProtocol_Udp:
            LOCK_NET(udp_disconnect(sock->udp_pcb));
            NotifySocketEvent(sock, kLTSocket_Event_Disconnected); // for connectionless sockets
            break;
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            LOCK_NET(raw_disconnect(sock->raw_pcb));
            NotifySocketEvent(sock, kLTSocket_Event_Disconnected); // for connectionless sockets
            break;
        default: break;
    }
}

PUB s32 NetLwIpEthernet_WriteSocket(LTSocket_Data *socket, const void *data, u32 data_len) {
    //LTLOG_DEBUG("skt.writ", "write %h data: %p, len: %d", __FUNCTION__, socket->handle, data, data_len);
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    Priv_Tran *tran = sock->privTran;
    TBUG("%s: %lu\n", __FUNCTION__, data_len);
    err_t err = 0;

    if (!sock->tcp_pcb) err = -1; // socket not open
    else switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
        case kIPProtocol_TcpServer:
            LOCK_NET(err = tcp_write(sock->tcp_pcb, data, data_len, TCP_WRITE_FLAG_COPY));
            if (err) break;
            LOCK_NET(tcp_output(sock->tcp_pcb));
            break;
        case kIPProtocol_Udp:
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            LOCK_API;
            struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, data_len, PBUF_RAM);
            err = pbuf_take(pbuf, data, data_len);
            if (!err) {
                if (sock->ipProto != kIPProtocol_Udp) err = raw_send(sock->raw_pcb, pbuf);  //raw_sendto(sock->raw_pcb, pbuf, &sock->ipAddr);
                else err = udp_sendto(sock->udp_pcb, pbuf, &sock->ipAddr, sock->ipPort); // check err, notify on failure !!!
                if (err == ERR_IF) err = ERR_MEM; // Tx Packet dropped
            }
            pbuf_free(pbuf);
            UNLOCK_API;
            break;
        default: break;
    }
    if (err) {
        if (err == ERR_MEM) return 0;
        LTLOG("wrt.err", "write error %d", err); // code is platform dependent
        NotifySocketEvent(sock, kLTSocket_Event_WriteError);
        return -1;
    }
    tran->metrics.upperBytesTx += data_len;
    return data_len;
}

PUB s32 NetLwIpEthernet_ReadSocket(LTSocket_Data *socket, void *data, u32 readLen) {
    // All of the fancy buffering happens in LwIP. So, it doesn't belong here.
    //LTLOG_DEBUG("skt.read", "read %h data: %p, len: %d", __FUNCTION__, socket->handle, data, readLen);
    Priv_Sock *sock = GET_SOCKET_PRIV(socket);
    Priv_Tran *tran = sock->privTran;
    TBUG("%s: %lu\n", __FUNCTION__, readLen);
    if (!sock->tcp_pcb) {  // socket not open
        NotifySocketEvent(sock, kLTSocket_Event_ReadError);
        return -1;
    }
    u16 tail = 0;
    bool query = !data && !readLen;
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
            if (sock->ipProto < kIPProtocol_TcpClient && readLen > pbuf->len) readLen = pbuf->len;
            u16 num = 0;
            while (readLen > 0 && (num = pbuf_copy_partial(pbuf, data, (u16)readLen, tail))) { // does not need LOCK_API
                tail += num;
                readLen -= num;
                //P("   ----num: %u len: %u tot: %u avail: %ld\n", num, pbuf->len, pbuf->tot_len, avail);
            }
        }
        if (!query) {
            tran->metrics.upperBytesRx += tail;
            TBUG("--------- Read %u bytes - avail: %ld (pbuf: %p tot: %u next: %p)\n", tail, readLen, pbuf, pbuf->tot_len, pbuf->next);
            sock->rxPbuf = pbuf_free_header(pbuf, tail);
        }
    }
    UNLOCK_API;
    if (!query && !sock->noConnect && tail > 0) {
        LOCK_NET(tcp_recved(sock->tcp_pcb, tail)); // adjust window
    }
    return tail;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

PUB void NetLwIpEthernetImpl_LibFini(void) {
    PF;
    if (pLCrypto) {
        LT_GetCore()->CloseLibrary((LTLibrary *)pLCrypto);
        pLCrypto = NULL;
    }
}

PUB bool NetLwIpEthernetImpl_LibInit(void) {
    pCore   = LT_GetCore();
    iEvent  = lt_getlibraryinterface(ILTEvent,  pCore);
    iThread = lt_getlibraryinterface(ILTThread, pCore);
    pLCrypto = (LTSystemCrypto *)LT_GetCore()->OpenLibrary("LTSystemCrypto");
    return true;
}

PUB void NetLwIpEthernet_Destroy(LTHandle handle) {
    PF;
    LT_UNUSED(handle);
    // Future: what else to cleanup???
}

/*******************************************************************************
 * Library Function Vectors - Exported as an LTNetDriver interface
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(NetLwIpEthernet, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(NetLwIpEthernet) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(LTNetDriver, NetLwIpEthernet_Destroy) {
    .OpenTransport     = NetLwIpEthernet_OpenTransport,
    .CloseTransport    = NetLwIpEthernet_CloseTransport,
    .GetTransportSpec  = NetLwIpEthernet_GetTransportSpec,
    .GetMetrics        = NetLwIpEthernet_GetMetrics,
    .IsOperating       = NetLwIpEthernet_IsOperating,
    .OpenSocket        = NetLwIpEthernet_OpenSocket,
    .GetSocketSpec     = NetLwIpEthernet_GetSocketSpec,
    .GetSocketProperty = NetLwIpEthernet_GetSocketProperty,
    .SetSocketProperty = NetLwIpEthernet_SetSocketProperty,
    .CloseSocket       = NetLwIpEthernet_CloseSocket,
    .ConnectSocket     = NetLwIpEthernet_ConnectSocket,
    .DisconnectSocket  = NetLwIpEthernet_DisconnectSocket,
    .WriteSocket       = NetLwIpEthernet_WriteSocket,
    .ReadSocket        = NetLwIpEthernet_ReadSocket
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(NetLwIpEthernet, (LTNetDriver));
