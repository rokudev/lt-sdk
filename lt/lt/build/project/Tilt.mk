################################################################################
# Tilt.mk - project makefile for TILT
#    ______ ____ __   ______
#   /_  __//  _// /  /_  __/       TILT 2.0: the Test
#    / /   / / / /    / /                    Infrastructure
#   / /  _/ / / /___ / /                     for LT
#  /_/  /___//_____//_/
#
#  TILT Framework for the Unit Testing, Performance Testing and Stress Testing
#     of LT Objects.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/tilt
LT_PROJECT_SOURCE_FILES := Tilt.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

