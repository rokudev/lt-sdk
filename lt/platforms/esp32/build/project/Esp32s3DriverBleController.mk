################################################################################################
# Esp32s3DriverBleController.mk
#
# Esp32s3DriverBleController.mk - project Esp32s3DriverBleController
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################################

BLE_DRV_DIR  = esp-wireless-drivers-3rdparty

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/blecontroller/esp32s3
LT_PROJECT_SOURCE_FILES      += Esp32s3DriverBleController.c

LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PROJECT_SOURCE_DIR)
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PROJECT_SOURCE_DIR)/../../esp32-lt-os-adapter
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(BLE_DRV_DIR)/include
# esp_bt.h under esp32s3/ is a copy of the esp32c3 header, because IDF v4.4's
# components/bt builds the esp32s3 against controller/esp32c3 and
# include/esp32c3/include.  The esp32c3 directory itself must stay off the
# include path: esp_bt.h pulls in "sdkconfig.h" from its own directory, so
# adding it would drag the esp32c3 sdkconfig in alongside the esp32s3 one.
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(BLE_DRV_DIR)/include/esp32s3

# make
include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################################
#   LOG
################################################################################################
#   27-Aug-26   claudius    created
