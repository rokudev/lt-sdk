/*
 * Copyright (c) 2021 Roku, Inc. All rights reserved.
 * This software and any compilation or derivative thereof is, and shall
 * remain, the proprietary information of Roku, Inc. and is highly confidential
 * in nature.
 */

#ifndef cc_85a3b345a5b98891
#define cc_85a3b345a5b98891

#include <lt/LT.h>

#define LWIP_NO_STDDEF_H 1
#define LWIP_NO_STDINT_H 1
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_CTYPE_H  1

#define SSIZE_MAX LTSSIZE_MAX
typedef LT_SIZE  size_t;
typedef LT_SSIZE ssize_t;

// TODO: This is not ideal as it will only work for GCC. lwIP's def.c
// non-optionally pulls in string.h which leads us to conflicting
// types for ptrdiff_t if we typedef it to LT_SSIZE.
//
// Solution is most likely to modify lwIP to not include def.c and
// instead provide all it's functionality through LT.
//
//typedef LT_SSIZE ptrdiff_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

typedef u8   u8_t;
typedef u16 u16_t;
typedef u32 u32_t;

typedef s8   s8_t;
typedef s16 s16_t;
typedef s32 s32_t;

#define  X8_F "x"
#define X16_F "x"
#define X32_F "x"

#define  U8_F "u"
#define U16_F "u"
#define U32_F "u"

#define  S8_F "d"
#define S16_F "d"
#define S32_F "d"

typedef LTHandle mem_ptr_t;

// Provide lwIP a platform diagnostic & assert (so it doesn't pull in
// stdio.h/printf)
static inline void lwip_log_diag(const bool bToServer, const char *tag, const char *pFormatString, ...) {
    u32 flag = kLTCore_LogFlags_LogTypeLog | kLTCore_LogFlags_LogToConsole;
    if (bToServer) {
        flag |= kLTCore_LogFlags_LogToServer;
    }
    lt_va_list args;
    lt_va_start(args, pFormatString);
    LT_GetCore()->LogV("lwip.diag", tag, flag, pFormatString, args);
    lt_va_end(args);
}

#define LWIP_PLATFORM_DIAG(ToServer, TAG, fmt, ...) \
    lwip_log_diag(ToServer, TAG, fmt, ##__VA_ARGS__)

#ifdef LT_DEBUG
#define LWIP_PLATFORM_ASSERT(x) \
    do { \
        LT_GetCore()->ConsolePrint( \
            "Assertion \"%s\" failed at line %d in %s\n", \
            x, __LINE__, __FILE__); \
        LT_ASSERT(0); \
    } while(0)
#else // LT_DEBUG
#define LWIP_PLATFORM_ASSERT(x) \
    do { \
        LT_ASSERT(0); \
    } while(0)
#endif // LT_DEBUG

#endif // cc_85a3b345a5b98891
