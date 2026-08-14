#####################################################################################################
# Esp32_LTBootloader.mk - project makefile for flash bootloader
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
#####################################################################################################

# Build a static library
LT_PROJECT_BUILD_SHARED_LIB := no
LT_PROJECT_BUILD_STATIC_LIB := yes
LT_PROJECT_BUILD_EXECUTABLE := no

#####################################################################################################
# Final build products - the make process places these in the target bin directory.  The dependencies
# on these targets determine which of the other targets defined in this file get built.
#
ESP32_BOOTLOADER_IMAGE       := $(LT_TARGET_BIN_DIR)/LTBootloader.bin
ESP32_BIN_ADDR2LINE          := $(LT_TARGET_BIN_DIR)/addr2line_LTBootloader
LT_PROJECT_POSTBUILD_TARGETS := BootImage HelperScripts $(ESP32_BIN_ADDR2LINE)

#####################################################################################################
# source dir and files

LTBOOTLOADER_ARCH          := arch1.1
LT_PROJECT_SOURCE_DIR      := $(LT_ROOTS_BASE)
LT_PROJECT_COMMON_SUBDIR   := lt/source/ltbootloader/$(LTBOOTLOADER_ARCH)
LT_PROJECT_PLATFORM_SUBDIR := platforms/esp32/source/esp32/ltbootloader
LT_PROJECT_SOURCE_SUBDIRS  += $(LT_PROJECT_COMMON_SUBDIR) $(LT_PROJECT_PLATFORM_SUBDIR) $(LT_PROJECT_PLATFORM_SUBDIR)/$(SOC_PLATFORM_NAME)

LT_PROJECT_SOURCE_FILES    := $(LT_PROJECT_COMMON_SUBDIR)/LTBoot.c

LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/$(SOC_PLATFORM_NAME)/LTBootDriver.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_start.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_init.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_mem.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_utility.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_clock_init.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_clock_loader.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_common.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_common_loader.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_console.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_console_loader.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_random.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/bootloader_flash.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/flash_encrypt.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/secure_boot.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/secure_boot_signatures_bootloader.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/cpu_util.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/mpu_hal.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_image_format.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_efuse_api.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_efuse_utility.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_rom_crc.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_rom_longjmp.S
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_rom_sys.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/esp_rom_uart.c
LT_PROJECT_SOURCE_FILES    += $(LT_PROJECT_PLATFORM_SUBDIR)/wdt_hal_iram.c
# Chip specific bootloader sources, taken from the $(SOC_PLATFORM_NAME)
# subdirectory.  That subdirectory is load bearing rather than tidiness: IDF has
# an esp_efuse_utility.c in both the shared efuse component and the per target
# one, and this library compiles both, so the two cannot share a directory.
#
# Several more are named for the part itself (bootloader_esp32.c,
# bootloader_efuse_esp32s3.c), so the list cannot be had by substituting
# $(SOC_PLATFORM_NAME) into a generic set of names - hence one list per chip.
#
# bootloader_sha.c is per chip upstream too, and used to sit in the shared list
# above.  The esp32 drives the SHA accelerator through the legacy SHA_256_*
# register set and the later parts have a unified DMA-capable block with an
# entirely different register layout, so it moved down here with the rest.
ifeq ($(SOC_PLATFORM_NAME),esp32s3)
  # No spi_flash_rom_patch.c - the esp32s3 ROM needs no such patching.  No
  # regi2c_ctrl.c either: that source only exists to wrap the analog-bus ROM calls
  # in a lock, and regi2c_ctrl.h maps the wrappers straight onto the ROM entries
  # under BOOTLOADER_BUILD, which this project defines.
  #
  # esp_rom_cache.c and its helper .S are errata workarounds: the esp32s3 ROM cache
  # suspend and freeze entries do not wait for the cache to go idle, and
  # Cache_WriteBack_Addr mishandles an unaligned range.  esp32s3.rom.ld renames the
  # affected ROM entries to rom_* so these wrappers can take their names, which is
  # why the link needs them.
  #
  # On where these came from: the shared sources above are forked from a v4.4
  # pre-release, IDF commit 4c0cf40a of June 2021.  That vintage does carry esp32s3
  # support, but it predates the production part, so the files here are taken from
  # v4.4.8 wherever the newer code matters for real silicon - rtc_init.c reading the
  # per part LDO calibration fuses, the esp_efuse_table.c that names them, the
  # esp_rom_cache.c errata wrappers - and from the fork point wherever the file
  # talks to shared code whose API moved afterwards, namely esp_efuse_utility.c and
  # bootloader_efuse_esp32s3.c.  Mixing that way is safe because the two versions
  # differ in software layering only; the register and fuse bit layouts they
  # describe are silicon, and did not change between them.
  ESP32_BOOTLOADER_SOC_SOURCES := bootloader_esp32s3.c bootloader_flash_config_esp32s3.c  \
                                  flash_encryption_secure_features.c                      \
                                  esp_rom_cache.c esp_rom_cache_writeback_esp32s3.S       \
                                  rtc_clk_init.c rtc_clk.c rtc_init.c rtc_time.c          \
                                  bootloader_efuse_esp32s3.c secure_boot_secure_features.c\
                                  esp_efuse_utility.c esp_efuse_api_key_esp32xx.c         \
                                  esp_efuse_table.c bootloader_random_esp32s3.c           \
                                  bootloader_soc.c bootloader_sha.c
else
  ESP32_BOOTLOADER_SOC_SOURCES := bootloader_esp32.c bootloader_flash_config_esp32.c       \
                                  flash_encryption_secure_features.c spi_flash_rom_patch.c \
                                  rtc_clk_init.c rtc_clk.c rtc_init.c rtc_time.c           \
                                  bootloader_efuse_esp32.c secure_boot_secure_features.c   \
                                  esp_efuse_utility.c esp_efuse_api_key_esp32.c            \
                                  esp_efuse_table.c bootloader_random_esp32.c              \
                                  bootloader_soc.c bootloader_sha.c
endif

LT_PROJECT_SOURCE_FILES    += $(foreach ltsource,$(ESP32_BOOTLOADER_SOC_SOURCES), \
                                  $(LT_PROJECT_PLATFORM_SUBDIR)/$(SOC_PLATFORM_NAME)/$(ltsource))

LT_CFLAGS_GENERIC          += -DESP_PLATFORM -DBOOTLOADER_BUILD=1
ifneq (, $(LT_PLATFORM_ID))
    LT_CFLAGS_GENERIC          += -DLT_PLATFORM_ID=PLID_$(LT_PLATFORM_ID)
endif
LT_CFLAGS_GENERIC          += -I$(LT_PROJECT_SOURCE_DIR)/lt/include/ltbootloader/$(LTBOOTLOADER_ARCH)
LT_CFLAGS_GENERIC          += -I$(LT_PROJECT_SOURCE_DIR)/$(LT_PROJECT_PLATFORM_SUBDIR)/include
LT_CFLAGS_GENERIC          += -I$(LT_PROJECT_SOURCE_DIR)/$(LT_PROJECT_PLATFORM_SUBDIR)/include/$(SOC_PLATFORM_NAME)
LT_CFLAGS_GENERIC          += -fstrict-volatile-bitfields

# These flags make the resultant binary smaller
LT_CFLAGS_GENERIC          += -freorder-blocks
LT_CFLAGS_GENERIC          += -fno-tree-switch-conversion
LT_CFLAGS_GENERIC          += -fno-stack-protector

#####################################################################################################
# The lingua franca of the LT Project Make Process:
#
include $(LT_PROJECT_RULES_MAKEFILE)

#####################################################################################################
# Some paths used throughout the project:
#
ESP32_MASTERING_PATH     := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/mastering
ESP32_LD_SCRIPT_PATH     := $(LT_PROJECT_SOURCE_DIR)/$(LT_PROJECT_PLATFORM_SUBDIR)/ld/$(SOC_PLATFORM_NAME)
ESP32_LD_ROM_SCRIPT_PATH := $(ESP32_LD_SCRIPT_PATH)/rom

#####################################################################################################
# esp32 image configuration:
#
ESP32_VERSION      := 1.3

# Stamped into the image header for the ROM loader to sanity check the part it is
# running on against.  A board property rather than a chip one, so a variant with
# a larger device says so in its own Makefile.config - which is read well before
# this file, and therefore wins over the default here.
ESP32_IMAGE_FLASH_SIZE ?= 4MB

# Stamped into byte 14 of the image header, where the first stage ROM loader reads
# it and refuses to start a second stage that asks for a newer part than it is
# running on.
ifeq ($(SOC_PLATFORM_NAME),esp32s3)
  # The esp32s3 has never shipped above rev 1, and asking for anything higher makes
  # the ROM reject the bootloader on every part in existence - the watchdog the ROM
  # armed then reboots the board every nine seconds, forever, with nothing on the
  # console to say why.  Leave this at 0.
  ESP32_IMAGE_MIN_CHIP_REV := 0
else
  # The floor is deliberate: LT targets rev 3 silicon and will not run on the
  # earlier errata.
  ESP32_IMAGE_MIN_CHIP_REV := 3
endif

ESP32_ELF_BASENAME := $(LT_PROJECT_OBJ_DIR)/LTBootloader
ESP32_ELF          := $(ESP32_ELF_BASENAME).elf

ESP32_ELF_STRIPPED := $(ESP32_ELF_BASENAME).stripped.elf
ESP32_MAP          := $(ESP32_ELF_BASENAME).map
ESP32_SYM          := $(ESP32_ELF_BASENAME).sym
ESP32_ASM          := $(ESP32_ELF_BASENAME).asm
ESP32_MAKE_ASM     := $(LT_PROJECT_OBJ_DIR)/make_assembly_listing
ESP32_ADDR2LINE    := $(LT_PROJECT_OBJ_DIR)/addr2line
ESP32_IMAGE        := $(LT_PROJECT_BIN_DIR)/LTBootloader.bin
ESP32_SBKEY_PEM    := secure_boot_key_bootloader.pem

ESP32_IMAGE_LIBRARIES := $(LT_TARGET_LIB_DIR)/libEsp32_LTBootloader.a
ESP32_IMAGE_LIBRARIES_L := $(foreach ltlibrary, $(foreach ltlibrary, $(ESP32_IMAGE_LIBRARIES), $(basename $(notdir $(ltlibrary)))), -l$(ltlibrary:lib%=%))

ESP32_ELF_DEPENDENCIES := $(ESP32_LD_SCRIPT_PATH)/*
ESP32_ELF_DEPENDENCIES += $(ESP32_LD_ROM_SCRIPT_PATH)/*
ESP32_ELF_DEPENDENCIES += $(ESP32_IMAGE_LIBRARIES)

############################################################################################################
# Linker arguments:

ESP32_LD_ARG := -mlongcalls
ESP32_LD_ARG += -fno-lto

# Linker script path
ESP32_LD_ARG += -L $(ESP32_LD_SCRIPT_PATH)

# Application linker scripts
ESP32_LD_ARG += -T bootloader.ld
ESP32_LD_ARG += -T bootloader.rom.ld

# Peripheral linker scripts
ESP32_LD_ARG += -T $(SOC_PLATFORM_NAME).peripherals.ld

# ROM linker scripts.  The bootloader takes a smaller set than the application
# does - see Esp32_MasterFirmwareImage.mk.
ifeq ($(SOC_PLATFORM_NAME),esp32s3)
  # Which the esp32s3 ROM offers is not a matter of name: it has no eco3 script and
  # offers a single newlib.ld where the esp32 splits newlib into -data/-funcs/-time.
  ESP32_LD_ROM_SCRIPTS := rom.ld rom.api.ld rom.libgcc.ld rom.newlib.ld rom.version.ld
else
  ESP32_LD_ROM_SCRIPTS := rom.ld rom.api.ld rom.libgcc.ld rom.newlib-funcs.ld rom.eco3.ld
endif

ESP32_LD_ARG += -L $(ESP32_LD_ROM_SCRIPT_PATH)
ESP32_LD_ARG += $(foreach ldscript,$(ESP32_LD_ROM_SCRIPTS),-T $(SOC_PLATFORM_NAME).$(ldscript))

# For ROM patch
ESP32_LD_ARG += -Wl,-wrap,longjmp

ESP32_LD_ARG += -Wl,--cref
ESP32_LD_ARG += -Wl,-Map=$(ESP32_MAP)
ESP32_LD_ARG += -Wl,--gc-sections

# Libraries
ESP32_LD_ARG += -L $(LT_TARGET_LIB_DIR)
ESP32_LD_ARG += $(ESP32_IMAGE_LIBRARIES_L)

############################################################################################################
# Tools used in the generation of images:
#
IMAGE_BUILDER    := $(LT_TARGET_DIR_BUILDTOOLS)/bin/rib

ifneq (,$(IDF_PYTHON_ENV_PATH))
    ESP_PYTHON         := $(IDF_PYTHON_ENV_PATH)/bin/python
    $(info Using IDF Python interpreter: $(ESP_PYTHON))
else
    ESP_PYTHON         := $(shell { command -v python3 || command -v python; } 2>/dev/null)
    $(info Using built-in Python interpreter: $(ESP_PYTHON))
endif
# Do not write intermediate python files in current folder
ESP_PYTHON       := PYTHONDONTWRITEBYTECODE=splatmenot $(ESP_PYTHON)

ESP_ESPTOOL_PY   := $(ESP32_MASTERING_PATH)/image/esptool.py

############################################################################################################
# Build the esp32 executable:

$(ESP32_ELF): $(ESP32_ELF_DEPENDENCIES)
	$(LT_QUIET_CMD)	@echo LINK the $(SOC_PLATFORM_NAME) executable
	$(LT_EXEC_CMD)  $(LT_CC) $(ESP32_LD_ARG) -o $@

$(ESP32_SYM): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo GENERATE $(SOC_PLATFORM_NAME) symbol list
	$(LT_EXEC_CMD)  $(LT_NM) $(ESP32_ELF) | sort > $@

$(ESP32_ELF_STRIPPED): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo GENERATE the stripped $(SOC_PLATFORM_NAME) executable
	$(LT_EXEC_CMD)  cp $(ESP32_ELF) $@
	$(LT_EXEC_CMD)  $(LT_STRIP) $@

############################################################################################################
# Write the esp32 helper scripts:
#
$(ESP32_MAKE_ASM): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo WRITE $(SOC_PLATFORM_NAME) assembly-listing-generation script
	$(LT_EXEC_CMD)  echo "$(LT_OBJDUMP) --disassemble-all --source $(ESP32_ELF)  > $(ESP32_ASM)" > $@
	$(LT_EXEC_CMD)  chmod +x $@

$(ESP32_ADDR2LINE): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo WRITE $(SOC_PLATFORM_NAME) addr2line utility script
	$(LT_EXEC_CMD)  echo "$(LT_ADDR2LINE) --addresses --pretty-print --inlines --functions --exe=$(ESP32_ELF)" '$$*' > $@
	$(LT_EXEC_CMD)  chmod +x $@

.PHONY: HelperScripts
HelperScripts: $(ESP32_ADDR2LINE) $(ESP32_MAKE_ASM)

$(ESP32_BIN_ADDR2LINE): $(ESP32_ADDR2LINE)
	$(LT_QUIET_CMD) @echo COPY $(SOC_PLATFORM_NAME) addr2line to bin directory
	$(LT_EXEC_CMD)  cp $< $@

############################################################################################################
# Build the bootloader image:
#
.PHONY: BootImage
BootImage: $(ESP32_BOOTLOADER_IMAGE)
	$(LT_EXEC_CMD) $(IMAGE_BUILDER) -f $(ESP32_BOOTLOADER_IMAGE) -a boot check

$(ESP32_BOOTLOADER_IMAGE): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo Convert ELF to bin
	$(LT_EXEC_CMD) $(ESP_PYTHON) $(ESP_ESPTOOL_PY) --chip $(SOC_PLATFORM_NAME) elf2image --flash_mode dio --flash_freq 80m --flash_size $(ESP32_IMAGE_FLASH_SIZE) --min-rev $(ESP32_IMAGE_MIN_CHIP_REV) --build-version $(ESP32_VERSION) -o $(ESP32_BOOTLOADER_IMAGE).tmp $(ESP32_ELF)
ifeq (REMOTE, $(LT_CRYPTO_KEY_DIR))
	$(LT_QUIET_CMD) @echo Signing image using remote server
  ifeq (, $(LT_PLATFORM_ID))
	$(error "LT_PLATFORM_ID must be specified for build)
  endif
	$(LT_QUIET_CMD) @echo let bess sign bootloader
	$(LT_EXEC_CMD) mv $(ESP32_BOOTLOADER_IMAGE).tmp $(ESP32_BOOTLOADER_IMAGE)
else ifneq (, $(LT_CRYPTO_KEY_DIR))
	$(error "Local signage is not supported for this build")
else
	$(LT_QUIET_CMD) @echo Image signing disabled
	$(LT_EXEC_CMD) mv $(ESP32_BOOTLOADER_IMAGE).tmp $(ESP32_BOOTLOADER_IMAGE)
endif

###############################################################################
#   LOG
###############################################################################
#   06-Feb-23   tiberius    Created
#   04-Aug-26   claudius    moved the esp32s3 out to the esp32s3 platform root
#   13-Aug-26   claudius    took the esp32s3 back in - the chip source list, the
#                           ROM linker scripts and the minimum chip revision now
#                           key off $(SOC_PLATFORM_NAME), and the mastered flash
#                           size off $(ESP32_IMAGE_FLASH_SIZE)
