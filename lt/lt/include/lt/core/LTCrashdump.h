/******************************************************************************
 * <lt/core/LTCrashdump.h>                                          LTCrashdump
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_CRASHDUMP_H
#define ROKU_LT_INCLUDE_LT_CORE_CRASHDUMP_H

#include <lt/LTTypes.h>

enum {
    kLTCrashdump_Partition_Magic = ('L' | ('T' << 8) | ('C' << 16) | ('D' << 24)),
    kLTLogBackup_Partition_Magic = ('B' | ('L' << 8) | ('O' << 16) | ('G' << 24)),
    kLTCrashdump_Version0        = 0,
};

typedef u8 LTCrashdumpContextType;
enum {
    /* Define two base context types here */
    kLTCrashdumpContextType_Thread         = 0,   /**< A thread context */
    kLTCrashdumpContextType_Interrupt      = 1,   /**< An interrupt context */
    kLTCrashDumpContextType_NumBaseTypes,
    /* The kernel may define other types as necessary, but NOT here. */
};

typedef struct {
    u32             magic;           /**< Crashdump partition magic number (kLTCrashDump_Partition_Magic) */
    u8              version;         /**< Crashdump version format (kLTCrashDump_Version0) */
    u8              numContexts;     /**< Number of contexts in crashdump */
    u16             threadInfoSize;  /**< Thread info size in bytes */
    u32             totalSize;       /**< Total size of the crashdump in bytes */
    u16             numData;         /**< Number of arch/context specific 32-bit words that follow */
    u16             rsvd;            /**< Reserved for future use (set to zero) */
    char            abi[16];         /**< Kernel ABI version, e.g.: "arm.m.main.1" */
    char            swVersion[32];   /**< Software version */
    u32             data[0];         /**< Architecture/context specific array of values */
} LTCrashdumpHeader;

typedef struct {
    LTCrashdumpContextType type;                 /**< Type of context (arch specific) */
    u8              numData;                     /**< Number of arch/context specific 32-bit words that follow */
    u16             stackdumpSize;               /**< Size of stack dump in bytes */
    u32             rsvd[2];                     /**< Reserved for future use (set to zero) */
    char            name[kLTThread_MaxNameBuff]; /**< Name of context (thread or interrupt) */
    LT_SIZE         threadInfoAddr;              /**< Location of LT Thread data in memory (if thread) */
    LT_SIZE         stackAddr;                   /**< Thread stack pointer location */
    u32             data[0];                     /**< Architecture/context specific array of values */
} LTCrashdumpContext;

typedef struct {
    u32             magic;
    u32             logSize;               /**< Does not include the size of LTLogBackupHeader. */
    LTTime          logBackupUTCTimestamp; /**< UTC time when the log backup was created */
    u8              reserved[16];
} LTLogBackupHeader;

LT_STATIC_ASSERT_SIZE_32_64(LTCrashdumpHeader, 64, 64)
LT_STATIC_ASSERT_SIZE_32_64(LTCrashdumpContext, 40, 48)
LT_STATIC_ASSERT_SIZE_32_64(LTLogBackupHeader, 32, 32)

/* Crashdump layout:
 *    LTCrashdumpHeader header
 *    Architecture/context specific data
 *    Context 0 data:
 *        LTCrashdumpContext context
 *        Architecture/context specific data
 *        Thread Info (for thread contexts only)
 *        Stack Dump
 *    Context 1 data:
 *        LTCrashdumpContext context
 *        Architecture/context specific data
 *        Thread Info (for thread contexts only)
 *        Stack Dump
 *    ...
 * LogBackup layout:
 *     LTLogBackupHeader
 *     Raw content of LTSystemLogger buffer
 */

#endif
