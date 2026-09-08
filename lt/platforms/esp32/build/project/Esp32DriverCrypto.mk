################################################################################
# Esp32DriverCrypto.mk - project makefile for the ESP32 platform's
#                        ESP32_DriverCrypto LT Driver Library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# source dir and files
#
# The software crypto sits at the root of crypto/ and is shared with
# Esp32s3DriverCrypto; only the files that drive the AES/SHA/RSA peripherals are
# per chip, under esp32/.
LT_PROJECT_SOURCE_DIR        := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/crypto
LT_PROJECT_SOURCE_SUBDIRS    += esp32

LT_PROJECT_SOURCE_FILES      := Esp32DriverCrypto.c
LT_PROJECT_SOURCE_FILES      += Esp32DriverCryptoHmacSha256.c
LT_PROJECT_SOURCE_FILES      += Esp32DriverCrypto25519.c
LT_PROJECT_SOURCE_FILES      += Esp32DriverCryptoX25519.c
LT_PROJECT_SOURCE_FILES      += Esp32DriverCryptoBigNumSoft.c

LT_PROJECT_SOURCE_FILES      += esp32/Esp32DriverCryptoEngine.c
LT_PROJECT_SOURCE_FILES      += esp32/Esp32DriverCryptoRandom.c
LT_PROJECT_SOURCE_FILES      += esp32/Esp32DriverCryptoSha256.c
LT_PROJECT_SOURCE_FILES      += esp32/Esp32DriverCryptoBigNum.c
LT_PROJECT_SOURCE_FILES      += esp32/Esp32DriverCryptoAesGcm.c

# reach the shared headers at the root of crypto/ from the esp32/ sources
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PROJECT_SOURCE_DIR)
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(SOC_PLATFORM_NAME)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   26-May-22   gallienus   created
#   27-Aug-26   claudius    hardware files moved down into crypto/esp32, alongside
#                           the esp32s3 driver sharing the software crypto above
