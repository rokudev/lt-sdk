###############################################################################
# CommonDriverSE5118Slider.mk - project makefile for the "common" platform's
#	        "CommonDriverSE5118Slider" LT driver library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
###############################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR := \
    $(LT_PLATFORM_ROOT)/../common/source/common/driver/se5118slider
LT_PROJECT_SOURCE_FILES      :=	CommonDriverSE5118SliderImpl.c

LT_CFLAGS_GENERIC += -DCONFIGURATION_FILE=$(LT_DRIVER_SLIDER_CONFIGURATION_FILE)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   03-Feb-22   commodus    created
