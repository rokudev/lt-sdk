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


LT_STM32_FLASH_DEVICE       ?= SWD
LT_STM32_DEVICE_ARG         := -c port=$(LT_STM32_FLASH_DEVICE)
LT_STM32_CUBE_CLI_DIR       ?= /opt/st/stm32cubeclt_1.22.0
ifeq (SWD, $(LT_STM32_FLASH_DEVICE))
  LT_STM32_DEVICE_INDEX     ?= 0
  LT_STM32_DEVICE_INDEX_ARG := index=$(LT_STM32_DEVICE_INDEX)
  LT_STM32_DEVICE_MODE      ?= UR
  LT_STM32_DEVICE_MODE_ARG  := mode=$(LT_STM32_DEVICE_MODE)
else
  LT_STM32_DEVICE_INDEX_ARG :=
  LT_STM32_DEVICE_MODE_ARG  :=
endif

ifeq (erase, $(LT_FLASH))
  LT_FLASH_ARG := -e all
else
  ifeq (all, $(LT_FLASH))
    LT_FLASH_ARG := -d $(LT_TARGET_BIN_DIR)/firmware.elf -d $(LT_TARGET_BIN_DIR)/LTPartitionTable.bin 0x08080000 -d $(LT_TARGET_BIN_DIR)/LTPartitionTable.bin 0x080A0000 -s
  else
    LT_FLASH_ARG := -d $(LT_TARGET_BIN_DIR)/firmware.elf -s
  endif
endif

ifneq (, $(LT_STM32_FLASH_NOSUDO))
    LT_STM32_FLASH_SUDO_CMD :=
else
    LT_STM32_FLASH_SUDO_CMD ?= sudo
endif

LT_STM32_CUBE_CLI_CMD := $(LT_STM32_CUBE_CLI_DIR)/STM32CubeProgrammer/bin/STM32_Programmer_CLI

ifeq (help,$(findstring help,$(MAKECMDGOALS)))

.PHONY: all
all:
	@echo "__________________"
	@echo "FlashFirmware HELP"
	@echo "---------------------------"
	@echo " use 'make FlashFirmware' - to flash build into firmware partition"
	@echo
	@echo " use 'LT_FLASH=all   make FlashFirmware' for first time flash init (all partitions flashed)"
	@echo " use 'LT_FLASH=erase make FlashFirmware' to erase the entire flash"
	@echo
	@echo " Note: By default sudo is used to run the STM32_Programmer_CLI program.  To prevent this"
	@echo "       set LT_STM32_FLASH_NOSUDO=1 in your environment."
	@echo
	@echo " Other environment variables that can be set are:"
	@echo "         LT_STM32_FLASH_DEVICE=<device>                         (default = SWD)"
	@echo "         LT_STM32_DEVICE_INDEX=<index of SWD device>            (default = 0)"
	@echo "          LT_STM32_DEVICE_MODE=<device reset mode>              (default = UR)"
	@echo "         LT_STM32_CUBE_CLI_DIR=<stm32 client tools install dir> (default = /opt/st/stm32cubeclt_1.22.0)"
	@echo "       LT_STM32_FLASH_SUDO_CMD=<sudo command>                   (default = sudo)"
	@echo "==========================="

else

all:
	$(LT_STM32_FLASH_SUDO_CMD) $(LT_STM32_CUBE_CLI_CMD) $(LT_STM32_DEVICE_ARG) $(LT_STM32_DEVICE_INDEX_ARG) $(LT_STM32_DEVICE_MODE_ARG) $(LT_FLASH_ARG)

endif
