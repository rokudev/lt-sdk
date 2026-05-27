################################################################################
# LTDeviceConfig.mk - project makefile for LTDeviceConfig
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/device/config
LT_PROJECT_SOURCE_FILES		:= 	LTDeviceConfig.c LTDeviceKonfig.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   30-Aug-22   aurelian    created
#   30-Jan-23   augustus    automated arbolation and arbolation incorporation
