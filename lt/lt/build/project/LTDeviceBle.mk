################################################################################
# LTDeviceBle.mk - project makefile for LT Library LTDeviceBle
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/device/ble
LT_PROJECT_SOURCE_FILES		:= 	LTDeviceBle.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   05-Sep-22   vespasian   created
