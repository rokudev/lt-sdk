################################################################################
# CommonDriverMAX7221LED.mk - project makefile for the "common" platform's
#				        "CommonDriverMAX7221LED" LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PLATFORM_ROOT)/../common/source/common/driver/max7221led
LT_PROJECT_SOURCE_FILES      :=	CommonDriverMAX7221LEDImpl.c LTChipMAX7221Impl.c
# Defined in the DriverMAX7221LED.mk for the platform:
LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_MAX7221LED_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   18-Jun-21   titus       created
