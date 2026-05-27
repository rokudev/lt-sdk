################################################################################
# Rules.mk - LT Master Makefile rules
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

ifneq ($(filter $(LT_PROJECT),$(LT_DYNAMIC_LIBRARIES_LIST)),)
LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER := yes
LT_PROJECT_BUILD_SHARED_LIB := yes
#NOTE: PLT cannot be generated for armv8(gcc limitation) and we absolutely don't want PLT and extra dependencies for the library
#      other than LT_GetCore() and LT_GetStdlib(), which are passed to LTLibrary_xxx_OpenLibrary()
#
# No use for R9 so far, to complicated to set and reset GOT ptr, and also need to compile the entire LT OS with R9 crap
#LT_CFLAGS_GENERIC       += -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative
LT_CFLAGS_GENERIC       :=	$(subst -DLT_NO_DYNAMIC_LOADER, -DLT_SHARED_LIB=1, $(LT_CFLAGS_GENERIC))
# Relative GOT base, everything else though got
LT_CFLAGS_GENERIC       += -fPIC -mno-pic-data-is-text-relative -mno-single-pic-base
LT_LD_FRONTEND	 := $(LT_CC)
# Have to wrap memset to prevent external memset symbol(and PLT), GCC adds memset() calls for certrain struct initializers
LT_LDFLAGS_SHAREDLIB := $(filter -march=%,$(LT_CFLAGS_GENERIC)) -nostdlib -ffreestanding -nostartfiles -nodefaultlibs -Wl,--wrap=memset -Wl,-z,max-page-size=16 -Wl,-static  -lgcc -shared
LT_DYNLIB_STUBS	:= $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)/LTSharedLibStubs.o
LT_PROJECT_PREBUILD_TARGETS  += $(LT_DYNLIB_STUBS)
LT_PROJECT_EXTRA_OBJ_FILES := $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)/LTSharedLibStubs.o

$(LT_DYNLIB_STUBS) :
	$(LT_QUIET_CMD) @echo MAKE $(@F)
	$(LT_EXEC_CMD)  rm -f $(LT_PROJECT_OBJ_DIR)/LTSharedLibStubs.c
	$(LT_EXEC_CMD)  echo "#include <lt/LT.h>" > $(LT_PROJECT_OBJ_DIR)/LTSharedLibStubs.c
	$(LT_EXEC_CMD)  echo "void __wrap_memset(void * pDest, int c, LT_SIZE nCount) { lt_memset(pDest, c, nCount); }" >> $(LT_PROJECT_OBJ_DIR)/LTSharedLibStubs.c
	$(LT_EXEC_CMD)  $(LT_CC) $(LT_CFLAGS) -c $(LT_PROJECT_OBJ_DIR)/LTSharedLibStubs.c -o $(LT_PROJECT_OBJ_DIR)/LTSharedLibStubs.o
endif

# find out if this library has a Resource.json file to ARBOLATE!
# 1. Special case LTDeviceConfig and LTProductConfig to always use
#     $(LT_PLATFORM_ROOT)/build/platform/$(LT_PLATFORM)/build/LTDeviceConfig.json - and -
#     $(LT_PRODUCT_ROOT)/build/product/$(LT_PRODUCT)/build/LTProductConfig.json
#    as the names for those libraries' Resource.json files respectively.
# 2. Check if the user has expliclity set LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE in their project .mk file.  If so
#    use it; otherwise:
# 3. Look for the library's auto-arbolation ResourceTree.json in its library project source directory, which is:
#    $(LT_PROJECT_SOURCE_DIR)/resources/ResourceTree.json
ifeq ($(LT_PROJECT), LTDeviceConfig)
    LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE := $(LT_DEVICE_CONFIG_JSON_FILE)
else
    ifeq ($(LT_PROJECT), LTProductConfig)
	    LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE := $(LT_PRODUCT_CONFIG_JSON_FILE)
    else
        ifeq (,$(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE))
	        ifeq (yes, $(shell if [ -f $(LT_PROJECT_SOURCE_DIR)/resources/ResourceTree.json ]; then echo yes; else echo no; fi;))
	            LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE := $(LT_PROJECT_SOURCE_DIR)/resources/ResourceTree.json
	        endif
        endif
    endif
endif

# after all that, if we have an arbolated tree source json file,  prepare Arbolation variables for Arbolation rules
ifneq (,$(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE))
    # YES, the library is calling for generation of library specific custom resource tree ARBOLATION!
    # first make sure we can find the .c file that defines LTLibrary root interface
    ifeq (,$(LT_PROJECT_SOURCE_FILES))
        # no source files, probably the mastering project
        LT_PROJECT_ROOT_IMPL_FILE :=
    else
        # simple case, look for define_LTLIBRARY_ROOT_INTERFACE|LTDEVICE_LIBRARY_ROOT_INTERFACE|define_LTLIBRARY_APPLICATION(<libname>
        LT_GREPEX := "define_(\|LTLIBRARY_ROOT_INTERFACE\|LTDEVICE_LIBRARY_ROOT_INTERFACE\|define_LTLIBRARY_APPLICATION)[[:space:]]*([[:space:]]*$(LT_PROJECT)"
        LT_PROJECT_ROOT_IMPL_FILE := $(shell cd $(LT_PROJECT_SOURCE_DIR) && grep -l $(LT_GREPEX) $(LT_PROJECT_SOURCE_FILES))
        ifeq (,$(LT_PROJECT_ROOT_IMPL_FILE))
            # didn't find it as regular, app or device lib;  Look for define_LTDEVICE_DRIVER_IMPLEMENTATION(<devicename>, <driverlibname>)
            LT_GREPEX := "define_LTDEVICE_DRIVER_IMPLEMENTATION[[:space:]]*([[:space:]]*LTDevice[[:alnum:]]\{3,\}[[:space:]]*,[[:space:]]*${LT_PROJECT}[[:space:]]*)"
            LT_PROJECT_ROOT_IMPL_FILE := $(shell cd $(LT_PROJECT_SOURCE_DIR) && grep -l $(LT_GREPEX) $(LT_PROJECT_SOURCE_FILES))
        endif
    endif
    #
    # ok, check to see if we found the .c file with the library root interface in it
    ifeq (,$(LT_PROJECT_ROOT_IMPL_FILE))
        $(info ERROR: $(LT_PROJECT) is calling for Resource Tree ARBOLATION but contains no LTLibrary Root interface)
        $(info $(LT_) $(LT_) $(LT_) $(LT_) $(LT_) $(LT_) $(LT_) To correct, remove LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE from $(LT_PROJECT).mk)
        $(error I shall now)
    endif
    #
    #  take the root impl source file out of the project source files list
    LT_PROJECT_SOURCE_FILES := $(patsubst $(LT_PROJECT_ROOT_IMPL_FILE),,$(LT_PROJECT_SOURCE_FILES))
    # setup the last few variables we'll need for our ARBOLATION RULES!
    LT_ARBOLATED_RESOURCE_TREE_INCLUDE_DIR  := $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)/arbolated/include
    LT_ARBOLATED_RESOURCE_TREE_INCLUDE_FILE := $(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_DIR)/$(LT_PROJECT)ResourceTree.h
    LT_ARBOLATED_RESOURCE_TREE_JSON_SOURCE_DIR := $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)/arbolated/json-source
    LT_CFLAGS_INCLUDE_ARBOLATED_TREE := -include $(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_FILE)
else
    LT_PROJECT_ROOT_IMPL_OBJ_FILE :=
endif
    #end of arbolation computation, arbolated targets below

# compute build targets
ifeq (,$(LT_PROJECT_ARTIFACT))
    LT_PROJECT_ARTIFACT := $(patsubst thirdparty-lib%,%,$(LT_PROJECT))
    LT_PROJECT_ARTIFACT := $(patsubst thirdparty-%,%,$(LT_PROJECT_ARTIFACT))
endif

LT_STATIC_LIB_TARGET_NAME 	:= $(LT_TARGET_LIB_DIR)/lib$(LT_PROJECT_ARTIFACT).a
LT_SHARED_LIB_TARGET_NAME 	:= $(LT_TARGET_BIN_DIR)/lib$(LT_PROJECT_ARTIFACT).so
LT_EXECUTABLE_TARGET_NAME 	:= $(LT_TARGET_BIN_DIR)/$(LT_PROJECT_ARTIFACT)
LT_PROJECT_OBJ_DIR 	  		:= $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)
LT_PROJECT_OBJ_SUBDIRS		:= $(foreach subdir, $(LT_PROJECT_SOURCE_SUBDIRS), $(LT_PROJECT_OBJ_DIR)/$(subdir))
LT_PROJECT_CPP_OBJ_FILES	:= $(patsubst %.cpp, $(LT_PROJECT_OBJ_DIR)/%.o, $(filter %.cpp, $(LT_PROJECT_SOURCE_FILES)))
LT_PROJECT_C_OBJ_FILES		:= $(patsubst %.c, $(LT_PROJECT_OBJ_DIR)/%.o, $(filter %.c, $(LT_PROJECT_SOURCE_FILES)))
LT_PROJECT_M_OBJ_FILES      := $(patsubst %.m, $(LT_PROJECT_OBJ_DIR)/%.o, $(filter %.m, $(LT_PROJECT_SOURCE_FILES)))
LT_PROJECT_S_OBJ_FILES		:= $(patsubst %.s, $(LT_PROJECT_OBJ_DIR)/%.o, $(filter %.s, $(LT_PROJECT_SOURCE_FILES)))
LT_PROJECT_S2_OBJ_FILES		:= $(patsubst %.S, $(LT_PROJECT_OBJ_DIR)/%.o, $(filter %.S, $(LT_PROJECT_SOURCE_FILES)))
LT_PROJECT_DEP_FILES        := \
			$(LT_PROJECT_CPP_OBJ_FILES:%.o=%.d) \
			$(LT_PROJECT_C_OBJ_FILES:%.o=%.d) \
            $(LT_PROJECT_M_OBJ_FILES:%.o=%.d) \
			$(LT_PROJECT_S_OBJ_FILES:%.o=%.d) \
			$(LT_PROJECT_S2_OBJ_FILES:%.o=%.d)


LT_PROJECT_ALL_OBJ_FILES_SANS_ASM   := $(LT_PROJECT_C_OBJ_FILES) $(LT_PROJECT_CPP_OBJ_FILES) $(LT_PROJECT_M_OBJ_FILES) $(LT_PROJECT_EXTRA_OBJ_FILES)
LT_PROJECT_ALL_OBJ_FILES            := $(LT_PROJECT_ALL_OBJ_FILES_SANS_ASM) $(LT_PROJECT_S_OBJ_FILES) $(LT_PROJECT_S2_OBJ_FILES)

# calculate and add the PROJECT_ROOT_IMPL_OBJ to the LT_PROJECT_DEP_FILES and to the sans asm obj files after they have already been added to all obj
# so that root gets picked up for stack sizing but not for building with the regular c obj target rule
ifneq (,$(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE))
    LT_PROJECT_ROOT_IMPL_OBJ_FILE := $(patsubst %.c, $(LT_PROJECT_OBJ_DIR)/%.o, $(LT_PROJECT_ROOT_IMPL_FILE))
    LT_PROJECT_DEP_FILES += $(LT_PROJECT_ROOT_IMPL_OBJ_FILE:%.o=%.d)
    LT_PROJECT_ALL_OBJ_FILES_SANS_ASM += $(LT_PROJECT_ROOT_IMPL_OBJ_FILE)
endif

LT_PROJECT_TARGETS :=
ifeq (yes, $(LT_PROJECT_BUILD_EXECUTABLE))
	LT_PROJECT_TARGETS += $(LT_EXECUTABLE_TARGET_NAME)
    LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS :=
else
    ifeq (yes, $(LT_PROJECT_BUILD_STATIC_LIB))
        LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS :=
    else
        LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS := -DCURRENTLY_BUILDING_LTLIBRARY="$(LT_PROJECT)"
    endif
endif
ifeq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
    ifeq (yes, $(LT_PROJECT_BUILD_SHARED_LIB))
    	LT_PROJECT_TARGETS += $(LT_SHARED_LIB_TARGET_NAME)
    endif
    ifeq (yes, $(LT_PROJECT_BUILD_STATIC_LIB))
    	LT_PROJECT_TARGETS += $(LT_STATIC_LIB_TARGET_NAME)
    endif
else
    # if we don't have a dynamic loader, no shared lib, force static
    ifeq (yes, $(LT_PROJECT_BUILD_SHARED_LIB))
    	LT_PROJECT_TARGETS += $(LT_STATIC_LIB_TARGET_NAME)
    else
        ifeq (yes, $(LT_PROJECT_BUILD_STATIC_LIB))
    	    LT_PROJECT_TARGETS += $(LT_STATIC_LIB_TARGET_NAME)
        endif
    endif
endif
LT_PROJECT_STATS_STACKUSAGE_TARGET_NAME	:= $(LT_TARGET_STATS_DIR)/$(LT_PROJECT).stackusage.txt
LT_PROJECT_STATS_CODESIZE_TARGET_NAME	:= $(LT_TARGET_STATS_DIR)/$(LT_PROJECT).codesize.txt
LT_PROJECT_STATS_TARGETS 				:= $(LT_PROJECT_STATS_STACKUSAGE_TARGET_NAME)	\
										   $(LT_PROJECT_STATS_CODESIZE_TARGET_NAME)

LT_PROJECT_ALL_TARGETS := 					            \
			$(LT_PROJECT_OBJ_DIR)			            \
			$(LT_PROJECT_OBJ_SUBDIRS)		            \
            $(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_DIR)   \
			$(LT_PROJECT_PREBUILD_TARGETS)	            \
			$(LT_PROJECT_TARGETS)			            \
			$(LT_PROJECT_POSTBUILD_TARGETS)

ifneq (yes, $(LT_DISABLE_STATS))
    LT_PROJECT_ALL_TARGETS += $(LT_PROJECT_STATS_TARGETS)
endif

# compute build flags
ifeq (debug, $(LT_BUILD_MODE))
	LT_CFLAGS		 		:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CFLAGS_GENERIC) $(LT_CFLAGS_LIBRARY) $(LT_CFLAGS_DEBUG) $(LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS) $(LT_TARGET_RULE_DEFS)
	LT_CXXFLAGS		 		:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CXXFLAGS_GENERIC) $(LT_CXXFLAGS_LIBRARY) $(LT_CXXFLAGS_DEBUG) $(LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS) $(LT_TARGET_RULE_DEFS)
	LT_CFLAGS_OPENSOURCE 	:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CFLAGS_GENERIC) $(LT_CFLAGS_LIBRARY) $(LT_CFLAGS_DEBUG)
else
	LT_CFLAGS		 		:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CFLAGS_GENERIC) $(LT_CFLAGS_LIBRARY) $(LT_CFLAGS_RELEASE) $(LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS) $(LT_TARGET_RULE_DEFS)
	LT_CXXFLAGS		 		:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CXXFLAGS_GENERIC) $(LT_CXXFLAGS_LIBRARY) $(LT_CXXFLAGS_RELEASE) $(LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS) $(LT_TARGET_RULE_DEFS)
	LT_CFLAGS_OPENSOURCE 	:= $(LT_PUBLIC_INCLUDE_FLAGS) $(LT_CFLAGS_GENERIC) $(LT_CFLAGS_LIBRARY) $(LT_CFLAGS_RELEASE)
endif

# execute targets
.PHONY: all
all: $(LT_PROJECT_ALL_TARGETS)

.PHONY: clean
clean:
	rm -rf $(LT_PROJECT_ALL_TARGETS)

.PHONY: examinate
examinate: $(LT_PROJECT_OBJ_DIR)
	$(LT_QUIET_CMD) @echo EXAMINATE $(LT_PROJECT) > /dev/null
	$(LT_EXEC_CMD) LT_CFLAGS='$(LT_CFLAGS)' LT_PROJECT_SOURCE_DIR=$(LT_PROJECT_SOURCE_DIR) LT_PROJECT_OBJ_DIR=$(LT_PROJECT_OBJ_DIR) $(LT_OS_BUILD_SCRIPTS_DIR)/examinate/examinate --comments

# TARGETS FOR ARBOLATING THE ARBOLATED RESOURCE TREE!
ifneq (,$(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE))

$(LT_PROJECT_ROOT_IMPL_OBJ_FILE): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.c $(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_FILE)
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CC) $(LT_CFLAGS) $(LT_CFLAGS_INCLUDE_ARBOLATED_TREE) -c $< -o $@

$(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_DIR):
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_DIR_THIS_BUILD)/%,%,$@)
	$(LT_EXEC_CMD)  mkdir -p $@

$(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_FILE): $(LT_ARBOLATED_RESOURCE_TREE_INCLUDE_DIR) $(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE) $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator
	$(LT_QUIET_CMD) @echo ARBOLATE $(patsubst $(LT_TARGET_DIR_THIS_BUILD)/%,%,$@)
	$(LT_EXEC_CMD)  $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator --genresourcetree $(LT_PROJECT_RESOURCE_TREE_JSON_SOURCE_FILE) $@ $(LT_ARBOLATED_RESOURCE_TREE_JSON_SOURCE_DIR)

#	$(LT_QUIET_CMD) @echo ARBOLATE $(patsubst $(LT_TARGET_DIR_THIS_BUILD)/%,%,$@)
endif

$(LT_PROJECT_OBJ_DIR):
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_DIR_THIS_BUILD)/%,%,$@)
	$(LT_EXEC_CMD)  mkdir -p $@

$(foreach subdir, $(LT_PROJECT_SOURCE_SUBDIRS), $(LT_PROJECT_OBJ_DIR)/$(subdir)):
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_DIR_THIS_BUILD)/%,%,$@)
	$(LT_EXEC_CMD)  mkdir -p $@

-include $(LT_PROJECT_DEP_FILES)

$(LT_PROJECT_CPP_OBJ_FILES): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.cpp
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CXX) $(LT_CXXFLAGS) -c $< -o $@

$(LT_PROJECT_C_OBJ_FILES): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.c
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CC) $(LT_CFLAGS) -c $< -o $@

$(LT_PROJECT_M_OBJ_FILES): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.m
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CC) $(LT_CFLAGS) -c $< -o $@

$(LT_PROJECT_S_OBJ_FILES): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.s
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CC) -x assembler-with-cpp $(LT_CFLAGS) -c $< -o $@

$(LT_PROJECT_S2_OBJ_FILES): $(LT_PROJECT_OBJ_DIR)/%.o : $(LT_PROJECT_SOURCE_DIR)/%.S
	$(LT_QUIET_CMD) @echo CC $(<F)
	$(LT_EXEC_CMD)  $(LT_CC) -x assembler-with-cpp $(LT_CFLAGS) -c $< -o $@

#LT_DUMPER = $(info $(shell printf "%-38s = %s\n" ${1} "${${1}}"))
#LT_OUT := $(info ______________________)
#$(call LT_DUMPER,LT_STATIC_LIB_TARGET_NAME)
#$(call LT_DUMPER,LT_SHARED_LIB_TARGET_NAME)
#$(call LT_DUMPER,LT_EXECUTABLE_TARGET_NAME)
#$(call LT_DUMPER,LT_PROJECT_ROOT_IMPL_OBJ_FILE)
#$(call LT_DUMPER,LT_PROJECT_ALL_OBJ_FILES)
#$(call LT_DUMPER,LT_EXECUTABLE_EXTRA_DEPENDS)


ifneq (,$(LT_PROJECT_PREBUILT_OBJ_LIBS))
LT_PROJECT_PREBUILT_OBJ_LIBNAMES 	:= $(basename $(notdir $(LT_PROJECT_PREBUILT_OBJ_LIBS)))
LT_TARGET_PREBUILT_OBJ_BASENAME		:= prebuilt-libobj
LT_TARGET_PREBUILT_OBJ_BASEDIR   	:= $(LT_PROJECT_OBJ_DIR)/$(LT_TARGET_PREBUILT_OBJ_BASENAME)
LT_TARGET_PREBUILT_OBJ_DIRS  		:= $(patsubst %,$(LT_TARGET_PREBUILT_OBJ_BASEDIR)/%,$(LT_PROJECT_PREBUILT_OBJ_LIBNAMES))
LT_TARGET_PREBUILT_OBJ_FILES		:= $(patsubst %,%/*.o,$(LT_TARGET_PREBUILT_OBJ_DIRS))

#LT_DUMPER = $(info $(shell printf "%-38s = %s\n" ${1} "${${1}}"))
#LT_OUT := $(info ______________________)
#$(call LT_DUMPER,LT_PROJECT_PREBUILT_OBJ_LIBS)
#$(call LT_DUMPER,LT_TARGET_PREBUILT_OBJ_BASEDIR)
#$(call LT_DUMPER,LT_TARGET_PREBUILT_OBJ_DIRS)
#$(call LT_DUMPER,LT_TARGET_PREBUILT_OBJ_FILES)
#$(error Please)
endif

$(LT_STATIC_LIB_TARGET_NAME): $(LT_PROJECT_ROOT_IMPL_OBJ_FILE) $(LT_PROJECT_ALL_OBJ_FILES) $(LT_PROJECT_PREBUILT_OBJ_LIBS)
	$(LT_QUIET_CMD) @echo RM $(@F)
	$(LT_EXEC_CMD)  rm -f $@
ifneq (,$(LT_PROJECT_PREBUILT_OBJ_LIBS))
	$(LT_EXEC_CMD)  rm -rf $(LT_TARGET_PREBUILT_OBJ_BASEDIR)
	$(LT_EXEC_CMD)  mkdir -p $(LT_TARGET_PREBUILT_OBJ_DIRS)
ifeq (1,$(LT_QUIET))
	@$(foreach archive, $(patsubst $(LT_PROJECT_OBJ_DIR)/%,%.a,$(LT_TARGET_PREBUILT_OBJ_DIRS)), echo "EXTRACT $(archive)";)
endif
	$(LT_EXEC_CMD)  $(foreach archive, $(LT_PROJECT_PREBUILT_OBJ_LIBS), cd $(LT_TARGET_PREBUILT_OBJ_BASEDIR)/$(basename $(notdir $(archive))) && $(LT_AR) -x $(archive);)
endif
	$(LT_QUIET_CMD) @echo AR $(@F)
	$(LT_EXEC_CMD)  $(LT_AR) $(LT_ARFLAGS_STATICLIB) $@ $(LT_PROJECT_ROOT_IMPL_OBJ_FILE) $(LT_PROJECT_ALL_OBJ_FILES) $(LT_TARGET_PREBUILT_OBJ_FILES)

# $(LT_EXEC_CMD)  @cd $(LT_TARGET_LIBOBJ_DIR) && $(foreach archive, $(LT_PROJECT_PREBUILT_OBJ_LIBS), ar -x $(archive);)

$(LT_SHARED_LIB_TARGET_NAME): $(LT_PROJECT_ROOT_IMPL_OBJ_FILE) $(LT_PROJECT_ALL_OBJ_FILES)
	$(LT_QUIET_CMD) @echo LD $(@F)
	$(LT_EXEC_CMD)  $(LT_LD_FRONTEND) -o $@ $^ $(LT_LDFLAGS_SHAREDLIB)
ifeq (release, $(LT_BUILD_MODE))
ifeq (true, $(LT_STRIP_RELEASE_BUILD))
	$(LT_QUIET_CMD) @echo STRIP $(@F)
	$(LT_EXEC_CMD)  $(LT_STRIP) $(LT_STRIPFLAGS_STRIPALL) $@
endif
endif

$(LT_EXECUTABLE_TARGET_NAME): $(LT_PROJECT_ROOT_IMPL_OBJ_FILE) $(LT_PROJECT_ALL_OBJ_FILES) $(LT_EXECUTABLE_EXTRA_DEPENDS)
	$(LT_QUIET_CMD) @echo LD-EXE $(@F)
	$(LT_EXEC_CMD)  $(LT_LD_FRONTEND) -o $@ $(LT_PROJECT_ALL_OBJ_FILES) $(LT_LDFLAGS_EXECUTABLE)
ifeq (release, $(LT_BUILD_MODE))
ifeq (true, $(LT_STRIP_RELEASE_BUILD))
	$(LT_QUIET_CMD) @echo STRIP $(@F)
	$(LT_EXEC_CMD)  $(LT_STRIP) $(LT_STRIPFLAGS_STRIPALL) $@
endif
endif

$(LT_PROJECT_STATS_STACKUSAGE_TARGET_NAME): $(LT_PROJECT_ALL_OBJ_FILES_SANS_ASM)
ifneq ($(LT_DISABLE_STATS), yes)
	$(LT_QUIET_CMD) @echo GENSTATS-STACKUSAGE $(@F)
	$(LT_EXEC_CMD) LT_RELEASE_IDENTIFIER=$(LT_BUILD_VERSION) $(LT_OS_BUILD_SCRIPTS_DIR)/stats/genstats-stackusage $(LT_PROJECT_OBJ_DIR) $@
endif

$(LT_PROJECT_STATS_CODESIZE_TARGET_NAME): $(LT_PROJECT_ROOT_IMPL_OBJ_FILE) $(LT_PROJECT_ALL_OBJ_FILES)
ifneq ($(LT_DISABLE_STATS), yes)
	$(LT_QUIET_CMD) @echo GENSTATS-CODESIZE $(@F)
	$(LT_EXEC_CMD) LT_RELEASE_IDENTIFIER=$(LT_BUILD_VERSION) $(LT_OS_BUILD_SCRIPTS_DIR)/stats/genstats-codesize $(LT_PROJECT_OBJ_DIR) $@
endif

###############################################################################
#	LOG
###############################################################################
#   21-Jan-20   augustus    created
#   14-Feb-20   augustus    added LT_PROJECT_PREBUILT_OBJ_LIBS support
#   01-Mar-20   augustus    added LT_CFLAGS_LIBRARY and LT_CXXFLAGS_LIBRARY
#   18-Mar-20   augustus    use LT_ECHO for echoing escape sequences properly
#   11-Apr-20   augustus    renamed LT_ECHO to LT_ECHO_ESCAPED
#   04-Jul-20   augustus    added LT_PROJECT_STATS_TARGETS with STACKUSAGE and CODESIZE targets
#   09-Jul-20   caligula    allow disablement of codesize and stacksize statistics
#   27-Aug-20   augustus    added LT_CURRENTLY_BUILDING_LTLIBRARY_FLAGS
#   29-Jan-21   augustus    added LT_TARGET_RULE_DEFS
#   06-Mar-21   augustus    added LT_CFLAGS_OPENSOURCE
#   06-Nov-21   augustus    added LT_LD_FRONTEND and support for Objective C .m files
#   31-Aug-22   augustus    added examinate rule
#   31-Jan-23   augustus    added arbolation targets
