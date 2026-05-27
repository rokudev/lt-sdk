/**
 * @file
 * Statistics module
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"

#if LWIP_STATS /* don't build if not configured for use in lwipopts.h */

#include "lwip/def.h"
#include "lwip/stats.h"
#include "lwip/mem.h"
#include "lwip/debug.h"

#include <string.h>

struct stats_ lwip_stats;

void
stats_init(void)
{
#ifdef LWIP_DEBUG
#if MEM_STATS
  lwip_stats.mem.name = "MEM";
#endif /* MEM_STATS */
#endif /* LWIP_DEBUG */
}

#if LWIP_STATS_DISPLAY
void
stats_display_proto(struct stats_proto *proto, const char *name, bool logToServer)
{
  char tag[20];
  lt_snprintf(tag, sizeof(tag), "proto.%s", name);
  LWIP_PLATFORM_DIAG(logToServer, tag, "tx: %u, rx: %u, drop: %u, protoerr: %u, err: %u",
                                                                        proto->xmit,
                                                                        proto->recv,
                                                                        proto->drop,
                                                                        proto->proterr,
                                                                        proto->err);


}

#if IGMP_STATS || MLD6_STATS
void
stats_display_igmp(struct stats_igmp *igmp, const char *name, bool logToServer)
{
  char tag[20];
  lt_snprintf(tag, sizeof(tag), "igmp.%s", name);
  LWIP_PLATFORM_DIAG(logToServer, tag, "tx: %u, rx: %u, drop: %u, protoerr: %u, err: %u",
                                                                        igmp->xmit,
                                                                        igmp->recv,
                                                                        igmp->drop,
                                                                        igmp->proterr,
                                                                        igmp->err);


}
#endif /* IGMP_STATS || MLD6_STATS */

#if MEM_STATS || MEMP_STATS
void
stats_display_mem(struct stats_mem *mem, const char *name, bool logToServer)
{
  char tag[20];
  lt_snprintf(tag, sizeof(tag), "mem.%s", name);
  LWIP_PLATFORM_DIAG(logToServer, tag, "avail:%u, used: %u, max: %u, err: %u",
                                                                        mem->avail,
                                                                        mem->used,
                                                                        mem->max,
                                                                        mem->err);
}

#if MEMP_STATS
void
stats_display_memp(struct stats_mem *mem, int idx, bool logToServer)
{
  if (idx < MEMP_MAX) {
    stats_display_mem(mem, mem->name, logToServer);
  }
}
#endif /* MEMP_STATS */
#endif /* MEM_STATS || MEMP_STATS */

#if SYS_STATS
void
stats_display_sys(struct stats_sys *sys, bool logToServer)
{
  LWIP_PLATFORM_DIAG(logToServer, "sys", "sem.used: %u, sem.max: %u, sem.err: %u, "
                                         "mutex.used: %u, mutex.max: %u, mutex.err: %u, "
                                         "mbox.used: %u, mbox.max: %u, mbox.err: %u",
                                          sys->sem.used, sys->sem.max, sys->sem.err,
                                          sys->mutex.used, sys->mutex.max, sys->mutex.err,
                                          sys->mbox.used, sys->mbox.max, sys->mbox.err);
}

#endif /* SYS_STATS */

void
stats_display(bool logToServer)
{
  s16_t i;
#ifndef ROKU_LTOS
  LINK_STATS_DISPLAY(false);
  ETHARP_STATS_DISPLAY(false);
  IPFRAG_STATS_DISPLAY(false);
  IP6_FRAG_STATS_DISPLAY(false);
  IP_STATS_DISPLAY(false);
  ND6_STATS_DISPLAY(false);
  IP6_STATS_DISPLAY(false);
  IGMP_STATS_DISPLAY(false);
  MLD6_STATS_DISPLAY(false);
  ICMP_STATS_DISPLAY(false);
  ICMP6_STATS_DISPLAY(false);
#endif
  UDP_STATS_DISPLAY(logToServer);
  TCP_STATS_DISPLAY(logToServer);
  MEM_STATS_DISPLAY(logToServer);
  for (i = 0; i < MEMP_MAX; i++) {
    MEMP_STATS_DISPLAY(i, logToServer);
  }
  SYS_STATS_DISPLAY(logToServer);
}
#endif /* LWIP_STATS_DISPLAY */

#endif /* LWIP_STATS */

