################################################################################
#
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
#
################################################################################


LT_FLASH_DEVICE ?= /dev/ttyUSB0

ifeq (1,$(LT_NOSTUB))
  LT_NOSTUB_ARG := --nostub
else
  LT_NOSTUB_ARG :=
endif

ifeq (all, $(LT_FLASH))
  LT_SMASH_ARG := --smash
else
  LT_SMASH_ARG :=
endif

all:
	$(LT_PLATFORM_ROOT)/build/image/esp32_flash_all.sh -D $(LT_FLASH_DEVICE) $(LT_NOSTUB_ARG) $(LT_SMASH_ARG) -c $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/LTFlashConfig.json -r $(LT_TARGET_BIN_DIR) program
