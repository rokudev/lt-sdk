################################################################################
# ExampleDhrystone.mk - project makefile for LT Dhrystone benchmark example
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# ________________
# ExampleDhrystone - LT_PROJECT_SOURCE_DIR and LT_PROJECT_SOURCE_FILES
LT_PROJECT_SOURCE_DIR			:=	$(LT_PROJECT_SOURCE_DIR_BASE)/example/dhrystone
LT_PROJECT_SOURCE_FILES			:= 	ExampleDhrystone.c

# _____________________________________________________
# DHRYSTONE 2.1 third party lib - SETUP BUILD VARIABLES

# update compiler flags for optimization
LT_CFLAGS_RELEASE := $(patsubst -Os,-O2,$(LT_CFLAGS_RELEASE))
LT_CXXFLAGS_RELEASE := $(patsubst -Os,-O2,$(LT_CXXFLAGS_RELEASE))

# variables for location of dhrystone tarfile distro/patch, extract dir, build dir, artifacts
LT_DHRY_VERSION					:= dhrystone_2.1
LT_DHRY_TARFILE					:= $(LT_DHRY_VERSION).tgz
LT_DHRY_PATCHFILE				:= $(LT_DHRY_VERSION).patch
LT_DHRY_DISTRO_DIR 				:= $(LT_OS_ROOT)/thirdparty/dhrystone
LT_DHRY_DISTRO_TARFILE_PATH		:= $(LT_DHRY_DISTRO_DIR)/$(LT_DHRY_TARFILE)
LT_DHRY_DISTRO_PATCHFILE_PATH	:= $(LT_DHRY_DISTRO_DIR)/$(LT_DHRY_PATCHFILE)
LT_DHRY_EXTRACT_SUBDIR			:= dhrystone
LT_DHRY_EXTRACT_DIR				:= $(LT_TARGET_OBJ_DIR)/$(LT_PROJECT)/$(LT_DHRY_EXTRACT_SUBDIR)
LT_DHRY_BUILD_DIR 				:= $(LT_DHRY_EXTRACT_DIR)/$(LT_DHRY_VERSION)
LT_DHRY_ARTIFACT_DIR			:= $(LT_DHRY_BUILD_DIR)/lib
LT_DHRY_LINKLINE_LIBNAME		:= dhry_2.1
LT_DHRY_STATIC_LIB_NAME			:= lib$(LT_DHRY_LINKLINE_LIBNAME).a
LT_DHRY_STATIC_LIB_PATH			:= $(LT_DHRY_ARTIFACT_DIR)/$(LT_DHRY_STATIC_LIB_NAME)

# _____________________________________________________________
# DHRYSTONE 2.1 third party lib - CREATE TARBALL EXTRACTION DIR
# add LT_DHRY_EXTRACT_SUBDIR as a 'project source subdir'.  Rules.mk will mkdir it as a subdir of our
# projects obj intermediate directory before invoking the prebuild targets - this will get our extract dir built
LT_PROJECT_SOURCE_SUBDIRS		:=	$(LT_DHRY_EXTRACT_SUBDIR)

# __________________________________________________________________
# TryoutDriverDhrystone Library - SET INCLUDE PATH AND LINKAGE SPECS
#							      for built DHRYSTONE 2.1 open source lib
# add the generated library include directory
LT_PUBLIC_INCLUDE_FLAGS			+= -I $(LT_DHRY_BUILD_DIR)/include

# put libdhry_2.1.a on the link line of libTryoutDriverDhrystone.so in the event we're building for a platform
# that has a runtime dynamic loader where LT Libraries are .so files
LT_LDFLAGS_SHAREDLIB			+= $(LT_DHRY_STATIC_LIB_PATH)

# set the libdhry_2.1.a artifact as a 'prebuilt obj lib' so that its files will be added to our LT Library .a file,
# libTryoutDriverDhrystone.a in the event we're building for a platform with no runtime dynamic loader
# like Halford, Tipton
LT_PROJECT_PREBUILT_OBJ_LIBS	:= $(LT_DHRY_STATIC_LIB_PATH)

# _________________________________________________________________
# TryoutDriverDhrystone Library - SET DHRYSTONE 2.1 PREREQUISITE
# set libdhry_2.1.a as a prebuild target so it gets built as a prerequisite of TryoutDriverDhrystone
LT_PROJECT_PREBUILD_TARGETS		:= $(LT_DHRY_STATIC_LIB_PATH)

# get Rules.mk
include $(LT_PROJECT_RULES_MAKEFILE)

# ______________________________________________________
# DHRYSTONE 2.1  third party lib - SET BUILD_CFLAGS
#set the CFLAGS to pass to the dhrystone makefile configure script
LT_DHRY_CFLAGS := $(strip $(subst -Wstrict-prototypes,,$(LT_CFLAGS_OPENSOURCE)))
LT_DHRY_CFLAGS := $(strip $(subst -Werror,,$(LT_DHRY_CFLAGS)))
LT_DHRY_CFLAGS := $(strip $(subst -Wall,,$(LT_DHRY_CFLAGS)))
LT_DHRY_CFLAGS := $(strip $(subst -Wextra,,$(LT_DHRY_CFLAGS)))
LT_DHRY_CFLAGS := $(strip $(subst -Wundef,,$(LT_DHRY_CFLAGS)))
LT_DHRY_CFLAGS += -Wno-implicit-int
LT_DHRY_CFLAGS += -Wno-implicit-function-declaration
LT_DHRY_CFLAGS += -Wno-pointer-to-int-cast

# ____________________________________________
# DHRYSTONE 2.1  third party lib - MAKE TARGETS
# PREBUILD TARGETS - steps (targets) to build libdhry_2.1.a for TryoutDriverDhrystone
#                  - this is done via dependency chain of targets starting at the bottom
#				   - with libdhry_2.1.a and working our way to the top which untars the tarfile
#				   - and then back down the chain running patch and then make

# untar the dhrystone tarfile, which we'll know we need to do based on whether or not there is a README_C file in the build dir
$(LT_DHRY_BUILD_DIR)/README_C:
	$(LT_QUIET_CMD) @echo UNTAR $(LT_DHRY_TARFILE)
	$(LT_EXEC_CMD)  tar xf $(LT_DHRY_DISTRO_TARFILE_PATH) -C $(LT_DHRY_EXTRACT_DIR)

# run patch, but it depends on untarring the package, as identified by any file in the package that won't get touched by this build, we'll use README
$(LT_DHRY_BUILD_DIR)/.dhry-patched : $(LT_DHRY_BUILD_DIR)/README_C
	$(LT_QUIET_CMD) @echo PATCH $(LT_DHRY_VERSION)
	$(LT_EXEC_CMD)  cd $(LT_DHRY_EXTRACT_DIR) && patch $(LT_GNU_QUIET_ARG) -p0 < $(LT_DHRY_DISTRO_PATCHFILE_PATH) && touch $(LT_DHRY_BUILD_DIR)/.dhry-patched

# run make to generate libdhry_2.1.a, which depends on the patch being applied, as identified by the presence of .dhry-patched
$(LT_DHRY_STATIC_LIB_PATH) : $(LT_DHRY_BUILD_DIR)/.dhry-patched
	$(LT_QUIET_CMD) @echo MAKE $(@F)
	$(LT_EXEC_CMD)  CC="$(LT_CC)" CFLAGS="$(LT_DHRY_CFLAGS)" AR="$(LT_AR)" ARFLAGS="$(LT_ARFLAGS_STATICLIB)" $(MAKE) $(LT_GNU_QUIET_ARG) -C $(LT_DHRY_BUILD_DIR)

###############################################################################
#   LOG
###############################################################################
#   06-Mar-21   augustus    created
