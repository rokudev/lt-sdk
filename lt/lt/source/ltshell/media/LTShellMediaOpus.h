/*******************************************************************************
 * Local definitions for the shell handlers for LTDeviceMedia
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAOPUS_H
#define ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAOPUS_H
#include <lt/LT.h>
#include <lt/system/fs/LTFile.h>
#include <lt/system/shell/LTSystemShell.h>

LT_EXTERN_C_BEGIN

enum {
    kOggsCapturePatternSize     = 4,
    kOpusHeadSize               = 19,
    kOggsPageMaxSegments        = 255,
    kOggsPageMaxSegmentSize     = 255,
    kOggsGranulePos1Second      = 48000,
};

enum {
    kOpusHeaderFlagFirstPage    = 0x02,
    kOpusHeaderFlagLastPage     = 0x04,
};

#define OPUSHEADMAGIC "OpusHead"
#define OPUSTAGMAGIC "OpusTags"

typedef struct __attribute__((packed)) {
    u8      capturePattern[kOggsCapturePatternSize];
    u8      version;
    u8      flags;
    u64     granulePos;
    u32     serial;
    u32     sequence;
    u32     checksum;
    u8      pageSegments;
} OggsPageHeader_t;

typedef struct {
    u8      magic[8];
    u8      version;
    u8      channels;
    u16     preSkip;
    u32     inputSampleRate;
    u32     outputGain;
    u16     channelMappingFamily;
    u8      streamCount;
    u8      coupledCount;
    u8      channelMapping[kOggsPageMaxSegments];
} OpusHead_t;

typedef struct {
    OggsPageHeader_t header;
    u8 segmentTable[kOggsPageMaxSegments];
    u8 packet[kOggsPageMaxSegments * kOggsPageMaxSegmentSize];
    u32 packetByteIndex;
    u32 segmentIndex;
} OggsPage_t;

typedef struct {
    LTFile *pFile;
    OggsPageHeader_t pageHeader;
    u8 segmentTable[kOggsPageMaxSegments];
    u32 seqmentIndex;
} OggsFileContext_t;

bool OggSIsPageHeaderValid(OggsPageHeader_t *pHeader);
u32 OggSGetAvailablePageSpace(OggsPage_t* pPage);
bool OggSGetPacketFromPage(OggsPage_t* pPage, u8 **ppPacket, u32 *pPacketSize);
bool OggSSetPacketToPage(OggsPage_t* pPage, u8 *pPacket, u32 packetSize, u32 encodedSamples);
void OggSInitPage(OggsPage_t *pPage, u32 streamSerial, u32 sequence, u64 granulePos);
bool OggSReadPageFromFile(LTFile *pFile, OggsPage_t *pPage);
bool OggSWritePageToFile(LTFile *pFile, OggsPage_t *pPage);
bool OggSInitFile(LTFile *pFile, u32 streamSerial, OpusHead_t *opusHead);

// Playback
bool PlayOpusFile(LTShell hShell, LTFile *pFile);
void StopOpusPlayback(void);

// Capture
bool StartOpusCapture(LTShell hShell, LTFile *pFile);
void StopOpusCapture(void);

LT_EXTERN_C_END

#endif /* ROKU_LT_SOURCE_LTSHELL_MEDIA_LTSHELLMEDIAOPUS_H */
