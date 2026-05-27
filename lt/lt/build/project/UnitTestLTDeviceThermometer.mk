################################################################################
# UnitTestLTDeviceThermometer.mk - project makefile for LT Library UnitTestLTDeviceThermometer
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/unittest/lt/device/thermometer
LT_PROJECT_SOURCE_FILES := UnitTestLTDeviceThermometerImpl.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   27-Jan-21   constantine created
#   20-Oct-21   constantine moved to UnitTestLTDeviceThermometer
