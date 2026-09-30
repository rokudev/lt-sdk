/*******************************************************************************
 *
 * temporary stub os adapter with __wrap functions only
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 ******************************************************************************/

#include <lt/core/LTCore.h>

DEFINE_LTLOG_SECTION("esp32.adpt");

void __assert_func(const char *file, int line,
                   const char *func, const char *expr)
{
    LTLOG_REDALERT("assert","failed in %s, %s:%d (%s)", func, file, line, expr);
    LT_ASSERT(0);
    while(1);
}

void * __wrap_malloc(u32 size)
{
    return lt_malloc(size);
}

void * __wrap_realloc(void * pData, LT_SIZE size)
{
    return lt_realloc(pData, size);
}

void __wrap_free(void * ptr)
{
    lt_free(ptr);
}


/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
