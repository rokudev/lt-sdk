################################################################################
# Esp32_LTCoreBSP.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# 1. Specify library source dir and subdirs, if any
LT_PROJECT_SOURCE_DIR     := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/ltcorebsp
LT_SDK_COMPONENT_DIR      := $(LT_PLATFORM_VENDOR_SDK_ROOT)/components
LT_PROJECT_SOURCE_SUBDIRS += $(SOC_PLATFORM_NAME)

# 2. LTCoreBSP and LTChipStart source files
LT_PROJECT_SOURCE_FILES   := Esp32_LTCoreBSP.c
LT_PROJECT_SOURCE_FILES   += Esp32_LTChipStart.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_Clock.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_GPIO.c

# Include directories
LT_PUBLIC_INCLUDE_FLAGS   += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(SOC_PLATFORM_NAME)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   22-Mar-22   tiberius    Created
