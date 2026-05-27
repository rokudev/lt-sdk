/******************************************************************************
 * Image.h                                                        Image Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef _IMAGE_H
#define _IMAGE_H

#include <sys/stat.h>

#include "lt/LTTypes.h"
#include "lt/device/flash/LTDeviceFlash.h"

typedef int (ImageFlashEraseFunc)(u32 nOffset, u32 nEraseSizeInBytes, u32 nNumBlocksToErase);

enum {
    kImageAreaAllIndex        =  0,  /*<< Index of area representing entire flash */
    kMaxAreaNameSize          = 16,  /*<< Maximum length of area names (including null) */
    kCryptoSHA2DigestLength   = 32   /*<< SHA2 256 checksum length */
};

typedef u32 ImageType;
enum ImageType {
    kImageType_All,              /*<< Entire image (type="all") */
    kImageType_Raw,              /*<< Raw partition (type="raw") */
    kImageType_PartitionTable,   /*<< LT partition table ("part") */
    kImageType_Boot,             /*<< Bootloader ("boot") */
    kImageType_OTAInfo,          /*<< OTA partition boot information ("otainfo") */
    kImageType_Firmware,         /*<< Firmware partition  ("fw")*/
    kImageType_FirmwareMfg,      /*<< Manufacturing firmware ("fw_mfg") */
    kImageType_Settings,         /*<< Settings ("settings") */
    kImageType_Misc,             /*<< Miscellaneous (other) */
    kImageType_Dummy,            /*<< Dummy, i.e.: not a partition */
};

typedef enum {
    FLASH_DUMMY,
} FlashAreaNum;

/*
 * An area is defined as a contiguous section of flash.
 * A partition is defined as a area that does no non-overlap other partitions.
 * The first area always refers to the entire flash part.
 */
typedef struct Area {
    char          filename[512];          /*<< Optional filename for area */
    char          name[kMaxAreaNameSize]; /*<< Area/partition name */
    char          type[kMaxAreaNameSize]; /*<< Area/partition type string */
    ImageType     nType;                  /*<< Area/partition enumerated type */
    FlashAreaNum  nAreaNum;               /*<< Area number */
    u32           nOffset;                /*<< Byte offset of area in flash */
    u32           nMaxSize;               /*<< Maximum size of area in bytes */
    bool          bMustFlashAll;          /*<< Image must be exact size of area or operation fails */
    bool          bExcludeFromAreas;      /*<< Exclude area from partition table */
    LTDeviceFlash_PartitionFlags  flags;  /*<< Area flag mask */
} Area;

typedef struct Image {
    u32           nSize;
    u32           nChecksum;
    u32           nCRC;
    u8          * pData;
    u8            digest[kCryptoSHA2DigestLength];
} Image;

unsigned ImageGetNumAreas(void);
char   * ImageGetDefaultAreaName(void);
Area   * ImageGetArea(int nAreaIdx);
Area   * ImageGetAreaByName(char * pName);
int      ImageGetAreaIndexByName(char * pName);
int      ImageGetNextAreaIndexByType(ImageType type, int nAreaIdx);
int      ImageGetFlasherImage(Image * pImage, const char * pDeviceVariant);
u32      ImageGetBlockSize(void);
u8       ImageGetTableVersionNumber(void);
u32      ImageGetWriteQuantum(void);
bool     ImageIsHostLittleEndian(void);
bool     ImageIsDeviceLittleEndian(void);
int      ImageList(void);

/* Endian conversions (pOut may be NULL) */
u16      ImageHostToDeviceU16(u16 * pOut, u16 nIn);
u16      ImageDeviceToHostU16(u16 * pOut, u16 nIn);
u32      ImageHostToDeviceU32(u32 * pOut, u32 nIn);
u32      ImageDeviceToHostU32(u32 * pOut, u32 nIn);

const char * ImageGetDeviceType(void);
const char * ImageGetDeviceFamily(void);

int  ImageCreateBlank(Image * pImage, u32 nSize);
int  ImageCreateFromBuffer(Image * pImage, u8 * pData, u32 nSize);
int  ImageCreateFromFile(Image * pImage, const char * pFilename);
int  ImageAppend(Image * pImage, Image * pAppendImage);
int  ImageAppendFromBuffer(Image * pImage, u8 * pAppendData, u32 nAppendSize);
int  ImageResize(Image * pImage, u32 nNewSize, u8 fillByte);
int  ImagePadToEnd(Image * pImage, u32 nAreaIdx);
int  ImagePadToBoundary(Image * pImage, u32 nBoundary);
int  ImageCalcChecksums(Image * pImage);
int  ImageWriteToFile(Image * pImage, char * pFilename);
/* Find a file and stat it, NB: caller must free *pFullPath */
int  ImageFindAndStatFile(const char * pFilename, char ** ppFullPath, struct stat * pStat);
void ImageFree(Image * pImage);

/* NOR Flash bulk erase helper function */
int ImageNorFlashErase(Image * pImage, Area * pArea, ImageFlashEraseFunc * pEraseSector,
                                                     ImageFlashEraseFunc * pErase32k,
                                                     ImageFlashEraseFunc * pErase64k);

int  ImageInit(char * pConfigFilename, char * pPaths);
void ImageFini(void);

#endif
