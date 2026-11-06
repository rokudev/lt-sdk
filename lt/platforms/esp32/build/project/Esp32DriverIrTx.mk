################################################################################
# Esp32DriverIrTx.mk - project makefile for the ESP32 platform's IR Transmit
#                      driver.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# Source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/irtx

# Source files in the main dir:
LT_PROJECT_SOURCE_FILES     += Esp32DriverIrTxImpl.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#  10-Sep-2026    augustus      created
