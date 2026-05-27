################################################################################
# MacOSDriverCrypto.mk - project makefile for MacOS LT Library MacOSDriverCrypto
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR          :=    $(LT_PROJECT_SOURCE_DIR_BASE)/macos/driver/crypto

LT_PROJECT_SOURCE_FILES        :=    MacOSDriverCryptoImpl.c
LT_PROJECT_SOURCE_FILES        +=    MacOSDriverCryptoEntropy.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   25-Jul-22   gallienus   created
#   03-Jul-23   constantine copied from the Linux platform
