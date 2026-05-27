/*******************************************************************************
 * LTSystemCrypto Unit Test
 *
 * Tests LTSystemCrypto
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include <tilt/JiltEngine.h>

// temporary for holding arbitrary data
static u8 d[1536];
static u8 e[256];

// sha256
static u8 r0[32];
static const u8 e00[] = {0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
                         0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
static u8 d01[]       = {0x06,0xe0,0x76,0xf5,0xa4,0x42,0xd5};
static const u8 e01[] = {0x3f,0xd8,0x77,0xe2,0x74,0x50,0xe6,0xbb,0xd5,0xd7,0x4b,0xb8,0x2f,0x98,0x70,0xc6,
                         0x4c,0x66,0xe1,0x09,0x41,0x8b,0xaa,0x8e,0x6b,0xbc,0xff,0x35,0x5e,0x28,0x79,0x26};
static const u8 d02[] = {0x6d,0xd6,0xef,0xd6,0xf6,0xca,0xa6,0x3b,0x72,0x9a,0xa8,0x18,0x6e,0x30,0x8b,0xc1,
                         0xbd,0xa0,0x63,0x07,0xc0,0x5a,0x2c,0x0a,0xe5,0xa3,0x68,0x4e,0x6e,0x46,0x08,0x11,
                         0x74,0x86,0x90,0xdc,0x2b,0x58,0x77,0x59,0x67,0xcf,0xcc,0x64,0x5f,0xd8,0x20,0x64,
                         0xb1,0x27,0x9f,0xdc,0xa7,0x71,0x80,0x3d,0xb9,0xdc,0xa0,0xff,0x53};
static const u8 e02[] = {0xc4,0x64,0xa7,0xbf,0x6d,0x18,0x0d,0xe4,0xf7,0x44,0xbb,0x2f,0xe5,0xdc,0x27,0xa3,
                         0xf6,0x81,0x33,0x4f,0xfd,0x54,0xa9,0x81,0x46,0x50,0xe6,0x02,0x60,0xa4,0x78,0xe3};

// hmac-sha256, L=32
static u8 r1[32];
// Count = 30
static const u8 e10[] = {0xee,0xf3,0x19,0x23,0x09,0x70,0x15,0x95,0x96,0xb9,0xec,0xcc,0x18,0xb6,0xab,0x4d,
                         0xe5,0x9a,0x40,0x73,0x3d,0xd7,0xc7,0x14,0x97,0x97,0x13,0x34,0x21,0x60,0xe0,0x77};
static const u8 k11[] = {0x97,0x79,0xd9,0x12,0x06,0x42,0x79,0x7f,0x17,0x47,0x02,0x5d,0x5b,0x22,0xb7,0xac,
                         0x60,0x7c,0xab,0x08,0xe1,0x75,0x8f,0x2f,0x3a,0x46,0xc8,0xbe,0x1e,0x25,0xc5,0x3b,
                         0x8c,0x6a,0x8f,0x58,0xff,0xef,0xa1,0x76};
static const u8 d11[] = {0xb1,0x68,0x9c,0x25,0x91,0xea,0xf3,0xc9,0xe6,0x60,0x70,0xf8,0xa7,0x79,0x54,0xff,
                         0xb8,0x17,0x49,0xf1,0xb0,0x03,0x46,0xf9,0xdf,0xe0,0xb2,0xee,0x90,0x5d,0xcc,0x28,
                         0x8b,0xaf,0x4a,0x92,0xde,0x3f,0x40,0x01,0xdd,0x9f,0x44,0xc4,0x68,0xc3,0xd0,0x7d,
                         0x6c,0x6e,0xe8,0x2f,0xac,0xea,0xfc,0x97,0xc2,0xfc,0x0f,0xc0,0x60,0x17,0x19,0xd2,
                         0xdc,0xd0,0xaa,0x2a,0xec,0x92,0xd1,0xb0,0xae,0x93,0x3c,0x65,0xeb,0x06,0xa0,0x3c,
                         0x9c,0x93,0x5c,0x2b,0xad,0x04,0x59,0x81,0x02,0x41,0x34,0x7a,0xb8,0x7e,0x9f,0x11,
                         0xad,0xb3,0x04,0x15,0x42,0x4c,0x6c,0x7f,0x5f,0x22,0xa0,0x03,0xb8,0xab,0x8d,0xe5,
                         0x4f,0x6d,0xed,0x0e,0x3a,0xb9,0x24,0x5f,0xa7,0x95,0x68,0x45,0x1d,0xfa,0x25,0x8e};
static const u8 e11[] = {0x76,0x9f,0x00,0xd3,0xe6,0xa6,0xcc,0x1f,0xb4,0x26,0xa1,0x4a,0x4f,0x76,0xc6,0x46,
                         0x2e,0x61,0x49,0x72,0x6e,0x0d,0xee,0x0e,0xc0,0xcf,0x97,0xa1,0x66,0x05,0xac,0x8b};
//Count = 129
static const u8 k12[] = {0xaf,0x81,0xe3,0x27,0x52,0x5f,0x3a,0x91,0x04,0xb7,0x28,0x29,0x59,0xa0,0xf6,0x60,
                         0x0f,0xad,0x7e,0xfa,0xe7,0x70,0x9b,0xb8,0xb3,0x3c,0xde,0x34,0xb1,0x2f,0x83,0x0c,
                         0x17,0x70,0xa3,0x42,0xef,0xb6,0xab,0xe3,0x25,0x0a,0x0c,0xe7,0xdf,0xcd,0x34,0x59,
                         0x0c,0xfc,0xbe,0xb8,0x40,0xb3,0xe5,0x9c,0xbf,0xf0,0x3f,0x9c,0xd8,0x9a,0xa8,0x70};
static const u8 d12[] = {0x75,0xed,0x3a,0xe9,0x08,0x5b,0xbf,0x2d,0x03,0x4b,0x86,0x4d,0x7f,0x87,0x05,0x7c,
                         0x2d,0x0b,0x12,0xc7,0x39,0x5f,0xeb,0x03,0x75,0x23,0x79,0x03,0xb3,0xeb,0xd6,0x0e,
                         0x72,0x4e,0x0c,0x8f,0xbe,0x3a,0x20,0x0f,0x51,0x8a,0x4f,0x61,0xfe,0xdb,0x97,0x1c,
                         0x50,0x9b,0x79,0x4f,0x6e,0x62,0xfe,0x6f,0x41,0x86,0xf8,0x94,0xd9,0xea,0x8a,0xe5,
                         0x0d,0x16,0xea,0x51,0x62,0x8d,0x66,0x81,0x2f,0x5a,0xa5,0x0a,0xfe,0xed,0x30,0xe6,
                         0x34,0x25,0x30,0x25,0xf5,0xae,0x7a,0xe0,0x42,0x8d,0xc8,0x6f,0x64,0xf9,0x49,0xdb,
                         0x8e,0x6d,0x5d,0x96,0xbe,0xfb,0x99,0x6a,0xe4,0xe3,0x12,0xb0,0x46,0x64,0xd8,0xc2,
                         0x23,0xd2,0xc0,0xb3,0x96,0xe9,0x67,0x3d,0xbe,0x61,0x73,0xfa,0x1c,0xc2,0x1c,0xd7};
static const u8 e12[] = {0x57,0x9d,0x35,0xce,0xf5,0xb6,0xf8,0x46,0x8c,0x82,0x85,0x82,0x98,0x61,0xe9,0x35,
                         0x87,0xc8,0xde,0xe5,0x79,0x12,0x08,0x40,0x6a,0x7f,0x4b,0xfa,0xfb,0x70,0xab,0xfd};

// drbg, using drbg.hash
static u8 r21[64], r22[64];

// aes gcm
static const u8 et30[] = {0x32,0x47,0x18,0x4B,0x3C,0x4F,0x69,0xA4,0x4D,0xBC,0xD2,0x28,0x87,0xBB,0xB4,0x18};
static const u8 k31[]  = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
static const u8 iv31[] = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88};
static const u8 p31[]  = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
                          0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
                          0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
                          0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39,0x1A,0xAF,0xD2,0x55};
// static u8 *a31   = NULL; // no aad here
// static u32 al31  = 0;
static const u32 tl31  = 16;
static const u8 ec31[] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
                          0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
                          0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
                          0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91,0x47,0x3F,0x59,0x85};
static const u8 et31[] = {0x4D,0x5C,0x2A,0xF3,0x27,0xCD,0x64,0xA6,0x2C,0xF3,0x5A,0xBD,0x2B,0xA6,0xFA,0xB4};
static const u8 p32[]  = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
                          0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
                          0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
                          0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39};
static const u8 a32[]  = {0x3A,0xD7,0x7B,0xB4,0x0D,0x7A,0x36,0x60,0xA8,0x9E,0xCA,0xF3,0x24,0x66,0xEF,0x97,0xF5,0xD3,0xD5,0x85};
static const u32 al32  = sizeof(a32);
static const u32 tl32  = 12;
static const u8 ec32[] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
                          0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
                          0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
                          0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91};
static const u8 et32[] = {0xF0,0x7C,0x25,0x28,0xEE,0xA2,0xFC,0xA1,0x21,0x1F,0x90,0x5E};
static const u8 k33[]  = {0x08,0x55,0x5d,0x33,0x9b,0x2f,0x8c,0xff,0x87,0xf3,0x6c,0x00,0xde,0x5e,0xa8,0x0a};
static const u8 iv33[] = {0xf7,0x0b,0xcf,0x9f,0x16,0xeb,0xcb,0xdf,0xd4,0x97,0xc5,0xbd};
static const u8 a33[]  = {0x17,0x03,0x03,0x00,0x2a};
static const u8 p33[]  = {0x08,0x00,0x00,0x15,0x00,0x13,0x00,0x01,0x00,0x01,0x03,0x00,0x10,0x00,0x0a,0x00,
                         0x08,0x07,0x6e,0x74,0x73,0x6b,0x65,0x2f,0x31,0x16};
static const u8 ec33[] = {0xbc,0xec,0x9f,0xb8,0xad,0x8a,0x4f,0xc6,0x9e,0xea,0x7c,0x43,0x51,0x13,0xb3,0xc2,
                          0x26,0x41,0xd4,0xad,0xa0,0xe6,0x6b,0xe9,0xc6,0xfc};
static const u8 et33[] = {0x4e,0x3a,0x48,0xe8,0x86,0xcd,0xda,0x3f,0x6f,0xf3,0xf5,0xbc,0x57,0x06,0xe4,0x34};
static const u8 k34[]  = {0xFE,0xFF,0xE9,0x92,0x86,0x65,0x73,0x1C,0x6D,0x6A,0x8F,0x94,0x67,0x30,0x83,0x08};
static const u8 iv34[] = {0xCA,0xFE,0xBA,0xBE,0xFA,0xCE,0xDB,0xAD,0xDE,0xCA,0xF8,0x88};
static const u8 a34[]  = {0x3A,0xD7,0x7B,0xB4,0x0D,0x7A,0x36,0x60,0xA8,0x9E,0xCA,0xF3,0x24,0x66,0xEF,0x97,0xF5,0xD3,0xD5,0x85};
static const u8 p34[]  = {0xD9,0x31,0x32,0x25,0xF8,0x84,0x06,0xE5,0xA5,0x59,0x09,0xC5,0xAF,0xF5,0x26,0x9A,
                          0x86,0xA7,0xA9,0x53,0x15,0x34,0xF7,0xDA,0x2E,0x4C,0x30,0x3D,0x8A,0x31,0x8A,0x72,
                          0x1C,0x3C,0x0C,0x95,0x95,0x68,0x09,0x53,0x2F,0xCF,0x0E,0x24,0x49,0xA6,0xB5,0x25,
                          0xB1,0x6A,0xED,0xF5,0xAA,0x0D,0xE6,0x57,0xBA,0x63,0x7B,0x39};
static const u8 ec34[] = {0x42,0x83,0x1E,0xC2,0x21,0x77,0x74,0x24,0x4B,0x72,0x21,0xB7,0x84,0xD0,0xD4,0x9C,
                          0xE3,0xAA,0x21,0x2F,0x2C,0x02,0xA4,0xE0,0x35,0xC1,0x7E,0x23,0x29,0xAC,0xA1,0x2E,
                          0x21,0xD5,0x14,0xB2,0x54,0x66,0x93,0x1C,0x7D,0x8F,0x6A,0x5A,0xAC,0x84,0xAA,0x05,
                          0x1B,0xA3,0x0B,0x39,0x6A,0x0A,0xAC,0x97,0x3D,0x58,0xE0,0x91};
static const u8 et34[] = {0xF0,0x7C,0x25,0x28,0xEE,0xA2,0xFC,0xA1,0x21,0x1F,0x90,0x5E,0x1B,0x6A,0x88,0x1B};

// eddsa
static const u8 priKey[EdDSA_KEY_LENGTH] = {0x83,0x3F,0xE6,0x24,0x09,0x23,0x7B,0x9D,0x62,0xEC,0x77,0x58,0x75,0x20,0x91,0x1E,
                                            0x9A,0x75,0x9C,0xEC,0x1D,0x19,0x75,0x5B,0x7D,0xA9,0x01,0xB9,0x6D,0xCA,0x3D,0x42};
static u8 r42[64], k42[32];
static const u8 d42[]  = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
                          0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
                          0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
                          0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
static const u8 er42[] = {0xdc,0x2a,0x44,0x59,0xe7,0x36,0x96,0x33,0xa5,0x2b,0x1b,0xf2,0x77,0x83,0x9a,0x00,
                          0x20,0x10,0x09,0xa3,0xef,0xbf,0x3e,0xcb,0x69,0xbe,0xa2,0x18,0x6c,0x26,0xb5,0x89,
                          0x09,0x35,0x1f,0xc9,0xac,0x90,0xb3,0xec,0xfd,0xfb,0xc7,0xc6,0x64,0x31,0xe0,0x30,
                          0x3d,0xca,0x17,0x9c,0x13,0x8a,0xc1,0x7a,0xd9,0xbe,0xf1,0x17,0x73,0x31,0xa7,0x04};
static const u8 ek42[] = {0xec,0x17,0x2b,0x93,0xad,0x5e,0x56,0x3b,0xf4,0x93,0x2c,0x70,0xe1,0x24,0x50,0x34,
                          0xc3,0x54,0x67,0xef,0x2e,0xfd,0x4d,0x64,0xeb,0xf8,0x19,0x68,0x34,0x67,0xe2,0xbf};

// sha1
static u8 r5[20];
static const u8 e50[] = {0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09};
static const u8 d51[] = {0x9e,0x61,0xe5,0x5d,0x9e,0xd3,0x7b,0x1c,0x20};
static const u8 e51[] = {0x41,0x1c,0xce,0xe1,0xf6,0xe3,0x67,0x7d,0xf1,0x26,0x98,0x41,0x1e,0xb0,0x9d,0x3f,0xf5,0x80,0xaf,0x97};
static const u8 d52[] = {0x45,0x92,0x7e,0x32,0xdd,0xf8,0x01,0xca,0xf3,0x5e,0x18,0xe7,0xb5,0x07,0x8b,0x7f,
                         0x54,0x35,0x27,0x82,0x12,0xec,0x6b,0xb9,0x9d,0xf8,0x84,0xf4,0x9b,0x32,0x7c,0x64,
                         0x86,0xfe,0xae,0x46,0xba,0x18,0x7d,0xc1,0xcc,0x91,0x45,0x12,0x1e,0x14,0x92,0xe6,
                         0xb0,0x6e,0x90,0x07,0x39,0x4d,0xc3,0x3b,0x77,0x48,0xf8,0x6a,0xc3,0x20,0x7c,0xfe};
static const u8 e52[] = {0xa7,0x0c,0xfb,0xfe,0x75,0x63,0xdd,0x0e,0x66,0x5c,0x7c,0x67,0x15,0xa9,0x6a,0x8d,0x75,0x69,0x50,0xc0};

// hmac-sha1, L=20
static u8 r6[32];
//Count = 108
static const u8 e60[] = {0x0e,0xb7,0x22,0x2f,0x76,0x5c,0x11,0xa8,0x84,0x17,0x4f,0x5e,0xae,0xd0,0xae,0x13,0x61,0x90,0x22,0x83};
static const u8 k61[] = {0x8d,0xdf,0x3b,0xe2,0xab,0x49,0xf1,0x1f,0x12,0xf3,0x92,0xa0,0x9f,0x5b,0x72,0xfc,
                         0xdd,0xec,0x1e,0x18,0x6d,0xd3,0xe4,0x9a,0xab,0x0e,0x95,0xa0,0x8e,0xc5,0x89,0xb1};
static const u8 d61[] = {0x8a,0x2d,0xb9,0x6a,0x4d,0xf1,0x88,0xec,0x32,0x3e,0xf6,0xea,0xa7,0xd5,0x8b,0x56,
                         0x21,0x6b,0x00,0x97,0xbe,0xb5,0x01,0x39,0x29,0xc2,0x31,0xe3,0xbe,0x8d,0x6f,0x89,
                         0xee,0xd3,0x58,0xe2,0xe5,0x22,0x0c,0x1d,0x6b,0x33,0x35,0xd0,0x08,0x79,0x46,0x31,
                         0x6c,0xfa,0x01,0x88,0x0d,0x5e,0x3c,0xe4,0x12,0x45,0xe4,0x0d,0x70,0xde,0x42,0xbb,
                         0x53,0xb6,0x7d,0x05,0xbf,0xcd,0x61,0x1c,0x77,0xef,0x5e,0x39,0x1e,0x41,0xd4,0xd4,
                         0x9c,0x1b,0x8e,0x17,0xc3,0x15,0x8c,0x92,0x33,0x65,0x05,0x30,0x7a,0x68,0xac,0x6a,
                         0x80,0x7e,0x33,0xba,0x23,0x1b,0x0d,0x53,0x1e,0x1b,0x79,0x0f,0x2f,0x56,0xbc,0xa9,
                         0x79,0x75,0xad,0x2c,0x27,0x04,0x77,0xab,0x52,0xc8,0x9b,0x33,0x24,0x52,0x34,0xfe};
static const u8 e61[] = {0x76,0x52,0xe4,0xb2,0x40,0x51,0x28,0x3a,0xf4,0xca,0xf6,0x70,0x79,0x95,0x53,0x73,0xf6,0x60,0x4c,0x9a};
// Count = 299
static const u8 k62[] = {0xce,0xb9,0xae,0xdf,0x8d,0x6e,0xfc,0xf0,0xae,0x52,0xbe,0xa0,0xfa,0x99,0xa9,0xe2,
                         0x6a,0xe8,0x1b,0xac,0xea,0x0c,0xff,0x4d,0x5e,0xec,0xf2,0x01,0xe3,0xbc,0xa3,0xc3,
                         0x57,0x74,0x80,0x62,0x1b,0x81,0x8f,0xd7,0x17,0xba,0x99,0xd6,0xff,0x95,0x8e,0xa3,
                         0xd5,0x9b,0x25,0x27,0xb0,0x19,0xc3,0x43,0xbb,0x19,0x9e,0x64,0x80,0x90,0x22,0x58,
                         0x67,0xd9,0x94,0x60,0x79,0x62,0xf5,0x86,0x6a,0xa6,0x29,0x30,0xd7,0x5b,0x58,0xf6};
static const u8 d62[] = {0x99,0x95,0x8a,0xa4,0x59,0x60,0x46,0x57,0xc7,0xbf,0x6e,0x4c,0xdf,0xcc,0x87,0x85,
                         0xf0,0xab,0xf0,0x6f,0xfe,0x63,0x6b,0x5b,0x64,0xec,0xd9,0x31,0xbd,0x8a,0x45,0x63,
                         0x05,0x59,0x24,0x21,0xfc,0x28,0xdb,0xcc,0xcb,0x8a,0x82,0xac,0xea,0x2b,0xe8,0xe5,
                         0x41,0x61,0xd7,0xa7,0x8e,0x03,0x99,0xa6,0x06,0x7e,0xba,0xca,0x3f,0x25,0x10,0x27,
                         0x4d,0xc9,0xf9,0x2f,0x2c,0x8a,0xe4,0x26,0x5e,0xec,0x13,0xd7,0xd4,0x2e,0x9f,0x86,
                         0x12,0xd7,0xbc,0x25,0x8f,0x91,0x3e,0xcb,0x5a,0x3a,0x5c,0x61,0x03,0x39,0xb4,0x9f,
                         0xb9,0x0e,0x90,0x37,0xb0,0x2d,0x68,0x4f,0xc6,0x0d,0xa8,0x35,0x65,0x7c,0xb2,0x4e,
                         0xab,0x35,0x27,0x50,0xc8,0xb4,0x63,0xb1,0xa8,0x49,0x46,0x60,0xd3,0x6c,0x3a,0xb2};
static const u8 e62[] = {0x4a,0xc4,0x1a,0xb8,0x9f,0x62,0x5c,0x60,0x12,0x5e,0xd6,0x5f,0xfa,0x95,0x8c,0x6b,0x49,0x0e,0xa6,0x70};

// aes128 ctr
static const u8 iv71[] = {0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF};
static const u8 p71[]  = {0x6B,0xC1,0xBE,0xE2,0x2E,0x40,0x9F,0x96,0xE9,0x3D,0x7E,0x11,0x73,0x93,0x17,0x2A,
                          0xAE,0x2D,0x8A,0x57,0x1E,0x03,0xAC,0x9C,0x9E,0xB7,0x6F,0xAC,0x45,0xAF,0x8E,0x51,
                          0x30,0xC8,0x1C,0x46,0xA3,0x5C,0xE4,0x11,0xE5,0xFB,0xC1,0x19,0x1A,0x0A,0x52,0xEF,
                          0xF6,0x9F,0x24,0x45,0xDF,0x4F,0x9B,0x17,0xAD,0x2B,0x41,0x7B,0xE6,0x6C,0x37,0x10};
static const u8 k71[]  = {0x2B,0x7E,0x15,0x16,0x28,0xAE,0xD2,0xA6,0xAB,0xF7,0x15,0x88,0x09,0xCF,0x4F,0x3C};
static const u8 ec71[] = {0x87,0x4D,0x61,0x91,0xB6,0x20,0xE3,0x26,0x1B,0xEF,0x68,0x64,0x99,0x0D,0xB6,0xCE,
                          0x98,0x06,0xF6,0x6B,0x79,0x70,0xFD,0xFF,0x86,0x17,0x18,0x7B,0xB9,0xFF,0xFD,0xFF,
                          0x5A,0xE4,0xDF,0x3E,0xDB,0xD5,0xD3,0x5E,0x5B,0x4F,0x09,0x02,0x0D,0xB0,0x3E,0xAB,
                          0x1E,0x03,0x1D,0xDA,0x2F,0xBE,0x03,0xD1,0x79,0x21,0x70,0xA0,0xF3,0x00,0x9C,0xEE};

// aes128 xts
// Non-8bit aligned tests omitted since we do not support non-8bit aligned payload.
// COUNT = 1
// DataUnitLen = 128
static const u8 k1_72[] = {0xa1,0xb9,0x0c,0xba,0x3f,0x06,0xac,0x35,0x3b,0x2c,0x34,0x38,0x76,0x08,0x17,0x62};
static const u8 k2_72[] = {0x09,0x09,0x23,0x02,0x6e,0x91,0x77,0x18,0x15,0xf2,0x9d,0xab,0x01,0x93,0x2f,0x2f};
static const u8 iv72[] =  {0x4f,0xae,0xf7,0x11,0x7c,0xda,0x59,0xc6,0x6e,0x4b,0x92,0x01,0x3e,0x76,0x8a,0xd5};
static const u8 p72[] =   {0xeb,0xab,0xce,0x95,0xb1,0x4d,0x3c,0x8d,0x6f,0xb3,0x50,0x39,0x07,0x90,0x31,0x1c};
static const u8 ec72[] =  {0x77,0x8a,0xe8,0xb4,0x3c,0xb9,0x8d,0x5a,0x82,0x50,0x81,0xd5,0xbe,0x47,0x1c,0x63};

// COUNT = 101
// DataUnitLen = 256
static const u8 k1_73[] = {0xb7,0xb9,0x3f,0x51,0x6a,0xef,0x29,0x5e,0xff,0x3a,0x29,0xd8,0x37,0xcf,0x1f,0x13};
static const u8 k2_73[] = {0x53,0x47,0xe8,0xa2,0x1d,0xae,0x61,0x6f,0xf5,0x06,0x2b,0x2e,0x8d,0x78,0xce,0x5e};
static const u8 iv73[] =  {0x87,0x3e,0xde,0xa6,0x53,0xb6,0x43,0xbd,0x8b,0xcf,0x51,0x40,0x31,0x97,0xed,0x14};
static const u8 p73[] =  {0x23,0x6f,0x8a,0x5b,0x58,0xdd,0x55,0xf6,0x19,0x4e,0xd7,0x0c,0x4a,0xc1,0xa1,0x7f,0x1f,0xe6,0x0e,0xc9,0xa6,0xc4,0x54,0xd0,0x87,0xcc,0xb7,0x7d,0x6b,0x63,0x8c,0x47};
static const u8 ec73[] =  {0x22,0xe6,0xa3,0xc6,0x37,0x9d,0xcf,0x75,0x99,0xb0,0x52,0xb5,0xa7,0x49,0xc7,0xf7,0x8a,0xd8,0xa1,0x1b,0x9f,0x1a,0xa9,0x43,0x0c,0xf3,0xae,0xf4,0x45,0x68,0x2e,0x19};

// COUNT = 301
// DataUnitLen = 200
static const u8 k1_74[] = {0x39,0x4c,0x97,0x88,0x1a,0xbd,0x98,0x9d,0x29,0xc7,0x03,0xe4,0x8a,0x72,0xb3,0x97};
static const u8 k2_74[] = {0xa7,0xac,0xf5,0x1b,0x59,0x64,0x9e,0xee,0xa9,0xb3,0x32,0x74,0xd8,0x54,0x1d,0xf4};
static const u8 iv74[] =  {0x4b,0x15,0xc6,0x84,0xa1,0x52,0xd4,0x85,0xfe,0x99,0x37,0xd3,0x9b,0x16,0x8c,0x29};
static const u8 p74[] = {0x2f,0x3b,0x9d,0xcf,0xba,0xe7,0x29,0x58,0x3b,0x1d,0x1f,0xfd,0xd1,0x6b,0xb6,0xfe,0x27,0x57,0x32,0x94,0x35,0x66,0x2a,0x78,0xf0};
static const u8 ec74[] = {0xf3,0x47,0x38,0x02,0xe3,0x8a,0x3f,0xfe,0xf4,0xd4,0xfb,0x8e,0x6a,0xa2,0x66,0xeb,0xde,0x55,0x3a,0x64,0x52,0x8a,0x06,0x46,0x3e};

// COUNT = 6
// DataUnitLen = 128
// DataUnitSeqNumber = 99
static const u8 k1_75[] = {0x8b,0x73,0x7d,0x18,0x20,0x17,0xa7,0x9a,0x61,0x39,0x29,0xb0,0x7b,0x70,0xe8,0x0e};
static const u8 k2_75[] = {0x74,0x23,0x5d,0xaa,0xb3,0xf4,0xd3,0xd5,0x5e,0xf4,0xd5,0x2a,0xdb,0xc4,0x4e,0x06};
static const u8 iv75[] =  {0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 p75[] =   {0x89,0x5b,0xf0,0x8d,0x41,0xb2,0x87,0x98,0xbe,0x0f,0x57,0x6f,0xdb,0x03,0xb7,0x10};
static const u8 ec75[] =  {0x66,0x32,0x21,0x99,0xdd,0x39,0x76,0x09,0x23,0x1c,0xb3,0xe9,0x49,0x5f,0x99,0x2e};

// COUNT = 355
// DataUnitLen = 200
// DataUnitSeqNumber = 183
static const u8 k1_76[] = {0x40,0xd1,0xb9,0x38,0xf4,0x62,0x0f,0xfb,0x29,0x61,0xd8,0x68,0xf8,0x9f,0x5d,0x7c};
static const u8 k2_76[] = {0x0d,0x26,0xa5,0xeb,0xdc,0xc3,0xa5,0x21,0x71,0x7a,0x72,0xf5,0x0c,0x7e,0x67,0xca};
static const u8 iv76[] =  {0xb7,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 p76[] =   {0x83,0xa9,0xfb,0x45,0x6d,0x9c,0xb7,0x28,0x22,0xea,0x21,0x35,0xf7,0xe7,0x9c,0xe5,0x77,0xb1,0x08,0xe6,0x37,0x2e,0x1e,0xe2,0x48};
static const u8 ec76[] =  {0x54,0xc2,0xe2,0x30,0xfc,0xfa,0x72,0x81,0xdc,0x04,0xe0,0xc3,0xa5,0x86,0xad,0x1c,0xdc,0x03,0x5a,0x27,0xd3,0xa4,0x81,0x10,0xa4};

// CBC test vectors generated with Openssl
static const u8 k77[] =   {0x42,0xfc,0xac,0x51,0x43,0x33,0xea,0x41,0x3e,0x55,0xee,0xa8,0xa0,0x58,0x3b,0x70};
static const u8 iv77[] =  {0xcb,0x52,0x75,0xf1,0x60,0xc3,0xd4,0x44,0xd9,0x0c,0x5c,0x62,0x71,0x18,0x68,0x1d};
static const u8 p77[] =   {0x0a,0xb4,0xe4,0x84,0x4f,0x23,0xb6,0x03,0x36,0xf5,0xca,0xb8,0x21,0xd7,0x29,0xfd};
static const u8 ec77[] =  {0x0e,0x88,0x60,0xc6,0xe4,0xf3,0x96,0x40,0x05,0x0e,0x0f,0x7b,0x6f,0x24,0x16,0xdc};

static const u8 k78[] =   {0xef,0x30,0xa7,0x8d,0x33,0xa2,0x04,0x72,0xd8,0x88,0xb6,0x59,0x0a,0x4a,0xb1,0xba};
static const u8 iv78[] =  {0x19,0x67,0xb3,0xbb,0x29,0xd6,0xcc,0x39,0xc0,0x5d,0x6d,0x45,0x64,0xcc,0x81,0xb8};
static const u8 p78[] =   {0x87,0x51,0x88,0x02,0xb6,0xe9,0x5a,0x9a,0xd4,0x40,0x16,0x14,0xe2,0x18,0xe9,0x5b,
                           0xcf,0x9e,0x41,0x8a,0xb7,0x93,0xf5,0x06,0xa4,0x04,0xed,0x84,0x4f,0x19,0x58,0xd9,
                           0x4b,0xf1,0x39,0xfe,0x0e,0x80,0x62,0x15,0xc9,0x13,0x22,0x95,0x79,0xdd,0x17,0x40,
                           0x56,0x1f,0x39,0xa4,0x60,0xc3,0xf8,0xc4,0x53,0x04,0x69,0x26,0x14,0x6a,0x2c,0xcc};
static const u8 ec78[] =  {0xb0,0x55,0x1c,0x83,0xa3,0xe8,0x8b,0x12,0x37,0x67,0x7f,0x79,0x75,0x77,0x4f,0x50,
                           0x31,0xac,0x5a,0x60,0xae,0xee,0xae,0x1b,0xc2,0x84,0x72,0xd1,0xbd,0xe3,0xc7,0xde,
                           0x9a,0x48,0xb3,0x2e,0x9a,0x09,0x41,0xf8,0x0a,0x72,0x09,0x84,0x7b,0x64,0x82,0xe1,
                           0x74,0xf4,0xe4,0xcb,0x85,0xd5,0xa1,0x97,0xe2,0xe5,0x62,0x0e,0xc3,0xf3,0xd4,0x50};

// ecdhe x25519
// a :  48024180843069071553745934684982006431825596986621126406018887516696408295280
static const u8         a[32] = {0x70,0x07,0x6D,0x0A,0x73,0x18,0xA5,0x7D,0x3C,0x16,0xC1,0x72,0x51,0xB2,0x66,0x45,
                                 0xDF,0x4C,0x2F,0x87,0xEB,0xC0,0x99,0x2A,0xB1,0x77,0xFB,0xA5,0x1D,0xB9,0x2C,0x6A};
// A :  48084050389777770101701157326923977117307187144965043058462938058489685090437
static const u8         A[32] = {0x85,0x20,0xF0,0x09,0x89,0x30,0xA7,0x54,0x74,0x8B,0x7D,0xDC,0xB4,0x3E,0xF7,0x5A,
                                 0x0D,0xBF,0x3A,0x0D,0x26,0x38,0x1A,0xF4,0xEB,0xA4,0xA9,0x8E,0xAA,0x9B,0x4E,0x6A};
// b :  48794194057373861652369136623399865312182792178494469274796512275582446775128
static const u8         b[32] = {0x58,0xAB,0x08,0x7E,0x62,0x4A,0x8A,0x4B,0x79,0xE1,0x7F,0x8B,0x83,0x80,0x0E,0xE6,
                                 0x6F,0x3B,0xB1,0x29,0x26,0x18,0xB6,0xFD,0x1C,0x2F,0x8B,0x27,0xFF,0x88,0xE0,0x6B};
// B :  35809631094079244041211258971985475468665640815735853089228998203411133079262
static const u8         B[32] = {0xDE,0x9E,0xDB,0x7D,0x7B,0x7D,0xC1,0xB4,0xD3,0x5B,0x61,0xC2,0xEC,0xE4,0x35,0x37,
                                 0x3F,0x83,0x43,0xC8,0x5B,0x78,0x67,0x4D,0xAD,0xFC,0x7E,0x14,0x6F,0x88,0x2B,0x4F};
// K :  29893438142586401087946310744922998080771935139441267052026283852717044358474
static const u8         K[32] = {0x4A,0x5D,0x9D,0x5B,0xA4,0xCE,0x2D,0xE1,0x72,0x8E,0x3B,0xF4,0x80,0x35,0x0F,0x25,
                                 0xE0,0x7E,0x21,0xC9,0x47,0xD1,0x9E,0x33,0x76,0xF0,0x9B,0x3C,0x1E,0x16,0x17,0x42};
static u8 o[32];

// ecdsa p256 sha256
static u8 k91[ECDSA_P256_PUBLICKEY_LENGTH];
static u8 s91[ECDSA_P256_SIGNATURE_LENGTH];
//private key, d = 0x519b423d715f8b581f4fa8ee59f4771a5b44c8130b4e3eacca54a56dda72b464;
static const u8 pk91[] = {0x51,0x9b,0x42,0x3d,0x71,0x5f,0x8b,0x58,0x1f,0x4f,0xa8,0xee,0x59,0xf4,0x77,0x1a,
                       0x5b,0x44,0xc8,0x13,0x0b,0x4e,0x3e,0xac,0xca,0x54,0xa5,0x6d,0xda,0x72,0xb4,0x64};
// Qx = 0x1ccbe91c075fc7f4f033bfa248db8fccd3565de94bbfb12f3c59ff46c271bf83;
// Qy = 0xce4014c68811f9a21a1fdb2c0e6113e06db7ca93b7404e78dc7ccd5ca89a4ca9;
static const u8 ek91[] = {0x1c,0xcb,0xe9,0x1c,0x07,0x5f,0xc7,0xf4,0xf0,0x33,0xbf,0xa2,0x48,0xdb,0x8f,0xcc,
                          0xd3,0x56,0x5d,0xe9,0x4b,0xbf,0xb1,0x2f,0x3c,0x59,0xff,0x46,0xc2,0x71,0xbf,0x83,
                          0xce,0x40,0x14,0xc6,0x88,0x11,0xf9,0xa2,0x1a,0x1f,0xdb,0x2c,0x0e,0x61,0x13,0xe0,
                          0x6d,0xb7,0xca,0x93,0xb7,0x40,0x4e,0x78,0xdc,0x7c,0xcd,0x5c,0xa8,0x9a,0x4c,0xa9};
// R = 0xc58eb273c7370291fe8c6116e878ba2302666536c02261cdb105a59bfbb38836
// S = 0x07d35d9ee15c1114f8eb936e714e83fd4dce528fcdeac5a6dcc0bb29e951442b
static const u8 es91[] = {0xc5,0x8e,0xb2,0x73,0xc7,0x37,0x02,0x91,0xfe,0x8c,0x61,0x16,0xe8,0x78,0xba,0x23,
                          0x02,0x66,0x65,0x36,0xc0,0x22,0x61,0xcd,0xb1,0x05,0xa5,0x9b,0xfb,0xb3,0x88,0x36,
                          0x07,0xd3,0x5d,0x9e,0xe1,0x5c,0x11,0x14,0xf8,0xeb,0x93,0x6e,0x71,0x4e,0x83,0xfd,
                          0x4d,0xce,0x52,0x8f,0xcd,0xea,0xc5,0xa6,0xdc,0xc0,0xbb,0x29,0xe9,0x51,0x44,0x2b};
static const u8 d91[] = {
    0x59,0x05,0x23,0x88,0x77,0xc7,0x74,0x21,0xf7,0x3e,0x43,0xee,0x3d,0xa6,0xf2,0xd9,
    0xe2,0xcc,0xad,0x5f,0xc9,0x42,0xdc,0xec,0x0c,0xbd,0x25,0x48,0x29,0x35,0xfa,0xaf,
    0x41,0x69,0x83,0xfe,0x16,0x5b,0x1a,0x04,0x5e,0xe2,0xbc,0xd2,0xe6,0xdc,0xa3,0xbd,
    0xf4,0x6c,0x43,0x10,0xa7,0x46,0x1f,0x9a,0x37,0x96,0x0c,0xa6,0x72,0xd3,0xfe,0xb5,
    0x47,0x3e,0x25,0x36,0x05,0xfb,0x1d,0xdf,0xd2,0x80,0x65,0xb5,0x3c,0xb5,0x85,0x8a,
    0x8a,0xd2,0x81,0x75,0xbf,0x9b,0xd3,0x86,0xa5,0xe4,0x71,0xea,0x7a,0x65,0xc1,0x7c,
    0xc9,0x34,0xa9,0xd7,0x91,0xe9,0x14,0x91,0xeb,0x37,0x54,0xd0,0x37,0x99,0x79,0x0f,
    0xe2,0xd3,0x08,0xd1,0x61,0x46,0xd5,0xc9,0xb0,0xd0,0xde,0xbd,0x97,0xd7,0x9c,0xe8};

static LTCore         *s_core   = NULL;
static LTSystemCrypto *s_crypto = NULL;
static JiltEngine     *s_engine;
static Tilt           *s_tilt;
static LTSystemCryptoOptions s_opts;

static void PrintData(const u8 * pRes, LT_SIZE nLen, const char * pTag, Tilt *tilt) {
    if (!pRes || !pTag) {
        return;
    }
    char h[] = "0123456789ABCDEF";
    char rs[nLen * 2 + 1];
    for (LT_SIZE i = 0; i < nLen; ++i) {
        rs[i * 2] =  h[pRes[i] >> 4];
        rs[i * 2 + 1] =  h[pRes[i] & 0xF];
    }
    rs[nLen * 2] = '\0';
    TILT_INFO(tilt, "%s %s", pTag, rs);
}

static void PrintStatus(LTSystemCryptoStatus eSt, const char * pTag, Tilt *tilt) {
    switch (eSt) {
        case kLTSystemCrypto_Status_Disabled:
            TILT_INFO(tilt, "%s %s", pTag, "Disabled");
            break;

        case kLTSystemCrypto_Status_Enabled_HW_Only:
            TILT_INFO(tilt, "%s %s", pTag, "Enabled_HW_Only");
            break;

        case kLTSystemCrypto_Status_Enabled_SW_Only:
            TILT_INFO(tilt, "%s %s", pTag, "Enabled_SW_Only");
            break;

        case kLTSystemCrypto_Status_Enabled_HW_SW:
            TILT_INFO(tilt, "%s %s", pTag, "Enabled_HW_SW");
            break;

        default :
            TILT_INFO(tilt, "%s %s", pTag, "Unknown status");
    }
}

void TestAvailability(Tilt *tilt) {
    LTSystemCryptoStatus eSt;
    s_crypto->GetStatus(kLTSystemCrypto_Method_Random, &eSt);
    PrintStatus(eSt, "Random", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SHA1, &eSt);
    PrintStatus(eSt, "SHA1", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SHA256, &eSt);
    PrintStatus(eSt, "SHA256", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_HMAC_SHA1, &eSt);
    PrintStatus(eSt, "HMAC_SHA1", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_HMAC_SHA256, &eSt);
    PrintStatus(eSt, "HMAC_SHA256", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_AES128_GCM, &eSt);
    PrintStatus(eSt, "AES128_GCM", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_AES128_CTR, &eSt);
    PrintStatus(eSt, "AES128_CTR", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_AES128_XTS, &eSt);
    PrintStatus(eSt, "AES128_XTS", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_AES128_CBC, &eSt);
    PrintStatus(eSt, "AES128_CBC", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_EdDSA, &eSt);
    PrintStatus(eSt, "EdDSA", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_ECDHE, &eSt);
    PrintStatus(eSt, "ECDHE", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SeqSHA1, &eSt);
    PrintStatus(eSt, "SeqSHA1", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SeqSHA256, &eSt);
    PrintStatus(eSt, "SeqSHA256", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SeqHMAC_SHA1, &eSt);
    PrintStatus(eSt, "SeqHMACSHA1", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_SeqHMAC_SHA256, &eSt);
    PrintStatus(eSt, "SeqHMACSHA256", tilt);
    s_crypto->GetStatus(kLTSystemCrypto_Method_ECDSA, &eSt);
    PrintStatus(eSt, "ECDSA", tilt);

    TILT_INFO(tilt, "Crypto %s", s_opts.enabled ? "enabled" : "not enabled");
}

void TestDrbg(Tilt *tilt) {
    int ret = s_crypto->GenRandomBytes(r21, sizeof(r21));
    PrintData(r21, 16, "drbg r21", tilt);
    TILT_EXPECT_TRUE(tilt, ret, "drbg ret21 %d", ret);
    ret = s_crypto->GenRandomBytes(r22, sizeof(r22));
    PrintData(r22, 16, "drbg r22", tilt);
    TILT_EXPECT_TRUE(tilt, ret, "drbg ret22 %d", ret);
}

void TestSha(Tilt *tilt) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_NoSupport;
    // sha256
    if (!s_opts.sha256) TILT_WARNING(tilt, "sha256 not supported !!!");
    else {
        ret = s_crypto->GenDigestSHA256((u8 *)"", 0, r0);
        TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA256() failed, error=%d", ret);

        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r0, 16, "sha256 r00", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e00, 32),
                            "sha256 ret00 %d", ret);
        }

        ret = s_crypto->GenDigestSHA256(d01, sizeof(d01), r0);
        if (ret == kLTSystemCrypto_Result_NoSupport) TILT_WARNING(tilt, "sha256 not supported !!!");
        else TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA256() failed, error=%d", ret);
        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r0, 16, "sha256 r01", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e01, 32),
                            "sha256 ret01 %d", ret);
        }

        ret = s_crypto->GenDigestSHA256(d02, sizeof(d02), r0);
        if (ret == kLTSystemCrypto_Result_NoSupport) TILT_WARNING(tilt, "sha256 not supported !!!");
        else TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA256() failed, error=%d", ret);
        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r0, 16, "sha256 r02", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e02, 32),
                            "sha256 ret02 %d", ret);
        }
    }

    // seq sha256
    if (!s_opts.seqSha256) TILT_WARNING(tilt, "seq sha256 not supported !!!");
    else {
        LT_SHA256_CTX * sha256Ctx = s_crypto->CreateSeqSHA256();
        TILT_EXPECT_TRUE(tilt, sha256Ctx, "CreateSeqSHA256() failed");
        if (sha256Ctx) {
            ret = s_crypto->UpdateSeqSHA256(sha256Ctx, d02, 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqSHA256() failed, error=%d", ret);
            s_crypto->UpdateSeqSHA256(sha256Ctx, d02 + 30, sizeof(d02) - 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqSHA256() failed, error=%d", ret);

            LT_SHA256_CTX * clonedSha256Ctx = s_crypto->CloneSeqSHA256(sha256Ctx);

            TILT_EXPECT_TRUE(tilt, clonedSha256Ctx, "CloneSeqSHA256() failed");
            if (clonedSha256Ctx) {
                lt_memset(r0, 0, 32);
                ret = s_crypto->FinishSeqSHA256(clonedSha256Ctx, r0);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqSHA256() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r0, 16, "seqsha256 r02", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e02, 32),
                                    "seqsha256 ret02 %d", ret);
                }
                s_crypto->DestroySeqSHA256(clonedSha256Ctx);
            }

            clonedSha256Ctx = s_crypto->CloneSeqSHA256(sha256Ctx);

            lt_memset(r0, 0, 32);
            ret = s_crypto->FinishSeqSHA256(sha256Ctx, r0);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "FinishSeqSHA256() failed, error=%d", ret);
            if (kLTSystemCrypto_Result_Ok == ret) {
                PrintData(r0, 16, "seqsha256 r02", tilt);
                TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e02, 32),
                                "seqsha256 ret02 %d", ret);
            }
            s_crypto->DestroySeqSHA256(sha256Ctx);

            TILT_EXPECT_TRUE(tilt, clonedSha256Ctx, "CloneSeqSHA256() failed");
            if (clonedSha256Ctx) {
                lt_memset(r0, 0, 32);
                ret = s_crypto->FinishSeqSHA256(clonedSha256Ctx, r0);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqSHA256() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r0, 16, "seqsha256 r02", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r0, e02, 32),
                                    "seqsha256 ret02 %d", ret);
                }
                s_crypto->DestroySeqSHA256(clonedSha256Ctx);
            }
        }
    }

    // sha1
    if (!s_opts.sha1) TILT_WARNING(tilt, "sha1 not supported !!!");
    else {
        ret = s_crypto->GenDigestSHA1((u8 *)"", 0, r5);
        TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA1() failed, error=%d", ret);
        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r5, 16, "sha1 r50", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e50, 20),
                            "sha1 ret50 %d", ret);
        }

        ret = s_crypto->GenDigestSHA1(d51, sizeof(d51), r5);
        TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA1() failed, error=%d", ret);
        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r5, 16, "sha1 r51", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e51, 20),
                            "sha1 ret51 %d", ret);
        }

        ret = s_crypto->GenDigestSHA1(d52, sizeof(d52), r5);
        TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                        "GenDigestSHA1() failed, error=%d", ret);
        if (kLTSystemCrypto_Result_Ok == ret) {
            PrintData(r5, 16, "sha1 r52", tilt);
            TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e52, 20),
                            "sha1 ret52 %d", ret);
        }
    }

    // seq sha1
    if (!s_opts.seqSha1) TILT_WARNING(tilt, "seq sha1 not supported !!!");
    else {
        LT_SHA1_CTX * sha1Ctx = s_crypto->CreateSeqSHA1();
        TILT_EXPECT_TRUE(tilt, sha1Ctx, "CreateSeqSHA1() failed");
        if (sha1Ctx) {
            ret = s_crypto->UpdateSeqSHA1(sha1Ctx, d52, 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqSHA1() failed, error=%d", ret);
            s_crypto->UpdateSeqSHA1(sha1Ctx, d52 + 30, sizeof(d52) - 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqSHA1() failed, error=%d", ret);

            LT_SHA1_CTX * clonedSha1Ctx = s_crypto->CloneSeqSHA1(sha1Ctx);

            TILT_EXPECT_TRUE(tilt, clonedSha1Ctx, "CloneSeqSHA1() failed");
            if (clonedSha1Ctx) {
                lt_memset(r5, 0, 20);
                ret = s_crypto->FinishSeqSHA1(clonedSha1Ctx, r5);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqSHA1() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r5, 16, "seqsha1 r52", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e52, 20),
                                    "seqsha1 ret52 %d", ret);
                }
                s_crypto->DestroySeqSHA1(clonedSha1Ctx);
            }

            clonedSha1Ctx = s_crypto->CloneSeqSHA1(sha1Ctx);

            lt_memset(r5, 0, 20);
            ret = s_crypto->FinishSeqSHA1(sha1Ctx, r5);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "FinishSeqSHA1() failed, error=%d", ret);
            if (kLTSystemCrypto_Result_Ok == ret) {
                PrintData(r5, 16, "seqsha1 r52", tilt);
                TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e52, 20),
                                "seqsha1 ret52 %d", ret);
            }

            TILT_EXPECT_TRUE(tilt, clonedSha1Ctx, "CloneSeqSHA1() failed");
            if (clonedSha1Ctx) {
                lt_memset(r5, 0, 20);
                ret = s_crypto->FinishSeqSHA1(clonedSha1Ctx, r5);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqSHA1() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r5, 16, "seqsha1 r52", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r5, e52, 20),
                                    "seqsha1 ret52 %d", ret);
                }
                s_crypto->DestroySeqSHA1(clonedSha1Ctx);
            }

            s_crypto->DestroySeqSHA1(sha1Ctx);
        }
    }
}

void TestHmac(Tilt *tilt) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_NoSupport;
    // hmacsha256
    if (!s_opts.hmacSha256) TILT_WARNING(tilt, "hmac-sha256 not supported !!!");
    else {
        ret = s_crypto->GenHMACSHA256(k11, sizeof(k11), (u8 *)"", 0, r1);
        PrintData(r1, 16, "hmacsha256 r10", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e10, 32), "hmacsha256 ret10 %d", ret);

        ret = s_crypto->GenHMACSHA256(k11, sizeof(k11), d11, sizeof(d11), r1);
        PrintData(r1, 16, "hmacsha256 r11", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e11, 32), "hmacsha256 ret11 %d", ret);

        ret = s_crypto->GenHMACSHA256(k12, sizeof(k12), d12, sizeof(d12), r1);
        PrintData(r1, 16, "hmacsha256 r12", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e12, 32), "hmacsha256 ret12 %d", ret);
    }

    // seq hmacsha256
    if (!s_opts.seqHmacSha256) TILT_WARNING(tilt, "seq hmac-sha256 not supported !!!");
    else {
        LT_HMAC_SHA256_CTX * hmacSha256Ctx = s_crypto->CreateSeqHMACSHA256(k12, sizeof(k12));
        TILT_EXPECT_TRUE(tilt, hmacSha256Ctx, "CreateSeqHMACSHA256() failed");
        if (hmacSha256Ctx) {
            ret = s_crypto->UpdateSeqHMACSHA256(hmacSha256Ctx, d12, 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqHMACSHA256() failed, error=%d", ret);
            ret = s_crypto->UpdateSeqHMACSHA256(hmacSha256Ctx, d12 + 30, sizeof(d12) - 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqHMACSHA256() failed, error=%d", ret);

            LT_HMAC_SHA256_CTX * clonedHmacSha256Ctx = s_crypto->CloneSeqHMACSHA256(hmacSha256Ctx);

            TILT_EXPECT_TRUE(tilt, clonedHmacSha256Ctx, "CloneSeqHMACSHA256() failed");
            if (clonedHmacSha256Ctx) {
                lt_memset(r1, 0, 32);
                ret = s_crypto->FinishSeqHMACSHA256(clonedHmacSha256Ctx, r1);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqHMACSHA256() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r1, 16, "seqhmacsha256 r12", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e12, 32), "seqhmacsha256 ret12 %d", ret);
                }
                s_crypto->DestroySeqHMACSHA256(clonedHmacSha256Ctx);
            }

            clonedHmacSha256Ctx = s_crypto->CloneSeqHMACSHA256(hmacSha256Ctx);

            lt_memset(r1, 0, 32);
            ret = s_crypto->FinishSeqHMACSHA256(hmacSha256Ctx, r1);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "FinishSeqHMACSHA256() failed, error=%d", ret);
            if (kLTSystemCrypto_Result_Ok == ret) {
                PrintData(r1, 16, "seqhmacsha256 r12", tilt);
                TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e12, 32), "seqhmacsha256 ret12 %d", ret);
            }
            s_crypto->DestroySeqHMACSHA256(hmacSha256Ctx);

            TILT_EXPECT_TRUE(tilt, clonedHmacSha256Ctx, "CloneSeqHMACSHA256() failed");
            if (clonedHmacSha256Ctx) {
                lt_memset(r1, 0, 32);
                ret = s_crypto->FinishSeqHMACSHA256(clonedHmacSha256Ctx, r1);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqHMACSHA256() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r1, 16, "seqhmacsha256 r12", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r1, e12, 32), "seqhmacsha256 ret12 %d", ret);
                }
                s_crypto->DestroySeqHMACSHA256(clonedHmacSha256Ctx);
            }
        }
    }

    // hmacsha1
    if (!s_opts.hmacSha1) TILT_WARNING(tilt, "hmac-sha1 not supported !!!");
    else {
        ret = s_crypto->GenHMACSHA1(k61, sizeof(k61), (u8 *)"", 0, r6);
        PrintData(r6, 16, "hmacsha1 r60", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e60, 20), "hmacsha1 ret60 %d", ret);

        ret = s_crypto->GenHMACSHA1(k61, sizeof(k61), d61, sizeof(d61), r6);
        PrintData(r6, 16, "hmacsha1 r61", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e61, 20), "hmacsha1 ret61 %d", ret);

        ret = s_crypto->GenHMACSHA1(k62, sizeof(k62), d62, sizeof(d62), r6);
        PrintData(r6, 16, "hmacsha1 r62", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e62, 20), "hmacsha1 ret62 %d", ret);
    }

    // seq hmacsha1
    if (!s_opts.seqHmacSha1) TILT_WARNING(tilt, "seq hmac-sha1 not supported !!!");
    else {
        LT_HMAC_SHA1_CTX * hmacSha1Ctx = s_crypto->CreateSeqHMACSHA1(k62, sizeof(k62));
        TILT_EXPECT_TRUE(tilt, hmacSha1Ctx, "CreateSeqHMACSHA1() failed");
        if (hmacSha1Ctx) {
            ret = s_crypto->UpdateSeqHMACSHA1(hmacSha1Ctx, d62, 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqHMACSHA1() failed, error=%d", ret);
            ret = s_crypto->UpdateSeqHMACSHA1(hmacSha1Ctx, d62 + 30, sizeof(d62) - 30);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "UpdateSeqHMACSHA1() failed, error=%d", ret);

            LT_HMAC_SHA1_CTX * clonedHmacSha1Ctx = s_crypto->CloneSeqHMACSHA1(hmacSha1Ctx);

            TILT_EXPECT_TRUE(tilt, clonedHmacSha1Ctx, "CloneSeqHMACSHA1() failed");
            if (clonedHmacSha1Ctx) {
                lt_memset(r6, 0, 20);
                ret = s_crypto->FinishSeqHMACSHA1(clonedHmacSha1Ctx, r6);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqHMACSHA1() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r6, 16, "seqhmacsha1 r62", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e62, 20), "seqhmacsha1 ret62 %d", ret);
                }
                s_crypto->DestroySeqHMACSHA1(clonedHmacSha1Ctx);
            }

            clonedHmacSha1Ctx = s_crypto->CloneSeqHMACSHA1(hmacSha1Ctx);

            lt_memset(r6, 0, 20);
            ret = s_crypto->FinishSeqHMACSHA1(hmacSha1Ctx, r6);
            TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                            "FinishSeqHMACSHA1() failed, error=%d", ret);
            if (kLTSystemCrypto_Result_Ok == ret) {
                PrintData(r6, 16, "seqhmacsha1 r62", tilt);
                TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e62, 20), "seqhmacsha1 ret62 %d", ret);
            }

            TILT_EXPECT_TRUE(tilt, clonedHmacSha1Ctx, "CloneSeqHMACSHA1() failed");
            if (clonedHmacSha1Ctx) {
                lt_memset(r6, 0, 20);
                ret = s_crypto->FinishSeqHMACSHA1(clonedHmacSha1Ctx, r6);
                TILT_EXPECT_TRUE(tilt, ret == kLTSystemCrypto_Result_Ok,
                                "FinishSeqHMACSHA1() failed, error=%d", ret);
                if (kLTSystemCrypto_Result_Ok == ret) {
                    PrintData(r6, 16, "seqhmacsha1 r62", tilt);
                    TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(r6, e62, 20), "seqhmacsha1 ret62 %d", ret);
                }
                s_crypto->DestroySeqHMACSHA1(clonedHmacSha1Ctx);
            }

            s_crypto->DestroySeqHMACSHA1(hmacSha1Ctx);
        }
    }
}

void TestAes(Tilt *tilt) {
    LTSystemCryptoResult ret = kLTSystemCrypto_Result_NoSupport;
    // aes gcm
    if (!s_opts.aes128Gcm) TILT_WARNING(tilt, "aes128-gcm not supported !!!");
    else {
        LT_UNUSED(et30);
        ret = s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, (u8 *)"", 0, d, e, 16);
        PrintData(d, 16, "gcm enc ctext30", tilt);
        PrintData(e, 16, "gcm enc tag30", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, et30, 16), "gcm enc ret30 %d", ret);

        ret = s_crypto->DecryptAES128GCM(k31, iv31, NULL, 0, (u8 *)"", 0, et30, 16, d);
        PrintData(d, 16, "gcm dec ptext30", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == ret, "gcm dec ret30 %d", ret);

        ret = s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, p31, sizeof(p31), d, e, tl31);
        PrintData(d, 16, "gcm enc ctext31", tilt);
        PrintData(e, tl31, "gcm enc tag31", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, ec31, sizeof(p31)) && 0 == lt_memcmp(e, et31, tl31), "gcm enc ret31 %d", ret);

        ret = s_crypto->DecryptAES128GCM(k31, iv31, NULL, 0, d, sizeof(p31), e, tl31, d);
        PrintData(d, 16, "gcm dec ptext31", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(d, p31, sizeof(p31)), "gcm dec ret31 %d", ret);

        ret = s_crypto->EncryptAES128GCM(k31, iv31, a32, al32, p32, sizeof(p32), d, e, tl32);
        PrintData(d, 16, "gcm enc ctext32", tilt);
        PrintData(e, tl32, "gcm enc tag32", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, ec32, sizeof(p32)) && 0 == lt_memcmp(e, et32, tl32), "gcm enc ret32 %d", ret);

        ret = s_crypto->DecryptAES128GCM(k31, iv31, a32, al32, d, sizeof(p32), e, tl32, d);
        PrintData(d, 16, "gcm dec ptext32", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(d, p32, sizeof(p32)), "gcm dec ret32 %d", ret);

        ret = s_crypto->EncryptAES128GCM(k33, iv33, a33, sizeof(a33), p33, sizeof(p33), d, e, 16);
        PrintData(d, 16, "gcm enc ctext33", tilt);
        PrintData(e, 16, "gcm enc tag33", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, ec33, sizeof(p33)) && 0 == lt_memcmp(e, et33, 16), "gcm enc ret33 %d", ret);

        ret = s_crypto->DecryptAES128GCM(k33, iv33, a33, sizeof(a33), ec33, sizeof(p33), et33, 16, d);
        PrintData(d, 16, "gcm dec ptext33", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(d, p33, sizeof(p33)), "gcm dec ret33 %d", ret);

        ret = s_crypto->EncryptAES128GCM(k34, iv34, a34, sizeof(a34), p34, sizeof(p34), d, e, 16);
        PrintData(d, 16, "gcm enc ctext34", tilt);
        PrintData(e, 16, "gcm enc tag34", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, ec34, sizeof(p34)) && 0 == lt_memcmp(e, et34, 16), "gcm enc ret34 %d", ret);

        ret = s_crypto->DecryptAES128GCM(k34, iv34, a34, sizeof(a34), d, sizeof(p34), e, 16, d);
        PrintData(d, 16, "gcm dec ptext34", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(d, p34, sizeof(p34)), "gcm dec ret34 %d", ret);
    }

    // aes ctr
    if (!s_opts.aes128Ctr) TILT_WARNING(tilt, "aes128-ctr not supported !!!");
    else {
        ret = s_crypto->EncryptAES128CTR(k71, iv71, p71, sizeof(p71), e);
        PrintData(e, 16, "ctr enc ctext71", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec71, sizeof(p71)), "ctr enc ret71 %d", ret);

        ret = s_crypto->DecryptAES128CTR(k71, iv71, e, sizeof(p71), d);
        PrintData(d, 16, "ctr dec ptext71", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p71, sizeof(p71)), "ctr dec ret71 %d", ret);
    }

    // aes xts
    if (!s_opts.aes128Xts) TILT_WARNING(tilt, "aes128-xts not supported !!!");
    else {
        ret = s_crypto->EncryptAES128XTS(k1_72, k2_72, iv72, p72, sizeof(p72), e);
        PrintData(e, sizeof(p72), "xts enc ctext72", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec72, sizeof(p72)), "xts enc ret72 %d", ret);

        ret = s_crypto->DecryptAES128XTS(k1_72, k2_72, iv72, e, sizeof(p72), d);
        PrintData(d, sizeof(p72), "xts dec ptext72", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p72, sizeof(p72)), "xts dec ret72 %d", ret);

        ret = s_crypto->EncryptAES128XTS(k1_73, k2_73, iv73, p73, sizeof(p73), e);
        PrintData(e, sizeof(p73), "xts enc ctext73", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec73, sizeof(p73)), "xts enc ret73 %d", ret);

        ret = s_crypto->DecryptAES128XTS(k1_73, k2_73, iv73, e, sizeof(p73), d);
        PrintData(d, sizeof(p73), "xts dec ptext73", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p73, sizeof(p73)), "xts dec ret73 %d", ret);

        ret = s_crypto->EncryptAES128XTS(k1_74, k2_74, iv74, p74, sizeof(p74), e);
        PrintData(e, sizeof(p74), "xts enc ctext74", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec74, sizeof(p74)), "xts enc ret74 %d", ret);

        ret = s_crypto->DecryptAES128XTS(k1_74, k2_74, iv74, e, sizeof(p74), d);
        PrintData(d, sizeof(p74), "xts dec ptext74", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p74, sizeof(p74)), "xts dec ret74 %d", ret);

        ret = s_crypto->EncryptAES128XTS(k1_75, k2_75, iv75, p75, sizeof(p75), e);
        PrintData(e, sizeof(p75), "xts enc ctext75", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec75, sizeof(p75)), "xts enc ret75 %d", ret);

        ret = s_crypto->DecryptAES128XTS(k1_75, k2_75, iv75, e, sizeof(p75), d);
        PrintData(d, sizeof(p75), "xts dec ptext75", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p75, sizeof(p75)), "xts dec ret75 %d", ret);

        ret = s_crypto->EncryptAES128XTS(k1_76, k2_76, iv76, p76, sizeof(p76), e);
        PrintData(e, sizeof(p76), "xts enc ctext76", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec76, sizeof(p76)), "xts enc ret76 %d", ret);

        ret = s_crypto->DecryptAES128XTS(k1_76, k2_76, iv76, e, sizeof(p76), d);
        PrintData(d, sizeof(p76), "xts dec ptext76", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p76, sizeof(p76)), "xts dec ret76 %d", ret);
    }

    // aes cbc
    if (!s_opts.aes128Cbc) TILT_WARNING(tilt, "aes128-cbc not supported !!!");
    else {
        ret = s_crypto->EncryptAES128CBC(k77, iv77, p77, sizeof(p77), e);
        PrintData(e, sizeof(p77), "cbc enc ctext77", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec77, sizeof(p77)), "cbc enc ret77 %d", ret);

        ret = s_crypto->DecryptAES128CBC(k77, iv77, e, sizeof(p77), d);
        PrintData(d, sizeof(p77), "cbc dec ptext77", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p77, sizeof(p77)), "cbc dec ret77 %d", ret);

        ret = s_crypto->EncryptAES128CBC(k78, iv78, p78, sizeof(p78), e);
        PrintData(e, sizeof(p78), "cbc enc ctext78", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(e, ec78, sizeof(p78)), "cbc enc ret78 %d", ret);

        ret = s_crypto->DecryptAES128CBC(k78, iv78, e, sizeof(p78), d);
        PrintData(d, sizeof(p78), "cbc dec ptext78", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(d, p78, sizeof(p78)), "cbc dec ret78 %d", ret);
    }
}

void TestEddsa(Tilt *tilt) {
    if (!s_opts.eddsa) TILT_WARNING(tilt, "ed25519 not supported !!!");
    else {
        LTTime t1 = s_core->GetKernelTime();
        int ret = s_crypto->GenEddsaPublicKey(priKey, k42);
        LTTime t2 = s_core->GetKernelTime();
        PrintData(k42, 16, "eddsa pubkey", tilt);
        TILT_INFO(tilt, "eddsa gen key %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(k42, ek42, 32), "eddsa genkey ret40 %d", ret);
        lt_memset(k42, 0, EdDSA_KEY_LENGTH);
        t1 = s_core->GetKernelTime();
        ret = s_crypto->SignEddsa(priKey, d42, 64, r42, k42);
        t2 = s_core->GetKernelTime();
        PrintData(r42, 16, "eddsa sign", tilt);
        PrintData(k42, 16, "eddsa pubkey", tilt);
        TILT_INFO(tilt, "eddsa sign %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(r42, er42, 64) && 0 == lt_memcmp(k42, ek42, 32), "eddsa sign ret41 %d", ret);
        t1 = s_core->GetKernelTime();
        ret = s_crypto->VerifyEddsa(d42, 64, r42, ek42);
        t2 = s_core->GetKernelTime();
        TILT_INFO(tilt, "eddsa verify %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret, "eddsa verify ret42 %d", ret);
    }
}

void TestEcdhe(Tilt *tilt) {
    if (!s_opts.ecdhe) TILT_WARNING(tilt, "x25519 not supported !!!");
    else {
        LTTime t1 = s_core->GetKernelTime();
        int ret = s_crypto->GenKeyEcdhe(a, (const u8 *)s_crypto->GetCryptoConsts()->kX25519_BP, o);
        LTTime t2 = s_core->GetKernelTime();
        PrintData(o, 16, "ecdhe A", tilt);
        TILT_INFO(tilt, "ecdhe A %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(A, o, 32), "ecdhe A ret %d", ret);

        t1 = s_core->GetKernelTime();
        s_crypto->GenKeyEcdhe(b, (const u8 *)s_crypto->GetCryptoConsts()->kX25519_BP, o);
        t2 = s_core->GetKernelTime();
        PrintData(o, 16, "ecdhe B", tilt);
        TILT_INFO(tilt, "ecdhe B %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(B, o, 32), "ecdhe B ret %d", ret);

        s_crypto->GenKeyEcdhe(a, B, o);
        PrintData(o, 16, "ecdhe Ka", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(K, o, 32), "ecdhe Ka ret %d", ret);

        s_crypto->GenKeyEcdhe(b, A, o);
        PrintData(o, 16, "ecdhe Kb", tilt);
        TILT_EXPECT_TRUE(tilt, 0 == lt_memcmp(K, o, 32), "ecdhe Kb ret %d", ret);
    }
}

void TestEcdsa(Tilt *tilt) {
    if (!s_opts.ecdsa) TILT_WARNING(tilt, "ecdsa-p256 not supported !!!");
    else {
        LTTime t1 = s_core->GetKernelTime();
        int ret = s_crypto->GenEcdsaPublicKey(pk91, k91);
        LTTime t2 = s_core->GetKernelTime();
        PrintData(k91, 16, "ecdsa pubkey", tilt);
        TILT_INFO(tilt, "ecdsa gen key %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret && 0 == lt_memcmp(k91, ek91, 64), "ecdsa genkey ret91 %d", ret);

        t1 = s_core->GetKernelTime();
        ret = s_crypto->SignEcdsa(pk91, d91, sizeof(d91), s91, NULL);
        t2 = s_core->GetKernelTime();
        PrintData(s91, 16, "ecdsa sign data", tilt);
        TILT_INFO(tilt, "ecdsa sign data %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        ret = s_crypto->VerifyEcdsa(d91, sizeof(d91), s91, k91);
        TILT_EXPECT_TRUE(tilt, 0 == ret, "ecdsa verify s91 ret %d", ret);
        t1 = s_core->GetKernelTime();
        ret = s_crypto->VerifyEcdsa(d91, sizeof(d91), es91, ek91);
        t2 = s_core->GetKernelTime();
        TILT_INFO(tilt, "ecdsa verify data %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret, "ecdsa verify es91 ret %d", ret);

        s_crypto->GenDigestSHA256(d91, sizeof(d91), e);
        t1 = s_core->GetKernelTime();
        s_crypto->SignEcdsaHash(pk91, e, s91, NULL);
        t2 = s_core->GetKernelTime();
        PrintData(s91, 16, "ecdsa sign hash", tilt);
        TILT_INFO(tilt, "ecdsa sign hash %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        t1 = s_core->GetKernelTime();
        ret = s_crypto->VerifyEcdsaHash(e, s91, k91);
        t2 = s_core->GetKernelTime();
        TILT_INFO(tilt, "ecdsa verify hash %lld us", LT_Ps64(LTTime_GetMicroseconds(LTTime_Subtract(t2, t1))));
        TILT_EXPECT_TRUE(tilt, 0 == ret, "ecdsa verify hash ret %d", ret);
    }
}

void TestSpeed(Tilt *tilt) {
    int i, len;
    int round = 100;
    s64 dt;
    LTTime t1, t2;

    // sha256
    if (s_opts.sha256) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA256((u8 *)d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA256((u8 *)d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA256((u8 *)d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1536;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA256((u8 *)d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));
    }

    // hmacsha256
    if (s_opts.hmacSha256) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA256(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA256(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA256(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1536;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA256(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha256 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));
    }

    // aes gcm
    if (s_opts.aes128Gcm) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, d, len, d, e, 16);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "gcm %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, d, len, d, e, 16);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "gcm %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        int shortround = 20;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < shortround; i++) {
            s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, d, len, d, e, 16);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "gcm %d : %lld us, %lld KBps", len, LT_Ps64(dt / shortround), LT_Ps64(len * 1000 * shortround / dt));

        len = 1536;
        shortround = 5;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < shortround; i++) {
            s_crypto->EncryptAES128GCM(k31, iv31, NULL, 0, d, len, d, e, 16);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "gcm %d : %lld us, %lld KBps", len, LT_Ps64(dt / shortround), LT_Ps64(len * 1000 * shortround / dt));
    }

    // sha1
    if (s_opts.sha1) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA1(d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA1(d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA1(d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1536;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenDigestSHA1(d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "sha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));
    }

    // hmacsha1
    if (s_opts.hmacSha1) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA1(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA1(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA1(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1536;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->GenHMACSHA1(k11, sizeof(k11), d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "hmacsha1 %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));
    }

    // aes128 ctr
    if (s_opts.aes128Ctr) {
        len = 256;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->EncryptAES128CTR(k71, iv71, d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "ctr %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 512;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < round; i++) {
            s_crypto->EncryptAES128CTR(k71, iv71, d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "ctr %d : %lld us, %lld KBps", len, LT_Ps64(dt / round), LT_Ps64(len * 1000 * round / dt));

        len = 1024;
        int shortround = 20;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < shortround; i++) {
            s_crypto->EncryptAES128CTR(k71, iv71, d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "ctr %d : %lld us, %lld KBps", len, LT_Ps64(dt / shortround), LT_Ps64(len * 1000 * shortround / dt));

        len = 1536;
        shortround = 5;
        t1 = s_core->GetKernelTime();
        for (i = 0; i < shortround; i++) {
            s_crypto->EncryptAES128CTR(k71, iv71, d, len, d);
        }
        t2 = s_core->GetKernelTime();
        dt = LTTime_GetMicroseconds(LTTime_Subtract(t2, t1));
        TILT_INFO(tilt, "ctr %d : %lld us, %lld KBps", len, LT_Ps64(dt / shortround), LT_Ps64(len * 1000 * shortround / dt));
    }
}

#define RUNTIME (2000)

static void RunEcdsa(void *clientData) {
    s32 start = LTTime_GetMilliseconds(s_core->GetKernelTime());
    s32 now = start;
    bool logged = false;
    u32 count = 0;
    bool status = true;
    while (now - start < RUNTIME) {
        lt_memset(s91, 0, 64);
        LTSystemCryptoResult ret = s_crypto->SignEcdsa(pk91, d91, sizeof(d91), s91, NULL);
        status &= (ret == kLTSystemCrypto_Result_Ok);
        ret = s_crypto->VerifyEcdsa(d91, sizeof(d91), s91, ek91);
        status &= (ret == kLTSystemCrypto_Result_Ok);
        ++count;
        now = LTTime_GetMilliseconds(s_core->GetKernelTime());
        if (now - start >= 1000 && !logged) {
            logged = true;
            TILT_DEBUG(s_tilt, "%lu over %ld ms", LT_Pu32(count), LT_Ps32(now - start));
        }
    }
    *(bool *)clientData = status;
    LTOThread *th = s_core->GetCurrentThreadObject();
    th->API->Terminate(th);
}

static void RunAes(void *clientData) {
    s32 start = LTTime_GetMilliseconds(s_core->GetKernelTime());
    s32 now = start;
    bool logged = false;
    u32 count = 0;
    bool status = true;
    while (now - start < RUNTIME) {
        lt_memset(d, 0, sizeof(p34));
        lt_memset(e, 0, 16);
        LTSystemCryptoResult ret = s_crypto->EncryptAES128GCM(k34, iv34, a34, sizeof(a34), p34, sizeof(p34), d, e, 16);
        status &= (ret == kLTSystemCrypto_Result_Ok) && (0 == lt_memcmp(d, ec34, sizeof(p34))) && (0 == lt_memcmp(e, et34, 16));
        ++count;
        now = LTTime_GetMilliseconds(s_core->GetKernelTime());
        if (now - start >= 1000 && !logged) {
            logged = true;
            TILT_DEBUG(s_tilt, "%lu over %ld ms", LT_Pu32(count), LT_Ps32(now - start));
        }
    }
    *(bool *)clientData = status;
    LTOThread *th = s_core->GetCurrentThreadObject();
    th->API->Terminate(th);
}

static void RunSha(void *clientData) {
    s32 start = LTTime_GetMilliseconds(s_core->GetKernelTime());
    s32 now = start;
    bool logged = false;
    u32 count = 0;
    bool status = true;
    while (now - start < RUNTIME) {
        lt_memset(r0, 0, 32);
        LTSystemCryptoResult ret = s_crypto->GenDigestSHA256(d02, sizeof(d02), r0);
        status &= (ret == kLTSystemCrypto_Result_Ok) && (0 == lt_memcmp(r0, e02, 32));
        ++count;
        now = LTTime_GetMilliseconds(s_core->GetKernelTime());
        if (now - start >= 1000 && !logged) {
            logged = true;
            TILT_DEBUG(s_tilt, "%lu over %ld ms", LT_Pu32(count), LT_Ps32(now - start));
        }
    }
    *(bool *)clientData = status;
    LTOThread *th = s_core->GetCurrentThreadObject();
    th->API->Terminate(th);
}

void TestConcurrency(Tilt *tilt) {
    LTOThread *eccThread = NULL;
    LTOThread *aesThread = NULL;
    LTOThread *shaThread = NULL;
    bool eccStatus = true;
    bool aesStatus = true;
    bool shaStatus = true;

    if (s_opts.ecdsa) {
        eccStatus = false;
        eccThread = lt_createobject(LTOThread);
        eccThread->API->SetStackSize(eccThread, 1024);
        eccThread->API->Start(eccThread, "ecdsa", NULL, NULL);
        eccThread->API->QueueTaskProc(eccThread, RunEcdsa, NULL, &eccStatus);
    }

    if (s_opts.aes128Gcm) {
        aesStatus = false;
        aesThread = lt_createobject(LTOThread);
        aesThread->API->SetStackSize(aesThread, 1024);
        aesThread->API->Start(aesThread, "aes", NULL, NULL);
        aesThread->API->QueueTaskProc(aesThread, RunAes, NULL, &aesStatus);
    }

    if (s_opts.sha256) {
        shaStatus = false;
        shaThread = lt_createobject(LTOThread);
        shaThread->API->SetStackSize(shaThread, 1024);
        shaThread->API->Start(shaThread, "sha", NULL, NULL);
        shaThread->API->QueueTaskProc(shaThread, RunSha, NULL, &shaStatus);
    }

    if (eccThread) {
        eccThread->API->WaitUntilFinished(eccThread, LTTime_Seconds(20));
        lt_destroyobject(eccThread);
    }
    if (aesThread) {
        aesThread->API->WaitUntilFinished(aesThread, LTTime_Seconds(20));
        lt_destroyobject(aesThread);
    }
    if (shaThread) {
        shaThread->API->WaitUntilFinished(shaThread, LTTime_Seconds(20));
        lt_destroyobject(shaThread);
    }

    TILT_EXPECT_TRUE(tilt, eccStatus, "ecdsa wrong");
    TILT_EXPECT_TRUE(tilt, aesStatus, "aes wrong");
    TILT_EXPECT_TRUE(tilt, shaStatus, "sha wrong");
}

static void BeforeAllTests(Tilt *tilt) {
    s_tilt     = tilt;
    s_core    = LT_GetCore();
    s_crypto = lt_openlibrary(LTSystemCrypto);
    TILT_ASSERT_TRUE(tilt, s_crypto, "failed to open crypto library");
    s_crypto->GetOptions(&s_opts);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    s_core->CloseLibrary((LTLibrary *)s_crypto);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestAvailability, "Availability", "Test HW/SW availability",            0 },
    { TestDrbg,         "DRBG",         "Test DRBG correctness",              0 },
    { TestSha,          "SHA",          "Test SHA correctness",               0 },
    { TestHmac,         "HMAC",         "Test HMAC correctness",              0 },
    { TestAes,          "AES",          "Test AES correctness",               0 },
    { TestEddsa,        "ED25519",      "Test ED25519 correctness and speed", 0 },
    { TestEcdhe,        "X25519",       "Test X25519 correctness and speed",  0 },
    { TestEcdsa,        "ECDSA-P256",   "Test ECDSA correctness and speed",   0 },
    { TestSpeed,        "Speed",        "Benchmark crypto speed",             0 },
    { TestConcurrency,  "Concurrency",  "Test concurrent crypto threads",     0 },
};

static int UnitTestLTSystemCryptoImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemCryptoImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemCryptoImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemCrypto, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemCrypto, UnitTestLTSystemCryptoImpl_Run, 1536) LTLIBRARY_DEFINITION;

