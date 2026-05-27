################################################################################
# LTShellMedia.mk - project makefile for LT Library LTShellMedia
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/ltshell/media
LT_PROJECT_SOURCE_FILES		:= 	LTShellMedia.c 
LT_PROJECT_SOURCE_FILES		+= 	LTShellVideoDriver.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaOpus.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaOpusPlayback.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaOpusCapture.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaPCMPlayback.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaPCMCapture.c
LT_PROJECT_SOURCE_FILES		+= 	LTShellMediaPlayTone.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)
