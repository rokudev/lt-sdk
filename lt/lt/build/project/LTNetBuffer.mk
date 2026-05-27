################################################################################
# LTMemoryPoolManager.mk - project makefile for the LTMemoryPoolManager
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/core
LT_PROJECT_SOURCE_FILES     += LTNetBuffer.c

DEBUG_ASSERT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net/debug_assert" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
DEBUG_NETBUF_SANTIZER := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/debug_netbuf_sanitize" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_META_POOL_BLOCK_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/meta_pool_block_count" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_SMALL_POOL_BLOCK_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/small_pool_block_count" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_SMALL_POOL_BLOCK_SIZE  := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/small_pool_block_size" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/medium_pool_block_count" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE  := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/medium_pool_block_size" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_LARGE_POOL_BLOCK_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/large_pool_block_count" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
LTNETBUFF_LARGE_POOL_BLOCK_SIZE  := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/large_pool_block_size" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))

ifeq ($(strip $(DEBUG_ASSERT)),1)
LT_CFLAGS_GENERIC += -DDEBUG_ASSERT
endif
ifeq ($(strip $(DEBUG_NETBUF_SANTIZER)),1)
LT_CFLAGS_GENERIC += -DDEBUG_NETBUF_SANTIZER
endif

ifneq ($(strip $(LTNETBUFF_META_POOL_BLOCK_COUNT)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_META_POOL_BLOCK_COUNT=$(LTNETBUFF_META_POOL_BLOCK_COUNT)
endif

ifneq ($(strip $(LTNETBUFF_SMALL_POOL_BLOCK_COUNT)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_SMALL_POOL_BLOCK_COUNT=$(LTNETBUFF_SMALL_POOL_BLOCK_COUNT)
endif

ifneq ($(strip $(LTNETBUFF_SMALL_POOL_BLOCK_SIZE)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_SMALL_POOL_BLOCK_SIZE=$(LTNETBUFF_SMALL_POOL_BLOCK_SIZE)
endif

ifneq ($(strip $(LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_MEDIUM_POOL_BLOCK_COUNT=$(LTNETBUFF_MEDIUM_POOL_BLOCK_COUNT)
endif

ifneq ($(strip $(LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_MEDIUM_POOL_BLOCK_SIZE=$(LTNETBUFF_MEDIUM_POOL_BLOCK_SIZE)
endif

ifneq ($(strip $(LTNETBUFF_LARGE_POOL_BLOCK_COUNT)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_LARGE_POOL_BLOCK_COUNT=$(LTNETBUFF_LARGE_POOL_BLOCK_COUNT)
endif

ifneq ($(strip $(LTNETBUFF_LARGE_POOL_BLOCK_SIZE)),)
LT_CFLAGS_GENERIC += -DLTNETBUFF_LARGE_POOL_BLOCK_SIZE=$(LTNETBUFF_LARGE_POOL_BLOCK_SIZE)
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Aug-24   galba       created
