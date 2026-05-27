/******************************************************************************
 * UnitTestLTSystemLogger_Print.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "UnitTestLTSystemLogger.h"

static void Test_Specifiers_Check(Tilt *tilt, int lineno, const char *format, ...) {
    lt_va_list args;
    lt_va_list print_args;
    lt_va_start(args, format);
    lt_va_copy(print_args, args);
    lt_memset(&s_context, 0xFF, sizeof(s_context));
    s_if.log->VPrint(NULL, NULL, kLTCore_LogFlags_LogTypeLog, format, args);
    s_if.log->Flush();

    s_context.print_size = lt_vsnprintf(s_context.print_buffer, sizeof(s_context.print_buffer), format, print_args);
    lt_va_end(args);
    TILT_ASSERT_TRUE(tilt,
                     lt_strcmp(s_context.log_buffer, s_context.print_buffer) == 0,
                     "[%s:%d] buffers don't match\n printf:%s\n log:%s",
                     format,
                     lineno,
                     s_context.print_buffer,
                     s_context.log_buffer);
    TILT_ASSERT_TRUE(tilt,
                     s_context.log_size == s_context.print_size,
                     "[%s:%d] sizes don't match printf:%d log:%d",
                     format,
                     lineno,
                     s_context.print_size,
                     s_context.log_size);
}

static void Test_Specifiers_Callback(const ILTLogConsumer *api, LTLogConsumer consumer, const LTLogMessage *message) {
    s_context.log_size = sizeof(s_context.log_buffer);
    LTLog_Error err    = api->Print(consumer, message, false, false, s_context.log_buffer, &s_context.log_size);

    TILT_ASSERT_TRUE(s_if.tilt, err == LTLog_Error_None, "Error %d printing");
    TILT_ASSERT_TRUE(s_if.tilt, message->printPayload.formatOffset == LT_LOG_INVALID_FORMAT_OFFSET, "Should not be getting a valid offset");
}

static void Test_Specifiers(Tilt *tilt) {
    const char *test_str                = "abcdefg";
    const char *test_readonly_charbuf   = "abcdefg0123456789";
    LTLogConsumer consumer              = s_if.log->AddConsumer(Test_Specifiers_Callback, NULL, NULL, NULL);
    s_if.tilt                           = tilt;

    // DRW: since AddConsumer queues the callback to add, our thread may proceed with the first check
    //      before our consumer is added.  Therefore, insert glorious hack:
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(200));
    Test_Specifiers_Check(tilt, __LINE__, "%c", 'k');

    Test_Specifiers_Check(tilt, __LINE__, "%o", 42);
    Test_Specifiers_Check(tilt, __LINE__, "%u", 42);
    Test_Specifiers_Check(tilt, __LINE__, "%x", 42);
    Test_Specifiers_Check(tilt, __LINE__, "%X", 42);
    Test_Specifiers_Check(tilt, __LINE__, "%d", 42);

    Test_Specifiers_Check(tilt, __LINE__, "%lo", (unsigned long)42);
    Test_Specifiers_Check(tilt, __LINE__, "%lu", (unsigned long)42);
    Test_Specifiers_Check(tilt, __LINE__, "%lx", (unsigned long)42);
    Test_Specifiers_Check(tilt, __LINE__, "%lX", (unsigned long)42);
    Test_Specifiers_Check(tilt, __LINE__, "%ld", (unsigned long)42);

    Test_Specifiers_Check(tilt, __LINE__, "%llo", (long long int)42);
    Test_Specifiers_Check(tilt, __LINE__, "%llu", (long long int)42);
    Test_Specifiers_Check(tilt, __LINE__, "%llx", (long long int)42);
    Test_Specifiers_Check(tilt, __LINE__, "%llX", (long long int)42);
    Test_Specifiers_Check(tilt, __LINE__, "%lld", (long long int)42);

    Test_Specifiers_Check(tilt, __LINE__, "%s", test_str);
    Test_Specifiers_Check(tilt, __LINE__, "%4s", test_str);
    Test_Specifiers_Check(tilt, __LINE__, "%-4s", test_str);
    Test_Specifiers_Check(tilt, __LINE__, "%10s", test_str);
    Test_Specifiers_Check(tilt, __LINE__, "%-10s", test_str);
    Test_Specifiers_Check(tilt,
                          __LINE__,
                          "%s",
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf "
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf "
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf "
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf "
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf "
                          "asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf asdf");

    Test_Specifiers_Check(tilt, __LINE__, "%s", &test_readonly_charbuf[10]);
    Test_Specifiers_Check(tilt, __LINE__, "%4s", &test_readonly_charbuf[10]);
    Test_Specifiers_Check(tilt, __LINE__, "%-4s", &test_readonly_charbuf[10]);
    Test_Specifiers_Check(tilt, __LINE__, "%10s", &test_readonly_charbuf[10]);
    Test_Specifiers_Check(tilt, __LINE__, "%-10s", &test_readonly_charbuf[10]);

    Test_Specifiers_Check(tilt, __LINE__, "%p", test_str);

    Test_Specifiers_Check(tilt, __LINE__, "%a", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%g", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%e", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%f", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%A", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%G", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%E", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%F", 42.0);

    Test_Specifiers_Check(tilt, __LINE__, "%La", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%Lg", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%Le", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%Lf", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%LA", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%LG", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%LE", (long double)42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%LF", (long double)42.0);

    Test_Specifiers_Check(tilt, __LINE__, "%2.6a", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6g", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6e", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6f", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6A", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6G", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6E", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%2.6F", 42.0);

    Test_Specifiers_Check(tilt, __LINE__, "%.2a", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2g", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2e", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2f", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2A", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2G", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2E", 42.0);
    Test_Specifiers_Check(tilt, __LINE__, "%.2F", 42.0);

    Test_Specifiers_Check(tilt, __LINE__, "%s", "Hello, world!");

    s_if.log->RemoveConsumer(consumer);
}
