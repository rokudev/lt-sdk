################################################################################
# OpenSSLDriverCrypto.mk - project makefile for Linux LT Library OpenSSLDriverCrypto
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR          :=    $(LT_PROJECT_SOURCE_DIR_BASE)/linux/driver/crypto

LT_PROJECT_SOURCE_FILES        :=    OpenSSLDriverCrypto.c

# Download pre-built OpenSSL SDK, extract it and make it available for
# building this library:
ifeq (, $(LT_OPENSSL_SDK_URL))
$(error LT_OPENSSL_SDK_URL not set for this platform.)
endif

OPENSSL_SDK_DIR		:= $(abspath $(LT_TARGET_OBJ_DIR)/openssl-sdk)
OPENSSL_SDK_ARCHIVE	:= $(OPENSSL_SDK_DIR)/openssl-sdk.tar.bz2
OPENSSL_SDK_INC_DIR	:= $(OPENSSL_SDK_DIR)/include
OPENSSL_SDK_LIB_DIR	:= $(OPENSSL_SDK_DIR)/lib

OPENSSL_SDK_FILES	:= $(OPENSSL_SDK_INC_DIR)/openssl/evp.h
OPENSSL_SDK_FILES	:= $(OPENSSL_SDK_INC_DIR)/openssl/rand.h
OPENSSL_SDK_FILES	+= $(OPENSSL_SDK_LIB_DIR)/libcypto.so
OPENSSL_SDK_FILES	+= $(OPENSSL_SDK_LIB_DIR)/libcypto.so.3

$(OPENSSL_SDK_ARCHIVE):
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) echo "Downloading OpenSSL SDK from $(LT_OPENSSL_SDK_URL)"
	$(LT_EXEC_CMD) curl --fail --no-progress-meter -o $@ $(LT_OPENSSL_SDK_URL)

$(OPENSSL_SDK_FILES): $(OPENSSL_SDK_ARCHIVE)
	$(LT_EXEC_CMD) rm -rf $(OPENSSL_SDK_INC_DIR) $(OPENSSL_SDK_LIB_DIR)
	$(LT_EXEC_CMD) tar -C $(OPENSSL_SDK_DIR) -xf $(OPENSSL_SDK_ARCHIVE)

LT_PUBLIC_INCLUDE_FLAGS		+= -I$(OPENSSL_SDK_INC_DIR)
LT_LDFLAGS_SHAREDLIB		+= -L$(OPENSSL_SDK_LIB_DIR) -lcrypto

.PHONY: openssl-sdk
openssl-sdk: $(OPENSSL_SDK_FILES)

LT_PROJECT_PREBUILD_TARGETS	+= openssl-sdk

# make
include $(LT_PROJECT_RULES_MAKEFILE)
