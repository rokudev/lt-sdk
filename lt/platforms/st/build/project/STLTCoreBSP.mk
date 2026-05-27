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

LT_PROJECT_SOURCE_SUBDIRS += $(ST_VENDOR_SDK_PATH)/CM7/Core/Src
LT_PROJECT_SOURCE_SUBDIRS += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src
LT_PROJECT_SOURCE_SUBDIRS += $(ST_LTCOREBSP_PATH)

# ST BSP
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/CM7/Src/stm32l4xx_hal_msp.c
#LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/system_stm32l4xx.c
#LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Core/Src/bsp_hal.c

# The BSP can't need all of these.  We can pare it down later.
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_crc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_crc_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cryp.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cryp_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ramfunc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_hash.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_hash_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_nand.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_nor.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_ospi.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_qspi.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rng.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rng_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_usart.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_usart_ex.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_wwdg.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_adc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_pwr.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_rcc.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_spi.c
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_utils.c

# Startup
LT_PROJECT_SOURCE_FILES += $(ST_VENDOR_SDK_PATH)/CM7/Core/Src/startup_stm32l4s5xx.s
LT_PROJECT_SOURCE_FILES += $(ST_LTCOREBSP_PATH)/STStartup.c

# LTCoreBSP
LT_PROJECT_SOURCE_FILES += $(ST_LTCOREBSP_PATH)/LTCoreBSP_STMicro.c

# Includes
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/CM7/Core/Inc
LT_PUBLIC_INCLUDE_FLAGS += -I$(ST_VENDOR_SDK_ROOT)/Drivers/CMSIS/Include

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   19-Jan-21   tiberius    created
