/*******************************************************************************
 *
 * LT LinuxDriverNetIP
 * -------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#define _GNU_SOURCE
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <paths.h>
#include <poll.h>
#include <resolv.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <utime.h>
#include <netinet/ip.h>

#include <lt/LT.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

#include <LinuxCloseFrom.h>

DEFINE_LTLOG_SECTION("lt.net.drv");

#define DEBUG_PREFIX "NetDrv"

/*******************************************************************************
** Local Constants and Definitions
*******************************************************************************/

#define P(...)
//#define PF P("==== %s (%d) ====\n", __FUNCTION__, __LINE__)
#define PF
#define NET_DEBUG(f, ...) if (tran->debug) LTLOG("net.dbg", DEBUG_PREFIX "-" f, ##__VA_ARGS__)
//#define ShowStatus(a, b) P("--- [%s] %s:%d\n", b, __FUNCTION__, __LINE__)
#define ShowStatus(a, b)

#define PUB static // Part of public interface
#define DBG static  // Debug function
#define FWD static  // Function forward reference
#define LOC static  // Local function related to API operation
#define UTL static  // Utility function

#define THREAD_CLIENTDATA_KEY   "netip"    // once we allow multiple transports, key will need to be transport  specific
#define TCP_LISTEN_BACKLOG      5          // might want to put this into transport config struct
#define EPOLL_WAIT_TIME_MSEC    500        // max wait time for epoll

#define TCP_KEEPCNT_DEFAULT     9
#define TCP_KEEPIDLE_DEFAULT    90
#define TCP_KEEPINTVL_DEFAULT   45

// epoll_...(2) wrappers
typedef struct {
    void (*func)(void *, uint32_t);
    void *data;
} EpollHandler;

LOC void EpollEventSocket(void *, uint32_t);
LOC void EpollEventNetlink(void *, uint32_t);
LOC void EpollEventDhcpcPipe(void *, uint32_t);

// This stack's data is stored in the transport handle allocation
typedef struct Priv_Tran {
    LTTransportData     *tranData;
    LTTransportDriver   *driverData;
    LTSocket_Create     *createSocket;   // Needed for listen to create new sockets (could expand to interface)
    LTThread             DnsThread;      // For asynchronous DNS resolution
    LTThread             EventThread;    // For Linux epoll loop
    int                  EpollFd;        // Epoll event file descriptor
    int                  NetlinkFd;      // Netlink socket
    EpollHandler         NetlinkEpollHandler;
    u32                  RtmSequence;
    bool                 UseDhcp;
    int                  DhcpcPipeFd;
    EpollHandler         DhcpcEpollHandler;
    pid_t                DhcpcPid;
    LTTransport_Metrics  metrics;        // Basic RX/TX metrics
    char                 IfName[IF_NAMESIZE];
    u32                  IfFlags;
    struct in_addr       ipAddr;         // Our address
    struct in_addr       ipMask;
    struct in_addr       ipGate;
    bool                 isRunning;
    bool                 isUp;
    bool                 debug;
} Priv_Tran;

// Compilation assert that there is adequate space for above struct:
int s_PrivateTranStructTooBig[(sizeof(Priv_Tran)) > (sizeof(PrivImpl)) ? -1 : 1];

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
} SocketFlags;

LOC const char *s_IPProtocolNames[] = { // must match above enum
    "?",
    "dns",
    "raw",
    "icmp",
    "udp",
    "tcp",
    "tcp listen"
};

// This stack's private data as stored within the socket handle:
typedef struct Priv_Sock {
    Priv_Tran          *privTran;      // mainly for debug
    LTSocket_Data      *socketData;    // points back up to handle data (could use math!!!)
    int                 socketFd;      // linux file descriptor
    struct sockaddr_in  address;       // remote address
    struct sockaddr_in  recvAddr;      // last receive address
    EpollHandler        epollHandler;  // callback used after epoll_wait(2)
    struct epoll_event  eventSpec;     // event trigger flags
    bool                ready;

    struct in_addr      ipAddr;        // remote ip, or local ip for listen
    u16                 ipPort;        // remote port or local port for TCP listen
//  struct in_addr      myAddr;        // (if we want to add local address, punt for now)
    u16                 myPort;        // local port
    struct in_addr      ipAllow;       // accept connections from this address only
    char               *hostname;      // for "host:" spec DNS lookup
    IPProtocol          ipProto;       // IP protocol, eg TCP UDP ...
    u8                  dscp;          // Differentiated Services Codepoint
    bool                connected;     // socket is connected
    u32                 flags;         // socket flags

} Priv_Sock;

#define GET_SOCKET_PRIV(s) (Priv_Sock*)(s + 1)  // stack private socket data

/*******************************************************************************
** Module Variables
*******************************************************************************/

static struct Statics {
    LTCore         *iCore;
    ILTThread      *iThread;
    ILTEvent       *iEvent;
    LTDeviceConfig *pDeviceConfig;
    LTDeviceWiFi   *pDeviceWiFi;
    const char     *DhcpInterface;
} S;

UTL void StopDhcpClient(Priv_Tran *);
UTL void StartDhcpClient(Priv_Tran *);
UTL void ShutdownDhcpClient(Priv_Tran *);

/*******************************************************************************
** Utilities
*******************************************************************************/

struct event_types {
    uint32_t    type;
    const char *name;
};

#define DEF_EVT(t) { t, #t }
struct event_types const s_EventTypes[6] = {
    DEF_EVT(EPOLLIN),
    DEF_EVT(EPOLLOUT),
    DEF_EVT(EPOLLRDHUP),
    DEF_EVT(EPOLLHUP),
    DEF_EVT(EPOLLERR),
    DEF_EVT(EPOLLET)
};

DBG void PrintEpollFlags(Priv_Tran *tran, Priv_Sock *sock, uint32_t events) {
    NET_DEBUG("epoll[%p] %p ", tran, sock);
    for (u8 n = 0; n < 6; n++) {
        if (events & s_EventTypes[n].type) { P("%s ", s_EventTypes[n].name); }
    }
    P("\n");
}

UTL char *AppendStr(const char *from, char *to, const char *end) { // LT has no strcat
    if (!to)   return NULL;
    if (!from) return to;
    u32 len = lt_strlen(from);
    if (to + len > end) return NULL;
    lt_memcpy(to, from, len+1);
    return to + len;
}

UTL char *IpToStr(const struct in_addr *addr, char *to, char *end) {
    char *out = inet_ntoa(*addr);
    return AppendStr(out, to, end);
}

UTL char *U32ToStr(char* buf, u16 max, u32 value) {  // LT has no u32tostr (itoa)
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

UTL void NotifyTransport(Priv_Tran *tran, LTTransport_Event event) {
    S.iEvent->NotifyEvent(tran->driverData->hEvent, 0, event);
}

#if defined(__UCLIBC__)
UTL void touch(const char *filename) {
    const time_t now = time(NULL);
    struct utimbuf times;
    struct stat stat_buf;
    if (stat(filename, &stat_buf) == 0) {
        times.actime = LT_MAX(now, stat_buf.st_atime + 1);
        times.modtime = LT_MAX(now, stat_buf.st_mtime + 1);
    } else {
        times.actime = now;
        times.modtime = now;
    }
    utime(filename, &times);
}
#endif

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
** Netlink Socket handling
*******************************************************************************/

LOC void CloseFd(int *pFd) {
    if (*pFd != -1) {
        close(*pFd);
        *pFd = -1;
    }
}

LOC bool InterfaceIsUp(u32 Flags) {
    const u32 WantedFlags = IFF_RUNNING | IFF_UP;
    return ((Flags & WantedFlags) == WantedFlags);
}

LOC bool SetNonBlock(int socketFd) {
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) return false;
    return (fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) != -1);
}

LOC void SetDscp(int socketFd, u8 dscp) {
    setsockopt(socketFd, IPPROTO_IP, IP_TOS, &(u8) { dscp << 2 }, sizeof(u8));
}

LOC void NagleOff(int socketFd) {
    static const int noDelay = 1;
    setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
}

LT_INLINE bool SameInAddr(const struct in_addr *pAddr1,
                          const struct in_addr *pAddr2) {
    return (pAddr1->s_addr == pAddr2->s_addr);
}

LOC void CreateNetlinkSocket(Priv_Tran *tran) {
    CloseFd(&tran->NetlinkFd);
   
    do {
        tran->NetlinkFd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (tran->NetlinkFd == -1) break;

        if (!SetNonBlock(tran->NetlinkFd)) break;

        static const struct sockaddr_nl NetlinkAddr = {
            .nl_family = AF_NETLINK,
            .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE
        };
        if (bind(tran->NetlinkFd,
                 (const struct sockaddr *)&NetlinkAddr,
                 sizeof(NetlinkAddr)) == -1) {
            break;
        }

        tran->NetlinkEpollHandler = (EpollHandler) {
            .func = EpollEventNetlink,
            .data = tran
        };
        struct epoll_event eventSpec = {
            .events   = EPOLLIN,
            .data.ptr = &tran->NetlinkEpollHandler
        };
        if (epoll_ctl(tran->EpollFd, EPOLL_CTL_ADD, tran->NetlinkFd, &eventSpec) == -1) break;

        return;
    } while (false);

    CloseFd(&tran->NetlinkFd);
}

LOC void Netlink_GetAddrFromAttrib(const struct in_addr **pInAddr,
                                   const void *data, const size_t payload) {
    if (payload == sizeof(struct in_addr))
        *pInAddr = data;
}

LOC void Netlink_LinkMsg(Priv_Tran *tran,
                         const struct nlmsghdr *hdr, size_t length) {
    if (NLMSG_PAYLOAD(hdr, length) < sizeof(struct ifinfomsg))
        return;

    const struct ifinfomsg *msg = NLMSG_DATA(hdr);
    if (msg->ifi_flags & IFF_LOOPBACK) return;

    char interface[IF_NAMESIZE] = {};

    for (const struct rtattr *attrib = (const struct rtattr *)&msg[1];
         RTA_OK(attrib, length);
         attrib = RTA_NEXT(attrib, length)) {
        if (attrib->rta_type != IFLA_IFNAME) continue;

        const size_t payload = RTA_PAYLOAD(attrib);
        if (payload > 0 && payload < sizeof(interface)) {
            lt_memcpy(interface, RTA_DATA(attrib), payload);
            interface[payload] = '\0';
        }
    }

    if (lt_strlen(interface) == 0) return;

    if (!tran->UseDhcp && lt_strlen(tran->IfName) == 0 &&
        InterfaceIsUp(msg->ifi_flags)) {
        // If the interface wasn't provided in the device configuration
        // (e.g. because LT is running on a development machine) pick
        // a suitable interface and stick to that.
        lt_strncpyTerm(tran->IfName, interface, sizeof(tran->IfName));
    }

    if (0 == lt_strcmp(interface, tran->IfName)) tran->IfFlags = msg->ifi_flags;
}

LOC void Netlink_AddrMsg(Priv_Tran *tran,
                         const struct nlmsghdr *hdr, size_t length) {
    if (NLMSG_PAYLOAD(hdr, length) < sizeof(struct ifaddrmsg))
        return;

    const struct ifaddrmsg *msg = NLMSG_DATA(hdr);
    if (msg->ifa_family != AF_INET || msg->ifa_prefixlen > 32)
        return;

    struct in_addr netmask;
    netmask.s_addr = htonl(0xFFFFFFFF << (32 - msg->ifa_prefixlen));

    const struct in_addr *addr = NULL;
    char interface[IF_NAMESIZE] = {};

    for (const struct rtattr *attrib = (const struct rtattr *)&msg[1];
         RTA_OK(attrib, length);
         attrib = RTA_NEXT(attrib, length)) {
        const void  *data = RTA_DATA(attrib);
        const size_t payload = RTA_PAYLOAD(attrib);
        switch (attrib->rta_type) {
        case IFA_LOCAL:
            Netlink_GetAddrFromAttrib(&addr, data, payload);
            break;

        case IFA_LABEL:
            if (payload > 0 && payload < sizeof(interface)) {
                lt_memcpy(interface, data, payload);
                interface[payload] = '\0';
            }
        }
    }

    if (!addr || lt_strlen(interface) == 0) return;
    if (0 != lt_strcmp(interface, tran->IfName)) return;

    const struct in_addr null_addr = { .s_addr = 0 };
    if (hdr->nlmsg_type == RTM_NEWADDR) {
        if (SameInAddr(&tran->ipAddr, &null_addr)) {
            tran->ipAddr = *addr;
            tran->ipMask = netmask;
        }
    } else {
        if (SameInAddr(&tran->ipAddr, addr)) {
            tran->ipAddr = null_addr;
            tran->ipMask = null_addr;
        }
    }
}

LOC void Netlink_RouteMsg(Priv_Tran *tran,
                          const struct nlmsghdr *hdr, size_t length) {
    if (NLMSG_PAYLOAD(hdr, length) < sizeof(struct rtmsg))
        return;

    const struct rtmsg *msg = NLMSG_DATA(hdr);
    if (msg->rtm_family != AF_INET)
        return;

    const struct in_addr *source = NULL;
    const struct in_addr *dest = NULL;
    const struct in_addr *gateway = NULL;

    for (const struct rtattr *attrib = (const struct rtattr *)&msg[1];
         RTA_OK(attrib, length);
         attrib = RTA_NEXT(attrib, length)) {
        const void  *data = RTA_DATA(attrib);
        const size_t payload = RTA_PAYLOAD(attrib);
        switch (attrib->rta_type) {
        case RTA_DST:
            if (msg->rtm_dst_len > 0)
                Netlink_GetAddrFromAttrib(&dest, data, payload);
            break;
        case RTA_SRC:
            if (msg->rtm_src_len > 0)
                Netlink_GetAddrFromAttrib(&source, data, payload);
            break;
        case RTA_GATEWAY:
            Netlink_GetAddrFromAttrib(&gateway, data, payload);
            break;
        default:
            P("rtmsg", "%d", attrib->rta_type);
        }
    }

    // The default gateway has neither a destination nor a source
    // but a gateway address.
    if (dest || source || !gateway) return;

    if (hdr->nlmsg_type == RTM_NEWROUTE) {
        tran->ipGate = *gateway;
    } else {
        tran->ipGate.s_addr = 0;
    }
}

LOC void Netlink_ReceiveMsg(Priv_Tran *tran) {
    const LT_SIZE NetlinkBufferSize = 8192;
    void *pNetlinkBuffer = lt_malloc(NetlinkBufferSize);
    if (!pNetlinkBuffer) return;

    ssize_t length;
    while ((length = recv(tran->NetlinkFd, pNetlinkBuffer, NetlinkBufferSize, 0)) > 0) {
        for (struct nlmsghdr *hdr = pNetlinkBuffer;
             NLMSG_OK(hdr, length);
             hdr = NLMSG_NEXT(hdr, length)) {
            switch (hdr->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                Netlink_LinkMsg(tran, hdr, length);
                break;

            case RTM_NEWADDR:
            case RTM_DELADDR:
                Netlink_AddrMsg(tran, hdr, length);
                break;

            case RTM_NEWROUTE:
            case RTM_DELROUTE:
                Netlink_RouteMsg(tran, hdr, length);
                break;

            default:
                P("hdr", "%d", hdr->nlmsg_type);
            }
        }
    }

    lt_free(pNetlinkBuffer);
}

typedef struct {
    struct nlmsghdr hdr;
    struct rtgenmsg msg;
} NetlinkRtm;

static bool Netlink_SendRtm(Priv_Tran *tran, u16 nlmsg_type) {
    NetlinkRtm rtm_msg = {
        .hdr.nlmsg_len = sizeof(rtm_msg),
        .hdr.nlmsg_type = nlmsg_type,
        .hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH,
        .hdr.nlmsg_seq = ++(tran->RtmSequence),
        .hdr.nlmsg_pid = 0,
        .msg.rtgen_family = AF_INET
    };
    if (send(tran->NetlinkFd, &rtm_msg, sizeof(rtm_msg), 0) == -1) return false;

    // Wait for up to 1 millisecond for a response by the kernel
    struct pollfd pfd = {
        .events = POLLIN,
        .fd     = tran->NetlinkFd
    };
    poll(&pfd, 1, 1);

    return true;
}

/*******************************************************************************
** Local Utility Defines and Functions
*******************************************************************************/

LOC void NotifySocketEvent(Priv_Sock *sock, int event) {
    //P("--- Queue event: 0x%02x, fd %d, hndl: %x\n", event, sock->socketFd, (int)sock->socketData->h_socket);
    if (!sock || !sock->socketData) return;
    if ((event == kLTSocket_Event_ReadReady)  && !LTAtomic_CompareAndExchange(&sock->socketData->readPending, 0, 1))  return;
    if ((event == kLTSocket_Event_WriteReady) && !LTAtomic_CompareAndExchange(&sock->socketData->writePending, 0, 1)) return;
    S.iEvent->NotifyEvent(sock->socketData->event, sock->socketData->h_socket, event);
}

LOC Priv_Sock *NetIp_CloneSocketAccept(Priv_Sock *parent) {
    PF;
    // Clone the parent socket to the new socket, then accept and update necessary fields:
    Priv_Tran *tran         = parent->privTran;
    LTSocket handle         = tran->createSocket(tran->tranData, NULL, parent->socketData->event); // use same event handler as the listener
    if (!handle) return 0;
    LTSocket_Data *sockData = S.iCore->ReserveHandlePrivateData(handle);
    sockData->connected     = true;

    Priv_Sock *sock         = GET_SOCKET_PRIV(sockData);
    *sock = (Priv_Sock) {
        .privTran           = parent->privTran,
        .socketData         = sockData,
        .ipProto            = kIPProtocol_TcpClient,
        .epollHandler.func  = EpollEventSocket,
        .epollHandler.data  = sock,
        .eventSpec.data.ptr = &sock->epollHandler,
        .eventSpec.events   = parent->eventSpec.events | EPOLLOUT,
        .ready              = true,
        .connected          = true,
    };

    // Set new fd and address info:
    do {
        socklen_t sockLen = sizeof(sock->address);
        sock->socketFd = accept(parent->socketFd, (struct sockaddr *)&sock->address, &sockLen);
        if (sock->socketFd == -1) {
            if (errno != EAGAIN) {
                LTLOG("skt.accept.fail", "accept failed: %d", errno);
            }
            break;
        }
        if (sockLen != sizeof(sock->address) ||
            sock->address.sin_family != AF_INET) {
            LTLOG("skt.accept.addr", "invalid remote address");
            break;
        }
        sock->ipAddr = sock->address.sin_addr;
        sock->ipPort = ntohs(sock->address.sin_port);

        // Make it non-blocking:
        if (!SetNonBlock(sock->socketFd)) {
            LTLOG("skt.accept.nonblock", "cannot switch socket to non blocking");
            break;
        }

        NagleOff(sock->socketFd);

        // Add to epoll event monitoring:
        if (epoll_ctl(tran->EpollFd, EPOLL_CTL_ADD, sock->socketFd,
                      &sock->eventSpec) == -1) {
            LTLOG("skt.accept.epoll", "cannot add socket to epoll descriptor");
            break;
        }

        NET_DEBUG("socket[%04lx] accept fd: %d ip: %s port: %d\n", LT_PLT_HANDLE(sockData->h_socket), sock->socketFd, inet_ntoa(sock->ipAddr), ntohs(sock->ipPort));
        S.iCore->ReleaseHandlePrivateData(handle, sockData);
        return sock;
    } while (0);

    CloseFd(&sock->socketFd);
    S.iCore->ReleaseHandlePrivateData(handle, sockData);
    S.iCore->DestroyHandle(handle);
    return NULL;
}

LOC void DnsLookupProc(void *pClientData) {
    LTSocket       hSocket = VOIDPTR_TO_LTHANDLE(pClientData);
    LTSocket_Data *sockData = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!sockData) return;

    Priv_Sock       *sock = GET_SOCKET_PRIV(sockData);
    struct addrinfo *result = NULL;
    LTSocket_Event   sockEvent = kLTSocket_Event_DnsError;
    do {
        static const struct addrinfo hints = (struct addrinfo) {
            .ai_family = AF_INET,
        };
        errno = 0;
        int gai_error = getaddrinfo(sock->hostname, NULL, &hints, &result);
        if (gai_error) {
            Priv_Tran *tran = sock->privTran;
            LTLOG_YELLOWALERT("host.fail", "host lookup failed: %s %d %d %d",
                               sock->hostname, gai_error, errno, tran->isUp);
            break;
        }

        const struct sockaddr_in *sin = NULL;
        for (const struct addrinfo *ai = result; ai != NULL; ai = ai->ai_next) {
            if (ai->ai_family == AF_INET &&
                ai->ai_addrlen == sizeof(struct sockaddr_in) &&
                ai->ai_addr != NULL) {
                sin = (const struct sockaddr_in *)ai->ai_addr;
                break;
            }
        }
        if (sin == NULL) {
            LTLOG_YELLOWALERT("host.invalid",
                              "getaddrinfo() returned invalid data");
            break;
        }

        sock->ipAddr = sin->sin_addr;
        sock->address.sin_addr = sock->ipAddr;

        sockEvent = kLTSocket_Event_DnsResolved;
    } while (false);
    freeaddrinfo(result);

    NotifySocketEvent(sock, sockEvent);

    if (sockEvent == kLTSocket_Event_DnsResolved &&
        sock->socketFd != -1 &&
        epoll_ctl(sock->privTran->EpollFd, EPOLL_CTL_ADD,
                  sock->socketFd, &sock->eventSpec) == -1) {
        NotifySocketEvent(sock, kLTSocket_Event_SocketError);
    }

    S.iCore->ReleaseHandlePrivateData(hSocket, sockData);
}

LOC bool FindHostAddress(LTSocket_Data *sockData) {
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!sock->hostname) return false;

    Priv_Tran *tran = sock->privTran;
    S.iThread->QueueTaskProc(tran->DnsThread, DnsLookupProc, NULL,
                             LTHANDLE_TO_VOIDPTR(sockData->h_socket));

   return true;
}

LOC void EpollEventSocket(void *data, uint32_t events) {
    PF;

    Priv_Sock *sock = data;
    LT_ASSERT(sock);

    int fd = sock->socketFd;
    if (fd < 0) return; // already closed, ignore events

    Priv_Tran *tran = sock->privTran;
    if (tran->debug) PrintEpollFlags(tran, sock, events);

    if (!sock->ready && (events & EPOLLOUT)) {
        ShowStatus(tran, "ready");
        sock->ready = true;
        NotifySocketEvent(sock, kLTSocket_Event_SocketReady);
        if (!(sock->flags & kSocketFlags_NoConnect) && sock->ipProto == kIPProtocol_TcpClient) {
            // Putting connect() here avoids an epoll event race condition
            if (connect(sock->socketFd, (struct sockaddr*)&sock->address, sizeof(sock->address)) < 0) {
                if (errno != EINPROGRESS) NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                return;
            }
        }
    }

    if (events & EPOLLERR) {
        int error = 0;
        socklen_t errlen = sizeof(error);
        int res;
        res = getsockopt(sock->socketFd, SOL_SOCKET, SO_ERROR, &error, &errlen);
        if (res == 0) {
            switch (error) {
            case ENETDOWN:      /* Network is down */
            case ENETUNREACH:   /* Network is unreachable */
            case ENETRESET:     /* Network dropped connection because of reset */
            case ECONNABORTED:  /* Software caused connection abort */
            case ECONNRESET:    /* Connection reset by peer */
            case ETIMEDOUT:     /* Connection timed out */
            case ECONNREFUSED:  /* Connection refused */
            case EHOSTDOWN:	    /* Host is down */
            case EHOSTUNREACH:  /* No route to host */
                if (!sock->connected) {
                    NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
                }
                NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
                break;
            default:
                LTLOG_YELLOWALERT("evt.err", "Transport driver event error(%d): %s", error, strerror(error));
                NotifySocketEvent(sock, kLTSocket_Event_Error);
                break;
            }
        } else {
            LTLOG_YELLOWALERT("evt.sock.err", "Transport driver getsockopt error(%d): %s", res, strerror(res));
            NotifySocketEvent(sock, kLTSocket_Event_Error);
        }
        return;
    }

    switch (sock->ipProto) {

        case kIPProtocol_TcpClient:
            // First, check connection
            if (events & (EPOLLIN | EPOLLOUT)) {
                if (!sock->connected) {
                    ShowStatus(tran, "connected");
                    // Error on unconnected socket. Ignore the other flags.
                    if (events & EPOLLERR) return;
                    // Mark socket as connected. Must be done here because a new
                    // event may arrive before LT knows about the connection. In
                    // that case it's just a ReadReady.
                    sock->connected = true;
                    NotifySocketEvent(sock, kLTSocket_Event_Connected);
                }
            }
            // Check for input, even if remote has closed (below)
            if (events & EPOLLIN) {
                ShowStatus(tran, "read-ready");
                NotifySocketEvent(sock, kLTSocket_Event_ReadReady);
            }
            // Remote connection may have closed, even though we got fresh input above.
            if (events & (EPOLLRDHUP | EPOLLHUP)) {
                sock->connected = false;
                ShowStatus(tran, "disconnect");
                NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
            }
            // Output only if not closed
            if (sock->connected && (events & EPOLLOUT)) {
                ShowStatus(tran, "write-ready");
                NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            }
            break;

        case kIPProtocol_TcpServer:
            if (events & EPOLLHUP) {
                NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
                break;
            }
            if (events & EPOLLIN) {  // Accept
                Priv_Sock *new_sock = NetIp_CloneSocketAccept(sock);
                if (new_sock) NotifySocketEvent(new_sock, kLTSocket_Event_Connected);
            }
            break;

        case kIPProtocol_Udp:
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            if (events & EPOLLIN)  NotifySocketEvent(sock, kLTSocket_Event_ReadReady);
            if (events & EPOLLOUT) NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            break;

        default:
            break;
    }
}

LOC void EpollEventNetlink(void *data, uint32_t events) {
    PF;

    Priv_Tran *tran = data;
    LT_ASSERT(tran);

    if (events & EPOLLIN) Netlink_ReceiveMsg(tran);
}

LOC void EpollEventDhcpcPipe(void *data, uint32_t events) {
    PF;

    Priv_Tran *tran = data;
    LT_ASSERT(tran);

    if (!(events & EPOLLIN)) return;

    char buffer[1024];
    ssize_t bytes = read(tran->DhcpcPipeFd, buffer, sizeof(buffer));
    if (bytes <= 0) return;

    ssize_t first = -1;
    for (ssize_t i = 0; i < bytes; i++) {
        if (first == -1) {
            if (!lt_isspace(buffer[i])) first = i;
            continue;
        }
        if (buffer[i] == '\n') {
            LTLOG("udhcpc", "%.*s", (int)(i - first), &buffer[first]);
            first = -1;
        }
    }
    if (first >= 0) {
        LTLOG("udhcpc", "%.*s", (int)(bytes - first), &buffer[first]);
    }
}

LOC void NetIp_HandleEvents(Priv_Tran *tran) {
    // Converts Linux Epoll events to LT events.
    // Runs in a separate Epoll thread context.
    PF;

    static u8 const maxEvents = 10;  // add to transport tran struct?
    struct epoll_event eventList[maxEvents];

    // Blocks until event or timeout. The timeout can be used as a base
    // for connection and other timeouts.
    int eventCount = epoll_wait(tran->EpollFd, eventList, maxEvents, EPOLL_WAIT_TIME_MSEC);
    if (eventCount < 0) LTLOG("epoll.fail", "epoll failed %d: %s (fd %d)", errno, strerror(errno), tran->EpollFd);

    // Process each events:
    for (int n = 0; n < eventCount; n++) {
        EpollHandler *epollHandler = eventList[n].data.ptr;
        LT_ASSERT(epollHandler);
        epollHandler->func(epollHandler->data, eventList[n].events);
    }
}

LOC bool CheckResolvConf(void) {
    struct stat stat_buf;

    if (stat(_PATH_RESCONF, &stat_buf) == -1) return false;
    if (!S_ISREG(stat_buf.st_mode)) return false;
    return (stat_buf.st_size > 0);
}

LOC void UpdateTransportState(Priv_Tran *tran) {
    bool newTransportUp = false;
    do {
        if (!InterfaceIsUp(tran->IfFlags)) break;
        if (tran->ipAddr.s_addr == 0) break;
        if (tran->ipMask.s_addr == 0) break;
        if (tran->ipGate.s_addr == 0) break;

        if (!CheckResolvConf()) break;

        newTransportUp = true;
    } while (0);

    if (tran->isUp != newTransportUp) {
        /*
         * If the network just came up reinitialize the DNS resolver to
         * force to start using the primary nameserver again.
         */
        if (newTransportUp) {
#if defined(__UCLIBC__)
            /*
             * res_init() is buggy in uClibc (and probably fixed in uClibc-ng).
             * Force a reset of the DNS resolver by pretending that
             * "/etc/resolv.conf" has been changed.
             */
            touch(_PATH_RESCONF);
#else
            res_init();
#endif
        }

        tran->isUp = newTransportUp;
        NotifyTransport(tran, tran->isUp ? kLTTransport_Event_Up :
                                           kLTTransport_Event_Down);
    }
}

LOC bool NetIp_OnThreadStart(void) {
    PF;

    LTLOG("thread.startup", "Starting transport thread");

    Priv_Tran *tran = (Priv_Tran*)S.iThread->GetThreadSpecificClientData(S.iThread->GetCurrentThread(), THREAD_CLIENTDATA_KEY);
    if (!tran) {
	    LTLOG_YELLOWALERT("thread.tran", "Failed to get thread-specific client data");
	    return false;
    }

    tran->EpollFd = epoll_create1(O_CLOEXEC);
    // No point in trying to handle this gracefully. If creating a single
    // file-descriptor fails the system is already in a very bad state.
    // And logging wouldn't reach the log servers anyhow because this
    // is the network transport trying to start up.
    LT_ASSERT(tran->EpollFd != -1);

    CreateNetlinkSocket(tran);
    LT_ASSERT(tran->NetlinkFd != -1);

    if (S.DhcpInterface) {
        lt_strncpyTerm(tran->IfName, S.DhcpInterface, sizeof(tran->IfName));
        tran->UseDhcp = true;
     }

    if (!tran->UseDhcp) {
        // If LT is using the BusyBox DHCP client it will receive the
        // necessary netlink messages to learn the IP address and default
        // gateway when that DHCP client configures the network interface.
        //
        // If LT is not performing DHCP (e.g. on a developer desktop
        // or a CI/CD cloud instance) it would usually not receive any
        // netlink messages because the network is already configured.
        //
        // In the code below will manually request netlink messages
        // from the Linux kernel. This will e.g. allow the transport
        // to figure out the correct network interface.
        if (Netlink_SendRtm(tran, RTM_GETLINK)) Netlink_ReceiveMsg(tran);
        if (Netlink_SendRtm(tran, RTM_GETADDR)) Netlink_ReceiveMsg(tran);
        if (Netlink_SendRtm(tran, RTM_GETROUTE)) Netlink_ReceiveMsg(tran);
    }

    // The transport only terminates when being closed explicitly by NetIp_CloseTransport.
    // So that the transport can resume when the network goes down and up.
    while (tran->isRunning) {
        // To-Do: Move to event driven LT thread.
        if (tran->UseDhcp) {
            if (S.pDeviceWiFi->IsConnected()) {
                StartDhcpClient(tran);
            } else {
                StopDhcpClient(tran);
            }
        }

        NetIp_HandleEvents(tran);
        UpdateTransportState(tran);
    }

    LTLOG("thread.shutdown", "Shutting down transport thread");

    tran->isUp = false;
    tran->isRunning = false;
    ShutdownDhcpClient(tran);
    CloseFd(&tran->NetlinkFd);
    CloseFd(&tran->EpollFd);

    S.iEvent->NotifyEvent(tran->driverData->hEvent, 0, kLTTransport_Event_Down);

    return false;
}

/*******************************************************************************
 * Dhcp Client Management Functions
 ******************************************************************************/

typedef enum {
    DhcpClientNotRunning,
    DhcpClientTerminated,
    DhcpClientRunning
} DhcpClientState;

UTL DhcpClientState CheckDhcpClient(Priv_Tran *tran) {
    LT_ASSERT(tran->DhcpcPid != 0);
    if (tran->DhcpcPid < 0) return DhcpClientNotRunning;

    int wstatus;
    if (waitpid(tran->DhcpcPid, &wstatus, WNOHANG) == tran->DhcpcPid) {
        if (WIFSIGNALED(wstatus)) {
            LTLOG("dhcpc.signal", "DHCP client terminated by signal %d",
                  WTERMSIG(wstatus));
        } else {
            LTLOG("dhcpc.exit", "DHCP client exited with status %d",
                  WEXITSTATUS(wstatus));
        }
        tran->DhcpcPid = -1;
        CloseFd(&tran->DhcpcPipeFd);
        return DhcpClientTerminated;
    }

    if (kill(tran->DhcpcPid, 0) != 0) {
        LTLOG("dhcpc.missing", "DHCP client process missing");
        tran->DhcpcPid = -1;
        CloseFd(&tran->DhcpcPipeFd);
        return DhcpClientTerminated;
    }

    return DhcpClientRunning;
}

UTL bool IsDhcpClientRunning(Priv_Tran *tran) {
    return CheckDhcpClient(tran) == DhcpClientRunning;
}

UTL void StopDhcpClient(Priv_Tran *tran) {
    if (!IsDhcpClientRunning(tran)) return;

    LTLOG("dhcpc.stop", "Stopping DHCP client");
    kill(tran->DhcpcPid, SIGTERM);
}

UTL void StartDhcpClient(Priv_Tran *tran) {
    LT_ASSERT(S.DhcpInterface);

    switch (CheckDhcpClient(tran)) {
    case DhcpClientNotRunning:
        LTLOG("dhcpc.start", "Starting DHCP client");
        break;

    case DhcpClientTerminated:
        LTLOG("dhcpc.restart", "Restarting DHCP client");
        NotifyTransport(tran, kLTTransport_Event_DhcpRetry);
        break;

    case DhcpClientRunning:
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    tran->DhcpcEpollHandler = (EpollHandler) {
        .func = EpollEventDhcpcPipe,
        .data = tran
    };
    struct epoll_event eventSpec = {
        .events   = EPOLLIN,
        .data.ptr = &tran->DhcpcEpollHandler
    };
    if (epoll_ctl(tran->EpollFd, EPOLL_CTL_ADD, pipefd[0], &eventSpec) == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
    }
    CloseFd(&tran->DhcpcPipeFd);
    tran->DhcpcPipeFd = pipefd[0];

    tran->DhcpcPid = fork();
    if (tran->DhcpcPid != 0) {
        close(pipefd[1]);
        if (tran->DhcpcPid == -1) {
            CloseFd(&tran->DhcpcPipeFd);
        }
        return;
    }

    // This is the child process.
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    closefrom(STDERR_FILENO + 1);

    int devnull = open(_PATH_DEVNULL, O_RDONLY);
    if (devnull != -1) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }

    static const char sDhcpcCmd[] = "udhcpc";
    static const char sDhcpScript[] = "/libexec/udhcpc/event-script";

    execlp(sDhcpcCmd, sDhcpcCmd, "-f",
                                 "-i", S.DhcpInterface,
                                 "-s", sDhcpScript,
                                 "-T", "5",
                                 "-t", "1",
                                 "-n",
                                 "-R",
                                 NULL);
    _exit(EX_OSERR);
}

UTL void ShutdownDhcpClient(Priv_Tran *tran) {
    if (!IsDhcpClientRunning(tran)) return;

    kill(tran->DhcpcPid, SIGTERM);
    S.iThread->Sleep(LTTime_Milliseconds(1));
    if (!IsDhcpClientRunning(tran)) return;

    kill(tran->DhcpcPid, SIGKILL);
    S.iThread->Sleep(LTTime_Milliseconds(1));
    waitpid(tran->DhcpcPid, NULL, WNOHANG);
    tran->DhcpcPid = -1;

}

/*******************************************************************************
** API Transport Functions
*******************************************************************************/

PUB u32 NetIp_OpenTransport(LTTransportData *tranData, LTSocket_Create *createSocket) {
    PF;
    LTTransportDriver *driverData = tranData->driverData;
    if (driverData->privData) return sizeof(Priv_Sock); // driver is already setup
    Priv_Tran *tran = lt_malloc(sizeof(Priv_Tran));
    if (!tran) return 0;
    driverData->privData = tran;
    *tran = (Priv_Tran) {
        .createSocket  = createSocket,
        .tranData      = tranData,
        .driverData    = driverData,
        .EpollFd       = -1,
        .NetlinkFd     = -1,
        .DhcpcPipeFd   = -1,
        .DhcpcPid      = -1
    };
    // Parse specification string (doesn't do much on Linux at this time if ever)
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use symbolic abbreviations for words. Example: "gw" rather than "gateway"
    char *spec = lt_strdup(driverData->tranSpec);
    char **tokens = TokenizeSpec(spec);
    if (tokens && FindToken(tokens, "debug") >= 0) tran->debug = true;
    lt_free(tokens); // NULL okay
    lt_free(spec);

    NET_DEBUG("transport[%p]: open spec: \"%s\"\n", tran, driverData->tranSpec);

    // Create thread used to provide asynchronous DNS resolution:
    tran->DnsThread = S.iCore->CreateThread("NetIpDns");
    if (!tran->DnsThread) return 0;

    // Create thread used for event handling:
    tran->EventThread = S.iCore->CreateThread("NetIpEvent");
    if (!tran->EventThread) {
        lt_destroyhandle(tran->DnsThread);
        tran->DnsThread = LTHANDLE_INVALID;
        return 0;
    }
    S.iThread->SetStackSize(tran->EventThread, 65536);
    S.iThread->SetThreadSpecificClientData(tran->EventThread, THREAD_CLIENTDATA_KEY, NULL, tran);

    // Setup event handler and start the thread:
    tran->isRunning = true;
    S.iThread->Start(tran->DnsThread, NULL, NULL);
    S.iThread->Start(tran->EventThread, NetIp_OnThreadStart, NULL);
    // This function will exit before the event occurs. There is no race since the
    // caller is running in the same context.
    return sizeof(Priv_Sock);
}

PUB void NetIp_CloseTransport(LTTransportDriver *driverData) { // Called from DestroyTransport
    PF;
    // All sockets are closed in LTNetCore before this is called
    // LTNet also destroys the transport handle where all storage is kept
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;
    NET_DEBUG("transport[%p]: close spec: \"%s\"\n", driverData, driverData->tranSpec);

    tran->isRunning = false;
    S.iThread->WaitUntilFinished(tran->EventThread, LTTime_Milliseconds(EPOLL_WAIT_TIME_MSEC+100));
    lt_destroyhandle(tran->EventThread);
    tran->EventThread = LTHANDLE_INVALID;

    S.iThread->Terminate(tran->DnsThread);
    lt_destroyhandle(tran->DnsThread);
    tran->DnsThread = LTHANDLE_INVALID;

    lt_free(tran);
    driverData->privData = NULL;
}

PUB bool NetIp_GetTransportSpec(LTTransportDriver *driverData, char *spec, u16 specSize) {
    PF;
    // Could be extended to take a spec like "ip:" and return just ip address. !!
    // LTNetCore asserts spec and specSize are valid
    Priv_Tran *tran = driverData->privData;
    spec[0] = 0;
    if (!tran) return false;
    char *s = spec;
    char *e = s + specSize - 1;
    if (tran->UseDhcp) {
        s = AppendStr("dhcp ", s, e);
    }
    if (tran->ipAddr.s_addr) {
        s = AppendStr("ip: ", s, e);
        s = IpToStr(&tran->ipAddr, s, e);
    }
    if (tran->ipGate.s_addr) {
        s = AppendStr(" gw: ", s, e);
        s = IpToStr(&tran->ipGate, s, e);
    }
    if (tran->ipMask.s_addr) {
        s = AppendStr(" mask: ", s, e);
        s = IpToStr(&tran->ipMask, s, e);
    }
    return true;
}

PUB void NetIp_GetMetrics(LTTransportDriver *driverData, LTTransport_Metrics *metrics, LT_SIZE sizeOfMetrics) {
    // driverData is never NULL
    Priv_Tran *tran = driverData->privData;
    if (!tran) return;
    lt_memset(metrics, 0, sizeOfMetrics);
    lt_memcpy(metrics, &tran->metrics, sizeof(metrics) > sizeOfMetrics ? sizeOfMetrics : sizeof(*metrics));
}

PUB s32 NetIp_IsOperating(LTTransportDriver *driverData, LTTransport_Nudge nudge) {
    PF;
    LT_UNUSED(nudge);
    Priv_Tran *tran = driverData->privData;
    if (!tran || !tran->isUp) return -1;
    tran->metrics.lowerBytesRx++; // fake it on linux
    return tran->metrics.lowerBytesRx;
}

/*******************************************************************************
** API Socket Functions
*******************************************************************************/

FWD void NetIp_ConnectSocket(LTSocket_Data *socket);
FWD void NetIp_CloseSocket(LTSocket hSocket);

PUB bool NetIp_OpenSocket(LTSocket_Data *sockData) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    Priv_Tran *tran = sockData->transData->driverData->privData;

    *sock = (Priv_Sock) {              // unspecified fields are zeroed
        .privTran          = tran,     // event fd is in transport
        .socketData        = sockData, // back reference
        .epollHandler.func = EpollEventSocket,
        .epollHandler.data = sock
    };

    // Parse specification string
    // WARNING: If you are adding names or labels to this spec, please keep them short and sweet.
    // It's okay to use short symbolic abbreviations.
    char *spec = lt_strdup(sockData->spec);
    char **tokens = TokenizeSpec(spec); // does malloc()
    if (!tokens) {
        lt_free(spec); // NULL ok
        return false;
    }
    bool listener = false;
    sock->ipProto = kIPProtocol_TcpClient; // The default protocol - does not need to be specified
    do {
        if (FindToken(tokens, "udp"   ) >= 0) { sock->ipProto = kIPProtocol_Udp;  break; }
        if (FindToken(tokens, "icmp"  ) >= 0) { sock->ipProto = kIPProtocol_Icmp; break; }
        if (FindToken(tokens, "raw"   ) >= 0) { sock->ipProto = kIPProtocol_Raw;  break; }
        if (FindToken(tokens, "dns"   ) >= 0) { sock->ipProto = kIPProtocol_Dns;  break; }
    } while (0);

    if (FindToken(tokens, "listen") >= 0) listener = true;

    struct in_addr ipAddr; // needed because ip4addr_aton sets bogus addr on parse failure

    s16 n = FindToken(tokens, "host:");  // maybe add "host: foo.com:80" format? !!
    if (n >= 0 && tokens[n+1]) {
        sock->hostname = lt_strdup(tokens[n+1]);
    }

    n = FindToken(tokens, "ip:");
    if (n >= 0 && tokens[n+1] && inet_pton(AF_INET, tokens[n+1], &ipAddr)) {
        sock->ipAddr = ipAddr;
    }

    n = FindToken(tokens, "myip:");
    if (n >= 0 && tokens[n+1] && inet_pton(AF_INET, tokens[n+1], &ipAddr)) {
        P("my_ip not supported (yet)");
    }

    n = FindToken(tokens, "port:");
    if (n >= 0 && tokens[n+1]) sock->ipPort = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "myport:");
    if (n >= 0 && tokens[n+1]) sock->myPort = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "allow:");
    if (n >= 0 && tokens[n+1] && inet_pton(AF_INET, tokens[n+1], &ipAddr)) {
        sock->ipAllow = ipAddr;
    } else sock->ipAllow.s_addr = 0;

    n = FindToken(tokens, "dscp:");
    if (n >= 0 && tokens[n+1]) sock->dscp = lt_strtou32(tokens[n+1], NULL, 10);

    n = FindToken(tokens, "keepalive");
    if (n >= 0) sock->flags |= kSocketFlags_KeepAlive;

    n = FindToken(tokens, "no-connect");
    if (n >= 0) sock->flags |= kSocketFlags_NoConnect;

    n = FindToken(tokens, "debug");
    if (n >= 0) tran->debug = true; // for entire transport (not just this socket)

    lt_free(tokens);
    lt_free(spec);

    // Next line requires knowing debug mode that is set above:
    NET_DEBUG("socket[%04lx]: open spec: \"%s\"\n", LT_PLT_HANDLE(sockData->h_socket), sockData->spec);

    if (listener && sock->ipProto == kIPProtocol_TcpClient) {
        if (sock->ipPort && !sock->myPort) sock->myPort = sock->ipPort;
        sock->ipProto = kIPProtocol_TcpServer;
    }

    sock->eventSpec = (struct epoll_event) {
        .events   = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET,
        .data.ptr = &sock->epollHandler
    };

    sock->address = (struct sockaddr_in) {
        .sin_family      = AF_INET,
        .sin_port        = htons(sock->myPort),
        .sin_addr.s_addr = sock->privTran->ipAddr.s_addr
    };

    int type = 0;
    int prot = 0;
    bool useBind = false;
    switch (sock->ipProto) {
        case kIPProtocol_Dns:
            sock->socketFd = -1;
            return FindHostAddress(sockData);
        case kIPProtocol_Raw:
            type = SOCK_RAW;
            sock->flags |= kSocketFlags_NoConnect;
            break;
        case kIPProtocol_Icmp:
            type = SOCK_RAW;
            prot = IPPROTO_ICMP;
            sock->flags |= kSocketFlags_NoConnect;
            break;
        case kIPProtocol_Udp:
            type = SOCK_DGRAM;
            useBind = true;
            sock->flags |= kSocketFlags_NoConnect;
            break;
        case kIPProtocol_TcpClient:
            type = SOCK_STREAM;
            break;
        case kIPProtocol_TcpServer:
            type = SOCK_STREAM;
            useBind = true;
            sock->flags |= kSocketFlags_NoConnect;
            break;
        default:
            break;
    }

    const char *reason = "none";
    do {
        reason = "can't create socket";
        sock->socketFd = socket(AF_INET, type | SOCK_NONBLOCK, prot);
        if (sock->socketFd < 0) break;

        reason = "set bcast failed";
        int bcast = 1;
        int ret = setsockopt(sock->socketFd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
        if (ret != 0) break;

        if (sock->ipProto == kIPProtocol_TcpClient) {
            // Switch off Nagle's algorithm to improve e.g. HTTP(S)
            // upload throughput
            NagleOff(sock->socketFd);
        }

        if (sock->dscp) {
            SetDscp(sock->socketFd, sock->dscp);
        }

        if (sock->flags & kSocketFlags_KeepAlive) {
            reason = "set keepalive failed";
            int keepalive = 1;
            int keepcnt = TCP_KEEPCNT_DEFAULT;
            int keepidle = TCP_KEEPIDLE_DEFAULT;
            int keepintvl = TCP_KEEPINTVL_DEFAULT;

            int ret = setsockopt(sock->socketFd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            if (ret != 0) break;

            ret = setsockopt(sock->socketFd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
            if (ret != 0) break;

            ret = setsockopt(sock->socketFd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
            if (ret != 0) break;

            ret = setsockopt(sock->socketFd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
            if (ret != 0) break;
        }

        if (useBind) {
            //P("BIND port %d\n", ntohs(sock->address.sin_port));
            reason = "can't bind socket";
            if (bind(sock->socketFd, (struct sockaddr *)&sock->address, sizeof(sock->address)) == -1) break;
        }

        sock->address.sin_addr = sock->ipAddr;
        sock->address.sin_port = htons(sock->ipPort);

        if (sock->ipProto == kIPProtocol_TcpServer) {
            reason = "can't set listen socket";
            if (listen(sock->socketFd, TCP_LISTEN_BACKLOG) == -1) break;
            NET_DEBUG("socket[%04lx]: listen spec: \"%s\"\n", LT_PLT_HANDLE(sockData->h_socket), sockData->spec);
        } else {
            sock->eventSpec.events |= EPOLLOUT;
        }

        if (FindHostAddress(sockData)) return true;

        reason = "can't add socket to epoll";
        if (epoll_ctl(sock->privTran->EpollFd, EPOLL_CTL_ADD, sock->socketFd, &sock->eventSpec) == -1) break;

        return true;
    } while (false);
    LTLOG("skt.open.err", "open error %d: %s (fd: %d reason: %s)", errno, strerror(errno), sock->socketFd, reason);
    // cleanup
    NotifySocketEvent(sock, kLTSocket_Event_SocketError);
    NetIp_CloseSocket(sockData->h_socket);
    sockData->h_socket = 0;
    // NetCore will call CloseSocket for any other cleanup
    return false;
}

PUB bool NetIp_GetSocketSpec(LTSocket_Data *sockData, char *spec, u16 specSize) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!spec) return false;
    char *s = spec;
    char *e = s + specSize - 1;
    if (sock->ipProto >= kIPProtocol_Max) return false;
    s = AppendStr(s_IPProtocolNames[sock->ipProto], s, e);
    if (sock->hostname) {
        s = AppendStr(" host: ", s, e);
        s = AppendStr(sock->hostname, s, e);
    }
    if (sock->ipAddr.s_addr) {
        s = AppendStr(" ip: ", s, e);
        s = IpToStr(&sock->ipAddr, s, e);
    }
    if (sock->ipPort) {
        s = AppendStr(" port: ", s, e);
        s = U32ToStr(s, 6, sock->ipPort);
    }
    if (sock->myPort) {
        s = AppendStr(" myport: ", s, e);
        s = U32ToStr(s, 6, sock->myPort);
    }
    if (sock->ipAllow.s_addr) {
        s = AppendStr(" allow: ", s, e);
        s = IpToStr(&sock->ipAddr, s, e);
    }
    return true;
}

PUB bool NetIp_GetProperty(LTSocket_Data *sockData, const char *name, void *value) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "recv.endpoint.v4") == 0) {
        LTNetIpv4Endpoint *ep = value;
        ep->address = sock->recvAddr.sin_addr.s_addr;
        ep->port    = ntohs(sock->recvAddr.sin_port);
    } else if (lt_strcmp(name, "local.endpoint.v4") == 0) {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);
        int ret = getsockname(sock->socketFd, (struct sockaddr *)&addr, &addrLen);
        if (ret != 0) {
            LTLOG_REDALERT("getname.fail", "getsockbyname failed %d: %s", errno, strerror(errno));
            return false;
        }
        LTNetIpv4Endpoint *ep = value;
        ep->address = addr.sin_addr.s_addr;
        ep->port    = ntohs(addr.sin_port);
    } else if (lt_strcmp(name, "dns.addr.v4") == 0) {
        if (sock->ipProto != kIPProtocol_Dns) {
            LTLOG_REDALERT("getprop.notdns", "Invalid attempt to get DNS address from non-DNS socket");
            return false;
        }
        *(u32 *)value = sock->ipAddr.s_addr;
    } else {
        LTLOG_YELLOWALERT("getprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB bool NetIp_SetProperty(LTSocket_Data *sockData, const char *name, const void *value) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (!name || !value) return false;
    if (lt_strcmp(name, "send.endpoint.v4") == 0) {
        const LTNetIpv4Endpoint *ep = value;
        sock->address.sin_addr.s_addr = ep->address;
        sock->address.sin_port        = htons(ep->port);
    } else if (lt_strcmp(name, "dscp") == 0) {
        sock->dscp = *(u8 *)value;
        SetDscp(sock->socketFd, sock->dscp);
    } else {
        LTLOG_YELLOWALERT("setprop.unk", "Unsupported property: %s", name);
        return false;
    }
    return true;
}

PUB void NetIp_CloseSocket(LTSocket hSocket) { // Also called by Transport.DestroySocket()
    PF;
    LTSocket_Data *sockData = S.iCore->ReserveHandlePrivateData(hSocket);
    if (!sockData || !sockData->h_socket) { // already closed
        S.iCore->ReleaseHandlePrivateData(hSocket, sockData);
        return;
    }
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (sock->socketData) {
        LTLOG_DEBUG("sock.close", "socket[%04lx]: close spec: \"%s\"\n", LT_PLT_HANDLE(sockData->h_socket), sockData->spec);
        epoll_ctl(sock->privTran->EpollFd, EPOLL_CTL_DEL, sock->socketFd, NULL);
        CloseFd(&sock->socketFd);
        lt_free(sock->hostname);
        // clear private sock data before freed by netcore's destroy
        // socket handle data will be cleared and freed by netcore's destroy.
        *sock = (Priv_Sock){};
    }
    S.iCore->ReleaseHandlePrivateData(hSocket, sockData);
}

PUB void NetIp_ConnectSocket(LTSocket_Data *sockData) {
    PF;
    LTLOG_DEBUG("skt.conn", "connect %lx", LT_PLT_HANDLE(sockData->h_socket));
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (sock->socketFd < 0) return; // already closed
    switch (sock->ipProto) {
        case kIPProtocol_TcpClient:
            if (sock->ready) {
                if (!connect(sock->socketFd, (struct sockaddr*)&sock->address, sizeof(sock->address))) return;
                if (errno == EINPROGRESS) return;
                LTLOG("cnt.fail", "connect failed %d: %s\n", errno, strerror(errno));
                NotifySocketEvent(sock, kLTSocket_Event_ConnectError);
            }
            break;
        case kIPProtocol_Udp:
        case kIPProtocol_Icmp:
        case kIPProtocol_Raw:
            NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
            return;
        default: break;
    }
}

PUB void NetIp_DisconnectSocket(LTSocket_Data *sockData) {
    PF;
    LTLOG_DEBUG("skt.disc", "disconnect %lx", LT_PLT_HANDLE(sockData->h_socket));
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    Priv_Tran *tran = sock->privTran;
    sock->connected = false;
    if (sock->socketFd >= 0) {
        epoll_ctl(tran->EpollFd, EPOLL_CTL_DEL, sock->socketFd, &sock->eventSpec);
        shutdown(sock->socketFd, SHUT_WR);
        // TODO (need more tests): Because socket is shutdown (not closed), so still keep valid socketData and socketFd.
        lt_memset(&sock->eventSpec, 0, sizeof(sock->eventSpec));
    }
    NotifySocketEvent(sock, kLTSocket_Event_Disconnected);
}

PUB s32 NetIp_WriteSocket(LTSocket_Data *sockData, const void *data, u32 dataLen) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (sock->socketFd < 0 || !data ||
        (!sock->connected && !(sock->flags & kSocketFlags_NoConnect)) || !sock->ready) {
        // Socket is not ready or connected to write
        return 0;
    }

    ssize_t len;
    if (sock->ipProto <= kIPProtocol_Udp) { // udp, icmp, and raw
        len = sendto(sock->socketFd, data, dataLen, MSG_NOSIGNAL, (struct sockaddr *) &sock->address, sizeof(sock->address));
    } else {
        len = send(sock->socketFd, data, dataLen, 0);
    }
    Priv_Tran *tran = sock->privTran;
    // Success
    if (len >= 0) {
        tran->metrics.upperBytesTx += len;
        NotifySocketEvent(sock, kLTSocket_Event_WriteReady);
        return len;
    }
    // Errors
    int err = errno;
    // Write buffer is full or unavailable temporarily. Upper stack needs to wait a bit and resend.
    if (err == EAGAIN) return 0;
    // Other errors
    LTLOG("skt.wr.err", "write len %ld error %d %s (fd %d)", LT_Ps32(len), err, strerror(err), sock->socketFd);
    NotifySocketEvent(sock, kLTSocket_Event_WriteError);
    return -1;
}

PUB s32 NetIp_ReadSocket(LTSocket_Data *sockData, void *data, u32 dataSize) {
    PF;
    Priv_Sock *sock = GET_SOCKET_PRIV(sockData);
    if (sock->socketFd < 0) return 0;
    ssize_t len = 0;
    int flags = 0;
    if (!data && !dataSize) {
        flags = MSG_PEEK | MSG_TRUNC;
        if (sock->ipProto > kIPProtocol_Udp) {
            // TCP reports at most 'dataSize' waiting bytes regardless of MSG_TRUNC
            dataSize = LT_U32_MAX;
        }
    }
    if (sock->ipProto <= kIPProtocol_Udp) {
        socklen_t addrLen = sizeof(sock->recvAddr);
        len = recvfrom(sock->socketFd, data, dataSize, flags, (struct sockaddr*)&sock->recvAddr, &addrLen);
    } else {
        len = recv(sock->socketFd, data, dataSize, flags);
    }
    Priv_Tran *tran = sock->privTran;
    // Success
    if (len >= 0) {
        tran->metrics.upperBytesRx += len;
        return len;
    }
    // Errors
    int err = errno;
    // No more to read in buffer now. Upper stack needs to wait for the next read ready event from socket.
    if (err == EAGAIN) return 0;
    // Other errors
    LTLOG("skt.rd.err", "read len %ld error %d %s (fd %d)", LT_Ps32(len), err, strerror(err), sock->socketFd);
    if (err == EBADF) { // ToDo: what other errs should signal disconnect? !!!
        sock->connected = false;
        sock->ready = false;
        CloseFd(&sock->socketFd);
        sock->socketData = NULL;
        lt_memset(&sock->eventSpec, 0, sizeof(sock->eventSpec));
    }
    NotifySocketEvent(sock, kLTSocket_Event_ReadError);
    return -1;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

PUB void NetIpImpl_LibFini(void) {
    lt_closelibrary(S.pDeviceConfig);
    lt_closelibrary(S.pDeviceWiFi);
    S = (struct Statics) {}; // optional, since Init always does it
}

PUB bool NetIpImpl_LibInit(void) {
    S = (struct Statics) {
        .iCore   = LT_GetCore(),
        .iEvent  = lt_getlibraryinterface(ILTEvent,  LT_GetCore()),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore())
    };

    do {
        S.pDeviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!S.pDeviceConfig) break;

        u32 driverSection = S.pDeviceConfig->GetDriverSection("LTDeviceWiFi",
                                                              "LinuxDriverWiFi");
        if (driverSection) {
            S.DhcpInterface = S.pDeviceConfig->ReadString(driverSection,
                                                          "dhcp_interface");
        }

        S.pDeviceWiFi = lt_openlibrary(LTDeviceWiFi);
        if (!S.pDeviceWiFi) break;

        return true;
    } while (0);

    LTLOG_YELLOWALERT("error", "fail to init Linux NetIp driver");
    NetIpImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Function Vectors - Exported as an LTNetStack interface
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(NetIp, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(NetIp) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(LTNetDriver) {
    .OpenTransport     = NetIp_OpenTransport,
    .CloseTransport    = NetIp_CloseTransport,
    .GetTransportSpec  = NetIp_GetTransportSpec,
    .GetMetrics        = NetIp_GetMetrics,
    .IsOperating       = NetIp_IsOperating,
    .OpenSocket        = NetIp_OpenSocket,
    .GetSocketSpec     = NetIp_GetSocketSpec,
    .GetSocketProperty = NetIp_GetProperty,
    .SetSocketProperty = NetIp_SetProperty,
    .CloseSocket       = NetIp_CloseSocket,
    .ConnectSocket     = NetIp_ConnectSocket,
    .DisconnectSocket  = NetIp_DisconnectSocket,
    .WriteSocket       = NetIp_WriteSocket,
    .ReadSocket        = NetIp_ReadSocket
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(NetIp, (LTNetDriver));
