################################################################################
# Esp32p4_LTOSAdapter.mk
#
# Esp32_LTOSAdapter.mk - temp for __wrap functions
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB := no
LT_PROJECT_BUILD_STATIC_LIB := yes
LT_PROJECT_BUILD_EXECUTABLE := no

# source dir and files
LT_PROJECT_SOURCE_DIR    := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/esp32-lt-os-adapter
LT_PROJECT_SOURCE_FILES  := Esp32p4_LTOSAdapter.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)
