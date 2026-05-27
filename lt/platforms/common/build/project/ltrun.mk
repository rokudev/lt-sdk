################################################################################
# ltrun.mk - 		project makefile for executable program "ltrun"
#
#                   The ltrun executable program starts, from function main, an
#                   instance of the LT operating system, passing argc/argv to
#                   LT_Run().  LT_Run() parses the command iine for the name
# 					of the user specified "genesis" library which it opens and
#                   transfers control to by calling that library's Run() function,
#                   also passing along argc/argv.  LT_Run() regains control when
#                   the genesis library's Run() function returns at which time
#					LT_Run() gracefully shuts LT down when all threads spawned by
#                   the genesis library have gracefully exited.  It passes the
#                   passes the genesis library's return code back to function
#                   main which dutifully returns it as the process return code.
#
#					The ltrun program, therefore, is an LT library launcher, able to
#					be used in shell scripts for spawning daemons or used for
#                   command line utilities, test automation, etc., where the
#                   implementations of such are LT libraries that may run
#                   in an entirely different context, e.g. on Elk or Cheapside or
#                   in a mobile app, etc.
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
LT_PROJECT_ARTIFACT			:= ltrun

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/common/ltrun
LT_PROJECT_SOURCE_FILES		:= 	ltrunmain.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Jan-20   augustus    created
