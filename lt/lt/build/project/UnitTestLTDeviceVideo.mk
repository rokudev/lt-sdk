################################################################################
# UnitTestLTDeviceVideo.mk - project makefile for LT Library UnitTestLTDeviceVideo
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/unittest/lt/device/video
LT_PROJECT_SOURCE_FILES := UnitTestLTDeviceVideo.c \
                           TestRunNoTilt.c

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Feb-24   gallienus   created
