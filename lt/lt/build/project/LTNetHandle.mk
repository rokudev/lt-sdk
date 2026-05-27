################################################################################
# LTNetHandle.mk - project makefile for the LTNetHandle
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/core
LT_PROJECT_SOURCE_FILES     := LTNetHandle.c

DEBUG_ASSERT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net/debug_assert" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
ifeq ($(strip $(DEBUG_ASSERT)),1)
LT_CFLAGS_GENERIC += -DDEBUG_ASSERT
endif

LTNETHANDLE_MAX_BUDGET := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/lt_net_handle/max_budget" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
ifneq ($(strip $(LTNETHANDLE_MAX_BUDGET)),)
LT_CFLAGS_GENERIC += -DLTNETHANDLE_MAX_BUDGET=$(LTNETHANDLE_MAX_BUDGET)
endif


# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   28-Aug-24   galba       created
