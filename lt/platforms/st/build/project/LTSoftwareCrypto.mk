################################################################################
# LTSoftwareCrypto.mk - project makefile for LT Library LTSoftwareCrypto
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# Refer to full software cryptos in lt/build/project/LTDriverCrypto.mk

LT_PROJECT_SOURCE_DIR        := $(LT_OS_ROOT)/source/lt/driver/crypto

LT_PROJECT_SOURCE_FILES      := LTDriverCrypto.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoBigNum.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSha1.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSha256.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqSha1.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqSha256.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoHmac.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoHmacSha256.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqHmacSha256.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoP256.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoEcdsaP256.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoProvisionedData.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureHmacSha256.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureEcdsaP256.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoKeyManager.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoCertManager.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   09-Feb-22   gallienus   created
