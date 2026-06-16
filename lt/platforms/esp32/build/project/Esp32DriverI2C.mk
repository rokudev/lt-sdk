################################################################################
# Esp32DriverI2C.mk
#
# ESP32 hardware I²C master driver implementing the ILTDriverI2C interface.
# Uses the ESP32's dedicated I²C controller (port 0 or port 1) rather than
# bit-banging GPIO, giving accurate timing for SCCB/I²C sensor buses.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/i2c
LT_PROJECT_SOURCE_FILES := Esp32DriverI2C.c

LT_PUBLIC_INCLUDE_FLAGS += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp32

include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################
#   LOG
################################################################################
#   02-Jun-26   created
