################################################################################
# Esp32DriverVideoDVP.mk
#
# ESP32 DVP parallel camera driver implementing the LTDeviceVideo / ILTVideo
# interface.  Uses I²S0 in camera mode with DMA ring for frame capture, and
# LTDeviceImageSensor for sensor configuration.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/video/dvp

LT_PROJECT_SOURCE_FILES     := Esp32DriverVideoDVP.c

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp32

include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################
#   LOG
################################################################################
#   31-May-26   created
