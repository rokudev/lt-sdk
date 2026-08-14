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
LT_PROJECT_SOURCE_FILES   := $(SOC_PLATFORM_NAME)/Esp32_LTCoreBSP.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_LTChipStart.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_Clock.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_Console.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_GPIO.c
LT_PROJECT_SOURCE_FILES   += $(SOC_PLATFORM_NAME)/Esp32_PSRAM.c

ifeq ($(SOC_PLATFORM_NAME),esp32s3)
  # The esp32s3 has to bring its own caches up - unlike the esp32, its bootloader
  # does not leave them configured and running for the application.
  LT_PROJECT_SOURCE_FILES += $(SOC_PLATFORM_NAME)/Esp32_Cache.c
endif

# Include directories
LT_PUBLIC_INCLUDE_FLAGS   += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(SOC_PLATFORM_NAME)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   22-Mar-22   tiberius    Created
#   03-Aug-26   claudius    esp32s3: added Esp32_Cache.c
#   13-Aug-26   claudius    shared with the esp32s3 variant: Esp32_LTCoreBSP.c and
#                           Esp32_LTChipStart.c are now per chip, under
#                           $(SOC_PLATFORM_NAME)/ like the rest
