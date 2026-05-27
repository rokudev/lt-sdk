################################################################################
# LTNetWebRtc.mk - project makefile for LT Library LTNetWebRtc
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/webrtc
LT_PROJECT_SOURCE_FILES := LTNetWebRtc.c
LT_PROJECT_SOURCE_FILES += LTRtcSessionDescription.c

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   22-May-22   trajan      created
