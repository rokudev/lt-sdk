/*******************************************************************************
 * <lt/utility/LTUtilityByteOps.h>                  String operation utilities
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltutility_byteops LTUtilityByteOps
 * @ingroup ltutility
 * @{
 *
 * @brief A library of string utilities.
 *
 * LTUtilityByteOps provides commonly useful string transformations such as
 * base64 encode/decode.
 */

#ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYBYTEOPS_H
#define ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYBYTEOPS_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#define LT_UUID_STRING_LEN     (36)      ///< length of a UUID string, 8-4-4-4-12, 36 characters
#define LT_UUID_BYTE_LEN       (16)      ///< length of a UUID data, 16 bytes

/*  _________________________
    Library Root Interface */
TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilityByteOps, 1);

struct LTUtilityByteOpsApi {

    INHERIT_LIBRARY_BASE

    void (* GenRandomBytes)(u8 * pBuffToFill, u32 nBytes);
        /**< Generates a sequence of random bytes.
         *
         *   %GenRandomBytes() uses a very fast, very small algorithm developed in 2014 by
         *   Melissa O'Neil of Harvey Mudd college called PCG-XSH-RR.  Though technically not
         *   cryptographically secure, it is statistically non-predictable and generates
         *   2^64 different streams of uniformly distributed non-recurring bit patterns in
         *   a 2^64 number space.
         *
         *   For more info, see http://www.pcg-random.org/pdf/hmc-cs-2014-0905.pdf
         *
         *   If a cryptographically secure random sequence of bytes is required, the output
         *   of GenRandomBytes() can be encrypted with any *appropriately seeded*, emphasis on
         *   appropriately seeded, cryptographically secure block cipher such as AES or hashed
         *   with any cryptographically secure hash algorithm.
         *
         *   @param pBuffToFill a pointer to a buffer to receive the random bytes
         *   @param nBytes the number of random bytes to fill pBuffToFill with
         *   @note  pBuffToFill must point to an allocated buffer of at least nBytes
         */

    void (* GenRandomBytesAsHexString)(u32 nNumHexBytes, char * pStringBuff, u32 nBuffSize);
        /**< Generates random hex-encoded string of specified length.
         *
         *   %GenRandomBytesAsHexString has the same randomness properties as
         *   %GenRandomBytes() as it is used to generate the underlying random
         *   bytes which are then encoded as hex.
         *
         *   @param nNumHexBytes the number of random bytes to generate
         *   @param pStringBuff the buffer in which to store the encoded hex string
         *   @param nBuffSize the size of pStringBuff in bytes.
         *                    Must be at least nNumHexBytes * 2 + 1 bytes.
         */

    void (* GenUUID)(u8 uuid[16]);
        /**< Generates a 16 byte UUID.
         *
         *   Call %GenUUID() to generate a Version 4, variant 1 UUID, that is a random
         *   UUID with 122 bits of randomness.  Probability of collision with entropy seeded
         *   PRNG is 1 in a billion per 103 trillion generated UUIDs.
         *
         *   @param uuid a 16 byte buffer to receive the UUID
         */

    bool (* UUIDToString)(const u8 uuid[16], char * pStringBuff, u32 nBuffSize);
        /**< Converts a 16 byte UUID to canonical null-terminated string form.
         *
         *   Call %UUIDtoString() to convert a 16 byte UUID to the canonical null-teriminated
         *   string form of "00112233-4455-4677-8899-aabbccddeeff".
         *
         *   @param uuid the UUID to convert to string
         *   @param pStringBuff the buffer into which to receive the converted string
         *   @param nBuffSize the size of the buffer
         *   @return true if successful, false if pStringBuff is NULL or nBuffSize is < 37
         *   @note  pStringBuff must point to at least nBuffSize bytes; nBuffSize must
         *          be at least 37 to contain 36 characters and a null terminator
         */

    bool (* StringToUUID)(const char * pUUIDString, u8 uuid[16]);
        /**< Converts a null-terminated UUID string into uuid bytes.
         *
         *   Call %StringToUUID() to convert a canonical null-terminated
         *   UUID string of the form "00112233-4455-4677-8899-aabbccddeeff" into
         *   uuid bytes.
         *
         *   @param pUUIDString the string to convert to uuid bytes
         *   @param uuid the uuid buffer into which to receive the uuid bytes
         *   @return true if successful, false if pUUIDString doesn't represenet a UUID
         *   @note  pUUIDString must point to a null terminated string of strlen 36 with all characters
         *          in the range of '0' to '9' or 'a' to 'f' (lower case only), except the characters
         *          at offsets 8, 13, 18, and 23 which must be '-'.
         */

    bool (* IsNullUUID)(const u8 uuid[16]);
        /**< Determines if a 16 byte UUID is null (all zeroes).
         *
         *   @param uuid the UUID to examine for null determination
         *   @return true if the UUID is null, false otherwise
         *   @see NullifyUUID()
         */

    void (* NullifyUUID)(u8 uuid[16]);
        /**< Sets a 16 byte UUID to null (all zeroes).
         *
         *   @param uuid the UUID to nullify
         *   @see IsNullUUID()
         */

    u32 (* Base64Encode)(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char * pBase64StringBufferToFill, u32 nBase64StringBufferSize);
        /**< Encodes arbitrary data into a null=terminated base64 encoded string.
         *
         *   @param pBytesToEncode pointer to the data bytes to encode
         *   @param nNumBytesToEncode the number of bytes to encode starting at pBytesToEncode
         *   @param pBase64StringBufferToFill a pointer to a string buffer that will receive the Base64 encoded string
         *   @param nBase64StringBufferSize size in bytes of the buffer pointed to by pBase64StringBufferToFill
         *   @return strlen (without null-terminator) of the null-terminated Base64 encoded string stored in pBase64StringBufferToFill
         *   @note   This function null terminates the encoded string, so nBase64StringBufferSize must be large enough to hold the null terminator.
         *           Use GetBase64EncodeBufferRequirement() to determine the size of the buffer to use.
         *   @see   GetBase64EncodeBufferRequirement()
         */

    u32 (* Base64Decode)(const char * pBase64EncodedString, u32 nBase64EncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize);
        /**< Decodes a base64 encoded string.
         *
         *   Decoding of incomplete bytes is not allowed, so for example an input string with only one
         *   valid character (6 bits) will have no decoded output.
         *
         *   @param pBase64EncodedString the string to decode
         *   @param nBase64EncodedStringStrLen string length of the string excluding null-terminator
         *   @param pDecodedDataBufferToFill a pointer to a buffer that will receive the decoded data
         *   @param nDecodedDataBufferSize size in bytes of the buffer pointed to by pDecodedDataBufferToFill
         *   @return the number of decoded bytes stored in pDecodedDataBufferToFill
         *   @note  pDecodedDataBufferToFill and nDecodedDataBufferSize must be large enough to hold the decoded data bytes.
         *          Use GetBase64DecodeBufferRequirement() to determine the size of the buffer to use. This will not
         *          check the string for padding characters so may be slightly larger than necessary, so you should
         *          always use the return value to know how many bytes from the buffer should be used.
         *   @see   GetBase64DecodeBufferRequirement()
         */

    u32 (* GetBase64EncodeBufferRequirement)(u32 nNumBytesToEncode);
        /**< Returns size of the string buffer required to base64 encode a given number of bytes.
         *
         *   Use this function to determine the minimum
         *   buffer size to pass into Base64Encode().  Since Base64Encode() null terminates the generated
         *   base64 encoded string, this function includes an extra byte for the null terminator.
         *
         *   The following equation is used:
         *
         *        bufferRequirement = (((nNumBytesToEncode + 2) / 3) * 4) + 1; / * plus 1 for null term * /
         *
         *   @param nNumBytesToEncode the number of bytes for which to determine the encode buffer requirement
         *   @return the length in bytes of the buffer required to base64 encode nNumBytesToEncode bytes
         *   @note the value returned includes room for the base64 encoded string's null-terminator and can be
         *         used as the minimum buffer size when calling Base64Encode() to encode nNumBytesToEncode
         *         number of bytes.
         *   @see Base64Encode()
         */

    u32 (* GetBase64DecodeBufferRequirement)(u32 nBase64EncodedStringStrLen);
        /**< Returns size of the data buffer required to base64 decode a base64 encoded string of a given length.
         *
         *   Use GetBase64DecodeBufferRequirement() to determine the minimum
         *   size of the buffer to pass into Base64Decode() for a base64 encoded string of strlen nBase64EncodedStringStrLen.
         *
         *   The following equation is used:
         *
         *        bufferRequirement = ((nBase64EncodedStringStrLen + 3) / 4) * 3;
         *
         *   @param nBase64EncodedStringStrLen the strlen (length without null terminator) of the base64 encoded string for which to determine the decode buffer requirement
         *   @return the size in bytes of the buffer required to base64 decode a base64 encoded string with strlen nBase64EncodedStringStrLen
         *   @note the value returned can be used as the minimum buffer size when calling Base64Decode() to decode
         *         a base64 encoded string with strlen nBase64EncodedStringStrLen
         *         number of bytes.
         *   @see Base64Decode()
         */

    u32 (* HexEncode)(const u8 * pBytesToEncode, u32 nNumBytesToEncode, char * pHexStringBufferToFill, u32 nHexStringBufferSize, bool bLowerCase);
        /**< Performs hex encoding.
         *
         *   A byte array is encoded as a string of hexadecimal digits, two digits per byte.
         *
         *   @param pBytesToEncode    pointer to the data bytes to encode
         *   @param nNumBytesToEncode the number of bytes to encode starting at pBytesToEncode
         *   @param pHexStringBufferToFill pointer to a string buffer that will receive the hex encoded string
         *   @param nHexStringBufferSize   size in bytes of the buffer pointed to by pHexStringBufferToFill
         *   @param bLowerCase  flag that defines if the generated hexadecimal string contains lowercase (bLowerCase == true) or uppercase characters (bLowerCase == false).
         *   @return strlen (without null-terminator) of the null-terminated hex encoded string stored in pHexStringBufferToFill
         *
         *   @note   This function null terminates the encoded string, so nHexStringBufferSize must be large enough for the encoded string to hold 2*nNumBytesToEncode+1 characters. */

    u32 (* HexDecode)(const char * pHexEncodedString, u32 nHexEncodedStringStrLen, u8 * pDecodedDataBufferToFill, u32 nDecodedDataBufferSize);
        /**< Performs hex decoding.
         *
         *   A hex encoded string is decoded to bytes, two hex digits per one byte.
         *
         *   @param  pHexEncodedString       pointer to a string of hexadecimal digits that may include both lowercase and uppercase characters.
         *   @param nHexEncodedStringMaxLen  maximum number of characters read from pHexEncodedString.
         *   If nHexEncodedStringMaxLen is an odd number, nHexEncodedStringMaxLen-1 is used instead.
         *   If a character is encountered that is not a valid hex digit and the maximum number of characters is not read yet, the decoding process stops. If the number of characters read at that time is an odd number, the last character is ignored.
         *   @param pDecodedDataBufferToFill a pointer to a buffer that will receive the decoded data.
         *   @param nDecodedDataBufferSize size in bytes of the buffer pointed to by pDecodedDataBufferToFill
         *
         *   @note  nDecodedDataBufferSize must be large enough to hold the decoded data bytes, which means that 2*nDecodedDataBufferSize >= nHexEncodedStringStrLen.
         *   @return the number of decoded bytes stored in pDecodedDataBufferToFill */

    void (* Crc32)(const void * pBuf, u32 nSize, u32 * pCrc);
        /**< Calculates the CRC32 of one or more sequential buffers.
         *
         *   Can be invoked either once or multiple times on partial buffers to calculate the CRC of the composite buffer.
         *
         *   This CRC32 implementation uses the following polynomial:
         *    X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X+1
         *
         *   @param  pBuf  the partial buffer over which the CRC32 is calculated.
         *   @param nSize  the partial buffer size in bytes.
         *   @param  pCrc  pointer to the partial CRC32 or (on final invocation) the final CRC32.
         *   @note  *pCrc is normally initialized to zero before the first call.
         */

    void (*SwapBytes)(u8 *buf, u32 len);
        /**< Swap bytes in a buffer.
         *
         *   @param buf  the buffer of bytes to swap.
         *   @param len  the length of buffer in bytes.
         */
};

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYBYTEOPS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Nov-21   augustus    created
 *  28-Feb-22   commodus    added hex coding functions
 */
