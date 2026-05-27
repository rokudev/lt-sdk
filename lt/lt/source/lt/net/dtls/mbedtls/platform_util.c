/*
 * Common and shared functions used by multiple modules in the Mbed TLS
 * library.
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <lt/core/LTTime.h>
#include <lt/core/LTCore.h>
#include "common.h"
#include "platform_util.h"
#include "error.h"

#if defined(MBEDTLS_THREADING_C)
#include "threading.h"
#endif

/* Eliminate a macro that interferes with lt stdlib functions on some platforms: */
#undef memset

/*
 * Where possible, we try to detect the presence of a platform-provided
 * secure memset, such as explicit_bzero(), that is safe against being optimized
 * out, and use that.
 *
 * For other platforms, we provide an implementation that aims not to be
 * optimized out by the compiler.
 *
 * This implementation for mbedtls_platform_zeroize() was inspired from Colin
 * Percival's blog article at:
 *
 * http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
 *
 * It uses a volatile function pointer to the standard memset(). Because the
 * pointer is volatile the compiler expects it to change at
 * any time and will not optimize out the call that could potentially perform
 * other operations on the input buffer instead of just setting it to 0.
 * Nevertheless, as pointed out by davidtgoldblatt on Hacker News
 * (refer to http://www.daemonology.net/blog/2014-09-05-erratum.html for
 * details), optimizations of the following form are still possible:
 *
 * if (memset_func != memset)
 *     memset_func(buf, 0, len);
 *
 * Note that it is extremely difficult to guarantee that
 * the memset() call will not be optimized out by aggressive compilers
 * in a portable way. For this reason, Mbed TLS also provides the configuration
 * option MBEDTLS_PLATFORM_ZEROIZE_ALT, which allows users to configure
 * mbedtls_platform_zeroize() to use a suitable implementation for their
 * platform and needs.
 */

void mbedtls_platform_zeroize(void *buf, size_t len)
{
    MBEDTLS_INTERNAL_VALIDATE(len == 0 || buf != NULL);

    if (len > 0) {
        lt_memset(buf, 0, len);
    }
}

#if defined(MBEDTLS_TEST_HOOKS)
void (*mbedtls_test_hook_test_fail)(const char *, int, const char *);
#endif /* MBEDTLS_TEST_HOOKS */

/*
 * Provide external definitions of some inline functions so that the compiler
 * has the option to not inline them
 */
extern inline void mbedtls_xor(unsigned char *r,
                               const unsigned char *a,
                               const unsigned char *b,
                               size_t n);

extern inline uint16_t mbedtls_get_unaligned_uint16(const void *p);

extern inline void mbedtls_put_unaligned_uint16(void *p, uint16_t x);

extern inline uint32_t mbedtls_get_unaligned_uint32(const void *p);

extern inline void mbedtls_put_unaligned_uint32(void *p, uint32_t x);

extern inline uint64_t mbedtls_get_unaligned_uint64(const void *p);

extern inline void mbedtls_put_unaligned_uint64(void *p, uint64_t x);

/* DEBUG_BUF_SIZE must be at least 2 */
#define DEBUG_BUF_SIZE      512

void *mbedtls_calloc(size_t nmemb, size_t size)
{
    void *ptr = lt_malloc(size*nmemb);
    if (ptr)
        lt_memset(ptr, 0, size*nmemb);
    return ptr;
}

void mbedtls_free(void *ptr)
{
    lt_free(ptr);
}

int mbedtls_printf(const char *format, ...)
{
    lt_va_list argp;
    char str[DEBUG_BUF_SIZE];
    lt_va_start(argp, format);
    int ret = mbedtls_vsnprintf(str, DEBUG_BUF_SIZE, format, argp);
    lt_va_end(argp);
    if (ret < 0) {
        ret = 0;
    } else {
        if (ret >= DEBUG_BUF_SIZE - 1) {
            ret = DEBUG_BUF_SIZE - 2;
        }
    }
    str[ret]     = '\n';
    str[ret + 1] = '\0';
    LT_GetCore()->ConsolePutString(str);
    return lt_strlen(str);
}

mbedtls_time_t mbedtls_time(mbedtls_time_t *timer) {
    LT_UNUSED(timer);
    LTTime t = LT_GetCore()->GetClockTimeUTC();
    return t.nNanoseconds;
}
