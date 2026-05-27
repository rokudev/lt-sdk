################################################################################
# lthost.mk - 	    project makefile for executable program "lthost"
#
#                   lthost is a Linux command line program that illustrates
#                   how to use liblthost.a to run LT in hosted mode.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB	:= no
LT_PROJECT_BUILD_STATIC_LIB	:= no
LT_PROJECT_BUILD_EXECUTABLE	:= yes
LT_PROJECT_ARTIFACT		:= lthost

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/macos/lthost
LT_PROJECT_SOURCE_FILES		:= 	lthost.c

# adjust linker flags
LT_LDFLAGS_EXECUTABLE := $(patsubst -lltrun,,$(LT_LDFLAGS_EXECUTABLE))
LT_LDFLAGS_EXECUTABLE := $(patsubst -ldl,,$(LT_LDFLAGS_EXECUTABLE))
LT_LDFLAGS_EXECUTABLE := $(patsubst -pthread,,$(LT_LDFLAGS_EXECUTABLE))
LT_LDFLAGS_EXECUTABLE += -llthost -ldl -pthread

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   31-Oct-21   augustus    created
