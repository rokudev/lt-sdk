/*******************************************************************************
 * <lt/device/flash/LTDeviceFlash.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltdevice_flash LTDeviceFlash
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for NOR flash memory operations.
 */

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_FLASH_LTDEVICEFLASH_H
#define ROKU_LT_INCLUDE_LT_DEVICE_FLASH_LTDEVICEFLASH_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

enum {
    kLTDeviceFlash_Magic_PartitionTable = 0x5450544c,  /**< Partition table magic number */
    kLTDeviceFlash_MaxNameSize          = 16,          /**< Max partition name size */
    kLTDeviceFlash_MaxTypeSize          = 16,          /**< Max partition type size */
};

typedef_LTENUM_SIZED(LTDeviceFlash_PartitionFlags, u32) {
    kLTDeviceFlash_PartitionFlags_Encrypted = 1 << 0,  /**< Automatic hardware encryption is enabled for partition */
};

/* Flash partition table entry */
typedef struct {
    char                          name[kLTDeviceFlash_MaxNameSize]; /**< Partition name (null-terminated) */
    char                          type[kLTDeviceFlash_MaxTypeSize]; /**< Partition type (null-terminated) */
    u32                           nByteOffset;                      /**< Offset of partition in bytes */
    u32                           nNumSectors;                      /**< Size of partition in sectors */
    LTDeviceFlash_PartitionFlags  flags;                            /**< Partition flags */
} LTDeviceFlash_PartitionEntry;

/* Flash partition table header */
typedef struct {
    u32                           nMagic;         /**< Magic number */
    u8                            nNumPartitions; /**< Number of partitions, minimum is 1 */
    u8                            nDeviceSize;    /**< Device size for table = 2^(N+6) bytes, ideally should match NOR device ID */
    u8                            nTableVersion;  /**< Table version number */
    u8                            nRsvd[5];       /**< Reserved (set to zero) */
 /*
  * Partition table entries and footer:
  *  (Note that the partition table itself should have a corresponding entry in the
  *   partition table)
  *
  * LTDeviceFlash_PartitionEntry  partitions[nNumPartitions];
  * u32                           nCRC; // CRC covers everything up to platform-specific data
  * ...                           optionalPlatformSpecific; // filled with 0xff if unused
  *
  */
} LTDeviceFlash_PartitionTableHeader;

/* Flash partition information */
typedef struct {
    LTDeviceFlash_PartitionEntry  entry;          /**< Partition table entry */
    u16                           nWriteQuantum;  /**< Power-of-2 flash write alignment and size constraint (bytes) */
} LTDeviceFlash_Partition;

LT_STATIC_ASSERT_SIZE_32_64(LTDeviceFlash_PartitionEntry, 44, 44)
LT_STATIC_ASSERT_SIZE_32_64(LTDeviceFlash_PartitionTableHeader, 12, 12)
LT_STATIC_ASSERT_SIZE_32_64(LTDeviceFlash_Partition, 48, 48)

typedef bool (LTDeviceFlash_PartitionEnumProc)(u32 nNumPartitions, const LTDeviceFlash_Partition * pPartition, void * pClientData);
    /**< Flash partition enumeration callback function.
     *  NB: Partition data is transitory and must be copied from the provided pointer.
     *
     * @param[in] nNumPartitions Total number of partitions.
     * @param[in] pPartition Partition data.
     * @param[in] pClientData Pointer to private data supplied to call to EnumeratePartitions().
     * @return    true to continue enumeration process.
     * @return    false to abort enumeration process. */

/*________________________________________
 / LTDeviceFlash Library ROOT INTERFACE */
TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFlash, 1);

/**  Library root interface for the LTDeviceFlash library, used to create Flash LTDeviceUnit instance handles.
 *
 *   LTDeviceFlash provides common LTDevice prototype functions to get the number of units (flash device
 *   chips) and to create handles that refer to those units (instances of flash device chips).
 *   Once a created handle is returned, @c lt_gethandleinterface will return the appropriate
 *   ILTFlashDeviceUnit interface (declared below) for operating on the created handle.
 */
struct LTDeviceFlash {

    /*  Relevant LTDevice common prototype functions are:
        u32             (* GetNumDeviceUnits)(void);
        LTDeviceUnit    (* CreateDeviceUnitHandle)(u32 nDeviceUnitNum);
    */

    INHERIT_DEVICE_LIBRARY_BASE

    bool (* GetPartition)(LTDeviceUnit hFlashUnit, const char * pPartitionName, LTDeviceFlash_Partition * pPartition);
        /**< Get local information for a named partition.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[in]  pPartitionName Name of the partition.
         * @param[out] pPartition Where to store partition information.
         * @return true if named partition exists, false if it doesn't.
         */

    bool (* GetActiveFirmwarePartition)(LTDeviceUnit hFlashUnit, LTDeviceFlash_Partition * pPartition);
        /**< Obtain location of active firmware partition.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[out] pPartition Where to store active partition information.
         * @return true if able to determine active partition, false if not. */

    bool (* ErasePartition)(LTDeviceUnit hFlashUnit, const char * pPartitionName);
        /**< Erase a named partition
         *
         * @param[in] hFlashUnit The handle to the flash device on which the partition is to be erased
         * @param[in] pPartitionName the name of the partition to erase
         * @return true if the partition was erased, false if it was not or if pPartitionName is not a valid partition name */

    bool (* EnumeratePartitions)(LTDeviceUnit hFlashUnit, LTDeviceFlash_PartitionEnumProc * pCallback, void * pClientData);
        /**< Invoke provided callback function for each partition on the flash device.
         *  NB: The provided callback MUST NOT alter the partition table on flash.
         *
         * @param[in] pCallback Callback function invoked for each partition.
         * @param[in] pClientData Client-provided data to supply to callback function.
         * @return    true if fully enumerated: all callbacks returned true.
         * @return    false if enumeration aborted early: a callback returned false OR an error occurred. */

    bool (* GetPartitionTableInfo)(LTDeviceUnit hFlashUnit, u8 * pVersionNumber, bool * pUsingPrimary);
	/**< Obtain information about partition table
	 *
	 * @param[in]  hFlashUnit The handle to the flash device being queried.
	 * @param[out] pVersionNumber Where to store the partition table version.
	 * @param[out] pUsingPrimary Set true if using the primary partition table, false for backup.
	 * @return true if able to determine partition table version, false if not. */
};

/* Maximum length of Flash Chip ID array */
enum { kLTFlashDeviceMaxChipIDBytes = 8 };

/*________________________________________________________________________________________
 / ILTFlashDeviceUnit INTERFACE - interface for operating on Flash LTDeviceUnit handles */
TYPEDEF_LTLIBRARY_INTERFACE(ILTFlashDeviceUnit, 1);

 /**  NOR Flash Driver Interface
  *
  *   \note If the partition write quantum is non-zero then all flash writes must be aligned to that value
  *   and write sizes also must be a multiple of the quantum. Write quantum is given in bytes. The quantum
  *   is intended to support hardware encryption modes that have aligned payload constraints. */

struct ILTFlashDeviceUnitApi {

    INHERIT_INTERFACE_BASE

    u32 (* GetFlashID)(LTDeviceUnit hFlashUnit, u8 flashIDToSet[kLTFlashDeviceMaxChipIDBytes]);
        /**< Returns the ID of the flash device.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[out] flashIDToSet The location to receive the ID.
         * @return the number of bytes written in flashIDToSet[], or zero on failure.
         * @note NOR flash IDs are typically 3 bytes in length. */

    u32 (* GetNumBytes)(LTDeviceUnit hFlashUnit);
        /**< Returns the number of bytes in the flash device.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @return the number of bytes in the flash device.
         * @note   The flash device may be read from or written to one byte
         *         at a time; valid byte addresses for the flash device are [0..GetNumBytes()-1] */

    u32 (* GetNumSectors)(LTDeviceUnit hFlashUnit);
        /**< Returns the number of sectors in the flash device.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @return the number of sectors in the flash device.
         * @note   Valid sectors numbers for the flash device are [0..GetNumSectors()-1].
         *         Whenever the flash device is written to, that location of the flash device
         *         must be erased first. Unfortunately the unit of erase resolution
         *         is the sector, not the byte. Furthermore sectors are relatively
         *         large.  Care must be taken to erase properly before writing.
         * @see    EraseSectors()
         * @see    EraseDevice() */

    u32 (* GetBytesPerSector)(LTDeviceUnit hFlashUnit);
        /**< Returns the number of bytes in a sector.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @return the number of bytes in a sector. */

    u32 (* SectorNumberToByteOffset)(LTDeviceUnit hFlashUnit, u32 nSectorNumber);
        /**< Converts a sector number to a byte address.
         *
         * @param[in] hFlashUnit The handle to the flash device to use for the conversion.
         * @param[in] nSectorNumber The sector number, in the range [0..GetNumSectors()-1]
         * @return the byte address of the sector number specified by nSectorNumber.
         * @todo Document behavior if nSectorNumber is out of range. */

    u32 (* ByteOffsetToSectorNumber)(LTDeviceUnit hFlashUnit, u32 nByteOffset);
        /**< Converts a byte address to a sector number.
         *
         * ByteOffsetToSectorNumber() returns a sector number for the sector containing
         * the byte address nByteOffset.
         *
         * @param[in] hFlashUnit The handle to the flash device to use for the conversion.
         * @param[in] nByteOffset The byte address to find the sector number of.
         * @return the sector number of the sector containing nByteOffset.
         * @note Calling SectorNumberToByteOffset(ByteOffsetToSectorNumber(nByteOffset)) will
         *       not always yield a byte address equal to the original nByteOffset.  For example,
         *       ByteOffsetToSectorNumber(hDevice, 0) and ByteOffsetToSectorNumber(hDevice, 1) both return 0 for the
         *       sector number, but SectorNumberToByteOffset(hDevice, 0) always byte address 0, i.e.
         *       SectorNumberToByteOffset(ByteOffsetToSectorNumber(hDevice, 0)) == 0 and
         *       SectorNumberToByteOffset(ByteOffsetToSectorNumber(hDevice, 1)) == 0.
         * @todo Document behavior if nByteOffset is out of range. */

    bool (* GetPartitionTableOffset)(LTDeviceUnit hFlashUnit, u32 * pByteOffset, bool bGetPrimary);
        /**< Obtain location of flash partition table.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[out] pByteOffset Where to store the byte offset.
         * @param[in]  bGetPrimary True for primary partition table, false for backup.
         * @return true if partition table exists. */

    bool (* BusAddressToByteOffset)(LTDeviceUnit hFlashUnit, void * pAddress, u32 * pByteOffset);
        /**< Obtain the flash byte offset corresponding to a given bus address (if any).
         * @note This function may be used to identify the currently running firmware partition.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[in]  pAddress The bus address to convert to a byte offset on flash.
         * @param[out] pByteOffset Where to store the byte offset.
         * @return true if mapping can be determined, false otherwise. */

    u16 (* GetWriteQuantum)(LTDeviceUnit hFlashUnit);
        /**< Obtain the alignment and size constraint value for writing flash.
         * Flash write addresses must be aligned to the write quantum.
         * Flash write sizes must be a multiple of the write quantum.
         * Quantum value is guaranteed to be a power-of-2.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @return power-of-2 constraint value.
         * @return 1 if no such constraint exists. */

    bool (* EraseDevice)(LTDeviceUnit hFlashUnit);
        /**< Performs a bulk erase of the flash device.
         *
         * @param[in] hFlashUnit The handle to the flash device to erase.
         * @return true if successful, false otherwise. */

    bool (* EraseSectors)(LTDeviceUnit hFlashUnit, u32 nFirstSector, u32 nNumSectors);
        /**< Erases the range of sectors starting at nFirstSector and continuing for nNumSectors.
         * @note If possible, the Driver will employ large-block erase operations to reduce erase time.
         *
         * @param[in] hFlashUnit the handle to the flash device to receive the sector erase operation.
         * @param[in] nFirstSector the beginning sector of the range to erase.
         * @param[in] nNumSectors the number of sectors to erase.
         * @return true if successful, false otherwise. */

    bool (* IsDeviceWriteProtected)(LTDeviceUnit hFlashUnit);
        /**< Determines if the entire flash device is write-protected.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @return true if the entire flash is write-protected, false otherwise. */

    bool (* IsSectorWriteProtected)(LTDeviceUnit hFlashUnit, u32 nSectorNumber);
        /**< Determines if a specified sector of a flash device is write-protected.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @param[in] nSectorNumber the sector number of the sector being queried.
         * @return true if the sector is write-protected, false otherwise. */

    bool (* WriteProtectDevice)(LTDeviceUnit hFlashUnit, bool bWriteProtect);
        /**< Sets write protection status of the entire flash device.
         *
         * @param[in] hFlashUnit The handle to the flash device being modified.
         * @param[in] bWriteProtect The write protect status to set: true for write-protected, false for not write-protected.
         * @return true if the write protect status was set successfully, false otherwise. */

    bool (* WriteProtectSector)(LTDeviceUnit hFlashUnit, u32 nSectorNumber, bool bWriteProtect);
        /**< Sets write protection status of a specific sector of a flash device.
         *
         * @param[in] hFlashUnit The handle to the flash device being modified.
         * @param[in] nSectorNumber The address of the sector being modified.
         * @param[in] bWriteProtect The write protect status to set: true for write-protected, false for not write-protected.
         * @return true if the write protect status was set successfully, false otherwise. */

    bool (* ReadBytes)(LTDeviceUnit hFlashUnit, u32 nByteOffset, u32 nNumBytes, u8 * pBuff);
        /**< Reads a specified number of bytes from a specific byte address of a flash device.
         *  Automatically decrypts bytes if partition supports hardware encryption.
         *
         * @param[in]  hFlashUnit The handle to the flash device being read.
         * @param[in]  nByteOffset The flash device byte address being read.
         * @param[in]  nNumBytes The number of bytes to read from the flash device.
         * @param[out] pBuff A buffer to receive the bytes read.
         *                   This buffer must contain at least nNumBytes bytes.
         * @return true if the specified number of bytes was read, false otherwise.
         * @note   Reading with nNumBytes equal to zero does nothing and returns true. */

    bool (* WriteBytes)(LTDeviceUnit hFlashUnit, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff);
        /**< Writes a specified number of bytes to a specific byte address of a flash device.
         *  Automatically encrypts bytes if partition supports hardware encryption.
         *
         * @param[in] hFlashUnit The handle to the flash device being written.
         * @param[in] nByteOffset The flash device byte address being written.
         * @param[in] nNumBytes The number of bytes to write to the flash device.
         * @param[in] pBuff A buffer holding the bytes to be written.
         *                  This buffer must contain at least nNumBytes bytes.
         * @return true if the specified number of bytes was written, false otherwise.
         * @note Be sure to issue an erase command before writing, or be sure that the target
         * byte addresses in the flash device are already erased.
         * @note Writing with nNumBytes equal to zero does nothing and returns true. */

    bool (* ReadRawBytes)(LTDeviceUnit hFlashUnit, u32 nByteOffset, u32 nNumBytes, u8 * pBuff);
        /**< Reads a specified number of bytes from a specific byte address of a flash device.
         *  Bypasses decryption if partition supports hardware encryption.
         *
         * @param[in]  hFlashUnit The handle to the flash device being read.
         * @param[in]  nByteOffset The flash device byte address being read.
         * @param[in]  nNumBytes The number of bytes to read from the flash device.
         * @param[out] pBuff A buffer to receive the bytes read.
         *                   This buffer must contain at least nNumBytes bytes.
         * @return true if the specified number of bytes was read, false otherwise.
         * @note   Reading with nNumBytes equal to zero does nothing and returns true. */

    bool (* WriteRawBytes)(LTDeviceUnit hFlashUnit, u32 nByteOffset, u32 nNumBytes, const u8 * pBuff);
        /**< Writes a specified number of bytes to a specific byte address of a flash device.
         *  Bypasses encryption if partition supports hardware encryption.
         *
         * @param[in] hFlashUnit The handle to the flash device being written.
         * @param[in] nByteOffset The flash device byte address being written.
         * @param[in] nNumBytes The number of bytes to write to the flash device.
         * @param[in] pBuff A buffer holding the bytes to be written.
         *                  This buffer must contain at least nNumBytes bytes.
         * @return true if the specified number of bytes was written, false otherwise.
         * @note Be sure to issue an erase command before writing, or be sure that the target
         * byte addresses in the flash device are already erased.
         * @note Writing with nNumBytes equal to zero does nothing and returns true. */

    /* ____________________________________________________________________
     / Partition Table Methods - Platform drivers implement these methods
       to support different partition table formats (standard LT format,
       vendor-specific formats, etc.) */

    bool (* GetPartition)(LTDeviceUnit hFlashUnit, const char * pPartitionName, LTDeviceFlash_Partition * pPartition);
        /**< Get local information for a named partition.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[in]  pPartitionName Name of the partition.
         * @param[out] pPartition Where to store partition information.
         * @return true if named partition exists, false if it doesn't. */

    bool (* GetActiveFirmwarePartition)(LTDeviceUnit hFlashUnit, LTDeviceFlash_Partition * pPartition);
        /**< Obtain location of active firmware partition.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[out] pPartition Where to store active partition information.
         * @return true if able to determine active partition, false if not. */

    bool (* EnumeratePartitions)(LTDeviceUnit hFlashUnit, LTDeviceFlash_PartitionEnumProc * pCallback, void * pClientData);
        /**< Invoke provided callback function for each partition on the flash device.
         *  NB: The provided callback MUST NOT alter the partition table on flash.
         *
         * @param[in] hFlashUnit The handle to the flash device being queried.
         * @param[in] pCallback Callback function invoked for each partition.
         * @param[in] pClientData Client-provided data to supply to callback function.
         * @return    true if fully enumerated: all callbacks returned true.
         * @return    false if enumeration aborted early: a callback returned false OR an error occurred. */

    bool (* GetPartitionTableInfo)(LTDeviceUnit hFlashUnit, u8 * pVersionNumber, bool * pUsingPrimary);
        /**< Obtain information about partition table.
         *
         * @param[in]  hFlashUnit The handle to the flash device being queried.
         * @param[out] pVersionNumber Where to store the partition table version.
         * @param[out] pUsingPrimary Set true if using the primary partition table, false for backup.
         * @return true if able to determine partition table info, false if not. */
};

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_DEVICE_FLASH_LTDEVICEFLASH_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  08-Nov-20   augustus    created
 *  07-Nov-23   augustus    added ErasePartition
 */
