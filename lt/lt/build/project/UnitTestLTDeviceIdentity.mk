################################################################################
#
#   This LT Library illustrates the use of the TILT Framework.
#   To implement a Unit Test LT Library using the Framework, list the source
#   code of your test functions, and TiltImpl.c, in LT_PROJECT_SOURCE_FILES.
#   TiltImpl.c adds the standard interface machinery which makes the Library
#   a Unit Test LT Library.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR   := $(LT_PROJECT_SOURCE_DIR_BASE)/unittest/lt/device/identity
LT_PROJECT_SOURCE_FILES := UnitTestLTDeviceIdentity.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   06-Mar-23   commodus    created
