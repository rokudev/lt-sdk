################################################################################
# ThirdPartyOpus.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB	:= no
LT_PROJECT_BUILD_STATIC_LIB	:= yes
LT_PROJECT_BUILD_EXECUTABLE	:= no

# set location of where opus is submoduled in our repo
LT_PROJECT_SOURCE_DIR   := $(LT_OS_ROOT)/thirdparty/opus
LT_THIRDPARTY_OPUS_CONFIG_PREFIX := build
LT_THIRDPARTY_OPUS_OBJ_DIR	 := $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)
LT_THIRDPARTY_OPUS_BUILD_DIR := $(LT_THIRDPARTY_OPUS_OBJ_DIR)/$(LT_THIRDPARTY_OPUS_CONFIG_PREFIX)
LT_THIRDPARTY_OPUS_STATIC_LIB_PATH := $(LT_THIRDPARTY_OPUS_BUILD_DIR)/lib/libopus.a

include $(LT_PROJECT_SOURCE_DIR)/opus_sources.mk
include $(LT_PROJECT_SOURCE_DIR)/silk_sources.mk
include $(LT_PROJECT_SOURCE_DIR)/celt_sources.mk

export PATH := $(PATH):$(LT_GCC_TOOLCHAIN_PATH)
export CC := $(LT_GCC_TOOLCHAIN_PREFIX)-gcc

LIBOPUS_SOURCE_FILES := $(addprefix $(LT_PROJECT_SOURCE_DIR)/,$(OPUS_SOURCES)) 
LIBOPUS_SOURCE_FILES += $(addprefix $(LT_PROJECT_SOURCE_DIR)/,$(SILK_SOURCES))
LIBOPUS_SOURCE_FILES += $(addprefix $(LT_PROJECT_SOURCE_DIR)/,$(SILK_SOURCES_FIXED))
LIBOPUS_SOURCE_FILES += $(addprefix $(LT_PROJECT_SOURCE_DIR)/,$(CELT_SOURCES))
LIBOPUS_SOURCE_FILES += $(LT_PROJECT_SOURCE_DIR)/include/*.h
LIBOPUS_SOURCE_FILES += $(LT_PROJECT_SOURCE_DIR)/celt/*.h
LIBOPUS_SOURCE_FILES += $(LT_PROJECT_SOURCE_DIR)/silk/*.h
LIBOPUS_SOURCE_FILES += $(LT_PROJECT_SOURCE_DIR)/silk/fixed/*.h

.PHONY: libopus

libopus: $(LT_THIRDPARTY_OPUS_STATIC_LIB_PATH)

$(LT_THIRDPARTY_OPUS_STATIC_LIB_PATH): $(LIBOPUS_SOURCE_FILES)
	@echo "Building Opus"
	cd $(LT_THIRDPARTY_OPUS_OBJ_DIR) && \
	$(LT_PROJECT_SOURCE_DIR)/configure --prefix=/ --host $(LT_GCC_TOOLCHAIN_PREFIX) && \
	make -C $(LT_THIRDPARTY_OPUS_OBJ_DIR) && \
	make -C $(LT_THIRDPARTY_OPUS_OBJ_DIR) DESTDIR=$(LT_THIRDPARTY_OPUS_BUILD_DIR) install
	@echo "Opus built"


LT_PROJECT_PREBUILD_TARGETS		+= libopus
LT_PROJECT_PREBUILT_OBJ_LIBS	+= $(LT_THIRDPARTY_OPUS_STATIC_LIB_PATH)

include $(LT_PROJECT_RULES_MAKEFILE)
