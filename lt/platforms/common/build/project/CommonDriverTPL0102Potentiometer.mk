################################################################################
# CommonDriverTPL0102Potentiometer.mk - project makefile for the "common" platform's
#				        "CommonDriverTPL0102Potentiometer" LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PLATFORM_ROOT)/../common/source/common/driver/tpl0102potentiometer
LT_PROJECT_SOURCE_FILES      :=	CommonDriverTPL0102PotentiometerImpl.c

LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_POTENTIOMETER_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   27-Jan-22   vitellius   created
