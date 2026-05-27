
//  Copyright 2019-2020 Matthew C Needes
//  You may not use this source file except in compliance with the
//  terms and conditions contained within the LICENSE file (the
//  "License") included under this distribution.

//
// MOS BSP HAL definitions
//   Definitions for processor and board HAL go here.
//

#ifndef _MOS_BSP_HAL_H_
#define _MOS_BSP_HAL_H_

//
// MOS HAL interface
//
#include <hal.h>

//
// Processor HAL including CMSIS definitions
//
#include <stm32f767xx.h>

//
// Processor-specific definitions
//
// Cycles per inner loop iteration (1 or 3, typically 3)
#define MOS_CYCLES_PER_INNER_LOOP   1

//
// SoC-specific definitions
//
#include <stm32f7xx_hal.h>

//
// Board-specific definitions
//

#endif
