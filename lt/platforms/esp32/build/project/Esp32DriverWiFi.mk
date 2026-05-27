################################################################################
# Esp32_DriverWiFi.mk
#
# Esp32_DriverWiFi.mk - project Esp32_DriverWiFi
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################


WIFI_DRV_DIR  = esp-wireless-drivers-3rdparty
# WiFi driver package from Espressif
#WIFI_DRV_ID      = 45701c0
#WIFI_DRV_ZIP     = $(WIFI_DRV_ID).zip
#WIFI_DRV_URL     = https://github.com/espressif/esp-wireless-drivers-3rdparty/archive

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/wifi
LT_PROJECT_SOURCE_FILES      :=	Esp32DriverWiFi.c

LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include/esp32

# make
include $(LT_PROJECT_RULES_MAKEFILE)
