/******************************************************************************
 * md5.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef MD5_H
#define MD5_H

#include <lt/LT.h>

typedef struct{
    u64 size;        // Size of input in bytes
    u32 buffer[4];   // Current accumulation of hash
    u8 input[64];    // Input to be used in the next step
    u8 digest[16];   // Result of algorithm
} MD5Context;

void md5Init(MD5Context *ctx);
void md5Update(MD5Context *ctx, const u8 *input, u32 input_len);
void md5Finalize(MD5Context *ctx);
void md5Step(u32 *buffer, u32 *input);

void md5String(char *input, u8 *result);

#endif