################################################################################
# CommonDriverPushButton.mk - project makefile for Common LT Library
# 								CommonDriverPushButton
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR        := $(LT_PLATFORM_ROOT)/../common/source/common/driver/pushbutton
LT_PROJECT_SOURCE_FILES      := CommonDriverPushButtonImpl.c

# Defined in the PushButton.mk for the platform:
LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_PUSHBUTTON_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   05-Mar-21   constantine created
#   05-Jun-21   constantine moved to AmebaDriverPushButton.mk
