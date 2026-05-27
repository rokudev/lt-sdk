/*******************************************************************************
 * lt/source/common/driver/crypto/LTDriverCryptoAes128.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/*********************************************************************
 * AES128, FIPS 197, https://csrc.nist.gov/publications/detail/fips/197/final
 * AES128: Figure 4, Nb=4, Nr=10, Nk=4
 * AES does not care about endianness.
 * But, implemented for little endian: the left most byte is on the lowest address.
 *
 * Figure 3, input and state have the same order as below.
 * state = [ 0,  4,  8, 12,
 *           1,  5,  9, 13,
 *           2,  6, 10, 14,
 *           3,  7, 11, 15 ]
 */

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>

// 5.1.1, Figure 7, S-box
static const u8 S[] = {0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
                       0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
                       0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
                       0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
                       0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
                       0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
                       0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
                       0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
                       0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
                       0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
                       0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
                       0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
                       0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
                       0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
                       0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
                       0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16};

// 5.2
static const u32 RCON[] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};

#define MakeWord(x)  ((((u32)x[0])) | (((u32)x[1]) << 8) | (((u32)x[2]) << 16) | (((u32)x[3]) << 24))
#define SubWord(x)   (((u32)S[x>>24] << 24) | ((u32)S[(x>>16) & 0xFF] << 16) | ((u32)S[(x>>8) & 0xFF] << 8) | (u32)S[x & 0xFF])
#define RotWord(x)   ((x>>8) | (x<<24))

/**
 * @brief Schedule key
 * @param key  the key
 * @param ek   the expanded key after scheduling
 */
void LT_AES128_Keysched(const u8 key[AES128_KEY_LENGTH], u32 ek[44]) {
    u32 i;
    u32 t;
    for (i = 0; i < 4; ++i) {
        ek[i] = MakeWord((key + (i << 2)));
    }
    for (; i < 44; ++i) {
        t = ek[i - 1];
        if ((i & 0x03) == 0) {
            t = SubWord(RotWord(t)) ^ RCON[i >> 2];
        }
        ek[i] = ek[i - 4] ^ t;
    }
}

// 5.1.1
static void SubBytes(u8 state[AES128_STATE_LENGTH]) {
    for (u32 i = 0; i < AES128_STATE_LENGTH; ++i) {
        state[i] = S[state[i]];
    }
}

// 5.1.2
static void ShiftRows(u8 state[AES128_STATE_LENGTH]) {
    u8 s;
    // row 0, no shift
    // row 1
    s = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = s;
    // row 2
    s = state[2];
    state[2] = state[10];
    state[10] = s;
    s = state[6];
    state[6] = state[14];
    state[14] = s;
    // row 3
    s = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = s;
}

/* 5.1.3
 * Galois modulo over x^8 + x^4 + x^3 + x + 1, noted as 0x11B.
 * For mixcolumn, let u11 = u8 x u3. This u11 has three hex digits: h and ll.
 * h is u3, and l is u4. So, u11 = hll = h00 ^ 0ll
 * Then, u11 % 0x11B = hll % 0x11B = (h00 % 0x11B) ^ (ll)
 * modg[8] is a table to compute (h00 % 0x11B) as modg[h]
 * Use modg[8] to avoid the timing attack and save computation.
 */
static const u8 s_kModg[8] = {0x00,0x1B,0x36,0x2D,0x6C,0x77,0x5A,0x41};

static void MixColumns(u8 state[AES128_STATE_LENGTH]) {
    u16 x0;
    u16 x1;
    u16 x2;
    u16 x3;
    u16 y0;
    u16 y1;
    u16 y2;
    u16 y3;
    for (u32 i = 0; i < 16; i += 4) {
        x0 = state[i + 0];
        x1 = state[i + 1];
        x2 = state[i + 2];
        x3 = state[i + 3];
        // y0 = 2*x0 + 3*x1 + x2 + x3
        y0 = (x0 << 1) ^ (x1 << 1) ^ x1 ^ x2 ^ x3;
        // y1 = x0 + 2*x1 + 3*x2 + x3
        y1 = x0 ^ (x1 << 1) ^ (x2 << 1) ^ x2 ^ x3;
        // y2 = x0 + x1 + 2*x2 + 3*x3
        y2 = x0 ^ x1 ^ (x2 << 1) ^ (x3 << 1)^x3;
        // y3 = 3*x0 + x1 + x2 + 2*x3
        y3 = (x0 << 1) ^ x0 ^ x1 ^ x2 ^ (x3 << 1);
        state[i + 0] = ((u8)y0) ^ s_kModg[y0 >> 8];
        state[i + 1] = ((u8)y1) ^ s_kModg[y1 >> 8];
        state[i + 2] = ((u8)y2) ^ s_kModg[y2 >> 8];
        state[i + 3] = ((u8)y3) ^ s_kModg[y3 >> 8];
    }
}

// 5.1.4
static void AddRoundKey(u32 state[4], const u32 ek[4]) {
    for (u32 i = 0 ; i < 4; ++i) {
        state[i] ^= ek[i];
    }
}

// 5.1, Figure 5
/**
 * @brief Encrypt data
 * @param input  the input data
 * @param ek     the expanded key
 * @param output the output data after encryption
 * @note  the length of data must be 16 Bytes
 */
void LT_AES128_Encrypt(const u8 input[AES128_BLOCK_LENGTH], const u32 ek[44], u8 output[AES128_BLOCK_LENGTH]) {
    u32 state[4];

    lt_memcpy(state, input, AES128_STATE_LENGTH);

    AddRoundKey(state, ek);

    for (u32 round = 1; round < 10; ++round) {
        SubBytes((u8 *)state);
        ShiftRows((u8 *)state);
        MixColumns((u8 *)state);
        AddRoundKey(state, ek + (round << 2));
    }

    SubBytes((u8 *)state);
    ShiftRows((u8 *)state);
    AddRoundKey(state, ek + (10 << 2));

    lt_memcpy(output, state, AES128_STATE_LENGTH);
}

// 5.3.1
static void InvShiftRows(u8 state[AES128_STATE_LENGTH]) {
    u8 t;
    // row 0, no shift
    // row 1
    t = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = t;
    // row 2
    t = state[2];
    state[2] = state[10];
    state[10] = t;
    t = state[6];
    state[6] = state[14];
    state[14] = t;
    // row 3
    t = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = t;
}

/* Try not use a mode that requires AES decryption */
// 5.3.2, Figure 14, Inverse S-box
static const u8 invS[] = {0x52,0x09,0x6A,0xD5,0x30,0x36,0xA5,0x38,0xBF,0x40,0xA3,0x9E,0x81,0xF3,0xD7,0xFB,
                          0x7C,0xE3,0x39,0x82,0x9B,0x2F,0xFF,0x87,0x34,0x8E,0x43,0x44,0xC4,0xDE,0xE9,0xCB,
                          0x54,0x7B,0x94,0x32,0xA6,0xC2,0x23,0x3D,0xEE,0x4C,0x95,0x0B,0x42,0xFA,0xC3,0x4E,
                          0x08,0x2E,0xA1,0x66,0x28,0xD9,0x24,0xB2,0x76,0x5B,0xA2,0x49,0x6D,0x8B,0xD1,0x25,
                          0x72,0xF8,0xF6,0x64,0x86,0x68,0x98,0x16,0xD4,0xA4,0x5C,0xCC,0x5D,0x65,0xB6,0x92,
                          0x6C,0x70,0x48,0x50,0xFD,0xED,0xB9,0xDA,0x5E,0x15,0x46,0x57,0xA7,0x8D,0x9D,0x84,
                          0x90,0xD8,0xAB,0x00,0x8C,0xBC,0xD3,0x0A,0xF7,0xE4,0x58,0x05,0xB8,0xB3,0x45,0x06,
                          0xD0,0x2C,0x1E,0x8F,0xCA,0x3F,0x0F,0x02,0xC1,0xAF,0xBD,0x03,0x01,0x13,0x8A,0x6B,
                          0x3A,0x91,0x11,0x41,0x4F,0x67,0xDC,0xEA,0x97,0xF2,0xCF,0xCE,0xF0,0xB4,0xE6,0x73,
                          0x96,0xAC,0x74,0x22,0xE7,0xAD,0x35,0x85,0xE2,0xF9,0x37,0xE8,0x1C,0x75,0xDF,0x6E,
                          0x47,0xF1,0x1A,0x71,0x1D,0x29,0xC5,0x89,0x6F,0xB7,0x62,0x0E,0xAA,0x18,0xBE,0x1B,
                          0xFC,0x56,0x3E,0x4B,0xC6,0xD2,0x79,0x20,0x9A,0xDB,0xC0,0xFE,0x78,0xCD,0x5A,0xF4,
                          0x1F,0xDD,0xA8,0x33,0x88,0x07,0xC7,0x31,0xB1,0x12,0x10,0x59,0x27,0x80,0xEC,0x5F,
                          0x60,0x51,0x7F,0xA9,0x19,0xB5,0x4A,0x0D,0x2D,0xE5,0x7A,0x9F,0x93,0xC9,0x9C,0xEF,
                          0xA0,0xE0,0x3B,0x4D,0xAE,0x2A,0xF5,0xB0,0xC8,0xEB,0xBB,0x3C,0x83,0x53,0x99,0x61,
                          0x17,0x2B,0x04,0x7E,0xBA,0x77,0xD6,0x26,0xE1,0x69,0x14,0x63,0x55,0x21,0x0C,0x7D};

static void InvSubBytes(u8 state[AES128_STATE_LENGTH]) {
    for (u32 i = 0; i < AES128_STATE_LENGTH; ++i) state[i] = invS[state[i]];
}

// 5.3.3
static void InvMixColumns(u8 state[AES128_STATE_LENGTH]) {
    u16 x0, x1, x2, x3, y0, y1, y2, y3;
    for (u32 i = 0; i < 16; i += 4) {
        x0 = state[i + 0];
        x1 = state[i + 1];
        x2 = state[i + 2];
        x3 = state[i + 3];
        // y0 = e*x0 + b*x1 + d*x2 + 9*x3
        y0 = (x0 << 3) ^ (x0 << 2) ^ (x0 <<1 ) ^ (x1 << 3) ^ (x1 << 1) ^ x1 ^ (x2 << 3) ^ (x2 << 2) ^ x2 ^ (x3 << 3) ^ x3;
        // y1 = 9*x0 + e*x1 + b*x2 + d*x3
        y1 = (x0 << 3) ^ x0 ^ (x1 << 3) ^ (x1 << 2) ^ (x1 << 1) ^ (x2 << 3) ^ (x2 << 1) ^ x2 ^ (x3 << 3) ^ (x3 << 2) ^ x3;
        // y2 = d*x0 + 9*x1 + e*x2 + b*x3
        y2 = (x0 << 3) ^ (x0 << 2) ^ x0 ^ (x1 << 3) ^ x1 ^ (x2 << 3) ^ (x2 << 2) ^ (x2 << 1) ^ (x3 << 3) ^ (x3 << 1) ^ x3;
        // y3 = b*x0 + d*x1 + 9*x2 + e*x3
        y3 = (x0 << 3) ^ (x0 << 1) ^ x0 ^ (x1 << 3) ^ (x1 << 2) ^ x1 ^ (x2 << 3) ^ x2 ^ (x3 << 3) ^ (x3 << 2) ^ (x3 << 1);
        state[i+0] = ((u8)y0) ^ s_kModg[y0 >> 8];
        state[i+1] = ((u8)y1) ^ s_kModg[y1 >> 8];
        state[i+2] = ((u8)y2) ^ s_kModg[y2 >> 8];
        state[i+3] = ((u8)y3) ^ s_kModg[y3 >> 8];
    }
}

// 5.3
/**
 * Decrypt data
 * @param input  the input data
 * @param ek     the expanded key
 * @param output the output data after decryption
 * @note  the length of data must be 16 Bytes
 */
void LT_AES128_Decrypt(const u8 input[AES128_BLOCK_LENGTH], const u32 ek[44], u8 output[AES128_BLOCK_LENGTH]) {
    u32 state[4];

    lt_memcpy(state, input, 16);

    AddRoundKey(state, ek+40);

    for (u32 round = 9; round > 0; --round) {
        InvShiftRows((u8 *)state);
        InvSubBytes((u8 *)state);
        AddRoundKey(state, ek + (round << 2));
        InvMixColumns((u8 *)state);
    }

    InvShiftRows((u8 *)state);
    InvSubBytes((u8 *)state);
    AddRoundKey(state, ek);

    lt_memcpy(output, state, 16);
}

#if 0
LTSystemCryptoResult LT_AES128_EncryptECB(const u8 key[AES128_KEY_LENGTH], const u8 plainText[AES128_BLOCK_LENGTH], u8 cipherText[AES128_BLOCK_LENGTH]) {
    u32 *ek = lt_malloc(44 * sizeof(u32));
    if (!ek) return kLTSystemCrypto_Result_OOM;
    LT_AES128_Keysched(key, ek);
    LT_AES128_Encrypt(plainText, ek, cipherText);
    lt_free(ek);
    return kLTSystemCrypto_Result_Ok;
}

LTSystemCryptoResult LT_AES128_DecryptECB(const u8 key[AES128_KEY_LENGTH], const u8 cipherText[AES128_BLOCK_LENGTH], u8 plainText[AES128_BLOCK_LENGTH]) {
    u32 *ek = lt_malloc(44 * sizeof(u32));
    if (!ek) return kLTSystemCrypto_Result_OOM;
    LT_AES128_Keysched(key, ek);
    LT_AES128_Decrypt(cipherText, ek, plainText);
    lt_free(ek);
    return kLTSystemCrypto_Result_Ok;
}
#endif

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Feb-22   gallienus   created
 */
