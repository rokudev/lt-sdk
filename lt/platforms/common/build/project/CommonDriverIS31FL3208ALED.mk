################################################################################
# CommonDriverIS31FL3208ALED.mk - project makefile for the "common" platform's
#                        "CommonDriverIS31FL3208ALED" LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR        :=	$(LT_PLATFORM_ROOT)/../common/source/common/driver/is31fl3208aled
LT_PROJECT_SOURCE_FILES      :=	CommonDriverIS31FL3208ALEDImpl.c
LT_PROJECT_SOURCE_FILES      +=	LTChipIS31FL3208AImpl.c
# Defined in the DriverIS31FL3208ALED.mk for the platform:
LT_CFLAGS_GENERIC            += -DCONFIGURATION_FILE=$(LT_DRIVER_IS31FL3208ALED_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   28-Oct-21   trajan      created
