/******************************************************************************
 * LTSystemLogger_Format.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemLogger_Private.h"

/******************************************************************************
 * @name Printf Internal Logic
 * @{
 */

typedef enum {
    kPrivate_Format_Type_error = 0,
    kPrivate_Format_Type_s32,
    kPrivate_Format_Type_u32,
    kPrivate_Format_Type_s64,
    kPrivate_Format_Type_u64,
    kPrivate_Format_Type_string,
    kPrivate_Format_Type_double,
    kPrivate_Format_Type_long_double,
    kPrivate_Format_Type_percent,
    kPrivate_Format_TypeMask = 15,

    kPrivate_Format_Flag_width_arg     = 16,
    kPrivate_Format_Flag_precision_arg = 32,
} Private_Format_Type;

LT_STATIC_ASSERT(sizeof(int) == 4, "Unexpected integer size");
LT_STATIC_ASSERT(sizeof(long) == LT_ARCHITECTURE_BITS / 8, "Unexpected long size");
LT_STATIC_ASSERT(sizeof(long long) == 8, "Unexpected long long size");
LT_STATIC_ASSERT(sizeof(void *) == LT_ARCHITECTURE_BITS / 8, "Unexpected pointer size");

static u32 Private_Format_SkipOptions(const char **pfmt) {
    const char *fmt = *pfmt;
    char next       = *fmt++;
    u32 result      = kPrivate_Format_Type_error;
    // Skip flags
    switch (next) {
        case '-':
        case '+':
        case ' ':
        case '#':
        case '0':
            next = *fmt++;
            break;
    }
    // Dynamic width not supported
    if (next == '*') {
        result |= kPrivate_Format_Flag_width_arg;
        next    = *fmt++;
    }
    // Skip width specifier
    while (next >= '0' && next <= '9') {
        next = *fmt++;
    }
    // Skip precision specifier
    if (next == '.') {
        next = *fmt++;
        // Dynamic precision not supported
        if (next == '*') {
            result |= kPrivate_Format_Flag_precision_arg;
            next    = *fmt++;
        }
        while (next >= '0' && next <= '9') {
            next = *fmt++;
        }
    }

    if (next == 'h') {
        next = *fmt++;
        if (next == 'h') {
            next = *fmt++;
        }
    }

    int type_offset = 0;
    switch (next) {
        case 'h':
            if (*fmt == 'h') {
                ++fmt;
            }
#if LT_ARCHITECTURE_BITS == 32
            // fall through
        case 'z':
        case 't':
#endif
            next = *fmt++;
            break;
        case 'l':
            if (*fmt == 'l') {
                ++fmt;
            } else {
#if LT_ARCHITECTURE_BITS == 32
                next = *fmt++;
                break;
#endif
            }
            // fall through
        case 'j':
#if LT_ARCHITECTURE_BITS == 64
        case 'z':
        case 't':
#endif
            next        = *fmt++;
            type_offset = 2;
            break;
        case 'L':
            next        = *fmt++;
            type_offset = 1;
            break;
    }

    *pfmt = fmt;
    switch (next) {
        case 'd':
        case 'i':
        case 'c':
            result |= kPrivate_Format_Type_s32 + type_offset;
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            result |= kPrivate_Format_Type_u32 + type_offset;
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            result |= kPrivate_Format_Type_double + type_offset;
            break;
        case 's':
            result |= kPrivate_Format_Type_string;
            break;
        case '%':
            result |= kPrivate_Format_Type_percent;
            break;
        case 'p':
#if LT_ARCHITECTURE_BITS == 64
            result |= kPrivate_Format_Type_u64;
            break;
#elif LT_ARCHITECTURE_BITS == 32
            result |= kPrivate_Format_Type_u32;
            break;
#else
    #error "What is this?"
            break;
#endif
    }

    return result;
}

static u32 Private_Format_CountOptions(const char *fmt) {
    u32 count = 0;
    for (char next = *fmt++; next != '\0'; next = *fmt++) {
        if (next != '%') {
            continue;
        }

        Private_Format_Type type = Private_Format_SkipOptions(&fmt);
        if (!(type & kPrivate_Format_Type_percent)) {
            ++count;
        }
    }
    return count;
}

static bool Private_Format_Write(LTMessagePack_Obj *w, const char *fmt, lt_va_list args) {
    bool success        = true;
    if (!S.i.mp->PutArray(w, Private_Format_CountOptions(fmt))) {
        return false;
    }
    for (char next = *fmt++; next != '\0'; next = *fmt++) {
        if (next != '%') {
            continue;
        }
        s32 type = Private_Format_SkipOptions(&fmt);
        if (type & kPrivate_Format_Flag_width_arg) {
            (void)lt_va_arg(args, int);
        }
        s32 precision = 0;
        if (type & kPrivate_Format_Flag_precision_arg) {
            precision = lt_va_arg(args, int);
        }
        switch (type & kPrivate_Format_TypeMask) {
            case kPrivate_Format_Type_s32:
                success &= LTMessagePackPut(S.i.mp, w, lt_va_arg(args, s32));
                break;
            case kPrivate_Format_Type_u32:
                success &= LTMessagePackPut(S.i.mp, w, lt_va_arg(args, u32));
                break;
            case kPrivate_Format_Type_s64:
                success &= LTMessagePackPut(S.i.mp, w, lt_va_arg(args, s64));
                break;
            case kPrivate_Format_Type_u64:
                success &= LTMessagePackPut(S.i.mp, w, lt_va_arg(args, u64));
                break;
            case kPrivate_Format_Type_string:
                if ((type & kPrivate_Format_Flag_precision_arg) == 0) {
                    success &= Private_Write_String(w, lt_va_arg(args, const char *));
                } else {
                    s32 pos = S.i.mp->PutString(w, NULL, precision + 1);
                    if ((pos != 0) && (w->end - w->head >= pos + precision)) {
                        lt_memcpy(w->head + pos, lt_va_arg(args, const char *), precision);
                        w->head[pos + precision] = '\0';
                    } else {
                        success = false;
                    }
                }
                break;
            case kPrivate_Format_Type_double:
                success &= LTMessagePackPut(S.i.mp, w, lt_va_arg(args, double));
                break;
            case kPrivate_Format_Type_long_double:
                {
                    long double value  = lt_va_arg(args, long double);
                    success           &= S.i.mp->PutBinary(w, (u8 *)&value, sizeof(value)) > 0;
                }
                break;
            case kPrivate_Format_Type_percent:
                success = true;
                break;
            case kPrivate_Format_Type_error:
                success = false;
                break;
        }
    }
    return success;
}

/**
 * @todo Rather than calling snprintf, use the value formatters that the LT
 * implementation of that function includes.
 */
static LT_SIZE Private_Format_Read(LTLogConsumer_Obj *consumer, const LTLogMessage *msg) {
    LTLogConsumerWorkingData *working = consumer->working;
    char *buffer                      = working->buffer;
    u32 length;
    LTMessagePack_Obj reader = msg->printPayload.args;
    if (S.i.mp->GetArray(&reader, &length) != LTMessagePack_Type_Array) {
        return 0;
    }
    for (const char *fmt = msg->printPayload.format;
         (fmt < msg->printPayload.formatEnd) && (buffer < working->bufferEnd);) {
        char next = *fmt++;
        if (next == '\0') {
            break;
        }
        if (next != '%') {
            *buffer++ = next;
            continue;
        }
        working->specStart = fmt - 1;
        u32 type           = Private_Format_SkipOptions(&fmt);
        if (type & kPrivate_Format_Type_percent) {
            *buffer++ = '%';
            // Skip taking a value from the argument list
            continue;
        }

        // Make a null-terminated copy of the format specifier
        LT_SIZE spec_len   = fmt - working->specStart;
        if (spec_len + 1 >= sizeof(working->spec)) {
            LT_ASSERT(false);
            return LTLog_Error_BadFormat;
        }
        lt_memcpy(working->spec, working->specStart, spec_len);
        working->spec[spec_len] = '\0';

        LTMessagePack_Value value;
        S.i.mp->GetValue(&reader, &value);
        switch (type & kPrivate_Format_TypeMask) {
            case kPrivate_Format_Type_s32:
                if (value.type != LTMessagePack_Type_Integer) {
                    return LTLog_Error_BadEncoding;
                }
                buffer += lt_snprintf(buffer,
                                      working->bufferEnd - buffer,
                                      working->spec,
                                      (s32)value.integer);
                break;
            case kPrivate_Format_Type_u32:
                if (value.type != LTMessagePack_Type_Integer) {
                    return LTLog_Error_BadEncoding;
                }
                buffer += lt_snprintf(buffer,
                                      working->bufferEnd - buffer,
                                      working->spec,
                                      (u32)value.uinteger);
                break;
            case kPrivate_Format_Type_s64:
                if (value.type != LTMessagePack_Type_Integer) {
                    return LTLog_Error_BadEncoding;
                }
                buffer += lt_snprintf(buffer,
                                      working->bufferEnd - buffer,
                                      working->spec,
                                      value.integer);
                break;
            case kPrivate_Format_Type_u64:
                if (value.type != LTMessagePack_Type_Integer) {
                    return LTLog_Error_BadEncoding;
                }
                buffer += lt_snprintf(buffer,
                                      working->bufferEnd - buffer,
                                      working->spec,
                                      value.uinteger);
                break;
            case kPrivate_Format_Type_string:
                {
                    const char *str;
                    if (!Private_Parse_String(value, &str)) {
                        return LTLog_Error_BadEncoding;
                    }
                    if ((type & kPrivate_Format_Flag_precision_arg) == 0) {
                        buffer += lt_snprintf(buffer,
                                              working->bufferEnd - buffer,
                                              working->spec,
                                              str);
                    } else {
                        buffer += lt_snprintf(buffer,
                                              working->bufferEnd - buffer,
                                              working->spec,
                                              lt_strlen(str),
                                              str);
                    }
                }
                break;
            case kPrivate_Format_Type_double:
                if (value.type != LTMessagePack_Type_Float64) {
                    return LTLog_Error_BadEncoding;
                }
                buffer += lt_snprintf(buffer,
                                      working->bufferEnd - buffer,
                                      working->spec,
                                      (double)value.float64);
                break;
            case kPrivate_Format_Type_long_double:
                if (value.type != LTMessagePack_Type_Binary || value.size != sizeof(long double)) {
                    return LTLog_Error_BadEncoding;
                } else {
                    long double real;
                    lt_memcpy(&real, value.data, sizeof(long double));
                    buffer += lt_snprintf(buffer, working->bufferEnd - buffer, working->spec, real);
                }
                break;
            case kPrivate_Format_Type_error:
                // This really shouldn't be possible at this point since the format should already
                // have been checked on the write side.
                return LTLog_Error_BadFormat;
        }
    }
    if (buffer >= working->bufferEnd) {
        buffer = working->bufferEnd - 1;
    }
    *buffer            = '\0';
    working->bufferEnd = buffer;

    return LTLog_Error_None;
}

/** @} */
