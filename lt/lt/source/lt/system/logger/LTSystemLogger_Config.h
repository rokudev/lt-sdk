/******************************************************************************
 * LTSystemLogger_Config.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LTSYSTEMLOGGER_CONFIG_H
#define LTSYSTEMLOGGER_CONFIG_H

#include <lt/LTTypes.h>

#ifndef LTLOG_CONFIG_ALIGNMENT
    #define LTLOG_CONFIG_ALIGNMENT _Alignof(u64)
#endif

LT_STATIC_ASSERT((LTLOG_CONFIG_ALIGNMENT & (LTLOG_CONFIG_ALIGNMENT - 1)) == 0,
                 "Alignment must be power of 2");

#ifndef LTLOG_CONFIG_MIN_ALLOCATION_SIZE
    #define LTLOG_CONFIG_MIN_ALLOCATION_SIZE 32
#endif

LT_STATIC_ASSERT((LTLOG_CONFIG_MIN_ALLOCATION_SIZE & (LTLOG_CONFIG_MIN_ALLOCATION_SIZE - 1)) == 0,
                 "Flex block size must be power of 2");

#ifndef LTLOG_CONFIG_MSG_COUNT
    #define LTLOG_CONFIG_MSG_COUNT 256
#endif

LT_STATIC_ASSERT((LTLOG_CONFIG_MSG_COUNT & (LTLOG_CONFIG_MSG_COUNT - 1)) == 0,
                 "Message count must be power of 2");

#define LTLOG_CONFIG_MIN_BUFFER_SIZE \
    ((LTLOG_CONFIG_MSG_COUNT / 2) * (LTLOG_CONFIG_MIN_ALLOCATION_SIZE))

#ifndef LTLOG_CONFIG_BUFFER_SIZE
    #define LTLOG_CONFIG_BUFFER_SIZE LTLOG_CONFIG_MIN_BUFFER_SIZE
#endif

LT_STATIC_ASSERT(LTLOG_CONFIG_BUFFER_SIZE >= LTLOG_CONFIG_MIN_BUFFER_SIZE,
                 "Buffer not large enough for all messages");

LT_STATIC_ASSERT((LTLOG_CONFIG_BUFFER_SIZE & (LTLOG_CONFIG_BUFFER_SIZE - 1)) == 0,
                 "Buffer size must be power of 2");

#ifndef LTLOG_CONFIG_BUFFER_AVAIL_WATERMARK
    #define LTLOG_CONFIG_BUFFER_AVAIL_WATERMARK (LTLOG_CONFIG_BUFFER_SIZE / 4)
#endif

#ifndef LTLOG_CONFIG_THREAD_PRIORITY
    #define LTLOG_CONFIG_THREAD_PRIORITY kLTThread_PriorityLowest
#endif

#ifndef LTLOG_CONFIG_ALLOW_PRIORITY_BOOST
    #define LTLOG_CONFIG_ALLOW_PRIORITY_BOOST 1

    #ifndef LTLOG_CONFIG_THREAD_PRIORITY_MAX
        #define LTLOG_CONFIG_THREAD_PRIORITY_MAX (kLTThread_PriorityDefault + 1)
    #endif
#endif

#ifndef LTLOG_CONFIG_ALLOW_PRIORITY_YIELD
    // This performs exceptionally poorly on a standard Linux system
    #define LTLOG_CONFIG_ALLOW_PRIORITY_YIELD 1
#endif

#ifndef LTLOG_CONFIG_CONSOLE_BUFFER_SIZE
    #define LTLOG_CONFIG_CONSOLE_BUFFER_SIZE 128
#endif

#ifndef LTLOG_CONFIG_DEBUG_ALLOCATION
    #define LTLOG_CONFIG_DEBUG_ALLOCATION 0
#endif

#ifndef LTLOG_CONFIG_DEBUG_IMMEDIATE_PROCESSING
    #define LTLOG_CONFIG_DEBUG_IMMEDIATE_PROCESSING 0
#endif

#ifndef LTLOG_CONFIG_RECORD_RETRY_PERIOD_MS
    #define LTLOG_CONFIG_RECORD_RETRY_PERIOD_MS 100
#endif

#ifndef LTLOG_CONFIG_RECORD_RETRY_PERIOD_LONG_MS
    #define LTLOG_CONFIG_RECORD_RETRY_PERIOD_LONG_MS 1000
#endif

#define LTLOG_CONFIG_RECORD_RETRY_PERIOD LTTime_Milliseconds(LTLOG_CONFIG_RECORD_RETRY_PERIOD_MS)
#define LTLOG_CONFIG_RECORD_RETRY_PERIOD_LONG \
    LTTime_Milliseconds(LTLOG_CONFIG_RECORD_RETRY_PERIOD_LONG_MS)

#ifndef LTLOG_CONFIG_MAGIC_ASSERTIONS
    #define LTLOG_CONFIG_MAGIC_ASSERTIONS 0
#endif

#ifndef LTLOG_CONFIG_DEBUG_EXECUTABLE
    #define LTLOG_CONFIG_DEBUG_EXECUTABLE 0
#endif

#ifndef LTLOG_CONFIG_CONSUMER_STACK_SIZE
    #define LTLOG_CONFIG_CONSUMER_STACK_SIZE 1536
#endif

#ifndef LTLOG_CONFIG_FORMAT_BUFFER_SIZE
    #define LTLOG_CONFIG_FORMAT_BUFFER_SIZE 256
#endif

#endif
