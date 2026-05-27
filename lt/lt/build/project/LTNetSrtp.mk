################################################################################
# LTNetSrtp.mk - project makefile for LT Library LTNetSrtp
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/srtp
LT_PROJECT_SOURCE_FILES := LTNetSrtp.c
LT_PROJECT_SOURCE_FILES += RtpPacket.c
LT_PROJECT_SOURCE_FILES += RtpPacketPool.c
LT_PROJECT_SOURCE_FILES += RtcpPacket.c
LT_PROJECT_SOURCE_FILES += RtpStream.c
LT_PROJECT_SOURCE_FILES += RtpStreamFormatG711.c
LT_PROJECT_SOURCE_FILES += RtpStreamFormatH264.c
LT_PROJECT_SOURCE_FILES += RtpStreamFormatOpus.c
LT_PROJECT_SOURCE_FILES += RtpCongestionControl.c
LT_PROJECT_SOURCE_FILES += RtpSendPacer.c
LT_PROJECT_SOURCE_FILES += SrtpCrypto.c
LT_PROJECT_SOURCE_FILES += SrtpTransport.c

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   26-May-22   trajan      created
