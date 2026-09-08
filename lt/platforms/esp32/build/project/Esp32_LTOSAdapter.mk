################################################################################
# Esp32_LTOSAdapter.mk
#
# Esp32_LTOSAdapter.mk - adapt LT to the poorly designed ESP32 os independent layer
# to satisfy the ESP32 less than satisfactory opaque binary libraries for BLE and
# WiFi, for which we sadly have no source.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB := no
LT_PROJECT_BUILD_STATIC_LIB := yes
LT_PROJECT_BUILD_EXECUTABLE := no

# source dir and files
LT_PROJECT_SOURCE_DIR    := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/esp32-lt-os-adapter
LT_PROJECT_SOURCE_FILES  := Esp32_LTOSAdapter.c
LT_PROJECT_SOURCE_FILES  += intrs.S

# The per-chip directory supplies sdkconfig.h, which is what selects the target
# shape of coex_adapter_funcs_t and wifi_osi_funcs_t in the shared headers.  The
# chip conditionals in the sources therefore read CONFIG_IDF_TARGET_*, exactly as
# the IDF sources they mirror do.
LT_PUBLIC_INCLUDE_FLAGS  += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp-wireless-drivers-3rdparty/include
LT_PUBLIC_INCLUDE_FLAGS  += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/esp-wireless-drivers-3rdparty/include/$(SOC_PLATFORM_NAME)

# The esp32s3 register, clock and timer entry points the shared table needs.
# The extra include reaches the WPA supplicant port's rom/ets_sys.h, which is
# where the ETSTimer declarations this file defines against live - the esp32
# takes the same seven entry points from ROM instead.  That directory holds
# nothing else.
ifeq ($(SOC_PLATFORM_NAME),esp32s3)
LT_PROJECT_SOURCE_FILES  += Esp32s3_LTOSAdapter.c
LT_PUBLIC_INCLUDE_FLAGS  += -I$(LT_PROJECT_SOURCE_DIR_BASE)/esp32/thirdparty/wpa_supplicant/roku-lt-port/esp32s3
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)
