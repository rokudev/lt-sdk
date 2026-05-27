################################################################################
# LTNetIce.mk - project makefile for LT Library LTNetIce
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/ice
LT_PROJECT_SOURCE_FILES := LTNetIce.c
LT_PROJECT_SOURCE_FILES += ProtocolMuxSocket.c

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   12-Jul-22   trajan      created
