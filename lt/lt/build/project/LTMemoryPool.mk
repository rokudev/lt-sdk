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
LT_PROJECT_SOURCE_FILES     := LTMemoryPool.c
# make
DEBUG_MEMPOOL_PROF := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_buf/mempool_profile" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))

ifeq ($(strip $(DEBUG_MEMPOOL_PROF)),1)
LT_CFLAGS_GENERIC += -DDEBUG_MEMPOOL_PROF
endif

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Aug-24   galba       created
