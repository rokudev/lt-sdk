################################################################################
# LTBuildTool.mk                                           Roku Image Tool (rit)
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

include ../../common/build/LTBuildTool-Preamble.mk

LT_TOOL_SOURCE_SUBDIRS := source
LT_TOOL_SOURCE_SUBDIRS += source/platforms

LT_TOOL_SOURCE_FILES   := source/rit.c
LT_TOOL_SOURCE_FILES   += source/Image.c
LT_TOOL_SOURCE_FILES   += source/FlashDevice.c
LT_TOOL_SOURCE_FILES   += source/Serial.c

# Allow sources under source/ and source/platforms/ to include headers via "Header.h".
LT_TOOL_CFLAGS         += -I$(LT_TOOL_SOURCEDIR)/source -I$(LT_TOOL_SOURCEDIR)/source/platforms

# Auto-detect platform implementations (only include present files for vendor drops)
LT_TOOL_SOURCE_FILES   += $(patsubst $(LT_TOOL_SOURCEDIR)/%,%,$(wildcard $(LT_TOOL_SOURCEDIR)/source/platforms/*.c))

# Platform flash support self-registers at startup via LT_USED_CONSTRUCTOR.

LT_TOOL_LT_LIBRARIES   := LTSystemCrypto
LT_TOOL_LT_LIBRARIES   += LTSoftwareCrypto
LT_TOOL_LT_LIBRARIES   += LTUtilityByteOps

include ../../common/build/LTBuildTool-Postamble.mk

