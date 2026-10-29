################################################################################
# LTNetIperf3.mk - project makefile for LT Library LTNetIperf3
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/iperf3
LT_PROJECT_SOURCE_FILES := LTNetIperf3.c

# Server thread priority. The right value is the platform's WiFi thread priority, so
# the product states it rather than this generic library guessing; unset leaves the
# server at the LT default of 0.
LT_IPERF_SERVER_PRIORITY := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/product/iperf3/LT_IPERF_SERVER_PRIORITY" $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE))
ifneq ($(strip $(LT_IPERF_SERVER_PRIORITY)),)
LT_CFLAGS_GENERIC += -DLT_IPERF_SERVER_PRIORITY=$(LT_IPERF_SERVER_PRIORITY)
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)

# LT_IPERF_SERVER_PRIORITY reaches the compiler as a flag, and flags are not part of the
# object dependencies, so an incremental build would keep an object compiled with the old
# value. Rebuild when the product configuration changes.
$(LT_PROJECT_ALL_OBJ_FILES): $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE)

###############################################################################
#   LOG
###############################################################################
#   01-Mar-24   gratian     created
