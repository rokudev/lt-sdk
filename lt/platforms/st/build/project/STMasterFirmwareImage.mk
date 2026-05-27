################################################################################
# STMasterFirmwareImage.mk - project makefile for mastering st firmware image
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_POSTBUILD_TARGETS ?= firmware_image

include $(LT_PROJECT_RULES_MAKEFILE)

############################################################################################################
# Linker script path
ST_LD_SCRIPT_PATH := $(ST_VENDOR_SDK_ROOT)

############################################################################################################
# The LT_PLATFORM_DRIVER_PROJECTS will all need LTDevice libraries.
# Generate the list of LTDevice libraries:
#
ST_LT_LIBRARIES := $(shell \
	  LT_PLATFORM_ROOT=$(LT_PLATFORM_ROOT) LT_PLATFORM=$(LT_PLATFORM) \
	   LT_PRODUCT_ROOT=$(LT_PRODUCT_ROOT)   LT_PRODUCT=$(LT_PRODUCT) \
	    LT_TARGET_ROOT=$(LT_TARGET_ROOT) LT_BUILD_MODE=$(LT_BUILD_MODE) \
	     MAKECMDGOALS=listlibraries $(MAKE) --no-print-directory listlibraries | grep -v LTBootloader)
ST_LT_LIBRARIES_  := $(foreach ltlibrary, $(ST_LT_LIBRARIES),$(basename $(notdir $(ltlibrary))))
ST_LT_LIBRARIES_L := $(foreach ltlibrary, $(ST_LT_LIBRARIES_),-l$(ltlibrary:lib%=%))

ST_ELF_DEPENDENCIES += $(ST_LT_LIBRARIES)

ST_ELF_BASENAME := $(LT_TARGET_BIN_DIR)/firmware
ST_ELF          := $(ST_ELF_BASENAME).elf
ST_MAP          := $(ST_ELF_BASENAME).map
ST_SYM          := $(ST_ELF_BASENAME).sym
ST_ASM          := $(ST_ELF_BASENAME).asm
ST_MAKE_ASM     := $(LT_TARGET_BIN_DIR)/make_assembly_listing
ST_ADDR2LINE    := $(LT_TARGET_BIN_DIR)/addr2line

LT_LDFLAGS_MASTERING += --specs=nosys.specs
LT_LDFLAGS_MASTERING += -Wl,--build-id=none
LT_LDFLAGS_MASTERING += -Wl,--use-blx

LT_LDFLAGS_MASTERING += -L $(LT_TARGET_LIB_DIR)
# Interesting, groups help with circular symbol resolution, so does whole-archive
#LT_LDFLAGS_MASTERING += -Wl,--start-group
#LT_LDFLAGS_MASTERING += -lLTCore
#LT_LDFLAGS_MASTERING += -lSTLTCoreBSP
#LT_LDFLAGS_MASTERING += -Wl,--end-group
LT_LDFLAGS_MASTERING += -Wl,--whole-archive
# NOTE: STLTCoreBSP *REQUIRES* --whole-archive (or --start-group-ing?) since it contains both weak and strong symbol bindings !
LT_LDFLAGS_MASTERING += $(ST_LT_LIBRARIES_L)
LT_LDFLAGS_MASTERING += -Wl,--no-whole-archive
LT_LDFLAGS_MASTERING += -lm
LT_LDFLAGS_MASTERING += -Wl,-Map=$(ST_MAP)
LT_LDFLAGS_MASTERING += -Wl,--cref
LT_LDFLAGS_MASTERING += -Wl,--gc-sections
LT_LDFLAGS_MASTERING += -o $(ST_ELF)

############################################################################################################
# Build the Vendor SDK
$(ST_SDK):
	$(MAKE) -C $(ST_VENDOR_SDK_ROOT)

############################################################################################################
# Link the image
#
$(ST_ELF): $(ST_SDK) $(ST_ELF_DEPENDENCIES)
	$(LT_QUIET_CMD)	@echo LINK $(ST_ELF)
	$(LT_EXEC_CMD)  $(LT_CC) $(LT_LDFLAGS_MASTERING)
	$(LT_QUIET_CMD) @echo GENERATE symbol list
	$(LT_EXEC_CMD)  $(LT_NM) $(ST_ELF) | sort > $(ST_SYM)
	$(LT_QUIET_CMD) @echo WRITE assembly-listing-generation script
	$(LT_EXEC_CMD)  @echo "$(LT_OBJDUMP) --disassemble-all --source $(ST_ELF)  > $(ST_ASM)" > $(ST_MAKE_ASM)
	$(LT_EXEC_CMD)  @chmod +x $(ST_MAKE_ASM)
	$(LT_QUIET_CMD) @echo WRITE addr2line utility script
	$(LT_EXEC_CMD)  @echo "$(LT_ADDR2LINE) --addresses --pretty-print --inlines --functions --exe=$(ST_ELF)" '$$*' > $(ST_ADDR2LINE)
	$(LT_EXEC_CMD)  @chmod +x $(ST_ADDR2LINE)

.PHONY: firmware_image
firmware_image: $(ST_ELF)

###############################################################################
#   LOG
###############################################################################
#   19-Jan-21   tiberius    created
