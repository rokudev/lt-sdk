/*
 * Copyright (c) 2021 Roku, Inc. All rights reserved.
 * This software and any compilation or derivative thereof is, and shall
 * remain, the proprietary information of Roku, Inc. and is highly confidential
 * in nature.
 */

#ifndef lwipopts_netipwifi
#define lwipopts_netipwifi

// This file overrides default options in <lwip/opt.h>

// Use LT's memcpy functions
#include <lt/core/LTStdlib.h>

//#define LWIP_DEBUG 1

#define  MEMCPY(dst,src,len) lt_memcpy(dst,src,len)
#define SMEMCPY(dst,src,len) lt_memcpy(dst,src,len)

// Use LT's memory management functions
#if 0
#define MEMP_MEM_MALLOC 1
#define MEM_LIBC_MALLOC 1
#define mem_clib_free   lt_free
#define mem_clib_malloc lt_malloc
#else
#define MEM_USE_POOLS 1
#define MEMP_USE_CUSTOM_POOLS 1
#endif

// mem_calloc is not currently used in any lwIP code
//#define mem_clib_calloc

// Pre-emptive dead-code removal
#define lwip_strnstr
#define lwip_stricmp
//#define lwip_strnicmp DNS needs it
#define lwip_itoa

// Linux provides errno
#define LWIP_PROVIDE_ERRNO 1

// We don't need lwIP's POSIX socket API
#define LWIP_SOCKET 0

// We don't need lwIP's sequential/blocking netconn API
#define LWIP_NETCONN 0

// We use EVENT (rather than CALLBACK) API to reduce number of
// required callback functions
#define LWIP_EVENT_API 1

// Enable IPv4 & IPv6
#define LWIP_IPV4 1
///#define LWIP_IPV6 1
#define LWIP_IPV6 0

// Enable TCP, ARP, ETHERNET
#define LWIP_TCP        1
#define LWIP_ARP        1
#define LWIP_ETHERNET   1

// LT needs to embed a socket pointer in the netif struct
#define LWIP_NUM_NETIF_CLIENT_DATA 1

// Arbitrary for now:
#define TCP_LISTEN_BACKLOG 2

// We make use of LwIP's DHCP for now. This should eventually be
// replaced with an LT implementation (using RAW sockets)
#define LWIP_DHCP 1
// On 7/27/22 had to add this define because DHCP stopped working, apparently due to ARP check
// not timing out which would stall the bound state. !!!!! Why?
#define DHCP_DOES_ARP_CHECK 1

#define LWIP_RAND() lt_lwip_rand()

#define LWIP_DNS 1
#define DNS_TABLE_SIZE      16
#define DNS_MAX_RETRIES     2
#define LT_DNS_MAX_SERVERS (DNS_MAX_SERVERS + 2)    /* 2 from Router, 2 for google dns server*/
#define LT_DNS_TMR_INTERVAL 500                     /* 500ms */
//#define LWIP_DHCP_PROVIDE_DNS_SERVERS 1

// TCP Keepalive configuration
#define  TCP_KEEPIDLE_DEFAULT     90000UL
#define  TCP_KEEPINTVL_DEFAULT    45000UL
#define  TCP_KEEPCNT_DEFAULT      9U

// Most common IP-packet MTU is 1500 octets (Ethernet)
// Allow 20 octets for TCP headers (no options)
// Allow 20/40 octets for IPv4/v6 headers respectively
#if LWIP_IPV6
#define TCP_MSS 1440
#else
#define TCP_MSS 1460
#endif

// high-throughput configuration
#ifdef LT_TCP_SND_BUF
#define TCP_SND_BUF       ((LT_TCP_SND_BUF) * TCP_MSS)
#else
#define TCP_SND_BUF      (10 * TCP_MSS)                                  // Default is 2x
#endif

#ifdef LT_TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN (((LT_TCP_SND_QUEUELEN) * (TCP_SND_BUF) + (TCP_MSS - 1))/(TCP_MSS)) // Default is 4x
#else
#define TCP_SND_QUEUELEN ((6 * (TCP_SND_BUF) + (TCP_MSS - 1))/(TCP_MSS)) // Default is 4x
#endif

#ifdef LT_TCP_WND
#define TCP_WND           ((LT_TCP_WND) * TCP_MSS)                                  // Default is 4x
#else
#define TCP_WND           (8 * TCP_MSS)                                  // Default is 4x
#endif

#define MEMP_NUM_TCP_SEG  TCP_SND_QUEUELEN

// MEMP_NUM_TCP_PCB (140 Bytes each) is used for simultaneous open sockets.
#ifndef LT_MEMP_NUM_TCP_PCB
#define LT_MEMP_NUM_TCP_PCB 16
#endif

#define MEMP_NUM_TCP_PCB LT_MEMP_NUM_TCP_PCB

#ifndef LT_MEMP_NUM_UDP_PCB
#define LT_MEMP_NUM_UDP_PCB 4
#endif
#define MEMP_NUM_UDP_PCB LT_MEMP_NUM_UDP_PCB

// PBUF_POOL_SIZE
#ifndef LT_PBUF_POOL_SIZE
#define LT_PBUF_POOL_SIZE 16
#endif
#define PBUF_POOL_SIZE      LT_PBUF_POOL_SIZE

// Enable raw IP
#define LWIP_RAW 1

// By setting this, we guarantee that we won't use lwIP from more than
// one context.
#define SYS_LIGHTWEIGHT_PROT 0

// Use lwIP without OS-awareness, no need to provided semaphores /
// mutexes / threads / mboxes. We must ensure that all calls to lwIP
// are in the same context.
#define NO_SYS 1

/* don't use ctype.h */
#define LWIP_NO_CTYPE_H 1

// Provide an LT implementation for timers
#define LWIP_TIMERS_CUSTOM 1

#define LWIP_ASSERT_CORE_LOCKED() lt_lwip_check_locking(__FUNCTION__)

#if LT_ARCHITECTURE_BITS == 32
#define MEM_ALIGNMENT 4
#elif LT_ARCHITECTURE_BITS == 64
#define MEM_ALIGNMENT 8
#else
#error unknown LT_ARCHITECTURE_BITS
#endif

void lt_lwip_sys_init(void);
void lt_lwip_sys_destroy(void);
void lt_lwip_lock(void);
void lt_lwip_unlock(void);
void lt_lwip_check_locking(const char *func);
u32  lt_lwip_rand(void);

void sys_timeouts_init(void);
void sys_timeouts_destroy(void);

/* Some useful debuging defines */
#if 0
#define LWIP_DEBUG     1
#define TCP_DEBUG                       LWIP_DBG_ON
#define PBUF_DEBUG                      LWIP_DBG_ON
#endif

#ifndef LT_LWIP_MEMP_STAT
#define LT_LWIP_MEMP_STAT 0
#endif

/* Server log for ping and TCP sync, fin and reset */
#ifndef LT_LWIP_ICMP_TCP_LOG
#define LT_LWIP_ICMP_TCP_LOG 0
#endif

#if LT_LWIP_MEMP_STAT
#define MEMP_STATS 1
#define MEM_STATS 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#endif

#endif // lwipopts
