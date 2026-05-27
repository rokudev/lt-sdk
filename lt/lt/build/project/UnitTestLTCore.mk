################################################################################
# UnitTestLTCore.mk - project makefile for LT Library UnitTestLTCore
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/unittest/lt/core
LT_PROJECT_SOURCE_FILES := UnitTestLTCoreImpl.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreAssertTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreConsoleTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreEventTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreHelpers.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreKernelTimeTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreMathTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreMemoryAllocationTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreMemoryTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreMutexTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreQsortTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreReleaseClientData.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreStringTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreStrtoTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreThreadTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreTimerTests.c
LT_PROJECT_SOURCE_FILES += UnitTestLTCoreCompatibility.cpp

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Jan-20   augustus    created
#   18-May-20   constantine convert LTUnitTestCore to C
#   20-Oct-21   constantine move to UnitTestLTCore
#   05-Aug-21   constantine add inclusion of LTCore source into UnitTestLTCore source
#   27-Oct-22   augustus    added UnitTestLTCoreReleaseClientData.c
#   18-Dec-22   augustus    removed abomination of adding LTCore source dir to -I directive
#   02-Feb-23   geta        add UnitTestLTCoreEventTests.c
