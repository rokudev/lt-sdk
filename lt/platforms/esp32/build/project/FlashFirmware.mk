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

# Left unset, rit picks the reset sequence from the serial device name, reading
# any ttyACM as a native USB serial/JTAG peripheral. That is wrong on boards
# whose USB-to-UART bridge enumerates as a CDC device, so a variant can say which
# it has via LT_FLASH_NATIVE_USB.
ifeq (no,$(LT_FLASH_NATIVE_USB))
  LT_RESET_ARG := --nousbjtag
else ifeq (yes,$(LT_FLASH_NATIVE_USB))
  LT_RESET_ARG := --usbjtag
else
  LT_RESET_ARG :=
endif

# Left unset, rit auto-detects the part from its chip-ID register. That register
# is not readable on every part, so a variant whose chip cannot be detected names
# itself via LT_FLASH_CHIP.
ifneq (,$(LT_FLASH_CHIP))
  LT_CHIP_ARG := --chip $(LT_FLASH_CHIP)
else
  LT_CHIP_ARG :=
endif

LT_SMASH_ARG :=
ifeq (all, $(LT_FLASH))
  LT_SMASH_ARG := --smash
endif
ifeq (erase, $(LT_FLASH))
  LT_SMASH_ARG := --erase
endif
ifeq (info, $(LT_FLASH))
  LT_SMASH_ARG := --info
endif

ifeq (help,$(findstring help,$(MAKECMDGOALS)))

.PHONY: all
all:
	@echo "__________________"
	@echo "FlashFirmware HELP"
	@echo "---------------------------"
	@echo " use 'make FlashFirmware' - to flash build into firmware partition"
	@echo
	@echo " use 'LT_FLASH=all make FlashFirmware' for first time flash init (all partitions flashed)"
	@echo " use 'LT_NOSTUB=1  make FlashFirmware' for slower flashing on older ESP32 hardware"
	@echo "==========================="

else

all:
	$(LT_PLATFORM_ROOT)/build/image/esp32_flash_all.sh -D $(LT_FLASH_DEVICE) $(LT_NOSTUB_ARG) $(LT_RESET_ARG) $(LT_CHIP_ARG) $(LT_SMASH_ARG) -c $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/LTFlashConfig.json -r $(LT_TARGET_BIN_DIR) program

endif
