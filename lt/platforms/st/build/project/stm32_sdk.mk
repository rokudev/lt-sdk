################################################################################
# stm32_sdk.mk - makefile for STMicro stm32 SDK for LT
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# Build a static library
LT_PROJECT_BUILD_SHARED_LIB := no
LT_PROJECT_BUILD_STATIC_LIB := yes
LT_PROJECT_BUILD_EXECUTABLE := no

# source dir and files
LT_PROJECT_SOURCE_DIR     := $(ST_VENDOR_SDK_ROOT)

LT_PROJECT_SOURCE_SUBDIRS += Common/Src
LT_PROJECT_SOURCE_SUBDIRS += CM7/Core/Src
LT_PROJECT_SOURCE_SUBDIRS += CM7/Core/Startup
LT_PROJECT_SOURCE_SUBDIRS += Drivers/BSP/STM32H7xx_Nucleo
LT_PROJECT_SOURCE_SUBDIRS += Drivers/STM32H7xx_HAL_Driver/Src

# make
LT_PROJECT_SOURCE_FILES += Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c
LT_PROJECT_SOURCE_FILES += CM7/Core/Src/bsp_hal.c
LT_PROJECT_SOURCE_FILES += CM7/Core/Src/syscalls.c
LT_PROJECT_SOURCE_FILES += CM7/Core/Startup/startup_stm32h755zitx.s
LT_PROJECT_SOURCE_FILES += Drivers/BSP/STM32H7xx_Nucleo/stm32h7xx_nucleo.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_iwdg.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart.c
LT_PROJECT_SOURCE_FILES += Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart_ex.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   10-Jul-2026	augustus    created
