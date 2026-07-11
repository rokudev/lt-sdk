################################################################################
# STLTCoreBSP.mk - makefile for ST BSP for LT
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/st/ltcorebsp

LT_PROJECT_SOURCE_FILES := STStartup.c
LT_PROJECT_SOURCE_FILES += LTCoreBSP_STMicro.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   19-Jan-21   tiberius    created
#   10-Jul-26   augustus    moved st hal files to stm32_sdk.mk
