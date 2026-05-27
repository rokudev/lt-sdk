################################################################################
# CommonDriverPIRBL612.mk - project makefile for Common LT Library
# 								CommonDriverBL612PIR
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR        := $(LT_PLATFORM_ROOT)/../common/source/common/driver/bl612pir
LT_PROJECT_SOURCE_FILES      := CommonDriverPIRBL612Impl.c

# Defined in the PushButton.mk for the platform:
LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_BL612PIR_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   03-Feb-21   vitellius   created
