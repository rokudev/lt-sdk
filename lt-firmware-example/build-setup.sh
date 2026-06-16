#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
#
# This file adds LT build aliases to your shell for ease of use
# Run it by typing "source build-setup.sh" on the command line of your shell

################################################################################################
# 1. set LT_GIT_BASE and FIRMWARE_EXAMPLE_GIT_BASE to the base of the lt and lt-firmware-example
#    dirs from their (possibly respective/distinct) git repos
# 2. no other variables in this file need changing
# 3. if LT_GIT_BASE or FIRMWARE_EXAMPLE_GIT_BASE are not set, an attempt
#    will be made to auto-locate them based on the location of this script

# if empty, will autodetect
LT_GIT_BASE=
FIRMWARE_EXAMPLE_GIT_BASE=

# example manual settings
#  LT_GIT_BASE=~/git/lt-sdk/lt
#  FIRMWARE_EXAMPLE_GIT_BASE=~/git/lt-sdk/lt-firmware-example

######
# test for bash, bash required
if test -z "$BASH"; then 
  echo "This script requires the bash shell"
  exit 0
fi

#######
# fixup dirs with realpath or auto-detect
if [ "$FIRMWARE_EXAMPLE_GIT_BASE" != "" ]; then
  FIRMWARE_EXAMPLE_GIT_BASE="$(realpath "$FIRMWARE_EXAMPLE_GIT_BASE")"
else
  FIRMWARE_EXAMPLE_GIT_BASE="$(dirname "$(realpath "$BASH_SOURCE")")"
fi

if [ "$LT_GIT_BASE" != "" ]; then
  LT_GIT_BASE="$(realpath "$LT_GIT_BASE")"
  else
  LT_GIT_BASE="$(realpath "$FIRMWARE_EXAMPLE_GIT_BASE/../lt")"
fi

export LT_GIT_BASE
export FIRMWARE_EXAMPLE_GIT_BASE

#echo LT_GIT_BASE=$LT_GIT_BASE
#echo FIRMWARE_EXAMPLE_GIT_BASE=$FIRMWARE_EXAMPLE_GIT_BASE

#############################################
# ltunset - unset lt configuration variables
#
# Run "ltunset" to unset all LT build configuration variables
# -------------------------------------
#
ltunset() {
    unset LT_OS_ROOT LT_PRODUCT_ROOT LT_PLATFORM_ROOT LT_TARGET_ROOT LT_BUILD_MODE
    unset LT_PRODUCT LT_PLATFORM LT_BUILD_BASE LT_OUTPUT_PRODUCT LT_OUTPUT_PLATFORM
}

#######################################
# ltwhich - report LT build directories
#
# ltwhich will report the current build configuration's
# configuration's build/, bin/, lib/, obj/, stats/,
# with a simple invocation, e.g. ltwhich build, ltwhich bin, etc.
# --------------------------------------------
#
ltwhich() {
    case "$1" in
                  build) echo ${LT_BUILD_BASE}/build ;;
                product) echo ${LT_PRODUCT_ROOT} ;;
               platform) echo ${LT_PLATFORM_ROOT} ;;
                     os) echo ${LT_OS_ROOT} ;;
      bin|lib|obj|stats) echo ${LT_TARGET_ROOT}/targets/${LT_OUTPUT_PRODUCT}/${LT_OUTPUT_PLATFORM}/${LT_BUILD_MODE}/$1 ;;
                distros) echo ${LT_TARGET_ROOT}/targets/distros ;;
                      *) echo "usage: ltwhich [build|bin|lib|obj|stats|product|platform|os]" ;;
    esac
}

#############################################
# ltcd - easily traverse LT build directories
#
# ltcd will effortlessly change back and forth into (cd into) 
# the current build configuration's build/, bin/, lib/, obj/, stats/,
# directories, e.g. ltcd build, ltcd bin, etc.
# --------------------------------------------
#
ltcd() {
    case "$1" in
        build|product|platform|os|bin|lib|obj|stats|distros) cd `ltwhich $1` ;;
        *) echo "usage: ltcd [build|bin|lib|obj|stats|distros|product|platform|os]" ;;
    esac
}

# export ltwhich and ltcd for use by subshells
export -f ltwhich > /dev/null 2>&1
export -f ltcd > /dev/null 2>&1

# common build env variables
export LT_OS_ROOT=${LT_GIT_BASE}/lt
export LT_BUILD_DIR_LT=${LT_OS_ROOT}/build
export LT_BUILD_DIR_EXAMPLE=${FIRMWARE_EXAMPLE_GIT_BASE}/build

# quick aliases to switch into relevant LT directories
alias bin='ltcd bin'
alias obj='ltcd obj'
alias lib='ltcd lib'
alias stats='ltcd stats'
alias build='cd ${LT_BUILD_DIR}'

#alias to rehash so ltwhich and ltcd work after changing environment variables 
alias rehash='. <(cd ${LT_BUILD_DIR} && make saveconfig)'
alias   espdhrystone='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=esp32 LT_PLATFORM=esp32-minimal LT_PRODUCT=dhrystone LT_BUILD_MODE=release && rehash'
alias   espshell='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=esp32 LT_PLATFORM=esp32-minimal LT_PRODUCT=shell LT_BUILD_MODE=release && rehash'
alias   espwifi='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=esp32 LT_PLATFORM=esp32-wifi LT_PRODUCT=shell-with-wifi LT_BUILD_MODE=release && rehash'
alias   eyeclops='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=esp32 LT_PLATFORM=esp32-eyeclops LT_PRODUCT=eyeclops LT_BUILD_MODE=release && rehash'
alias linuxshell='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=linux LT_PLATFORM=x86_minimal LT_PRODUCT=shell LT_BUILD_MODE=release && rehash'
alias linuxwifi='ltunset && export LT_BUILD_DIR=${LT_BUILD_DIR_EXAMPLE} LT_PLATFORM_ROOT=linux LT_PLATFORM=x86_minimal_wifi LT_PRODUCT=shell-with-wifi LT_BUILD_MODE=release && rehash'

# quick aliases to run picocom on /dev/ttyUSB0
alias pcom='picocom -b 115200 --imap=lfcrlf --omap=delbs --lower-dtr --lower-rts /dev/ttyUSB0'

# run the alias "espshell" to set the example shell esp build by default
espshell

echo "______________"
echo "build-setup.sh"
echo
echo "  ADDED ALIASES: build, bin, obj, stats"
echo "               - quick cd into the build, bin, obj, and stats directories of the current build"
echo
echo "  ADDED ALIASES: espdhrystone, espshell, espwifi, linuxshell, linuxwifi"
echo "               - quick set environment variables for building esp and linux builds of example firmware"
echo
echo "  ADDED ALIASES: eyeclops"
echo "               - quick set environment variables for building new ESP32-CAM application demo"
echo
echo "  ADDED ALIASES: pcom"
echo "               - run picocom on ttyUSB0"
echo "================"
