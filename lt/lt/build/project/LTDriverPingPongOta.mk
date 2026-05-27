################################################################################################
# LTDriverPingPongOta.mk
#
# LTDriverPingPongOta.mk - project LTDriverPingPongOta
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR	     := $(LT_OS_ROOT)/source/lt/driver/pingpongota
LT_PROJECT_SOURCE_FILES      += LTDriverPingPongOta.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################################
#   LOG
################################################################################################
#   07-Aug-23   gallienus   created
