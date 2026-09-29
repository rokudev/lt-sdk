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

ifneq ($(filter $(SOC_PLATFORM_NAME),esp32 esp32s3 esp32p4),)
  # The esp32c3 is the only one of these with no PSRAM interface at all, so
  # there is no Esp32_PSRAM.h under include/esp32c3 either.
  LT_PROJECT_SOURCE_FILES += $(SOC_PLATFORM_NAME)/Esp32_PSRAM.c
endif

ifneq ($(filter $(SOC_PLATFORM_NAME),esp32s3 esp32c3 esp32p4),)
  # These parts have to bring their own caches up - unlike the esp32, their
  # bootloaders do not leave them configured and running for the application.
  LT_PROJECT_SOURCE_FILES += $(SOC_PLATFORM_NAME)/Esp32_Cache.c
endif

ifneq ($(filter $(SOC_PLATFORM_NAME),esp32c3 esp32p4),)
  # RISC-V: LTK's SetInterruptVector/SetInterruptPriority are no-ops, so the BSP
  # owns the vector table, the trap dispatcher, the system tick and the cycle
  # counter.  The Xtensa parts get all of that from LTK.
  LT_PROJECT_SOURCE_FILES += $(SOC_PLATFORM_NAME)/Esp32_Interrupt.c
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
#   17-Sep-26   claudius    esp32c3: added Esp32_Interrupt.c and Esp32_Cache.c,
#                           gated out Esp32_PSRAM.c
#   21-Sep-26   claudius    esp32p4: joins the esp32c3 on Esp32_Interrupt.c (CLIC
#                           rather than an interrupt matrix, but the BSP owns the
#                           vectors either way) and on Esp32_Cache.c
#   28-Sep-26   claudius    esp32p4: added Esp32_PSRAM.c
