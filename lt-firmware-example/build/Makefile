################################################################################
# LT USER FIRMWARE PROJECT MAKEFILE
#     <user_project>/build/Makefile
#
# DESCRIPTION
#   This build/Makefile for your LT firmware project invokes the LT Master makefile to build, clean,
#   rebuild and master custom LT firmware flash images, including partition tables, bootloaders,
#   data partitions, and complete product firmware images containing LT OS, platform driver, and
#   product software libraries.
#
#   You do not need to write custom makefiles; you do not need to modify this makefile.
#   You only need to invoke this Makefile.
#
# MAKEFILE INVOCATION
#   Instruct make to supply the Makefile directory as the current directory:
#      % make -C <user_project>/build
#
# PLATFORM AND PRODUCT VARIANT SPECIFICATION
#   The LT Master Makefile build firmware images of product software
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################
MAKEFLAGS += --no-print-directory

################################################################################
# 1. set LT_USER_FIRMWARE_PROJECT_ROOT as the root of the project containing this build/Makefile
	LT_USER_FIRMWARE_PROJECT_ROOT	:= $(abspath $(dir $(abspath $(lastword "$(MAKEFILE_LIST))"))..)

################################################################################
# 2. Derive LT_OS_ROOT, if not already set (location of LT OS Source Distro)
	LT_OS_ROOT				?= $(abspath $(LT_USER_FIRMWARE_PROJECT_ROOT)/../lt/lt)

###############################################
# 3. set the location of the LT_MASTER_MAKEFILE
	LT_MASTER_MAKEFILE		:= $(LT_OS_ROOT)/build/Makefile

##############################
# 4. invoke LT_MASTER_MAKEFILE
	ifeq (yes, $(shell if [ -f $(LT_MASTER_MAKEFILE) ] ; then echo yes; else echo no; fi;))
		include $(LT_MASTER_MAKEFILE)
	else
		LT_OUT := $(info LT Make: Cannot locate LT Master Makefile $(LT_MASTER_MAKEFILE))
		LT_OUT := $(info -------> Set LT_OS_ROOT to point to the root of the LT OS Source Distribution and reinvoke)
		LT_OUT := $(info )
		LT_OUT := $(error Jim....  Must... Now..)
	endif

###############################################################################
# 	LOG
###############################################################################
#   11-Dec-20   augustus    created

