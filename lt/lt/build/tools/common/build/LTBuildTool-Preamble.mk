################################################################################
# LTBuildTool-Preamble.mk - Part 1 of  LT Master Make's native build tool system
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################
#
# DESCRIPTION
#    The LT Master Make system builds a few host program build tools that it
#    uses for various purposes in the build, most notably for firmware image
#    preparation and manipulation.
#
#    To create a new build tool that is automatically incorporated into the
#    LT master makefile build process, just make a subdirectory under
#    ~/git/lt/tools with the name of your tool and inside that
#    subdir make a build/ subdir and place a file called LTBuildTool.mk
#    inside.  This will make the master makefile notice your tool
#    and automatically build it by invoking your LTBuildTool.mk makefile.
#
#    Your LTBuildTool.mk file can do anything as long as it deposits the
#    resulting tool program with the same name as the subdir of the tool
#    into the $(LT_TARGET_DIR_BUILDTOOLS)/bin directory.
#
#    All of the standard LT master makefile variables are available to
#    your LTBuldTool.mk makefile.
#
#    The LTBuildTool.mk build system also supports invocation standalone
#    from the LT master makefile which may aid in development and testing
#    of your LTBuildTool.mk file.  In the case of standalone invocation
#    only LT_TARGET_ROOT must be defined in your environment; the rest
#    of the required variables, normally provisioned by the LT Master
#    Makefile are automatically derived.
#
#    Example tool: creating the arbolator tool
#    arbolator source dir:  ~/git/lt/tools/arbolator
#    arbolator makefile:    ~/git/lt/tools/arbolator/build/LTBuildTool.mk
#
#    arbolator makefile contents:
#    ____________________________________
#
#      include ../../common/build/LTBuildTool-Preamble.mk
#         LT_TOOL_SOURCE_SUBDIRS := source
#         LT_TOOL_SOURCE_FILES   := source/Arbolate.c
#         LT_TOOL_SOURCE_FILES   += source/JsonTree.c
#         LT_TOOL_LT_LIBRARIES   := LTUtilityJsonParser
#      include ../../common/build/LTBuildTool-Postamble.mk
#    ____________________________________
#
#
#    How to build the arbolator with the master makefile:
#      Nothing to do, completely automatic.
#
#    How to build the arbolator without the master makefile:
#      % cd ~/git/lt/tools/arbolator/build
#      % LT_TARGET_ROOT=/tmp/test make -f LTBuildTool.mk
#         or
#      % LT_TARGET_ROOT=/tmp/test make -C ~/git/lt/tools/arbolator/build -f LTBuildTool.mk
#   _______________________________
#
#    Note: The LTBuildTool-Preamble.mk makefile defines many useful variables to aid you in
#          the construction of your makefile.  It only defines variables and does not create
#          any makefile targets so it is always completely safe to include in your
#          LTBuildTool.mk, even if your program's build process is not simple like the arbolator's.
#
#          The LTBuildTool-Postamble.mk file defines makefile targets to automatically build
#          the tool when included into the LTBuildTool.mk file.
#
################################################################################

# __________________
# Standalone support: LT_TARGET_ROOT
# Error out when a build tool makefile is invoked standalone (not from LT master makefile)
# to force the user to set LT_TARGET_ROOT, No default is chosen for LT_TARGET_ROOT because
# that would result in be splatting files down on disk in a default location that may
# not be convenient for users, and that's just downright rude.
#
# So, to invoke your buildtool makefile for standalone test, invoke like this:
# LT_TARGET_ROOT=/tmp/buildtool/test make -C ~/git/lt/tools/arbolator/build -f LTBuildTool.mk
#

ifeq (,$(LT_TARGET_ROOT))
  $(error LT_TARGET_ROOT must be specified.  Invoke from master makefile or set env variable.  Now we must)
endif


#
# The rest of the variables for standalone mode are automatically derived without harm.
#

# __________________
# Standalone support: LT_TARGET_DIR_BUILDTOOLS
# This is going in the same place as the master makefile puts it relative to LT_TARGET_ROOT
# It's not a problem if they get out of sync as long as its under the LT_TARGET_ROOT; this
# standalone business is just for testing your build tool makefile in isolation.
#
ifeq (,$(LT_TARGET_DIR_BUILDTOOLS))
  LT_TARGET_DIR_BUILDTOOLS   :=$(LT_TARGET_ROOT)/targets/buildtools
endif
#

# __________________
# Standalone support: LT_QUIET.
# Quiet is only supported when invoked from the master makefile; the following determines when
# quiet isn't setup and supresses the quiet messages so only the regular messages emit.
#
ifeq (,$(LT_QUIET_CMD))
ifeq (,$(LT_EXEC_CMD))
  LT_QUIET_CMD               := @echo > /dev/null
endif
endif

LT_LAST_MAKEFILE_LIST        := $(filter-out $(lastword $(MAKEFILE_LIST)),$(MAKEFILE_LIST))
LT_TOOL_SOURCEDIR            := $(abspath $(dir $(abspath "$(lastword $(LAST_MAKEFILE_LIST))"))..)
ifeq (,$(LT_OS_ROOT))
  LT_OS_ROOT                 := $(abspath $(LT_TOOL_SOURCEDIR)/../../lt)
endif
ifeq (,$(LT_ROOTS_BASE))
  LT_ROOTS_BASE              := $(abspath $(LT_OS_ROOT)/..)
endif

#$(info LT_LAST_MAKEFILE_LIST = $(LT_LAST_MAKEFILE_LIST))
#$(info LT_TOOL_SOURCEDIR = $(LT_TOOL_SOURCEDIR))
#$(info LT_OS_ROOT = $(LT_OS_ROOT))
#$(info LT_ROOTS_BASE = $(LT_ROOTS_BASE))
#$(error Please)

LT_BUILDTOOLS_THIRDPARTY_DIR := $(LT_OS_ROOT)/../tools/thirdparty
LT_CURRENTLY_BUILDING_TOOL   := $(shell basename $(LT_TOOL_SOURCEDIR))
LT_TARGET_TOOLSDIR_BIN       := $(LT_TARGET_DIR_BUILDTOOLS)/bin
LT_TARGET_TOOLSDIR_LIB       := $(LT_TARGET_DIR_BUILDTOOLS)/lib
LT_TARGET_TOOLSDIR_OBJ       := $(LT_TARGET_DIR_BUILDTOOLS)/obj/$(LT_CURRENTLY_BUILDING_TOOL)
LT_TOOL_TARGET               := $(LT_TARGET_TOOLSDIR_BIN)/$(LT_CURRENTLY_BUILDING_TOOL)
LT_TOOL_LT_LIBRARIES         :=

LT_TOOL_CROSS_PREFIX         ?=

# Detect the host OS for build tools - needed when cross-compiling
# (e.g. building bl702l/bl618d firmware on macOS where LT_PLATFORM_FAMILY
# is the target platform, not the host).
LT_TOOL_HOST_OS                  := $(shell uname -s)
LT_TOOL_CC                   := $(LT_TOOL_CROSS_PREFIX)gcc
LT_TOOL_CXX                  := $(LT_TOOL_CROSS_PREFIX)g++
LT_TOOL_LD                   := $(LT_TOOL_CC)
LT_TOOL_STRIP                := $(LT_TOOL_CROSS_PREFIX)strip

ifeq (debug, $(LT_TOOL_BUILD_MODE))
  LT_TOOL_CFLAGS             := -std=gnu99 -Wall -Werror -g -O0 -MMD -MP -I$(LT_OS_ROOT)/include
  LT_TOOL_CXXFLAGS           := -Wall -Werror -g -O0 -MMD -MP -I$(LT_OS_ROOT)/include
else
  LT_TOOL_BUILD_MODE         := release
  LT_TOOL_CFLAGS             := -std=gnu99 -Wall -Werror -O2 -MMD -MP -I$(LT_OS_ROOT)/include
  LT_TOOL_CXXFLAGS           := -Wall -Werror -O2 -MMD -MP -I$(LT_OS_ROOT)/include
endif

ifeq (Darwin, $(LT_TOOL_HOST_OS))
  LT_TOOL_CFLAGS             += -std=c11 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
endif

LT_TOOL_LDFLAGS              :=

ifeq (Darwin, $(LT_TOOL_HOST_OS))
  LT_TOOL_STRIPFLAGS           :=
else
  LT_TOOL_STRIPFLAGS           := -s
endif

###############################################################################
#   LOG
###############################################################################
#   28-Jan-23   augustus    created

