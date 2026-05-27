################################################################################
# ThirdPartyLWIP.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB	:= no
LT_PROJECT_BUILD_STATIC_LIB	:= yes
LT_PROJECT_BUILD_EXECUTABLE	:= no
#LT_PROJECT_ARTIFACT			:= LTThirdPartyLWIP

# set location of where lwip is submoduled in our repo
LT_PROJECT_SOURCE_DIR   := $(LT_OS_ROOT)/thirdparty/lwip

# set LWIPDIR for Filelists.mk
LWIPDIR := src
include $(LT_PROJECT_SOURCE_DIR)/src/Filelists.mk

# set LT_PROJECT_SOURCE_SUBDIRS for obj file subdir generation
LT_PROJECT_SOURCE_SUBDIRS := roku-lt-port                           \
                             src/core                               \
                             src/core/ipv4                          \
                             src/core/ipv6                          \
                             src/netif

# include our port files ahead of the lwip include folder
LT_PUBLIC_INCLUDE_FLAGS += -I$(LT_PROJECT_SOURCE_DIR)/roku-lt-port  \
                           -I$(LT_PROJECT_SOURCE_DIR)/src/include

# and build-o-rama
LT_PROJECT_SOURCE_FILES += roku-lt-port/sys_arch.c                  \
                           $(COREFILES)                             \
                           $(CORE4FILES)                            \
                           $(CORE6FILES)                            \
                           $(LWIPDIR)/netif/ethernet.c

LT_TCP_SND_BUF := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_SND_BUF" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_TCP_SND_QUEUELEN := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_SND_QUEUELEN" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_TCP_WND := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_WND" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_TCP_MEM_POOLS_256 := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_MEM_POOLS_256" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_TCP_MEM_POOLS_512 := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_MEM_POOLS_512" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_TCP_MEM_POOLS_1540 := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_TCP_MEM_POOLS_1540" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_MEMP_NUM_UDP_PCB := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_MEMP_NUM_UDP_PCB" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_MEMP_NUM_TCP_PCB := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_MEMP_NUM_TCP_PCB" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_LWIP_MEMP_STAT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_MEMP_STAT" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_LWIP_ICMP_TCP_LOG := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_ICMP_TCP_LOG" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_LWIP_DNS_DSCP := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_DNS_DSCP" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_PBUF_POOL_SIZE := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_PBUF_POOL_SIZE" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))

LT_CFLAGS_GENERIC += -DROKU_LTOS
ifneq ($(strip $(LT_TCP_SND_BUF)),)
LT_CFLAGS_GENERIC += -DLT_TCP_SND_BUF=$(LT_TCP_SND_BUF)
endif
ifneq ($(strip $(LT_TCP_SND_QUEUELEN)),)
LT_CFLAGS_GENERIC += -DLT_TCP_SND_QUEUELEN=$(LT_TCP_SND_QUEUELEN)
endif
ifneq ($(strip $(LT_TCP_WND)),)
LT_CFLAGS_GENERIC += -DLT_TCP_WND=$(LT_TCP_WND)
endif
ifneq ($(strip $(LT_TCP_MEM_POOLS_256)),)
LT_CFLAGS_GENERIC += -DLT_TCP_MEM_POOLS_256=$(LT_TCP_MEM_POOLS_256)
endif
ifneq ($(strip $(LT_TCP_MEM_POOLS_512)),)
LT_CFLAGS_GENERIC += -DLT_TCP_MEM_POOLS_512=$(LT_TCP_MEM_POOLS_512)
endif
ifneq ($(strip $(LT_TCP_MEM_POOLS_1540)),)
LT_CFLAGS_GENERIC += -DLT_TCP_MEM_POOLS_1540=$(LT_TCP_MEM_POOLS_1540)
endif
ifneq ($(strip $(LT_MEMP_NUM_UDP_PCB)),)
LT_CFLAGS_GENERIC += -DLT_MEMP_NUM_UDP_PCB=$(LT_MEMP_NUM_UDP_PCB)
endif
ifneq ($(strip $(LT_LWIP_MEMP_STAT)),)
LT_CFLAGS_GENERIC += -DLT_LWIP_MEMP_STAT=$(LT_LWIP_MEMP_STAT)
endif
ifneq ($(strip $(LT_LWIP_DNS_DSCP)),)
LT_CFLAGS_GENERIC += -DLT_LWIP_DNS_DSCP=$(LT_LWIP_DNS_DSCP)
endif
ifneq ($(strip $(LT_LWIP_ICMP_TCP_LOG)),)
LT_CFLAGS_GENERIC += -DLT_LWIP_ICMP_TCP_LOG=$(LT_LWIP_ICMP_TCP_LOG)
endif
ifneq ($(strip $(LT_PBUF_POOL_SIZE)),)
LT_CFLAGS_GENERIC += -DLT_PBUF_POOL_SIZE=$(LT_PBUF_POOL_SIZE)
endif
ifneq ($(strip $(LT_MEMP_NUM_TCP_PCB)),)
LT_CFLAGS_GENERIC += -DLT_MEMP_NUM_TCP_PCB=$(LT_MEMP_NUM_TCP_PCB)
endif
# get Rules.mk and build-o-rama
include $(LT_PROJECT_RULES_MAKEFILE)
