################################################################################
# STLTCoreBSP.mk - makefile for ST BSP for LT
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

ST_LTCOREBSP_PATH := source/st/ltcorebsp

# source dir and files
LT_PROJECT_SOURCE_DIR     := $(LT_PLATFORM_ROOT)

LT_PROJECT_SOURCE_SUBDIRS += $(ST_VENDOR_SDK_PATH)/Core/Src
LT_PROJECT_SOURCE_SUBDIRS += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src
LT_PROJECT_SOURCE_SUBDIRS += $(ST_LTCOREBSP_PATH)

# ST BSP
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/stm32f7xx_hal_msp.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/system_stm32f7xx.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/bsp_hal.c

LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_eth.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_exti.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rng.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pcd.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pcd_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_usb.c

# Startup
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/startup_stm32f767xx.s
LT_PROJECT_SOURCE_FILES += $(ST_LTCOREBSP_PATH)/STStartup.c

# LTCoreBSP
LT_PROJECT_SOURCE_FILES += $(ST_LTCOREBSP_PATH)/LTCoreBSP_STMicro.c

# Includes
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Core/Inc
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Drivers/STM32F7xx_HAL_Driver/Inc
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Drivers/CMSIS/Device/ST/STM32F7xx/Include
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Drivers/CMSIS/Include

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   19-Jan-21   tiberius    created
