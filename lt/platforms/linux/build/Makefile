################################################################################
# LT PLATFORM MAKEFILE / PRODUCT MAKEFILE
#    <platform_root>/build/Makefile
#     <product_root>/build/Makefile
#
# DESCRIPTION
#   This makefile is the platform/product Makefile located inside the build/
#   sub-directory of product and platform roots.
#
#   This Makefile sets LT_MAKE_INVOCATION_ROOT and invokes the LT_MASTER_MAKEFILE
#   found at $(LT_OS_ROOT)/build/Makefile.  LT_OS_ROOT can be specified as an
#   environment variable or it will be automatically derived by this makefile.
#
#   The LT_MASTER_MAKEFILE will then automatically derive LT_PRODUCT_ROOT and
#   LT_PLATFORM_ROOT from the LT_INVOCATION_ROOT set herein unless those variables
#   are already set in the environment.
#
# MAKE TARGETS
#   This makefile has no direct make targets - the make targets from the LT_MASTER_MAKEFILE are used.
#
# OPTIONAL MAKEFILE VARIABLES
#   LT_OS_ROOT 			- the path to the LT OS Standard Distribution.  If not specified, will
#                		  be derived as ../../../lt/ with respect to this Makefile location
#	LT_PRODUCT_ROOT		- the path to the LT Product Source distribution to be used.  If
#                		  not specified, the LT Master Makefile will derive it as ..
#						  relative to this Makefile location (e.g. this root)
#						  An LT_PRODUCT_ROOT must have a build/product/ subdirectory to qualify.
#   LT_PLATFORM_ROOT	- the path to the LT Platform Source distribution to be used.  If
#						  not specified, the LT Master Makefile will derive it as ..
#						  relative to this Makefile location (e.g. this root).
#						  An LT_PLATFORM_ROOT must have a build/platform/ subdirectory to qualify.
#	LT_TARGET_ROOT		- the path in which the LT_MASTER_MAKEFILE will create a targets/
#						  directory which will receive all intermediate and final build artifacts.
#						  If not specified, the LT_MASTER_MAKEFILE will derive it as ..
#						  relative to this Makefile location (e.g. this root)
#	LT_PRODUCT			- the LT_PRODUCT variant to build for.  If not specified, the LT_MASTER_MAKEFILE
#						  will derive a default from the final path element of LT_PRODUCT_ROOT.
#	LT_PLATFORM			- the LT_PLATFORM variant to build for.  If not specified, the LT_MASTER_MAKEFILE
#						  will derive a default.
#   LT_BUILD_MODE		- The LT_BUILD_MODE (debug or release) to build for.

# Copyright 2020, Roku, Inc.  All rights reserved.
################################################################################

################################################################################
# 1. set LT_MAKE_INVOCATION_ROOT as the root containing this build/Makefile
LT_MAKE_INVOCATION_ROOT	:= $(abspath $(dir $(abspath $(lastword "$(MAKEFILE_LIST))"))..)

################################################################################
# 2. Derive LT_OS_ROOT, if not already set (location of LT OS Source Distro)
LT_OS_ROOT				?= $(abspath $(LT_MAKE_INVOCATION_ROOT)/../../lt)

###############################################
# 3. set the location of the LT_MASTER_MAKEFILE
LT_MASTER_MAKEFILE		:= $(LT_OS_ROOT)/build/Makefile

##############################
# 4. invoke LT_MASTER_MAKEFILE
$(info )
ifeq (yes, $(shell if [ -f $(LT_MASTER_MAKEFILE) ] ; then echo yes; else echo no; fi;))
    include $(LT_MASTER_MAKEFILE)
else
    $(info LT Make: Cannot locate LT Master Makefile $(LT_MASTER_MAKEFILE))
    $(info -------> Set LT_OS_ROOT to point to the root of the LT OS Standard Distribution and reinvoke)
    $(info )
    $(error Must. Now)
endif

###############################################################################
# 	LOG
###############################################################################
#   11-Dec-20   augustus    created
