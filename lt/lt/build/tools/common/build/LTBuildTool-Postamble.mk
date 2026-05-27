################################################################################
# LTBuildTool-Postamble.mk - Part 3 of LT Master Make's native build tool system
#                           (part 2 is each tool's LTBuildTool.mk file).
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_TOOL_OBJ_SUBDIRS   := $(foreach subdir, $(LT_TOOL_SOURCE_SUBDIRS), $(LT_TARGET_TOOLSDIR_OBJ)/$(subdir))
LT_TOOL_CPP_OBJ_FILES := $(patsubst %.cpp, $(LT_TARGET_TOOLSDIR_OBJ)/%.o, $(filter %.cpp, $(LT_TOOL_SOURCE_FILES)))
LT_TOOL_C_OBJ_FILES   := $(patsubst %.c, $(LT_TARGET_TOOLSDIR_OBJ)/%.o, $(filter %.c, $(LT_TOOL_SOURCE_FILES)))
LT_TOOL_OBJ_FILES     := $(LT_TOOL_CPP_OBJ_FILES) $(LT_TOOL_C_OBJ_FILES)
LT_TOOL_DEP_FILES     := $(LT_TOOL_CPP_OBJ_FILES:%.o=%.d) $(LT_TOOL_C_OBJ_FILES:%.o=%.d)

LT_TOOL_TARGETS       := $(LT_TARGET_TOOLSDIR_BIN)
LT_TOOL_TARGETS       += $(LT_TARGET_TOOLSDIR_LIB)
LT_TOOL_TARGETS       += $(LT_TARGET_TOOLSDIR_OBJ)
LT_TOOL_TARGETS       += $(LT_TOOL_OBJ_SUBDIRS)

ifeq (, $(LT_TOOL_LT_LIBRARIES))
  LT_TOOL_BUILT_LT_LIBRARIES :=
  LT_TOOL_EXE_LDFLAGS_LT_LIBRARIES :=
else
# LT_TOOL_BUILD_SQUELCH              :=
  LT_TOOL_BUILD_SQUELCH              := -s
  LT_TOOL_LT_CORE_LIBRARIES          := LTCore
  LT_TOOL_LT_SUPPORT_LIBRARIES       := $(LT_TOOL_LT_LIBRARIES)
  LT_TOOL_LT_LIBRARIES               := $(LT_TOOL_LT_CORE_LIBRARIES) $(LT_TOOL_LT_SUPPORT_LIBRARIES)
ifeq (Darwin, $(LT_TOOL_HOST_OS))
  LT_TOOL_LTLIBRARY_PLATFORM_FAMILY  := apple
else
  LT_TOOL_LTLIBRARY_PLATFORM_FAMILY  := linux
endif
  LT_TOOL_LTLIBRARY_PLATFORM_VARIANT := tools
  LT_TOOL_LTLIBRARY_PLATFORM_ROOT    := $(LT_ROOTS_BASE)/platforms/$(LT_TOOL_LTLIBRARY_PLATFORM_FAMILY)
  LT_TOOL_LTLIBRARY_PLATFORM         := $(LT_TOOL_LTLIBRARY_PLATFORM_VARIANT)
  LT_TOOL_PREBUILD_TARGETS           := lt_tool_build_lt_libraries $(LT_TOOL_PREBUILD_TARGETS)
  LT_TOOL_POSTBUILD_TARGETS          := $(LT_TOOL_POSTBUILD_TARGETS) lt_tool_build_complete
  LT_TOOL_LTLIBRARY_LIBDIR           := $(LT_TARGET_DIR_BUILDTOOLS)/targets/lt/$(LT_TOOL_LTLIBRARY_PLATFORM_FAMILY).$(LT_TOOL_LTLIBRARY_PLATFORM_VARIANT)/$(LT_TOOL_BUILD_MODE)/lib
  LT_TOOL_BUILT_LT_LIBRARIES         := $(patsubst %, $(LT_TOOL_LTLIBRARY_LIBDIR)/lib%.a, $(LT_TOOL_LT_LIBRARIES))
ifeq (Darwin, $(LT_TOOL_HOST_OS))
  LT_TOOL_BUILT_LT_CORE_LIBRARIES    := $(patsubst %, -force_load $(LT_TOOL_LTLIBRARY_LIBDIR)/lib%.a, $(LT_TOOL_LT_CORE_LIBRARIES))
  LT_TOOL_BUILT_LT_SUPPORT_LIBRARIES := $(patsubst %, -force_load $(LT_TOOL_LTLIBRARY_LIBDIR)/lib%.a, $(LT_TOOL_LT_SUPPORT_LIBRARIES))
  LT_TOOL_BUILT_LT_LIBRARY_LOADS     := $(LT_TOOL_BUILT_LT_CORE_LIBRARIES) $(LT_TOOL_BUILT_LT_SUPPORT_LIBRARIES)
else
  LT_TOOL_BUILT_LT_LIBRARY_LOADS     := -Wl,--whole-archive $(LT_TOOL_BUILT_LT_LIBRARIES) -Wl,--no-whole-archive
endif
  LT_TOOL_EXE_LDFLAGS_LT_LIBRARIES   := -pthread $(LT_TOOL_BUILT_LT_LIBRARY_LOADS)
  LT_TOOL_BUILD_LT_LIBRARIES         := @(env -i PATH="$(PATH)" LD_LIBRARY_PATH="$(LD_LIBRARY_PATH)" \
                                           LT_BANNER=no                                              \
                                           LT_QUIET=$(LT_QUIET)                                      \
                                           LT_CURRENTLY_BUILDING_TOOL=$(LT_CURRENTLY_BUILDING_TOOL)  \
                                           LT_TARGET_ROOT=$(LT_TARGET_DIR_BUILDTOOLS)                \
                                           LT_PLATFORM_ROOT=$(LT_TOOL_LTLIBRARY_PLATFORM_ROOT)       \
                                           LT_PLATFORM=$(LT_TOOL_LTLIBRARY_PLATFORM)                 \
                                           LT_BUILD_MODE=$(LT_TOOL_BUILD_MODE)                       \
                                           LT_GCC_TOOLCHAIN_PREFIX=$(LT_TOOL_CROSS_PREFIX:-=)        \
                                           LT_DISABLE_STATS=yes                                      \
                                           $(MAKE) --silent --no-print-directory -C $(LT_OS_ROOT)/build $(LT_TOOL_LT_LIBRARIES))
endif

#  | sed 's/^/    /'

LT_TOOL_TARGETS                      += $(LT_TOOL_PREBUILD_TARGETS) $(LT_TOOL_TARGET) $(LT_TOOL_POSTBUILD_TARGETS)
LT_TOOL_TARGETS_CLEAN                := $(LT_TARGET_TOOLSDIR_OBJ) $(LT_TOOL_TARGET)

.PHONY: all
all: $(LT_TOOL_TARGETS)

.PHONY: clean
clean:
	rm -rf $(LT_TOOL_TARGETS_CLEAN)

$(LT_TARGET_TOOLSDIR_BIN) $(LT_TARGET_TOOLSDIR_LIB) $(LT_TARGET_TOOLSDIR_OBJ):
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_ROOT)/targets/%,%,$@)
	$(LT_EXEC_CMD)  mkdir -p $@

$(foreach subdir, $(LT_TOOL_SOURCE_SUBDIRS), $(LT_TARGET_TOOLSDIR_OBJ)/$(subdir)):
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_ROOT)/targets/%,%,$@)
	$(LT_EXEC_CMD)  mkdir -p $@

-include $(LT_TOOL_DEP_FILES)

$(LT_TOOL_CPP_OBJ_FILES): $(LT_TARGET_TOOLSDIR_OBJ)/%.o : $(LT_TOOL_SOURCEDIR)/%.cpp
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_TOOL_CXX) $(LT_TOOL_CXXFLAGS) -c $< -o $@

$(LT_TOOL_C_OBJ_FILES): $(LT_TARGET_TOOLSDIR_OBJ)/%.o : $(LT_TOOL_SOURCEDIR)/%.c
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_TOOL_CC) $(LT_TOOL_CFLAGS) -c $< -o $@

$(LT_TOOL_TARGET): $(LT_TOOL_OBJ_FILES) $(LT_TOOL_BUILT_LT_LIBRARIES)
	$(LT_QUIET_CMD) @echo LD-EXE $(@F)
	$(LT_EXEC_CMD)  $(LT_TOOL_LD) -o $@ $(LT_TOOL_OBJ_FILES) $(LT_TOOL_LDFLAGS) $(LT_TOOL_EXE_LDFLAGS_LT_LIBRARIES)
ifneq (debug, $(LT_TOOL_BUILD_MODE))
	$(LT_QUIET_CMD) @echo STRIP $(@F)
	$(LT_EXEC_CMD)  $(LT_TOOL_STRIP) $(LT_TOOL_STRIPFLAGS) $@
endif

ifneq (, $(LT_TOOL_LT_LIBRARIES))
.PHONY: lt_tool_build_lt_libraries
lt_tool_build_lt_libraries:
	@echo "Making LT static link libs for $(LT_CURRENTLY_BUILDING_TOOL):"
	$(LT_EXEC_CMD) $(LT_TOOL_BUILD_LT_LIBRARIES)

.PHONY: lt_tool_build_complete
lt_tool_build_complete:
	@echo "make: BUILDTOOL $(LT_CURRENTLY_BUILDING_TOOL) complete"
endif

###############################################################################
#   LOG
###############################################################################
#   28-Jan-23   augustus    created

