################################################################################################
# Esp32DriverBleController.mk
#
# Esp32DriverBleController.mk - project Esp32DriverBleController
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################################

BLE_DRV_DIR  = esp-wireless-drivers-3rdparty

# source dir and files
LT_PROJECT_SOURCE_DIR	     :=	$(LT_PROJECT_SOURCE_DIR_BASE)/esp32/driver/blecontroller
LT_PROJECT_SOURCE_FILES      += Esp32DriverBleController.c

LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PROJECT_SOURCE_DIR)
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PROJECT_SOURCE_DIR)/../lt-esp32-os-adapter
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(BLE_DRV_DIR)/include
LT_PUBLIC_INCLUDE_FLAGS      += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(BLE_DRV_DIR)/include/esp32

# make
include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################################
#   LOG
################################################################################################
#   15-Aug-22   vespasian   created
#   12-Sep-22   vespasian   cleaned up
#   12-Feb-23   gallienus   revised
