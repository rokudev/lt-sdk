################################################################################
# LTUtilityByteOps.mk - project makefile for LT Library LTUtilityByteOps
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/utility/byteops
LT_PROJECT_SOURCE_FILES		:= 	LTUtilityByteOps.c
LT_PROJECT_SOURCE_FILES		+= 	LTUtilityByteOpsBase64.c
LT_PROJECT_SOURCE_FILES		+= 	LTUtilityByteOpsCrc.c
LT_PROJECT_SOURCE_FILES		+= 	LTUtilityByteOpsHex.c
LT_PROJECT_SOURCE_FILES		+= 	LTUtilityByteOpsRandom.c
LT_PROJECT_SOURCE_FILES		+= 	LTUtilityByteOpsUUID.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   22-Nov-21   augustus    created
