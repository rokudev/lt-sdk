################################################################################
# LTDriverNetTransportLwipWiFi.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/driver/net/transport/lwipwifi

LT_PROJECT_SOURCE_FILES     :=  LTDriverNetTransportLwipWiFi.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_OS_ROOT)/thirdparty/lwip/roku-lt-port \
                               -I$(LT_OS_ROOT)/thirdparty/lwip/src/include
LT_LWIP_MEMP_STAT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_MEMP_STAT" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_LWIP_ICMP_TCP_LOG := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_ICMP_TCP_LOG" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_WIFI_DRV_MEMPOOL := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/wifi/LT_WIFI_DRV_MEMPOOL" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LT_PBUF_POOL_SIZE := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_PBUF_POOL_SIZE" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
ifneq ($(strip $(LT_LWIP_MEMP_STAT)),)
LT_CFLAGS_GENERIC += -DLT_LWIP_MEMP_STAT=$(LT_LWIP_MEMP_STAT)
endif
ifneq ($(strip $(LT_LWIP_ICMP_TCP_LOG)),)
LT_CFLAGS_GENERIC += -DLT_LWIP_ICMP_TCP_LOG=$(LT_LWIP_ICMP_TCP_LOG)
endif
ifneq ($(strip $(LT_WIFI_DRV_MEMPOOL)),)
LT_CFLAGS_GENERIC += -DLT_WIFI_DRV_MEMPOOL=$(LT_WIFI_DRV_MEMPOOL)
endif
ifneq ($(strip $(LT_PBUF_POOL_SIZE)),)
LT_CFLAGS_GENERIC += -DLT_PBUF_POOL_SIZE=$(LT_PBUF_POOL_SIZE)
endif
LT_LWIP_TCP := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lwip/LT_LWIP_TCP" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
ifneq ($(strip $(LT_LWIP_TCP)),)
LT_CFLAGS_GENERIC += -DLWIP_TCP=$(LT_LWIP_TCP)
endif

# get Rules.mk
include $(LT_PROJECT_RULES_MAKEFILE)

# The driver's socket paths are compiled against the same LWIP_TCP selection as
# the stack, so a stale object here would pair a TCP-aware driver with a
# TCP-less lwIP (or the reverse).  Track the value the same way the stack does.
LT_LWIP_TCP_STAMP := $(LT_PROJECT_OBJ_DIR)/lwip-tcp-selection.stamp
$(LT_LWIP_TCP_STAMP): LT_LWIP_TCP_VALUE := $(strip $(LT_LWIP_TCP))
$(LT_PROJECT_ALL_OBJ_FILES): $(LT_LWIP_TCP_STAMP)

.PHONY: lwip-tcp-selection-force
lwip-tcp-selection-force:

$(LT_LWIP_TCP_STAMP): lwip-tcp-selection-force
	$(LT_EXEC_CMD) mkdir -p $(@D)
	$(LT_EXEC_CMD) printf '%s\n' '$(LT_LWIP_TCP_VALUE)' | cmp -s - $@ || printf '%s\n' '$(LT_LWIP_TCP_VALUE)' > $@
