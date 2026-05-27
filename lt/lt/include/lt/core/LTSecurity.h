/******************************************************************************
 * LTSecurity.h                                                 LTCore Security
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTSECURITY_H
#define ROKU_LT_INCLUDE_LT_CORE_LTSECURITY_H

enum {
    kLTSecurity_Magic_LTAT      = 0x5441544c,       /**< LTAT Magic number */
    kLTSecurity_DeviceIDLength  = 12,               /**< Length of Device ID field */
};

/**********************************************************
 * LT Authentication Token (LTAT) Claim Groups and Masks */
typedef u8 LTSecurityClaimGroup;
enum {
    kLTSecurityClaimGroup1 = 0,                     /**< Bootloader claims */
    kLTSecurityClaimGroup2 = 1,                     /**< Application claims */
    kLTSecurityClaimGroup3 = 2,                     /**< Reserved */
    kLTSecurityClaimGroup4 = 3,                     /**< Reserved */
};

typedef u32 LTSecurityClaimMask;
enum {
    /* Group 1 claims normally reserved for Bootloader (group numbering starts at 1) */
    kLTSecurityClaimGroup1_BypassAppSignature  = 0x1,    /**< Bypass signature check */
    kLTSecurityClaimGroup1_EnableConsoleOutput = 0x2,    /**< Enable developer console output */
    kLTSecurityClaimGroup1_EnableConsoleInput  = 0x4,    /**< Enable developer console input */

    /* Group 2 claims normally reserved for LT Application */
    kLTSecurityClaimGroup2_AuthorizeTestServer = 0x1,    /**< Authorize test server use (cloud bypass) */
    kLTSecurityClaimGroup2_EnableConsoleOutput = 0x2,    /**< Enable developer console output */
    kLTSecurityClaimGroup2_EnableConsoleInput  = 0x4,    /**< Enable developer console input */

    /* Group 3 and 4 claims are reserved for future use */
};

/***********************************
 * LT Authentication Token (LTAT) */
typedef struct __attribute__((packed)) {
    u32                  nMagic;                         /**< Magic number, set to kLTSecurity_Magic_LTAT */
    u16                  nVersion;                       /**< Version number (for issuer use only) */
    u32                  timeStamp;                      /**< Time of issue */
    u8                   rsvd[42];                       /**< For future use */
    u8                   id[kLTSecurity_DeviceIDLength]; /**< Device identity */
    LTSecurityClaimMask  mask[4];                        /**< Security claim group bitmasks */
} LTSecurityLTATHeader;

typedef struct __attribute__((packed)) {
    LTSecurityLTATHeader hdr;                       /**< Header */
    u8                   data[128];                 /**< For future use */
    u8                   unalloc[240];              /**< For future use */
    u8                   sig[0];                    /**< Platform-dependent signature follows */
} LTSecurityLTATPayload;

LT_STATIC_ASSERT_SIZE_32_64(LTSecurityLTATPayload, 448, 448)

/**************************************
 * Cryptographic Security Operations */
enum LTSecurityOperationType {
    kLTSecurityOperationType_Reserved       = 0,    /**< Reserved */
    kLTSecurityOperationType_CheckPassword,         /**< Password checking */
    kLTSecurityOperationType_DeriveKey,             /**< Key derivation */
    kLTSecurityOperationType_GenerateHash   = 100,  /**< Generate hash */
    kLTSecurityOperationType_CheckHash,             /**< Check hash */
    kLTSecurityOperationType_Encrypt        = 110,  /**< Encryption */
    kLTSecurityOperationType_Decrypt,               /**< Decryption */
    kLTSecurityOperationType_Sign           = 120,  /**< Signing */
    kLTSecurityOperationType_CheckSignature         /**< Signature checking */
};
typedef u8 LTSecurityOperation;

/**************************************
 * Cryptographic Security Algorithms */
enum LTSecurityAlgorithm {
    kLTSecurityAlgorithm_Reserved           = 0,    /**< Reserved */
    kLTSecurityAlgorithm_Password,                  /**< Device password related algorithm */
    kLTSecurityAlgorithm_KeyDerivation,             /**< Key derivation related algorithm */
    kLTSecurityAlgorithm_SHA256             = 10,   /**< SHA-2 256-bit hash */
    kLTSecurityAlgorithm_AES128_ECB         = 30,   /**< AES128 using ECB (Electronic CodeBook) mode */
    kLTSecurityAlgorithm_AES128_CBC,                /**< AES128 using CBC (Cipher Block Chaining) mode */
    kLTSecurityAlgorithm_Ed25519            = 50    /**< Ed25519 asymmetric cryptography using Edwards curve */

};
typedef u8 LTSecurityAlgorithm;

/******************************
 * API Status (Return Codes) */
typedef enum LTSecurityStatus {
    kLTSecurityStatus_Success               = 0xba5eba11,  /**< Operation successful, check (if applicable) passed. */
    kLTSecurityStatus_CheckFail             = 0xdead5add,  /**< Signature or password check failed. */
    kLTSecurityStatus_Fail                  = 0xdeaddead   /**< Security operation failure. */

} LTSecurityStatus;

/*********************************
 * Password Permission Bit Mask */
typedef enum LTSecurityPasswordMask {
    /* Reserved for LT */
    kLTSecurityPasswordMask_None            = 0,        /**< No special permissions */
    kLTSecurityPasswordMask_ChangeState     = 1 << 0,   /**< General permission to change state and settings via console */
    kLTSecurityPasswordMask_ChangeBootState = 1 << 1,   /**< Extra permission to change OTA update and boot state */
    kLTSecurityPasswordMask_Reserved1       = 1 << 2,   /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved2       = 1 << 3,   /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved3       = 1 << 4,   /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved4       = 1 << 5,   /**< Reserved for future use */
    kLTSecurityPasswordMask_ReadSystemRegs  = 1 << 6,   /**< Permission to read non-secure system registers */
    kLTSecurityPasswordMask_WriteSystemRegs = 1 << 7,   /**< Permission to write non-secure system registers */
    kLTSecurityPasswordMask_Reserved5       = 1 << 8,   /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved6       = 1 << 9,   /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved7       = 1 << 10,  /**< Reserved for future use */
    kLTSecurityPasswordMask_Reserved8       = 1 << 11,  /**< Reserved for future use */

    /* Application-Specific */
    kLTSecurityPasswordMask_Application0    = 1 << 20,  /**< Application-specific permission 0 */
    kLTSecurityPasswordMask_Application1    = 1 << 21,  /**< Application-specific permission 1 */
    kLTSecurityPasswordMask_Application2    = 1 << 22,  /**< Application-specific permission 2 */
    kLTSecurityPasswordMask_Application3    = 1 << 23   /**< Application-specific permission 3 */

} LTSecurityPasswordMask;

/*********************
 * Crypto Operation */
typedef struct LTSecurity_CryptoOperation {
    /* Algorithm and Operation */
    LTSecurityOperation  nType;        /**< Type of operation to perform (LTSecurityOperationType) */
    LTSecurityAlgorithm  nAlgorithm;   /**< Algorithm to use (LTSecurityAlgorithm) */

    /* nKey, nIV and nSeed are indexes to values internal to the Security Container. */
    u8 nKeyIndex;         /**< Index of key to use (if applicable) */
    u8 nIVIndex;          /**< Index of Initialization Vector to use (if applicable) */
    u8 nSeedIndex;        /**< Index of seed to use (if applicable). */

    /* nSeed can be used for protocol versioning (e.g.: key invalidation) */

} LTSecurity_CryptoOperation;

/********************************************
 * Completion callback (e.g: DMA complete) */
typedef LT_ISR_SAFE void (LTSecurity_CompletionFunc)(u32 nChannel);

/**********************************
 * Security Container Descriptor */
typedef struct LTSecurity_Info {
    u16  nAPIVersion;                               /**< security container API version */
    u16  nVersion;                                  /**< security container version */
    u16  nNumSecureContexts;                        /**< number of available secure application contexts */
    u16  nNumCryptoChannels;                        /**< number of crypto channel instances */

    /* API Functions
     *   1. Set to NULL if not implemented.
     *   2. At most four 32 bit arguments and one 32-bit return value */
    void (* GetRandomBytes)(u8 * pBytesToFill, u32 nNumBytes);
                                                    /**< get cryptographically secure random numbers */
    LTSecurityStatus
        (* SetOTPValue)(const u8 * pValue, u32 nOffsetToWrite, u32 nNumBytes);
                                                    /**< write value to OTP (e.g.: manufacturing) */

    /* API Functions requiring Crypto channels */

    LTSecurityStatus (* CheckOTP)(u32 nChannel);    /**< check OTP integrity (e.g.: manufacturing) */
    LTSecurityStatus
        (* SetCryptoOperation)(u32 nChannel, const LTSecurity_CryptoOperation * pOperation, const u8 * pIV,
                                   LT_SIZE nNumIVBytes);      /**< set crypto operation on channel */
        /* If pIV is NULL for operations requiring an Initialization Vector, then an internal IV
         *   is chosen using the LTSecurity_CryptoOperation nIV index. */

    LT_ISR_SAFE LTSecurityStatus
        (* SetCryptoDMA)(u32 nChannel, LTSecurity_CompletionFunc * pCompletionFunc);
                                                    /**< set DMA mode and completion callback */
    LTSecurityStatus
        (* RunCryptoOperation)(u32 nChannel, u8 * pOutput, const u8 * pInput, LT_SIZE nNumBytes);
                                                    /**< run crypto operation on channel */
    LTSecurityStatus
        (* CheckPassword)(u32 nChannel, u8 * pOutput, const u8 * pInput, LT_SIZE nNumBytes);
                                                    /**< run crypto operation on channel */
} LTSecurity_Info;

#endif // #ifndef ROKU_LT_INCLUDE_LT_CORE_LTSECURITY_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Nov-21   tiberius    created
 */
