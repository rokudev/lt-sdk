################################################################################
# LTK.mk - project makefile for LTK
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/ltk

# LTKernel architecture selection
ifeq (LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS, $(LT_LTKERNEL_ARCHITECTURE))
  $(error Cannot build LTK when hosted by another OS)
endif

ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V6M, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchArmCortexM_Base.c
else ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V7M, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchArmCortexM_Main.c
else ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_BASE, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchArmCortexM_Base.c
else ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchArmCortexM_Main.c
else ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M, $(LT_LTKERNEL_ARCHITECTURE))
  # Note V8M == V8M_Main
  LT_PROJECT_SOURCE_FILES   += LTKArchArmCortexM_Main.c
else ifeq (LT_LTKERNEL_ARCHITECTURE_ARM_V5, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchArm_V5.c
  LT_PROJECT_SOURCE_FILES   += LTKArchArm_V5_Vectors.S
else ifeq (LT_LTKERNEL_ARCHITECTURE_XTENSA, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchXtensa.c
  LT_PROJECT_SOURCE_FILES   += LTKArchXtensa_Vectors.S
  LT_PROJECT_SOURCE_FILES   += LTKArchXtensa_Vendor.S
else ifeq (LT_LTKERNEL_ARCHITECTURE_RISC_V, $(LT_LTKERNEL_ARCHITECTURE))
  LT_PROJECT_SOURCE_FILES   += LTKArchRISC_V.c
  LT_PROJECT_SOURCE_FILES   += LTKArchRISC_V_Vectors.S
else
  $(error Unknown LT_LTKERNEL_ARCHITECTURE)
endif

LT_PROJECT_SOURCE_FILES     += LTKernel.c
LT_PROJECT_SOURCE_FILES     += LTKAllocator.c

ifneq (yes, $(LT_PROJECT_KEEP_OS_OPTIMIZATION))
LT_CFLAGS_RELEASE           := $(patsubst -Os,-O2,$(LT_CFLAGS_RELEASE))
LT_CXXFLAGS_RELEASE         := $(patsubst -Os,-O2,$(LT_CXXFLAGS_RELEASE))
endif
LT_LDFLAGS_SHAREDLIB        += -l$(LT_PLATFORM_LTCOREBSP_PROJECT)

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   31-Aug-22   tiberius    Created
