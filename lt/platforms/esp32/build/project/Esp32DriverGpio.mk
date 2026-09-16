################################################################################
# Esp32DriverGpio.mk - project makefile for the ESP32 platform's
#                      Esp32DriverGpio LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# One source file serves both chips: everything that differs is behind the
# $(SOC_PLATFORM_NAME) copy of Esp32_GPIO.h.
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/gpio
LT_PROJECT_SOURCE_FILES     := Esp32DriverGpio.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(SOC_PLATFORM_NAME)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   08-Sep-26   claudius    created
