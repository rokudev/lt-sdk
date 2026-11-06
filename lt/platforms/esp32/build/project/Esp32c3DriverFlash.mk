################################################################################
# Esp32c3DriverFlash.mk - project makefile for the ESP32-C3 platform's
#                         Esp32c3DriverFlash LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/flash/esp32c3
LT_PROJECT_SOURCE_FILES     := Esp32c3DriverFlash.c
LT_PROJECT_SOURCE_FILES     += Esp32c3FlashDeviceUnit.c
LT_PROJECT_SOURCE_FILES     += Esp32c3SPIFlash.c
LT_PROJECT_SOURCE_FILES     += Esp32c3SPIFlashCache.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp32c3

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   17-Sep-26   claudius    created
