/*******************************************************************************
 * LTBootDriver.h                                       LT Bootloader Driver API
 *                                                                (Arch Ver 1.1)
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/* This file defines the interface between the LTBoot executive and
 *   platform-specific boot code. */

#ifndef __LTBOOTDRIVER_H_
#define __LTBOOTDRIVER_H_

#include "LTBoot.h"

/*
 * Application Bootloader API
 *   On success, security functions shall return kLTBootSecurityCheck_Pass.
 *   On failure, security functions shall invoke:
 *      LTBootPlatform_SecurityAbort()              -OR-
 *      LTBootPlatform_SecurityAbortWithFailover()
 *   to be followed up with:
 *      LTBootPlatform_SecurityAssert()
 */

const char * LTBootDriver_GetVersion(void);
/**< Obtain version name for the bootloader.
 *
 *  @return pointer to the version string. */

LTBootSecurityCheck LTBootDriver_TRNGDelay(void);
/**< Wait for a small random delay (< 100 microseconds).
  *  The delay is to be generated with a True Random Number Generator, if available.
  *  TRNG delays are injected to help protect against fault injection.
  *  Function should ONLY print diagnostic messages on failure.
  *
  *  @return kLTBootSecurityCheck_Pass if call succeeds. */

LTBootSecurityCheck LTBootDriver_CheckChipSecurity(void);
/**< Perform security check on chip and OTP (e.g.: key checks, chip revision, security feature enables).
  *  These checks are to help protect against use on altered or unauthorized devices.
  *  Function should ONLY print diagnostic messages on failure.
  *
  *  @return kLTBootSecurityCheck_Pass if check succeeds. */

LTBootSecurityCheck LTBootDriver_CheckBootloaderSecurity(void);
/**< Perform security check on THIS bootloader (e.g.: bootloader dynasty).
  *  Function is responsible for keeping bootloader dynasty OTP (if any) up-to-date.
  *  Function should ONLY print diagnostic messages on failure.
  *
  *  @return kLTBootSecurityCheck_Pass if check succeeds. */

bool LTBootDriver_GetDeviceID(u8 deviceID[kLTBootDeviceIDLength]);
/**< Obtain Device ID from OTP or flash
  *
  *  @param[out] pDeviceID Function writes device ID to this array.
  *  @return true if successful. */

LTBootSecurityCheck LTBootDriver_AuthenticateLTAT(const LTBootInfo * pBootInfo);
/**< Verify signature of LT Authentication Token.
  *  Function must check the signature of the token.
  *  Function can assume magic number, version and identity have already been checked.
  *  Function should ONLY print diagnostic messages on failure.
  *
  *  @param[in] pBootInfo Pointer to boot information (e.g.: partition information).
  *  @return kLTBootSecurityCheck_Pass if signature check passes and identity matches device. */

bool LTBootDriver_CheckOTAPartition(const LTBootInfo * pBootInfo, u32 nBootIdx);
/**< Perform sanity check on OTA partition contents (e.g.: check magic number or image header validity).
  *  This function is guaranteed to be invoked before LTBootDriver_CheckApplicationSecurity.
  *  Function should ONLY print diagnostic messages on failure. This is NOT a security check.
  *
  *  @param[in] pBootInfo Pointer to boot information (e.g.: partition information).
  *  @param[in] nBootIndex Index of firmware partition to check.
  *  @return true if check succeeds, false otherwise. */

LTBootSecurityCheck LTBootDriver_LoadAndAuthenticateApplication(const LTBootInfo * pBootInfo);
/**< Perform application security checks while (optionally) simultaneously loading the application.
  *  This function should perform security checks such as signature checking and application rollback.
  *  This function is invoked _before_ LTBootDriver_RunApplication.
  *
  *  Function should ONLY print diagnostic messages on failure.
  *  @param[in] pBootInfo Pointer to boot information (e.g.: partition information).
  *  @return kLTBootSecurityCheck_Pass if check succeeds. */

void LTBootDriver_RunApplication(const LTBootInfo * pBootInfo);
/**< Load (if not loaded in LTBootDriver_CheckApplicationSecurity) and run LT Application.
  *
  *  This function can contain additional security checking but should NOT return under any
  *  circumstance. Function should print diagnostic messages on failure and LIMITED diagnostic
  *  messages on success.
  *  @param[in] pBootInfo Pointer to boot information (e.g.: partition information). */

void LTBootDriver_Crc32(u8 * pBuffer, u32 nSizeInBytes, u32 * pCrc);
/**< Calculates the CRC32 of one or more sequential buffers.
 *  NOTE: This CRC implementation uses the 0xedb88320 polynomial.
 *
 *  NB: The implementation of this in the LTBootDriver is OPTIONAL since a default implementation
 *  with weak linkage already exists in LTBoot.c. This weak function may be overridden if, for
 *  example, the underlying implementation supports hardware acceleration.
 *
 *  @param[in]     pBuffer Buffer to calculate incremental CRC over.
 *  @param[in]     nSizeInBytes Size of buffer in bytes.
 *  @param[in,out] pCrc pointer to the partial CRC or (on final invocation) the final CRC.
 */

/* Serial Driver API */

void LTBootDriverSerial_PutCharsToConsole(const char * pChars, u32 nChars) LT_ISR_SAFE;
/**< Write characters to console.
 *
 * NB: The implementation of this function is OPTIONAL unless LTBootStdlib_printf() is used.
 *  @param[in]     pChars Pointer to buffer of characters to print.
 *  @param[in]     nChars Number of characters to print.
 */

/* Flash Driver API */

u32 LTBootDriverFlash_GetBytesPerSector(void);
/**< Number of bytes per flash sector.
 *
 *   @return Size of flash sector in bytes. */

u16 LTBootDriverFlash_GetWriteQuantum(void);
/**< Obtain the alignment and size constraint value for writing flash.
 * Flash write addresses must be aligned to the write quantum.
 * Flash write sizes must be a multiple of the write quantum.
 * Quantum value is guaranteed to be a power-of-2.
 *
 * @return power-of-2 constraint value.
 * @return 1 if no such constraint exists. */

bool LTBootDriverFlash_GetPartitionTableOffset(u32 * pByteOffsetToSet, bool bGetPrimary);
/**< Get byte offset of partition table in flash
 *
 *   @param[out] pByteOffsetToSet Pointer to store the byte offset of the partition table into.
 *   @param[in]  bGetPrimary Set true for primary partition table, false to obtain the backup partition table.
 *   @return true if partition table exists, false otherwise */

bool LTBootDriverFlash_ReadBytes(u32 nByteOffset, u32 nNumBytes, u8 * pBuff);
/**< Read bytes from flash into provided buffer.
 *
 *   @param[in] nByteOffset Byte offset in flash to start reading from.
 *   @param[in] nNumBytes Number of 
 *   @return true if read successful, false otherwise. */

#endif

