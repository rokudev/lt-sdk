################################################################################
# libltrun.mk - project makefile for libltrun.a - a tiny static link library to
#                   link with any executable to give it access to the functions
#                   LT_Run() and LT_GetCore().
#
#                   This library gives any executable the ability as a process to
#                   host an instance of the LT operating system.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB	:= no
LT_PROJECT_BUILD_STATIC_LIB	:= yes
LT_PROJECT_BUILD_EXECUTABLE	:= no
LT_PROJECT_ARTIFACT		:= ltrun

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/macos/libltrun
LT_PROJECT_SOURCE_FILES		:= 	libltrun.c libltrun_loader_macos.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   17-Jun-20   caligula    created
