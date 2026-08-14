#####################################################################################################
# Esp32MasterFirmwareImage.mk - project makefile for mastering firmware image
#
#   - Link all required objects into executable files
#   - Generate map and symbol files.
#   - Generate scripts to produce the assembly listings later if needed.
#   - Generate scripts to convert addresses to source-code lines.
#   - Generate signatures.
#   - Combine signatures and firmware binaries into firmware images.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
#####################################################################################################

#####################################################################################################
# Final build products - the make process places these in the target bin directory.  The dependencies
# on these targets determine which of the other targets defined in this file get built.
#
ESP32_PART_TABLE             := $(LT_TARGET_BIN_DIR)/LTPartitionTable.bin
ESP32_FIRMWARE_IMAGE         := $(LT_TARGET_BIN_DIR)/LTFirmwareImage.bin
ESP32_FIRMWARE_UPDATE_DATA   := $(LT_TARGET_BIN_DIR)/LTSystemUpdateData.bin
ESP32_FIRMWARE_UPDATE_IMAGE  := $(LT_TARGET_BIN_DIR)/LTSystemUpdateImage.bin
ESP32_FIRMWARE_MFG_IMAGE     := $(LT_TARGET_BIN_DIR)/LTManufacturingImage.bin
ESP32_BOOTLOADER_IMAGE       := $(LT_TARGET_BIN_DIR)/LTBootloader.bin
ESP32_DEVICE_ID_IMAGE        := $(LT_TARGET_BIN_DIR)/LTDeviceIdentity.bin
ESP32_BIN_ADDR2LINE          := $(LT_TARGET_BIN_DIR)/addr2line
ESP32_FLASH_ALL              := $(LT_TARGET_BIN_DIR)/esp32_flash_all.sh

ESP32_FLASH_CONFIG_FILES     := $(LT_TARGET_BIN_DIR)/config/$(notdir $(LT_FLASHER_JSON_FILE)) $(LT_TARGET_BIN_DIR)/config/$(notdir $(LT_FLASH_CONFIG_JSON_FILE))

LT_PROJECT_POSTBUILD_TARGETS := FirmwareImages $(ESP32_PART_TABLE) HelperScripts $(ESP32_BIN_ADDR2LINE) $(ESP32_PART_TABLE) $(ESP32_BOOTLOADER_IMAGE) $(ESP32_DEVICE_ID_IMAGE) $(ESP32_FIRMWARE_UPDATE_DATA) $(ESP32_FLASH_ALL) $(ESP32_FLASH_CONFIG_FILES)

#####################################################################################################
# The lingua franca of the LT Project Make Process:
#
include $(LT_PROJECT_RULES_MAKEFILE)

#####################################################################################################
# Some paths used throughout the project:
#
ESP32_MASTERING_PATH     := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/mastering
ESP32_CONFIG_PATH        := $(ESP32_MASTERING_PATH)/config
ESP32_CHIP_CONFIG_PATH   := $(ESP32_CONFIG_PATH)/$(SOC_PLATFORM_NAME)
ESP32_LD_SCRIPT_PATH     := $(ESP32_MASTERING_PATH)/ld/$(SOC_PLATFORM_NAME)
ESP32_LD_ROM_SCRIPT_PATH := $(ESP32_LD_SCRIPT_PATH)/rom
ESP32_LIB_PATH           := $(ESP32_MASTERING_PATH)/lib/$(SOC_PLATFORM_NAME)

ifneq (,$(LT_CRYPTO_KEY_DIR))
  ifneq (REMOTE,$(LT_CRYPTO_KEY_DIR))
	ESP32_RFEK := $(LT_CRYPTO_KEY_DIR)/root_firmware_encryption_key.bin
  else
    ESP32_RFEK := $(ESP32_CONFIG_PATH)/root_firmware_encryption_key.bin
  endif
else
	ESP32_RFEK := $(ESP32_CONFIG_PATH)/root_firmware_encryption_key.bin
endif

#####################################################################################################
# esp32 image configuration:
#
# ESP32_IMAGE_FLASH_SIZE is stamped into the image header for the ROM loader to
# sanity check the part it is running on against.  A board property rather than a
# chip one, so a variant with a larger device says so in its own Makefile.config -
# which is read well before this file, and therefore wins over the default here.
ESP32_IMAGE_FLASH_SIZE ?= 4MB

ESP32_ELF_BASENAME := $(LT_PROJECT_OBJ_DIR)/LTFirmwareImage
ESP32_ELF          := $(ESP32_ELF_BASENAME).elf
ESP32_ELF_STRIPPED := $(ESP32_ELF_BASENAME).stripped.elf
ESP32_MAP          := $(ESP32_ELF_BASENAME).map
ESP32_SYM          := $(ESP32_ELF_BASENAME).sym
ESP32_ASM          := $(ESP32_ELF_BASENAME).asm
ESP32_MAKE_ASM     := $(LT_PROJECT_OBJ_DIR)/make_assembly_listing
ESP32_ADDR2LINE    := $(LT_PROJECT_OBJ_DIR)/addr2line

ESP32_IMAGE        := $(LT_PROJECT_OBJ_DIR)/LTFirmwareImage.bin
ESP32_SBKEY_PEM    := secure_boot_key_application.pem

# Where rib looks for the partition payloads named by LTFlashConfig.json.  The bin
# directory leads, so anything this build produced wins over anything checked in.
# The per chip config directory is only listed when it exists, because rib stats
# every element of the path and treats a missing one as fatal.
ESP32_IMAGE_SEARCH_PATH := $(LT_TARGET_BIN_DIR)
ESP32_IMAGE_SEARCH_PATH := $(ESP32_IMAGE_SEARCH_PATH)$(if $(wildcard $(ESP32_CHIP_CONFIG_PATH)),:$(ESP32_CHIP_CONFIG_PATH))
ESP32_IMAGE_SEARCH_PATH := $(ESP32_IMAGE_SEARCH_PATH):$(ESP32_CONFIG_PATH):$(LT_TARGET_BIN_DIR)

ESP32_IMAGE_LIBRARIES := $(shell \
	  LT_PLATFORM_ROOT=$(LT_PLATFORM_ROOT) LT_PLATFORM=$(LT_PLATFORM) \
	   LT_PRODUCT_ROOT=$(LT_PRODUCT_ROOT)   LT_PRODUCT=$(LT_PRODUCT) \
	    LT_TARGET_ROOT=$(LT_TARGET_ROOT) LT_BUILD_MODE=$(LT_BUILD_MODE) \
	     MAKECMDGOALS=listlibraries $(MAKE) --no-print-directory listlibraries | grep -v LTBootloader)
ESP32_IMAGE_LIBRARIES_L := $(foreach ltlibrary, $(foreach ltlibrary, $(ESP32_IMAGE_LIBRARIES), $(basename $(notdir $(ltlibrary)))), -l$(ltlibrary:lib%=%))

ESP32_ELF_DEPENDENCIES := $(ESP32_LD_SCRIPT_PATH)/*
ESP32_ELF_DEPENDENCIES += $(ESP32_LD_ROM_SCRIPT_PATH)/*
ESP32_ELF_DEPENDENCIES += $(ESP32_IMAGE_LIBRARIES)

############################################################################################################
# Linker arguments:

# we could add -Wl,--size-opt for even more space optimization in exchange for a performance
# hit of an unknown scale
ESP32_LD_ARG += -Wl,--relax

ESP32_LD_ARG += -L $(ESP32_LD_SCRIPT_PATH)
ESP32_LD_ARG += -T memory.ld
ESP32_LD_ARG += -T sections.ld

# Peripheral linker scripts
ESP32_LD_ARG += -T $(SOC_PLATFORM_NAME).peripherals.ld

# ROM linker scripts.  Which scripts a ROM offers is not just a matter of name -
# substituting $(SOC_PLATFORM_NAME) into one part's list would not work for the
# other - so there is one list per chip.
ifeq ($(SOC_PLATFORM_NAME),esp32s3)
  # The esp32s3 has neither eco3, syscalls nor redefined, and offers a single
  # newlib.ld plus newlib-nano.ld where the esp32 splits newlib three ways.
  ESP32_LD_ROM_SCRIPTS := rom.ld rom.api.ld rom.libgcc.ld rom.newlib.ld \
                          rom.newlib-nano.ld rom.newlib-time.ld rom.version.ld
else
  # rom.redefined.ld supplies ets_timers, needed by wpa_supplicant (WiFi component).
  ESP32_LD_ROM_SCRIPTS := rom.ld rom.api.ld rom.eco3.ld rom.libgcc.ld            \
                          rom.newlib-data.ld rom.syscalls.ld rom.newlib-funcs.ld \
                          rom.newlib-time.ld rom.redefined.ld
endif

ESP32_LD_ARG += -L $(ESP32_LD_ROM_SCRIPT_PATH)
ESP32_LD_ARG += $(foreach ldscript,$(ESP32_LD_ROM_SCRIPTS),-T $(SOC_PLATFORM_NAME).$(ldscript))

# For ROM patch
ESP32_LD_ARG += -Wl,-wrap,longjmp

# Force linker to include these symbols
ESP32_LD_ARG += -u ld_include_highint_hdl
ESP32_LD_ARG += -u applicationDescriptor

ESP32_LD_ARG += -Wl,--cref
ESP32_LD_ARG += -Wl,-Map=$(ESP32_MAP)
ESP32_LD_ARG += -Wl,--gc-sections

# Prevent libc memory allocation from being used by WiFi binaries
ESP32_LD_ARG += -ffreestanding
ESP32_LD_ARG += -nostartfiles
ESP32_LD_ARG += -nodefaultlibs
ESP32_LD_ARG += -nostdlib
# Wrappers for WiFi binaries use
ESP32_LD_ARG += -Wl,-wrap,malloc -Wl,-wrap,realloc -Wl,-wrap,free -Wl,-wrap,calloc -Wl,-wrap,gettimeofday -Wl,-wrap,puts -Wl,-wrap,sprintf
ESP32_LD_ARG += -Wl,-wrap,intr_matrix_set
ESP32_LD_ARG += -specs nosys.specs
ESP32_LD_ARG += -fno-rtti
ESP32_LD_ARG += -fno-lto

ESP32_LD_ARG += $(ESP32_OBJS)
ESP32_LD_ARG += $(ESP32_IMG3_CMSE_IMPLIB)
ESP32_LD_ARG += -L $(LT_TARGET_LIB_DIR)
ESP32_LD_ARG += -Wl,--whole-archive
ESP32_LD_ARG += $(ESP32_IMAGE_LIBRARIES_L)
ESP32_LD_ARG += -Wl,--no-whole-archive

# Prebuilt Espressif WiFi, PHY and BT blobs.
#
# Whether the image links them at all is the platform variant's call, made through
# LT_PLATFORM_HAS_WIRELESS_BLOBS in its Makefile.config.  That is not the same
# question as whether the chip has a radio: nothing in LT reaches these blobs
# except Esp32DriverWiFi and Esp32DriverBleController, so a variant that has
# neither driver has nothing to link them for, and dragging in a megabyte of
# unreferenced archives to be garbage collected only slows the link down.
ifeq (yes,$(LT_PLATFORM_HAS_WIRELESS_BLOBS))

    # The blob set is per chip in membership as well as in content: the esp32 has the
    # BT baseband in ROM and so wants no libbtbb.a, and the esp32s3 has its RTC
    # support in ROM and so wants no librtc.a.
    ifeq ($(SOC_PLATFORM_NAME),esp32s3)
        ESP32_WIFI_LIBS := libnet80211.a libcore.a libpp.a libsmartconfig.a libespnow.a \
                           libphy.a libwpa_supplicant.a
        ESP32_BT_LIBS   := libbtdm_app.a libbtbb.a
    else
        ESP32_WIFI_LIBS := libnet80211.a libcore.a libpp.a libsmartconfig.a libespnow.a \
                           libphy.a librtc.a libwpa_supplicant.a
        ESP32_BT_LIBS   := libbtdm_app.a
    endif

    # libwpa_supplicant.a is not an Espressif binary drop - IDF ships wpa_supplicant as
    # source and this one was built by Roku, so a fresh vendoring drop does not supply
    # it.  Check for it rather than let the link fail with a wall of undefined symbols.
    ESP32_MISSING_LIBS := $(strip $(foreach ltlib,$(ESP32_WIFI_LIBS) $(ESP32_BT_LIBS) libcoexist.a, \
                                      $(if $(wildcard $(ESP32_LIB_PATH)/$(ltlib)),,$(ltlib))))
    ifneq (,$(ESP32_MISSING_LIBS))
        $(error Missing prebuilt blob(s) in $(ESP32_LIB_PATH): $(ESP32_MISSING_LIBS))
    endif

    # WiFi
    ESP32_LD_ARG += $(foreach ltlib,$(ESP32_WIFI_LIBS),$(ESP32_LIB_PATH)/$(ltlib))
    # BT coexist
    ESP32_LD_ARG += $(ESP32_LIB_PATH)/libcoexist.a

else ifneq (no,$(LT_PLATFORM_HAS_WIRELESS_BLOBS))
    $(error $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Makefile.config FAILED to properly \
            specify yes or no for the variable LT_PLATFORM_HAS_WIRELESS_BLOBS)
else
    # A radio driver in the image with no blobs behind it links to a wall of
    # undefined symbols in libnet80211 and libpp, which says nothing about the
    # actual mistake.  Name it here instead.
    ESP32_RADIO_LIBRARIES := $(filter %DriverWiFi.a %DriverBleController.a,$(notdir $(ESP32_IMAGE_LIBRARIES)))
    ifneq (,$(ESP32_RADIO_LIBRARIES))
        $(error $(ESP32_RADIO_LIBRARIES) is in the image, but LT_PLATFORM_HAS_WIRELESS_BLOBS \
                is no for LT_PLATFORM=$(LT_PLATFORM) so the Espressif blobs it calls into are \
                not linked - either drop the driver from LTDeviceConfig.json or populate \
                $(ESP32_LIB_PATH) and turn the flag on)
    endif
endif

# Not part of the blob set above: -nodefaultlibs means the compiler's own support
# routines have to be asked for by name, whether or not there is a radio.
ESP32_LD_ARG += -lgcc

ifeq (yes,$(LT_PLATFORM_HAS_WIRELESS_BLOBS))
    # BT.  After -lgcc, as it was when this was one unconditional block.
    ESP32_LD_ARG += $(foreach ltlib,$(ESP32_BT_LIBS),$(ESP32_LIB_PATH)/$(ltlib))
endif

############################################################################################################
# Tools used in the generation of images:
#
IMAGE_BUILDER := $(LT_TARGET_DIR_BUILDTOOLS)/bin/rib

ifneq (,$(IDF_PYTHON_ENV_PATH))
    ESP_PYTHON := $(IDF_PYTHON_ENV_PATH)/bin/python
    $(info Using IDF Python interpreter: $(ESP_PYTHON))
else
    ESP_PYTHON := $(shell { command -v python3 || command -v python; } 2>/dev/null)
    $(info Using built-in Python interpreter: $(ESP_PYTHON))
endif
ESP_ESPTOOL_PY := $(ESP32_MASTERING_PATH)/image/esptool.py

############################################################################################################
# Make python not splat intermediate files on the disk
ESP_PYTHON := PYTHONDONTWRITEBYTECODE=splatmenot $(ESP_PYTHON)

############################################################################################################
# Build the esp32 executable:
#
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
# Build the firmware images:
#
.PHONY: FirmwareImages
FirmwareImages: $(ESP32_FIRMWARE_IMAGE) $(ESP32_PART_TABLE) $(ESP32_BOOTLOADER_IMAGE)
	$(LT_QUIET_CMD) @echo Build manufacturing image
	$(LT_EXEC_CMD) $(IMAGE_BUILDER) -c $(LT_FLASH_CONFIG_JSON_FILE) -p $(ESP32_IMAGE_SEARCH_PATH) -o $(ESP32_FIRMWARE_MFG_IMAGE) build
	$(LT_QUIET_CMD) @echo Build firmware update image
	$(LT_EXEC_CMD) $(IMAGE_BUILDER) -c $(LT_FLASH_CONFIG_JSON_FILE) -f $(ESP32_FIRMWARE_IMAGE) -o $(ESP32_FIRMWARE_UPDATE_IMAGE) -k $(ESP32_RFEK) -v $(LT_SOFTWARE_VERSION) update

$(ESP32_FIRMWARE_IMAGE): $(ESP32_ELF)
	$(LT_QUIET_CMD) @echo Convert ELF to bin
	$(LT_EXEC_CMD) $(ESP_PYTHON) $(ESP_ESPTOOL_PY) --chip $(SOC_PLATFORM_NAME) elf2image --flash_mode dio --flash_freq 80m --flash_size $(ESP32_IMAGE_FLASH_SIZE) --elf-sha256-offset 0xb0 -o $(ESP32_FIRMWARE_IMAGE).tmp $(ESP32_ELF)
ifeq (REMOTE, $(LT_CRYPTO_KEY_DIR))
	$(LT_QUIET_CMD) @echo Signing image using remote server
  ifeq (, $(LT_PLATFORM_ID))
	$(error "LT_PLATFORM_ID must be specified for build)
  endif
	$(LT_QUIET_CMD) @echo let bess sign master fw
	$(LT_EXEC_CMD) mv $(ESP32_FIRMWARE_IMAGE).tmp $(ESP32_FIRMWARE_IMAGE)
else ifneq (, $(LT_CRYPTO_KEY_DIR))
	$(LT_QUIET_CMD) @echo Signing image locally
	$(LT_EXEC_CMD) $(ESP_PYTHON) $(ESP_ESPSECURE_PY) sign_data --keyfile $(LT_CRYPTO_KEY_DIR)/$(ESP32_SBKEY_PEM) -v 2 -o $(ESP32_FIRMWARE_IMAGE) $(ESP32_FIRMWARE_IMAGE).tmp
else
	$(LT_QUIET_CMD) @echo Signing image disabled
	$(LT_EXEC_CMD) mv $(ESP32_FIRMWARE_IMAGE).tmp $(ESP32_FIRMWARE_IMAGE)
endif

$(ESP32_PART_TABLE): $(LT_FLASH_CONFIG_JSON_FILE)
	$(LT_QUIET_CMD) @echo Create partition table
	$(LT_EXEC_CMD) $(IMAGE_BUILDER) -c $(LT_FLASH_CONFIG_JSON_FILE) -o $(ESP32_PART_TABLE) partition

$(LT_TARGET_BIN_DIR)/%.bin: $(ESP32_CONFIG_PATH)/%.bin
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) cp -p $^ $@

############################################################################################################
# Where the bootloader in the image comes from.
#
# Not from this project: the bootloader is a build of its own, `make bootloaders`,
# because it is signed and released on a cadence of its own, so what the mastering
# step wants from it is a binary rather than a link.  A released one checked in
# beside the other image inputs is copied in; otherwise it has to be built.
#
# The prebuilt is keyed by chip - config/$(SOC_PLATFORM_NAME)/LTBootloader.bin - and
# not merely by name, so that the pattern rule above, which copies anything named
# *.bin out of config/, cannot supply a bootloader by accident: putting the wrong
# part's second stage in an image gives a device that does not come back, and the
# two parts do not even load the second stage from the same flash offset, byte
# 0x1000 against byte 0.  A chip with none checked in gets the explicit rule below
# instead, which asks for `make bootloaders` by name.  An explicit rule beats the
# pattern rule above, so this settles it either way.
ifneq (,$(wildcard $(ESP32_CHIP_CONFIG_PATH)/LTBootloader.bin))

$(ESP32_BOOTLOADER_IMAGE): $(ESP32_CHIP_CONFIG_PATH)/LTBootloader.bin
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) cp -p $< $@

else

# No prerequisites, so make runs this only when the file is genuinely absent - a
# bootloader already left in the bin directory by `make bootloaders` is taken as is.
$(ESP32_BOOTLOADER_IMAGE):
	@echo "$@ is missing, and no released $(SOC_PLATFORM_NAME) bootloader is checked in at $(ESP32_CHIP_CONFIG_PATH)/LTBootloader.bin - run 'make bootloaders' to build one" >&2
	@false

endif

$(LT_TARGET_BIN_DIR)/config/$(notdir $(LT_FLASHER_JSON_FILE)): $(LT_FLASHER_JSON_FILE)
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) cp -p $^ $@

$(LT_TARGET_BIN_DIR)/config/$(notdir $(LT_FLASH_CONFIG_JSON_FILE)): $(LT_FLASH_CONFIG_JSON_FILE)
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) cp -p $^ $@

$(ESP32_FLASH_ALL): $(LT_PLATFORM_ROOT)/build/image/esp32_flash_all.sh
	$(LT_EXEC_CMD) mkdir -p $(dir $@)
	$(LT_EXEC_CMD) cp -p $^ $@

###############################################################################
#   LOG
###############################################################################
#   22-Mar-22   tiberius    Created
#   29-Jul-26   claudius    made the radio blobs and the prebuilt bootloader
#                           conditional so a no-radio image can master
#   04-Aug-26   claudius    moved the esp32s3 out to the esp32s3 platform root
#   13-Aug-26   claudius    took the esp32s3 back in - the ROM linker scripts and
#                           the radio blob set now key off $(SOC_PLATFORM_NAME),
#                           and the mastered flash size off $(ESP32_IMAGE_FLASH_SIZE)
