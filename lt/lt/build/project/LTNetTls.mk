################################################################################
# LTNetTls.mk - project makefile for LT Library LTNetTls
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR        := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/tls
LT_PROJECT_SOURCE_FILES      := LTNetTls.c
LT_PROJECT_SOURCE_FILES      += LTNetTls13.c

# Only enable the debug macro for debug. Don't enable in any release build.
# Macro to dump TLS data to console for devices
# LT_CFLAGS_GENERIC += -DTLSKEY=0
# Macro to dump TLS data to a file in Linux
# LT_CFLAGS_GENERIC += -DTLSKEY=1

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   31-May-22   gallienus   created
