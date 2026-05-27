################################################################################
# Makefile - LT Master makefile
#
# Copyright 2020-2026, Roku, Inc.  All rights reserved.
################################################################################
#
# DESCRIPTION
#   This is the LT Master Makefile.  It lives in the build/ subdirectory of the
#   root of the LT Standard Distribution.  It may be invoked directly -
#   "make -C /home/lt/build" or "cd /home/lt/build && make".
#
#   This Makefile works in conjunction with an LT_PRODUCT_ROOT and an LT_PLATFORM_ROOT.
#   This Makefile has several optional Makefile variables that may be set in the
#   environment or on the make command line, e.g. "LT_PLATFORM_ROOT=/home/lt/platforms/macos make"
#   Any variables that are not present in the environment or supplied on the command
#   line will be automatically derived by this Makefile.  The complete set of parameterizing
#   makefile variables is listed below under the heading OPTIONAL MAKEFILE VARIABLES.
#
#   This Makefile is commonly executed indirectly via the Makefile present in an
#   LT_PRODUCT_ROOT/build/ or LT_PLATFORM_ROOT/build/ directory.  This makefile functions
#   in the same way independently of the directory from which the user enacted 'make', i.e.
#   Whether or not the user is in LT_PRODUCT_ROOT/build LT_PLATFORM_ROOT/build or LT_OS_ROOT/build
#   and types 'make', this LT_MASTER_MAKEFILE is invoked and the build proceeds as normal.
#   The only difference is that the default value for LT_TARGET_ROOT as well as those for any missing
#   optional variables are optimized for the location from which the user executes make.
#
# BUILD OUTPUT
#   This Makefile builds the LTCore OS library along with the collection of network, system, media,
#   and device LT OS libraries that comprise the LT Basic Set. This basic set of LT OS libraries remains
#   constant across all platform and product variants.  This LT_MASTER_MAKEFILE also builds the platform-specific
#   LTCore OS BSP (board support package) and device driver libraries from the specified LT_PLATFORM_ROOT.
#   It also builds all specified product libraries from the specified LT_PRODUCT_ROOT.  The result is a
#   complete product build comprised entirely of LT Libraries.  On host operating systems like Linux,
#   these libraries are individual .so files, whereas on real-time embedded systems that boot LT directly,
#   the final build output is a binary firmware image, ready for flashing, that contains all of the libraries
#   in a statically linked configuration.
#
#   All build output, including LT library binaries, statically link libraries, and intermediate build files,
#   are output into a targets/ subdirectory created under LT_TARGET_ROOT.  LT_TARGET_ROOT will be automatically
#   derived if not excplicitly specified.  All build output underneath LT_TARGET_ROOT/targets/ is further
#   segmented by product, platform, and release mode, e.g. targets/exciter/halford/release/bin/ obj/ lib/
#
# MAKE TARGETS
#   This makefile supports the following make targets using the following invocations:
#   ________________________________________________________________________________
#   INVOCATION              DESCRIPTION
#    make                   - same as make all
#    make quiet             - same as make all quiet
#    make all               - makes the specified product/platform/os configuration with full
#                             command line invocation output for all build tools
#    make all quiet         - makes the specified product/platform/os configuration with terse,
#                             easier to read, build output
#    make bootloaders       - builds all bootloader targets
#    make images            - builds flash images
#    make clean             - does a hyper-fast-clean of the specified product/platform/os configuration
#    make rebuild           - equivalent to make clean; make
#    make rebuild quiet     - equivalent to make clean; make quiet
#    make <libname>         - makes a specific library, e.g. make LTSystemNetwork
#    make <libname> quiet   - makes a specific library, quietly, e.g. make LTSystemNetwork quiet
#    make clean-<libname>   - cleans a specific library, e.g. make clean-LTSystemNetwork
#    make rebuild-<libname> - rebuilds a specific library, e.g. make rebuild-LTSystemNetwork
#    make examinate-<libname> - runs examinate on a single library, e.g. make examinate-LTSystemShell (see next item)
#    make examinate         - runs the preprocessor over library public interfaces of lt, platform, and product libraries
#                           - and puts a combined mirror of the public include directories under "examinated/" in the targets directory
#                             alongside obj/, lib/, bin/, stats/, and doc/ - use this to examine the 'real' interface declarations
#                             also runs as a prerequisite dependency of 'make doxydocs' (not yet but it will)
#    make doxydocs          - makes doxygen documentation
#    make dumpvars          - dumps all makefile variables for debugging purposes
#
#    make buildtools                    - makes build tools from source in ../../tools/ subdirs
#    make clean-buildtools              - cleans build tools
#    make buildtool-<toolname>          - make specific buildtool
#    make clean-buildtool-<toolname>    - cleans  specific buildtool
#    make rebuild-buildtool-<toolname>  - rebuilds specific buildtool
#
#    make dumpvar-<VARNAME>             - dumps VARNAME=value for the makefile variable "VARNAME", in the same form as dumpvars, intended for human consumption
#    make printvar-<VARNAME>            - prints the value of the makefile variable "VARNAME" with no adornments, designed for easy access to variables by scripts
#
#    make foo               - quick and dirty way to check PRODUCT and PLATFORM settings without building anything
#
# OPTIONAL MAKEFILE VARIABLES
#   The following makefile variables control which product and platform roots are used for any given make invocation:
#   ________________________________________________________________________________
#   VARIABLE                 DESCRIPTION
#    LT_PRODUCT_ROOT         - the path to the LT Product Source distribution to be used.  If
#                              not specified, the LT Master Makefile will derive it as ..
#                              relative to this Makefile location (i.e. relative to LT_OS_ROOT)
#                              An LT_PRODUCT_ROOT must have a build/product/ subdirectory to qualify.
#    LT_PLATFORM_ROOT        - the path to the LT Platform Source distribution to be used.  If
#                              not specified, the LT Master Makefile will derive it as ..
#                              relative to this Makefile location (i.e. relative to LT_OS_ROOT).
#                              An LT_PLATFORM_ROOT must have a build/platform/ subdirectory to qualify.
#    LT_TARGET_ROOT          - the path in which the LT_MASTER_MAKEFILE will create a targets/
#                              directory which will receive all intermediate and final build artifacts.
#                              If not specified, the LT_MASTER_MAKEFILE will derive it as ../..
#                              relative to this Makefile location i.e. at the same level LT_OS_ROOT exists in.
#    LT_PRODUCT              - the LT_PRODUCT variant within the product root to build for.  The term LT_PRODUCT_ROOT
#                              is synonymous with "product family" and the term LT_PRODUCT is synonymous with "product
#                              variant within a product family."  Product variants are located in subdirectories of the
#                              LT_PRODUCT_ROOT/build/product directory.  If LT_PRODUCT is not specified, the
#                              LT_MASTER_MAKEFILE will derive a default if possible.
#    LT_PLATFORM             - the LT_PLATFORM variant within the platform root to build for.  The term LT_PLATFORM_ROOT
#                              is synonymous with "platform family" and the term LT_PLATFORM is synonymous with ""platform
#                              variant within a platform family."  Platform variants are located in subdirectories of the
#                              LT_PLATFORM_ROOT/build/platform directory.  If LT_PLATFORM is not specified, the
#                              LT_MASTER_MAKEFILE will derive a default if possible.
#    LT_BUILD_MODE           - The LT_BUILD_MODE (debug or release) to build for.
#
#    LT_VERSION_MAJOR        - The major numeric component of the build version in the format major.minor.buildnumber
#                              If not specified will be defaulted to 0.
#    LT_VERSION_MINOR        - The minor numeric component of the build version in the format major.minor.buildnumber
#                              If not specified will be defaulted to 1.
#    LT_VERSION_BUILD_NUMBER - The buildnumber numeric component of the build version in the format major.minor.buildnumber
#                              If not specified will be defaulted to the non-numeric string "dev" to indicated developer made build.
#    LT_VERSION_COMMIT       - The shortened SHA hash of the git commit.
#                              If not specified will be generated using git if installed, otherwise all zeroes.
#    LT_BUILD_TIME           - The time stamp to be used for the version string, formatted as `date +%s`.
#                              If not specified will be defaulted to the current time.
#    LT_CRYPTO_KEY_DIR       - The directory containing cryptographic secrets used by the mastering process.
#                              Set to REMOTE to use a remote signing service (optional).
#                              If not specified the build will be non-secure (e.g.: no signage, encryption, etc).
#
# Copyright 2020-2026, Roku, Inc.  All rights reserved.
################################################################################

#######################################################################
#     ____ _                 _              _
#    / ___| |__   __ _ _ __ | |_ ___ _ __  / |
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__| | |
#   | |___| | | | (_| | |_) | ||  __/ |    | |
#    \____|_| |_|\__,_| .__/ \__\___|_|    |_|
#                     |_|
#######################################################################
# CHAPTER 1 - setup all of the roots, either according to user
#             specification or as derived defaults as can be determined
#
###
# a. First a handy utility

###
# b. Determine the roots: LT_OS_ROOT, LT_MAKE_INVOCATION_ROOT, LT_PLATFORM_ROOT, LT_PRODUCT_ROOT, and LT_TARGET_ROOT
#    Note that we do not use make's ?= test/assignment operator because if a variable VAR is cleared with VAR= instead
#         of unset VAR, then ?= fails to consider VAR empty.

# LT_OS_ROOT - the root of the LT OS standard distribution
LT_OS_ROOT                          := $(abspath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))..)

# LT_MAKE_INVOCATION_ROOT - the root from where make was issued, either this LT_OS_ROOT or a platform or product root whose makefile included this makefile
ifeq (,$(LT_MAKE_INVOCATION_ROOT))
    ifeq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
        LT_MAKE_INVOCATION_ROOT     := $(LT_OS_ROOT)
    else
        LT_MAKE_INVOCATION_ROOT     := $(LT_USER_FIRMWARE_PROJECT_ROOT)
    endif
else
    LT_MAKE_INVOCATION_ROOT         := $(abspath $(LT_MAKE_INVOCATION_ROOT))
endif

# LT_USER_FIRMWARE_PROJECT_NAME
ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
  LT_USER_FIRMWARE_PROJECT_NAME := $(notdir $(LT_USER_FIRMWARE_PROJECT_ROOT))
else
  LT_USER_FIRMWARE_PROJECT_NAME :=
endif

# LT_ROOTS_BASE - where to autofind other roots from if we can - the dir containing lt/, platforms/, and products/
LT_ROOTS_BASE := $(abspath $(dir $(LT_OS_ROOT)))

# LT_PLATFORM_ROOT
ifeq (,$(LT_PLATFORM_ROOT))
    ifeq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
        ifeq (yes, $(shell if [ -d $(LT_MAKE_INVOCATION_ROOT)/build/platform ] ; then echo yes; else echo no; fi;))
            LT_PLATFORM_ROOT            := $(LT_MAKE_INVOCATION_ROOT)
        endif
    endif
else
    ifeq (no, $(shell if [ -d $(abspath $(LT_PLATFORM_ROOT)) ] ; then echo yes; else echo no; fi;))
      # allow, for example, LT_PLATFORM_ROOT=esp32 to be expanded to $(LT_ROOTS_BASE)/platforms/esp32
      LT_PLATFORM_ROOT_TEST := $(LT_ROOTS_BASE)/platforms/$(LT_PLATFORM_ROOT)
      ifeq (yes, $(shell if [ -d $(abspath $(LT_PLATFORM_ROOT_TEST)) ] ; then echo yes; else echo no; fi;))
        LT_PLATFORM_ROOT := $(LT_PLATFORM_ROOT_TEST)
      endif
    endif
    LT_PLATFORM_ROOT := $(abspath $(LT_PLATFORM_ROOT))
endif

# LT_PRODUCT_ROOT
ifeq (,$(LT_PRODUCT_ROOT))
    ifeq (yes, $(shell if [ -d $(LT_MAKE_INVOCATION_ROOT)/build/product ] ; then echo yes; else echo no; fi;))
        LT_PRODUCT_ROOT             := $(LT_MAKE_INVOCATION_ROOT)
    else
        ifeq (yes, $(shell if [ -d $(LT_OS_ROOT)/build/product ] ; then echo yes; else echo no; fi;))
            LT_PRODUCT_ROOT         := $(LT_OS_ROOT)
        endif
    endif
else
    ifeq (no, $(shell if [ -d $(abspath $(LT_PRODUCT_ROOT)) ] ; then echo yes; else echo no; fi;))
      # allow, for example, LT_PRODUCT_ROOT=iot to be expanded to $(LT_ROOTS_BASE)/products/iot
      LT_PRODUCT_ROOT_TEST := $(LT_ROOTS_BASE)/products/$(LT_PRODUCT_ROOT)
      ifeq (yes, $(shell if [ -d $(abspath $(LT_PRODUCT_ROOT_TEST)) ] ; then echo yes; else echo no; fi;))
        LT_PRODUCT_ROOT := $(LT_PRODUCT_ROOT_TEST)
      endif
    endif
    LT_PRODUCT_ROOT                 := $(abspath $(LT_PRODUCT_ROOT))
endif

# LT_TARGET_ROOT
ifeq (,$(LT_TARGET_ROOT))
    LT_TARGET_ROOT                  := $(abspath $(LT_ROOTS_BASE)/..)
    ifeq (yes, $(shell if [ -d $(LT_TARGET_ROOT)/.git ] ; then echo yes; else echo no; fi;))
      LT_TARGET_ROOT		        := $(abspath $(LT_TARGET_ROOT)/..)
    endif
else
    LT_TARGET_ROOT                  := $(abspath $(LT_TARGET_ROOT))
endif

###
# c. Set LT_PLATFORM_FAMILY, LT_PRODUCT_FAMILY, and set default LT_PLATFORM and LT_PRODUCT values if not already set
ifeq (,$(LT_PLATFORM_ROOT))
    LT_PLATFORM_FAMILY              :=
else
    LT_PLATFORM_FAMILY              := $(notdir $(LT_PLATFORM_ROOT))
    ifeq (, $(LT_PLATFORM))
        ifeq (yes, $(shell if [ -f $(LT_PLATFORM_ROOT)/build/platform/$(LT_PLATFORM_FAMILY)/Makefile.config ] ; then echo yes; else echo no; fi;))
            LT_PLATFORM := $(LT_PLATFORM_FAMILY)
        endif
        ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
            ifeq (yes, $(shell if [ -f $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/platform/$(LT_USER_FIRMWARE_PROJECT_NAME)/Makefile.config ] ; then echo yes; else echo no; fi;))
                LT_PLATFORM := $(LT_USER_FIRMWARE_PROJECT_NAME)
            endif
        endif
    endif
endif

ifeq (,$(LT_PRODUCT_ROOT))
    LT_PRODUCT_FAMILY               :=
else
    LT_PRODUCT_FAMILY               := $(notdir $(LT_PRODUCT_ROOT))
    ifeq (, $(LT_PRODUCT))
        ifeq (yes, $(shell if [ -f $(LT_PRODUCT_ROOT)/build/product/$(LT_PRODUCT_FAMILY)/LTProductConfig.json ] ; then echo yes; else echo no; fi;))
            LT_PRODUCT  := $(LT_PRODUCT_FAMILY)
        endif
        ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
            ifeq (yes, $(shell if [ -f $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/product/$(LT_USER_FIRMWARE_PROJECT_NAME)/LTProductConfig.json ] ; then echo yes; else echo no; fi;))
                LT_PRODUCT := $(LT_USER_FIRMWARE_PROJECT_NAME)
            endif
        endif
    endif
endif

# strip LT_PRODUCT, LT_PLATFORM and LT_BUILD_MODE
ifneq (,$(LT_PRODUCT))
    LT_PRODUCT := $(strip $(LT_PRODUCT))
endif

ifneq (,$(LT_PLATFORM))
    LT_PLATFORM := $(strip $(LT_PLATFORM))
endif

ifneq (,$(LT_BUILD_MODE))
    LT_BUILD_MODE := $(strip $(LT_BUILD_MODE))
else
    LT_BUILD_MODE := release
endif

# set default values for LT_VERSION_PLID, LT_VERSION_MAJOR, LT_VERSION_MINOR, LT_VERSION_PATCH, and LT_VERSION_BUILD_NUMBER if not set
ifeq (,$(LT_VERSION_PLID))
    LT_VERSION_PLID := 00
endif
ifeq (,$(LT_VERSION_MAJOR))
    LT_VERSION_MAJOR := 7
endif
ifeq (,$(LT_VERSION_MINOR))
    LT_VERSION_MINOR := 2
endif
ifeq (,$(LT_VERSION_PATCH))
    LT_VERSION_PATCH := 9
endif
ifeq (,$(LT_VERSION_TYPE))
    LT_VERSION_TYPE := S
endif
ifeq (,$(LT_VERSION_BUILD_NUMBER))
    # if there is no LT_VERSION_BUILD_NUMBER then it's a developer build.
    # We mark this by using a build number of 99999 to prevent SWUP updates and
    # by using the LT_VERSION_SIGNING_FLAG of X
    LT_IS_DEV_BUILD := 1
    LT_VERSION_BUILD_NUMBER := 99999
    LT_VERSION_SIGNING_FLAG := X
else
    LT_IS_DEV_BUILD := 0
endif
ifeq (,$(LT_VERSION_SIGNING_FLAG))
    LT_VERSION_SIGNING_FLAG := A
endif
LT_VERSION_COMMIT_LENGTH=8
LT_VERSION_COMMIT := $(or $(shell echo '$(LT_VERSION_COMMIT)' | head -c $(LT_VERSION_COMMIT_LENGTH)), $(shell git rev-parse --short=$(LT_VERSION_COMMIT_LENGTH) HEAD 2>/dev/null | head -c $(LT_VERSION_COMMIT_LENGTH)), $(shell for i in $(shell seq -s' ' $(LT_VERSION_COMMIT_LENGTH)); do echo -n 0; done))
ifeq (,$(LT_BUILD_TIME))
    LT_BUILD_TIME := $(shell date +%s)
endif

#######################################################################
#     ____ _                 _              ____
#    / ___| |__   __ _ _ __ | |_ ___ _ __  |___ \
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__|   __) |
#   | |___| | | | (_| | |_) | ||  __/ |     / __/
#    \____|_| |_|\__,_| .__/ \__\___|_|    |_____|
#                     |_|
#######################################################################
# CHAPTER 2 - print out our banner.  Not really a chapter but such a cool
#             banner it gets its own chapter - we print the banner before
#             validating that the roots, products, and variants are set
#             to valid values so that the user can see what the values
#             are set to before the makefile errors out in any check.
#
# First see if we should suppress the banner - we supress it if the special
# listlibraries or saveconfig targets are in MAKECMDGOALS
LT_BANNER ?= yes
LT_HIDE_BUILDTOOL :=
LT_BUILDTOOL_CMD :=
ifeq (listlibraries,$(findstring listlibraries,$(MAKECMDGOALS)))
    LT_BANNER := no
    LT_HIDE_BUILDTOOL := > /dev/null 2>&1
    LT_BUILDTOOL_CMD := @
endif
LT_SAVECONFIG := no
ifeq (saveconfig,$(findstring saveconfig,$(MAKECMDGOALS)))
    LT_BANNER := no
    LT_SAVECONFIG := yes
    LT_HIDE_BUILDTOOL := > /dev/null 2>&1
    LT_BUILDTOOL_CMD := @
endif

ifeq (yes,$(LT_BANNER))
LT_    :=
$(info $(LT_))
$(info $(LT_)______________________________________________________________________________)
$(info $(LT_)______________________________________________________________________________)
$(info $(LT_)==============================================================================)
$(info $(LT_)      ____)
$(info $(LT_)     / __ \ ____   __ __ __  __   __ _______)
$(info $(LT_)    / /_/ // __ \ / //_// / / /  / //__  __/)
$(info $(LT_)   / _, _// /_/ // ,<  / /_/ /  / /_  / /)
$(info $(LT_)  /_/ |_| \____//_/|_\ \__,_/  /____ /_/)
$(info $(LT_)                        Master Makefile)
$(info $(LT_)Roku LT OS)
$(info $(LT_)Copyright 2020-2024 Roku, Inc. All rights reserved)
$(info $(LT_)______________________________________________________________________________)
$(info $(LT_)______________________________________________________________________________)
$(info $(LT_)                      LT_OS_ROOT = $(LT_OS_ROOT))
$(info $(LT_)                 LT_PRODUCT_ROOT = $(if $(LT_PRODUCT_ROOT),$(LT_PRODUCT_ROOT),undefined))
$(info $(LT_)                LT_PLATFORM_ROOT = $(if $(LT_PLATFORM_ROOT),$(LT_PLATFORM_ROOT),undefined))
$(info $(LT_)                  LT_TARGET_ROOT = $(if $(LT_TARGET_ROOT),$(LT_TARGET_ROOT),undefined))
ifneq (,$(LT_USER_ROOT))
    $(info $(LT_)                    LT_USER_ROOT = $(if $(LT_USER_ROOT),$(LT_USER_ROOT),undefined))
endif
ifneq (cleanall, $(MAKECMDGOALS))
    $(info $(LT_)                      LT_PRODUCT = $(if $(LT_PRODUCT),$(LT_PRODUCT),undefined))
    $(info $(LT_)                     LT_PLATFORM = $(if $(LT_PLATFORM),$(LT_PLATFORM),undefined))
    $(info $(LT_)                   LT_BUILD_MODE = $(if $(LT_BUILD_MODE),$(LT_BUILD_MODE),undefined))
endif
    $(info $(LT_)==============================================================================)
endif


#######################################################################
#     ____ _                 _              _____
#    / ___| |__   __ _ _ __ | |_ ___ _ __  |___ /
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__|   |_ \
#   | |___| | | | (_| | |_) | ||  __/ |     ___) |
#    \____|_| |_|\__,_| .__/ \__\___|_|    |____/
#                     |_|
#######################################################################
# CHAPTER 3 - validate all of our input variables -
#              LT_PRODUCT_ROOT, LT_PLATFORM_ROOT, LT_TARGET_ROOT,
#              LT_PRODUCT, LT_PLATFORM, LT_BUILD_MODE,
#              LT_VERSION_PLID, LT_VERSION_MAJOR, LT_VERSION_MINOR, LT_VERSION_PATCH, LT_VERSION_BUILD_NUMBER
###

# LT_PLATFORM_ROOT - do this first
# First determine if there is an error as defined by:
#  (a) LT_PLATFORM_ROOT not being set or
#  (b) LT_PLATFORM_ROOT being set to a nonexistent path or a path that does not contain any build/platform/* subdirs with a Makefile.config file in them
LT_ERROR := no
ifeq (,$(LT_PLATFORM_ROOT))
    LT_ERROR := yes
else
    ifeq (yes, $(shell if [ -d $(LT_PLATFORM_ROOT)/build/platform ] ; then echo yes; else echo no; fi;))
        LT_VALID_PLATFORM_ROOT := $(shell find $(LT_PLATFORM_ROOT)/build/platform -maxdepth 2 -type f -path "*" -name Makefile.config)
        ifeq (,$(LT_VALID_PLATFORM_ROOT))
            LT_ERROR := yes
        endif
    else
        LT_ERROR := yes
    endif
endif
# if there is an error with LT_PLATFORM_ROOT, be helpful and tell the user what it was and how to rectify
ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (, $(LT_PLATFORM_ROOT))
        LT_OUT := Error: LT_PLATFORM_ROOT is undefined.
    else
        LT_OUT := Error: "$(LT_PLATFORM_ROOT)" is not a valid platform root.
    endif
    LT_OUT := $(info $(LT_OUT))
    # gather all the roots that have build/platform/ dirs
    LT_KNOWN_PLATFORM_ROOTS := $(foreach LT_PLATFORM_ROOT, $(strip $(shell find $(LT_ROOTS_BASE)/products -maxdepth 5 -type f -path \*/build/platform/\* -name Makefile.config)), $(abspath $(dir $(LT_PLATFORM_ROOT))/../../..))
    LT_KNOWN_PLATFORM_ROOTS += $(foreach LT_PLATFORM_ROOT, $(strip $(shell find $(LT_ROOTS_BASE)/platforms -maxdepth 5 -type f -path \*/build/platform/\* -name Makefile.config)), $(abspath $(dir $(LT_PLATFORM_ROOT))/../../..))
    ifeq (yes, $(shell if [ -d $(LT_OS_ROOT)/build/platform ] ; then echo yes; else echo no; fi;))
        LT_KNOWN_PLATFORM_ROOTS += $(LT_OS_ROOT)
    endif
    LT_KNOWN_PLATFORM_ROOTS := $(sort $(LT_KNOWN_PLATFORM_ROOTS))
    ifneq (, $(LT_KNOWN_PLATFORM_ROOTS))
        LT_OUT := $(info $(LT_))
        LT_OUT := $(info Known platform roots are:)
        LT_OUT := $(foreach LT_PLATFORM_ROOT, $(LT_KNOWN_PLATFORM_ROOTS), $(info $(LT_) $(LT_) $(LT_PLATFORM_ROOT)))
    endif
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_PLATFORM_ROOT to a valid platform root and reinvoke make)
endif

LT_VALID_VERSION_TYPES := S E

#LT_PLATFORM - much easier to verify now that we know we have a good platform root
LT_ERROR:=no
ifeq (,$(LT_PLATFORM))
    LT_ERROR := yes
else
    ifeq (no, $(shell if [ -f $(LT_PLATFORM_ROOT)/build/platform/$(LT_PLATFORM)/Makefile.config ] ; then echo yes; else echo no; fi;))
        ifeq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
            LT_ERROR := yes
        else
            ifeq (no, $(shell if [ -f $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/platform/$(LT_PLATFORM)/Makefile.config ] ; then echo yes; else echo no; fi;))
                LT_ERROR := yes
            endif
        endif
    endif
endif

# if there is an error with LT_PLATFORM, be helpful and tell the user what it was and how to rectify
ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (, $(LT_PLATFORM))
        LT_OUT := Error: LT_PLATFORM is undefined.
    else
        LT_OUT := Error: "$(LT_PLATFORM)" is not a valid platform variant.
    endif
    LT_OUT := $(info $(LT_OUT))
    # gather the list of valid platforms under this platform root
    LT_KNOWN_PLATFORMS := $(foreach LT_PLATFORM, $(strip $(shell find $(LT_PLATFORM_ROOT)/build/platform -maxdepth 2 -type f -path "*" -name Makefile.config)), $(shell basename $(dir $(LT_PLATFORM))))
    LT_KNOWN_PLATFORMS := $(sort $(LT_KNOWN_PLATFORMS))
    ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
      ifeq (yes, $(shell if [ -d $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/platform ] ; then echo yes; else echo no; fi;))
        LT_KNOWN_PLATFORMS2 += $(foreach LT_PLATFORM, $(strip $(shell find $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/platform -maxdepth 2 -type f -path "*" -name Makefile.config)), $(shell basename $(dir $(LT_PLATFORM))))
        LT_KNOWN_PLATFORMS2 := $(sort $(LT_KNOWN_PLATFORMS2))
        LT_KNOWN_PLATFORMS += $(LT_KNOWN_PLATFORMS2)
      endif
    endif
    LT_KNOWN_PLATFORMS  := $(patsubst _template%,,$(LT_KNOWN_PLATFORMS))
    ifneq (, $(LT_KNOWN_PLATFORMS))
        LT_OUT := $(info $(LT_))
        LT_OUT := $(info Known LT_PLATFORM variants for the current platform root are:)
        LT_OUT := $(foreach LT_PLATFORM, $(LT_KNOWN_PLATFORMS), $(info $(LT_) $(LT_) $(LT_PLATFORM)))
    endif
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_PLATFORM to a valid platform variant and reinvoke make)
endif

# LT_PRODUCT_ROOT
# First determine if there is an error as defined by:
#  (a) LT_PRODUCT_ROOT not being set or
#  (b) LT_PRODUCT_ROOT being set to a nonexistent path or a path that does not contain any build/product/* subdirs with a Makefile.config file in them
LT_ERROR := no
ifeq (,$(LT_PRODUCT_ROOT))
    LT_ERROR := yes
else
    ifeq (yes, $(shell if [ -d $(LT_PRODUCT_ROOT)/build/product ] ; then echo yes; else echo no; fi;))
        LT_VALID_PRODUCT_ROOT := $(shell find $(LT_PRODUCT_ROOT)/build/product -maxdepth 2 -type f -path "*" -name LTProductConfig.json)
        ifeq (,$(LT_VALID_PRODUCT_ROOT))
            LT_ERROR := yes
        endif
    else
      LT_ERROR := yes
    endif
endif

# if there is an error with LT_PRODUCT_ROOT, be helpful and tell the user what it was and how to rectify
ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (, $(LT_PRODUCT_ROOT))
        LT_OUT := Error: LT_PRODUCT_ROOT is undefined.
    else
        LT_OUT := Error: "$(LT_PRODUCT_ROOT)" is not a valid product root.
    endif
    LT_OUT := $(info $(LT_OUT))
    # gather all the roots that have build/product/ dirs
    LT_KNOWN_PRODUCT_ROOTS := $(foreach LT_PRODUCT_ROOT, $(strip $(shell find $(LT_ROOTS_BASE)/products -maxdepth 5 -type f -path \*/build/product/\* -name LTProductConfig.json)), $(abspath $(dir $(LT_PRODUCT_ROOT))/../../..))
    LT_KNOWN_PRODUCT_ROOTS += $(foreach LT_PRODUCT_ROOT, $(strip $(shell find $(LT_ROOTS_BASE)/platforms -maxdepth 5 -type f -path \*/build/product/\* -name LTProductConfig.json)), $(abspath $(dir $(LT_PRODUCT_ROOT))/../../..))
    ifeq (yes, $(shell if [ -d $(LT_OS_ROOT)/build/product ] ; then echo yes; else echo no; fi;))
        LT_KNOWN_PRODUCT_ROOTS += $(LT_OS_ROOT)
    endif
    LT_KNOWN_PRODUCT_ROOTS := $(sort $(LT_KNOWN_PRODUCT_ROOTS))
    ifneq (, $(LT_KNOWN_PRODUCT_ROOTS))
        LT_OUT := $(info $(LT_))
        LT_OUT := $(info Known product roots are:)
        LT_OUT := $(foreach LT_PRODUCT_ROOT, $(LT_KNOWN_PRODUCT_ROOTS), $(info $(LT_) $(LT_) $(LT_PRODUCT_ROOT)))
    endif
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_PRODUCT_ROOT to a valid product root and reinvoke make)
endif

#LT_PRODUCT - much easier to verify now that we know we have a good product root
LT_ERROR:=no
ifeq (,$(LT_PRODUCT))
    LT_ERROR := yes
else
    ifeq (no, $(shell if [ -f $(LT_PRODUCT_ROOT)/build/product/$(LT_PRODUCT)/LTProductConfig.json ] ; then echo yes; else echo no; fi;))
        ifeq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
            LT_ERROR := yes
        else
            ifeq (no, $(shell if [ -f $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/product/$(LT_PRODUCT)/LTProductConfig.json ] ; then echo yes; else echo no; fi;))
                LT_ERROR := yes
            endif
        endif
    endif
endif

# if there is an error with LT_PRODUCT, be helpful and tell the user what it was and how to rectify
ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (, $(LT_PRODUCT))
        LT_OUT := Error: LT_PRODUCT is undefined.
    else
        LT_OUT := Error: "$(LT_PRODUCT)" is not a valid product variant.
    endif
    LT_OUT := $(info $(LT_OUT))
    # gather the list of valid products under this product root
    LT_KNOWN_PRODUCTS := $(foreach LT_PRODUCT, $(strip $(shell find $(LT_PRODUCT_ROOT)/build/product -maxdepth 2 -type f -path "*" -name LTProductConfig.json)), $(shell basename $(dir $(LT_PRODUCT))))
    LT_KNOWN_PRODUCTS := $(sort $(LT_KNOWN_PRODUCTS))
    ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
        ifneq ($(LT_PRODUCT_ROOT),$(LT_USER_FIRMWARE_PROJECT_ROOT))
          ifeq (yes, $(shell if [ -d $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/product ] ; then echo yes; else echo no; fi;))
            LT_KNOWN_PRODUCTS2 += $(foreach LT_PRODUCT, $(strip $(shell find $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/product -maxdepth 2 -type f -path "*" -name LTProductConfig.json)), $(shell basename $(dir $(LT_PRODUCT))))
            LT_KNOWN_PRODUCTS2 := $(sort $(LT_KNOWN_PRODUCTS2))
            LT_KNOWN_PRODUCTS += $(LT_KNOWN_PRODUCTS2)
          endif
      endif
    endif
    ifneq (, $(LT_KNOWN_PRODUCTS))
        LT_OUT := $(info $(LT_))
        LT_OUT := $(info Known LT_PRODUCT variants for the current product root are:)
        LT_OUT := $(foreach LT_PRODUCT, $(LT_KNOWN_PRODUCTS), $(info $(LT_) $(LT_) $(LT_PRODUCT)))
    endif
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_PRODUCT to a valid product variant and reinvoke make)
endif

#LT_TARGET_ROOT
ifneq (printvar-,$(findstring printvar-,$(MAKECMDGOALS)))
    ifeq (no, $(shell if [ -d $(LT_TARGET_ROOT) ] ; then echo yes; else echo no; fi;))
        LT_OUT := $(info $(LT_))
        LT_OUT := $(info Error: The LT_TARGET_ROOT directory, "$(LT_TARGET_ROOT)", does not exist.)
        LT_OUT := $(info $(LT_))
        LT_OUT := $(error Please set LT_TARGET_ROOT to an existing directory and reinvoke make)
    endif
endif

#LT_BUILD_MODE
LT_ERROR:=no
LT_VALID_BUILD_MODES := debug release
ifeq (,$(LT_BUILD_MODE))
    LT_ERROR := yes
else
    ifneq ($(LT_BUILD_MODE), $(findstring $(LT_BUILD_MODE), $(LT_VALID_BUILD_MODES)))
        LT_ERROR := yes
    endif
endif

ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (,$(LT_BUILD_MODE))
        LT_OUT := Error: LT_BUILD_MODE is undefined.
    else
        LT_OUT := Error: "$(LT_BUILD_MODE)" is not a valid build mode..
    endif
    LT_OUT := $(info $(LT_OUT))
    LT_OUT := $(info $(LT_))
    LT_OUT := $(info Valid build modes are:)
    LT_OUT := $(foreach LT_BUILD_MODE, $(LT_VALID_BUILD_MODES), $(info $(LT_) $(LT_) $(LT_BUILD_MODE)))
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_BUILD_MODE to a valid build mode and reinvoke make)
endif

ValidateBase31 = echo $(1) | egrep -q ^[0-9AC-HJ-NPR-Y]{$(2)}$$ ; echo $$?

# validate LT_VERSION_PLID, LT_VERSION_MAJOR, LT_VERSION_MINOR, LT_VERSION_PATCH, LT_VERSION_BUILD_NUMBER
LT_ERROR:=no
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_PLID
    ifneq (0, $(shell $(call ValidateBase31,$(LT_VERSION_PLID),2)))
        # set LT_ERROR with the name of the variable that failed
        LT_ERROR := LT_VERSION_PLID
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_MAJOR
    ifneq (0, $(LT_VERSION_MAJOR))
        # if non-zero, must be max of 30 (one base-31 digit); the >0 test is to filter out string values
        ifeq (0, $(shell echo "$(LT_VERSION_MAJOR) > 0 && $(LT_VERSION_MAJOR) <= 30" | bc))
            # set LT_ERROR with the name of the variable that failed
            LT_ERROR := LT_VERSION_MAJOR
        endif
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_MINOR
    ifneq (0, $(LT_VERSION_MINOR))
        # if non-zero, must be max of 30 (one base-31 digit); the >0 test is to filter out string values
        ifeq (0, $(shell echo "$(LT_VERSION_MINOR) > 0 && $(LT_VERSION_MINOR) <= 30" | bc))
            # set LT_ERROR with the name of the variable that failed
            LT_ERROR := LT_VERSION_MINOR
        endif
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_PATCH
    ifneq (0, $(LT_VERSION_PATCH))
        # if non-zero, must be max of 30 (one base-31 digit); the >0 test is to filter out string values
        ifeq (0, $(shell echo "$(LT_VERSION_PATCH) > 0 && $(LT_VERSION_PATCH) <= 30" | bc))
            # set LT_ERROR with the name of the variable that failed
            LT_ERROR := LT_VERSION_PATCH
        endif
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_TYPE
    ifeq ($(filter $(LT_VERSION_TYPE), $(LT_VALID_VERSION_TYPES)),)
        # set LT_ERROR with the name of the variable that failed
        LT_ERROR := LT_VERSION_TYPE
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_BUILD_NUMBER
    ifneq (0, $(LT_VERSION_BUILD_NUMBER))
        # if non-zero, must be max five digits; the >0 test is to filter out string values
        ifeq (0, $(shell echo "$(LT_VERSION_BUILD_NUMBER) > 0 && $(LT_VERSION_BUILD_NUMBER) <= 99999" | bc))
            # set LT_ERROR with the name of the variable that failed
            LT_ERROR := LT_VERSION_BUILD_NUMBER
        endif
    endif
endif
ifeq (no, $(LT_ERROR))
    # validate LT_VERSION_COMMIT
    # must be LT_VERSION_COMMIT_LENGTH hex characters
    ifneq (valid, $(shell if expr $(LT_VERSION_COMMIT) : '^[0-9a-fA-F]\{$(LT_VERSION_COMMIT_LENGTH)\}$$' >/dev/null ; then echo valid; fi))
        # set LT_ERROR with the name of the variable that failed
        LT_ERROR := LT_VERSION_COMMIT
    endif
endif
ifeq (no, $(LT_ERROR))
    ifneq (,$(LT_CRYPTO_KEY_DIR))
        # validate LT_CRYPTO_KEY_DIR (folder must exist)
        ifneq (REMOTE, $(LT_CRYPTO_KEY_DIR))
            ifeq (no, $(shell if [ -d $(LT_CRYPTO_KEY_DIR) ] ; then echo yes; else echo no; fi;))
                LT_OUT := $(info $(LT_))
                LT_OUT := $(info Error: The LT_CRYPTO_KEY_DIR directory, "$(LT_CRYPTO_KEY_DIR)", does not exist.)
                LT_OUT := $(info $(LT_))
                LT_OUT := $(error Please set LT_CRYPTO_KEY_DIR to an existing directory or clear and reinvoke make)
                LT_ERROR := LT_CRYPTO_KEY_DIR
            endif
        endif
    endif
endif
ifneq (no, $(LT_ERROR))
# we have an error, first print out the major, minor build numbers as if we had already in the banner
# they are overkill so, we only print them out like this if there was an error so the user can see
# what the values were
LT_OUT := $(info $(LT_)                 LT_VERSION_PLID = $(LT_VERSION_PLID))
LT_OUT := $(info $(LT_)                LT_VERSION_MAJOR = $(LT_VERSION_MAJOR))
LT_OUT := $(info $(LT_)                LT_VERSION_MINOR = $(LT_VERSION_MINOR))
LT_OUT := $(info $(LT_)                LT_VERSION_PATCH = $(LT_VERSION_PATCH))
LT_OUT := $(info $(LT_)                 LT_VERSION_TYPE = $(LT_VERSION_TYPE))
LT_OUT := $(info $(LT_)         LT_VERSION_BUILD_NUMBER = $(LT_VERSION_BUILD_NUMBER))
LT_OUT := $(info $(LT_)               LT_VERSION_COMMIT = $(LT_VERSION_COMMIT))
LT_OUT := $(info $(LT_))
LT_OUT := $(info Error: "$($(LT_ERROR))" is an invalid value for $(LT_ERROR))
LT_OUT := $(info $(LT_))
ifeq (LT_VERSION_PLID, $(LT_ERROR))
    LT_OUT := $(info LT_VERSION_PLID must be a two digit base-31 encoded value)
else
    ifeq (LT_VERSION_MAJOR, $(LT_ERROR))
        LT_OUT := $(info LT_VERSION_MAJOR must be in the range of 0 to 30)
    else
        ifeq (LT_VERSION_MINOR, $(LT_ERROR))
            LT_OUT := $(info LT_VERSION_MINOR must be in the range of 0 to 30)
        else
            ifeq (LT_VERSION_PATCH, $(LT_ERROR))
                LT_OUT := $(info LT_VERSION_PATCH must be in the range of 0 to 30)
            else
                ifeq (LT_VERSION_TYPE, $(LT_ERROR))
                    LT_OUT := $(info LT_VERSION_TYPE must be one of $(LT_VALID_VERSION_TYPES))
                else
                    ifeq (LT_VERSION_BUILD_NUMBER, $(LT_ERROR))
                        LT_OUT := $(info LT_VERSION_BUILD_NUMBER must be in the range of 0 to 99999)
                    else
                        ifeq (LT_VERSION_COMMIT, $(LT_ERROR))
                            LT_OUT := $(info LT_VERSION_COMMIT must be $(LT_VERSION_COMMIT_LENGTH) hexadecimal characters)
                        endif
                    endif
                endif
            endif
        endif
    endif
endif
LT_OUT := $(info $(LT_))
LT_OUT := $(error Please set $(LT_ERROR) to a valid value and reinvoke make)
endif

#######################################################################
#     ____ _                 _              _  _
#    / ___| |__   __ _ _ __ | |_ ___ _ __  | || |
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__| | || |_
#   | |___| | | | (_| | |_) | ||  __/ |    |__   _|
#    \____|_| |_|\__,_| .__/ \__\___|_|       |_|
#                     |_|
#######################################################################
# CHAPTER 4 - Now that externally supplied variables are validated,
#             a. compute the build version string, parameterize our build
#                with special mkcmdgoals directives, print version and mkcmdgoals
#                and finalize the banner.
#             b. then initialize internal variables used in target generation.
#             c. read platform and product Makefile.config files

# a. COMPUTE LT_BUILD_VERSION,
#    PULL AND SET UP SPECIAL TARGET DIRECTIVES FROM MKCMDGOALS,
#    PRINT BUILD VERSION & MKCMDGOALS AND FINALIZE BANNER

# compute LT_BUILD_VERSION

# Detect the host OS for date formatting — macOS BSD date uses -r<epoch>,
# GNU/Linux date uses --date='@<epoch>'. We cannot use LT_PLATFORM_FAMILY
# because that reflects the TARGET platform, not the build host.
LT_HOST_OS := $(shell uname -s)

ifeq (Darwin, $(LT_HOST_OS))
    LT_VERSION_DATE             := $(shell date -r $(LT_BUILD_TIME) +%Y%m%d)
else
    LT_VERSION_DATE             := $(shell date --date='@$(LT_BUILD_TIME)' +%Y%m%d)
endif
ifeq ($(LT_PRODUCT), $(LT_PRODUCT_FAMILY))
    LT_VERSION_PRODUCT      := $(LT_PRODUCT)
else
    LT_VERSION_PRODUCT      := $(LT_PRODUCT_FAMILY).$(LT_PRODUCT)
endif
ifeq ($(LT_PLATFORM), $(LT_PLATFORM_FAMILY))
    LT_VERSION_PLATFORM     := $(LT_PLATFORM)
else
    LT_VERSION_PLATFORM     := $(LT_PLATFORM_FAMILY).$(LT_PLATFORM)
endif

# if we are a dev build, fold in local time of day into version date so developers can use it to easily
# verify that a build on the running target is or is not what they just built and intended to flash
ifeq (1,$(LT_IS_DEV_BUILD))
    ifeq (Darwin, $(LT_HOST_OS))
        LT_MERIDIAN := $(shell date -r$(LT_BUILD_TIME) +%p)
    else
        LT_MERIDIAN := $(shell date --date='@$(LT_BUILD_TIME)' +%p)
    endif
    ifeq (PM,$(LT_MERIDIAN))
        LT_MERIDIAN := pm
    else
        ifeq (AM,$(LT_MERIDIAN))
            LT_MERIDIAN := am
        endif
    endif
    ifeq (Darwin, $(LT_HOST_OS))
        LT_VERSION_DATE := $(LT_VERSION_DATE)_$(shell date -r$(LT_BUILD_TIME) +%I:%M${LT_MERIDIAN}.%Z)
    else
        LT_VERSION_DATE := $(LT_VERSION_DATE)_$(shell date --date='@$(LT_BUILD_TIME)' +%I:%M${LT_MERIDIAN}.%Z)
    endif
endif

BASE31_CHARS := 0 1 2 3 4 5 6 7 8 9 A C D E F G H J K L M N P R S T U V W X Y
LT_VERSION_MAJOR_BASE31        := $(word $(shell echo "$(LT_VERSION_MAJOR) + 1" | bc), $(BASE31_CHARS))
LT_VERSION_MINOR_BASE31        := $(word $(shell echo "$(LT_VERSION_MINOR) + 1" | bc), $(BASE31_CHARS))
LT_VERSION_PATCH_BASE31        := $(word $(shell echo "$(LT_VERSION_PATCH) + 1" | bc), $(BASE31_CHARS))
LT_VERSION_BUILD_NUMBER_PADDED := $(shell printf "%05d" $(LT_VERSION_BUILD_NUMBER))

LT_VERSION_PRODUCT_PLATFORM_DATE := $(LT_VERSION_PRODUCT)-$(LT_VERSION_PLATFORM)-$(LT_VERSION_DATE)
ifeq (debug, $(LT_BUILD_MODE))
    LT_VERSION_PRODUCT_PLATFORM_DATE := $(LT_VERSION_PRODUCT_PLATFORM_DATE)-DEBUG
endif

LT_SOFTWARE_VERSION             := $(LT_VERSION_PLID)$(LT_VERSION_MAJOR_BASE31).$(LT_VERSION_MINOR_BASE31)$(LT_VERSION_PATCH_BASE31)$(LT_VERSION_TYPE)$(LT_VERSION_BUILD_NUMBER_PADDED)$(LT_VERSION_SIGNING_FLAG)
LT_BUILD_VERSION                := RokuLT-$(LT_SOFTWARE_VERSION)-$(LT_VERSION_COMMIT)-$(LT_VERSION_PRODUCT_PLATFORM_DATE)
LT_PLATFORM_DISTRO_VERSION      := RokuLT-$(LT_SOFTWARE_VERSION)-$(LT_VERSION_DATE)-$(LT_PLATFORM_FAMILY)

LT_OPENSOURCE_BUILD_NUMBER                  ?= 001
ifeq (Darwin, $(LT_HOST_OS))
LT_OPENSOURCE_VERSION                       := $(LT_VERSION_MAJOR_BASE31).$(LT_VERSION_MINOR_BASE31).$(LT_OPENSOURCE_BUILD_NUMBER).$(shell date +%Y%m%d)
else
LT_OPENSOURCE_VERSION                       := $(LT_VERSION_MAJOR_BASE31).$(LT_VERSION_MINOR_BASE31).$(LT_OPENSOURCE_BUILD_NUMBER).$(shell date --date= +%Y%m%d)
endif
LT_OPENSOURCE_DISTRO_VERSION                := RokuLT-$(LT_OPENSOURCE_VERSION)-opensource_distro
LT_OPENSOURCE_DISTRO_VERSION_LT             := $(LT_OPENSOURCE_DISTRO_VERSION)_lt
LT_OPENSOURCE_DISTRO_VERSION_LT_TOOLCHAINS  := $(LT_OPENSOURCE_DISTRO_VERSION)_lt-toolchains
LT_SDK_CEO_DISTRO_VERSION                   := RokuLT-SDK-CEO1.$(LT_OPENSOURCE_VERSION)

# BEGIN BANNER DEBUGGING - INVOCATION (BANNER) DEBUGGING is enabled when LT_DEBUG_BANNER_INVOCATION is yes; always checkin with it set to no!
LT_DEBUG_BANNER_INVOCATION := no
#                          setting LT_DEBUG_BANNER_INVOCATION to yes can help one understand what is going on
#                          if problems are introduced into the machinery of invoking build tool makes, the turnaround
#                          makes of the LTLibraries they use, and the machinery of using the platform variant's
#                          DeviceConfig.json and the product variant's ProductConfig.json to specify the libraries
#                          to be incoporated in any specific platform variant / product variant build combination.
#
#                          If LT_DEBUG_BANNER_INVOCATION is set to yes then invocation, banner, and meta-goal information
#                          for each invocation of this Makefile will be appended to /tmp/make.out both before and after the
#                          meta-goal processing occurs.
#
#                          FYI, this makefile can invoke itself for three reasons:
#                            1. This makefile may invoke itself to build any required buildtools.
#                            2. The make of each buildtool turns around and invokes this makefile to build the LTLibraries it uses.
#                            3. This makefile restarts itself whenever the arbolator generates new or updated Makefile include files
#                               from the platform variant's DeviceConfig.json file and the product variant's ProductConfig.json file.
#                               The restart mechanism is an automatic feature of make that is triggered whenever execution of a target
#                               updates any included makefile and is the make workaround for dynamically generating build targets based
#                               on the execution of a build target since once make starts executing targets, they are fixed.
#
ifeq (yes, $(LT_DEBUG_BANNER_INVOCATION))
    MAKE_LOG_FILE := /tmp/make.out
    MAKE_LOG_SECS := $(shell date +%s)
    ifeq (Darwin, $(LT_HOST_OS))
        MAKE_LOG_DATE := $(shell date +%b\ %d,\ %Y\ \ %I:%M%P\ %\#Z -r$(MAKE_LOG_SECS))
    else
        MAKE_LOG_DATE := $(shell date +%b\ %d,\ %Y\ \ %I:%M%P\ %\#Z --date='@$(MAKE_LOG_SECS)')
    endif
    $(shell echo "________________________" >> $(MAKE_LOG_FILE))
    $(shell echo "make pre-meta/banner  at   $(MAKE_LOG_DATE)" >> $(MAKE_LOG_FILE))
    $(shell echo "MAKECMDGOALS               = $(MAKECMDGOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_RUN_ARBOLATOR           = $(LT_RUN_ARBOLATOR)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_CURRENTLY_BUILDING_TOOL = $(LT_CURRENTLY_BUILDING_TOOL)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_BANNER                  = $(LT_BANNER)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_BANNER_GOALS            = $(LT_BANNER_GOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_META_GOALS              = $(LT_META_GOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_META_DEPS               = $(LT_META_DEPS)" >> $(MAKE_LOG_FILE))
endif
# END BANNER DEBUGGING
######################

# LT_META_GOALS
#
# Meta goals that aren't actual targets like quiet and notstats preclude make from adding an
# implicit 'all' target, but we would like to simulate one so that we can have 'make quiet'
# be equivalent to 'make quiet all' just as 'make' is equivalent to 'make all'.
#
# We detect when we want to add in our simulated implicit 'all' target by harvesting the
# meta-goals out of MAKECMDGOALS, placing them into LT_META_GOALS.  When we're done, if
# LT_META_GOALS is non-empty and MAKECMDGOALS is empty, we know we need to simulate the
# 'all' target because make itself will not.
#
# We do this by adding 'all' as a dependency of the meta goals, but take care not to add
# it when make is gong to run it explictly anyway - so that "make quiet all" doesn't
# run all twice.
#
# get ready to harvest the meta-goals
LT_META_GOALS :=
LT_META_DEPS :=

# harvest the meta-goals from MAKECMDGOALS
ifeq (dumpvars,$(findstring dumpvars,$(MAKECMDGOALS)))
    LT_META_GOALS += dumpvars
    MAKECMDGOALS := $(strip $(subst dumpvars,,$(MAKECMDGOALS)))
    LT_DUMPVARS:=yes
else
    LT_DUMPVARS:=no
endif

ifeq (nostats,$(findstring nostats,$(MAKECMDGOALS)))
    LT_META_GOALS += nostats
    MAKECMDGOALS := $(strip $(subst nostats,,$(MAKECMDGOALS)))
    LT_DISABLE_STATS:=yes
else
    LT_DISABLE_STATS:=no
endif

# The quiet directive is both meta-goal that is also passed into and along build tools make chains.
# Respect the LT_QUIET value, validating it and adopting it as the default quiet specificier,
# allowing the quiet meta-goal to override it.  LT_QUIET can be 0 or 1; 0 is the default
ifneq (1, $(LT_QUIET))
    LT_QUIET := 0
endif
ifeq (quiet, $(findstring quiet,$(MAKECMDGOALS)))
    LT_QUIET := 1
endif
ifeq (1, $(LT_QUIET))
    LT_META_GOALS += quiet
    MAKECMDGOALS := $(strip $(subst quiet,,$(MAKECMDGOALS)))

    LT_QUIET_CMD        :=
    LT_EXEC_CMD         := @
    LT_GNU_QUIET_ARG    := --quiet
    LT_UNZIP_FLAGS      := -q
    LT_ZIP_QUIET_FLAG   := -q
    LT_ARROWLINE        := @echo > /dev/null
else
    LT_QUIET_CMD        := @echo > /dev/null
    LT_EXEC_CMD         :=
    LT_GNU_QUIET_ARG    :=
    LT_UNZIP_FLAGS      :=
    LT_ZIP_QUIET_FLAG   :=
    LT_ARROWLINE        := @echo
endif

# set default target
.DEFAULT_GOAL := all

# test MAKECMDGOALS for empty
MAKECMDGOALS := $(strip $(MAKECMDGOALS))
LT_META_GOALS := $(strip $(LT_META_GOALS))
ifeq (, $(MAKECMDGOALS))
    # MAKECMDGOALS is empty.  Either it was from time of invocation or we made it empty by stripping out all meta goals.

    # set MAKECMDGOALS to 'all'.  This has no effect on the make but we'd like to see it print in our banner, even when implicit.
    MAKECMDGOALS := all

    # set LT_META_DEPS to 'all' - if we found meta goals this will trigger all; if we found no meta-goals it won't trigger, but make has 'all' as an implicit target and will run it
    LT_META_DEPS := all
endif

ifeq (,$(LT_CURRENTLY_BUILDING_TOOL))
  ifeq (yes, $(LT_DUMPVARS))
      LT_BANNER_GOALS := dumpvars
  else
      LT_BANNER_GOALS := $(strip $(sort $(LT_META_GOALS)) $(MAKECMDGOALS))
  endif
else
  LT_BANNER_GOALS  := $(strip $(MAKECMDGOALS))
endif

ifneq (, $(LT_META_GOALS))
.PHONY: $(LT_META_GOALS)
$(LT_META_GOALS): $(LT_META_DEPS)
	@echo >/dev/null
endif

# 4B - SETUP INTERNAL VARIABLES

# check MAKECMDGOALS to see if we need to run the arbolator
LT_RUN_ARBOLATOR := 0
ifneq (,$(filter all images listlibraries rebuild, $(MAKECMDGOALS)))
    LT_RUN_ARBOLATOR := 1
endif

ifeq (,$(LT_BUILDTOOL_CMD))
    LT_BUILDTOOL_CMD := $(LT_EXEC_CMD)
endif
ifneq (,$(LT_CURRENTLY_BUILDING_TOOL))
    LT_ARROWLINE :=
endif

# FINALIZE THE BANNER
LT_BANNER_BUILDTOOLS := 0
ifeq (1, $(LT_RUN_ARBOLATOR))
   ifeq (no, $(LT_DUMPVARS))
       LT_BANNER_BUILDTOOLS := 1
   endif
endif

# BEGIN BANNER DEBUGGING (enabled if LT_DEBUG_BANNER_INVOCATION was set above to yes)
ifeq (yes, $(LT_DEBUG_BANNER_INVOCATION))
    $(shell echo "________________________" >> $(MAKE_LOG_FILE))
    $(shell echo "make banner post-meta at     $(MAKE_LOG_DATE)" >> $(MAKE_LOG_FILE))
    $(shell echo "MAKECMDGOALS               = $(MAKECMDGOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_RUN_ARBOLATOR           = $(LT_RUN_ARBOLATOR)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_CURRENTLY_BUILDING_TOOL = $(LT_CURRENTLY_BUILDING_TOOL)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_BANNER                  = $(LT_BANNER)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_BANNER_GOALS            = $(LT_BANNER_GOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_META_GOALS              = $(LT_META_GOALS)" >> $(MAKE_LOG_FILE))
    $(shell echo "LT_META_DEPS               = $(LT_META_DEPS)" >> $(MAKE_LOG_FILE))
endif
# END BANNER DEBUGGING

ifeq (yes,$(LT_BANNER))
    $(info $(LT_)==============================================================================)
    ifneq (cleanall, $(MAKECMDGOALS))
        $(info $(shell printf "%78s\n" "${LT_BUILD_VERSION}"))
    endif
        $(info $(LT_)                    $(LT_)MAKECMDGOALS =$(LT_) $(LT_BANNER_GOALS))
    ifeq (1, $(LT_BANNER_BUILDTOOLS))
    $(info ------------------------------------------------------------------------------)
        $(info $(LT_)                        $(LT_)first make buildtools)
    endif
    $(info ------------------------------------------------------------------------------)
endif

# SET UP INTERNAL VARIABLES FOR MAKE TARGET EXECUTION
LT_OS_PUBLIC_INCLUDE_DIR            := $(LT_OS_ROOT)/include
LT_OS_PRIVATE_SOURCE_DIR            := $(LT_OS_ROOT)/source
LT_PLATFORM_PUBLIC_INCLUDE_DIR      := $(LT_PLATFORM_ROOT)/include
LT_PLATFORM_THIRDPARTY_DIR          := $(LT_PLATFORM_ROOT)/thirdparty
LT_PLATFORM_PRIVATE_SOURCE_DIR      := $(LT_PLATFORM_ROOT)/source
LT_PRODUCT_PUBLIC_INCLUDE_DIR       := $(LT_PRODUCT_ROOT)/include
LT_PRODUCT_PRIVATE_SOURCE_DIR       := $(LT_PRODUCT_ROOT)/source
LT_OS_DOXYFILE                      := $(LT_OS_ROOT)/doc/Doxyfile
LT_SDK_SOURCE_DIR                   := $(LT_OS_ROOT)/doc/sdk
LT_BUILDTOOLS_ROOT                  := $(LT_OS_ROOT)/build/tools
LT_TARGET_DIR_BUILDTOOLS            := $(LT_TARGET_ROOT)/targets/buildtools
LT_TARGET_DIR_THIS_BUILD            := $(LT_TARGET_ROOT)/targets/$(LT_VERSION_PRODUCT)/$(LT_VERSION_PLATFORM)/$(LT_BUILD_MODE)
LT_TARGET_BIN_DIR                   := $(LT_TARGET_DIR_THIS_BUILD)/bin
LT_TARGET_LIB_DIR                   := $(LT_TARGET_DIR_THIS_BUILD)/lib
LT_TARGET_OBJ_DIR                   := $(LT_TARGET_DIR_THIS_BUILD)/obj
LT_TARGET_STATS_DIR                 := $(LT_TARGET_DIR_THIS_BUILD)/stats
LT_TARGET_BUILD_ARTIFACTS_DIR       := $(LT_TARGET_DIR_THIS_BUILD)/build-artifacts
LT_TARGET_EXAMINATE_DIR             := $(LT_TARGET_DIR_THIS_BUILD)/examinated
LT_TARGET_DOXYDOCS_DIR              := $(LT_TARGET_DIR_THIS_BUILD)/doc/apiref
LT_TARGET_DOXYDOCS_ERROR_FILE       := $(LT_TARGET_DOXYDOCS_DIR)/doxygen-errors.log
LT_TARGET_DOXYDOCS_INDEX_HTML       := $(LT_TARGET_DOXYDOCS_DIR)/html/index.html
LT_TARGET_SDKDOCS_DIR               := $(LT_TARGET_DIR_THIS_BUILD)/doc/sdk
LT_TARGET_SDKDOCS_ERROR_FILE        := $(LT_TARGET_SDKDOCS_DIR)/sphinx-errors.log
LT_TARGET_SDKDOCS_INDEX_HTML        := $(LT_TARGET_SDKDOCS_DIR)/html/index.html
LT_SDK_DISTRO_DIR_STAGING_NAME      := sdk-distro-staging
LT_SDK_DISTRO_DIR_STAGING_BASE      := $(LT_TARGET_OBJ_DIR)/$(LT_SDK_DISTRO_DIR_STAGING_NAME)
LT_SDK_DISTRO_DIR_STAGING           := $(LT_SDK_DISTRO_DIR_STAGING_BASE)/$(LT_SDK_CEO_DISTRO_VERSION)
LT_TARGET_DISTRO_DIR_BASE           := $(LT_TARGET_ROOT)/targets/distros
LT_SDK_DISTRO_ZIP_FILE_NAME         := $(LT_SDK_CEO_DISTRO_VERSION).zip
LT_SDK_DISTRO_ZIP_FILE              := $(LT_TARGET_DISTRO_DIR_BASE)/$(LT_SDK_DISTRO_ZIP_FILE_NAME)
LT_OPENSOURCE_DISTRO_TGZ_NAME       := $(LT_OPENSOURCE_DISTRO_VERSION)_lt.tgz
LT_OPENSOURCE_DISTRO_TGZ            := $(LT_TARGET_DISTRO_DIR_BASE)/$(LT_OPENSOURCE_DISTRO_TGZ_NAME)
LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ_NAME := $(LT_OPENSOURCE_DISTRO_VERSION)_lt-toolchains.tgz
LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ  := $(LT_TARGET_DISTRO_DIR_BASE)/$(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ_NAME)
LT_OPENSOURCE_TOOLS_DISTRO_TGZ_NAME := $(LT_OPENSOURCE_DISTRO_VERSION)_lt-commander.tgz
LT_OPENSOURCE_TOOLS_DISTRO_TGZ      := $(LT_TARGET_DISTRO_DIR_BASE)/$(LT_OPENSOURCE_TOOLS_DISTRO_TGZ_NAME)

# set tools to use for doxybuild, user can override in environment
LT_DOXYDOCS_DOXYGEN_TOOL            ?= doxygen
LT_DOXYDOCS_PREVIEW_BROWSER         ?= firefox
# set tools to use for SDK build, user can override in environment
LT_SDKDOCS_SPHINX_TOOL              ?= sphinx-build
LT_SDKDOCS_PREVIEW_BROWSER          ?= firefox
# set the architecture of the toolchain host (the machine running this makefile)
LT_TOOLCHAIN_HOST_ARCHITECTURE      := $(shell uname -m)
LT_TOOLCHAIN_HOST_OS_NAME           := $(shell uname -s)
# set default vendor toolchain root location
LT_VENDOR_TOOLCHAINS_DIR            ?= $(abspath $(HOME)/lt-vendor-toolchains)
# allow the caller to set default vendor toolchain tarball location
LT_VENDOR_TOOLCHAIN_TARBALLS_DIR    ?= $(abspath $(LT_OS_ROOT)/../../lt-toolchains)
# output of build info for TeamCity
LT_BUILDINFO_FILE                   ?= $(LT_TARGET_DIR_THIS_BUILD)/buildinfo
# output of version info for automation
LT_VERSIONINFO_FILE                 ?= $(LT_TARGET_BUILD_ARTIFACTS_DIR)/version.env
# Artifacts that are published to TeamCity.  The platform's Makefile.config may
# add platform-specific files to this list.  The list will unconditionally include
# the build-artifacts package specified below.
LT_PUBLISHED_ARTIFACTS              ?=
# set the os root build scripts directives
LT_OS_BUILD_SCRIPTS_DIR             := $(LT_OS_ROOT)/build/scripts
# set the os root project dir
LT_OS_BUILD_PROJECT_DIR             := $(LT_OS_ROOT)/build/project
# set the platform variant makefile dir and the platform root project dir, also legacy allow rules makefile override from LT_PLATFORM variant
ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
  LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR := $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/platform/$(LT_PLATFORM)
  ifeq (no, $(shell if [ -d $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR) ] ; then echo yes; else echo no; fi;))
    LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR := $(LT_PLATFORM_ROOT)/build/platform/$(LT_PLATFORM)
  endif
else
  LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR := $(LT_PLATFORM_ROOT)/build/platform/$(LT_PLATFORM)
endif
LT_PLATFORM_BUILD_PROJECT_DIR       := $(LT_PLATFORM_ROOT)/build/project
LT_PROJECT_RULES_MAKEFILE           := $(shell if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Rules.mk ] ; then echo $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Rules.mk; else echo $(LT_OS_ROOT)/build/Rules.mk; fi;)
# set the product variant makefile dir and the product root project dir
ifneq (,$(LT_USER_FIRMWARE_PROJECT_ROOT))
  LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR := $(LT_USER_FIRMWARE_PROJECT_ROOT)/build/product/$(LT_PRODUCT)
  ifeq (no, $(shell if [ -d $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR) ] ; then echo yes; else echo no; fi;))
    LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR := $(LT_PRODUCT_ROOT)/build/product/$(LT_PRODUCT)
  endif
else
    LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR      := $(LT_PRODUCT_ROOT)/build/product/$(LT_PRODUCT)
endif
LT_PRODUCT_BUILD_PROJECT_DIR              := $(LT_PRODUCT_ROOT)/build/project
# set default flags
LT_CFLAGS_OS_INCLUDE                := -I$(LT_OS_PUBLIC_INCLUDE_DIR)
LT_CFLAGS_PLATFORM_INCLUDE          := -I$(LT_OS_PUBLIC_INCLUDE_DIR) -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)
LT_CFLAGS_USER_INCLUDES        		:= -I$(LT_OS_PUBLIC_INCLUDE_DIR) -I$(LT_USER_ROOT)
ifeq ($(LT_OS_PUBLIC_INCLUDE_DIR), $(LT_PRODUCT_PUBLIC_INCLUDE_DIR))
LT_CFLAGS_PRODUCT_INCLUDE           := -I$(LT_OS_PUBLIC_INCLUDE_DIR)
else
LT_CFLAGS_PRODUCT_INCLUDE           := -I$(LT_OS_PUBLIC_INCLUDE_DIR) -I$(LT_PRODUCT_PUBLIC_INCLUDE_DIR)
endif
LT_CSTANDARD                        := gnu11
LT_CFLAGS_GENERIC                   := -nostdlib                                    \
                                       -fstack-usage -fvisibility=hidden -ffunction-sections -fdata-sections    \
                                       -Wall -Wextra -Werror -Wpointer-arith -Wstrict-prototypes -Wundef -MMD -MP \
                                       -fno-common -fno-strict-aliasing
ifneq (apple, $(LT_PLATFORM_FAMILY))
LT_CFLAGS_GENERIC                   += -fno-defer-pop -fno-function-cse
endif
LT_CFLAGS_DEBUG                     := -DLT_DEBUG -g -O0
LT_CFLAGS_RELEASE                   := -g -Os
LT_CXXFLAGS_GENERIC                 := $(patsubst -Wstrict-prototypes,,$(LT_CFLAGS_GENERIC)) \
                                       -fno-rtti -fno-exceptions -std=c++14
LT_CXXFLAGS_DEBUG                   := $(LT_CFLAGS_DEBUG)
LT_CXXFLAGS_RELEASE                 := $(LT_CFLAGS_RELEASE)
LT_LDFLAGS_SHAREDLIB                := -shared -L $(LT_TARGET_LIB_DIR) -pthread
LT_LDFLAGS_EXECUTABLE               := -L $(LT_TARGET_LIB_DIR) -lltrun -ldl -pthread
ifeq (debug, $(LT_BUILD_MODE))
#   LT_LDFLAGS_SHAREDLIB  += -rdynamic
#   LT_LDFLAGS_EXECUTABLE += -rdynamic
endif
LT_ARFLAGS_STATICLIB                := rc
LT_STRIPFLAGS_STRIPALL              := -s
LT_STRIP_RELEASE_BUILD              ?= true

############################################################
# explicitly specify the very especial LTCore & LTK projects
LT_OS_LTCORE_PROJECT                := LTCore
LT_OS_LTK_PROJECT                   := LTK

#############
# setup "all" as the first target
all:

# read the platform Makefile.config
ifeq (yes, $(shell if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Makefile.config ] ; then echo yes; else echo no; fi;))
include $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Makefile.config
else
$(error "No Makefile.config in $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)")
endif

#if we only have static linkage then we dont link -lltrun with executalbles
ifneq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
LT_LDFLAGS_EXECUTABLE := $(subst -lltrun,,$(LT_LDFLAGS_EXECUTABLE))
endif

# LT_LTKERNEL_ARCHITECTURE
LT_ERROR:=no
LT_VALID_LTKERNEL_ARCHITECTURES := LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V6M
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V7M
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_BASE
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M_MAIN
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_ARM_CORTEX_V8M
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_ARM_V5
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_XTENSA
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_RISC_V
LT_VALID_LTKERNEL_ARCHITECTURES += LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS
ifeq (,$(LT_LTKERNEL_ARCHITECTURE))
    LT_ERROR := yes
else
    ifneq ($(LT_LTKERNEL_ARCHITECTURE), $(findstring $(LT_LTKERNEL_ARCHITECTURE), $(LT_VALID_LTKERNEL_ARCHITECTURES)))
        LT_ERROR := yes
    endif
endif

#LT_LTCORE_PROJECT_DEPS (to build with LTK or not to)
ifeq (LT_LTKERNEL_ARCHITECTURE_BSP_DEFINED_HOST_OS, $(LT_LTKERNEL_ARCHITECTURE))
    # Hosted architectures do not include LTK
    LT_LTCORE_PROJECT_DEPS := $(LT_PLATFORM_LTCOREBSP_PROJECT)
else
    LT_LTCORE_PROJECT_DEPS := $(LT_PLATFORM_LTCOREBSP_PROJECT) $(LT_OS_LTK_PROJECT)
endif

ifeq (yes, $(LT_ERROR))
    LT_OUT := $(info $(LT_))
    ifeq (,$(LT_LTKERNEL_ARCHITECTURE))
        LT_OUT := Error: LT_LTKERNEL_ARCHITECTURE is not defined in platform Makefile.config.
    else
        LT_OUT := Error: "$(LT_LTKERNEL_ARCHITECTURE)" is not a valid architecture..
    endif
    LT_OUT := $(info $(LT_OUT))
    LT_OUT := $(info $(LT_))
    LT_OUT := $(info Valid LTKernel architectures are:)
    LT_OUT := $(foreach ARCH, $(LT_VALID_LTKERNEL_ARCHITECTURES), $(info $(LT_) $(LT_) $(ARCH)))
    LT_OUT := $(info $(LT_))
    LT_OUT := $(error Please set LT_LTKERNEL_ARCHITECTURE to a valid architecture in platform Makefile.config and reinvoke make)
endif

# having read the platform Makefile.config, update LT_CFLAGS_PLATFORM_INCLUDE
ifneq (,$(LT_PLATFORM_VENDOR_SDK_ROOT))
LT_CFLAGS_PLATFORM_INCLUDE += -I$(LT_PLATFORM_VENDOR_SDK_ROOT)
endif

# pass the LT_RELEASE_IDENTIFER to LTTypes.h via -D flag
# note the use of quotes around escaped quotes.  If we don't do this the preprocessor
# will perform token expansion on the components of our version string, i.e.:
# 1. doing this: -DSOMETHING="foo-linux-bar" is equivalent to #define SOMETHING foo-linux-bar
#    (foo minus linux minus bar) and the preprocessor will expand that to foo-1-bar using the
#    predefined #define linux 1
# 2. doing this: -DSOMETHING="\"foo-linux-bar\"" is equivalent to #define SOMETHING "foo-linux-bar"
LT_CFLAGS_GENERIC   += -DLT_RELEASE_IDENTIFIER="\"$(LT_BUILD_VERSION)\"" -DLT_SOFTWARE_VERSION="\"$(LT_SOFTWARE_VERSION)\"" -DLT_BUILD_TIME=$(LT_BUILD_TIME)
LT_CXXFLAGS_GENERIC += -DLT_RELEASE_IDENTIFIER="\"$(LT_BUILD_VERSION)\"" -DLT_SOFTWARE_VERSION="\"$(LT_SOFTWARE_VERSION)\"" -DLT_BUILD_TIME=$(LT_BUILD_TIME)

ifeq (no,$(LT_INCLUDE_LTTRACE))
LT_CFLAGS_GENERIC   += -DLTLIBRARY_INCLUDE_LTTRACE=0
LT_CXXFLAGS_GENERIC += -DLTLIBRARY_INCLUDE_LTTRACE=0
endif

# add the C standard
LT_CFLAGS_GENERIC += -std=$(LT_CSTANDARD)

LT_BUILD_ARTIFACT_PACKAGE           := $(LT_BUILD_VERSION)-build_archive
LT_BUILD_ARTIFACT_PACKAGE_TGZ       := $(LT_BUILD_ARTIFACT_PACKAGE).tgz
LT_BUILD_ARTIFACT_PACKAGE_PATH      := $(LT_TARGET_BUILD_ARTIFACTS_DIR)/$(LT_BUILD_ARTIFACT_PACKAGE_TGZ)
LT_PUBLISHED_ARTIFACTS              := $(strip $(LT_BUILD_ARTIFACT_PACKAGE_PATH))

# take note of the platform Makefile.config specification of LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER
ifeq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
    LT_DYNAMIC_LIBRARIES_LIST := all
endif
ifeq (no, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
    LT_CFLAGS_GENERIC   += -DLT_NO_DYNAMIC_LOADER
    LT_CXXFLAGS_GENERIC += -DLT_NO_DYNAMIC_LOADER
else
    ifneq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
        LT_OUT := $(error "$(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/Makefile.config FAILED to properly specify yes or no for the variable LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER")
    endif
    LT_CFLAGS_LIBRARY   ?= -fPIC -DPIC
    LT_CXXFLAGS_LIBRARY ?= -fPIC -DPIC
endif

# check if the platform Makefile.config specified nullification of all non-server LTLOGS and nullification of all asserts
ifeq (yes, $(LT_NULLIFY_ALL_NON_SERVER_LTLOGS))
    LT_CFLAGS_GENERIC     += -DLT_NULLIFY_ALL_NON_SERVER_LOGS
    LT_CXXFLAGS_GENERIC   += -DLT_NULLIFY_ALL_NON_SERVER_LOGS
endif

ifeq (yes, $(LT_NULLIFY_ALL_LT_ASSERTS))
    LT_CFLAGS_GENERIC     += -DLT_NULLIFY_ALL_ASSERTS
    LT_CXXFLAGS_GENERIC   += -DLT_NULLIFY_ALL_ASSERTS
endif

# check if the platform Makefile.config specified a gcc toolchain by setting LT_GCC_TOOLCHAIN_PATH
ifeq (, $(LT_GCC_TOOLCHAIN_PATH))
    ifneq (, $(LT_VENDOR_TOOLCHAIN_TGZ))
        ifneq (, $(LT_GCC_TOOLCHAIN_NAME))
            LT_GCC_TOOLCHAIN_PATH := $(LT_VENDOR_TOOLCHAINS_DIR)/$(LT_GCC_TOOLCHAIN_NAME)/bin
        else
            LT_GCC_TOOLCHAIN_PATH := $(LT_VENDOR_TOOLCHAINS_DIR)/$(basename $(notdir $(LT_VENDOR_TOOLCHAIN_TGZ)))/bin
        endif
    else
        LT_OUT := $(error "Must specify either LT_GCC_TOOLCHAIN_PATH or LT_VENDOR_TOOLCHAIN_TGZ")
    endif
endif

# determine the location of the toolchain tarball, if a name for it is given.  This is necessary
# for the toolchain target.
ifneq (, $(LT_VENDOR_TOOLCHAIN_TGZ))
    ifneq (, $(wildcard $(LT_VENDOR_TOOLCHAIN_TARBALLS_DIR)/$(LT_VENDOR_TOOLCHAIN_TGZ)))
        LT_VENDOR_TOOLCHAIN_TARBALL_PATH := $(abspath $(LT_VENDOR_TOOLCHAIN_TARBALLS_DIR)/$(LT_VENDOR_TOOLCHAIN_TGZ))
    endif
endif

ifeq (, $(LT_GCC_TOOLCHAIN_PREFIX))
    LT_GCC_TOOLCHAIN_PATH_AND_PREFIX := $(LT_GCC_TOOLCHAIN_PATH)/
else
    LT_GCC_TOOLCHAIN_PATH_AND_PREFIX := $(LT_GCC_TOOLCHAIN_PATH)/$(LT_GCC_TOOLCHAIN_PREFIX)-
endif

LT_CC        := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)gcc
LT_CXX       := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)g++
LT_CC_TOOL   := $(LT_CC)

ifneq (, $(LT_CCACHE))
    LT_CC    := $(LT_CCACHE) $(LT_CC)
    LT_CXX   := $(LT_CCACHE) $(LT_CXX)
endif

LT_LD        := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)ld
LT_LD_FRONTEND   ?= $(LT_CC)
LT_AR        := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)ar
LT_ADDR2LINE := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)addr2line
LT_RANLIB    := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)ranlib

# When a platform enables LTO, archives must be created with gcc-ar / gcc-ranlib
# so the linker plugin can read GIMPLE bytecode inside .o files.
ifeq (yes, $(LT_ENABLE_LTO))
LT_AR        := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)gcc-ar
LT_RANLIB    := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)gcc-ranlib
endif
LT_OBJCOPY   := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)objcopy
LT_OBJDUMP   := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)objdump
LT_NM        := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)nm
LT_SIZE      := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)size
LT_STRIP     := $(LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)strip

LT_TAR      := tar
LT_ZIP      := zip
LT_UNZIP    := unzip

# figure out how to cause echo to interpret escape sequences by testing if -e is supported
# if so assume -e required (as in bash); if not, assume esc sequences interpreted by default (as in dash)
ifeq (foo, $(shell echo -e "foo"))
    LT_ECHO_ESCAPED := echo -e
else
    LT_ECHO_ESCAPED := echo
endif

# The LT_PLATFORM_ARCHITECTURE determination is generating spurious errors and not currently used, commenting out for now...
# call LT_CC_TOOL to determine the platform architecture, we'll support arm, mips and x86_64 for now
#LT_ARCHITECTURE_DEFS := "\
#  \#if defined(__arm__) \n \
#    \#define LT_PLATFORM_ARCHITECTURE arm \n \
#  \#elif defined (__mips__) \n \
#    \#define LT_PLATFORM_ARCHITECTURE mips \n \
#  \#elif defined (__x86_64__) \n \
#    \#define LT_PLATFORM_ARCHITECTURE x86_64 \n \
#  \#else \n \
#    \#define LT_PLATFORM_ARCHITECTURE unknown \n \
#  \#endif "
#
#LT_PLATFORM_ARCHITECTURE = $(shell $(LT_ECHO_ESCAPED) $(LT_ARCHITECTURE_DEFS) | $(LT_CC_TOOL) -dM -E - | grep LT_PLATFORM_ARCHITECTURE | cut -f3 -d ' ')

ifeq (,$(LT_CURRENTLY_BUILDING_TOOL))
    # figure out the set of buildtools we need to ensure are built before starting the master make-ola
    LT_KNOWN_BUILDTOOLS := $(subst /build/LTBuildTool.mk,,$(shell find $(LT_BUILDTOOLS_ROOT) -maxdepth 3 -type f -path "*/build/*" -name LTBuildTool.mk | sed -e "s^$(LT_BUILDTOOLS_ROOT)/^^"))
    LT_KNOWN_BUILDTOOLS := $(sort $(LT_KNOWN_BUILDTOOLS))
endif

# gather all of the known os projects, platform projects, and product projects in separate lists
# by finding all of the project .mk files in the following directories:
#                         os_root/build/project/
#                   platform_root/build/project/
#                    product_root/build/project/
# platform_root/build/platform/platform_variant/
#    product_root/build/product/product_variant/
#
# Then strip off the .mk extension from the file names to yield the project names we will use to create
# the recipes and rules for each individual project.
# First, however, make sure to remove LTCore, LTK, the LTCoreBSP project, and the mastering projects.
# Those projects have specialized rules and recipes written herein and we don't want or need to autogen them
# from these lists.

# Find the Platform Projects
LT_KNOWN_PLATFORM_PROJECTS := $(subst .mk,,$(notdir $(shell find $(LT_PLATFORM_BUILD_PROJECT_DIR) -name "*.mk") $(shell find $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR) -name "*.mk")))
LT_KNOWN_PLATFORM_PROJECTS := $(sort $(LT_KNOWN_PLATFORM_PROJECTS))

# Find the OS Projects
LT_KNOWN_OS_PROJECTS := $(subst .mk,,$(notdir $(shell find $(LT_OS_BUILD_PROJECT_DIR) -name "*.mk")))
LT_KNOWN_OS_PROJECTS := $(sort $(LT_KNOWN_OS_PROJECTS))
LT_KNOWN_OS_PROJECTS := $(filter-out $(LT_KNOWN_PLATFORM_PROJECTS),$(LT_KNOWN_OS_PROJECTS))

# Find the platform bootloader projects
LT_PLATFORM_BOOTLOADER_PROJECTS := $(subst .mk,,$(notdir $(shell find $(LT_PLATFORM_BUILD_PROJECT_DIR) -name "*LTBootloader*.mk") $(shell find $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR) -name "*LTBootloader*.mk")))
LT_PLATFORM_BOOTLOADER_PROJECTS := $(sort $(LT_PLATFORM_BOOTLOADER_PROJECTS))

# Find the Product Projects, skipping over the main build/project directory if the product root is equal to the os or platform root
LT_KNOWN_PRODUCT_PROJECTS :=
ifneq ($(LT_PRODUCT_BUILD_PROJECT_DIR),$(LT_OS_BUILD_PROJECT_DIR))
    ifneq ($(LT_PRODUCT_BUILD_PROJECT_DIR),$(LT_PLATFORM_BUILD_PROJECT_DIR))
        ifeq (yes, $(shell if [ -d $(LT_PRODUCT_BUILD_PROJECT_DIR) ] ; then echo yes; else echo no; fi;))
            LT_KNOWN_PRODUCT_PROJECTS := $(subst .mk,,$(notdir $(shell find $(LT_PRODUCT_BUILD_PROJECT_DIR) -name "*.mk")))
        endif
    endif
endif
LT_KNOWN_PRODUCT_PROJECTS += $(subst .mk,,$(notdir $(shell find $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR) -name "*.mk")))
LT_KNOWN_PRODUCT_PROJECTS := $(sort $(LT_KNOWN_PRODUCT_PROJECTS))

# filter out LTCore, LTK, BSP, Mastering and Bootloader projects from the KNOWN lists.  These KNOWN lists are used to foreach the main make all target for each individual project
LT_KNOWN_OS_PROJECTS       := $(filter-out $(LT_OS_LTCORE_PROJECT) $(LT_OS_LTK_PROJECT) $(LT_PLATFORM_LTCOREBSP_PROJECT) $(LT_PLATFORM_MASTERING_PROJECTS) $(LT_PLATFORM_BOOTLOADER_PROJECTS), $(LT_KNOWN_OS_PROJECTS))
LT_KNOWN_PLATFORM_PROJECTS := $(filter-out $(LT_OS_LTCORE_PROJECT) $(LT_OS_LTK_PROJECT) $(LT_PLATFORM_LTCOREBSP_PROJECT) $(LT_PLATFORM_MASTERING_PROJECTS) $(LT_PLATFORM_BOOTLOADER_PROJECTS), $(LT_KNOWN_PLATFORM_PROJECTS))
LT_KNOWN_PRODUCT_PROJECTS  := $(filter-out $(LT_OS_LTCORE_PROJECT) $(LT_OS_LTK_PROJECT) $(LT_PLATFORM_LTCOREBSP_PROJECT) $(LT_PLATFORM_MASTERING_PROJECTS) $(LT_PLATFORM_BOOTLOADER_PROJECTS), $(LT_KNOWN_PRODUCT_PROJECTS))

# now add Mastering and Bootloader projects back into their proper list so they get clean- and rebuild- targets. LTCore, LTK and BSP are handled manually for clean- and rebuild-
LT_OS_ALL_KNOWN_PROJECTS       := $(LT_KNOWN_OS_PROJECTS)
LT_PLATFORM_ALL_KNOWN_PROJECTS := $(LT_KNOWN_PLATFORM_PROJECTS) $(LT_PLATFORM_MASTERING_PROJECTS) $(LT_PLATFORM_BOOTLOADER_PROJECTS)
LT_PRODUCT_ALL_KNOWN_PROJECTS  := $(LT_KNOWN_PRODUCT_PROJECTS)

ifeq (1, $(LT_RUN_ARBOLATOR))
# DeviceConfig and ProductConfig variables for the arbolator
LT_DEVICE_CONFIG_JSON_FILE_DIR := $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)
LT_DEVICE_CONFIG_JSON_FILE := $(LT_DEVICE_CONFIG_JSON_FILE_DIR)/LTDeviceConfig.json
LT_DEVICE_CONFIG_ARBOLATED_JSON_SOURCE_DIR := $(LT_TARGET_OBJ_DIR)/LTDeviceConfig/arbolated/json-source
LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE := $(LT_DEVICE_CONFIG_ARBOLATED_JSON_SOURCE_DIR)/LTDeviceConfig.json
LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR := $(LT_TARGET_OBJ_DIR)/LTDeviceConfig/arbolated/build
LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE := $(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR)/LTDeviceConfig_LibraryIncludes.mk
LT_PRODUCT_CONFIG_JSON_FILE_DIR := $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)
LT_PRODUCT_CONFIG_JSON_FILE := $(LT_PRODUCT_CONFIG_JSON_FILE_DIR)/LTProductConfig.json
LT_PRODUCT_CONFIG_ARBOLATED_JSON_SOURCE_DIR := $(LT_TARGET_OBJ_DIR)/LTProductConfig/arbolated/json-source
LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE := $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_SOURCE_DIR)/LTProductConfig.json
LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR := $(LT_TARGET_OBJ_DIR)/LTProductConfig/arbolated/build
LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE := $(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR)/LTProductConfig_LibraryIncludes.mk

LT_FORCE_ERROR_ON_MISSING_FILES := no
ifeq (yes, $(LT_FORCE_ERROR_ON_MISSING_FILES))
    ifeq (no, $(shell if [ -f $(LT_DEVICE_CONFIG_JSON_FILE) ] ; then echo yes; else echo no; fi;))
        $(error "No LTDeviceConfig.json file in $(LT_DEVICE_CONFIG_JSON_FILE_DIR)/")
    endif
    ifeq (no, $(shell if [ -f $(LT_PRODUCT_CONFIG_JSON_FILE) ] ; then echo yes; else echo no; fi;))
        $(error "No LTProductConfig.json file in $(LT_PRODUCT_CONFIG_JSON_FILE_DIR)/")
    endif
endif

# Arbolator dependency: only require buildtool-arbolator if sources exist; otherwise assume prebuilt binary
$(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator: $(if $(wildcard $(LT_BUILDTOOLS_ROOT)/arbolator/build/LTBuildTool.mk),buildtool-arbolator)

$(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR) $(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR) :
	$(LT_QUIET_CMD) @echo MKDIR $(patsubst $(LT_TARGET_OBJ_DIR)/%,%,$@)
	$(LT_BUILDTOOL_CMD)  mkdir -p $@

$(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE) : $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator $(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR) $(LT_DEVICE_CONFIG_JSON_FILE)
	$(LT_QUIET_CMD) @echo ARBOLATE $(patsubst $(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR)/%,%,$@)
	$(LT_BUILDTOOL_CMD)  $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator --gendeviceconfig $(LT_DEVICE_CONFIG_JSON_FILE) $@ $(LT_DEVICE_CONFIG_ARBOLATED_JSON_SOURCE_DIR)

$(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE) : $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator $(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR) $(LT_PRODUCT_CONFIG_JSON_FILE)
	$(LT_QUIET_CMD) @echo ARBOLATE $(patsubst $(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR)/%,%,$@)
	$(LT_BUILDTOOL_CMD)  $(LT_TARGET_DIR_BUILDTOOLS)/bin/arbolator --genproductconfig $(LT_PRODUCT_CONFIG_JSON_FILE) $@ $(LT_PRODUCT_CONFIG_ARBOLATED_JSON_SOURCE_DIR)

include $(LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE) $(LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE)

# ## #### ########     #########                                      #########
# We just included the arbolated DeviceConfig.mk and ProductConfig.mk makefiles that specify which libraries are to be built.
#
# Now we must sanitize, categorize, and dichotomize those named libraries into three distinct lists that will dictate
# their proper build invocation settings, dependencies, build method, etc.
#
# The 3 lists we construct are: LT_BUILD_OS_PROJECTS, LT_BUILD_PLATFORM_PROJECTS and LT_BUILD_PRODUCT_PROJECTS.  We ensure each library goes into only 1 list
# and that each list has no duplicate entries.  Furthermore we ensure the genesis library, LTDeviceConfig library and LTProductConfig library are always
# included in every build regardless of whether or not they are listed explicitly for building in either LTProductConfig.json or LTDeviceConfig.json.
#
# Rule of thumb: only specify the genesis library once in the genesis section of LTProductConfig.json, and do not specify LTDeviceConfig nor LTProductConfig
# as libraries to build; it is unnecessary, they are always built.
#
#   1
#   ----> PLATFORM_PROJECTS
#   BUILD_PLATFORM_PROJECTS    PLATFORM_PROJECTS are simply those driver libraries found in DeviceConfig.json
 LT_BUILD_PLATFORM_PROJECTS := $(LT_BUILD_DRIVER_LIBRARIES)
#
#   2
#   -----> PRODUCT_PROJECTS    PRODUCT_PROJECTS are any libs in ProductConfig.json that are not OS libs.  We'll add genesis and cull duplicates before
#    BUILD_PRODUCT_PROJECTS    and strip out all known OS libraries, leaving the list with product libraries only
 LT_BUILD_PRODUCT_LIBRARIES := $(sort $(LT_BUILD_PRODUCT_LIBRARIES) $(LT_PRODUCT_GENESIS_LIBRARY))
  LT_BUILD_PRODUCT_PROJECTS := $(filter-out $(LT_KNOWN_OS_PROJECTS), $(LT_BUILD_PRODUCT_LIBRARIES))
#
#   3
#   ----------> OS_PROJECTS    first, filter out the non-OS libs from ProductConfig.json, then add DeviceLibs from DeviceConfig.json,
#         BUILD_OS_PROJECTS    then manually specify LTCore, LTDeviceConfig, and LTProductConfig to ensure their inclusion
       LT_BUILD_OS_PROJECTS := $(filter-out $(LT_BUILD_PRODUCT_PROJECTS), $(LT_BUILD_PRODUCT_LIBRARIES))
       LT_BUILD_OS_PROJECTS += $(LT_BUILD_DEVICE_LIBRARIES) LTCore LTDeviceConfig LTProductConfig
#
#   4 Use sort to guarantee entries are unique
 LT_BUILD_PLATFORM_PROJECTS := $(sort $(LT_BUILD_PLATFORM_PROJECTS))
  LT_BUILD_PRODUCT_PROJECTS := $(sort $(LT_BUILD_PRODUCT_PROJECTS))
       LT_BUILD_OS_PROJECTS := $(sort $(LT_BUILD_OS_PROJECTS))
#
# Now here, now on line 1412, we finally know what we are going to build!
##########

endif

#LT COMMUNITY/EXTERNAL LIBRARIES
LT_USER_LIBRARY_FORAGER := $(LT_OS_ROOT)/build/UserLib.mk
ifeq (no, $(shell if [ -f $(LT_USER_LIBRARY_FORAGER) ] ; then echo yes; else echo no; fi;))
	LT_USER_LIBRARY_FORAGER :=
endif

ifeq (,$(LT_CURRENTLY_BUILDING_TOOL))
    ifneq (, $(findstring /,$(MAKECMDGOALS)))
        LT_OTHERGOALS := $(shell grep ".PHONY:" Makefile | cut -f2 -d':')
        LT_USER_ROOT := $(filter-out $(LT_OTHERGOALS),$(MAKECMDGOALS))
    endif
	LT_USER_ROOT := $(strip $(abspath $(LT_USER_ROOT)))
else
	LT_USER_ROOT     :=
endif

ifeq (,$(LT_USER_LIBRARY_FORAGER))
	LT_USER_ROOT	:=
endif

ifeq (,$(LT_USER_ROOT))
	LT_USER_LIB_DIRS				:=
else

    #IF LT_USER_ROOT is set as a variable before making, this will remove any errant / at the end
	LT_USER_ROOT					:= $(foreach user_root, $(LT_USER_ROOT),$(shell echo $(user_root) | sed 's/\/$$//g'))

	ifeq (/,$(LT_USER_ROOT))
        $(error Why are you trying to search every possible file in your filesystem?  You should just)
	else
		LT_USER_LIB_DIRS            	:= $(sort $(shell find $(LT_USER_ROOT) -maxdepth 4 -type d -name "source" | sed 's/\/source*//g'))
	endif

# Allows user to make via path/to/target both lt libraries and user libraries in same make command line goals
LT_USER_LIB_DIRS                += $(foreach user_root, $(LT_USER_ROOT), $(if $(findstring /lt/lt/source/example,$(user_root)),$(user_root)))

	ifeq (,$(LT_USER_LIB_DIRS))
		LT_USER_LIB_DIRS 			:= $(LT_USER_ROOT)
	endif
endif

# Flash images
LT_FLASH_CONFIG_JSON_FILE_DIR := $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)
LT_FLASH_CONFIG_JSON_FILE := $(LT_FLASH_CONFIG_JSON_FILE_DIR)/LTFlashConfig.json
LT_FLASHER_JSON_FILE := $(LT_PLATFORM_ROOT)/build/image/LTFlasher.json

# setup complete

#######################################################################
#     ____ _                 _              ____
#    / ___| |__   __ _ _ __ | |_ ___ _ __  | ___|
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__| |___ \
#   | |___| | | | (_| | |_) | ||  __/ |     ___) |
#    \____|_| |_|\__,_| .__/ \__\___|_|    |____/
#                     |_|
#######################################################################
# CHAPTER 5 - dumpvars if requested
#######################################################################
# dumpvars if requested
ifeq (yes,$(LT_DUMPVARS))
include $(LT_PROJECT_RULES_MAKEFILE)
LT_DUMP = $(info $(shell printf "%-38s = %s\n" ${1} "${${1}}"))
$(info $(LT_)LT MASTER MAKEFILE VARIABLE DUMP)
$(info $(LT_)________________________________)
$(info $(LT_)CHAPTER 1)
$(call LT_DUMP,LT_OS_ROOT)
$(call LT_DUMP,LT_MAKE_INVOCATION_ROOT)
$(call LT_DUMP,LT_ROOTS_BASE)
$(call LT_DUMP,LT_PRODUCT_ROOT)
$(call LT_DUMP,LT_PLATFORM_ROOT)
$(call LT_DUMP,LT_TARGET_ROOT)
$(call LT_DUMP,LT_PRODUCT_FAMILY)
$(call LT_DUMP,LT_PLATFORM_FAMILY)
$(call LT_DUMP,LT_PRODUCT)
$(call LT_DUMP,LT_PLATFORM)
$(call LT_DUMP,LT_BUILD_MODE)
$(call LT_DUMP,LT_VERSION_MAJOR)
$(call LT_DUMP,LT_VERSION_MINOR)
$(call LT_DUMP,LT_VERSION_BUILD_NUMBER)
$(call LT_DUMP,LT_VERSION_COMMIT)
$(call LT_DUMP,LT_CRYPTO_KEY_DIR)
$(info $(LT_)_________)
$(info $(LT_)CHAPTER 2)
$(call LT_DUMP,LT_BANNER)
$(call LT_DUMP,LT_SAVECONFIG)
$(info $(LT_)_________)
$(info $(LT_)CHAPTER 3)
$(call LT_DUMP,LT_VALID_BUILD_MODES)
$(info $(LT_)_________)
$(info $(LT_)CHAPTER 4)
$(call LT_DUMP,LT_VERSION_DATE)
$(call LT_DUMP,LT_VERSION_PRODUCT)
$(call LT_DUMP,LT_VERSION_PLATFORM)
$(call LT_DUMP,LT_BUILD_VERSION)
$(call LT_DUMP,LT_PLATFORM_DISTRO_VERSION)
$(call LT_DUMP,LT_OPENSOURCE_DISTRO_VERSION)
$(call LT_DUMP,LT_DUMPVARS)
$(call LT_DUMP,LT_DISABLE_STATS)
$(call LT_DUMP,MAKECMDGOALS)
$(call LT_DUMP,LT_QUIET_CMD)
$(call LT_DUMP,LT_EXEC_CMD)
$(call LT_DUMP,LT_GNU_QUIET_ARG)
$(call LT_DUMP,LT_OS_PUBLIC_INCLUDE_DIR)
$(call LT_DUMP,LT_OS_PRIVATE_SOURCE_DIR)
$(call LT_DUMP,LT_PLATFORM_PUBLIC_INCLUDE_DIR)
$(call LT_DUMP,LT_PLATFORM_THIRDPARTY_DIR)
$(call LT_DUMP,LT_PLATFORM_PRIVATE_SOURCE_DIR)
$(call LT_DUMP,LT_PRODUCT_PUBLIC_INCLUDE_DIR)
$(call LT_DUMP,LT_PRODUCT_PRIVATE_SOURCE_DIR)
$(call LT_DUMP,LT_OS_DOXYFILE)
$(call LT_DUMP,LT_BUILDTOOLS_ROOT)
$(call LT_DUMP,LT_TARGET_DIR_BUILDTOOLS)
$(call LT_DUMP,LT_TARGET_DIR_THIS_BUILD)
$(call LT_DUMP,LT_TARGET_BIN_DIR)
$(call LT_DUMP,LT_TARGET_LIB_DIR)
$(call LT_DUMP,LT_TARGET_OBJ_DIR)
$(call LT_DUMP,LT_TARGET_STATS_DIR)
$(call LT_DUMP,LT_TARGET_BUILD_ARTIFACTS_DIR)
$(call LT_DUMP,LT_TARGET_EXAMINATE_DIR)
$(call LT_DUMP,LT_TARGET_DOXYDOCS_DIR)
$(call LT_DUMP,LT_TARGET_DOXYDOCS_INDEX_HTML)
$(call LT_DUMP,LT_TARGET_SDKDOCS_DIR)
$(call LT_DUMP,LT_TARGET_SDKDOCS_INDEX_HTML)
$(call LT_DUMP,LT_TARGET_DISTRO_DIR_BASE)
$(call LT_DUMP,LT_OPENSOURCE_DISTRO_TGZ_NAME)
$(call LT_DUMP,LT_OPENSOURCE_DISTRO_TGZ)
$(call LT_DUMP,LT_DOXYDOCS_DOXYGEN_TOOL)
$(call LT_DUMP,LT_DOXYDOCS_PREVIEW_BROWSER)
$(call LT_DUMP,LT_TOOLCHAIN_HOST_ARCHITECTURE)
$(call LT_DUMP,LT_TOOLCHAIN_HOST_OS_NAME)
$(call LT_DUMP,LT_VENDOR_TOOLCHAINS_DIR)
$(call LT_DUMP,LT_VENDOR_TOOLCHAIN_TARBALLS_DIR)
$(call LT_DUMP,LT_BUILDINFO_FILE)
$(call LT_DUMP,LT_OS_BUILD_SCRIPTS_DIR)
$(call LT_DUMP,LT_OS_BUILD_PROJECT_DIR)
$(call LT_DUMP,LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)
$(call LT_DUMP,LT_PLATFORM_BUILD_PROJECT_DIR)
$(call LT_DUMP,LT_PROJECT_RULES_MAKEFILE)
$(call LT_DUMP,LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)
$(call LT_DUMP,LT_PRODUCT_BUILD_PROJECT_DIR)
$(call LT_DUMP,LT_CFLAGS_OS_INCLUDE)
$(call LT_DUMP,LT_CFLAGS_PLATFORM_INCLUDE)
$(call LT_DUMP,LT_CFLAGS_PRODUCT_INCLUDE)
$(call LT_DUMP,LT_CFLAGS_GENERIC)
$(call LT_DUMP,LT_CFLAGS_DEBUG)
$(call LT_DUMP,LT_CFLAGS_RELEASE)
$(call LT_DUMP,LT_CXXFLAGS_GENERIC)
$(call LT_DUMP,LT_CXXFLAGS_DEBUG)
$(call LT_DUMP,LT_CXXFLAGS_RELEASE)
$(call LT_DUMP,LT_LDFLAGS_SHAREDLIB)
$(call LT_DUMP,LT_LDFLAGS_EXECUTABLE)
$(call LT_DUMP,LT_ARFLAGS_STATICLIB)
$(call LT_DUMP,LT_STRIPFLAGS_STRIPALL)
$(call LT_DUMP,LT_OS_LTCORE_PROJECT)
$(call LT_DUMP,LT_OS_LTK_PROJECT)
$(call LT_DUMP,LT_BUILD_ARTIFACT_PACKAGE)
$(call LT_DUMP,LT_BUILD_ARTIFACT_PACKAGE_TGZ)
$(call LT_DUMP,LT_BUILD_ARTIFACT_PACKAGE_PATH)
$(call LT_DUMP,LT_PUBLISHED_ARTIFACTS)
$(call LT_DUMP,LT_VENDOR_TOOLCHAIN_TARBALL_PATH)
$(call LT_DUMP,LT_GCC_TOOLCHAIN_PATH_AND_PREFIX)
$(call LT_DUMP,LT_CC)
$(call LT_DUMP,LT_CXX)
$(call LT_DUMP,LT_LD)
$(call LT_DUMP,LT_LD_FRONTEND)
$(call LT_DUMP,LT_AR)
$(call LT_DUMP,LT_RANLIB)
$(call LT_DUMP,LT_OBJCOPY)
$(call LT_DUMP,LT_OBJDUMP)
$(call LT_DUMP,LT_NM)
$(call LT_DUMP,LT_SIZE)
$(call LT_DUMP,LT_STRIP)
$(call LT_DUMP,LT_TAR)
$(call LT_DUMP,LT_ECHO_ESCAPED)
$(info $(LT_)LT_ARCHITECTURE_DEFS                   = $(LT_ARCHITECTURE_DEFS))
#$(call LT_DUMP,LT_PLATFORM_ARCHITECTURE)
$(call LT_DUMP,LT_LTKERNEL_ARCHITECTURE)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - known tools, known libraries)
$(call LT_DUMP,LT_KNOWN_BUILDTOOLS)
$(call LT_DUMP,LT_KNOWN_OS_PROJECTS)
$(call LT_DUMP,LT_KNOWN_PLATFORM_PROJECTS)
$(call LT_DUMP,LT_PLATFORM_BOOTLOADER_PROJECTS)
$(call LT_DUMP,LT_KNOWN_PRODUCT_PROJECTS)
$(call LT_DUMP,LT_OS_ALL_KNOWN_PROJECTS)
$(call LT_DUMP,LT_PLATFORM_ALL_KNOWN_PROJECTS)
$(call LT_DUMP,LT_PRODUCT_ALL_KNOWN_PROJECTS)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - arbolated makefile locations)
$(call LT_DUMP,LT_DEVICE_CONFIG_JSON_FILE_DIR)
$(call LT_DUMP,LT_DEVICE_CONFIG_JSON_FILE)
$(call LT_DUMP,LT_DEVICE_CONFIG_ARBOLATED_JSON_SOURCE_DIR)
$(call LT_DUMP,LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE)
$(call LT_DUMP,LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE_DIR)
$(call LT_DUMP,LT_DEVICE_CONFIG_ARBOLATED_MAKEFILE)
$(call LT_DUMP,LT_PRODUCT_CONFIG_JSON_FILE_DIR)
$(call LT_DUMP,LT_PRODUCT_CONFIG_JSON_FILE)
$(call LT_DUMP,LT_PRODUCT_CONFIG_ARBOLATED_JSON_SOURCE_DIR)
$(call LT_DUMP,LT_PRODUCT_CONFIG_ARBOLATED_JSON_FILE)
$(call LT_DUMP,LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE_DIR)
$(call LT_DUMP,LT_PRODUCT_CONFIG_ARBOLATED_MAKEFILE)
$(info $(LT_)____________________________________)
$(info $(LT_)CHAPTER 4 - platform Makefile.config)
$(call LT_DUMP,LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER)
$(call LT_DUMP,LT_GCC_TOOLCHAIN_PATH)
$(call LT_DUMP,LT_VENDOR_TOOLCHAIN_TGZ)
$(call LT_DUMP,LT_GCC_TOOLCHAIN_PREFIX)
$(call LT_DUMP,LT_PLATFORM_LTCOREBSP_PROJECT)
$(call LT_DUMP,LT_PLATFORM_MASTERING_PROJECTS)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - arbolated LTDeviceConfig_LibraryIncludes.mk)
$(call LT_DUMP,LT_BUILD_DEVICE_LIBRARIES)
$(call LT_DUMP,LT_BUILD_DRIVER_LIBRARIES)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - arbolated LTProductConfig_LibraryIncludes.mk)
$(call LT_DUMP,LT_PRODUCT_GENESIS_LIBRARY)
$(call LT_DUMP,LT_BUILD_PRODUCT_LIBRARIES)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - calculated build projects)
$(call LT_DUMP,LT_OS_LTCORE_PROJECT)
$(call LT_DUMP,LT_LTCORE_PROJECT_DEPS)
$(call LT_DUMP,LT_BUILD_OS_PROJECTS)
$(call LT_DUMP,LT_BUILD_PLATFORM_PROJECTS)
$(call LT_DUMP,LT_BUILD_PRODUCT_PROJECTS)
$(call LT_DUMP,LT_PLATFORM_MASTERING_PROJECTS)
$(call LT_DUMP,LT_PLATFORM_BOOTLOADER_PROJECTS)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - images)
$(call LT_DUMP,LT_FLASH_CONFIG_JSON_FILE_DIR)
$(call LT_DUMP,LT_FLASH_CONFIG_JSON_FILE)
$(info $(LT_)___________________________________)
$(info $(LT_)CHAPTER 4 - user libraries)
$(call LT_DUMP,LT_USER_ROOT)
$(call LT_DUMP,LT_USER_LIB_DIRS)
$(call LT_DUMP,LT_USER_LIBRARY_FORAGER)

$(info $(LT_))
$(error var dump complete, now we may..)
endif

#################################################################################
#     ____ _                 _               __
#    / ___| |__   __ _ _ __ | |_ ___ _ __   / /_
#   | |   | '_ \ / _` | '_ \| __/ _ \ '__| | '_ \
#   | |___| | | | (_| | |_) | ||  __/ |    | (_) |
#    \____|_| |_|\__,_| .__/ \__\___|_|     \___/
#                     |_|
#################################################################################
# CHAPTER 6 - Actual make targets!
#######################################################################
.EXPORT_ALL_VARIABLES:

.PHONY: all
all: buildtools toolchain announce-firmware-build targetdirs $(LT_BUILD_OS_PROJECTS) $(LT_BUILD_PLATFORM_PROJECTS) $(LT_USER_LIB_DIRS) $(LT_BUILD_PRODUCT_PROJECTS) $(LT_PLATFORM_MASTERING_PROJECTS) stats-summarize versioninfo

ifeq (yes,$(LT_BANNER))
.PHONY: announce-firmware-build
announce-firmware-build:
	@echo
	@echo   "=============================================================================="
	@echo   "=============================================================================="
	@printf "%78s\n" "MAKE ${LT_BUILD_VERSION}"
	@echo   "                    MAKECMDGOALS = $(LT_BANNER_GOALS)"
	@echo   "------------------------------------------------------------------------------"
else
.PHONY: announce-firmware-build
announce-firmware-build: ;
endif

.PHONY: images
images: all bootloaders
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	$(LT_BUILDTOOL_CMD) $(LT_TARGET_DIR_BUILDTOOLS)/bin/rib -p $(LT_TARGET_DIR_THIS_BUILD)/bin -c $(LT_FLASH_CONFIG_JSON_FILE) -o $(LT_TARGET_DIR_THIS_BUILD)/bin/LTManufacturingFlashImageAll.bin build

.PHONY: bootloaders
bootloaders: buildtools toolchain announce-firmware-build targetdirs $(LT_PLATFORM_BOOTLOADER_PROJECTS)

.PHONY: rebuild
rebuild: clean all

# REGULAR TARGETS, in order of execution
ifeq (, $(LT_CURRENTLY_BUILDING_TOOL))
    LT_ARROW_SEPARATOR   := "====================================>"
else
    LT_ARROW_SEPARATOR   := "============= BUILDTOOL LTLIB ==>"
endif

LT_BUILDTOOLS_BUILD_TARGETS := $(subst arbolator,,$(LT_KNOWN_BUILDTOOLS))
.PHONY: buildtools
buildtools: $(patsubst %,buildtool-%,$(LT_BUILDTOOLS_BUILD_TARGETS))

# TOOLCHAIN
.PHONY: toolchain
ifeq (no, $(shell if [ -x $(LT_CC_TOOL) ] ; then echo yes; else echo no; fi;))
toolchain: $(LT_CC_TOOL)

$(LT_CC_TOOL):
	$(LT_QUIET_CMD) @echo INSTALL toolchain
	@if [ -z $(LT_VENDOR_TOOLCHAIN_TARBALL_PATH) ]; then \
		echo -n "Error: Unable to find toolchain tarball $(LT_VENDOR_TOOLCHAIN_TGZ) "; \
		if [ -z $(LT_VENDOR_TOOLCHAIN_TARBALLS_DIR) ]; then \
			echo "in default location."; \
		else \
			echo "in $(abspath $(LT_VENDOR_TOOLCHAIN_TARBALLS_DIR))."; \
		fi; \
		echo "       Please set LT_VENDOR_TOOLCHAIN_TARBALLS_DIR to specify location of toolchain tarballs or copy tarballs to $(abspath $(LT_VENDOR_TOOLCHAIN_TARBALLS_DIR))."; exit 1; \
	fi
	$(LT_EXEC_CMD)  mkdir -p $@
	$(LT_EXEC_CMD)  tar --directory=$(LT_VENDOR_TOOLCHAINS_DIR) -xf $(LT_VENDOR_TOOLCHAIN_TARBALL_PATH)
else
toolchain: ;
endif

# TARGET DIRS
$(LT_TARGET_BIN_DIR) $(LT_TARGET_LIB_DIR) $(LT_TARGET_OBJ_DIR) $(LT_TARGET_STATS_DIR) $(LT_TARGET_BUILD_ARTIFACTS_DIR):
	$(LT_QUIET_CMD) @echo mkdir $(abspath $@)
	$(LT_EXEC_CMD)  mkdir -p $@

.PHONY: targetdirs
targetdirs: $(LT_TARGET_BIN_DIR) $(LT_TARGET_LIB_DIR) $(LT_TARGET_OBJ_DIR) $(LT_TARGET_STATS_DIR) $(LT_TARGET_BUILD_ARTIFACTS_DIR)

# DEVICE LIBRARY / DRIVER LIBRARY inclusion as determined by the ARCH


# do not EVER create any more special TARGET_RULE_DEFS.  See Don.
LT_TARGET_RULE_DEFS_LT_OS_LTCORE_PROJECT :=
LT_TARGET_RULE_DEFS_LT_OS_PROJECTS :=
LT_TARGET_RULE_DEFS_LT_PRODUCT_LIBRARY_PROJECTS :=
LT_TARGET_RULE_DEFS_LT_USER_LIBRARY_PROJECTS :=

LT_TARGET_RULE_DEFS_LT_PLATFORM_LTCOREBSP_PROJECT := -DLT_GENESIS_LIBRARY=\"\\\"$(LT_PRODUCT_GENESIS_LIBRARY)\\\"\" $(LT_PLATFORM_PROJECT_ONLY_DEFS)
LT_TARGET_RULE_DEFS_LT_OS_LTK_PROJECT := -DLT_LTKERNEL_ARCHITECTURE=$(LT_LTKERNEL_ARCHITECTURE) $(LT_PLATFORM_PROJECT_ONLY_DEFS)

LT_TARGET_RULE_DEFS_LT_PLATFORM_DRIVER_PROJECTS := $(LT_PLATFORM_PROJECT_ONLY_DEFS)
LT_TARGET_RULE_DEFS_LT_PLATFORM_MASTERING_PROJECTS := $(LT_PLATFORM_PROJECT_ONLY_DEFS)
LT_TARGET_RULE_DEFS_LT_PLATFORM_BOOTLOADER_PROJECTS := $(LT_PLATFORM_PROJECT_ONLY_DEFS)

# LTCORE BSP
$(LT_PLATFORM_LTCOREBSP_PROJECT) : toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
	export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_LTCOREBSP_PROJECT)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk all;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$@.mk all; \
    fi;

# LTK
$(LT_OS_LTK_PROJECT) : toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_LTK_PROJECT)"; \
    LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$@.mk all;

# LTCORE
$(LT_OS_LTCORE_PROJECT) : $(LT_LTCORE_PROJECT_DEPS)
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_LTCORE_PROJECT)"; \
    LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$@.mk all;

# LT_KNOWN_OS_PROJECTS
$(LT_KNOWN_OS_PROJECTS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_PROJECTS)"; \
    LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$@.mk all; \

# LT_KNOWN_PLATFORM_PROJECTS
$(LT_KNOWN_PLATFORM_PROJECTS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_DRIVER_PROJECTS)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk all;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$@.mk all; \
    fi;

# USER LIBRARY DIRECTORIES
ifneq (,$(LT_USER_ROOT))

    ifeq ($(abspath $(LT_USER_ROOT)),$(abspath $(LT_USER_LIB_DIRS)))
	    LT_USER_ROOT              := $(abspath $(LT_USER_ROOT)/..)
	    LT_USER_LIB_DIRS		  := $(abspath $(LT_USER_LIB_DIRS))
    endif
#.PHONY: $(LT_USER_ROOT)
#$(LT_USER_ROOT): $(LT_USER_LIB_DIRS)
#$(LT_USER_LIB_DIRS): $(LT_USER_ROOT)
#$(LT_USER_ROOT)/: $(LT_USER_LIB_DIRS)
endif

$(LT_USER_LIB_DIRS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_USER_INCLUDES)"; \
	export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_USER_LIBRARY_PROJECTS)";    \
	LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$@ CURRENTLY_BUILDING_LT_LIBRARY=$@ LT_PROJECT=$(shell basename $@) $(MAKE) --no-print-directory -f $(LT_USER_LIBRARY_FORAGER) LT_USER_LIB_DIR=$@;

# LT_KNOWN_PRODUCT_PROJECTS
$(LT_KNOWN_PRODUCT_PROJECTS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PRODUCT_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PRODUCT_LIBRARY_PROJECTS)"; \
    if [ -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$@.mk ] ;    \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$@.mk all;  \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PROJECT_DIR)/$@.mk all;   \
    fi;

# FIRMWARE IMAGE MASTERING PROJECTS
$(LT_PLATFORM_MASTERING_PROJECTS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_MASTERING_PROJECTS)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk all;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$@.mk all; \
    fi;

# BOOTLOADER PROJECTS
$(LT_PLATFORM_BOOTLOADER_PROJECTS): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_BOOTLOADER_PROJECTS)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$@.mk all;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$@ $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$@.mk all; \
    fi;

# super-mega-fast-award-winning CLEAN
.PHONY: clean
clean:
	rm -rf $(LT_TARGET_DIR_THIS_BUILD)

# super-mega-fast-award-winning mega mega mega CLEANALL
.PHONY: cleanall
cleanall:
	rm -rf $(LT_TARGET_ROOT)/targets

# DOXYDOCS
.PHONY: clean-doxydocs
clean-doxydocs:
	rm -rf $(LT_TARGET_DOXYDOCS_DIR)
	rm -f $(LT_TARGET_DOXYDOCS_ERROR_FILE)

.PHONY: doxydocs-files
doxydocs-files: clean-doxydocs
	mkdir -p $(LT_TARGET_DOXYDOCS_DIR)
	# Capture doxgen STDERR into LT_TARGET_DOXYDOCS_ERROR_FILE while displaying all output to the console.
	cd $(LT_OS_ROOT) && $(LT_DOXYDOCS_DOXYGEN_TOOL) $(LT_OS_DOXYFILE) 3>&1 1>&2 2>&3 | tee $(LT_TARGET_DOXYDOCS_ERROR_FILE)
	@echo Documentation created in $(LT_TARGET_DOXYDOCS_DIR)

# doxydocs-files redirects STDERR to LT_TARGET_DOXYDOCS_ERROR_FILE. Check if that file is empty.
.PHONY: check-doxydocs-files
check-doxydocs-files: doxydocs-files
	@echo Checking for documenation build failures...
	@if [ -s "$(LT_TARGET_DOXYDOCS_ERROR_FILE)" ]; then \
		echo "Error: Problems occurred building the documentation see $(LT_TARGET_DOXYDOCS_ERROR_FILE)"; \
		cat $(LT_TARGET_DOXYDOCS_ERROR_FILE); \
		exit 1; \
	else \
		echo "Documentation build successful"; \
		rm -f $(LT_TARGET_DOXYDOCS_ERROR_FILE); \
	fi

.PHONY: launch-doxydocs-files
launch-doxydocs-files:
	$(LT_DOXYDOCS_PREVIEW_BROWSER) file://$(LT_TARGET_DOXYDOCS_INDEX_HTML) > /dev/null 2>&1 &

.PHONY: doxydocs-strict
doxydocs-strict: doxydocs-files check-doxydocs-files launch-doxydocs-files

.PHONY: doxydocs
doxydocs: doxydocs-files launch-doxydocs-files

# SDKDOCS
.PHONY: clean-sdkdocs
clean-sdkdocs:
	rm -rf $(LT_TARGET_SDKDOCS_DIR)
	rm -f $(LT_TARGET_SDKDOCS_ERROR_FILE)

.PHONY: sdkdocs-files
sdkdocs-files: clean-sdkdocs
	mkdir -p $(LT_TARGET_SDKDOCS_DIR)
	# Capture sphinx STDERR into LT_TARGET_SDKDOCS_ERROR_FILE while displaying all output to the console.
	cd $(LT_OS_ROOT) && $(LT_SDKDOCS_SPHINX_TOOL) -M html $(LT_SDK_SOURCE_DIR) $(LT_TARGET_SDKDOCS_DIR) 3>&1 1>&2 2>&3 | tee $(LT_TARGET_SDKDOCS_ERROR_FILE)
	@echo Documentation created in $(LT_TARGET_SDKDOCS_DIR)

# sdkdocs-files redirects STDERR to LT_TARGET_SDKDOCS_ERROR_FILE. Check if that file is empty.
.PHONY: check-sdkdocs-files
check-sdkdocs-files: sdkdocs-files
	@echo Checking for SDK documentation build failures...
	@if [ -s "$(LT_TARGET_SDKDOCS_ERROR_FILE)" ]; then \
		echo "Error: Problems occurred building SDK documentation; see $(LT_TARGET_SDKDOCS_ERROR_FILE)"; \
		cat $(LT_TARGET_SDKDOCS_ERROR_FILE); \
		exit 1; \
	else \
		echo "SDK documentation build successful"; \
		rm -f $(LT_TARGET_SDKDOCS_ERROR_FILE); \
	fi

.PHONY: launch-sdkdocs-files
launch-sdkdocs-files:
	$(LT_SDKDOCS_PREVIEW_BROWSER) file://$(LT_TARGET_SDKDOCS_INDEX_HTML) > /dev/null 2>&1 &

.PHONY: sdkdocs-strict
sdkdocs-strict: doxydocs-files sdkdocs-files check-sdkdocs-files launch-sdkdocs-files

.PHONY: sdkdocs
sdkdocs: doxydocs-files sdkdocs-files launch-sdkdocs-files

.PHONY: clean-sdk-distro
clean-sdk-distro:
	$(LT_QUIET_CMD) @echo RM obj/$(LT_SDK_DISTRO_DIR_STAGING_NAME)
	$(LT_EXEC_CMD)  rm -rf $(LT_SDK_DISTRO_DIR_STAGING_BASE)
	$(LT_QUIET_CMD) @echo RM distros/$(LT_SDK_DISTRO_ZIP_FILE_NAME)
	$(LT_EXEC_CMD)  rm -f $(LT_SDK_DISTRO_ZIP_FILE)

.PHONY: sdk-distro
sdk-distro: clean-sdk-distro opensource-distro
	$(LT_QUIET_CMD) @echo MKDIR distros/
	$(LT_EXEC_CMD)  mkdir -p $(LT_TARGET_DISTRO_DIR_BASE)
	$(LT_QUIET_CMD) @echo MKDIR staging/
	$(LT_EXEC_CMD)  mkdir -p $(LT_SDK_DISTRO_DIR_STAGING_BASE)
	$(LT_QUIET_CMD) @echo UNTAR $(LT_OPENSOURCE_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  cd $(LT_SDK_DISTRO_DIR_STAGING_BASE) && $(LT_TAR) xzf $(LT_OPENSOURCE_DISTRO_TGZ) --transform 's`^$(LT_OPENSOURCE_DISTRO_VERSION)/`$(LT_SDK_CEO_DISTRO_VERSION)/`'
	$(LT_QUIET_CMD) @echo UNTAR $(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  cd $(LT_SDK_DISTRO_DIR_STAGING_BASE) && $(LT_TAR) xf $(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ) --transform 's`^$(LT_OPENSOURCE_DISTRO_VERSION)/`$(LT_SDK_CEO_DISTRO_VERSION)/`'
	$(LT_QUIET_CMD) @echo UNTAR $(LT_OPENSOURCE_TOOLS_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  cd $(LT_SDK_DISTRO_DIR_STAGING_BASE) && $(LT_TAR) xzf $(LT_OPENSOURCE_TOOLS_DISTRO_TGZ) --transform 's`^$(LT_OPENSOURCE_DISTRO_VERSION)/`$(LT_SDK_CEO_DISTRO_VERSION)/`'
	$(LT_QUIET_CMD) @echo "ZIP $(LT_SDK_DISTRO_ZIP_FILE_NAME)"
	$(LT_EXEC_CMD)  cd $(LT_SDK_DISTRO_DIR_STAGING_BASE) && $(LT_ZIP) -q $(LT_SDK_DISTRO_ZIP_FILE) --recurse-paths $(LT_SDK_CEO_DISTRO_VERSION)

.PHONY: clean-opensource-distro
clean-opensource-distro:
	$(LT_QUIET_CMD) @echo RM distros/$(LT_OPENSOURCE_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  rm -f $(LT_OPENSOURCE_DISTRO_TGZ)
	$(LT_QUIET_CMD) @echo RM distros/$(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  rm -f $(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ)
	$(LT_QUIET_CMD) @echo RM distros/$(LT_OPENSOURCE_TOOLS_DISTRO_TGZ_NAME)
	$(LT_EXEC_CMD)  rm -f $(LT_OPENSOURCE_TOOLS_DISTRO_TGZ)

.PHONY: clean-distros
clean-distros:
	$(LT_QUIET_CMD) @echo RM staging/$(LT_SDK_CEO_DISTRO_VERSION)
	$(LT_EXEC_CMD)  rm -rf $(LT_SDK_DISTRO_DIR_STAGING_BASE)
	$(LT_QUIET_CMD) @echo RM $(LT_TARGET_DISTRO_DIR_BASE)
	$(LT_EXEC_CMD)  rm -rf $(LT_TARGET_DISTRO_DIR_BASE)

.PHONY: opensource-distro
opensource-distro: clean-opensource-distro opensource-toolchain-distro opensource-tools-distro
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	$(LT_QUIET_CMD) @echo MKDIR distros/
	$(LT_EXEC_CMD)  mkdir -p $(LT_TARGET_DISTRO_DIR_BASE)
	$(LT_QUIET_CMD) @echo "TAR   $(LT_OPENSOURCE_DISTRO_TGZ)"
	$(LT_EXEC_CMD)  $(LT_TAR) --directory=$(abspath $(LT_ROOTS_BASE)) \
        --mode='u+w' --create --gzip \
		--file $(LT_OPENSOURCE_DISTRO_TGZ) \
        --transform 's`^./lt/source/example`$(LT_OPENSOURCE_DISTRO_VERSION)/lt/examples`' \
    	--transform 's`^.`$(LT_OPENSOURCE_DISTRO_VERSION)/lt`' \
		  ./lt \
          ./platforms/apple \
          ./platforms/common\
          ./platforms/esp32 \
          ./platforms/linux \
          ./platforms/st

.PHONY: opensource-toolchain-distro
opensource-toolchain-distro:
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	$(LT_QUIET_CMD) @echo MKDIR distros/
	$(LT_EXEC_CMD)  mkdir -p $(LT_TARGET_DISTRO_DIR_BASE)
	$(LT_QUIET_CMD) @echo "TAR   $(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ)"
	$(LT_EXEC_CMD)  $(LT_TAR) --directory=$(abspath $(LT_ROOTS_BASE)/..) --mode='u+w' --create --file $(LT_OPENSOURCE_TOOLCHAIN_DISTRO_TGZ) \
    	--transform 's`^`$(LT_OPENSOURCE_DISTRO_VERSION)/`' \
          lt-toolchains/arm-ak3918x-eabi.tgz \
          lt-toolchains/xtensa-esp32-elf-gcc11_2_0-esp-2022r1-linux-amd64.tar.xz

.PHONY: opensource-tools-distro
opensource-tools-distro:
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making $@
	$(LT_QUIET_CMD) @echo MKDIR distros/
	$(LT_EXEC_CMD)  mkdir -p $(LT_TARGET_DISTRO_DIR_BASE)
	$(LT_QUIET_CMD) @echo "TAR   $(LT_OPENSOURCE_TOOLS_DISTRO_TGZ)"
	$(LT_EXEC_CMD)  $(LT_TAR) --directory=$(abspath $(LT_OS_ROOT)/../tools) --mode='u+w' --create --gzip \
       --exclude=source-code/node_modules \
	   --file $(LT_OPENSOURCE_TOOLS_DISTRO_TGZ) --transform 's`^`$(LT_OPENSOURCE_DISTRO_VERSION)/`' lt-commander

# CLEAN-LT_PLATFORM_LTCOREBSP_PROJECT
clean-$(LT_PLATFORM_LTCOREBSP_PROJECT):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning $(subst clean-,,$@)
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
	export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_LTCOREBSP_PROJECT)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst clean-,,$@).mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst clean-,,$@).mk clean;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$(subst clean-,,$@).mk clean; \
    fi;

clean-$(LT_OS_LTK_PROJECT):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning $(subst clean-,,$@)
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_LTK_PROJECT)"; \
    LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst clean-,,$@).mk clean;

# CLEAN-LT_OS_ALL_KNOWN_PROJECTS, e.g. make clean-LTCore, make clean-LTSystemShell
$(foreach lt_project, $(LT_OS_ALL_KNOWN_PROJECTS), clean-$(lt_project)) clean-$(LT_OS_LTCORE_PROJECT):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning $(subst clean-,,$@)
	@LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst clean-,,$@).mk clean

# CLEAN-LT_PLATFORM_ALL_KNOWN_PROJECTS, e.g. make clean-HalfordLTCoreBSP, make clean-HalfordDriverFlash
$(foreach lt_project, $(LT_PLATFORM_ALL_KNOWN_PROJECTS), clean-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning $(subst clean-,,$@)
	@if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst clean-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst clean-,,$@).mk clean; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$(subst clean-,,$@).mk clean; \
	fi

# CLEAN-LT_PRODUCT_ALL_KNOWN_PROJECTS, e.g. make clean-ExciterGenesis
$(foreach lt_project, $(LT_PRODUCT_ALL_KNOWN_PROJECTS), clean-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning $(subst clean-,,$@)
	@if [ -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst clean-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst clean-,,$@).mk clean; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst clean-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PROJECT_DIR)/$(subst clean-,,$@).mk clean; \
	fi

# clean-buildtools
# clean-buildtools
# clean-buildtools
# clean-buildtools, i.e. make clean-buildtools
.PHONY: clean-buildtools
clean-buildtools:
	$(LT_QUIET_CMD) @echo RM $(LT_TARGET_DIR_BUILDTOOLS)
	$(LT_EXEC_CMD) rm -rf $(LT_TARGET_DIR_BUILDTOOLS)

# clean-buildtool-<buildtool>
# clean-buildtool-<buildtool>
# clean-buildtool-<buildtool>
# clean-buildtool-<buildtool>, e.g. make clean-buildtool-arbolator
$(foreach lt_buildtool, $(LT_KNOWN_BUILDTOOLS), clean-buildtool-$(lt_buildtool)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Cleaning BUILDTOOL $(subst clean-buildtool-,,$@) | sed 's/^/    /'
	@$(MAKE) --no-print-directory -C $(LT_BUILDTOOLS_ROOT)/$(subst clean-buildtool-,,$@)/build -f LTBuildTool.mk clean | sed 's/^/    /' | awk -v rc=0 '/recipe for target/ { rc=1 } 1; END {exit rc}'

# rebuild-buildtool-<buildtool>
# rebuild-buildtool-<buildtool>
# rebuild-buildtool-<buildtool>
# rebuild-buildtool-<buildtool>, e.g. make rebuild-buildtool-arbolator
$(foreach lt_buildtool, $(LT_KNOWN_BUILDTOOLS), rebuild-buildtool-$(lt_buildtool)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding BUILDTOOL $(subst rebuild-buildtool-,,$@) | sed 's/^/    /'
	@$(MAKE) --no-print-directory -C $(LT_BUILDTOOLS_ROOT)/$(subst rebuild-buildtool-,,$@)/build -f LTBuildTool.mk clean all | sed 's/^/    /' | awk -v rc=0 '/recipe for target/ { rc=1 } 1; END {exit rc}'

# buildtool-<buildtool>
# buildtool-<buildtool>
# buildtool-<buildtool>
# buildtool-<buildtool>, e.g. make buildtool-arbolator
$(foreach lt_buildtool, $(LT_KNOWN_BUILDTOOLS), buildtool-$(lt_buildtool)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Making BUILDTOOL $(subst buildtool-,,$@) | sed 's/^/    /' $(LT_HIDE_BUILDTOOL)
	@$(MAKE) --no-print-directory -C $(LT_BUILDTOOLS_ROOT)/$(subst buildtool-,,$@)/build -f LTBuildTool.mk all | sed -u 's/^/    /' | awk -v rc=0 '/recipe for target/ { rc=1 } 1; END {exit rc}' $(LT_HIDE_BUILDTOOL)

# rebuild-<lt_platform_ltcorebsp_project>
# rebuild-<lt_platform_ltcorebsp_project>
# rebuild-<lt_platform_ltcorebsp_project>
# rebuild-<lt_platform_ltcorebsp_project>, e.g. make rebuild-LinuxLTCoreBSP, do this separately because of non default output type of static lib
rebuild-$(LT_PLATFORM_LTCOREBSP_PROJECT): toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)"; \
	export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_PLATFORM_LTCOREBSP_PROJECT)"; \
    if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst rebuild-,,$@).mk ] ;   \
    then                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst rebuild-,,$@).mk clean all;    \
    else                                            \
        LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all; \
    fi;

# rebuild-LTK
rebuild-$(LT_OS_LTK_PROJECT) : toolchain targetdirs
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_LTK_PROJECT)"; \
    LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=no LT_PROJECT_BUILD_STATIC_LIB=yes LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all;

# rebuld-LTCore
rebuild-$(LT_OS_LTCORE_PROJECT) : $(patsubst %,rebuild-%,$(LT_LTCORE_PROJECT_DEPS))
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@export LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)"; \
    export LT_TARGET_RULE_DEFS="$(LT_TARGET_RULE_DEFS_LT_OS_LTCORE_PROJECT)"; \
    LT_PROJECT_KEEP_OS_OPTIMIZATION=$(LT_PROJECT_KEEP_OS_OPTIMIZATION) LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all;

# rebuild-<lt_os_projects>
# rebuild-<lt_os_projects>
# rebuild-<lt_os_projects>
# rebuild-<lt_os_projects>, e.g. make rebuild-LTSystemNetwork
$(foreach lt_project, $(LT_OS_ALL_KNOWN_PROJECTS), rebuild-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)" LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all;

# rebuild-<lt_platform_projects>
# rebuild-<lt_platform_projects>
# rebuild-<lt_platform_projects>
# rebuild-<lt_platform_projects>, e.g. make rebuild-HalfordDriverFlash, make rebuild-HalfordDriverPins
$(foreach lt_project, $(LT_PLATFORM_ALL_KNOWN_PROJECTS), rebuild-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst rebuild-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)" LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst rebuild-,,$@).mk clean all; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)" LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all; \
	fi

# rebuild-<lt_product_projects>
# rebuild-<lt_product_projects>
# rebuild-<lt_product_projects>
# rebuild-<lt_product_projects>, e.g. make rebuild-ExciterGenesis
$(foreach lt_project, $(LT_PRODUCT_ALL_KNOWN_PROJECTS), rebuild-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Rebuilding $(subst rebuild-,,$@)
	@if [ -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst rebuild-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PRODUCT_INCLUDE)" LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst rebuild-,,$@).mk clean all; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PRODUCT_INCLUDE)" LT_PROJECT=$(subst rebuild-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PROJECT_DIR)/$(subst rebuild-,,$@).mk clean all; \
	fi

# examinate-LT_OS_ALL_KNOWN_PROJECTS, e.g. make examinate-LTCore, make examinate-LTSystemNetwork
$(foreach lt_project, $(LT_OS_ALL_KNOWN_PROJECTS), examinate-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Examinating $(subst examinate-,,$@)
	@LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_OS_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_OS_INCLUDE)" LT_PROJECT=$(subst examinate-,,$@) $(MAKE) --no-print-directory -f $(LT_OS_BUILD_PROJECT_DIR)/$(subst examinate-,,$@).mk examinate;

# examinate-LT_KNOWN_PLATFORM_PROJECTS, e.g. make examinate-HalfordLTCoreBSP, make examinate-HalfordDriverFlash
$(foreach lt_project, $(LT_KNOWN_PLATFORM_PROJECTS), examinate-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Examinating $(subst examinate-,,$@)
	@if [ -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst examinate-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)" LT_PROJECT=$(subst examinate-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)/$(subst examinate-,,$@).mk examinate; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PLATFORM_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PLATFORM_INCLUDE)" LT_PROJECT=$(subst examinate-,,$@) $(MAKE) --no-print-directory -f $(LT_PLATFORM_BUILD_PROJECT_DIR)/$(subst examinate-,,$@).mk examinate; \
	fi

# don't examinate the mastering and bootloader projects
$(foreach lt_project, $(LT_PLATFORM_MASTERING_PROJECTS), examinate-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Examinating $(subst examinate-,,$@)
	@echo EXAMINATE $(subst examinate-,,$@) is a mastering project, skipping...

$(foreach lt_project, $(LT_PLATFORM_BOOTLOADER_PROJECTS), examinate-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Examinating $(subst examinate-,,$@)
	@echo EXAMINATE $(subst examinate-,,$@) is a bootloader project, skipping...

# examinate-LT_PRODUCT_ALL_KNOWN_PROJECTS, e.g. make examinate-ExciterGenesis
$(foreach lt_project, $(LT_PRODUCT_ALL_KNOWN_PROJECTS), examinate-$(lt_project)):
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Examinating $(subst examinate-,,$@)
	@if [ -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst examinate-,,$@).mk ]; then \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PRODUCT_INCLUDE)" LT_PROJECT=$(subst examinate-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PRODUCT_VARIANT_DIR)/$(subst examinate-,,$@).mk examinate; \
	else \
		LT_PROJECT_BUILD_SHARED_LIB=yes LT_PROJECT_BUILD_STATIC_LIB=no LT_PROJECT_BUILD_EXECUTABLE=no LT_PROJECT_ARTIFACT= LT_PROJECT_SOURCE_DIR_BASE=$(LT_PRODUCT_PRIVATE_SOURCE_DIR) LT_PUBLIC_INCLUDE_FLAGS="$(LT_CFLAGS_PRODUCT_INCLUDE)" LT_PROJECT=$(subst examinate-,,$@) $(MAKE) --no-print-directory -f $(LT_PRODUCT_BUILD_PROJECT_DIR)/$(subst examinate-,,$@).mk examinate; \
	fi

# EXAMINATE
.PHONY: clean-examinate
clean-examinate:
	rm -rf $(LT_TARGET_EXAMINATE_DIR)

.PHONY: examinate-projects
examinate-projects: $(foreach lt_project, $(LT_OS_ALL_KNOWN_PROJECTS), examinate-$(lt_project)) $(foreach lt_project, $(LT_PLATFORM_ALL_KNOWN_PROJECTS), examinate-$(lt_project)) $(foreach lt_project, $(LT_PRODUCT_ALL_KNOWN_PROJECTS), examinate-$(lt_project))

.PHONY: examinate
examinate: clean-examinate examinate-projects

.PHONY: stats-summarize
ifneq ($(LT_DISABLE_STATS), yes)
stats-summarize:
	$(LT_ARROWLINE)
	@echo $(LT_ARROW_SEPARATOR) Generating code size summary
	$(LT_QUIET_CMD) @echo SUMMARIZE $(LT_TARGET_STATS_DIR)
	$(LT_EXEC_CMD) $(LT_OS_BUILD_SCRIPTS_DIR)/stats/genstats-summarize-codesize $(LT_TARGET_STATS_DIR)
	$(LT_EXEC_CMD) $(LT_OS_BUILD_SCRIPTS_DIR)/stats/genstats-summarize-stackusage $(LT_TARGET_STATS_DIR)
	@echo
	@cat $(LT_TARGET_STATS_DIR)/LTLIBRARY-CODESIZE-SUMMARY.txt
else
stats-summarize: ;
endif

ifeq (yes, $(LT_PLATFORM_HAS_RUNTIME_DYNAMIC_LOADER))
    LT_LIBRARY_SUFFIX := .so
    LT_LIBRARY_LIB_DIR := $(LT_TARGET_BIN_DIR)
else
    LT_LIBRARY_SUFFIX := .a
    LT_LIBRARY_LIB_DIR := $(LT_TARGET_LIB_DIR)
endif
LT_ALL_LIBRARY_PROJECTS_THIS_BUILD := $(LT_BUILD_OS_PROJECTS) $(LT_BUILD_PLATFORM_PROJECTS) $(LT_BUILD_PRODUCT_PROJECTS) $(basename $(notdir $(LT_USER_LIB_DIRS)))
LT_ALL_LIBRARIES_THIS_BUILD := $(patsubst %,$(LT_LIBRARY_LIB_DIR)/lib%$(LT_LIBRARY_SUFFIX),$(LT_ALL_LIBRARY_PROJECTS_THIS_BUILD))

.PHONY: listlibraries
listlibraries:
	@for lib in $(LT_ALL_LIBRARIES_THIS_BUILD); do \
		if [ -f "$$lib" ]; then \
			echo "$$lib"; \
		fi \
	done

.PHONY: $(LT_BUILDINFO_FILE)
# This is preliminary, to get builds going as soon as possible.
# Parts that are missing that will likely need to be addressed, now or eventually:
# - Add proper PLID (not necessary for reference platforms)
# - Check build number to ensure modulo 100000 (limit to 5 digits)
# - Check MAJOR and MINOR for base-31 validity
# - separate MINOR digits by '.' (maybe)
buildinfo: targetdirs $(LT_BUILDINFO_FILE)
	@echo "##teamcity[buildNumber '"$(LT_BUILD_VERSION)"']" | tee $(LT_BUILDINFO_FILE)
	@echo "##teamcity[setParameter name='env.BN' value='"$(LT_BUILD_VERSION)"']" | tee -a $(LT_BUILDINFO_FILE)

.PHONY: versioninfo
versioninfo: $(LT_VERSIONINFO_FILE)
$(LT_VERSIONINFO_FILE): targetdirs
	@echo "LT_SOFTWARE_VERSION=$(LT_SOFTWARE_VERSION)" > $@
	@echo "LT_BUILD_VERSION=$(LT_BUILD_VERSION)" >> $@
	@echo "LT_PLATFORM_DISTRO_VERSION=$(LT_PLATFORM_DISTRO_VERSION)" >> $@
	@echo "LT_OPENSOURCE_DISTRO_VERSION=$(LT_OPENSOURCE_DISTRO_VERSION)" >> $@
	@echo "LT_VERSION_COMMIT=$(LT_VERSION_COMMIT)" >> $@

.PHONY: publish
publish: $(LT_TARGET_BUILD_ARTIFACTS_DIR)
	$(LT_QUIET_CMD) @echo Make build-artifact package
	$(LT_EXEC_CMD)  @tar -C $(abspath $(LT_TARGET_BIN_DIR)/../..)                       \
						--transform 's/^$(LT_BUILD_MODE)/$(LT_BUILD_ARTIFACT_PACKAGE)/' \
						-czf $(LT_BUILD_ARTIFACT_PACKAGE_PATH) $(LT_BUILD_MODE)/bin     \
																$(LT_BUILD_MODE)/obj    \
																$(LT_BUILD_MODE)/lib    \
																$(LT_BUILD_MODE)/stats
	$(LT_QUIET_CMD) @echo Publish artifacts to TeamCity
	@$(foreach lt_artifact,$(LT_PUBLISHED_ARTIFACTS),echo "##teamcity[publishArtifacts '$(lt_artifact)']";)

.PHONY: publish-opensource-distro
publish-opensource-distro:
	@echo "##teamcity[publishArtifacts '$(LT_OPENSOURCE_DISTRO_TGZ)']"

ifeq (yes,$(LT_SAVECONFIG))
.PHONY: saveconfig
ifeq ($(LT_PRODUCT),$(LT_PRODUCT_FAMILY))
    LT_SAVECONFIG_FUNCTION_PRODUCT:=$(LT_PRODUCT)
else
    LT_SAVECONFIG_FUNCTION_PRODUCT:=$(LT_PRODUCT)$(LT_PRODUCT_FAMILY)
endif
ifeq ($(LT_PLATFORM),$(LT_PLATFORM_FAMILY))
    LT_SAVECONFIG_FUNCTION_PLATFORM:=$(LT_PLATFORM)
else
    LT_SAVECONFIG_FUNCTION_PLATFORM:=$(LT_PLATFORM)$(LT_PLATFORM_FAMILY)
endif
ifeq ($(LT_SAVECONFIG_FUNCTION_PRODUCT),$(LT_SAVECONFIG_FUNCTION_PLATFORM))
    LT_SAVECONFIG_FUNCTION:=$(LT_SAVECONFIG_FUNCTION_PRODUCT)_$(LT_BUILD_MODE)
else
    LT_SAVECONFIG_FUNCTION:=$(LT_SAVECONFIG_FUNCTION_PRODUCT)_$(LT_SAVECONFIG_FUNCTION_PLATFORM)_$(LT_BUILD_MODE)
endif
saveconfig:
	@echo "#############################################"
	@echo "# ltunset - unset lt configuration variables"
	@echo "#"
	@echo "# Run \"ltunset\" to unset all LT build configuration variables"
	@echo "#"
	@echo "# Copy [once] into your .bashrc or equivalent."
	@echo "# -------------------------------------"
	@echo "#"
	@echo "ltunset() {"
	@echo '    unset LT_OS_ROOT LT_PRODUCT_ROOT LT_PLATFORM_ROOT LT_TARGET_ROOT LT_BUILD_MODE'
	@echo '    unset LT_PRODUCT LT_PLATFORM LT_BUILD_BASE LT_OUTPUT_PRODUCT LT_OUTPUT_PLATFORM LT_PLATFORM_VARIANT_DIR'
	@echo "}"
	@echo
	@echo
	@echo "##############################################"
	@echo "# function: ltset_$(LT_SAVECONFIG_FUNCTION)"
	@echo "#"
	@echo "# Run \"ltset_$(LT_SAVECONFIG_FUNCTION)\" to set"
	@echo "# LT build configuration variables to build"
	@echo "# the $(LT_SAVECONFIG_FUNCTION) configuration."
	@echo "#"
	@echo "# Copy into your .bashrc or equivalent."
	@echo "# -------------------------------------"
	@echo "#"
	@echo "ltset_$(LT_SAVECONFIG_FUNCTION)() {"
	@echo "    export LT_OS_ROOT=$(LT_OS_ROOT)"
	@echo "    export LT_PRODUCT_ROOT=$(LT_PRODUCT_ROOT)"
	@echo "    export LT_PLATFORM_ROOT=$(LT_PLATFORM_ROOT)"
	@echo "    export LT_TARGET_ROOT=$(LT_TARGET_ROOT)"
	@echo "    export LT_PRODUCT=$(LT_PRODUCT)"
	@echo "    export LT_PLATFORM=$(LT_PLATFORM)"
	@echo "    export LT_BUILD_MODE=$(LT_BUILD_MODE)"
	@echo "    export LT_BUILD_BASE=$(LT_MAKE_INVOCATION_ROOT)"
	@echo "    export LT_OUTPUT_PRODUCT=$(LT_VERSION_PRODUCT)"
	@echo "    export LT_OUTPUT_PLATFORM=$(LT_VERSION_PLATFORM)"
	@echo "    export LT_PLATFORM_VARIANT_DIR=$(LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR)"
	@echo "}"
	@echo
	@echo "# set the $(LT_SAVECONFIG_FUNCTION) configuration for building"
	@echo "ltset_$(LT_SAVECONFIG_FUNCTION)"
	@echo
	@echo
	@echo "#######################################"
	@echo "# ltwhich - report LT build directories"
	@echo "#"
	@echo "# Run any ltset_ function and ltwhich will report"
	@echo "# that configuration's build/, bin/, lib/, obj/, stats/,"
	@echo "# product root, platform root and os root directories with a simple invocation, e.g. ltwhich build, ltwhich bin, etc."
	@echo "# Run a different ltset_ function and ltwhich automatically tracks to the new configuration."
	@echo "# "
	@echo "# Copy [once] into your .bashrc or equivalent."
	@echo "# --------------------------------------------"
	@echo "#"
	@echo "ltwhich() {"
	@echo '    case "$$1" in'
	@echo '                  build) echo $${LT_BUILD_BASE}/build ;;'
	@echo '                product) echo $${LT_PRODUCT_ROOT} ;;'
	@echo '               platform) echo $${LT_PLATFORM_ROOT} ;;'
	@echo '                     os) echo $${LT_OS_ROOT} ;;'
	@echo '      bin|lib|obj|stats) echo $${LT_TARGET_ROOT}/targets/$${LT_OUTPUT_PRODUCT}/$${LT_OUTPUT_PLATFORM}/$${LT_BUILD_MODE}/$$1 ;;'
	@echo '                distros) echo $${LT_TARGET_ROOT}/targets/distros ;;'
	@echo '                      *) echo "usage: ltwhich [build|bin|lib|obj|stats|product|platform|os]" ;;'
	@echo '    esac'
	@echo "}"
	@echo
	@echo "#############################################"
	@echo "# ltcd - easily traverse LT build directories"
	@echo "#"
	@echo "# Run any ltset_ function and ltcd will effortlessly change back and forth into"
	@echo "# (cd into) that configuration's build/, bin/, lib/, obj/, stats/,"
	@echo "# product root, platform root and os root directories with a simple invocation, e.g. ltcd build, ltcd bin, etc."
	@echo "# Run a different ltset_ function and ltcd automatically tracks to the new configuration."
	@echo "# "
	@echo "# Copy [once] into your .bashrc or equivalent."
	@echo "# --------------------------------------------"
	@echo "#"
	@echo "ltcd() {"
	@echo '    case "$$1" in'
	@echo '        build|product|platform|os|bin|lib|obj|stats|distros) cd `ltwhich $$1` ;;'
	@echo '        *) echo "usage: ltcd [build|bin|lib|obj|stats|distros|product|platform|os]" ;;'
	@echo '    esac'
	@echo "}"
	@echo
	@echo "# export ltwhich and ltcd for use by subshells"
	@echo "export -f ltwhich > /dev/null 2>&1"
	@echo "export -f ltcd > /dev/null 2>&1"
	@echo

endif

###########
# dumpvar-%  prints VARNAME=value in the style of "make dumpvars" for interactive queries, e.g.
#              % make dumpvar-LT_PRODUCT
#              LT_PRODUCT=indoor_camera
dumpvar-%:
	@echo && echo "${@:dumpvar-%=%}=${${@:dumpvar-%=%}}"


############
# printvar-% prints the value of VARNAME by itself without any extra adornments so external scripts
#            can easily ingest computed values like that of LT_TARGET_BIN_DIR.
#            Caution: modifying this target may increase the risk of build breakage. :)
printvar-%:
	@echo "${${@:printvar-%=%}}"


###############################################################################
#   LOG
###############################################################################
#   20-Jan-20   augustus    created
#   02-Feb-20   augustus    added LT_THIRDPARTY support
#   02-Mar-20   augustus    added LT_CFLAGS_LIBRARY and LT_CXXFLAGS_LIBRARY
#                           and listlibraries target
#   05-Mar-20   licinius    anded LTMediaSASReceiver / media
#   18-Mar-20   augustus    added LT_ECHO, dump LT_CFLAGS and LT_CXXFLAGS for 'make dumpvars'
#   30-Mar-20   diocletian  moved RingBuffer<T> and etc to LTDevicePCMAudioOutput
#   11-Apr-20   augustus    renamed LT_ECHO to LT_ECHO_ESCAPED with test for -e support
#                           comment out LTMediaSASReceiver until external dependency free
#   17-Jun-20   caligula    fix make error when determining LT_ARCHITECTURE_DEFS
#   20-Jun-20   augustus    added LT_GENESIS_PROJECTS
#   04-Jul-20   augustus    added LT_TARGET_STATS_DIR
#   09-Jul-20   caligula    allow disablement of stacksize and codesize statistics
#   28-Sep-20   constantine added toolchain install
#   12-Oct-20   constantine added preliminary TeamCity support
#   06-Nov-20   augustus    added LTSystemShell
#   05-Nov-20   hadrian     added LTDeviceWiFi
#   08-Nov-20   augustus    added LTSystemSettings, LTDeviceFlash
#   09-Nov-20   augustus    added HalfordDeviceFlash, but only temporarily
#   06-Dec-20   constantine added TryoutPrintfFormatCheckTest
#   05-Dec-20   augustus    special case the rebuild target if 'test' is in MAKECMDGOALS
#   21-Dec-20   augustus    added Carl's TryoutPingPongTest; commented out the other tryout libs for now
#   05-Jan-21   augustus    added initial support for bifurcation of product and platform from OS
#   14-Jan-21   augustus    full support for product root, platform root and lt os root
#                           where platform and product roots are platform and product 'families' with
#                           arbitrary platform and product variants
#   15-Jan-21   augustus    make LT_ROOTS_BASE the default LT_TARGET_ROOT; added cleanall target
#   27-Jan-21   constantine added LTDeviceThermoMeter
#   28-Jan-21   constantine added LTDeviceLED
#   29-Jan-21   augustus    added LT_TARGET_RULE_DEFS
#   01-Feb-21   hadrian     added LTUtilityMacAddress
#   10-Feb-21   constantine added LTDevicePushButton
#   06-Mar-21   augustus    don't duplicate -I if os root is the product root
#   20-Mar-21   augustus    added LT_UNZIP and LT_UNZIP_FLAGS
#   10-Jun-21   augustus    prettified quiet mode for distro targets; make distro basename root dir inside tarball
#   11-Jun-21   augustus    added the perforce changelist number to the version string
#   10-Aug-21   augustus    allow LT_OS_PROJECT .mk file overrides in LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR
#   16-Sep-21   augustus    add human readable time of day to dev.unofficial build version string
#   28-Oct-21   augustus    only prepend LT_CCACHE if it is non-empty; empty was prepending a space and messing with configure
#   06-Nov-21   augustus    added LT_LD_FRONTEND
#   22-Nov-21   augustus    added LTUtilityByteOps
#   24-Jan-22   augustus    commented out media libraries and alloy to save on static ram
#   22-Mar-22   vitellius   added LTUtilityDSPFilter
#   16-May-22   augustus    filter driver out when corresponding device library is excluded from build
#   17-May-22   augustus    added LTNetSNTPClient
#   31-Aug-22   augustus    added examinate targets
#   22-Dec-22   augustus    fake change to try and get gitlab to run the darn pipelines!
#   27-Feb-23   augustus    scan for known libraries of all types so they don't need to be listed explicity;
#                           added buildtool auto-build capability;
#                           arbolate PLATFORM_VARIANT/LTDeviceConfig.json into LTDeviceConfig_LibraryIncludes.mk and
#                           arbolate PRODUCT_VARIANT/LTProductConfig.json into LTProductConfig_LibraryIncludes.mk
#                           for specification of build included libraries from LTDeviceConfig.json and LTProductConfig.json
#   04-Aug-23   augustus    only scan PRODUCT_BUILD_PROJECT_DIR if it exists
#   14-Jun-24   domitian    added all variables under the LT_USER_ namespace for implementing "Local User Libraries" and
#                           the LT_USER_LIBRARY_FORAGER to reference the UserLib.mk
#   26-Jun-24   domitian    LT_USER_ROOT is now treated as an optional enviromental variable to be set during the build process
#   03-Jul-24   domitian    Removed publish-platform-distro & platform-distro, added opensource-distro & publish-opensource-distro
#   16-Jul-24   domitian    added opensource-toolchain-distro & opensource-tools-distro targets

