################################################################################

# UserLib.mk - make a user library separate from LT, platform and product roots

#

# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.

################################################################################


ifeq (,$(LT_USER_LIB_DIR))
$(error LT_USER_LIB_DIR not set)
endif

LT_USER_LIB_DIR := $(abspath $(LT_USER_LIB_DIR))
ifeq (no, $(shell if [ -d "$(LT_USER_LIB_DIR)" ]; then echo yes; else echo no; fi))
$(error $(LT_USER_LIB_DIR) does not exist)
endif

# source dir and files
LT_PROJECT_SOURCE_DIR		:= $(or $(shell find $(LT_USER_LIB_DIR) -maxdepth 1 -type d -name "source"),$(LT_USER_LIB_DIR))
LT_PROJECT_SOURCE_SUBDIRS	:= $(patsubst $(LT_PROJECT_SOURCE_DIR)/%,%,$(shell find $(LT_PROJECT_SOURCE_DIR) -mindepth 1 -type d))
LT_PROJECT_SOURCE_FILES 	:= $(shell find $(LT_PROJECT_SOURCE_DIR) -type f -name "*.c")

# All possible combinations of < style includes
LT_USER_PROJECT_ILLEGAL_FILES := $(shell grep -rhl "\s*[\#]\s*include\s*<[[:graph:]]*/source/" $(strip $(LT_PROJECT_SOURCE_DIR)))

# No rules for "../../" style includes because it should? be impossible for this style to access a source directory it shouldn't touch
ifneq (,$(LT_USER_PROJECT_ILLEGAL_FILES))
$(error You have illegal include paths. They are $(LT_USER_PROJECT_ILLEGAL_FILES))
endif

LT_PROJECT_SOURCE_FILES 	:= $(patsubst $(LT_PROJECT_SOURCE_DIR)/%,%,$(LT_PROJECT_SOURCE_FILES))

# make
include $(LT_PROJECT_RULES_MAKEFILE)


###############################################################################
#              LOG
###############################################################################
#   11-Jun-24   domitian    created
#   14-Jun-24   domitian    refactored /source/ includes rule checking to be a singluar shell command, enabling
#                           the ability to determine exactly which user files break the LT Library Rules.
#   21-Jun-24   domitian    pathing to subdirectories now supported
#   25-Jun-24   domitian    expanded and fixed /source/ rule checking, as logical failure within the code would mean that no
#                           .h files could be used if placed inside the /source/ folder regardless of if it would
#                           actually be legal under LT Library Rules, and a greater logical failure with its placement within the code
#   09-Jul-24   domitian    If LT_USER_ROOT is set from the make command goals, a directory without a /source/ can be called in this way.
#                           Formerly, attempting to build anything from the lt example folder would fail to build spectacularly because
#                           of conventions of directory naming in lt. Solution is setting LT_PROJECT_SOURCE_DIR equal to LT_USER_LIB_DIR.
#                           LT_USER_LIB_DIR is id'd by retreating one directory from /source, won't harm normal ways of configuring LT_USER_ROOT.


