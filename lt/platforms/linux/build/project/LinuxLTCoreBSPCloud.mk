################################################################################
# LinuxLTCoreBSPCloud.mk - makefile for Linux BSP for LT running on Linux in
#  						   Cloud environments.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:= $(LT_PROJECT_SOURCE_DIR_BASE)/linux/ltcorebsp

LT_PROJECT_SOURCE_FILES		:= LTCoreBSP_Linux_Cloud.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   12-Sep-22   galerius    created
