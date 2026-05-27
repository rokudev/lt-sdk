################################################################################
# Esp32_DriverPins.mk - project makefile for the ESP32 platform's
#                       Esp32DriverPins LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/pins
LT_PROJECT_SOURCE_FILES     := Esp32DriverPinsImpl.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp32

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   15-Jul-22   vitellius   created
