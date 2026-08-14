################################################################################
# Esp32s3DriverFlash.mk - project makefile for the ESP32-S3 platform's
#                         Esp32s3DriverFlash LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/flash/esp32s3
LT_PROJECT_SOURCE_FILES     := Esp32s3DriverFlash.c
LT_PROJECT_SOURCE_FILES     += Esp32s3FlashDeviceUnit.c
LT_PROJECT_SOURCE_FILES     += Esp32s3SPIFlash.c
LT_PROJECT_SOURCE_FILES     += Esp32s3SPIFlashCache.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp32s3

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   29-Jul-26   claudius    created
#   04-Aug-26   claudius    moved to the esp32s3 platform root
#   13-Aug-26   claudius    back in the esp32 platform root, under driver/flash/esp32s3
