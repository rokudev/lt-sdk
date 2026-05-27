/******************************************************************************
 * UnitTestLTSystemLogger.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef UNITTEST_LTSYSTEMLOGGER_H
#define UNITTEST_LTSYSTEMLOGGER_H

#include <lt/core/LTCore.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/system/logger/LTSystemLogger.h>
#include <lt/system/logger/LTSystemLogger_Records.h>
#include <tilt/JiltEngine.h>

#define TEST_ENABLE_BENCHMARK 0
#define TEST_BUFFER_SIZE 512
#define COUNTOF(x) (sizeof(x) / sizeof((x)[0]))

static struct {
    Tilt           *tilt;
    LTSystemLogger *log;
    ILTThread      *thread;
} s_if;

static struct {
    char    log_buffer[TEST_BUFFER_SIZE];
    LT_SIZE log_size;
    char    print_buffer[TEST_BUFFER_SIZE];
    LT_SIZE print_size;
} s_context;

LTLOG_DEFINE_RECORD_EVENTS(s_record_events);

static void Test_Specifiers(Tilt *tilt);
static void Test_Rodata(Tilt *tilt);
static void Test_Benchmark(Tilt *tilt);
static void Test_Records(Tilt *tilt);
static void Test_Corners(Tilt *tilt);
static void Test_Fuzz(Tilt *tilt);

#endif
