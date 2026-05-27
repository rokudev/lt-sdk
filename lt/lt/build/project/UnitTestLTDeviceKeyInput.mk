################################################################################
# UnitTestLTDeviceKeyInput.mk - project makefile for LT Library UnitTestLTDeviceKeyInput
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/unittest/lt/device/keyinput
LT_PROJECT_SOURCE_FILES := UnitTestLTDeviceKeyInputImpl.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   15-Nov-21   vitellius   created
