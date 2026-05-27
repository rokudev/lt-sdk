################################################################################
# LTCore.mk - project makefile for LTCore library
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       :=  $(LT_PROJECT_SOURCE_DIR_BASE)/lt/core
LT_PROJECT_SOURCE_FILES     +=  LTCoreImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTConsoleConnector.c
LT_PROJECT_SOURCE_FILES     +=  LTEventImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTHandle.c
LT_PROJECT_SOURCE_FILES     +=  LTLibraryManager.c
LT_PROJECT_SOURCE_FILES     +=  LTLoggerImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTStdlibImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTStdlibImpl_vsnprintf.c
LT_PROJECT_SOURCE_FILES     +=  LTArrayImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTThreadImpl.c
LT_PROJECT_SOURCE_FILES     +=  LTSpinLock.c
LT_PROJECT_SOURCE_FILES     +=  LTCountingSemaphore.c
LT_PROJECT_SOURCE_FILES     +=  LTResourceTreeImpl.c

ifeq (LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS, $(LT_LTKERNEL_ARCHITECTURE))
  # Satisfy OS dependencies for hosted OS directly in LTCore
  LT_PROJECT_SOURCE_FILES   +=  LTKArchHosted.c
else
  LT_PROJECT_SOURCE_FILES   +=  LTKArchNative.c
endif

ifneq (yes, $(LT_PROJECT_KEEP_OS_OPTIMIZATION))
LT_CFLAGS_RELEASE           := $(patsubst -Os,-O2,$(LT_CFLAGS_RELEASE))
LT_CXXFLAGS_RELEASE         := $(patsubst -Os,-O2,$(LT_CXXFLAGS_RELEASE))
endif
# NOTE: -fno-builtin prevents unintended recursion in LTStdlibImpl
LT_CFLAGS_GENERIC           := -fno-inline -fno-builtin $(LT_CFLAGS_GENERIC)
LT_CXXFLAGS_GENERIC         := -fno-inline -fno-builtin $(LT_CXXFLAGS_GENERIC)
LT_LDFLAGS_SHAREDLIB        += -l$(LT_PLATFORM_LTCOREBSP_PROJECT)

ifeq (yes, $(LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING))
    LT_CFLAGS_GENERIC     += -DLTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING=1
    LT_CXXFLAGS_GENERIC   += -DLTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING=1
else
    LT_CFLAGS_GENERIC     += -DLTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING=0
    LT_CXXFLAGS_GENERIC   += -DLTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING=0
endif

# if we only have static linking then poner adentro bsp and ltk
ifneq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
  LT_PROJECT_PREBUILT_OBJ_LIBS := $(LT_TARGET_LIB_DIR)/lib$(LT_PLATFORM_LTCOREBSP_PROJECT).a
  ifneq (LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS, $(LT_LTKERNEL_ARCHITECTURE))
    LT_PROJECT_PREBUILT_OBJ_LIBS += $(LT_TARGET_LIB_DIR)/lib$(LT_OS_LTK_PROJECT).a
  endif
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   23-Jan-20   augustus    created
#   31-Aug-22   tiberius    split out LTK build from LTCore
