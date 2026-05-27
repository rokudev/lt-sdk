################################################################################
# LTDriverNetTransportLwipEthOpenCores.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:= $(LT_PROJECT_SOURCE_DIR_BASE)/lt/driver/net/transport/lwipethopencores

LT_PROJECT_SOURCE_FILES     := LTDriverNetTransportLwipEthOpenCores.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_OS_ROOT)/thirdparty/lwip/roku-lt-port \
                               -I$(LT_OS_ROOT)/thirdparty/lwip/src/include

LT_CFLAGS_GENERIC += -DOPENCORES_ETH_INT_NUMBER=12 -DOPENCORES_ETH_INT_PRIORITY=1 -DOPENCORES_ETH_REG_BASE=0x3ff69000

# get Rules.mk
include $(LT_PROJECT_RULES_MAKEFILE)
