################################################################################
# LTUtilityJsonParser.mk - project makefile for LT Library LTUtilityJsonParser
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/lt/utility/jsonparser
LT_PROJECT_SOURCE_FILES		:= 	LTUtilityJsonParser.c
LT_PROJECT_SOURCE_FILES		+= 	JSON_parser.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   18-Jun-24   augustus    created
