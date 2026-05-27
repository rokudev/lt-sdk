/*******************************************************************************
 * source/lt/utility/webrtcagc/platform_defines.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_UTILITY_WEBRTCAGC_PLATFORMDEFINES_H
#define ROKU_LT_SOURCE_LT_UTILITY_WEBRTCAGC_PLATFORMDEFINES_H

#include <lt/core/LTCore.h>
#include <lt/LTTypes.h>

#define size_t      LT_SIZE
#define int16_t     s16
#define int32_t     s32
#define uint8_t     u8
#define int8_t      s8
#define uint16_t    u16
#define uint32_t    u32
#define int64_t     s64
#define uint64_t    u64
#define INT32_MAX   LT_S32_MAX
#define INT32_MIN   LT_S32_MIN
#define malloc      lt_malloc
#define memcpy      lt_memcpy
#define memset      lt_memset
#define free        lt_free
#define uintptr_t   u32
#define RTC_DCHECK_LT(x, y) LT_ASSERT((x) < (y))
#define RTC_DCHECK_EQ(x, y) LT_ASSERT((x) == (y))
#define RTC_DCHECK_LE(x, y) LT_ASSERT((x) <= (y))
#define RTC_DCHECK_GT(x, y) LT_ASSERT((x) > (y))
#define RTC_DCHECK_GE(x, y) LT_ASSERT((x) >= (y))

#endif /* #ifndef ROKU_LT_SOURCE_LT_UTILITY_WEBRTCAGC_PLATFORMDEFINES_H */