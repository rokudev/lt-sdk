################################################################################
# LTDeviceIrTx.mk - project makefile for LTDeviceIrTx
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# Set to 1 if building with Universal IR support (the default),
# or to 0 to stub it out (when providing sources for other vendors)
RCU_UNIVERSAL_IR_SUPPORT    := 1

# Source directory:
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/device/irtx

# Source files in main dir:
LT_PROJECT_SOURCE_FILES		:= 	LTDeviceIrTxImpl.c

ifeq ($(RCU_UNIVERSAL_IR_SUPPORT), 1)
	# Subdirs for this project
	LT_PROJECT_SOURCE_SUBDIRS   += universal_ir

	# Source files under the subdirs:
	LT_PROJECT_SOURCE_FILES     += universal_ir/universal_ir_vendor_interface.c
	LT_PROJECT_SOURCE_FILES     += universal_ir/universal_ir_mem_share.c
	LT_PROJECT_SOURCE_FILES     += universal_ir/universal_ir_ssu_convert.c
endif

# Add in some needed defines and flags
MODULE_FLAGS := -Wno-unused-parameter -DIR_FUNCTION_EN

ifeq ($(RCU_UNIVERSAL_IR_SUPPORT), 1)
	LT_CFLAGS_LIBRARY += \
		-I$(LT_PROJECT_SOURCE_DIR)              \
		-I$(LT_PROJECT_SOURCE_DIR)/universal_ir \
		-I$(LT_PROJECT_SOURCE_DIR)/universal_ir/inc_internal

	MODULE_FLAGS += -DRCU_UNIVERSAL_IR_SUPPORT
endif

LT_CFLAGS_GENERIC += $(MODULE_FLAGS)


# make
include $(LT_PROJECT_RULES_MAKEFILE)
