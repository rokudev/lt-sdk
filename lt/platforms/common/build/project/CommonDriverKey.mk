################################################################################
# CommonDriverKey.mk - project makefile for platform dependent LT Library CommonDriverKey
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_ROOTS_BASE)/platforms/common/source/common/driver/key
LT_PROJECT_SOURCE_FILES	:= CommonDriverKey.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   25-Oct-24   gallienus   created
