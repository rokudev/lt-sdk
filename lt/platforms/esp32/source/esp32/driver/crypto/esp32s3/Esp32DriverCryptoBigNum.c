/*******************************************************************************
 * platforms/esp32/source/esp32/driver/crypto/esp32s3/Esp32DriverCryptoBigNum.c
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *******************************************************************************/

/*******************************************************
 * The RSA peripheral half of the big number library.  The chip independent
 * software arithmetic is shared, in ../Esp32DriverCryptoBigNumSoft.c.
 *
 * The esp32s3 engine differs from the esp32's in two ways that shape this file.
 * Operand size is one register, LENGTH, holding words - 1, rather than the
 * esp32's separate MODEXP_MODE and MULT_MODE holding words/16 - 1.  And there is
 * a native modular multiply, MOD_MULT_START, so the modular paths that the esp32
 * has to build out of two raw Montgomery multiplications are single operations
 * here.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>
#include "Esp32DriverCrypto.h"
#include "Esp32DriverCryptoBigNum.h"

/**
 * @brief  Clear RSA registers
 * @param RSAbase  the RSA base
 * @param len      the length of data, in u32
 */
static void ClearRSA(volatile u32 *RSAbase, LT_SIZE len) {
    for (LT_SIZE i = 0; i < len; ++i) {
        RSAbase[i] = 0;
    }
}

/**
 * @brief  Copy data to RSA registers
 * @param RSAbase  the RSA base
 * @param data     the data
 * @param len      the length of data, in u32
 */
static void CopyToRSA(volatile u32 *RSAbase, const u32 *data, LT_SIZE len) {
    for (LT_SIZE i = 0; i < len; ++i) {
        RSAbase[i] = data[i];
    }
}

/**
 * @brief  Copy data from RSA registers
 * @param data     the data
 * @param len      the length of data, in u32
 * @param RSAbase  the RSA base
 */
static void CopyFromRSA(u32 *data, LT_SIZE len, const volatile u32 *RSAbase) {
    for (LT_SIZE i = 0; i < len; ++i) {
        data[i] = RSAbase[i];
    }
}

/**
 * @brief Begin an RSA operation. 
 * @param reg  the regiester to 'START'
 */
static void StartRSAOp(volatile u32 *reg) {
    /* Clear interrupt status */
    ESP32_REG(RSA_CLEAR_INTERRUPT) = 1;
    *reg = 1;
}

/* Wait for an RSA operation to complete. */
static bool WaitRSAOp(void) {
    bool bTimeout = false;
    LTTime t = LT_GetCore()->GetKernelTime();
    while (ESP32_REG(RSA_QUERY_INTERRUPT) != 1 && !bTimeout) {
        bTimeout = LTTime_IsGreaterThan(LTTime_Subtract(LT_GetCore()->GetKernelTime(), t), ESP32_TIMEOUT_LONG);
    }
    // reuse bTimeout for return now
    bTimeout = (ESP32_REG(RSA_QUERY_INTERRUPT) == 1);
    // clear the interrupt
    ESP32_REG(RSA_CLEAR_INTERRUPT) = 1;
    return bTimeout;
}

/**
 * @brief   Clear X, Y and Z RSA regs.
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_ClearRSA(LT_SIZE RSALen) {
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK), RSALen);
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK), RSALen);
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK), RSALen);
}

/**
 * @brief  Set RSA parameters, N and N1, to engine, and R2 and RSALen to context.
 *         So that following RSA computation won't need to set these parameters to engine again.
 *         It must guarantee that the engine's parameters are kept untouched until next explicit BN_Set_Mod.
 *         The following calls are ESP32_BN_Pow_Mod_Preset and ESP32_BN_Multiply_Mod_Preset
 * @param ctx     the RSA context
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param len     the length of N and R2, in u32, len <= RSALen
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 * @return   Error code
 */
LTSystemCryptoResult ESP32_BN_Set_Mod(ESP32_RSA_CTX *ctx, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen) {
    // sanity check
    if (!ctx || !N || !R2) {
        return kLTSystemCrypto_Result_Null;
    }
    if (len > RSALen || (RSALen != LTSYSTEMCRYPTO_RSA512LEN && RSALen != LTSYSTEMCRYPTO_RSA1024LEN && RSALen != LTSYSTEMCRYPTO_RSA2048LEN)) {
        return kLTSystemCrypto_Result_WrongLength;
    }

    lt_memset(ctx, 0, sizeof(ESP32_RSA_CTX));
    // load N
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_M_BLOCK) + len, RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_M_BLOCK), N, len);
    // load N1
    ESP32_REG(RSA_M_DASH) = N1;
    // load R2, must be wide as RSALen
    ctx->R2 = R2;
    // set nDataLen and RSALen
    ctx->dataLen = len;
    ctx->RSALen = RSALen;
    
    return kLTSystemCrypto_Result_Ok;
}

/**
 * @brief Modular pow two unsigned a and b, i.e. res = a ^ b % n.
 *        Shall only be called after the context is set already (ESP32_BN_Set_Mod)
 * @param ctx  the RSA context
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @note    
 *          Use example:
 *          ESP32_RSA_CTX ctx;
 *          ESP32_BN_Set_Mod(&ctx, N, R2, u256_WORDS, N1, RSA512Len);
 *          u256 res, a, b;
 *          ESP32_BN_Pow_Mod_Preset(&ctx, res, a, b);
 */
void ESP32_BN_Pow_Mod_Preset(const ESP32_RSA_CTX *ctx, u32 *res, const u32 *A, const u32 *B) {
    LT_SIZE len = ctx->dataLen;

    // load A to X
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK), A, len);

    // load B to Y
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK), B, len);

    // load R2 to Z
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK), ctx->R2, len);

    // operand size, in words - 1
    ESP32_REG(RSA_LENGTH) = ctx->RSALen - 1;

    /* Leave the engine in its constant time mode.  Its faster alternative skips
     * the leading zero bits of the exponent, which leaks the exponent length. */
    ESP32_REG(RSA_CONSTANT_TIME) = 1;
    ESP32_REG(RSA_SEARCH_OPEN)   = 0;

    /* Execute modular exponentiation */
    StartRSAOp(ESP32_REG_ADDR(RSA_MODEXP_START));
    if (!WaitRSAOp()) {
        return;
    }

    CopyFromRSA(res, len, ESP32_REG_ADDR(RSA_MEM_Z_BLOCK));
}

/**
 * @brief Modular pow two unsigned a and b, i.e. res = a ^ b % n.
 * @param res     the result
 * @param A       the first operand
 * @param B       the second operand
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param len     the length of A, B, N, R2, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Pow_Mod_HW(u32 *res, const u32 *A, const u32 *B, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen) {
    ESP32_RSA_CTX ctx;
    ESP32_BN_Set_Mod(&ctx, N, R2, len, N1, RSALen);
    ESP32_BN_Pow_Mod_Preset(&ctx, res, A, B);
}

/**
 * @brief Modular multiply two unsigned a and b, i.e. res = a * b % n.
 *        Shall only be called after the context is set already (ESP32_BN_Set_Mod)
 * @param ctx  the RSA context
 * @param res  the result
 * @param A    the first operand
 * @param B    the second operand
 * @note
 *          Use example:
 *          ESP32_RSA_CTX ctx;
 *          ESP32_BN_Set_Mod(&ctx, N, R2, u256_WORDS, N1, RSA512Len);
 *          u256 res, a, b;
 *          ESP32_BN_Multiply_Mod_Preset(&ctx, res, a, b);
 */
void ESP32_BN_Multiply_Mod_Preset(const ESP32_RSA_CTX *ctx, u32 *res, const u32 *A, const u32 *B) {
    LT_SIZE len = ctx->dataLen;

    // load A to X
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK), A, len);

    // load B to Y
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK), B, len);

    // load R2 to Z, which the engine reads as the RB operand
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK) + len, ctx->RSALen - len);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK), ctx->R2, len);

    // operand size, in words - 1
    ESP32_REG(RSA_LENGTH) = ctx->RSALen - 1;

    /* One shot: the engine reduces modulo N itself, so there is no second stage. */
    StartRSAOp(ESP32_REG_ADDR(RSA_MOD_MULT_START));
    if (!WaitRSAOp()) {
        return;
    }

    CopyFromRSA(res, len, ESP32_REG_ADDR(RSA_MEM_Z_BLOCK));
}

/**
 * @brief Modular multiply two unsigned a and b, i.e. res = a * b % n.
 * @param res     the result
 * @param A       the first operand
 * @param B       the second operand
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param len     the length of A, B, N, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Multiply_Mod_HW(u32 *res, const u32 *A, const u32 *B, const u32 *N, const u32 *R2, LT_SIZE len, u32 N1, LT_SIZE RSALen) {
    ESP32_RSA_CTX ctx;
    ESP32_BN_Set_Mod(&ctx, N, R2, len, N1, RSALen);
    ESP32_BN_Multiply_Mod_Preset(&ctx, res, A, B);
}

/**
 * @brief Multiply two unsigned a and b, i.e. res = a * b.
 * @param res     the result
 * @param A       the first operand
 * @param ALen    the length of a, in u32
 * @param B       the second operand
 * @param BLen    the length of b, in u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Multiply_HW(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *B, LT_SIZE BLen, LT_SIZE RSALen) {
    // Copy A to RSA_X, right extended: A0, A1, A2, ..., A_l, ..., 0, 0, 0, 0
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK) + ALen, ((LT_SIZE)RSALen << 1) - ALen);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK), A, ALen);
    // Copy B to RSA_Z, left extended:  0, 0, 0, 0, ..., B0, B1, B2, ..., B_l
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK), (LT_SIZE)RSALen << 1);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK) + RSALen, B, BLen);

    // the product is twice as wide as the operands
    ESP32_REG(RSA_LENGTH) = ((LT_SIZE)RSALen << 1) - 1;

    StartRSAOp(ESP32_REG_ADDR(RSA_MULT_START));
    if (!WaitRSAOp()) {
        return;
    }

    CopyFromRSA(res, ALen + BLen, ESP32_REG_ADDR(RSA_MEM_Z_BLOCK));
}

/**
 * @brief res = a % N
 * @param res     the result, the length must be NLen in u32
 * @param A       the number
 * @param ALen    the length of A, in u32
 * @param N       the modulus
 * @param R2      R2, R^2 % N
 * @param NLen    the length of N, in u32
 * @param N1      N1, -1/N % R, then take the lowest u32
 * @param RSALen  the RSA length, in u32
 */
void ESP32_BN_Mod_HW(u32 *res, const u32 *A, LT_SIZE ALen, const u32 *N, const u32 *R2, LT_SIZE NLen, u32 N1, LT_SIZE RSALen) {
    // load N
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_M_BLOCK) + NLen, RSALen - NLen);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_M_BLOCK), N, NLen);
    // load N1
    ESP32_REG(RSA_M_DASH) = N1;

    // load A to X
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK) + ALen, RSALen - ALen);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_X_BLOCK), A, ALen);

    // load 1 to Y, so the modular multiply below reduces A alone
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Y_BLOCK), RSALen);
    ESP32_REG(RSA_MEM_Y_BLOCK) = 1;

    // load R2 to Z, which the engine reads as the RB operand
    ClearRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK) + NLen, RSALen - NLen);
    CopyToRSA(ESP32_REG_ADDR(RSA_MEM_Z_BLOCK), R2, NLen);

    // operand size, in words - 1
    ESP32_REG(RSA_LENGTH) = RSALen - 1;

    StartRSAOp(ESP32_REG_ADDR(RSA_MOD_MULT_START));
    if (!WaitRSAOp()) {
        return;
    }

    CopyFromRSA(res, NLen, ESP32_REG_ADDR(RSA_MEM_Z_BLOCK));
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  27-Aug-26   claudius    created, from the esp32 driver
 */
