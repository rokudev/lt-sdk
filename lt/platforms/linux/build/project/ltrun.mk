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

LT_PLATFORM_PROPER_NAME		?= Linux
LT_CFLAGS_GENERIC           += -DPLATFORM_PROPER_NAME=$(LT_PLATFORM_PROPER_NAME)

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	$(LT_PROJECT_SOURCE_DIR_BASE)/linux/ltrun
LT_PROJECT_SOURCE_FILES		:= 	ltrunmain.c

# if this platform doesn't have a runtime dynamic loader, the libraries
# make the libraries all statically link into ltrun
ifneq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
    # make ltrun depend on all of the libraries it is going to link into itself
    LT_EXECUTABLE_EXTRA_DEPENDS := $(LT_ALL_LIBRARIES_THIS_BUILD)

    # add -l to the lib projects for the link line
    LTRUN_LT_LIBRARIES_L := $(patsubst %,-l%,$(LT_ALL_LIBRARY_PROJECTS_THIS_BUILD))

    # Ensure the whole archive is included as the linker can't know which
    # symbols it can/can't keep
    LT_LDFLAGS_EXECUTABLE += -Wl,--whole-archive $(LTRUN_LT_LIBRARIES_L) -Wl,--no-whole-archive -ldl
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Jan-20   augustus    created
#   24-Feb-22   augustus    consolidated in the static linkage case
