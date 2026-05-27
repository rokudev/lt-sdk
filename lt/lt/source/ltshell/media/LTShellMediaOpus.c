/*******************************************************************************
 *
 * Opus helper functions for Opus Playback and capture from LT Media Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include "LTShellMediaOpus.h"
#include <lt/system/fs/LTFile.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

static u8 OggsOpusTags[] = {
    0x4F, 0x70, 0x75, 0x73, 0x54, 0x61, 0x67, 0x73, 0x0D, 0x00, 0x00, 0x00, 0x4C, 0x61, 0x76, 0x66,  // OpusTags....Lavf
    0x35, 0x38, 0x2E, 0x32, 0x39, 0x2E, 0x31, 0x30, 0x30, 0x01, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00,  // 58.29.100......
    0x00, 0x45, 0x4E, 0x43, 0x4F, 0x44, 0x45, 0x52, 0x3D, 0x4C, 0x61, 0x76, 0x63, 0x35, 0x38, 0x2E,  // .ENCODER=Lavc58.
    0x35, 0x34, 0x2E, 0x31, 0x30, 0x30, 0x20, 0x6C, 0x69, 0x62, 0x6F, 0x70, 0x75, 0x73               // 54.100 libopus
};

static const u8 kOggsCaptureString[] = "OggS";

bool OggSIsPageHeaderValid(OggsPageHeader_t *pHeader) {
    return (lt_memcmp(pHeader->capturePattern, kOggsCaptureString, kOggsCapturePatternSize) == 0);
}

u32 OggSGetAvailablePageSpace(OggsPage_t* pPage) {
    return (kOggsPageMaxSegments - pPage->header.pageSegments) * kOggsPageMaxSegmentSize;
}

bool OggSGetPacketFromPage(OggsPage_t* pPage, u8 **ppPacket, u32 *pPacketSize) {
    if (!pPage) {
        return false;
    }

    if (pPage->segmentIndex >= pPage->header.pageSegments) {
        return false;
    }

    u32 packetSize = 0;
    while (pPage->segmentIndex < pPage->header.pageSegments) {
        u8 segmentSize = pPage->segmentTable[pPage->segmentIndex];
        packetSize += segmentSize;
        pPage->segmentIndex++;
        if (segmentSize < kOggsPageMaxSegmentSize) {
            break;
        }
    }
    *ppPacket = pPage->packet + pPage->packetByteIndex;
    *pPacketSize = packetSize;
    pPage->packetByteIndex += packetSize;
    return true;
}

bool OggSSetPacketToPage(OggsPage_t* pPage, u8 *pPacket, u32 packetSize, u32 encodedSamples) {
    if (!pPage) {
        return false;
    }
    
    if (OggSGetAvailablePageSpace(pPage) < packetSize) {
        return false;
    }

    lt_memcpy(((u8 *)pPage->packet) + pPage->packetByteIndex, pPacket, packetSize);
    pPage->packetByteIndex += packetSize;
    while (packetSize > 0) {
        u32 segmentSize = packetSize;
        if (segmentSize > kOggsPageMaxSegmentSize) {
            segmentSize = kOggsPageMaxSegmentSize;
        }
        pPage->segmentTable[pPage->header.pageSegments] = segmentSize;
        pPage->header.pageSegments++;
        packetSize -= segmentSize;
    }
    pPage->header.granulePos += encodedSamples;
    return true;
}

void OggSInitPage(OggsPage_t *pPage, u32 streamSerial, u32 sequence, u64 granulePos) {
    if (pPage) {
        lt_memset(pPage, 0, sizeof(OggsPage_t));
        lt_memcpy(pPage->header.capturePattern, kOggsCaptureString, kOggsCapturePatternSize);
        pPage->header.version = 0;
        pPage->header.pageSegments = 0;
        pPage->header.serial = streamSerial;
        pPage->header.sequence = sequence;
        pPage->header.granulePos = granulePos;
    }
}

bool OggSReadPageFromFile(LTFile *pFile, OggsPage_t *pPage) {
    if (!pFile || !pPage) {
        return false;
    }

    lt_memset(pPage, 0, sizeof(OggsPage_t));
    while (pFile->API->Read(pFile, kOggsCapturePatternSize, pPage->header.capturePattern) == kOggsCapturePatternSize) {
        if (OggSIsPageHeaderValid(&pPage->header)) {
            // Read the rest of the header
            u32 read = kOggsCapturePatternSize;
            read += pFile->API->Read(pFile, 
                                     sizeof(OggsPageHeader_t) - kOggsCapturePatternSize, 
                                     (u8 *)&(pPage->header) + kOggsCapturePatternSize);
            if (read != (sizeof(OggsPageHeader_t))) {
                break;
            }

            // Check if we have a valid page
            if (pPage->header.version == 0 && pPage->header.pageSegments > 0) {
                u32 totalDataSize = 0;
                pPage->segmentIndex = 0;
                pPage->packetByteIndex = 0;
                // Read the segment table
                read = pFile->API->Read(pFile, 
                                        pPage->header.pageSegments, 
                                        pPage->segmentTable);
                if (read != pPage->header.pageSegments) {
                    break;
                }

                // Read the packets
                for (u32 i = 0; i < pPage->header.pageSegments; i++) {
                    totalDataSize += pPage->segmentTable[i];
                }

                read = pFile->API->Read(pFile, 
                                        totalDataSize,
                                        pPage->packet);
                
                if (read != totalDataSize) {
                    break;
                }
                return true;
            }

            // Skip the page
            continue;
        }
    }
    return false;
}

void OggSCRC32(u8 *data, u32 size, u32 *crc) {
    for (u32 i = 0; i < size; i++) {
        *crc ^= data[i] << 24;
        for (u32 j = 0; j < 8; j++) {
            if (*crc & 0x80000000) {
                *crc = (*crc << 1) ^ 0x04c11db7;
            } else {
                *crc = (*crc << 1);
            }
        }
    }
}

bool OggSWritePageToFile(LTFile *pFile, OggsPage_t *pPage) {
    //LTUtilityByteOps *pUtilityByteOps;
    u32 totalDataSize = 0;
    u32 crc = 0;
    if (!pPage) {
        return false;
    }

    for (u32 i = 0; i < pPage->header.pageSegments; i++) {
        totalDataSize += pPage->segmentTable[i];
    }

    // calculate the checksum
    pPage->header.checksum = 0;
    crc = 0;
    OggSCRC32((u8 *)&pPage->header, sizeof(OggsPageHeader_t), &crc);
    OggSCRC32(pPage->segmentTable, pPage->header.pageSegments, &crc);
    OggSCRC32(pPage->packet, totalDataSize, &crc);
    pPage->header.checksum = crc;

    // Write the header
    if (pFile->API->Write(pFile, sizeof(OggsPageHeader_t), ((u8 *)&pPage->header)) != sizeof(OggsPageHeader_t)) {
        return false;
    }

    // Write the segment table
    if (pFile->API->Write(pFile, pPage->header.pageSegments, pPage->segmentTable) != pPage->header.pageSegments) {
        return false;
    }

    // Write the packets
    if (pFile->API->Write(pFile, totalDataSize, pPage->packet) != totalDataSize) {
        return false;
    }

    return true;
}

bool OggSInitFile(LTFile *pFile, u32 streamSerial, OpusHead_t *opusHead) {
    OggsPage_t *pPage = NULL;
    do {
        //Write the OpusHead page
        pPage = (OggsPage_t *)lt_malloc(sizeof(OggsPage_t));

        if (!pPage) break;

        OggSInitPage(pPage, streamSerial, 0, 0);
        pPage->header.flags |= kOpusHeaderFlagFirstPage; // First page

        if (!OggSSetPacketToPage(pPage, (u8 *)opusHead, kOpusHeadSize, 0)) {
            break;
        }

        if (!OggSWritePageToFile(pFile, pPage)) {
            break;
        }

        // Write the OpusTags page
        OggSInitPage(pPage, streamSerial, 1, 0);

        if (!OggSSetPacketToPage(pPage, OggsOpusTags, sizeof(OggsOpusTags), 0)) {
            break;
        }

        if (!OggSWritePageToFile(pFile, pPage)) {
            break;
        }

        lt_free(pPage);
        return true;
    } while (0);

    lt_free(pPage);
    return false;
}
