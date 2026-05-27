################################################################################
# LTDriverMotorPanTiltMS320008N.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR   := $(LT_ROOTS_BASE)/lt/source/lt/driver/motor/
LT_PROJECT_SOURCE_FILES += LTDriverMotorPanTiltMS320008N.c
LT_PROJECT_SOURCE_FILES += motor.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)
