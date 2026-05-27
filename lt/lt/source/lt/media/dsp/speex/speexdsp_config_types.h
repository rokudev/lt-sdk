/******************************************************************************
 * speexdsp_config_types.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef __SPEEX_TYPES_H__
#define __SPEEX_TYPES_H__

#if 0
    #include <stdint.h>
    typedef int16_t spx_int16_t;
    typedef uint16_t spx_uint16_t;
    typedef int32_t spx_int32_t;
    typedef uint32_t spx_uint32_t;
#else
    #include <lt/LT.h>
    typedef s16 spx_int16_t;
    typedef u16 spx_uint16_t;
    typedef s32 spx_int32_t;
    typedef u32 spx_uint32_t;
#endif

#endif

