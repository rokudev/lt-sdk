################################################################################
#
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
#
################################################################################


LT_TOOL_SOURCEDIR           := $(abspath $(dir $(abspath "$(lastword $(LAST_MAKEFILE_LIST))")))
LT_CURRENTLY_BUILDING_TOOL  := testmediamp4

include ../../../../build/tools/common/build/LTBuildTool-Preamble.mk

LT_TOOL_SOURCE_SUBDIRS      :=
LT_TOOL_SOURCE_FILES        := testmediamp4.c
LT_TOOL_LT_LIBRARIES        := LTMediaMP4

include ../../../../build/tools/common/build/LTBuildTool-Postamble.mk
