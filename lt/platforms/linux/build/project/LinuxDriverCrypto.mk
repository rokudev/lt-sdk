################################################################################
# LinuxDriverCrypto.mk - project makefile for Linux LT Library LinuxDriverCrypto
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR          :=    $(LT_PROJECT_SOURCE_DIR_BASE)/linux/driver/crypto

LT_PROJECT_SOURCE_FILES        :=    LinuxDriverCrypto.c
LT_PROJECT_SOURCE_FILES        +=    LinuxDriverCryptoEntropy.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   25-Jul-20   gallienus   created