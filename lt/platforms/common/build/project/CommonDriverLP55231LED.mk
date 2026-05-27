################################################################################
# CommonDriverLP55231LED.mk - project makefile for the "common" platform's
#                        "CommonDriverLP55231LED" LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR        :=	$(LT_PLATFORM_ROOT)/../common/source/common/driver/lp55231led
LT_PROJECT_SOURCE_FILES      :=	CommonDriverLP55231LEDImpl.c
LT_PROJECT_SOURCE_FILES      +=	LTChipLP55231Impl.c
# Defined in the DriverLP55231LED.mk for the platform:
LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_LP55231LED_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   01-Oct-21   trajan      created
