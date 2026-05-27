################################################################################
# LTDriverCrypto.mk - project makefile for LT Library LTDriverCrypto
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# This file includes all software cryptos, for other platforms to reference.
# Define LTSoftwareCrypto.mk in each platform's build/project directory to include the needed software cryptos.

LT_PROJECT_SOURCE_DIR        := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/driver/crypto

LT_PROJECT_SOURCE_FILES      := LTDriverCrypto.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoBigNum.c

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSha1.c              # require LTDriverCryptoBigNum.c,  used in Provision
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSha256.c            # require LTDriverCryptoBigNum.c,  always
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqSha1.c           # require LTDriverCryptoSha1.c,    used in SWUP
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqSha256.c         # require LTDriverCryptoSha256.c,  always

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoHmac.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoHmacSha1.c          # require LTDriverCryptoHmac.c and LTDriverCryptoSha1.c,    used in SRTP, STUN
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoHmacSha256.c        # require LTDriverCryptoHmac.c and LTDriverCryptoSha256.c,  always
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqHmacSha1.c       # require LTDriverCryptoHmac.c and LTDriverCryptoSha1.c,    none
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSeqHmacSha256.c     # require LTDriverCryptoHmac.c and LTDriverCryptoSha256.c,  used in BLE

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoDrbg.c              # require LTDriverCryptoBigNum.c and LTDriverCryptoSha256.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoRandom.c            # require LTDriverCryptoSha256.c and LTDriverCryptoDrbg.c,  always

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoAes128.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoAes128Cbc.c         # require LTDriverCryptoAes128.c,  used in AESKEY2
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoAes128Ctr.c         # require LTDriverCryptoAes128.c,  used in SRTP
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoAes128Gcm.c         # require LTDriverCryptoAes128.c,  always
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoAes128Xts.c         # require LTDriverCryptoAes128.c,  used in flash data

LT_PROJECT_SOURCE_FILES      += LTDriverCrypto25519.c             # require LTDriverCryptoBigNum.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSha512.c            # require LTDriverCryptoBigNum.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoEd25519.c           # require LTDriverCryptoBigNum.c, LTDriverCrypto25519.c and LTDriverCryptoSha512.c,   used in TLS if enabled
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoX25519.c            # require LTDriverCryptoBigNum.c and LTDriverCrypto25519.c,   used in TLS, Provision

LT_PROJECT_SOURCE_FILES      += LTDriverCryptoP256.c              # require LTDriverCryptoBigNum.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoEcdsaP256.c         # require LTDriverCryptoBigNum.c, LTDriverCryptoHmacSha256.c and LTDriverCryptoP256.c,  used in TLS

# Only if no hardware secure crypto
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoProvisionedData.c
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureSha256.c      # require LTDriverCryptoSha256.c, used in flash key
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureHmacSha256.c  # require LTDriverCryptoHmacSha256.c, used in BLE, Provision, SWUP, Bundle OTA
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureEcdsaP256.c   # used in TLS, DTLS,
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoSecureEd25519.c     # used in TLS if enabled

# Key manager and certificate manager
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoKeyManager.c        # setting-based key manager, may use secure key manager if available in hardware
LT_PROJECT_SOURCE_FILES      += LTDriverCryptoCertManager.c       # setting-based cert manager, may use secure cert manager if available in hardware

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   11-Jul-20   gallienus   created