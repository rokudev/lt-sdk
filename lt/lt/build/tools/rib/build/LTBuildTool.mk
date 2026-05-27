################################################################################
# LTBuildTool.mk                                        Roku Image Builder (rib)
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

include ../../common/build/LTBuildTool-Preamble.mk

LT_TOOL_SOURCE_SUBDIRS := source

LT_TOOL_SOURCE_FILES   := source/rib.c
LT_TOOL_SOURCE_FILES   += source/Image.c
LT_TOOL_SOURCE_FILES   += source/Builder.c
LT_TOOL_SOURCE_FILES   += source/Assets.c

LT_TOOL_LT_LIBRARIES   := LTSystemCrypto
LT_TOOL_LT_LIBRARIES   += LTSoftwareCrypto
LT_TOOL_LT_LIBRARIES   += LTUtilityByteOps
LT_TOOL_LT_LIBRARIES   += LTUtilityJsonParser

include ../../common/build/LTBuildTool-Postamble.mk

