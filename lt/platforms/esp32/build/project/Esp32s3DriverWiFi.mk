################################################################################
# Esp32s3DriverWiFi.mk
#
# Esp32s3DriverWiFi.mk - project Esp32s3DriverWiFi
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################


WIFI_DRV_DIR  = esp-wireless-drivers-3rdparty

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/wifi/esp32s3
LT_PROJECT_SOURCE_FILES      :=	Esp32s3DriverWiFi.c

LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include/esp32s3

# make
include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################
#   LOG
################################################################################
#   27-Aug-26   claudius    created
