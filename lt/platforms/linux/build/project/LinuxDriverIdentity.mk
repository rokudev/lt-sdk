################################################################################
# LinuxDriverIdentity.mk - project makefile for the Linux platform's
#                                       LinuxDriverIdentity LT library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/linux/driver/identity
LT_PROJECT_SOURCE_FILES     := LinuxDriverIdentity.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   13-Apr-23   trajan      created
