/*******************************************************************************
 * LTBootPlatform.h                           LT Bootloader for Example Platform
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef __LTBOOT_PLATFORM_H_
#define __LTBOOT_PLATFORM_H_

/*
 * Platform-specific definitions
 *   Used to configure LTBootloader to either use LTBootloader built-ins
 *   or SoC functionality such as ROM functions or hardware acceleration.
 */

/************************** STDLIB Implementation *****************************/

/* Use built-in implementations */
#define LTBootPlatform_memcpy                 (LTBootStdlib_memcpy)
#define LTBootPlatform_memset                 (LTBootStdlib_memset)
#define LTBootPlatform_strncpyTerm            (LTBootStdlib_strncpyTerm)
#define LTBootPlatform_strncmp                (LTBootStdlib_strncmp)
#define LTBootPlatform_snprintf(fmt, ...)     (LTBootStdlib_snprintf(fmt __VA_OPT__(,) __VA_ARGS__))
#define LTBootPlatform_printf(fmt, ...)       (LTBootStdlib_printf(fmt __VA_OPT__(,) __VA_ARGS__))

/* Internal buffer size for built-in printf implementation */
#define LTBootPlatform_PrintBufferSize        128

/****************************** Debug Print ***********************************/

#ifndef LT_DEBUG
  #define LTBootPlatform_DebugPrintf(...)
#else
  #define LTBootPlatform_DebugPrintf          (LTBootPlatform_printf)
#endif

/************************** Crypto Implementation *****************************/

/* Use built-in implementations */
#define LTBootPlatform_SHA256_Init            (LTBootCrypto_SHA256_Init)
#define LTBootPlatform_SHA256_Update          (LTBootCrypto_SHA256_Update)
#define LTBootPlatform_SHA256_Finish          (LTBootCrypto_SHA256_Finish)
#define LTBootPlatform_SHA512_Init            (LTBootCrypto_SHA512_Init)
#define LTBootPlatform_SHA512_Update          (LTBootCrypto_SHA512_Update)
#define LTBootPlatform_SHA512_Finish          (LTBootCrypto_SHA512_Finish)
#define LTBootPlatform_Ed25519_Verify         (LTBootCrypto_Ed25519_Verify)

/****************** Security Abort/Assert Implementation **********************/

void Reboot(void);

/* Security abort - Abort gracefullly, allowing serial console to drain.
 *   Use this for general bootloader failures including security failures.
 *   Follow this with a security assertion to thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAbort() { \
    while (CONSOLE_BUSY) {} \
    Reboot(); \
    Reboot(); \
    Reboot(); \
}

/* Security abort - Abort gracefullly, allowing serial console to drain and the watchdog
 *   timer to expire. Watchdog resets can failover to retry the other firmware partition.
 *   Follow this with a security assertion to thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAbortWithFailover() { \
    while (1) {} \
    Reboot(); \
    Reboot(); \
    Reboot(); \
}

/* Security assertion check - Immediate abort if condition is not true.
 *    Perform normal check first then use this assertion as a backup
 *    to help thwart fault-injection attacks. */
#define LTBootPlatform_SecurityAssert(CND) { \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) Reboot();                    \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) Reboot();                    \
    asm volatile ("" ::: "memory");          \
    if (!(CND)) Reboot();                    \
}

#endif

