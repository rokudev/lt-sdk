################################################################################
# liblthost.mk - project makefile for liblthost.a - a tiny static link library to
#                link with any executable to give its process the ability to
#				 host an instance of the LT operating system.
#
#                This library, liblthost.a, provides two functions: LT_GetCore()
#                and LT_Run().  The version of LT_GetCore() in this library
#                [lazily] initializes the LT operating system in "HOSTED" mode.
#                The first time LT_GetCore() is called, either directly or via
#                one of the LTCore.h convenience macrs, e.g. lt_openlibrary,
#                it dynamically loads libLTCore.so with dlopen(), and calls
#                functions therin to perform the HOSTED mode initialization.
#                In this library, liblthost.a, LT_Run() works like the shell
#		 ltrun command.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_STATIC_LIB	:= yes
LT_PROJECT_BUILD_SHARED_LIB	:= no
LT_PROJECT_BUILD_EXECUTABLE	:= no
LT_PROJECT_ARTIFACT		:= lthost

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/apple/liblthost
LT_PROJECT_SOURCE_FILES		:= 	liblthost.m

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   31-Oct-21   augustus    created
