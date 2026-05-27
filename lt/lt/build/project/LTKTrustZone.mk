################################################################################
# LTKTrustZone.mk - project makefile for LTK TrustZone library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR    := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/ltk
LT_PROJECT_SOURCE_FILES  := LTKArchArmCortexM_TrustZone.c

# enable security extensions
LT_CFLAGS_GENERIC        += -mcmse

# replace existing debug flags with -Os for code-size
LT_CFLAGS_DEBUG          := -Os

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   15-Nov-21   tiberius    Created
